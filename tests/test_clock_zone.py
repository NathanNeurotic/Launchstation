#!/usr/bin/env python3
"""Settings > System > Time zone: Console follows the console's zone and summer time; a chosen zone replaces both."""
import pathlib
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
src = (root / "src/main_title.c").read_text()
store = (root / "src/dash_store.c").read_text()
settings = (root / "src/dash_settings.c").read_text()


def function(signature):
    a = src.index(signature + "(")
    while src[src.index(")", a) + 1] == ";":  # skip a forward declaration
        a = src.index(signature + "(", a + 1)
    b = src.index("{", a) + 1
    depth = 1
    while depth:
        depth += (src[b] == "{") - (src[b] == "}")
        b += 1
    return src[a:b] + "\n"


a = src.index("static const short clock_zones[]")
zones = src[a:src.index("\n", src.index("#define CLOCK_ZONES", a)) + 1]
c = r"""
#include <assert.h>
#include <string.h>
#define MENU_TITLE_PS2_RTC_BASE_OFFSET_MINUTES 540
#define DASH_CLOCK_ZONE_CONSOLE 0x7FFF
typedef struct { unsigned version : 3; unsigned timezoneOffset : 11; } ConfigParam;
typedef struct { int timeFormat, dateFormat, daylightSaving; } Config2Param;
static struct { int clock_zone, clock_summer; } dashCfg;
static ConfigParam console; static int console_summer;
static void GetOsdConfigParam(ConfigParam *p) { *p = console; }
static void GetOsdConfigParam2(Config2Param *p, int size, int offset) { (void)size; (void)offset; p->daylightSaving = console_summer; }
static void readOsdClockFormatsConsole(int *use_12h, int *date_format, int *local_offset_minutes);
""" + zones
for sig in ("int menuTitleClockZoneCount", "int menuTitleClockZoneMinutes", "int menuTitleClockZoneIndex",
            "static int decodeTimezoneOffsetMinutes", "static void readOsdClockFormats", "static void readOsdClockFormatsConsole"):
    c += function(sig)
c += r"""
static int offset(void) { int a, b, o; readOsdClockFormats(&a, &b, &o); return o; }
int main(void) {
    int i;
    console.version = 2; console.timezoneOffset = (unsigned)-300 & 0x7FF; console_summer = 0;
    dashCfg.clock_zone = DASH_CLOCK_ZONE_CONSOLE; dashCfg.clock_summer = 1;
    assert(offset() == -300);            /* Console: the console's zone, the Summer time row unused */
    console_summer = 1; assert(offset() == -240);
    /* A console left on Japan's zone, used in UTC-5, showed a synced clock 14 hours ahead. */
    console.timezoneOffset = 540; console_summer = 0; assert(offset() == 540);
    dashCfg.clock_zone = -300; dashCfg.clock_summer = 0; assert(offset() == -300 && 540 - offset() == 14 * 60);
    dashCfg.clock_summer = 1; assert(offset() == -240);
    console_summer = 1; assert(offset() == -240);   /* the console's summer time is not added to a chosen zone */
    dashCfg.clock_zone = 0; dashCfg.clock_summer = 0; assert(offset() == 0);   /* UTC is a choice */
    dashCfg.clock_zone = 7; console_summer = 0; assert(offset() == 540);      /* off the list: Console */
    console.version = 0; dashCfg.clock_zone = -300; assert(offset() == -300); /* works without a console config */
    assert(menuTitleClockZoneIndex(DASH_CLOCK_ZONE_CONSOLE) == 0 && menuTitleClockZoneMinutes(0) == DASH_CLOCK_ZONE_CONSOLE);
    assert(menuTitleClockZoneCount() <= 39);
    for (i = 1; i <= menuTitleClockZoneCount(); i++) {
        int m = menuTitleClockZoneMinutes(i);
        assert(m >= -720 && m <= 840 && m % 15 == 0 && menuTitleClockZoneIndex(m) == i);
        assert(i == 1 || m > menuTitleClockZoneMinutes(i - 1));
    }
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="clock-zone-") as d:
    path = pathlib.Path(d)
    (path / "zone.c").write_text(c)
    exe = path / "zone.exe"
    subprocess.run([shutil.which("gcc") or shutil.which("cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "zone.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

assert 'clampInt(value, -720, 840, DASH_CLOCK_ZONE_CONSOLE)' in store and '"Dash_ClockZone = %d\\r\\n"' in store
assert "dashCfg.clock_zone = DASH_CLOCK_ZONE_CONSOLE;" in store   # default: as before the setting existed
assert settings.count("menuTitleClockZoneChanged();") == 3         # both rows and Discard
print("clock zone ok")
