#!/usr/bin/env python3
"""DashPause(): a sleep that keeps an open progress popup drawing.

Compiles the real function against a fake clock and checks that it never
returns early, draws only while a popup is open on the dashboard thread,
and keeps short sleeps and the other cases a plain DelayThread.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")
a = ui.index("void DashPause(u32 usec)\n")
b = ui.index("\n}\n", a) + 3
assert "DashPause(delay_us);" in (root / "src/dash_devices.c").read_text(encoding="utf-8")
assert "DashPause(USB_DISCOVERY_SETTLE_MS * 1000);" in (root / "src/filer.c").read_text(encoding="utf-8")
assert (root / "src/mmce_card_switch.c").read_text(encoding="utf-8").count("DashPause(1000000);") == 3
assert "DashPause(1000000);" in (root / "src/neutrino_gameid.c").read_text(encoding="utf-8")
assert "python3 tests/test_dash_pause.py" in (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")

code = r"""
#include <assert.h>
#include <stdio.h>
typedef unsigned int u32;
typedef unsigned long long u64;
static u64 now_us;
static int loading_active, active = 1, on_worker, frames, sleeps;
static u64 Timer(void) { return now_us / 1000; }
static int DashIsActive(void) { return active; }
static int dashWorkerOnThread(void) { return on_worker; }
static void DelayThread(u32 us) { now_us += us; sleeps++; }
/* A frame waits for vsync, so it can end anywhere inside the 16.7 ms. */
static void loadingDraw(void) { frames++; now_us += 16683 - (now_us % 16683); }
""" + ui[a:b] + r"""
static void reset(void) { now_us = 123456; frames = sleeps = 0; loading_active = 1; active = 1; on_worker = 0; }
int main(void) {
    u32 waits[] = {50000, 250000, 500000, 600000, 1000000};
    for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); i++) {
        u64 start;
        reset(); start = now_us;
        DashPause(waits[i]);
        assert(now_us - start >= waits[i]);              /* never early */
        assert(now_us - start <= waits[i] + 2000 + 16683); /* at most one frame late */
        assert(frames >= (int)(waits[i] / 16683) - 1);    /* kept drawing */
    }
    reset(); loading_active = 0; DashPause(600000); assert(!frames && sleeps == 1 && now_us == 123456 + 600000);
    reset(); active = 0; DashPause(600000); assert(!frames && sleeps == 1);
    reset(); on_worker = 1; DashPause(600000); assert(!frames && sleeps == 1);
    reset(); DashPause(16000); assert(!frames && sleeps == 1 && now_us == 123456 + 16000);
    puts("PASS: DashPause never returns early, draws only in an open popup on the UI thread");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="dash-pause-") as folder:
    p = Path(folder)
    (p / "test.c").write_text(code)
    subprocess.run([shutil.which("gcc") or "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
