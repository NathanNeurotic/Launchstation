"""Internal temporary folders preserve the CA through format and check ownership."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/hdd_install_ps2.c').read_text()
helpers = s[s.index('static int pathJoin('):s.index('static int checkStageHash(')]
stage = s[s.index('int hddInstallClaimStage('):s.index('static int makeDirectory(')]
code = r'''
#define _POSIX_C_SOURCE 200809L
#include "hdd_install.h"
int hddInstallRecoveryFault(void){return 0;}
void hddInstallRecoveryObserve(int rc){(void)rc;}
int hddInstallResumeSave(HddInstall *in){(void)in;return 0;}
int hddInstallResumeRemove(HddInstall *in){(void)in;return 0;}
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
#define FIO_MT_RDWR 1
typedef uint64_t u64;
typedef int64_t s64;
static unsigned ticks;
static const char *work;
static int mounted;
static u64 Timer(void) { return ++ticks; }
static int mapped(const char *p, char *out, size_t cap) {
    const char *base,*tail=strchr(p,':');int n;
    if(!tail)return -EINVAL;
    if(!strncmp(p,"ata0:",5))base="ata";
    else if(!strncmp(p,"pfs2:",5)&&mounted)base="common";
    else return -ENOENT;
    n=snprintf(out,cap,"%s/%s%s",work,base,tail+1);return n<0||n>=(int)cap?-ENAMETOOLONG:0;
}
static int restore_open_error;
static int genOpen(const char*p,int mode) { char path[2048]; int r=mapped(p,path,sizeof(path));if(r<0)return r;if(restore_open_error && (mode & FIO_O_CREAT) && strstr(p,"owner.bin"))return -EIO;return open(path,mode,0600); }
static int genRead(int fd,void*p,unsigned n) {if(n>3)n=3;return read(fd,p,n);}
static int genWrite(int fd,const void*p,unsigned n) {if(n>3)n=3;return write(fd,p,n);}
#define genLseek lseek
#define genClose close
static int genRemove(char*p) {char path[2048];int r=mapped(p,path,sizeof(path));return r<0?r:unlink(path);}
typedef struct stat iox_stat_t;
static int fileXioGetStat(const char*p, iox_stat_t *st) {
    char path[2048]; int r=mapped(p,path,sizeof(path)); if(r<0)return r;
    return stat(path,st)<0?-errno:0;
}
static int genRmdir(char*p) {char path[2048];int r=mapped(p,path,sizeof(path));return r<0?r:rmdir(path);}
static int genMkdir(const char*p,int mode) {char path[2048];int r=mapped(p,path,sizeof(path));return r<0?r:mkdir(path,mode)<0?-errno:0;}
static int fileXioMount(const char*m,const char*p,int flags){assert(!strcmp(m,"pfs2:")&&!strcmp(p,"hdd0:__common")&&flags==FIO_MT_RDWR&&!mounted);mounted=1;return 0;}
static int fileXioUmount(const char*m){assert(!strcmp(m,"pfs2:")&&mounted);mounted=0;return 0;}
'''
code += r'''

void hddInstallLog(const char *format, ...) { (void)format; }
int hddInstallLogEnabled(void) { return 0; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
void hddInstallStatus(const char *s,int done,int total) { (void)s;(void)done;(void)total; }
'''
a=s.index('static void launcherStageName('); b=s.index('static int findLauncher(',a)
code += helpers + s[a:b] + stage
code += r'''
int main(int argc,char**argv) {
    char path[2048]; HddInstall in={0},other={0}; int fd;
    assert(argc==2);work=argv[1];
    snprintf(path,sizeof(path),"%s/ata/old-ca.pem",work);fd=open(path,O_CREAT|O_WRONLY,0600);assert(fd>=0);assert(write(fd,"CA fixture",10)==10);close(fd);
    strcpy(in.ca_path,"ata0:/old-ca.pem");assert(hddInstallLoadCa(&in)==0&&in.ca_size==11);
    assert(!unlink(path)); /* source disappears during formatting */
    in.jail=1;strcpy(in.stage,"ata0:/PSBBN_DP_TEMP");
    assert(hddInstallMountStage(&in)==0&&!mounted);
    assert(hddInstallCreateStage(&in)==0&&in.owned&&in.ca_data);
    assert(!strcmp((const char *)in.ca_data,"CA fixture"));
    assert(hddInstallFindStage(&in)==0);
    in.launcher_files=calloc(1,sizeof(*in.launcher_files));assert(in.launcher_files);in.launcher_count=1;
    fd=genOpen("ata0:/PSBBN_DP_TEMP/launcher-000.bin",FIO_O_CREAT|FIO_O_EXCL|FIO_O_WRONLY);assert(fd>=0);close(fd);
    other.jail=1;strcpy(other.stage,in.stage);other.ca_data=malloc(1);other.ca_data[0]=0;other.ca_size=1;
    assert(hddInstallCreateStage(&other)==-EEXIST&&!other.owned);
    assert(hddInstallCleanup(&other)==-ENOENT&&hddInstallFindStage(&in)==0);hddInstallRelease(&other);
    snprintf(path,sizeof(path),"%s/ata/PSBBN_DP_TEMP/OSDMBR.XLF",work);fd=open(path,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
    assert(!hddInstallCleanup(&in));hddInstallRelease(&in);assert(!in.owned&&!in.stage[0]);assert(access(path,F_OK)<0);
    strcpy(other.stage,"pfs2:/PSBBN_DP_TEMP");other.jail=0;other.ca_data=malloc(2);memcpy(other.ca_data,"CA",2);other.ca_size=2;
    assert(hddInstallMountStage(&other)==0&&mounted);
    assert(hddInstallCreateStage(&other)==0&&other.owned);
    assert(hddInstallUnmountStage(&other)==0&&!mounted);
    assert(hddInstallMountStage(&other)==0);hddInstallCleanup(&other);assert(!other.owned);assert(hddInstallUnmountStage(&other)==0);hddInstallRelease(&other);
    puts("PASS: create ATA/PFS temp folders, preserve CA before erasure, short I/O, refuse existing folders and ownership-checked cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-stage-') as directory:
    d = Path(directory); (d / 'test.c').write_text(code); (d / 'ata').mkdir(); (d / 'common').mkdir()
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'include'),
                    str(d / 'test.c'), str(root / 'src/hdd_install_plan.c'), str(root / 'src/save_sign.c'),
                    '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test'), str(d)], check=True, timeout=10)
