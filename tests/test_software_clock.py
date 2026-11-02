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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef uint64_t u64;
typedef uint8_t u8;
#define MENU_TITLE_CLOCK_POLL_MS 1000
typedef struct {
 int initialized, valid, year, month, day, hour, minute, second;
 int use_12h, date_format, local_offset_minutes;
 u64 last_tick_ms;
} MenuTitleClockState;
static MenuTitleClockState title_clock;
typedef struct { char software_clock[24]; } ClockConfig;
static ClockConfig dashCfg, persisted;
#define SAVE_SETTINGS 0
static int save_dirty[1];
static u64 tick;
static u64 Timer(void) { return tick; }
static char active_software_seed[24];
static MenuTitleClockState settings_clock;
static char settings_software_seed[24];
static int rtc_reads;
static int refreshClockText(int t, int d) { (void)t; (void)d; return 1; }
static int seedClockFromPs2(u64 tick, int t, int d) {
 (void)t; (void)d; rtc_reads++; title_clock.last_tick_ms = tick;
 title_clock.initialized = title_clock.valid = 1; return 1;
}
static void readOsdClockFormats(int *u, int *d, int *o) { *u=0; *d=0; *o=540; }
"""
header = (root / "include/dash_internal.h").read_text()
end = header.index("} DashSettings;") + len("} DashSettings;")
start = header.rfind("typedef struct", 0, end)
settings_type = header[start:end]
c = c.replace("typedef struct { char software_clock[24]; } ClockConfig;", """
#define DASH_PATH_MAX 256
#define DASH_MAX_SCAN 12
#define DASH_MAX_RECENT 16
#define DASH_DEVA_ALL 127
#define DASH_SEC_COUNT 5
#define DASH_START_LAUNCH_KEYS DASH_SEC_COUNT
#define DASH_SEC_HOME 0
#define DASH_VIEW_COUNT 4
#define DASH_VIEW_GRID 0
#define DASH_IGR_AFTER_COUNT 3
#define DASH_IGR_AFTER_DASHBOARD 0
""" + settings_type + "\ntypedef DashSettings ClockConfig;\n")
for sig in ("static int daysInMonth(", "static void advanceClockSeconds(",
            "static int isValidTimestamp(", "static int seedSoftwareClock(", "static int updateClock("):
    c += function(sig)
c += "int menuTitleUpdateAsync(int force) { return updateClock(tick, force, 1, 1); }\n"
for sig in ("void menuTitleClockSnapshot(", "void menuTitleClockRestore(", "int menuTitleSoftwareClockActive(",
            "int menuTitleSoftwareClockCheckpoint("):
    c += function(sig)
c += function("int menuTitleGetClockFields(")
store = (root / "src/dash_store.c").read_text()
a = store.index("int dashStoreSaveSettings(void)")
b = store.index("\n}\n", a) + 3
c += store[a:b]
c += r'''
typedef int (*DashKvCallback)(const char *, char *, void *);
#define stricmp clock_case_compare
static int stricmp(const char *a, const char *b) {
 while (*a && tolower((unsigned char)*a)==tolower((unsigned char)*b)) { a++; b++; }
 return tolower((unsigned char)*a)-tolower((unsigned char)*b);
}
static int dashThemeStoreWrite(char *out,int cap) { (void)out; (void)cap; return 0; }
static int dashThemeStoreKey(const char *key,char *value) { (void)key; (void)value; return 0; }
'''
for sig in ("void dashStrCopy(", "char *dashTrim(", "int dashKvParse(",
            "static int clampInt(", "static int percentSetting(", "static int settingsKv(",
            "char *dashStoreSettingsText("):
    a = store.index(sig)
    pos = store.index("{", a) + 1
    depth = 1
    while depth:
        depth += (store[pos] == "{") - (store[pos] == "}")
        pos += 1
    c += store[a:pos] + "\n"
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
 /* Discard retains an advancing clock, not the old persisted seed. */
 strcpy(dashCfg.software_clock, "2028-06-01 10:00:00");
 tick=100; menuTitleUpdateAsync(1);
 tick=10100; menuTitleClockSnapshot(); assert(title_clock.second==10);
 strcpy(dashCfg.software_clock, "2029-07-02 15:00:00");
 tick=15100; menuTitleUpdateAsync(1); assert(title_clock.year==2029);
 strcpy(dashCfg.software_clock, "2028-06-01 10:00:00");
 tick=20100; menuTitleClockRestore();
 assert(title_clock.year==2028 && title_clock.hour==10 && title_clock.second==20);
 assert(menuTitleSoftwareClockActive());
 tick=30100; dashStoreSaveSettings();
 assert(save_dirty[0] && !strcmp(persisted.software_clock,"2028-06-01 10:00:30"));
 strcpy(dashCfg.software_clock, "malformed");
 assert(!menuTitleSoftwareClockActive());
 dashStoreSaveSettings(); assert(!strcmp(persisted.software_clock,"malformed"));
 dashCfg.software_clock[0]=0; menuTitleClockSnapshot();
 strcpy(dashCfg.software_clock, "2029-07-02 15:00:00"); menuTitleUpdateAsync(1);
 dashCfg.software_clock[0]=0; int before=rtc_reads; menuTitleClockRestore();
 assert(rtc_reads==before+1 && !menuTitleSoftwareClockActive());
 /* Real CNF serialization/parsing and production field getter: no minute subtraction. */
 const char *seeds[] = {"2028-06-01 10:01:20", "2028-02-29 00:00:01", "2028-12-31 23:59:59"};
 for (int seed=0; seed<3; seed++) {
  memset(&dashCfg,0,sizeof(dashCfg)); strcpy(dashCfg.software_clock,seeds[seed]);
  memset(&title_clock,0,sizeof(title_clock)); active_software_seed[0]=0; tick=0;
  int reads=rtc_reads;
  for (int boot=0; boot<3; boot++) {
   char expected[24]; int length,scan_reset=0;
   dashStoreSaveSettings(); strcpy(expected,persisted.software_clock);
   assert(!strcmp(expected,seeds[seed]));
   char *text=dashStoreSettingsText(&persisted,&length);
   assert(text && length==(int)strlen(text) && strstr(text,expected));
   memset(&dashCfg,0,sizeof(dashCfg)); memset(&title_clock,0,sizeof(title_clock));
   active_software_seed[0]=0; tick=0;
   assert(dashKvParse(text,settingsKv,&scan_reset)>0); free(text);
   assert(!strcmp(dashCfg.software_clock,expected) && menuTitleSoftwareClockActive());
   int y,m,d,h,min,sec,u,f; char actual[24];
   assert(menuTitleGetClockFields(&y,&m,&d,&h,&min,&sec,&u,&f));
   snprintf(actual,sizeof(actual),"%04d-%02d-%02d %02d:%02d:%02d",y,m,d,h,min,sec);
   assert(!strcmp(actual,expected) && rtc_reads==reads);
  }
 }
 /* #198: a save adopts the running clock instead of re-seeding it, so the
  * current second keeps its phase and repeated saves never lose time. */
 memset(&dashCfg,0,sizeof(dashCfg)); strcpy(dashCfg.software_clock,"2028-06-01 10:00:00");
 memset(&title_clock,0,sizeof(title_clock)); active_software_seed[0]=0; tick=1000;
 menuTitleUpdateAsync(1); assert(title_clock.second==0);
 for (int i=1; i<=20; i++) { tick=(u64)(i*1000+500); dashStoreSaveSettings(); }
 assert(!strcmp(persisted.software_clock,"2028-06-01 10:00:19"));
 assert(!strcmp(active_software_seed,persisted.software_clock));
 tick=21000; menuTitleUpdateAsync(0);
 assert(title_clock.second==20 && title_clock.last_tick_ms==21000);
 { char untouched[24]="keep"; active_software_seed[0]=0; dashCfg.software_clock[0]=0;
   assert(!menuTitleSoftwareClockCheckpoint(untouched,sizeof(untouched)) && !strcmp(untouched,"keep")); }
 /* A minute of unsaved runtime is not added to the checkpoint on power loss. */
 strcpy(dashCfg.software_clock,"2028-06-01 10:00:20");
 active_software_seed[0]=0; tick=0; dashStoreSaveSettings();
 tick=60000; menuTitleUpdateAsync(0); assert(title_clock.minute==1);
 dashCfg=persisted; active_software_seed[0]=0; memset(&title_clock,0,sizeof(title_clock)); tick=0;
 menuTitleUpdateAsync(0); assert(title_clock.minute==0 && title_clock.second==20);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="software-clock-") as folder:
    path = Path(folder)
    (path / "clock.c").write_text(c)
    exe = path / "clock.exe"
    subprocess.run([shutil.which("gcc") or shutil.which("cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "clock.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
print("PASS: software clock ticks, save keeps sub-second phase (#198), Settings continuity, actual CNF round trips, repeated boots and unsaved-runtime boundary")
