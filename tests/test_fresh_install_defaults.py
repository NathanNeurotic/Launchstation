"""The built-in starting values are the project's chosen defaults.

Checks the classic defaults in include/config_private.h and src/config.c
match the list, so a new install (no LAUNCHSTATION.CNF) and any key missing
from an older file both get them, and runs the real region launch-key
mapping from src/main_boot.c, used only when no configuration was loaded.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
assert "python3 tests/test_fresh_install_defaults.py" in (root / ".github/workflows/compile.yml").read_text()
boot = (root / "src/main_boot.c").read_text()

# region launch key only on the no-config path; nothing else overrides initConfig
block = boot[boot.index("if (local_cnf_error < 0) {"):]
block = block[:block.index("bringUpBootNetworkStack")]
assert "mapDefaultFileBrowserLaunchKeyToOkButton();" in block
assert "applyFreshInstallDefaults" not in boot

# the classic defaults are the list
private = (root / "include/config_private.h").read_text()
for name, value in {
    "DEF_TIMEOUT": "10", "DEF_HIDE_PATHS": "TRUE", "DEF_MENU_FRAME": "TRUE",
    "DEF_LEGACY_NET_DRIVERS": "FALSE", "DEF_CDROM_DISABLE_GAMEID": "FALSE",
    "DEF_POPUP_OPAQUE": "FALSE", "DEF_USBKBD_USED": "0", "DEF_LANGUAGE": "BUILTIN_LANGUAGE_ENGLISH",
    "DEF_STARTUP_RESET_IOP_ELFLOAD": "1", "DEF_VIRTUAL_KEYBOARD_LAYOUT": "VKEY_LAYOUT_QWERTY",
    "DEF_SHOW_TITLES": "1", "DEF_PATHPAD_LOCK": "0", "DEF_PSU_HUGENAMES": "0",
    "DEF_PSU_DATENAMES": "0", "DEF_PSU_NOOVERWRITE": "0", "DEF_FB_NOICONS": "0",
    "DEF_HOSTWRITE": "TRUE", "DEF_APP_GAMEID": "TRUE", "DEF_INIT_DELAY": "0",
    "DEF_HIDE_HDD": "HIDE_HDD_SHOW_ALL", "DEF_HIDE_MCMMCE": "1",
}.items():
    assert re.search(rf"\b{name} = {value},", private), name
colors = re.findall(r"DEF_COLOR(\d) = GS_SETREG_RGBA\((0x[0-9a-f]+|0), (0x[0-9a-f]+|0), (0x[0-9a-f]+|0), 0\)", private)
wanted = ["00000000", "00353535", "00FFCE6C", "00A0A0A0", "0099CCFF", "00006A48", "007C3A43", "0028C4D6"]
for n, r, g, b in colors:
    abgr = "00%02X%02X%02X" % (int(b, 16), int(g, 16), int(r, 16))
    assert abgr == wanted[int(n) - 1], (n, abgr)
assert len(colors) == 16  # CUSTOM_COLORS and plain builds
config = (root / "src/config.c").read_text()
init = config[config.index("void initConfig(void)"):config.index("//endfunc initConfig")]
for line in ("setting->TV_mode = TV_mode_AUTO;", "setting->screen_x = 0;", "setting->screen_y = 0;",
             'strcpy(setting->SMB_Server_User, "ps2");', 'strcpy(setting->SMB_Server_Pass, "ps2");',
             "strcpy(setting->LK_Path[SETTING_LK_TRIANGLE], setting->Misc_About_uLE);",
             "setting->disable_ember = 0;", "setting->disable_popstarter = 0;", "setting->disable_neutrino = 0;"):
    assert line in init, line

start = boot.index("static void mapDefaultFileBrowserLaunchKeyToOkButton(void)")
end = boot.index("enum BOOT_DEVICE performEarlyBootInitialization")
code = r'''
#include <assert.h>
#include <string.h>
enum { SETTING_LK_AUTO, SETTING_LK_CIRCLE, SETTING_LK_CROSS, SETTING_LK_COUNT };
typedef struct {
    char LK_Path[SETTING_LK_COUNT][64]; int LK_Flag[SETTING_LK_COUNT];
    char Misc_FileBrowser[64]; int swapKeys;
} SETTING;
static SETTING s, *setting = &s;
''' + boot[start:end] + r'''
int main(void) {
    strcpy(s.Misc_FileBrowser, "MISC/FileBrowser");
    strcpy(s.LK_Path[SETTING_LK_CIRCLE], "MISC/FileBrowser"); s.LK_Flag[SETTING_LK_CIRCLE] = 1;
    s.swapKeys = 1;                       /* non-Japanese console: Cross confirms */
    mapDefaultFileBrowserLaunchKeyToOkButton();
    assert(!strcmp(s.LK_Path[SETTING_LK_CROSS], "MISC/FileBrowser") && s.LK_Flag[SETTING_LK_CROSS]);
    assert(s.LK_Path[SETTING_LK_CIRCLE][0] == 0 && !s.LK_Flag[SETTING_LK_CIRCLE]);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="fresh-defaults-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: the built-in defaults are the chosen list; region launch key only without a config")

themes = (root / "src/dash_themes.c").read_text()
assert "#define P_DEFAULT P_CRYSTAL" in themes
for needle in ("theme_preset = P_DEFAULT;", "store_preset = P_DEFAULT;", "(p >= 0) ? p : P_DEFAULT;"):
    assert needle in themes, needle
settings_src = (root / "src/dash_settings.c").read_text()
assert "dashThemeSetPreset(0)" not in settings_src
print("PASS: the dashboard starts on the Crystal preset")

# The new default applies only to absent/invalid keys. Saved on/off settings persist.
store = (root / "src/dash_store.c").read_text(encoding="utf-8")
assert "dashCfg.icon_backing = 0;" in store
assert "dashCfg.icon_backing = clampInt(value, 0, 1, 0);" in store
assert '"Dash_IconBacking = %d' in store
assert settings_src.count("dashCfg.icon_backing = 0;") == 2

# Partition lists cannot display stale or meaningless timestamps.
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
assert "if (!isPartitionList(fb.dir) && time_valid && f->stats._Modify.Year)" in browser
print("PASS: control backings default off, theme resets, and partition dates hidden")

# Dashboard fresh installs prefer List view, preserve music On and Crystal.
assert "dashCfg.view = DASH_VIEW_LIST;" in store
assert "dashCfg.view = strictClampInt(value, 0, DASH_VIEW_COUNT - 1, DASH_VIEW_LIST);" in store
assert "errno == ERANGE" in store and "*end != '\\0'" in store  # Reject malformed / overflowing numbers
assert "dashCfg.bgm = 1;" in store
assert "dashCfg.bgm = clampInt(value, 0, 1, 1);" in store
