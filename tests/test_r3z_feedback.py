"""Execute video rollback and hardware/stack guards; no console acceptance."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def function(name, signature):
    source = (root / name).read_text()
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"

code = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MX4SIO 1
static int console_is_PSX;
static char ROMVER_data[16];
enum {STORAGE_STACK_DEFAULT, STORAGE_STACK_MMCE, STORAGE_STACK_MX4SIO};
static int storage_driver_stack_mode, mx4sio_driver_running, loads, load_ok;
static int loadMmceModules(void) { loads++; return load_ok; }
static struct { int TV_mode, screen_x, screen_y; char Misc[64], Misc_PS2Net[64]; } config, *setting = &config;
static int updates, checks, invalidations, accept;
static void updateScreenMode(void) { updates++; if(setting->TV_mode==2) { setting->screen_x=0; setting->screen_y=0; } }
static int dashGfxCheckScreenChange(void) { checks++; return 1; }
static void dashArtInvalidateVram(void) { invalidations++; }
static int dashModalKeepVideo(void) { return accept; }
"""
code += function("src/init.c", "int isIlinkConsoleSupported(")
code += function("src/init.c", "int loadMmceModulesForGameID(")
code += function("src/dash_settings.c", "static int previewVideoMode(")
code += function("src/lang.c", "const char *getMiscEntryDisplayName(")
code += r"""
int main(void) {
 strcpy(setting->Misc,"MISC/");strcpy(setting->Misc_PS2Net,"MISC/PS2Net");
 assert(!strcmp(getMiscEntryDisplayName("MISC/","PS2Net"),"FTP server"));
 assert(!strcmp(getMiscEntryDisplayName("usb0:/","PS2Net"),"PS2Net"));
 assert(!strcmp(setting->Misc_PS2Net,"MISC/PS2Net"));
 strcpy(setting->Misc,"Tools/");strcpy(setting->Misc_PS2Net,"Tools/CustomFTP");
 assert(!strcmp(getMiscEntryDisplayName("Tools/","CustomFTP"),"FTP server"));
 assert(!strcmp(getMiscEntryDisplayName("Tools/","Configure"),"Configure"));
 const char *supported[]={"0100JC", "0101JC", "0120EC", "0150AC", "0160AC", "0160ED"};
 const char *unsupported[]={"", "0", "01x0EC", "0000JC", "0170JC", "0180JC", "0190AC", "0200EC", "0230EC"};
 for(unsigned i=0;i<sizeof(supported)/sizeof(*supported);i++) {
  strcpy(ROMVER_data,supported[i]);assert(isIlinkConsoleSupported());
 }
 for(unsigned i=0;i<sizeof(unsupported)/sizeof(*unsupported);i++) {
  strcpy(ROMVER_data,unsupported[i]);assert(!isIlinkConsoleSupported());
 }
 console_is_PSX=1;strcpy(ROMVER_data,"0160JC");assert(!isIlinkConsoleSupported());
 for(int stack=0;stack<=2;stack++) for(int mx=0;mx<=1;mx++) for(int ok=0;ok<=1;ok++) {
  loads=0;storage_driver_stack_mode=stack;mx4sio_driver_running=mx;load_ok=ok;
  int safe=stack!=STORAGE_STACK_MX4SIO && !mx;
  assert(loadMmceModulesForGameID()==(safe && ok));assert(loads==safe);
 }
 for(int keep=0;keep<=1;keep++) {
  setting->TV_mode=1;setting->screen_x=12;setting->screen_y=-8;
  updates=checks=invalidations=0;accept=keep;
  assert(previewVideoMode(2)==keep);
  assert(setting->TV_mode==(keep?2:1));
  assert(setting->screen_x==(keep?0:12) && setting->screen_y==(keep?0:-8));
  assert(updates==(keep?1:2) && checks==updates && invalidations==updates);
  assert(!previewVideoMode(setting->TV_mode) && updates==(keep?1:2));
 }
 return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("cc")
assert cc
with tempfile.TemporaryDirectory(prefix="r3z-feedback-") as td:
    path=Path(td); (path/"test.c").write_text(code)
    exe=path/"test.exe"
    subprocess.run([cc,"-std=c99","-Wall","-Wextra","-Werror",str(path/"test.c"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("PASS: video acceptance/rollback, supported BIOS/PSX gates and MMCE initialization without stack replacement")
