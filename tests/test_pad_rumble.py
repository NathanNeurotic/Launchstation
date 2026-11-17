"""Opt-in controller vibration (src/pad_rumble.c, ported from RiptOPL).

Compiles the real pad_rumble.c against libpad stubs and checks: off means no
pad traffic, alignment waits for an idle controller, each motor change is
sent a bounded number of times, a pulse always ends with stops, and an
unplugged pad is re-aligned. Also checks the setting is wired and off by
default.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
assert "python3 tests/test_pad_rumble.py" in (root / ".github/workflows/compile.yml").read_text()
assert "- src/pad_rumble.c" in (root / "ps2.yaml").read_text()
for pad in ("src/pad.c", "src/pad_ds34.c"):
    text = (root / pad).read_text()
    assert text.count("padRumbleTick(paddata);") == 2, pad
    assert "padRumbleReset();" in text, pad
assert (root / "src/pad_ds34.c").read_text().count("padRumbleLevel(), padRumbleLevel()") == 2
store = (root / "src/dash_store.c").read_text()
assert "Dash_Vibration" in store and "padRumbleEnable(dashCfg.rumble);" in store
assert "dashCfg.rumble = 1" not in store  # default stays off (memset in dashStoreDefaults)
settings = (root / "src/dash_settings.c").read_text()
assert "S_RUMBLE, \"Vibration\"" in settings
assert "padRumbleFlush();" in (root / "src/dash_worker.c").read_text()
assert "padRumbleFlush();" in (root / "src/main_actions.c").read_text()

stub = r'''
#include <assert.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned long long u64;
#define PAD_STATE_DISCONN 0
#define PAD_STATE_FINDCTP1 2
#define PAD_STATE_STABLE 6
#define PAD_RSTAT_BUSY 1
static u64 now_ms; static int state[2] = {PAD_STATE_STABLE, PAD_STATE_DISCONN};
static int acts = 4, aligns, sends, last_small, last_large;
u64 Timer(void) { return now_ms; }
int padGetState(int p, int s) { (void)s; return state[p]; }
int padGetReqState(int p, int s) { (void)p; (void)s; return 0; }
int padInfoAct(int p, int s, int a, int b) { (void)p; (void)s; (void)a; (void)b; return acts; }
int padSetActAlign(int p, int s, char *a) { (void)p; (void)s; assert(a[0] == 0 && a[1] == 1); aligns++; return 1; }
int padSetActDirect(int p, int s, char *a) { (void)p; (void)s; sends++; last_small = a[0]; last_large = (unsigned char)a[1]; return 1; }
'''
main = r'''
static void tick(u32 held) { padRumbleTick(held); now_ms += 16; }
int main(void) {
    int i;
    /* off: nothing armed, nothing sent */
    padRumbleReset();
    padRumbleNav();
    for (i = 0; i < 200; i++) tick(0);
    assert(aligns == 0 && sends == 0 && padRumbleLevel() == 0);

    /* on: alignment only after a quiet second, never while a button is held */
    padRumbleEnable(1);
    for (i = 0; i < 100; i++) tick(1);
    assert(aligns == 0);
    for (i = 0; i < 70; i++) tick(0);
    assert(aligns == 1);

    /* a decision pulse: 1 + 3 "on" sends, then silence, then stops */
    padRumbleDecision();
    assert(padRumbleLevel() == 0x60);
    for (i = 0; i < 4; i++) tick(0);
    assert(sends == 4 && last_small == 1 && last_large == 0x60);
    tick(0);
    assert(sends == 4);
    for (i = 0; i < 10; i++) tick(0);
    assert(last_small == 0 && last_large == 0 && sends == 4 + 2 + 3);
    assert(padRumbleLevel() == 0);

    /* turning it off mid-pulse stops the motor at once */
    padRumbleDecision();
    tick(0);
    sends = 0;
    padRumbleEnable(0);
    assert(sends == 2 && last_small == 0);
    for (i = 0; i < 10; i++) tick(0);
    assert(sends == 5 && last_small == 0);

    /* unplugged and replugged: aligned again after the next quiet second */
    padRumbleEnable(1);
    state[0] = PAD_STATE_DISCONN; tick(0);
    state[0] = PAD_STATE_STABLE;
    for (i = 0; i < 70; i++) tick(0);
    assert(aligns == 2);

    /* fast cursor repeats faster than the motor can answer stay silent */
    padRumbleNav();
    for (i = 0; i < 40; i++) tick(0);
    sends = 0;
    padRumbleNav(); now_ms += 50; padRumbleNav();
    assert(padRumbleLevel() == 0x50);
    now_ms += 200;                        /* past the first 240 ms, before a restarted one */
    assert(padRumbleLevel() == 0);

    /* IOP parked: the input loop flushes every poll and never ticks. A pulse
     * gets its stops, then the flushes go quiet instead of re-arming forever. */
    padRumbleDecision();
    tick(0);
    sends = 0;
    for (i = 0; i < 20; i++) padRumbleFlush();
    assert(sends == 4 && last_small == 0);
    for (i = 0; i < 10; i++) tick(0);
    assert(sends == 4);
    return 0;
}
'''
header = (root / "include/launchelf.h").read_text()
protos = header[header.index("/* pad_rumble.c"):header.index("int padRumbleLevel(void);") + len("int padRumbleLevel(void);")]
code = (root / "src/pad_rumble.c").read_text().replace('#include "launchelf.h"', stub + protos)
code = code.replace('#include "dash_sound.h"', (root / "include/dash_sound.h").read_text())
with tempfile.TemporaryDirectory(prefix="rumble-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code + main)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: vibration is off by default, aligns when idle, and always ends with stops")
