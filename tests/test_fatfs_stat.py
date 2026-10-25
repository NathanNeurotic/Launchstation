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
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1024
typedef struct { int mode; } iox_stat_t;
static int driver_result;
static int ensurePathPcCardCfOnline(const char *p) {(void)p;return 1;}
static void ensurePathMemoryCardAccessible(const char *p) {(void)p;}
static void normalizePcCardCfPath(char *p, unsigned long n) {(void)p;(void)n;}
static void genLimObjName(char *p,int n) {(void)p;(void)n;}
static int fileXioGetStat(const char *p,iox_stat_t *s) {(void)p;(void)s;return driver_result;}
"""
if "static int normalizeFatFsStatResult(" in source:
    code += function("static int normalizeFatFsStatResult(")
code += function("int genGetStat(")
code += r"""
static int stat_result(const char *p,int raw) {
 iox_stat_t st;driver_result=raw;return genGetStat(p,&st);
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
 const char *other[]={"mc0:/NEW.ELF","mc1:/NEW.ELF","vmc0:/NEW.ELF",
                      "pfs0:/NEW.ELF","host:/NEW.ELF","udpfs:/NEW.ELF",
                      "mmce0:/NEW.ELF","usbgarbage:/NEW.ELF","mass3x:/NEW.ELF"};
 for (unsigned i=0;i<sizeof(other)/sizeof(*other);i++) {
  assert(stat_result(other[i],-ENOENT)==-ENOENT);
  assert(stat_result(other[i],-EINTR)==-EINTR);
  assert(stat_result(other[i],-EACCES)==-EACCES);
 }
 puts("PASS: FatFs stat results become errno without changing other filesystem domains");
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
