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
#include <stdlib.h>
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
#define MAX_NAME 256
#define PICK_MAX 16
#define DASH_PICK_FOLDER 1
#define DASH_PICK_LAUNCH 2
#define sceMcFileAttrSubdir 0x20
typedef unsigned char u8;
typedef struct {char name[MAX_NAME];struct {int AttrFile;}stats;} FILEINFO;
typedef struct {char name[MAX_NAME];unsigned char dir;} PickEntry;
typedef struct {int gen,n;char path[MAX_PATH];FILEINFO *files;} PickListJob;
static struct { int loading,active,gen,n,mode;const char *ext;PickEntry *entries; } pk;
static int genCmpFileExt(const char *name,const char *extension) {(void)name;(void)extension;return 1;}
static int pickExtOk(const char *dir,const char *name,const char *ext) {(void)dir;(void)name;(void)ext;return 1;}
static int pickCmp(const void *a,const void *b) {return strcmp(((const PickEntry*)a)->name,((const PickEntry*)b)->name);}
static void filerRootMcPollIdle(void) { drains++; }
static int rpc_down,rpc_starts,rpc_fail;
static void ensureCoreIoStackReady(void) { assert(!worker); rpc_starts++; rpc_down=rpc_fail; }
static int isMemoryCardRpcReady(void) { return !rpc_down; }
static int filerRootMcPresent(int p,void *type,void *free_units) {
    (void)type;(void)free_units;return root_mc_polled[p]?root_mc_present[p]:-1;
}
static int filerPrepareMemoryCardRead(int port);
static int dashWorkerOnThread(void) { return worker; }
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
code += function(widgets, 'static void pickListDone(')
code += r'''
static void reset_case(int mode,int running) {
    storage_driver_stack_mode=mode;mx4sio_driver_running=running;
    jobs=1;worker=refuse_reset=resets=drains=handshakes=waits=cancel_wait=0;
    queued=picker_queued=root_returns=picker_roots=quiet=toasts=0;pk.loading=1;
    root_mc_polled[0]=root_mc_polled[1]=root_mc_present[0]=root_mc_present[1]=1;
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
        assert(handshakes==2 && resets==1 && isPathDeviceStackLoaded("mc1:/"));
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

    /* A failed card RPC start is retried on the UI thread only. */
    reset_case(STORAGE_STACK_DEFAULT,0);rpc_down=1;rpc_starts=0;worker=1;
    assert(filerDirReadNeedsPrep("mc0:/") && !filerDirReadNeedsPrep("vmc0:/"));
    assert(filerPrepareMemoryCardRead(0)==sceMcResFailDetect && !rpc_starts && !handshakes);
    worker=0;assert(filerPrepareMemoryCardRead(0)==sceMcResSucceed && rpc_starts==1 && handshakes==1);
    assert(filerPrepareMemoryCardRead(0)==sceMcResSucceed && rpc_starts==1);
    /* A retry that fails again leaves the card unready; the next open retries. */
    rpc_down=1;rpc_starts=0;rpc_fail=1;
    assert(!filerPrepareDirRead("mc0:/") && rpc_starts==1 && !isMemoryCardRpcReady() && filerDirReadNeedsPrep("mc0:/"));
    rpc_fail=0;assert(filerPrepareDirRead("mc0:/") && rpc_starts==2 && isMemoryCardRpcReady());
    /* A stale absence/unknown snapshot requires a fresh foreground handshake.
     * No reset is needed with the ordinary driver stack already resident. */
    for(int port=0;port<2;port++)for(int state=0;state<3;state++) {
        reset_case(STORAGE_STACK_DEFAULT,0);
        root_mc_polled[port]=state!=2;root_mc_present[port]=state==1;
        char path[16];snprintf(path,sizeof(path),"mc%d:/",port);
        if(state==1)continue;
        assert(filerDirReadNeedsPrep(path));browse(path);
        assert(waits==1&&handshakes==1&&queued==1&&!resets&&root_mc_present[port]);
        /* All card rows remain actionable even if the picker kept old state. */
        for(int cached=0;cached<4;cached++) {
            const DashDevice card={cached,DI_MC,path};choosePickerDevice(&card);
            assert(picker_queued>0);
        }
    }
    for(int port=0;port<2;port++) {
        pk.active=1;pk.gen=4;pk.entries=calloc(PICK_MAX,sizeof(PickEntry));assert(pk.entries);
        pk.loading=1;PickListJob *failed=calloc(1,sizeof(*failed));assert(failed);
        failed->files=calloc(1,sizeof(FILEINFO));failed->gen=pk.gen;failed->n=-12;
        snprintf(failed->path,sizeof(failed->path),"mc%d:/",port);
        int roots_before=picker_roots,warnings_before=toasts;pickListDone(failed);
        assert(picker_roots==roots_before+1&&toasts==warnings_before+1&&!pk.loading&&!pk.n);
        free(pk.entries);pk.entries=NULL;pk.active=0;
    }
    puts("PASS: failed physical-card listings return the picker to devices with a read error");
    puts("PASS: stale absent/unknown card snapshots trigger foreground checks in either slot; picker accepts all cached card states");
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
