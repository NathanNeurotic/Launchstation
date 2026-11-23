"""Run the real PSBBN extraction flow with gzip archives and disk/FS facades."""
from pathlib import Path
import gzip
import io
import re
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/hdd_install_psbbn.c').read_text()


def function(signature):
    a = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}':
            return source[a:a + token.end()] + '\n'
    raise AssertionError(signature)


code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "hdd_install.h"
#include "install_ext2.h"
#define MAX_PATH 1025
#define FIO_O_EXCL O_EXCL
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define FIO_MT_RDWR 1
#define FIO_S_ISDIR S_ISDIR
typedef struct {unsigned mode;} iox_stat_t;
typedef int64_t s64;
static const char *work;
static unsigned mounted,unmounted,created,finished,config_writes,entries,archive_opens;
static HddInstall settings;
struct InstallExt2 {int unused;};
int installExt2Create(InstallExt2 **out,const HddFormatIo *io,uint32_t a,uint32_t b,uint32_t serial){
 (void)a;(void)b;(void)serial;assert(io);*out=calloc(1,sizeof(**out));assert(*out);created++;return 0;
}
int installExt2Begin(InstallExt2 *fs,const InstallTarEntry *e){assert(fs&&e);entries++;return 0;}
int installExt2Data(InstallExt2 *fs,const void *p,unsigned n){assert(fs&&p&&n);return 0;}
int installExt2End(InstallExt2 *fs){assert(fs);return 0;}
int installExt2Finish(InstallExt2 *fs){assert(fs);finished++;return 0;}
void installExt2Free(InstallExt2 *fs){free(fs);}
int installPsbbnSwap(const HddFormatIo *io,uint32_t a,uint32_t b){(void)io;(void)a;(void)b;return 0;}
int installPsbbnMusic(const HddFormatIo *io,uint32_t a,uint32_t b,uint32_t c){(void)io;(void)a;(void)b;(void)c;return 0;}
int hddInstallResumeSave(HddInstall *in){assert(in);return 0;}
void hddInstallLog(const char *s,...){assert(s);}
void hddInstallLogFlush(HddInstall *in){assert(in);}
void hddInstallStatus(const char *s,int a,int b){assert(s&&a>=0&&b>=a);}
void hddInstallProgress(const char *s,int a,int b){assert(s&&a>=0&&b>=a);}
void hddInstallOverall(uint64_t a,uint64_t b){assert(b&&a<=b);}
int hddInstallWriteConfigs(HddInstall *in){assert(in);config_writes++;return 0;}
static const char *translate(const char *p){
 static char path[2048];
 if(!strncmp(p,"pfs",3)){assert(p[3]>='0'&&p[3]<='2'&&p[4]==':');snprintf(path,sizeof(path),"%s/pfs%c%s",work,p[3],p+5);return path;}
 return p;
}
static int genOpen(const char *p,int flags){
 if(strstr(p,".tar.gz")){assert(flags==O_RDONLY);archive_opens++;}
 int fd=open(translate(p),flags,0600);return fd<0?-errno:fd;
}
static int genRead(int fd,void *p,unsigned n){int r=read(fd,p,n);return r<0?-errno:r;}
static int genWrite(int fd,void *p,unsigned n){int r=write(fd,p,n);return r<0?-errno:r;}
static int64_t genLseek(int fd,int offset,int whence){return lseek(fd,offset,whence);}
static int genClose(int fd){return close(fd);}
static int genMkdir(const char *p,int mode){return mkdir(translate(p),mode)<0?-errno:0;}
static int fileXioGetStat(const char *p,iox_stat_t *out){struct stat st;if(stat(translate(p),&st)<0)return -errno;out->mode=st.st_mode;return 0;}
static int fileXioMount(const char *p,const char *partition,int mode){assert(p&&partition&&mode==1);mounted++;return 0;}
static int fileXioUmount(const char *p){assert(p);unmounted++;return 0;}
'''
a = source.index('static const char *const linux_names')
code += source[a:source.index('static uint32_t le32(', a)] + function('static uint32_t le32(')
a = source.index('typedef struct {\n    HddInstall *in;')
code += source[a:source.index('} Payload;', a) + len('} Payload;')]
for sig in ('static int routePath(', 'static int mkdirParents(', 'static int begin(', 'static int data(',
            'static int end(', 'static int readArchive(', 'static int archives(', 'int hddInstallPsbbnPayload('):
    code += function(sig)
code += r'''
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static int rawRead(void *ctx,uint32_t lba,unsigned n,void *p){
 HddInstall *in=ctx;unsigned char *bytes=p;assert(n==2);memset(p,0,1024);
 unsigned i=0;while(i<10&&in->linux[i].start!=lba)i++;assert(i<10);
 put32(bytes+4,0x00415041);strcpy((char*)bytes+16,linux_names[i]);
 put32(bytes+64,lba);put32(bytes+68,in->linux[i].sectors);put32(bytes+72,i==1?0x82:0x83);
 uint32_t sum=0;for(unsigned w=1;w<256;w++)sum+=le32(bytes+w*4);put32(bytes,sum);return 0;
}
static int rawFlush(void *ctx){assert(ctx);return 0;}
int main(int argc,char **argv){
 assert(argc==3);work=argv[1];int scenario=atoi(argv[2]),linux=scenario>=5;
 settings.flags=7|(linux?HDD_INSTALL_PS2LINUX:0);settings.jail=1;strcpy(settings.stage,work);
 for(unsigned i=0;i<10;i++){settings.linux[i].start=2048+i*10000000;settings.linux[i].sectors=hddInstallPartitionMb(&settings,i)*2048;}
 const unsigned assets[]={HI_SYSTEM,HI_LANGUAGE,HI_PS2LINUX};
 for(unsigned i=0;i<(linux?3u:2u);i++){
  char path[2048];struct stat st;snprintf(path,sizeof(path),"%s/%s",work,hddInstallAssetName(assets[i]));assert(!stat(path,&st));settings.sizes[assets[i]]=st.st_size;
 }
 HddFormatIo io={.context=&settings,.read=rawRead,.flush=rawFlush};
 int ret=hddInstallPsbbnPayload(&settings,&io);
 if(scenario==0||scenario==5){assert(!ret&&finished==created&&config_writes==1&&archive_opens==(linux?3u:2u));}
 else{assert(ret<0&&!finished&&!config_writes&&strstr(settings.error,"Boot MBR was not activated"));}
 assert(mounted==3&&unmounted==mounted&&entries>0);
 puts("PASS: single-pass extraction checks CRCs, required boot files, protected paths and capacity before clean/boot commit");
}
'''


def archive(path, entries):
    payload = io.BytesIO()
    with tarfile.open(fileobj=payload, mode='w') as t:
        for name, content in entries:
            e = tarfile.TarInfo(name)
            e.mode = 0o644
            e.size = len(content)
            t.addfile(e, io.BytesIO(content))
    path.write_bytes(gzip.compress(payload.getvalue(), mtime=0))


with tempfile.TemporaryDirectory(prefix='hdd-installer-streaming-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-I', str(root / 'include'), str(d / 'test.c'), str(root / 'src/hdd_install_plan.c'), str(root / 'src/hdd_install_recovery.c'),
                    str(root / 'src/install_tar.c'), '-lz', '-o', str(d / 'test')], check=True)
    required = ['__system/p2lboot/osdboot.elf', '__system/p2lboot/vmlinux', '__linux.1/sbin/init',
                '__linux.1/bin/sh', '__linux.1/etc/fstab', '__linux.4/bn/script/utility/sysconf.xml']
    required += ['__system/osd100/' + name for name in ('OSDSYS_A.XLF', 'FNTOSD', 'ICOIMAGE', 'JISUCS', 'SKBIMAGE', 'SNDIMAGE', 'TEXIMAGE')]
    for scenario in range(7):
        work = d / str(scenario)
        work.mkdir()
        for i in range(3): (work / f'pfs{i}').mkdir()
        names = required[:-1] if scenario == 1 else required
        entries = [(name, b'fixture') for name in names]
        if scenario == 3: entries.append(('__system/osdmenu/hosdmenu.elf', b'protected'))
        if scenario == 4: entries.append(('__system/oversized', bytes(33 * 1024 * 1024)))
        archive(work / 'system.tar.gz', entries)
        archive(work / 'language.tar.gz', [('__linux.4/translation', b'localized')])
        if scenario == 2:
            p = work / 'language.tar.gz'
            corrupted = bytearray(p.read_bytes()); corrupted[-8] ^= 1; p.write_bytes(corrupted)
        if scenario >= 5:
            archive(work / 'PS2Linux.tar.gz', [('sbin/init', b'init')] + ([] if scenario == 6 else [('bin/sh', b'shell')]))
        subprocess.run([str(d / 'test'), str(work), str(scenario)], check=True)
