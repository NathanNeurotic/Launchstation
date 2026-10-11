"""Execute Launchstation's generic-USB-root selection against mocked mount timing.

The fast path must avoid duplicate synchronous root stat probes only when
both mounts were just verified; late, absent, and stale media still rescan.
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
    /* With only one unit cached, keep the full late-second-device window. */
    clear_state();
    USB_mass_scanned=1;USB_mass_ix[0]='0';USB_mass_scan_time=now_ms;
    present_at_start=1;late_second_at=11000;
    assert(prepareUsbRootBrowse()==0 && scans==3 && waits==2);
    assert(USB_mass_ix[1]=='1');
    clear_state();
    present_at_start=1;
    assert(prepareUsbRootBrowse()==0 && scans==3 && waits==2);
    clear_state();
    present_at_start=2;
    assert(prepareUsbRootBrowse()==1 && scans==3 && waits==2);
    clear_state();
    assert(prepareUsbRootBrowse()==-1 && scans==3 && waits==2);
    /* A stale two-drive cache must not mask an actual disconnect. */
    clear_state();
    USB_mass_scanned=1;USB_mass_ix[0]='0';USB_mass_ix[1]='1';
    USB_mass_scan_time=now_ms-1000;
    assert(prepareUsbRootBrowse()==-1 && scans==3 && waits==2);
    puts("PASS: fresh two-drive root reuse; stale, single, delayed and missing drives rescan");
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
