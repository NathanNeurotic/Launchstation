"""Execute Launchstation's generic-USB-root selection against mocked mount timing.

The fast path must avoid duplicate synchronous root stat probes only when
both mounts were just verified. One mounted unit opens the root without
waiting for a second drive (#277 boot time): late arrivals are left for the
entry rescan, which is no longer freshness-blocked while a drive is missing.
Absent and stale media still rescan with the full attempt window.
No real IOP or USB timeout is simulated.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/"src/filer.c").read_text()
signature="int prepareUsbRootBrowse(void)\n{"
start=source.index(signature)
end=source.index("{",start)+1
depth=1
while depth:
    depth+=(source[end]=="{")-(source[end]=="}")
    end+=1
function=source[start:end]+"\n"
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;
#define USB_DISCOVERY_ATTEMPTS 3
#define USB_DISCOVERY_SETTLE_MS 500
#define USB_BROWSER_MAX_DRIVES 2
static u64 now_ms=10000, USB_mass_scan_time;
static int USB_mass_scanned,USB_mass_ix[10];
static int scans,waits,present_at_start,late_second_at=-1;
static u64 Timer(void) { return now_ms; }
static void scanUsbMassDevices(int force) {
    int present=present_at_start;
    assert(force==1);
    scans++;
    if(late_second_at>=0 && now_ms >= (u64)late_second_at) present|=2;
    USB_mass_ix[0]=(present&1)?'0':0;
    USB_mass_ix[1]=(present&2)?'1':0;
    USB_mass_scanned=present!=0;
    if(present)USB_mass_scan_time=now_ms;
}
static void waitUsbDiscoverySettle(void) {
    waits++;now_ms+=USB_DISCOVERY_SETTLE_MS;
}
'''
program+=function
program+=r'''
static void clear_state(void) {
    now_ms=10000;
    USB_mass_scanned=0;
    USB_mass_ix[0]=USB_mass_ix[1]=0;
    USB_mass_scan_time=0;
    scans=waits=0;
    present_at_start=0;
    late_second_at=-1;
}
int main(void) {
    clear_state();
    USB_mass_scanned=1;
    USB_mass_ix[0]='0'; USB_mass_ix[1]='1';
    USB_mass_scan_time=now_ms;
    assert(prepareUsbRootBrowse()==0 && scans==0 && waits==0);
    /* Exactly at the freshness boundary remains validated. */
    now_ms+=USB_DISCOVERY_SETTLE_MS;
    assert(prepareUsbRootBrowse()==0 && scans==0 && waits==0);
    now_ms++;
    present_at_start=3;
    assert(prepareUsbRootBrowse()==0 && scans==1 && waits==0);
    /* IOP reset or timer discontinuity must never trust a future scan. */
    clear_state();
    USB_mass_scanned=1;USB_mass_ix[0]='0';USB_mass_ix[1]='1';
    USB_mass_scan_time=now_ms+500;
    present_at_start=3;
    assert(prepareUsbRootBrowse()==0 && scans==1);
    /* With only one unit mounted, open the root immediately: a late second
     * drive is found by the next entry's rescan instead of a boot wait. */
    clear_state();
    USB_mass_scanned=1;USB_mass_ix[0]='0';USB_mass_scan_time=now_ms;
    present_at_start=1;late_second_at=11000;
    assert(prepareUsbRootBrowse()==0 && scans==1 && waits==0);
    assert(USB_mass_ix[1]==0);
    now_ms=11000;
    assert(prepareUsbRootBrowse()==0 && scans==2 && waits==0);
    assert(USB_mass_ix[1]=='1');
    clear_state();
    present_at_start=1;
    assert(prepareUsbRootBrowse()==0 && scans==1 && waits==0);
    clear_state();
    present_at_start=2;
    assert(prepareUsbRootBrowse()==1 && scans==1 && waits==0);
    clear_state();
    assert(prepareUsbRootBrowse()==-1 && scans==3 && waits==2);
    /* A stale two-drive cache must not mask an actual disconnect. */
    clear_state();
    USB_mass_scanned=1;USB_mass_ix[0]='0';USB_mass_ix[1]='1';
    USB_mass_scan_time=now_ms-1000;
    assert(prepareUsbRootBrowse()==-1 && scans==3 && waits==2);
    puts("PASS: fresh two-drive root reuse; single drive opens without waiting, late/absent/stale rescan");
}
'''
compiler=shutil.which("gcc") or shutil.which("clang")
assert compiler,"Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-usb-browse-cache-") as tmp:
    p=Path(tmp)/"test.c"
    exe=Path(tmp)/"test"
    p.write_text(program)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    str(p),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)

# The freshness guard applies only to non-forced scans, so the mocked scanner
# above cannot regress it. Exercise the production scanner with a mocked
# fileXioGetStat: a fresh single-drive cache must re-probe and discover a
# second unit; a fresh complete cache must skip the probes entirely.
def extract(sig):
    start=source.index(sig)
    end=source.index("{",start)+1
    depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}")
        end+=1
    return source[start:end]+"\n"

scanner=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define DPRINTF(...) ((void)0)
#define USB_BROWSER_MAX_DRIVES 2
typedef struct { int unused; } iox_stat_t;
static u64 now_ms=10000, USB_mass_scan_time;
static int USB_mass_scanned, USB_mass_ix[10];
static int USB_mass_loaded=1, USB_mass_max_drives=2;
static int stat_calls, present, load_calls;
static u64 Timer(void) { return now_ms; }
static void filerRootMcPollIdle(void) {}
static void loadUsbModules(void) { load_calls++; }
static void DashStorageProbeStatus(const char *s) { (void)s; }
static int fileXioGetStat(const char *path, iox_stat_t *st) {
    char expected[8];
    (void)st;
#ifdef EXFAT
    snprintf(expected,sizeof(expected),"usb%d:/",stat_calls);
#else
    snprintf(expected,sizeof(expected),"mass%d:/",stat_calls);
#endif
    assert(!strcmp(path,expected));
    return (present & (1<<stat_calls++)) ? 0 : -1;
}
'''
scanner+=extract("static void scanUsbMassDevices(int force)\n{")
scanner+=extract("void scan_USB_mass(void)\n{")
scanner+=r'''
static void fresh_single(void) {
    now_ms=10000; stat_calls=0; present=1; load_calls=0;
    USB_mass_loaded=1; USB_mass_max_drives=2;
    USB_mass_scanned=1; USB_mass_ix[0]='0'; USB_mass_ix[1]=0;
    USB_mass_scan_time=now_ms;
}
int main(void) {
    /* fresh single-drive cache: the entry rescan finds the late second unit */
    fresh_single();
    present=3;
    scan_USB_mass();
    assert(stat_calls==2 && USB_mass_ix[0]=='0' && USB_mass_ix[1]=='1');
    assert(USB_mass_scanned==1 && USB_mass_scan_time==now_ms);
    /* fresh complete cache: freshness skip, no probes at all */
    stat_calls=0;
    scan_USB_mass();
    assert(stat_calls==0);
    /* stale complete cache still re-probes */
    USB_mass_scan_time=now_ms-6000;
    scan_USB_mass();
    assert(stat_calls==2);
    /* a forced scan ignores freshness */
    USB_mass_scan_time=now_ms;
    stat_calls=0;
    scanUsbMassDevices(1);
    assert(stat_calls==2);
    /* single-drive reporting limit never probes unit 1 */
    fresh_single();
    USB_mass_max_drives=1;
    scan_USB_mass();
    assert(stat_calls==0);
    /* driver load failure: force clears the table, no probes */
    fresh_single();
    USB_mass_loaded=0;
    scanUsbMassDevices(1);
    assert(load_calls==1 && stat_calls==0 && !USB_mass_scanned && !USB_mass_ix[0]);
    puts("PASS: production scanner re-probes a fresh single-drive cache, skips a fresh complete one");
}
'''
with tempfile.TemporaryDirectory(prefix="launchstation-usb-scanner-") as tmp:
    src=Path(tmp)/"scanner.c"
    src.write_text(scanner)
    for label,flags in (("fatfs",["-DEXFAT"]),("legacy",[])):
        exe=Path(tmp)/label
        subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                        *flags,str(src),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=5)
