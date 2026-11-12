"""The USB wait popup keeps one steady line: "Waiting for USB storage... (attempt n/4)".

probeUsbMediaAnimated (src/dash_devices.c) is compiled on the host with stubs. Checked: the first
probe shows "Checking USB media...", every later probe and every wait shows the waiting line with
the next attempt's number, the per-root status lines are held only during those later probes, the
hold is always released, and the progress counts are unchanged."""
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
#define DASH_USB_PROBE_ATTEMPTS 4
#define DASH_USB_PROBE_DELAY_US 600000
#define DASH_DEVA_USB 1
typedef unsigned int u32;
static int quiet, held, probes, pauses, arrive_at = 99;
static char text[128], log_[16][128];
static int isStartupDriverStatusQuiet(void) { return quiet; }
static int dashBootDeviceBit(void) { return DASH_DEVA_USB; }
static int dashLoadingActive(void) { return 1; }
static void dashLoadingHoldProbeStatus(int h) { held = h; }
static void dashLoadingStatus(const char *s, int d, int t) {
    snprintf(text, sizeof(text), "%s", s);
    snprintf(log_[probes], sizeof(log_[0]), "status %s %d/%d", s, d, t);
}
static void dashLoadingFrame(const char *s, int d, int t) {
    snprintf(text, sizeof(text), "%s", s);
    assert(!held);
    assert(d == pauses + 1 && t == 4);
}
static void DashPause(u32 us) { assert(us == 600000 && !held); pauses++; }
static int filerUsbRefreshDriveMask(void) {
    char want[64];
    if (probes == 0) {
        assert(!held && !strcmp(text, "Checking USB media..."));
    } else {
        snprintf(want, sizeof(want), "Waiting for USB storage... (attempt %d/4)", probes + 1);
        assert(held && !strcmp(text, want));
    }
    want[0] = probes > arrive_at;
    probes++;
    return want[0];
}
'''
c += function("src/dash_devices.c", "static int probeUsbMediaAnimated(void)\n{")
c += r'''
int main(void) {
    /* No USB device: four probes, three waits, the line steady from the first wait on. */
    assert(probeUsbMediaAnimated() == 0);
    assert(probes == 4 && pauses == 3 && !held);
    assert(!strcmp(log_[0], "status Checking USB media... 1/4"));
    assert(!strcmp(log_[1], "status Waiting for USB storage... (attempt 2/4) 2/4"));
    assert(!strcmp(log_[3], "status Waiting for USB storage... (attempt 4/4) 4/4"));
    assert(!strcmp(text, "Waiting for USB storage... (attempt 4/4)"));

    /* A drive found on the second probe: quiet startup accepts it at once, hold released. */
    probes = pauses = 0; arrive_at = 0; quiet = 1;
    assert(probeUsbMediaAnimated() == 1);
    assert(probes == 2 && pauses == 1 && !held);
    puts("PASS: steady USB wait text with attempt counter; probe lines held only during later probes");
    return 0;
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="usb-wait-text-") as tmp:
    path = Path(tmp) / "test.c"
    exe = Path(tmp) / "test.exe"
    path.write_text(c)
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
