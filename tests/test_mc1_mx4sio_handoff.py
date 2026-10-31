"""Exercise the production MC1/MX4SIO handoff before browser/picker jobs.

IOP reset and libmc are modeled; this is host evidence, not hardware validation.
An optional source-tree path runs against a saved older implementation.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
init = (root / "src/init.c").read_text()
filer = (root / "src/filer.c").read_text()
device = (root / "src/filer_device.c").read_text()
browser = (root / "src/dash_browser.c").read_text()
widgets = (root / "src/dash_widgets.c").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MX4SIO 1
#define TRUE 1
#define FALSE 0
#define MAX_PATH 256
#define DPRINTF(...) ((void)0)
#define STORAGE_STACK_DEFAULT 0
#define STORAGE_STACK_MX4SIO 2
#define BLOCK_STACK_HDD 1
#define sceMcResSucceed 0
#define sceMcResFailDetect -3
#define DASH_TOAST_WARN 1
#define DASH_DEV_READY 1
#define DASH_DEV_BUSY 2
#define DI_MC 3
static int have_usbd,have_usb_mass,have_HDD_modules,block_storage_stack_mode,have_cdvd;
static int have_mx4sio,mx4sio_driver_running,storage_driver_stack_mode;
static int USB_mass_loaded,hddNparties[2],worker,jobs,refuse_reset,resets,drains,handshakes;
static int mctype_PSx,root_mc_polled[2],root_mc_present[2],root_mc_known_type[2];
static int root_mc_known_free[2],root_mc_failures[2];
static int waits,cancel_wait,queued,picker_queued,root_returns,picker_roots,quiet,toasts;
static struct { int loading; } pk;
static void filerRootMcPollIdle(void) { drains++; }
static void resetRuntimeDeviceState(int show) {
    assert(show==TRUE);
    if(worker || refuse_reset) return;
    assert(!jobs); /* UI must settle outstanding file I/O before this boundary. */
    filerRootMcPollIdle();resets++;
    storage_driver_stack_mode=STORAGE_STACK_DEFAULT;
    have_mx4sio=mx4sio_driver_running=0;
}
static int waitMemoryCardReadyForBrowse(int port) {
    assert(port==0 || (!mx4sio_driver_running && storage_driver_stack_mode!=STORAGE_STACK_MX4SIO));
    handshakes++;mctype_PSx=2;return sceMcResSucceed;
}
static int isPcCardCfPath(const char *p) { (void)p;return 0; }
static int loadPcCardCfModules(void) { return 0; }
static int isPcCardCfOnline(void) { return 0; }
static void loadUsbModules(void) { USB_mass_loaded=1; }
static int loadMx4sioModules(void) { return 1; }
static int loadHddModules(void) { return 1; }
static void loadCdModules(void) { }
static int getHddDeviceFromPath(const char *p,char *dev) { (void)p;(void)dev;return -1; }
static int getHddUnitFromDevice(const char *d) { (void)d;return -1; }
static void setPartyListForDevice(const char *d) { (void)d; }
static int dashWorkerAwaitAll(const char *title) {
    assert(title);waits++;if(cancel_wait)return 0;jobs=0;return 1;
}
static int isUsbBrowsePath(const char *p) { return !strncmp(p,"usb",3); }
static int isStartupDriverStatusQuiet(void) { return quiet; }
static void setStartupDriverStatusQuiet(int q) { quiet=q; }
static void dashLoadingBegin(const char *title) { assert(title); }
static void dashLoadingEnd(void) { }
static void dashStrCopy(char *out,const char *in,int n) { snprintf(out,n,"%s",in); }
static void dashToast(int type,const char *msg) { assert(type==DASH_TOAST_WARN && msg);toasts++; }
static void openDir(const char *p,const char *selected) { assert(!p[0] && selected);root_returns++; }
'''
for source, signature in (
    (init, "int isPathDeviceStackLoaded(const char *path)\n{"),
    (init, "void ensureMemoryCardPortAccessible(int port)\n{"),
    (device, "static int isMc1Path("),
    (device, "int ensurePathDeviceStackReady("),
    (filer, "int filerDirReadNeedsPrep("),
    (filer, "int filerPrepareDirRead("),
    (filer, "int filerPrepareMemoryCardRead("),
):
    code += function(source, signature)
# Execute the real preparation blocks, with a queue counter at the exact point
# where the production functions continue to their directory-job allocation.
body = function(browser, "static void openDir(const char *dir, const char *select)\n{")
start = body.index("\tif (filerDirReadNeedsPrep(dir))")
end = body.index("\t/* the classic browser remembers", start)
code += "static void browse(const char *dir) {\n" + body[start:end] + "queued++;\n}\n"
body = function(widgets, "static void pickOpen(const char *dir)\n{")
start = body.index("\tif (filerDirReadNeedsPrep(dir))")
end = body.index("\tj = (PickListJob *)malloc", start)
code += 'static void pickOpen(const char *dir) { if(!dir[0]){picker_roots++;pk.loading=0;return;}\n'
code += body[start:end] + "picker_queued++;\n}\n"
# The actual root-row condition must allow an unresolved normal memory card.
start = widgets.index("\t\t\t\tif (d->state == DASH_DEV_READY")
end = widgets.index("\t\t\t\telse if (d->state", start)
code += 'typedef struct {int state,icon;const char *browse;} DashDevice;\n'
code += 'static void choosePickerDevice(const DashDevice *d) {\n' + widgets[start:end] + '}\n'
code += r'''
static void reset_case(int mode,int running) {
    storage_driver_stack_mode=mode;mx4sio_driver_running=running;
    jobs=1;worker=refuse_reset=resets=drains=handshakes=waits=cancel_wait=0;
    queued=picker_queued=root_returns=picker_roots=quiet=toasts=0;pk.loading=1;
}
int main(void) {
    for(int mode=0;mode<=1;mode++) {
        reset_case(mode?STORAGE_STACK_MX4SIO:STORAGE_STACK_DEFAULT,!mode);
        assert(!isPathDeviceStackLoaded("mc1:/") && isPathDeviceStackLoaded("mc0:/"));
        assert(filerDirReadNeedsPrep("mc1:/") && filerDirReadNeedsPrep("mc1:/APPS/"));
        assert(!filerDirReadNeedsPrep("mc0:/") && !filerDirReadNeedsPrep("vmc1:/"));
        browse("mc1:/");assert(waits==1 && !jobs && resets==1 && queued==1);
        worker=1;
        assert(filerPrepareMemoryCardRead(1)==sceMcResSucceed);
        assert(handshakes==1 && resets==1 && isPathDeviceStackLoaded("mc1:/"));
        assert(!filerDirReadNeedsPrep("mc1:/"));
    }
    reset_case(STORAGE_STACK_MX4SIO,1);
    browse("mc0:/");assert(!resets && !waits && queued==1);
    worker=1;assert(filerPrepareMemoryCardRead(0)==sceMcResSucceed && !resets);

    reset_case(STORAGE_STACK_MX4SIO,1);
    const DashDevice d={DASH_DEV_BUSY,DI_MC,"mc1:/"};
    choosePickerDevice(&d);assert(waits==1 && resets==1 && picker_queued==1);

    reset_case(STORAGE_STACK_MX4SIO,1);cancel_wait=1;
    browse("mc1:/");assert(waits==1 && jobs && !queued && !resets && root_returns==1);
    choosePickerDevice(&d);assert(!picker_queued && !pk.loading && !resets && picker_roots==1);

    reset_case(STORAGE_STACK_MX4SIO,1);refuse_reset=1;
    browse("mc1:/");assert(!queued && !resets && root_returns==1 && toasts==1);
    choosePickerDevice(&d);assert(!picker_queued && !resets && picker_roots==1 && toasts==2);
    assert(!handshakes && !quiet);

    reset_case(STORAGE_STACK_MX4SIO,1);worker=1;
    assert(filerPrepareMemoryCardRead(1)==sceMcResFailDetect);
    assert(!handshakes && !resets); /* worker refusal never reaches libmc */

    reset_case(STORAGE_STACK_DEFAULT,0);browse("mc1:/");
    assert(queued==1 && !resets && !waits);
    worker=1;assert(filerPrepareMemoryCardRead(1)==sceMcResSucceed && !resets);
    puts("PASS: mc1 resets before browser/picker reads; mc0 unchanged, cancel/refused resets safe, workers never probe MX4SIO-owned mc1");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-mc1-mx4sio-") as tmp:
    source = Path(tmp) / "handoff.c"
    executable = Path(tmp) / "handoff"
    source.write_text(code)
    subprocess.run([compiler,"-std=c99","-Wall","-Wextra","-Werror","-Wno-unused-function",str(source),"-o",str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
