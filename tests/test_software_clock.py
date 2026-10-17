"""Execute the production software clock across ticks, force refresh and reboot."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
src = (root / "src/main_title.c").read_text()
def function(signature):
    a = src.index(signature)
    b = src.index("{", a) + 1
    depth = 1
    while depth:
        depth += (src[b] == "{") - (src[b] == "}")
        b += 1
    return src[a:b] + "\n"
c = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define MENU_TITLE_CLOCK_POLL_MS 1000
typedef struct {
 int initialized, valid, year, month, day, hour, minute, second;
 int use_12h, date_format, local_offset_minutes;
 u64 last_tick_ms;
} MenuTitleClockState;
static MenuTitleClockState title_clock;
static struct { char software_clock[24]; } dashCfg;
static char active_software_seed[24];
static int rtc_reads;
static int refreshClockText(int t, int d) { (void)t; (void)d; return 1; }
static int seedClockFromPs2(u64 tick, int t, int d) {
 (void)t; (void)d; rtc_reads++; title_clock.last_tick_ms = tick;
 title_clock.initialized = title_clock.valid = 1; return 1;
}
static void readOsdClockFormats(int *u, int *d, int *o) { *u=0; *d=0; *o=540; }
"""
for sig in ("static int daysInMonth(", "static void advanceClockSeconds(",
            "static int isValidTimestamp(", "static int seedSoftwareClock(", "static int updateClock("):
    c += function(sig)
c += r"""
int main(void) {
 strcpy(dashCfg.software_clock, "2028-02-28 23:59:59");
 updateClock(100, 1, 1, 1);
 assert(title_clock.day == 28 && title_clock.second == 59 && rtc_reads == 0);
 updateClock(2100, 0, 1, 1);
 assert(title_clock.month == 2 && title_clock.day == 29 && title_clock.second == 1);
 updateClock(3100, 1, 1, 1);
 assert(title_clock.second == 2 && rtc_reads == 0);
 updateClock(0, 1, 1, 1); /* timer restart never underflows */
 assert(title_clock.second == 2 && rtc_reads == 0);
 memset(&title_clock, 0, sizeof(title_clock));
 active_software_seed[0] = 0; /* restart resumes persisted seed */
 updateClock(100, 0, 1, 1);
 assert(title_clock.day == 28 && title_clock.second == 59);
 strcpy(dashCfg.software_clock, "2026-02-30 12:00:00");
 updateClock(100, 1, 1, 1); assert(rtc_reads == 1);
 strcpy(dashCfg.software_clock, "2028-02-29 12:00:00 x");
 updateClock(100, 1, 1, 1); assert(rtc_reads == 2);
 dashCfg.software_clock[0] = 0;
 updateClock(100, 1, 1, 1); assert(rtc_reads == 3);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="software-clock-") as folder:
    path = Path(folder)
    (path / "clock.c").write_text(c)
    exe = path / "clock.exe"
    subprocess.run([shutil.which("gcc") or shutil.which("cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "clock.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
print("PASS: software clock leap day, ticks, force refresh, restart and invalid seed fallback")
