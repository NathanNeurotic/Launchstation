"""Execute the production root memory-card presence poll with a mocked libmc.

Regression: a slot whose libmc requests keep failing (for example slot 2 while
the MX4SIO stack owns it) stayed "Checking..." forever, could not be opened
from the dashboard and also stopped slot 1 from being re-polled.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
filer = (root / "src/filer.c").read_text(encoding="utf-8")
files = (root / "src/dash_files.c").read_text(encoding="utf-8")
devices = (root / "src/dash_devices.c").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


state = "\n".join(line for line in filer.splitlines()
                  if line.startswith(("#define ROOT_MC_", "static int root_mc_", "static u64 root_mc_"))) + "\n"
code = r'''
#include <assert.h>
#include <stdio.h>
#define TRUE 1
#define FALSE 0
typedef unsigned long long u64;
static u64 now_ms;
static u64 Timer(void) { return now_ms; }
static int fail_port1_start, fail_port1_sync, pending_port = -1;
static int mcGetInfo(int port, int slot, int *type, int *free, int *format) {
    (void)slot; (void)format;
    if (port == 1 && fail_port1_start) return -1;
    *type = 2; *free = 4096; pending_port = port; return 0;
}
static int mcSync(int mode, int *cmd, int *result) {
    (void)mode; (void)cmd;
    if (pending_port == 1 && fail_port1_sync) { pending_port = -1; return -1; }
    pending_port = -1; *result = 0; return 1;
}
static void DashStorageProbeStatus(const char *s) { (void)s; }
#define DPRINTF(...) ((void)0)
'''
code += state
code += function(filer, "static int memoryCardResultIndicatesPresent(")
code += function(filer, "static void rootMcPollFailed(")
code += function(filer, "int pollRootMemoryCardDevices(")
code += function(filer, "int filerRootMcPresent(")
code += function(filer, "void filerRootMcPollIdle(void)\n{")
code += r'''
static void run(int steps) { int i; for (i = 0; i < steps; i++) { pollRootMemoryCardDevices(); now_ms += 600; } }
int main(void) {
    /* Slot 2 never answers its sync: slot 1 still resolves and slot 2 stops
     * reporting "not polled yet" (-1) after the failure limit. */
    fail_port1_sync = 1;
    run(40);
    assert(filerRootMcPresent(0, NULL, NULL) == 1);
    assert(filerRootMcPresent(1, NULL, NULL) == -2);
    /* The slot recovers as soon as libmc answers again. */
    fail_port1_sync = 0;
    run(8);
    assert(filerRootMcPresent(1, NULL, NULL) == 1);

    /* Same for a request that cannot even be started. */
    fail_port1_start = 1;
    run(40);
    assert(filerRootMcPresent(0, NULL, NULL) == 1);
    assert(filerRootMcPresent(1, NULL, NULL) == 1); /* last known state kept */
    puts("PASS: an unresponsive memory-card slot cannot starve the other or stay Checking forever");
    return 0;
}
'''

compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-mc-poll-") as tmp:
    harness = Path(tmp) / "poll.c"
    exe = Path(tmp) / "poll.exe"
    harness.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=gnu11", str(harness), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The dashboard must let the user open an unresolved slot: browsing runs
# ensureMemoryCardPortAccessible(), which switches the IOP away from MX4SIO.
selectable = function(files, "static int devSelectable(")
assert "DASH_DEV_BUSY" in selectable and "DI_MC" in selectable
assert "present == -2" in devices
print("PASS: unresolved memory-card slots stay selectable from Devices")
