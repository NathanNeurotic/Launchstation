"""Library 'Start at boot' (AUTO launch key) and hold-confirm IOP-reset inversion."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
apps = (root / "src/dash_apps.c").read_text(encoding="utf-8")
main = (root / "src/dash_main.c").read_text(encoding="utf-8")
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")


def function(src, signature):
    a = src.index(signature)
    return src[a:src.index("\n}\n", a) + 3]


# wiring: the inversion applies to ordinary ELF launches only, once, right after the setting is read
assert actions.count("mainActionIopInvertHook()") == 1
assert "x = setting->reboot_iop_elf_load;\n\t\t\tif (mainActionIopInvertHook != NULL && mainActionIopInvertHook())\n\t\t\t\tx = !x;" in actions
assert "mainActionIopInvertHook = dashIopInvertHook;" in main
assert 'detail = setting->reboot_iop_elf_load ? "hold to skip IOP reset" : "hold to reset IOP"' in apps
assert "ids[n++] = 9;" in apps and "case 9:\n\t\t\ttoggleBootApp(a);" in apps

# Dashboard (START) menu: Files is a tab, so there is no duplicate entry and the dispatcher matches the items
menu = main[main.index("void dashDashboardMenu(void)"):]
menu = menu[:menu.index("int r;")]
labels = [l.split('"')[1] for l in menu.splitlines() if '{DI_' in l]
assert labels == ["Home", "Refresh application library", "Settings", "Reboot IOP (reset drivers)", "Power off"], labels
gm = function(main, "static void globalMenu(")
assert "(r == 3 || r == 4) && !dashConfirmLeave()" in gm
for n, what in ((0, "DASH_SEC_HOME"), (1, "dashRescanLibrary"), (2, "DASH_SEC_SETTINGS"), (3, "Misc_Reboot_IOP"), (4, "Misc_PS2PowerOff")):
    body = gm[gm.index(f"case {n}:"):]
    assert what in body[:body.index("break;")], (n, what)
assert "case 5:" not in gm and "DASH_SEC_FILES" not in gm

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef unsigned int u32;
#define stricmp strcasecmp
#define MAX_PATH 256
#define MAX_ELF_TITLE 40
#define SETTING_LK_AUTO 0
#define DASH_TOAST_OK 1
#define DASH_TOAST_INFO 2
#define DASH_TOAST_WARN 3
#define PAD_CROSS 0x4000
#define PAD_CIRCLE 0x2000
typedef struct { char LK_Path[2][MAX_PATH]; char LK_Title[2][MAX_ELF_TITLE]; int PathPad_Lock, timeout; } SETTING;
static SETTING config, *setting = &config;
typedef struct { char path[MAX_PATH]; char title[MAX_ELF_TITLE]; } DashApp;
static int toasts, last_kind, saves, save_result = 1;
static char last_msg[128];
static void dashToast(int kind, const char *m) { toasts++; last_kind = kind; snprintf(last_msg, sizeof(last_msg), "%s", m); }
static void dashStrCopy(char *d, const char *s, int n) { snprintf(d, n, "%s", s); }
static int dashSettingsSaveCnf(void) { saves++; return save_result; }
static int dash_active = 1, pad_ok = 1, swapKeys;
static u32 paddata;
static int readpad_no_KB(void) { return pad_ok; }
''' + function(apps, "static void toggleBootApp(") + function(main, "static int dashIopInvertHook(") + r'''
int main(void) {
    DashApp a = {"mass:/APPS/OPL/OPNPS2LD.ELF", "Open PS2 Loader"};
    DashApp b = {"mc0:/APPS/SMS/SMS.ELF", "SMS"};
    config.timeout = 5;
    /* set, then replace, then clear (case-insensitive path match) */
    toggleBootApp(&a);
    assert(!strcmp(config.LK_Path[0], a.path) && !strcmp(config.LK_Title[0], "Open PS2 Loader") && saves == 1 && last_kind == DASH_TOAST_OK);
    assert(strstr(last_msg, "5 s") && strstr(last_msg, "cancels"));
    toggleBootApp(&b);
    assert(!strcmp(config.LK_Path[0], b.path) && saves == 2);
    strcpy(b.path, "MC0:/apps/sms/sms.elf");
    toggleBootApp(&b);
    assert(config.LK_Path[0][0] == 0 && config.LK_Title[0][0] == 0 && saves == 3 && last_kind == DASH_TOAST_INFO);
    /* locked launch keys are never touched */
    config.PathPad_Lock = 1; saves = 0;
    toggleBootApp(&a);
    assert(config.LK_Path[0][0] == 0 && saves == 0 && last_kind == DASH_TOAST_WARN);
    config.PathPad_Lock = 0;
    /* a failed save reports nothing as success */
    save_result = 0; toasts = 0;
    toggleBootApp(&a);
    assert(toasts == 0);

    /* hold-confirm detection follows the swap-keys setting */
    paddata = PAD_CIRCLE; swapKeys = 0; assert(dashIopInvertHook() == 1);
    paddata = PAD_CROSS;  swapKeys = 0; assert(dashIopInvertHook() == 0);
    paddata = PAD_CROSS;  swapKeys = 1; assert(dashIopInvertHook() == 1);
    paddata = PAD_CIRCLE; swapKeys = 1; assert(dashIopInvertHook() == 0);
    paddata = 0; assert(dashIopInvertHook() == 0);            /* a plain tap: released long before */
    paddata = PAD_CIRCLE; swapKeys = 0; pad_ok = 0; assert(dashIopInvertHook() == 0);   /* pad not ready */
    pad_ok = 1; dash_active = 0; assert(dashIopInvertHook() == 0);  /* classic UI launches unchanged */
    puts("ok");
    return 0;
}
'''
d = Path(tempfile.mkdtemp())
(d / "t.c").write_text(code)
subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                "-o", str(d / "t"), str(d / "t.c")], check=True)
out = subprocess.run([str(d / "t")], capture_output=True, text=True, check=True)
assert out.stdout.strip() == "ok"
print("PASS: library 'Start at boot' maps to the AUTO launch key; holding confirm inverts the IOP reset for one launch")
