"""Compile the production browser alias cache with fake BDM driver identities.

A USB unit number must never silently become a shared BDM slot number.
All ioctl work is confined to explicit root refreshes, not UI formatting.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/filer_device.c").read_text()
begin = source.index("/* Display names are never filesystem paths.")
end = source.index("\nenum {\n\tTRANSFER_STORAGE_DEFAULT", begin)
production = source[begin:end]

code = r"""
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define USB_MASS_MAX_DRIVES 10
#define USBMASS_IOCTL_GET_DRIVERNAME 3
#define USBMASS_IOCTL_GET_DEVICE_NUMBER 6
static struct { int modern_browser_device_paths; } cfg, *setting = &cfg;
static unsigned generation = 1;
static unsigned getIopResetGeneration(void) {return generation;}
#ifdef EXFAT
static int duplicate_partition, probes, identity_queries;
static int fileXioDopen(const char *path) {
 int n = -1;
 probes++;
 if (sscanf(path, "usb%d:/", &n) == 1 && n >= 0 && n <= 1) return n+1;
 if (sscanf(path, "mass%d:/", &n) != 1) return -1;
 if (n == 0 || n == 2 || n == 3 || n == 4) return 11+n;
 if (n == 5 && duplicate_partition) return 16;
 return -1;
}
static int fileXioIoctl2(int fd,int op,void *a,int alen,void *out,int len) {
 int number = -1;
 const char *driver = NULL;
 (void)a;(void)alen;
 if (fd == 1) {number=0;driver="usb";}
 if (fd == 2) {number=1;driver="usb";}
 if (fd == 11) {number=0;driver="ata";}
 if (fd == 13) {number=0;driver="sdc";}
 if (fd == 14 || fd == 16) {number=0;driver="usb";}
 if (fd == 15) {number=1;driver="usb";}
 if (number < 0) return -1;
 if (op == USBMASS_IOCTL_GET_DEVICE_NUMBER) {
  assert(len == sizeof(int));*(int *)out=number;identity_queries++;return 0;
 }
 if (op == USBMASS_IOCTL_GET_DRIVERNAME) {
  assert(driver && len > (int)strlen(driver));strcpy(out,driver);return 0;
 }
 return -1;
}
static int fileXioDclose(int fd) {(void)fd;return 0;}
#endif
"""
code += production
code += r"""
static void expect(const char *path,const char *wanted) {
 char buf[128];
 filerFormatDevicePathForDisplay(path,buf,sizeof(buf));
 assert(!strcmp(buf,wanted));
}
int main(void) {
 /* Unknown or missing identity must not silently label usb0 as mass0. */
 expect("usb0:/APPS/","usb0:/APPS/");
 expect("mass0:/ATA/","mass0:/ATA/");
 expect("mc0:/","mc0:/");
 assert(setting->modern_browser_device_paths == 0);
 filerRefreshUsbMassDisplayAlias(0);
#ifdef EXFAT
 expect("usb0:/APPS/","mass3:/APPS/");
 filerRefreshUsbMassDisplayAlias(1);
 expect("usb1:/APPS/","mass4:/APPS/");
 assert(probes > 0 && identity_queries > 0);
 int before=probes;
 for (int i=0;i<100;i++) expect("usb0:/APPS/","mass3:/APPS/");
 assert(probes == before); /* drawing must never trigger IOP RPCs */
 duplicate_partition=1;
 filerRefreshUsbMassDisplayAlias(0);
 expect("usb0:/APPS/","usb0:/APPS/"); /* ambiguous partition mapping: fail closed */
 duplicate_partition=0;
#else
 expect("usb0:/APPS/","mass0:/APPS/");
 filerRefreshUsbMassDisplayAlias(1);
 expect("usb1:/APPS/","mass1:/APPS/");
#endif
 setting->modern_browser_device_paths=1;
 expect("usb0:/APPS/","usb0:/APPS/");
 filerRefreshUsbMassDisplayAlias(0);
 expect("usb0:/APPS/","usb0:/APPS/");
 setting->modern_browser_device_paths=0;
 filerInvalidateUsbMassDisplayAliases();
 expect("usb0:/APPS/","usb0:/APPS/");
 filerRefreshUsbMassDisplayAlias(0);
#ifdef EXFAT
 expect("usb0:/APPS/","mass3:/APPS/");
#else
 expect("usb0:/APPS/","mass0:/APPS/");
#endif
 generation++;
 expect("usb0:/APPS/","usb0:/APPS/"); /* IOP reset invalidates identity */
 puts("PASS: MASS slot identity, legacy default, typed option, cache, resets and ambiguity");
}
"""
cc = shutil.which("gcc") or shutil.which("cc")
assert cc, "gcc or cc required"
with tempfile.TemporaryDirectory(prefix="usb-mass-semantics-") as folder:
    p = Path(folder)
    (p / "test.c").write_text(code)
    for exfat in (False, True):
        exe = p / ("typed.exe" if exfat else "classic.exe")
        subprocess.run(
            [cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
             *(["-DEXFAT"] if exfat else []), str(p / "test.c"), "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe)], check=True)
