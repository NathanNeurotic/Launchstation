"""Multitap memory card rows appear only for a card that answers.

Compiles dashDevicesMultitapChanged() from src/dash_devices.c against stub
presence and checks the File Browser's device list leaves out empty
multitap slots, adds a row once a card answers and drops it when it goes.
Also guards that a mounted VMC is listed once (by the device model only).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_multitap_rows.py" in workflow
devices = (root / "src/dash_devices.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
widgets = (root / "src/dash_widgets.c").read_text(encoding="utf-8")


def function(source, signature):
    a = source.index(signature)
    b = source.index("{", a) + 1
    depth = 1
    while depth:
        depth += (source[b] == "{") - (source[b] == "}")
        b += 1
    return source[a:b] + "\n"


listing = function(devices, "int dashDevicesList(DashDevice *out, int max)")
assert "MC_UNIT_SLOT(port) != 0 && filerRootMcPresent(port, NULL, NULL) != 1" in listing
assert "dashDevicesMultitapChanged(fb.devs, fb.ndevs)" in function(browser, "static void browserRefreshRootCards(void)")
assert "dashDevicesMultitapChanged(pk.devs, pk.ndevs)" in widgets
# VMC rows come from dashDevicesList(); the browser must not add them again.
assert "Virtual Memory Card (vmc" not in browser
assert '"Virtual Memory Card", mount, browse' in listing

code = r'''
#include <assert.h>
#include <string.h>
#include "mc_unit.h"
#define DI_MC 3
typedef struct{int icon;int state;char name[48];char mount[24];char browse[24];}DashDevice;
static int present[MC_UNITS];
static int filerRootMcPresent(int port, int *type, int *free_kb){(void)type;(void)free_kb;return present[port];}
''' + function(devices, "int dashDevicesMultitapChanged(") + r'''
static DashDevice row(int icon, const char *mount){DashDevice d;memset(&d,0,sizeof(d));d.icon=icon;strcpy(d.mount,mount);return d;}
int main(void){
 DashDevice d[4];
 d[0]=row(DI_MC,"mc0:"); d[1]=row(DI_MC,"mc1:"); d[2]=row(DI_MC,"vmc0:");
 /* no multitap card answers: nothing to add */
 assert(!dashDevicesMultitapChanged(d,3));
 present[0]=present[1]=1; present[3]=-1; present[5]=-2;
 assert(!dashDevicesMultitapChanged(d,3)); /* unknown or silent slots stay out */
 present[3]=1; /* a card answers in mc3 */
 assert(dashDevicesMultitapChanged(d,3));
 d[3]=row(DI_MC,"mc3:");
 assert(!dashDevicesMultitapChanged(d,4));
 present[3]=0; /* and is taken out */
 assert(dashDevicesMultitapChanged(d,4));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="multitap-rows-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", f"-I{root / 'include'}", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: multitap rows only for cards that answer; VMC listed once")
