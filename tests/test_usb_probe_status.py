"""Verify pre-RPC diagnostics and that worker reporting never renders."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text()
    begin = source.index(signature)
    end = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end] + "\n"


c = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int active=1, worker, loading_active=1, frames, reports, stats;
static char last[128];
static int DashIsActive(void) { return active; }
static int isStartupDriverStatusQuiet(void) { return 0; }
static int dashWorkerOnThread(void) { return worker; }
static void dashWorkerStatus(const char *s,int d,int t) {
    (void)d;(void)t; reports++; snprintf(last,sizeof(last),"%s",s);
}
static void dashLoadingFrame(const char *s,int d,int t) {
    (void)d;(void)t; assert(!worker); frames++; snprintf(last,sizeof(last),"%s",s);
}
'''
c += function("src/dash_ui.c", "void DashStorageProbeStatus(")
c += r'''
#define EXFAT 1
#define DPRINTF(...) ((void)0)
#define USB_BROWSER_MAX_DRIVES 2
typedef int iox_stat_t;
static int USB_mass_loaded=1, USB_mass_scanned, USB_mass_max_drives=2;
static int USB_mass_ix[10];
static int drains, display_invalidations;
static void filerInvalidateUsbMassDisplayAliases(void) {display_invalidations++;}
static void filerRootMcPollIdle(void) { drains++; }
static unsigned long USB_mass_scan_time;
static unsigned long Timer(void) { return 10000; }
static void loadUsbModules(void) { assert(0); }
static int fileXioGetStat(const char *p,iox_stat_t *s) {
    char expected[80]; (void)s;
    snprintf(expected,sizeof(expected),"Checking USB root %s",p);
    assert(!strcmp(last,expected)); /* already presented before entry */
    assert(drains == stats/2 + 1); /* pending libmc was drained before scan */
    stats++; return p[3]=='0' ? 0 : -19;
}
'''
c += function("src/filer.c", "static void scanUsbMassDevices(int force)\n{")
c += r'''
int main(void) {
    scanUsbMassDevices(1);
    assert(stats==2 && drains==1 && frames==4 && reports==0 && display_invalidations==1);
    assert(USB_mass_ix[0]=='0' && !USB_mass_ix[1]);
    assert(!strcmp(last,"USB root usb1:/ returned -19"));
    worker=1; scanUsbMassDevices(1);
    assert(stats==4 && drains==2 && frames==4 && reports==4 && display_invalidations==2);
    worker=0; loading_active=0;
    DashStorageProbeStatus("no overlay"); assert(frames==4);
    active=0; worker=1;
    DashStorageProbeStatus("classic"); assert(reports==4);
    puts("PASS: pre-RPC USB slot/result status; worker and classic render isolation");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="usb-probe-status-") as tmp:
    path = Path(tmp) / "test.c"
    exe = Path(tmp) / "test.exe"
    path.write_text(c)
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
