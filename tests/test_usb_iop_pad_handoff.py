"""Execute production IOP/PAD reset policy against ordered libmc/PAD mocks."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/init.c").read_text(encoding="utf-8")


def function(sig):
    begin = source.index(sig)
    pos = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[begin:pos] + "\n"


body = function("static void resetRuntimeDeviceState(int show_status)\n{")
assert body.index("filerRootMcPollIdle();") < body.index("padPortClose(1, 0)")
assert body.index("filerRootMcPollIdle();") < body.index("Reset();")
assert body.index("padEnd();") < body.index("Reset();") < body.index("setupPad()")
assert body.index("iopResetNotify(0)") < body.index("dashSoundShutdown();") < body.index("Reset();")
assert "iopResetBlocked()" in body
assert "clearPadPressState()" in body

c = r"""
#include <assert.h>
#include <stdio.h>
static int order[32], count, blocked;
static void log_event(int what) { assert(count<32); order[count++]=what; }
#define DPRINTF(...) ((void)0)
#define TRUE 1
#define FALSE 0
static int workerBlock(void) { return blocked; }
static int (*iopResetBlocked)(void)=workerBlock;
static void resetNotify(int phase) { log_event(phase?12:2); }
static void (*iopResetNotify)(int)=resetNotify;
static void filerRootMcPollIdle(void) { log_event(1); }
static void dashSoundShutdown(void) { log_event(14); }
static void closeKeyboardIfOpened(void) { log_event(3); }
static int padPortClose(int p, int slot) { (void)slot; log_event(p?4:5); return 1; }
static void padEnd(void) { log_event(6); }
static void clearPadPressState(void) { log_event(7); }
static void showRebootingIopMsg(void) { log_event(8); }
static void unmountAll(void) { log_event(9); }
static void Reset(void) { log_event(10); }
static int setupPad(void) { log_event(11); return 1; }
static void startKbd(void) { log_event(13); }
"""
c += body
c += r"""
int main(void) {
    blocked=1;
    resetRuntimeDeviceState(TRUE);
    assert(count==0); /* background workers cannot destroy the pad IOP */
    blocked=0;
    resetRuntimeDeviceState(TRUE);
    int expected[]={1,2,14,3,4,5,6,7,8,9,10,11,7,13,12};
    assert(count==(int)(sizeof(expected)/sizeof(expected[0])));
    for (int i=0;i<count;i++) assert(order[i]==expected[i]);
    count=0;
    resetRuntimeDeviceState(FALSE);
    assert(count==14 && order[0]==1 && order[1]==2 && order[2]==14);
    for(int i=0;i<count;i++) assert(order[i]!=8);
    puts("PASS: root-card drain, audsrv teardown, PAD teardown and IOP reset sequencing");
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="usb-pad-reset-") as tmp:
    path = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    path.write_text(c, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
