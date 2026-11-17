"""File Browser device rows read "What it is (where it is)".

Compiles the real add() from src/dash_devices.c and checks each row's label,
including the longest one (a multitap slot) fitting DashDevice.name, and
USB's usbN: shown as massN: the way the browser shows its paths.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_device_labels.py" in workflow
devices = (root / "src/dash_devices.c").read_text(encoding="utf-8")
internal = (root / "include/dash_internal.h").read_text(encoding="utf-8")


def function(source, signature):
    a = source.index(signature)
    b = source.index("{", a) + 1
    depth = 1
    while depth:
        depth += (source[b] == "{") - (source[b] == "}")
        b += 1
    return source[a:b] + "\n"


size = int(re.search(r"char name\[(\d+)\];", internal[internal.index("float fill;") - 400:]).group(1))
for name in ('"Memory Card Slot %d"', '"Multitap Memory Card Slot %d-%c"', '"USB Mass Storage Port"',
             '"MMCE SD Slot 1"', '"MMCE SD Slot 2"', '"MX4SIO SD Slot 2"', '"FireWire iLink Port"',
             '"Internal APA HDD"', '"PC Card CompactFlash"', '"PSX DVR HDD"', '"PSX Internal Flash"',
             '"Internal exFAT HDD"', '"Disc Drive"', '"Network"', '"Virtual Memory Card"'):
    assert name in devices, name
for old in ('"MASS"', '"MASS %d"', '"mc%d (Slot %d)"', '"MMCE Slot 1"', '"Internal HDD (APA)"',
            '"Network (UDPFS)"', '"Network Host"', '"iLink / FireWire"', '"PC CARD CF"'):
    assert old not in devices, old

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DASH_DEV_READY 1
static const char *localizeDashboardText(const char *s){return s;}
static void dashStrCopy(char *d,const char *s,int n){snprintf(d,n,"%s",s);}
typedef struct{int icon;int state;char name[NAME];char mount[24];char browse[24];char status[48];char detail[48];float fill;}DashDevice;
''' + function(devices, "static DashDevice *add(") + r'''
int main(void){
 DashDevice d[8];int n=0;
 add(d,&n,8,0,1,"Multitap Memory Card Slot 2-B","mc3:","mc3:/","");
 add(d,&n,8,0,1,"USB Mass Storage Port","usb0:","usb0:/","");
 add(d,&n,8,0,1,"USB Mass Storage Port","usb:","usb:/","");
 add(d,&n,8,0,1,"Internal APA HDD","hdd0:","hdd0:/","");
 add(d,&n,8,0,1,"Network","udpfs:","udpfs:/","");
 add(d,&n,8,0,1,"PC Card CompactFlash","cf0:","cf0:/","");
 assert(!strcmp(d[0].name,"Multitap Memory Card Slot 2-B (mc3:/)"));
 assert(!strcmp(d[1].name,"USB Mass Storage Port (mass0:/)"));
 assert(!strcmp(d[2].name,"USB Mass Storage Port (mass:/)"));
 assert(!strcmp(d[3].name,"Internal APA HDD (hdd0:/)"));
 assert(!strcmp(d[4].name,"Network (udpfs:/)"));
 assert(!strcmp(d[5].name,"PC Card CompactFlash (cf0:/)"));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="device-labels-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", f"-DNAME={size}", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: device rows named as what they are and the path they open")
