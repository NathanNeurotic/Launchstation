"""Compile Launchstation's USB Ember path resolver with two simulated devices.

Verify that a reordered IOP BDM namespace cannot silently send the game to
another drive, and that duplicates/unsupported shared-BDM transports fail.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/init.c").read_text()
ember = (root / "src/ember.c").read_text()
header = (root / "include/init.h").read_text()
assert "const char *game_image" in header
assert "prepareEmberUsbHandoffIop(ember_elf, path," in ember
assert "snprintf(fullpath, sizeof(fullpath), \"%s\", remounted_elf)" in ember
assert "USBMASS_IOCTL_GET_DRIVERNAME" in src
assert "current.digest != before->digest" in src
assert "matches > 1" in src
assert "resetUsbHostControllerBeforeIopReset()" in src
assert '"BDMFS_FATFS"' in src and '"USBMASS_BD"' in src

def extract(signature):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 1
    j = brace + 1
    while depth:
        depth += (src[j] == "{") - (src[j] == "}")
        j += 1
    return src[start:j] + "\n"

code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct {u32 size, hisize, mode;} iox_stat_t;
#define MAX_PATH 1024
#define FIO_O_RDONLY 1
#define USBMASS_IOCTL_GET_DRIVERNAME 0xDEAD
#define FIO_S_ISDIR(mode) (((mode) & 0x4000) != 0)
#define DPRINTF(...) ((void)0)
static int duplicate = 0, bad_driver=0;
static int active_fd=-1, file_pos;
static const char *game_tail="/EMBER/games/Spyro/Disc.CUE";
static const char *elf_tail="/EMBER/ember.elf";
static int unit_of(const char *path) {
    return (!strncmp(path, "usb0:", 5) ? 0 :
            !strncmp(path, "usb1:", 5) ? 1 :
            !strncmp(path, "mass1:", 6) ? 2 :
            !strncmp(path, "mass2:", 6) ? 3 : -1);
}
static int fileXioGetStat(const char *path, iox_stat_t *st) {
    int unit=unit_of(path);
    if(unit<0 || (!strstr(path,game_tail) && !strstr(path,elf_tail)))
        return -1;
    st->mode=0;
    st->hisize=0;
    st->size=strstr(path,game_tail) ? 6 : 2048;
    return 0;
}
static int fileXioOpen(const char *path,int mode,int rights) {
    int unit=unit_of(path);
    (void)rights;assert(mode==FIO_O_RDONLY);
    if(unit<0 || !strstr(path, game_tail))return -1;
    active_fd=unit+1;file_pos=0;return active_fd;
}
static int fileXioRead(int fd,void *dst,int count) {
    const char *s;
    int n=6;
    assert(fd==active_fd);
    s=(fd==1 || fd==3 || (duplicate && fd==2)) ? "TARGET" : "OTHER!";
    if(file_pos>=n)return 0;
    if(count>n-file_pos)count=n-file_pos;
    memcpy(dst,s+file_pos,count);file_pos+=count;return count;
}
static int fileXioClose(int fd){assert(fd==active_fd);active_fd=-1;return 0;}
static int fileXioDopen(const char *path) {
    if(!strcmp(path,"mass1:/"))return 1;
    if(!strcmp(path,"mass2:/"))return 2;
    return -1;
}
static int fileXioIoctl2(int fd,int command,void *in,int ilen,void *out,int olen) {
    (void)in;(void)ilen;assert(command==USBMASS_IOCTL_GET_DRIVERNAME);
    snprintf(out,olen,"%s",fd==1 && !bad_driver?"usb":"ata");
    return 0;
}
static int fileXioDclose(int fd){(void)fd;return 0;}
static void DelayThread(int us){(void)us;}
"""
code += src[src.index("typedef struct {\n\tu32 size, high, digest;\n} EmberUsbIdentity;"):
            src.index("\n/* Compare the selected *game image*", src.index("typedef struct {\n\tu32 size, high, digest;\n} EmberUsbIdentity;"))]
for f in ("static int emberUsbSourceIsUsb(", "static int emberUsbIdentifyFile(", "static int emberUsbFindRemounted("):
    code += extract(f)
code += r"""
int main(void) {
    EmberUsbIdentity before;
    char out[1024]={0};
    const char *image="mass1:/EMBER/games/Spyro/Disc.CUE";
    const char *elf="mass1:/EMBER/ember.elf";
    assert(emberUsbSourceIsUsb(image));
    assert(!emberUsbSourceIsUsb("mass2:/EMBER/ember.elf"));
    assert(emberUsbIdentifyFile(image,&before));
    assert(emberUsbFindRemounted(image,elf,&before,out,sizeof(out)));
    /* Original mass1 now lives at usb0 after the reset. */
    assert(!strcmp(out,"usb0:/EMBER/ember.elf"));
    duplicate=1;
    assert(!emberUsbFindRemounted(image,elf,&before,out,sizeof(out)));
    bad_driver=1;
    assert(!emberUsbSourceIsUsb(image));
    assert(emberUsbSourceIsUsb("usb0:/EMBER/ember.elf"));
    puts("PASS: two USB drives, reordered enumeration, duplicate games, shared-BDM transport");
    return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "host C compiler required"
with tempfile.TemporaryDirectory(prefix="ember-dual-usb-") as td:
    source = Path(td) / "handoff.c"
    exe = Path(td) / "handoff"
    source.write_text(code)
    subprocess.run([cc,"-std=gnu11","-Wall","-Werror","-Wno-unused-parameter",str(source),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
