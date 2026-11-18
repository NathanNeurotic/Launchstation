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
             '"Internal APA HDD"', '"Internal APA HDD 2"', '"PC Card CompactFlash"', '"PSX DVR HDD"', '"PSX Internal Flash"',
             '"Internal exFAT HDD"', '"Internal exFAT HDD 2"', '"Disc Drive"', '"Network"', '"Virtual Memory Card"'):
    assert name in devices, name
for old in ('"MASS"', '"MASS %d"', '"mc%d (Slot %d)"', '"MMCE Slot 1"', '"Internal HDD (APA)"',
            '"Network (UDPFS)"', '"Network Host"', '"iLink / FireWire"', '"PC CARD CF"'):
    assert old not in devices, old

# Secondary unit names, cached probes, and start paths must remain independent.
# A family driver being resident does not establish that unit 1 was checked.
auto_needed = function(devices, "static int autoStartNeeded(")
auto_probe = function(devices, "void dashDevicesAutoProbe(")
start_path = function(devices, "void dashDevicesStartForPath(")
for unit in ("hdd", "ata"):
    assert f'storageState("{unit}1:") == DASH_DEV_PROBE' in auto_needed
    assert f'probeMount("{unit}1:");' in auto_probe
    assert f'if (filerShow{unit.capitalize()}Unit(1) && !console_is_PSX' in auto_probe
assert 'storageState(mount) != DASH_DEV_READY' in start_path
assert 'isPathDeviceStackLoaded(path))' not in start_path.split("const char *mount = NULL;", 1)[1].split("#ifdef EXFAT", 1)[0]
for guard in (
    'if (filerShowHddUnit(1) && !console_is_PSX)',
    'if (filerShowAtaUnit(1) && !console_is_PSX)',
    'if (!strncmp(mount, "hdd1:", 5)) return PROBE_HDD1;',
    'if (!strncmp(mount, "ata1:", 5)) return PROBE_ATA1;',
    """probeRoot(m[3] == '1' ? "hdd1:" : "hdd0:", 0)""",
    """probeRoot(m[3] == '1' ? "ata1:/" : "ata0:/", 2000)""",
    """mount = path[3] == '1' ? "hdd1:" : "hdd0:";""",
    """mount = path[3] == '1' ? "ata1:" : "ata0:";""",
):
    assert guard in devices, guard

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
 add(d,&n,8,0,1,"Internal APA HDD 2","hdd1:","hdd1:/","");
 add(d,&n,8,0,1,"Internal exFAT HDD 2","ata1:","ata1:/","");
 assert(!strcmp(d[0].name,"Multitap Memory Card Slot 2-B (mc3:/)"));
 assert(!strcmp(d[1].name,"USB Mass Storage Port (mass0:/)"));
 assert(!strcmp(d[2].name,"USB Mass Storage Port (mass:/)"));
 assert(!strcmp(d[3].name,"Internal APA HDD (hdd0:/)"));
 assert(!strcmp(d[4].name,"Network (udpfs:/)"));
 assert(!strcmp(d[5].name,"PC Card CompactFlash (cf0:/)"));
 assert(!strcmp(d[6].name,"Internal APA HDD 2 (hdd1:/)"));
 assert(!strcmp(d[7].name,"Internal exFAT HDD 2 (ata1:/)"));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="device-labels-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", f"-DNAME={size}", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
# Exercise the actual auto-start walker against a resident HDD/ATA stack.
# Unit 0 is ready; each previously unchecked unit 1 must be probed once.
autoprobe = r'''
#include <assert.h>
#include <string.h>
#define DASH_DEVA_HDD 1
#define DASH_DEVA_ATA 2
#define DASH_DEVA_USB 4
#define DASH_DEV_PROBE 0
#define DASH_DEV_READY 1
static int console_is_PSX = 0;
static int counts[4];
static int safeAutoStartMask(void) { return DASH_DEVA_HDD | DASH_DEVA_ATA; }
static unsigned int getIopResetGeneration(void) { return 7; }
static int isPathDeviceStackLoaded(const char *p) { (void)p; return 1; }
static int filerShowHddUnit(int unit) { (void)unit; return 1; }
static int filerShowAtaUnit(int unit) { (void)unit; return 1; }
static int storageState(const char *p) {
    return (p[3] == '1') ? DASH_DEV_PROBE : DASH_DEV_READY;
}
static int probeMount(const char *p) {
    int unit = p[3] == '1' ? 1 : 0;
    counts[(p[0] == 'a' ? 2 : 0) + unit]++;
    return 1;
}
''' + function(devices, "void dashDevicesAutoProbe(") + r'''
int main(void) {
    dashDevicesAutoProbe();
    assert(counts[0] == 0 && counts[1] == 1);
    assert(counts[2] == 0 && counts[3] == 1);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="secondary-unit-probe-") as tmp:
    src, exe = Path(tmp) / "probe.c", Path(tmp) / "probe"
    src.write_text(autoprobe)
    subprocess.run([cc, "-std=c99", "-DEXFAT", "-Wall", "-Wextra",
                    "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Cancellation and absent-unit retry limits must not be regressed.
main = (root / "src/dash_main.c").read_text()
lib = (root / "src/dash_lib.c").read_text()
assert "if (dashDevicesStartForPath(path) < 0)" in main
assert "unsigned int tried_ata_units = 0;" in main
assert "j->successful_disk_units" in lib
assert "dashDevicesConfirmScannedDisks(j->successful_disk_units, j->iop_generation);" in lib
assert "generation != getIopResetGeneration()" in devices

print("PASS: device rows, resident secondary-unit probing, scan cancellation and read evidence")
