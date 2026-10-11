"""Run the real Home rebuild logic against a favorite reorder on the host."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (subprocess.check_output(["git", "show", f"{sys.argv[1]}:src/dash_home.c"], cwd=root, text=True)
          if len(sys.argv) > 1 else (root / "src/dash_home.c").read_text())


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
#define DASH_PATH_MAX 128
#define DASH_TITLE_MAX 64
#define HOME_MAX_APPS 24
#define HOME_MAX_CHIPS 24
#define DASH_APP_META_PIN 1
#define SETTING_LK_BTN_COUNT 3
#define SETTING_LK_LEFT 1
#define SETTING_LK_RIGHT 2
#define stricmp strcmp
#define CHIP_APPS 1
#define CHIP_FILES 2
#define CHIP_LIBRARY 0
#define CHIP_LAST_FOLDER 1
#define DI_DISC 1
#define DI_KEY 2
#define CHIP_DISC 3
#define CHIP_DEVICES 4
#define CHIP_TOOLS 5
#define CHIP_SETTINGS 6
#define CHIP_LAUNCHKEY 7
#define DI_APPS 3
#define DI_CLOCK 8
#define DI_FOLDER 4
#define DI_DEVICES 5
#define DI_TOOLS 6
#define DI_SETTINGS 7
typedef struct { char path[DASH_PATH_MAX]; int flags; } DashApp;
typedef struct { char pin[8][DASH_PATH_MAX], recent[8][DASH_PATH_MAX], fav[8][DASH_PATH_MAX]; int npin, nrecent, nfav; } Lists;
typedef struct { char LK_Path[SETTING_LK_BTN_COUNT][DASH_PATH_MAX]; char LK_Title[SETTING_LK_BTN_COUNT][DASH_TITLE_MAX]; int LK_Flag[SETTING_LK_BTN_COUNT]; } Setting;
static Lists dashLists;
static struct { int recent_count; int home_shortcuts; } dashCfg = {8,1};
static Setting setting_data;
static Setting *setting = &setting_data;
static char LastDir[DASH_PATH_MAX];
static DashApp dashApps[8], home_tmp[HOME_MAX_APPS], *home_apps[HOME_MAX_APPS];
static u8 home_badge[HOME_MAX_APPS];
static int dashAppCount, nhome, nchips, row, row_forced, col[2];
static float col_anim[2], cam[2];
static const char *const lk_names[SETTING_LK_BTN_COUNT] = {"Auto", "Left", "Right"};
static void dashStrCopy(char *d, const char *s, int n) { snprintf(d, n, "%s", s); }
static int dashDevicePathReady(const char *p) { (void)p; return 1; }
static DashApp *dashLibResolve(const char *p, DashApp *tmp) {
    int i;
    for (i = 0; i < dashAppCount; i++) if (!strcmp(dashApps[i].path, p)) return &dashApps[i];
    dashStrCopy(tmp->path, p, sizeof(tmp->path)); return tmp;
}
static void dashLibPrettyName(const char *p, char *name, int n) { dashStrCopy(name, p, n); }
static void addChip(int kind, int lk, int icon, const char *label, const char *desc) {
    (void)kind; (void)lk; (void)icon; (void)label; (void)desc; nchips++;
}
"""
code += function("static int homeHas(") + function("static void homeAdd(") + function("static void homeRebuild(")
code += r"""
int main(void) {
    dashAppCount = 3;
    dashStrCopy(dashApps[0].path, "mass0:/A.ELF", DASH_PATH_MAX);
    dashStrCopy(dashApps[1].path, "mass0:/B.ELF", DASH_PATH_MAX);
    dashStrCopy(dashApps[2].path, "mass0:/C.ELF", DASH_PATH_MAX);
    homeRebuild();
    col[0] = 1; /* User selected B before making it a favorite. */
    dashStrCopy(dashLists.fav[0], dashApps[1].path, DASH_PATH_MAX);
    dashLists.nfav = 1;
    homeRebuild();
    assert(!strcmp(home_apps[col[0]]->path, dashApps[1].path));
    assert(col[0] == 0);
    assert(col_anim[0] == 0.0f);
    /* Library is always accessible; Last folder is Home-only and conditional.
     * Disabling user-configured launch keys must not hide navigation. */
    dashCfg.home_shortcuts = 0;
    homeRebuild();
    assert(nchips == 1);
    dashStrCopy(LastDir, "mass0:/APPS/", sizeof(LastDir));
    homeRebuild();
    assert(nchips == 2);
    LastDir[0] = '\0';
    homeRebuild();
    assert(nchips == 1);
    return 0;
}
"""

cc = shutil.which("gcc") or shutil.which("cc")
if not cc:
    raise SystemExit("A host C compiler is required")
with tempfile.TemporaryDirectory() as temp:
    source_path = Path(temp) / "home_focus.c"
    binary_path = Path(temp) / "home_focus.exe"
    source_path.write_text(code)
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", str(source_path), "-o", str(binary_path)], check=True)
    subprocess.run([str(binary_path)], check=True)
print("Home favorite reorder keeps the selected app")
