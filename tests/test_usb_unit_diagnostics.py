"""Run production per-USB-unit diagnostic action under mocked IOP/fileXio.

Must never enumerate the opposite logical USB unit, refresh cached mount data,
or make any filesystem RPC without explicit consent and an idle worker.
Runs under both EXFAT (usbN:/) and legacy (massN:/) paths.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_tools.c").read_text()
sig = "static void dashToolsUsbUnitDiagnostics(void)\n{"
start = source.index(sig)
pos = source.index("{", start) + 1
depth = 1
while depth:
    depth += (source[pos] == "{") - (source[pos] == "}")
    pos += 1
function = source[start:pos] + "\n"
assert "TA_USB_DIAG" in source
assert "case TA_USB_DIAG:" in source
assert "dashToolsUsbUnitDiagnostics();" in source
assert "USB unit diagnostics" in source
assert "filerUsbRefreshDriveMask()" not in function
assert "scanUsbMassDevices" not in function
assert function.index("dashWorkerAwaitAll(") < function.index("filerRootMcPollIdle();")
assert function.index("dashLoadingFrame(status, 0, 0)") < function.index("fileXioGetStat(path, &stat)")

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DPRINTF(...) ((void)0)
enum {DI_INFO=1, DI_CHECK=2, DI_WARNING=3};
typedef struct { int unused; } iox_stat_t;
static int choice, allowed=1, worker_idle=1, loaded=1, load_succeeds=1;
static int cache_mask=3, count_stat, count_load, count_drain, count_frames;
static int count_end, count_begin, count_modal, count_confirm, count_wait;
static int probe_return, expected_unit=-1;
static char last_frame[96], last_title[96], last_body[256];

static void reset(void) {
    choice=-1; allowed=worker_idle=loaded=load_succeeds=1;
    cache_mask=3;
    count_stat=count_load=count_drain=count_frames=0;
    count_end=count_begin=count_modal=count_confirm=count_wait=0;
    probe_return=0;expected_unit=-1;
    last_frame[0]=last_title[0]=last_body[0]=0;
}
static int dashModalChoice(const char *title,const char *const *labels,int n,int current) {
    assert(!strcmp(title,"USB unit diagnostics"));
    assert(n==3 && current==0);
    assert(strstr(labels[0],"cached") && strstr(labels[1],"unit 0")
           && strstr(labels[2],"unit 1"));
    return choice;
}
static int dashModalConfirm(const char *title,const char *body,const char *label,int danger) {
    assert(!strcmp(title,"Probe one USB unit?"));
    assert(strstr(body,"may freeze") && !strcmp(label,"Probe once") && !danger);
    count_confirm++;
    return allowed;
}
static int dashWorkerAwaitAll(const char *name) {
    assert(strstr(name,"background"));
    count_wait++;
    return worker_idle;
}
static void filerRootMcPollIdle(void) {
    assert(count_wait==1 && count_stat==0);
    count_drain++;
}
static int filerUsbDriversLoaded(void) { return loaded; }
static int isUsbMassStackLoaded(void) { return loaded; }
static void loadUsbModules(void) {
    assert(count_stat==0 && count_drain==1);
    count_load++;
    if(load_succeeds)loaded=1;
}
static int filerUsbDriveMask(void) {
    assert(count_stat==0 && count_drain==0);
    return cache_mask;
}
static void dashLoadingBegin(const char *title) {
    assert(strstr(title,"USB diagnostic"));
    count_begin++;
}
static void dashLoadingFrame(const char *status,int done,int total) {
    assert(done==0 && total==0);
    assert(count_drain==1 && loaded);
    count_frames++;
    snprintf(last_frame,sizeof(last_frame),"%s",status);
}
static void dashLoadingEnd(void) { count_end++; }
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail) {
    assert(icon==DI_INFO||icon==DI_CHECK||icon==DI_WARNING);
    (void)detail;
    count_modal++;
    snprintf(last_title,sizeof(last_title),"%s",title);
    snprintf(last_body,sizeof(last_body),"%s",body);
}
static int fileXioGetStat(const char *path,iox_stat_t *out) {
    char expected[24],heading[72];
    assert(out!=NULL && count_drain==1 && count_frames==1 && count_load<=1);
    assert(expected_unit==0||expected_unit==1);
#ifdef EXFAT
    snprintf(expected,sizeof(expected),"usb%d:/",expected_unit);
#else
    snprintf(expected,sizeof(expected),"mass%d:/",expected_unit);
#endif
    snprintf(heading,sizeof(heading),"Checking USB root %s",expected);
    assert(!strcmp(path,expected) && !strcmp(last_frame,heading));
    count_stat++;
    assert(count_stat==1); /* Never probe the other unit implicitly. */
    return probe_return;
}
'''
code += function
code += r'''
int main(void) {
    reset();dashToolsUsbUnitDiagnostics();
    assert(!count_stat && !count_modal && !count_confirm);
    reset();choice=0;loaded=0;
    dashToolsUsbUnitDiagnostics();
    assert(!count_load&&!count_stat&&!count_drain);
    assert(strstr(last_body,"not loaded")!=NULL);
    reset();choice=0;cache_mask=3;
    dashToolsUsbUnitDiagnostics();
    assert(!count_stat&&!count_load&&!count_wait);
    assert(strstr(last_body,"unit 0, unit 1")!=NULL);
    reset();choice=0;cache_mask=0;
    dashToolsUsbUnitDiagnostics();
    assert(!count_stat&&strstr(last_body,"none confirmed"));
    reset();choice=1;allowed=0;
    dashToolsUsbUnitDiagnostics();
    assert(!count_stat&&!count_wait&&!count_load&&!count_drain);
    reset();choice=1;worker_idle=0;
    dashToolsUsbUnitDiagnostics();
    assert(count_confirm==1&&count_wait==1&&!count_stat&&!count_load);
    reset();choice=1;loaded=0;load_succeeds=0;
    dashToolsUsbUnitDiagnostics();
    assert(count_load==1 && count_drain==1 && count_stat==0);
    assert(count_begin==1 && count_end==1);
    assert(strstr(last_title,"driver unavailable")!=NULL);
    reset();choice=1;expected_unit=0;loaded=0;
    dashToolsUsbUnitDiagnostics();
    assert(count_load==1 && count_stat==1 && count_frames==1);
    assert(count_begin==1 && count_end==1);
    assert(strstr(last_body,"returned 0")!=NULL);
    reset();choice=2;expected_unit=1;probe_return=-19;
    dashToolsUsbUnitDiagnostics();
    assert(count_load==0&&count_stat==1&&count_frames==1);
    assert(strstr(last_body,"returned -19")!=NULL);
    puts("PASS: user-selected single USB unit only, no implicit dual scan, safe worker/pad sequencing");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler,"Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-usb-unit-diag-") as tmp:
    src = Path(tmp)/"check.c"
    src.write_text(code)
    for label,flags in (("fatfs",["-DEXFAT"]),("legacy",[])):
        binary=Path(tmp)/label
        subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                        *flags,str(src),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=5)
