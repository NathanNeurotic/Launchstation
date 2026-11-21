"""Cache identity follows USB renumbering; unowned/ambiguous folders survive."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/hdd_install_ps2.c').read_text()
helpers = s[s.index('static int pathJoin('):s.index('static int checkHash(')]
stage = s[s.index('int hddInstallClaimStage('):s.index('static int makeDirectory(')]
code = r'''
#define _POSIX_C_SOURCE 200809L
#include "hdd_install.h"
#include "save_sign.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define MAX_PATH 1025
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
typedef uint64_t u64;
static unsigned ticks;
static const char *units[10];
static u64 Timer(void) { return ++ticks; }
static int mapped(const char *p, char *out, size_t cap) {
    unsigned u; int n;
    if(strncmp(p,"usb",3) || p[3]<'0' || p[3]>'9' || p[4]!=':')return -ENOENT;
    u=p[3]-'0';if(!units[u])return -ENOENT;
    n=snprintf(out,cap,"%s%s",units[u],p+5);return n<0||n>=(int)cap?-ENAMETOOLONG:0;
}
static int genOpen(const char*p,int mode) { char path[2048]; int r=mapped(p,path,sizeof(path));return r<0?r:open(path,mode,0600); }
static int genRead(int fd,void*p,unsigned n) {if(n>3)n=3;return read(fd,p,n);}
static int genWrite(int fd,const void*p,unsigned n) {if(n>3)n=3;return write(fd,p,n);}
#define genLseek lseek
#define genClose close
static int genRemove(char*p) {char path[2048];int r=mapped(p,path,sizeof(path));return r<0?r:unlink(path);}
static int genRmdir(char*p) {char path[2048];int r=mapped(p,path,sizeof(path));return r<0?r:rmdir(path);}
'''
code += helpers + stage
code += r'''
int main(int argc,char**argv) {
    char path[2048]; HddInstall in={0},other={0}; int fd;
    assert(argc==3); units[0]=argv[1]; units[1]=argv[2];
    snprintf(path,sizeof(path),"%s/cache",argv[1]);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/cache",argv[2]);assert(!mkdir(path,0700));
    strcpy(in.stage,"usb0:/cache"); strcpy(other.stage,"usb1:/cache");
    assert(hddInstallClaimStage(&in)==0 && in.owned);
    assert(hddInstallClaimStage(&other)==0 && memcmp(in.owner,other.owner,20));
    assert(hddInstallClaimStage(&other)<0); /* refuses overwriting a marker */
    /* Markers remain valid after a refused second claim. */
    units[0]=argv[2]; units[1]=argv[1];
    assert(hddInstallFindStage(&in)==0 && !strcmp(in.stage,"usb1:/cache"));
    snprintf(path,sizeof(path),"%s/cache/OSDMBR.XLF",argv[1]);fd=open(path,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
    /* Duplicate the exact marker onto the other USB drive: fail closed. */
    snprintf(path,sizeof(path),"%s/cache/owner.bin",argv[2]);fd=open(path,O_WRONLY|O_TRUNC);assert(fd>=0);assert(write(fd,in.owner,20)==20);close(fd);
    assert(hddInstallFindStage(&in)==-EEXIST); hddInstallCleanup(&in);
    snprintf(path,sizeof(path),"%s/cache/OSDMBR.XLF",argv[1]);assert(access(path,F_OK)==0);
    snprintf(path,sizeof(path),"%s/cache/owner.bin",argv[2]);assert(!unlink(path));
    assert(hddInstallFindStage(&in)==0); hddInstallCleanup(&in); assert(!in.stage[0] && !in.owned);
    snprintf(path,sizeof(path),"%s/cache",argv[1]);assert(access(path,F_OK)<0);
    snprintf(path,sizeof(path),"%s/cache",argv[2]);assert(access(path,F_OK)==0);
    puts("PASS: short marker I/O, USB renumbering, duplicate rejection and ownership-checked cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-stage-') as directory:
    d = Path(directory); (d / 'test.c').write_text(code); (d / 'a').mkdir(); (d / 'b').mkdir()
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'include'),
                    str(d / 'test.c'), str(root / 'src/hdd_install_plan.c'), str(root / 'src/save_sign.c'),
                    '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test'), str(d / 'a'), str(d / 'b')], check=True, timeout=10)
