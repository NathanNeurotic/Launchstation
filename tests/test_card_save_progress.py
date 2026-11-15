"""Run the real physical-card import writer on short-I/O host files."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_card_tool.c').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mc_card_write.h"
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef int64_t s64;
#define CT_BLOCK 4096
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_O_TRUNC O_TRUNC
static int fileMode=0700, handles, closes, fail_close, fail_write, fail_meta, metas, completions, freec=100;
static u64 total_seen, done_seen;
static int genOpen(const char *p,int f){int fd=open(p,f,0600);if(fd>=0)handles++;return fd;}
static int genClose(int fd){assert(handles==1);handles--;int r=close(fd);return ++closes==fail_close?-1:r;}
static int genWrite(int fd,void *p,int n){if(fail_write)return -1;return write(fd,p,n>17?17:n);}
static int genRead(int fd,void *p,int n){return read(fd,p,n>13?13:n);}
static s64 genLseek(int fd,s64 o,int w){return lseek(fd,o,w);}
static int genRemove(char *p){return unlink(p);}
static int genRmdir(char *p){return rmdir(p);}
static int genMkdir(char *p,int m){return mkdir(p,m);}
static int genDopen(const char *p){(void)p;return -1;}
static int genDclose(int fd){(void)fd;assert(0);return -1;}
static int wleExists(const char *p){return !access(p,F_OK);}
static int filerMcFreeClusters(int p){assert(p==0);return freec;}
int mcCardNameOk(const uint8_t *p){return p[0]&&memchr(p,0,32)!=NULL;}
uint32_t mcCardSaveClusters(const McSaveData *s){(void)s;return 8;}
static int filerMcSetEntry(const char *p,const uint8_t *e){(void)p;(void)e;return ++metas==fail_meta?-1:0;}
static int DashFileProgress(const char *a,const char *name,u64 remaining,u64 total){
 assert(!strcmp(a,"Importing save")&&!strcmp(name,"SAVE (2/3)"));
 assert(remaining<=total);
 if(!total_seen)total_seen=total;
 assert(total_seen==total&&total-remaining>=done_seen);
 done_seen=total-remaining;
 if(!remaining){assert(metas==4&&!handles);completions++;}
 return 1;
}
'''
start = source.index('static struct {\n\tint active;\n\tu64 done, total;')
code += source[start:source.index('static int ctWriteFile(const char *path', start)]
code += 'static char ct_mc_left[128];\n' + function('static int ctPutSaveMc(')
code += r'''
static void reset(void){
 assert(!handles);closes=fail_close=fail_write=fail_meta=metas=completions=0;
 total_seen=done_seen=0;
}
int main(void){
 assert(!mkdir("mc0:",0700));
 McSaveFile files[3]={0};McSaveData s={0};s.files=files;s.nfiles=3;
 strcpy((char *)s.entry+0x40,"SAVE");
 uint8_t first[83],second[119];memset(first,0x5a,sizeof(first));memset(second,0xa5,sizeof(second));
 for(int i=0;i<3;i++)sprintf((char *)files[i].entry+0x40,"FILE%d",i);
 files[0].data=first;files[0].size=sizeof(first);files[1].data=second;files[1].size=sizeof(second);
 for(int failure=0;failure<4;failure++){
  reset();fail_write=failure==0;fail_close=failure==1?1:failure==2?2:0;fail_meta=failure==3?4:0;
  assert(ctPutSaveMc(0,&s,2,3)==MCW_ERR_VERIFY);
  assert(!handles&&!completions&&!ct_save_progress.active&&access("mc0:/SAVE",F_OK)<0);
  assert(total_seen==405&&done_seen<total_seen);
 }
 reset();assert(ctPutSaveMc(0,&s,2,3)==MCW_OK);
 assert(completions==1&&done_seen==405&&!ct_save_progress.active);
 /* An existing folder is untouched, even if it is empty. */
 reset();assert(ctPutSaveMc(0,&s,2,3)==MCW_ERR_EXISTS&&!total_seen&&!metas);
 for(int i=0;i<3;i++){char p[64];sprintf(p,"mc0:/SAVE/FILE%d",i);assert(!unlink(p));}
 assert(!rmdir("mc0:/SAVE"));
 reset();freec=8;assert(ctPutSaveMc(0,&s,2,3)==MCW_ERR_FULL&&!total_seen);freec=100;
 reset();files[0].size=files[1].size=0;
 assert(ctPutSaveMc(0,&s,2,3)==MCW_OK&&completions==1&&total_seen==1);
 puts("PASS: stable whole-save progress, verified closes/metadata, failure cleanup, no overwrite, space and empty files");
}
'''
with tempfile.TemporaryDirectory(prefix='card-save-progress-') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(code)
    exe = path / 'test'
    cc = shutil.which('gcc') or shutil.which('cc')
    subprocess.run([cc, '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'include'),
                    str(path / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=path, check=True, timeout=15)
