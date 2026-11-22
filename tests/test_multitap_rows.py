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
init = (root / "src/init.c").read_text(encoding="utf-8")


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
retry_state = "\n".join(line for line in init.splitlines()
                        if line.startswith("static u64 mtap_")) + "\n"
retry_code = r'''
#include <assert.h>
#define HOMEBREW_SIO2MAN 1
typedef unsigned char u8;
typedef unsigned long long u64;
static u8 have_mtapman = 1, have_mc_rpc_ready = 1, mtap_bound;
static u8 mtap_open[2];
static u64 now_ms;
static int worker_busy, connected, init_calls, open_calls, close_calls, connection_calls;
static int port1_loaded = 1;
static __attribute__((unused)) u64 Timer(void) { return now_ms; }
static int mtapInit(void) { init_calls++; return 1; }
static int mtapPortOpen(int port) { assert(port == 2 || port == 3); open_calls++; return (connected & (1 << (port - 2))) != 0; }
static int mtapPortClose(int port) { assert(port == 2 || port == 3); close_calls++; return 1; }
static int mtapGetConnection(int port) { assert(port == 2 || port == 3); connection_calls++; return (connected & (1 << (port - 2))) != 0; }
static int dashWorkerOnThread(void) { return 0; }
static int dashWorkerIoBusy(void) { return worker_busy; }
static int isPathDeviceStackLoaded(const char *path) { (void)path; return port1_loaded; }
static void filerRootMcPollIdle(void) {}
static int rediscovered[2];
static void filerRootMcForgetMultitap(int port) { rediscovered[port]++; }
''' + retry_state + function(init, "void mcMultitapRefresh(void)") + r'''
int main(void) {
 worker_busy=1;mcMultitapRefresh();assert(init_calls==0 && open_calls==0);
 worker_busy=0;mcMultitapRefresh();assert(init_calls==1 && open_calls==2);
 mcMultitapRefresh();assert(open_calls==2); /* absent devices use a backoff */
 now_ms+=1000;mcMultitapRefresh();assert(open_calls==2);
 now_ms+=10000;mcMultitapRefresh();assert(open_calls==4); /* later insertion is retried */
 connected=3;now_ms+=10000;mcMultitapRefresh();assert(open_calls==6 && mtap_open[0] && mtap_open[1]);
 assert(rediscovered[0]==1 && rediscovered[1]==1);
 now_ms+=1000;mcMultitapRefresh();assert(open_calls==6 && connection_calls==2);
 assert(rediscovered[0]==1 && rediscovered[1]==1); /* steady taps keep card state */
 /* A present tap on port 0 must not disable the absent port 1 backoff. */
 mtap_open[0]=mtap_open[1]=0;mtap_retry_after[0]=mtap_retry_after[1]=0;
 connected=1;now_ms+=10000;open_calls=connection_calls=0;
 mcMultitapRefresh();assert(open_calls==2 && mtap_open[0] && !mtap_open[1]);
 now_ms+=1000;mcMultitapRefresh();
 assert(open_calls==2 && connection_calls==1); /* port 1 is still backed off */
 now_ms+=9000;mcMultitapRefresh();
 assert(open_calls==3 && connection_calls==2); /* only absent port retries */
 connected=3;now_ms+=10000;mcMultitapRefresh();
 assert(open_calls==4 && mtap_open[0] && mtap_open[1]);
 /* A port reserved for MX4SIO must not receive background MTAP RPCs. */
 mtap_open[1]=0;mtap_retry_after[1]=0;port1_loaded=0;
 open_calls=close_calls=0;now_ms+=1000;mcMultitapRefresh();
 assert(open_calls==0 && close_calls==0 && !mtap_open[1]);
 port1_loaded=1;mcMultitapRefresh();
 assert(open_calls==1 && mtap_open[1]); /* prompt re-detection */
 connected=1;mcMultitapRefresh();assert(!mtap_open[1]);
 int old_port0=rediscovered[0], old_port1=rediscovered[1];
 connected=3;now_ms+=10000;mcMultitapRefresh();
 assert(mtap_open[1] && rediscovered[1]==old_port1+1 && rediscovered[0]==old_port0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="multitap-retry-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(retry_code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: multitap rows only for cards that answer; VMC listed once")
print("PASS: busy refresh retries immediately; absent multitaps back off without hiding later insertion")
