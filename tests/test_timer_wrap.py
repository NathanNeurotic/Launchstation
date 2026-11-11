#!/usr/bin/env python3
"""Timer(): milliseconds since boot that keep counting past 71.6 minutes.

clock() on the EE is a 32-bit microsecond count that wraps every 2^32 us, so
a Timer() built on it dropped back to 0 and every `deadline = Timer() + n`
loop hung (or a `Timer() - start` check ended early). Compiles the real
function against a fake 64-bit bus clock and checks it stays monotonic and a
deadline loop ends across the old wrap point.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/timer.c").read_text(encoding="utf-8")
a = src.index("u64 Timer(void)\n")
b = src.index("\n}\n", a) + 3

code = r"""
#include <assert.h>
#include <stdio.h>
typedef unsigned long long u64;
#define kBUSCLK (147456000)
static u64 bus;
static u64 GetTimerSystemTime(void) { return bus; }
""" + src[a:b] + r"""
int main(void) {
    const u64 wrap_ms = 4294967296ull / 1000; /* where clock()/1000 used to wrap */
    u64 starts[] = {0, wrap_ms - 5000, wrap_ms - 1, 3 * wrap_ms - 7};
    for (unsigned k = 0; k < sizeof(starts) / sizeof(starts[0]); k++) {
        bus = starts[k] * (kBUSCLK / 1000);
        u64 t0 = Timer(), deadline = t0 + 10000, prev = t0;
        assert(t0 == starts[k]);
        int steps = 0;
        while (Timer() < deadline) {
            bus += kBUSCLK / 1000 * 100; /* 100 ms per pass */
            assert(Timer() > prev);
            prev = Timer();
            assert(++steps <= 100); /* ends on time, never hangs */
        }
        assert(steps == 100 && Timer() - t0 == 10000);
    }
    puts("PASS: Timer() keeps counting past the 32-bit clock() wrap; deadline loops end on time");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="timer-wrap-") as folder:
    p = Path(folder)
    (p / "test.c").write_text(code)
    subprocess.run([shutil.which("gcc") or "cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
