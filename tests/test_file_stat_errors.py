# Adapted from saildot4k/Launchstation at 1057feb4a3870d2bc10d771c72ff0cfa3d20be73.
"""Run production stat wrappers against backend-specific error contracts.

BDM contract: ps2dev/ps2sdk commit 5d64b0eb2ad5248eb863c503dcf07fddd8e6c8b7,
iop/fs/bdmfs_fatfs/src/fs_driver.c, fs_getstat: failed f_stat returns -FRESULT.
Test both EXFAT/BDM and legacy builds; other backends keep their native errors.
An optional fileops source path provides an executable negative control.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'src/filer_fileops.c').read_text()


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
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define ETH 1
#define MAX_PATH 1024
typedef struct { unsigned mode,size; } iox_stat_t;
static int status,rpcs,mc_access,cf_online=1;
static char received[MAX_PATH];
static int isPcCardCfPath(const char *p) { return p && !strncmp(p,"cf",2); }
static int isPcCardCfOnline(void) { return cf_online; }
static void ensureMemoryCardPortAccessible(int port) { assert(port==1);mc_access++; }
static char *makeHostPath(char *out,char *in) { if(out!=in)strcpy(out,in);return out; }
static void genLimObjName(char *path,int reserve) { (void)path;assert(!reserve); }
static int fileXioGetStat(const char *path,iox_stat_t *stat) {
    rpcs++;snprintf(received,sizeof(received),"%s",path);
    if(status>=0) { stat->mode=0x1234;stat->size=65537; }
    return status;
}
'''
for signature in ('static void ensurePathMemoryCardAccessible(',
                  'static int ensurePathPcCardCfOnline(', 'static void normalizePcCardCfPath('):
    code += function(signature)
if 'static int fatfsStatResult(' in source:
    code += '#ifdef EXFAT\n' + function('static int fatfsStatResult(') + '#endif\n'
code += function('int genGetStat(')
code += r'''
static int check(const char *path,int reply) {
    iox_stat_t stat={0xa5a5,0x5a5a};status=reply;int before=rpcs;
    int result=genGetStat(path,&stat);assert(rpcs==before+1);
    if(reply<0)assert(stat.mode==0xa5a5 && stat.size==0x5a5a);
    else assert(stat.mode==0x1234 && stat.size==65537);
    return result;
}
int main(void) {
    const char *bdm[]={"usb:/NEW.ELF","usb0:/APP/NEW.ELF","usb1:/NEW.ELF",
        "mass:/NEW.ELF","mass0:/NEW.ELF","mass9:/NEW.ELF",
        "ata:/NEW.ELF","ata0:/NEW.ELF","ata1:/NEW.ELF",
        "mx4sio:/NEW.ELF","mx4sio0:/NEW.ELF","cf0:/NEW.ELF","ilink:/NEW.ELF","ilink0:/NEW.ELF"};
    for(unsigned i=0;i<sizeof(bdm)/sizeof(*bdm);i++) {
#ifdef EXFAT
        /* The reported missing USB file must be usable as a new target. */
        assert(check(bdm[i],-4)==-ENOENT); /* FR_NO_FILE */
        assert(check(bdm[i],-5)==-ENOENT); /* FR_NO_PATH */
        /* Numeric collisions with POSIX must never permit a false miss. */
        assert(check(bdm[i],-2)==-EIO);    /* FR_INT_ERR, not ENOENT */
        assert(check(bdm[i],-1)==-EIO);    /* FR_DISK_ERR */
        assert(check(bdm[i],-3)==-ENODEV); /* FR_NOT_READY */
        assert(check(bdm[i],-6)==-ENXIO);  /* native unresolved volume / invalid name */
        assert(check(bdm[i],-7)==-EACCES); /* FR_DENIED */
        assert(check(bdm[i],-10)==-EROFS); /* FR_WRITE_PROTECTED */
        assert(check(bdm[i],-15)==-ETIMEDOUT);
        for(int failure=1;failure<=19;failure++)
            assert((check(bdm[i],-failure)==-ENOENT)==(failure==4 || failure==5));
#else
        /* Legacy USBMASS does not speak the BDM FatFs result convention. */
        assert(check(bdm[i],-EINTR)==-EINTR);
        assert(check(bdm[i],-ENOENT)==-ENOENT);
        assert(check(bdm[i],-EIO)==-EIO);
#endif
        assert(check(bdm[i],0)==0 && !strcmp(received,bdm[i]));
        assert(check(bdm[i],-1000)==-1000);
        assert(check(bdm[i],INT_MIN)==INT_MIN);
    }
    const char *native[]={"mc0:/NEW.ELF","mc1:/NEW.ELF","vmc0:/NEW.ELF",
        "vmc1:/NEW.ELF","pfs0:/NEW.ELF","hdd0:/__common/NEW.ELF",
        "dvr_pfs0:/NEW.ELF","mmce0:/NEW.ELF","mmce1:/NEW.ELF",
        "host:/NEW.ELF","udpfs:/NEW.ELF","cdfs:/NEW.ELF",
        "xfrom0:/NEW.ELF","usb-backup:/NEW.ELF","massive:/NEW.ELF",
        "atafile:/NEW.ELF","mx4siofile:/NEW.ELF","folder/usb0:/NEW.ELF"};
    for(unsigned i=0;i<sizeof(native)/sizeof(*native);i++) {
        assert(check(native[i],-ENOENT)==-ENOENT);
        assert(check(native[i],-EINTR)==-EINTR);
        assert(check(native[i],-EIO)==-EIO);
        assert(check(native[i],-ETIMEDOUT)==-ETIMEDOUT);
        assert(check(native[i],0)==0);
    }
    assert(mc_access==5); /* MC1 gate still precedes each stat call. */
    assert(check("cf:/NEW.ELF",0)==0 && !strcmp(received,"cf0:/NEW.ELF"));
    iox_stat_t stat;int before=rpcs;
    cf_online=0;assert(genGetStat("cf0:/NEW.ELF",&stat)==-ENODEV && rpcs==before);
    assert(genGetStat(NULL,&stat)<0 && genGetStat("",&stat)<0 && genGetStat("usb0:/x",NULL)<0);
    assert(rpcs==before);
    puts("PASS: BDM FatFs stat errors, aliases and real failures; native/legacy errors, metadata and device gates retained");
}
'''
cc = shutil.which('gcc') or shutil.which('clang')
assert cc, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='file-stat-errors-') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(code)
    for flags in ([], ['-DEXFAT']):
        exe = path / ('bdm' if flags else 'legacy')
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', *flags,
                        str(path / 'test.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
