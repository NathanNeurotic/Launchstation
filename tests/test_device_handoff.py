"""Execute ordinary-ELF namespace translation with mixed BDM mount identities."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/elf.c").read_text()

def function(signature, source=source):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

code = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define MAX_PATH 1025
#define USB_MASS_MAX_DRIVES 10
#define FIO_O_RDONLY 1
#define USBMASS_IOCTL_GET_DRIVERNAME 3
#define USBMASS_IOCTL_GET_DEVICE_NUMBER 6
#define USBMASS_IOCTL_GET_LBA 2
typedef uint32_t u32;
typedef uint64_t u64;
static struct { int modern_device_handoff; } settings;
#define setting (&settings)
#ifdef EXFAT
static int unsupported, close_failure, opened, closed;
static int fileXioOpen(const char *p, int mode, int perm) {
 (void)mode;(void)perm;
 if (!strcmp(p,"usb0:/APP.ELF") || !strcmp(p,"mass3:/APP.ELF")) {opened++;return 1;}
 if (!strcmp(p,"usb1:/APP.ELF") || !strcmp(p,"mass4:/APP.ELF")) {opened++;return 2;}
 if (!strcmp(p,"ata0:/APP.ELF") || !strcmp(p,"mass0:/APP.ELF")) {opened++;return 3;}
 if (!strcmp(p,"mx4sio0:/APP.ELF") || !strcmp(p,"mass2:/APP.ELF")) {opened++;return 4;}
 /* Same driver, same path and same LBA, different physical device. */
 if (!strcmp(p,"mass1:/APP.ELF")) {opened++;return 5;}
 /* Same driver and physical device, different partition/file LBA. */
 if (!strcmp(p,"mass5:/APP.ELF")) {opened++;return 6;}
 return -1;
}
static int fileXioIoctl2(int fd, int op, void *arg, int arglen, void *out, int size) {
 (void)arg;(void)arglen;(void)size;
 if (unsupported) return -1;
 if(op==3)strcpy(out,fd==3?"ata":fd==4?"sdc":"usb");
 if(op==6)*(u32*)out=fd==2?1:fd==5?8:0;
 if(op==2)*(u64*)out=fd==6?999:123;
 return 0;
}
static int fileXioClose(int fd) {(void)fd;closed++;return close_failure?-1:0;}
"""
start = source.index("typedef struct {\n\tchar driver[32];")
end = source.index("\n#endif", start)
code += source[start:end] + "\n#endif\n"
code += "static int isHddBrowserPath(const char *p) { return !strncmp(p,\"hdd0:/\",6); }\n"
code += function("static int parseUsbMassPathUnit(")
code += function("static const char *normalizeExecArg0Path(")
code += function("void PrepareElfHandoffPath(")
code += "static void filerFormatDevicePathForDisplay(const char *p,char *out,int n){snprintf(out,n,\"%s\",p);}\n"
code += function("static void formatBrowserPathForDisplay(", (root / "src/filer_browser.c").read_text())
code += r"""
static void expect(const char *p,const char *wanted) {
 char out[MAX_PATH];PrepareElfHandoffPath(p,out,sizeof(out));assert(!strcmp(out,wanted));
}
int main(void) {
 char display[MAX_PATH];
 formatBrowserPathForDisplay("usb1:/APPS/APP.ELF",display);assert(!strcmp(display,"usb1:/APPS/APP.ELF"));
 formatBrowserPathForDisplay("usb:/APPS/APP.ELF",display);assert(!strcmp(display,"usb:/APPS/APP.ELF"));
 formatBrowserPathForDisplay("mass3:/APPS/APP.ELF",display);assert(!strcmp(display,"mass3:/APPS/APP.ELF"));
 formatBrowserPathForDisplay("ata0:/APP.ELF",display);assert(!strcmp(display,"ata0:/APP.ELF"));
 settings.modern_device_handoff=0;
#ifdef EXFAT
 expect("usb0:/APP.ELF","mass3:/APP.ELF");
 expect("mass:/APP.ELF","mass3:/APP.ELF");
 expect("usb1:/APP.ELF","mass4:/APP.ELF");
 expect("ata0:/APP.ELF","mass0:/APP.ELF");
 expect("mx4sio0:/APP.ELF","mass2:/APP.ELF");
 unsupported=1;expect("usb0:/APP.ELF","usb0:/APP.ELF");
 expect("mass:/APP.ELF","mass:/APP.ELF");unsupported=0;
 close_failure=1;expect("ata0:/APP.ELF","ata0:/APP.ELF");close_failure=0;
 settings.modern_device_handoff=1;
 expect("usb1:/APP.ELF","usb1:/APP.ELF");
 expect("mass:/APP.ELF","usb0:/APP.ELF");
 expect("ata0:/APP.ELF","ata0:/APP.ELF");
 expect("mass3:/APP.ELF","usb0:/APP.ELF");
 expect("mass4:/APP.ELF","usb1:/APP.ELF");
 expect("mass0:/APP.ELF","ata0:/APP.ELF");
 expect("mass2:/APP.ELF","mx4sio0:/APP.ELF");
 expect("mass9:/APP.ELF","mass9:/APP.ELF");
 assert(opened==closed);
#else
 expect("usb1:/APP.ELF","mass1:/APP.ELF");
 settings.modern_device_handoff=1;
 expect("usb1:/APP.ELF","mass1:/APP.ELF");
#endif
 expect("mc0:/BOOT/APP.ELF","mc0:/BOOT/APP.ELF");
 expect("hdd0:/+APPS/APP.ELF","hdd0:+APPS:pfs:/APP.ELF");
 expect("mmce0:/APP.ELF","mmce0:/APP.ELF");
 return 0;
}
"""
main = (root / "src/main_actions.c").read_text()
assert main.index("PrepareElfHandoffPath(path, handoff") < main.index("RunLoaderElfWithHandoff(fullpath")
segment = main[main.index("PrepareElfHandoffPath(path, handoff"):main.index("RunLoaderElfWithHandoff(fullpath")]
assert "CleanUpForExec();" in segment
config = (root / "src/config.c").read_text()
assert "setting->modern_device_handoff = 0;" in config
assert '"Modern_Device_Handoff = %d\\r\\n"' in config
assert "setting->modern_device_handoff = (atoi(value) == 1);" in config
cc = shutil.which("gcc") or shutil.which("cc")
with tempfile.TemporaryDirectory(prefix="device-handoff-") as folder:
    path = Path(folder)
    c = path / "test.c"
    c.write_text(code)
    for exfat in (False, True):
        exe = path / ("exfat.exe" if exfat else "classic.exe")
        subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *(["-DEXFAT"] if exfat else []), str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print("PASS: legacy/modern handoffs preserve mixed BDM device and partition identity; classic fallback")
