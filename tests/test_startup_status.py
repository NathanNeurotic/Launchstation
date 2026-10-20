"""Execute startup presentation policy without changing driver/PAD lifetimes."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
init = (root / "src/init.c").read_text()
ui = (root / "src/dash_ui.c").read_text()
devices = (root / "src/dash_devices.c").read_text()

def function(source, signature):
    a = source.index(signature)
    b = source.index("{", a) + 1
    depth = 1
    while depth:
        depth += (source[b] == "{") - (source[b] == "}")
        b += 1
    return source[a:b] + "\n"

c = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define LNG(key) key
#define Loading_Drivers "Loading %s drivers..."
#define Rebooting_IOP "Rebooting IOP"
static int is_early_init=1, startup_driver_status_quiet=1;
static int draws, frames, reports, begins, ends, probes, rebuilds;
static int active=1, worker, loading_active=1, needed=1, allowed=1;
static int loading_done, loading_total;
static char loading_status[128], loading_remaining[64];
static void dashStrCopy(char *out,const char *s,int size) { snprintf(out,size,"%s",s); }
static void (*drawMsgOverride)(const char *);
static void drawMsg(const char *s) { if (drawMsgOverride) drawMsgOverride(s); else draws++; }
static int DashIsActive(void) { return active; }
static int dashWorkerOnThread(void) { return worker; }
static void dashWorkerStatus(const char *s,int d,int t) { (void)s;(void)d;(void)t; reports++; }
static void dashLoadingFrame(const char *s,int d,int t) { (void)s;(void)d;(void)t; frames++; }
static void dashLoadingStatus(const char *s,int d,int t) { (void)s;(void)d;(void)t; }
static void dashLoadingBegin(const char *title) { assert(!strcmp(title,"Starting Launchstation")); begins++; }
static void dashLoadingEnd(void) { ends++; }
static int autoStartNeeded(void) { return needed; }
static int dashWorkerAwaitAll(const char *s) { (void)s; return allowed; }
static void dashAppsRebuildView(void) { rebuilds++; }
'''
for signature in ("int isStartupDriverStatusQuiet(", "void setStartupDriverStatusQuiet(",
                  "static void showLoadingModulesMsg(", "static void showRebootingIopMsg("):
    # Find definitions rather than forward declarations.
    start = init.index(signature)
    if init.index(";", start) < init.index("{", start):
        start = init.index(signature, start + len(signature))
    c += function(init[start:], signature)
c += function(ui, "static void loadingProgress(")
c += function(ui, "void DashStorageProbeStatus(")
c += r'''
static void dashDevicesAutoProbe(void) {
    probes++;
    assert(isStartupDriverStatusQuiet());
    loadingProgress("Checking USB media...",3,4);
    assert(!loading_status[0] && !loading_done && !loading_total);
    showLoadingModulesMsg("usb"); showRebootingIopMsg();
    DashStorageProbeStatus("Checking USB root");
}
'''
c += function(devices, "static void quietDriverMsg(")
c += function(devices, "void dashDevicesAutoStart(")
c += r'''
int main(void) {
    is_early_init=0; /* GS ready, runtime input modules still starting. */
    showLoadingModulesMsg("usbkbd"); showRebootingIopMsg();
    DashStorageProbeStatus("Waiting for USB");
    assert(!draws && !frames && !reports);
    setStartupDriverStatusQuiet(0);
    strcpy(loading_remaining,"Remaining size: 0.00 MB");
    loadingProgress("Checking memory card...",1,2);
    assert(!loading_remaining[0]);
    assert(!strcmp(loading_status,"Checking memory card...") && loading_done==1 && loading_total==2);
    loadingProgress(NULL,0,0); assert(loading_status[0]);
    showLoadingModulesMsg("usb"); showRebootingIopMsg();
    DashStorageProbeStatus("Manual probe");
    assert(draws==2 && frames==1);
    dashDevicesAutoStart();
    assert(probes==1 && begins==1 && ends==1 && rebuilds==1);
    assert(draws==2 && frames==1 && !drawMsgOverride && !isStartupDriverStatusQuiet());
    setStartupDriverStatusQuiet(1); dashDevicesAutoStart();
    assert(probes==2 && isStartupDriverStatusQuiet());
    setStartupDriverStatusQuiet(0); worker=1;
    DashStorageProbeStatus("Worker probe"); assert(reports==1 && frames==1);
    setStartupDriverStatusQuiet(1);
    DashStorageProbeStatus("Startup worker"); assert(reports==1 && frames==1);
    setStartupDriverStatusQuiet(0); worker=0; active=0;
    DashStorageProbeStatus("Classic"); assert(frames==1);
    is_early_init=1; showLoadingModulesMsg("before GS"); assert(draws==2);
    needed=0; dashDevicesAutoStart(); assert(probes==2 && begins==2);
    needed=1; allowed=0; dashDevicesAutoStart(); assert(probes==2 && begins==2);
    puts("PASS: quiet startup driver/probe feedback, real auto-probes, manual/worker status and restoration");
}
'''
assert "static u8 startup_driver_status_quiet = 1;" in init
stop = function(init, "static void stopDs34Input(void)\n{")
assert "is_early_init" in stop and "isStartupDriverStatusQuiet" not in stop
main = (root / "src/dash_main.c").read_text()
entry = main[main.index("void DashRun("):]
assert entry.index("firstInit();") < entry.index("setStartupDriverStatusQuiet(0);")
assert entry.index("screens[section]->enter();") < entry.index("setStartupDriverStatusQuiet(0);")
for signature in ("void dashLoadingStatus(", "void dashLoadingFrame("):
    assert "loadingProgress(status, done, total);" in function(ui,signature)
with tempfile.TemporaryDirectory(prefix="startup-status-") as folder:
    p = Path(folder)
    (p / "test.c").write_text(c)
    exe = p / "test.exe"
    cc = shutil.which("gcc") or shutil.which("clang")
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(p / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
