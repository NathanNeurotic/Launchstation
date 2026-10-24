"""Exercise the real genGetStat boundary with SDK FatFs result codes.

The SDK bdmfs_fatfs driver returns -FRESULT, not -errno. Keep that
translation at the driver boundary so copy's absence checks are portable.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/filer_fileops.c").read_text()


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1024
#define FIO_O_RDONLY 1
#define FIO_S_IFDIR 0x2000
#define FIO_S_IFREG 0x1000
#define SEEK_END 2
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { int mode; unsigned int size; unsigned int hisize; } iox_stat_t;
static int driver_result, dopen_result = -ENOENT, open_result = -ENOENT;
static s64 lseek_result = 0;
static int ensurePathPcCardCfOnline(const char *p) {(void)p;return 1;}
static void ensurePathMemoryCardAccessible(const char *p) {(void)p;}
static void normalizePcCardCfPath(char *p, unsigned long n) {(void)p;(void)n;}
static void genLimObjName(char *p,int n) {(void)p;(void)n;}
static int fileXioGetStat(const char *p,iox_stat_t *s) {(void)p;(void)s;return driver_result;}
static int fileXioDopen(const char *p) {(void)p;return dopen_result;}
static int fileXioDclose(int fd) {(void)fd;return 0;}
static int fileXioOpen(const char *p,int flags,int mode) {(void)p;(void)flags;(void)mode;return open_result;}
static int fileXioLseek(int fd,int pos,int how) {(void)fd;(void)pos;(void)how;return (int)lseek_result;}
static int fileXioClose(int fd) {(void)fd;return 0;}
"""
if "static int isMcmanStatPath(" in source:
    code += function("static int isMcmanStatPath(")
if "static int probeMcmanStat(" in source:
    code += function("static int probeMcmanStat(")
if "static int normalizeFatFsStatResult(" in source:
    code += function("static int normalizeFatFsStatResult(")
code += function("int genGetStat(")
code += r"""
static int stat_result(const char *p,int raw) {
 iox_stat_t st;st.mode=0x2000;driver_result=raw;
 dopen_result=-ENOENT;open_result=raw;lseek_result=1234;
 int r=genGetStat(p,&st);
 if (!isMcmanStatPath(p) || raw < 0) assert(st.mode==0);
 return r;
}
int main(void) {
 const char *fatfs[]={"usb0:/NEW.ELF","mass3:/NEW.ELF","mx4sio0:/NEW.ELF",
                      "ata0:/NEW.ELF","ilink0:/NEW.ELF","cf0:/NEW.ELF","mass:/NEW.ELF"};
 for (unsigned i=0;i<sizeof(fatfs)/sizeof(*fatfs);i++) {
#ifdef EXFAT
  assert(stat_result(fatfs[i],-4)==-ENOENT); /* FR_NO_FILE */
  assert(stat_result(fatfs[i],-5)==-ENOENT); /* FR_NO_PATH */
  assert(stat_result(fatfs[i],-2)==-EIO); /* FR_INT_ERR is NOT ENOENT */
  assert(stat_result(fatfs[i],-1)==-EIO);
  assert(stat_result(fatfs[i],-3)==-ENODEV);
  assert(stat_result(fatfs[i],-6)==-ENXIO); /* driver also returns -ENXIO directly */
  assert(stat_result(fatfs[i],-7)==-EACCES);
  assert(stat_result(fatfs[i],-8)==-EEXIST);
  assert(stat_result(fatfs[i],-10)==-EROFS);
  assert(stat_result(fatfs[i],-15)==-ETIMEDOUT);
#else
  assert(stat_result(fatfs[i],-4)==-4);
  assert(stat_result(fatfs[i],-2)==-2);
#endif
  assert(stat_result(fatfs[i],0)==0);
  assert(stat_result(fatfs[i],-ENXIO)==-ENXIO);
 }
 const char *other[]={"vmc0:/NEW.ELF","pfs0:/NEW.ELF","host:/NEW.ELF",
                      "udpfs:/NEW.ELF","mmce0:/NEW.ELF","usbgarbage:/NEW.ELF","mass3x:/NEW.ELF"};
 for (unsigned i=0;i<sizeof(other)/sizeof(*other);i++) {
  assert(stat_result(other[i],-ENOENT)==-ENOENT);
  assert(stat_result(other[i],-EINTR)==-EINTR);
  assert(stat_result(other[i],-EACCES)==-EACCES);
 }
 const char *mc[]={"mc0:/NEW.ELF","mc1:/NEW.ELF","xfrom:/NEW.ELF","xfrom0:/NEW.ELF"};
 for (unsigned i=0;i<sizeof(mc)/sizeof(*mc);i++) {
  iox_stat_t st;
  /* Missing file when mcman's getstat would falsely return 0 with directory mode. */
  driver_result=0;dopen_result=-4;open_result=-4;
  assert(genGetStat(mc[i],&st)==-ENOENT && st.mode==0);
  driver_result=0;dopen_result=-ENOENT;open_result=-ENOENT;
  assert(genGetStat(mc[i],&st)==-ENOENT && st.mode==0);
  driver_result=0;dopen_result=-ENOENT;open_result=-EACCES;
  assert(genGetStat(mc[i],&st)==-EACCES && st.mode==0);
  /* Existing regular file on mc0/mc1/xfrom. */
  driver_result=0;dopen_result=-4;open_result=7;lseek_result=1566464;
  assert(genGetStat(mc[i],&st)==0 && st.mode==FIO_S_IFREG && st.size==1566464);
  /* Existing directory on mc0/mc1/xfrom. */
  driver_result=0;dopen_result=3;open_result=-4;
  assert(genGetStat(mc[i],&st)==0 && st.mode==FIO_S_IFDIR);
 }
 puts("PASS: FatFs and mcman stat results become errno without changing other filesystem domains");
}
"""
cc = shutil.which("gcc") or shutil.which("cc")
assert cc
with tempfile.TemporaryDirectory(prefix="fatfs-stat-") as folder:
    p = Path(folder)
    (p / "test.c").write_text(code)
    for exfat in (False, True):
        exe = p / ("fatfs.exe" if exfat else "classic.exe")
        subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                        *(["-DEXFAT"] if exfat else []), str(p / "test.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
