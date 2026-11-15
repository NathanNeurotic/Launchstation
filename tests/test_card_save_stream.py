"""Production card exports with one descriptor, bounded allocations and fault injection."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import mc_fixture as fx

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'src/dash_card_tool.c').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include "mc_save_export.h"
#include "mc_sha256.h"
#define MAX_PATH 1024
#define CT_WAYS 4
#define CT_BLOCK 4096
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_RDWR O_RDWR
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_O_TRUNC O_TRUNC
#define stricmp strcasecmp
#define DASH_TOAST_WARN 1
#define CT_EXPORT_PUBLISH_FAILED (-101)
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef int64_t s64;
static int handles, read_failure, write_failure, close_failure, corrupt_read, fail_alloc, cap;
static int current_output, reading_output, completions, worker_steps, rename_failure, rollback_failure;
static u64 progress_done, progress_total;
static size_t largest;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
static int allowed(size_t n) { if(cap && n>largest) largest=n;return !cap || (!fail_alloc && n<=131072); }
void *__wrap_malloc(size_t n) { return allowed(n)?__real_malloc(n):NULL; }
void *__wrap_calloc(size_t n,size_t s) { return allowed(n*s)?__real_calloc(n,s):NULL; }
void *__wrap_realloc(void *p,size_t n) { return allowed(n)?__real_realloc(p,n):NULL; }
static void *bounded_align(size_t a,size_t n) { return allowed(n)?memalign(a,n):NULL; }
#define memalign bounded_align
static int genOpen(const char *p,int flags) {
    assert(!handles);int fd=open(p,flags,0600);
    if(fd>=0){handles++;current_output=strstr(p,"out.")!=NULL;reading_output=current_output && !(flags&O_WRONLY);}
    return fd;
}
static s64 genLseek(int fd,s64 off,int whence) { return lseek(fd,off,whence); }
static int genRead(int fd,void *p,int n) {
    if(!current_output && read_failure && lseek(fd,0,SEEK_CUR)==44*1024) return -1;
    if(n>31)n=31;
    int r=read(fd,p,n);
    if(r>0 && reading_output && corrupt_read) { ((u8 *)p)[0]^=1;corrupt_read=0; }
    return r;
}
static int genWrite(int fd,const void *p,int n) { if(write_failure)return -1;if(n>37)n=37;return write(fd,p,n); }
static int genClose(int fd) { assert(handles==1);handles--;int r=close(fd);if(current_output && close_failure){close_failure=0;return -1;}return r; }
static int genRemove(char *p) { return unlink(p); }
static int wleExists(const char *p) { return access(p,F_OK)==0; }
static int genRenameNoReplace(const char *from,const char *to) {
    if(rollback_failure && strstr(from,".BAK")) return -1;
    if(rename_failure>0 && !--rename_failure) return -1;
    return wleExists(to)?-1:rename(from,to);
}
static void dashToast(int kind,const char *message) { assert(kind==DASH_TOAST_WARN && message[0]); }
static int genFixPath(const char *p,char *out) { snprintf(out,MAX_PATH,"%s",p);return 0; }
static int DashFileTransferChunk(void (*run)(void *),void *arg) { worker_steps++;run(arg);return 1; }
static int DashFileProgress(const char *action,const char *name,u64 remaining,u64 total) {
    assert(!strcmp(action,"Exporting save") && !strcmp(name,"BASLUS-12345DATA"));
    assert(remaining<=total);
    if(!progress_total) progress_total=total;
    assert(total==progress_total && total-remaining>=progress_done);
    progress_done=total-remaining;if(!remaining)completions++;return 1;
}
'''
start = source.index('typedef struct {\n\tint fd;')
code += source[start:source.index('static McViewSave ct_saves', start)]
for sig in ['static int ctRead(', 'static int ctOpen(', 'static int ctClose(']:
    code += function(sig)
start = source.index('typedef struct CtSaveStream')
code += source[start:source.index('static const char *const ct_save_as', start)]
code += r'''
static int ctFingerprintImage(const char *image,const char *name,u8 digest[32]) {
    CardIo input;McSaveData save;
    int r=ctOpen(&input,image,0);if(r)return r;
    r=mcCardReadSaveMetadata(&ct_view,name,&save);
    if(!r)r=ctFingerprint(&save,1,digest);
    int c=ctClose(&input);mcCardFreeSave(&save);return r?r:c;
}
static void disk_digest(const char *path,u8 out[32]) {
    int fd=open(path,O_RDONLY),n;u8 data[4096];McSha256 hash;
    assert(fd>=0);mcSha256Init(&hash);
    while((n=read(fd,data,sizeof(data)))>0) mcSha256Update(&hash,data,n);
    assert(!n && !close(fd));mcSha256Final(&hash,out);
}
static void reset(void) {
    assert(!handles);progress_total=progress_done=0;completions=worker_steps=0;
    read_failure=write_failure=close_failure=corrupt_read=fail_alloc=rename_failure=rollback_failure=0;largest=0;
}
int main(int argc,char **argv) {
    assert(argc==3 && !chdir(argv[2]));
    McSaveFile duplicate_files[2] = {0};
    McSaveData duplicate_save = {0};
    McSaveStreamIo measure = {0};
    uint32_t measured;
    memcpy(duplicate_save.entry+0x40,"SAVE",5);
    duplicate_save.nfiles=2;duplicate_save.files=duplicate_files;
    memcpy(duplicate_files[0].entry+0x40,"DATA",5);
    memcpy(duplicate_files[1].entry+0x40,"DATA",5);
    duplicate_files[1].entry[0x45]=0x7f;
    for(int fmt=0;fmt<2;fmt++)
        assert(mcSaveExportStream(&duplicate_save,fmt,&measure,&measured)==MCW_ERR_INVALID);
    for(int fmt=0;fmt<2;fmt++) {
        reset();cap=1;
        assert(!ctExportStream(argv[1],"BASLUS-12345DATA",fmt,fmt?"out.psv":"out.psu",1));
        assert(!handles && completions==1 && worker_steps>20 && largest<=131072);
    }
    for(int failure=0;failure<5;failure++) {
        reset();unlink("out.failed");
        write_failure=failure==0;close_failure=failure==1;corrupt_read=failure==2;
        read_failure=failure==3;fail_alloc=failure==4;
        assert(ctExportStream(argv[1],"BASLUS-12345DATA",1,"out.failed",1)<0);
        assert(!handles && !completions && access("out.failed",F_OK)<0);
    }
    reset();assert(ctExportStream(argv[1],"BASLUS-12345DATA",0,argv[1],0)==MCW_ERR_INVALID);
    reset();assert(ctExportStream(argv[1],"BASLUS-12345DATA",0,"out.psu",1)<0);
    assert(!completions && !handles);
    u8 previous[32], retained[32];disk_digest("out.psu",previous);
    for(int failure=0;failure<5;failure++) {
        reset();write_failure=failure==0;close_failure=failure==1;corrupt_read=failure==2;
        read_failure=failure==3;fail_alloc=failure==4;
        assert(ctExportStream(argv[1],"BASLUS-12345DATA",0,"out.psu",0)<0);
        assert(!handles && !completions && !wleExists("out.psu.PART"));
        disk_digest("out.psu",retained);assert(!memcmp(previous,retained,32));
    }
    reset();
    assert(!ctExportStream(argv[1],"BASLUS-12345DATA",0,"out.psu",0));
    assert(!handles && completions==1 && !wleExists("out.psu.PART") && !wleExists("out.psu.BAK"));
    for(int failure=1;failure<=2;failure++) {
        reset();rename_failure=failure;
        assert(ctExportStream(argv[1],"BASLUS-12345DATA",0,"out.psu",0)==CT_EXPORT_PUBLISH_FAILED);
        assert(!handles && !completions && wleExists("out.psu") && wleExists("out.psu.PART") && !wleExists("out.psu.BAK"));
        assert(!unlink("out.psu.PART"));
    }
    reset();rename_failure=2;rollback_failure=1;
    assert(ctExportStream(argv[1],"BASLUS-12345DATA",0,"out.psu",0)==CT_EXPORT_PUBLISH_FAILED);
    assert(!handles && !completions && !wleExists("out.psu") && wleExists("out.psu.PART"));
    disk_digest("out.psu.BAK",retained);assert(!memcmp(previous,retained,32));
    assert(!rename("out.psu.BAK","out.psu") && !unlink("out.psu.PART"));
    for(int suffix=0;suffix<2;suffix++) {
        reset();const char *recovery=suffix?"out.psu.BAK":"out.psu.PART";
        int fd=open(recovery,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);
        assert(write(fd,"KEEP",4)==4 && !close(fd));
        u8 recovery_before[32], recovery_after[32];disk_digest(recovery,recovery_before);
        assert(ctExportStream(argv[1],"BASLUS-12345DATA",0,"out.psu",0)==CT_EXPORT_PUBLISH_FAILED);
        disk_digest(recovery,recovery_after);assert(!memcmp(recovery_before,recovery_after,32));
        disk_digest("out.psu",retained);assert(!memcmp(previous,retained,32));
        assert(!unlink(recovery));
    }
    reset();
    u8 first[32], next[32];
    assert(!ctFingerprintImage(argv[1],"BASLUS-12345DATA",first));
    assert(!ctFingerprintImage(argv[1],"BASLUS-12345DATA",next) && !memcmp(first,next,32));
    McSaveData memory;CardIo input;
    cap=0;assert(!ctOpen(&input,argv[1],0));
    assert(!mcCardReadSave(&ct_view,"BASLUS-12345DATA",&memory) && !ctClose(&input));
    cap=1;assert(!ctFingerprintMemory(&memory,next) && !memcmp(first,next,32));mcCardFreeSave(&memory);
    read_failure=1;assert(ctFingerprintImage(argv[1],"BASLUS-12345DATA",next)<0);read_failure=0;
    /* A same-size source rewrite must invalidate a Cut without taking a
     * second in-memory snapshot of the save. Restore it for Python's check. */
    int fd=open(argv[1],O_RDWR);assert(fd>=0);
    unsigned char old,changed;
    assert(pread(fd,&old,1,42*1024)==1);changed=old^1;
    assert(pwrite(fd,&changed,1,42*1024)==1 && !close(fd));
    assert(!ctFingerprintImage(argv[1],"BASLUS-12345DATA",next) && memcmp(first,next,32));
    fd=open(argv[1],O_RDWR);assert(fd>=0 && pwrite(fd,&old,1,42*1024)==1 && !close(fd));
    puts("PASS: production streamed PSU/PSV, one descriptor, 128 KiB allocation cap, short IO, duplicate-name rejection, source retention, failed read/write/close/allocation/verification, staged replacement, rename/rollback failures, recovery-file preservation, exclusive creation and source alias refusal");
}
'''
with tempfile.TemporaryDirectory(prefix='card-stream-') as folder:
    folder = Path(folder)
    card = fx.Card(mb=8)
    payload = bytes(range(256)) * (5 * 1024 * 4)
    card.add_save('BASLUS-12345DATA', {'DATA': payload, 'EMPTY': b''})
    image = card.finish().image()
    (folder/'source.mcd').write_bytes(image)
    (folder/'test.c').write_text(code)
    exe = folder/'test'
    subprocess.run(['gcc','-std=c99','-g','-O1','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc',
                    '-I',str(ROOT/'include'),str(folder/'test.c'),
                    *[str(ROOT/'src'/name) for name in ['mc_save_export.c','mc_card_write.c','mc_card_view.c','mc_sha256.c','mc_ps1_card.c']],
                    '-lmbedcrypto','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(folder/'source.mcd'),str(folder)],check=True,timeout=90)
    assert (folder/'source.mcd').read_bytes() == image
    psu = (folder/'out.psu').read_bytes()
    assert psu[2048:2048+len(payload)] == payload
    psv = (folder/'out.psv').read_bytes()
    offset = struct.unpack_from('<I',psv,160+56)[0]
    assert psv[offset:offset+len(payload)] == payload
