"""A new install (no LAUNCHSTATION.CNF) starts from the project's chosen defaults.

Runs the real applyFreshInstallDefaults() and launch-key mapping from
src/main_boot.c, checks the classic defaults in include/config_private.h
and src/config.c cover the rest of the list, and that the fresh-install
values are applied only when no configuration was loaded.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
assert "python3 tests/test_fresh_install_defaults.py" in (root / ".github/workflows/compile.yml").read_text()
boot = (root / "src/main_boot.c").read_text()

# only on the no-config path, after the region-based confirm button
block = boot[boot.index("if (local_cnf_error < 0) {"):]
block = block[:block.index("bringUpBootNetworkStack")]
assert "mapDefaultFileBrowserLaunchKeyToOkButton();\n\t\tapplyFreshInstallDefaults();" in block
assert boot.count("applyFreshInstallDefaults();") == 1

# the classic defaults already match the rest of the list
private = (root / "include/config_private.h").read_text()
for name, value in {
    "DEF_TIMEOUT": "10", "DEF_HIDE_PATHS": "TRUE", "DEF_MENU_FRAME": "TRUE",
    "DEF_LEGACY_NET_DRIVERS": "FALSE", "DEF_CDROM_DISABLE_GAMEID": "FALSE",
    "DEF_POPUP_OPAQUE": "FALSE", "DEF_USBKBD_USED": "0", "DEF_LANGUAGE": "BUILTIN_LANGUAGE_ENGLISH",
    "DEF_STARTUP_RESET_IOP_ELFLOAD": "1", "DEF_VIRTUAL_KEYBOARD_LAYOUT": "VKEY_LAYOUT_QWERTY",
    "DEF_SHOW_TITLES": "1", "DEF_PATHPAD_LOCK": "0", "DEF_PSU_HUGENAMES": "0",
    "DEF_PSU_DATENAMES": "0", "DEF_PSU_NOOVERWRITE": "0", "DEF_FB_NOICONS": "0",
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
enum { HIDE_HDD_SHOW_ALL = 0, HIDE_HDD_HDD1_ATA1 = 5 };
typedef struct {
    char LK_Path[SETTING_LK_COUNT][64]; int LK_Flag[SETTING_LK_COUNT];
    char Misc_FileBrowser[64]; int swapKeys, HOSTwrite, app_gameid, Init_Delay, Hide_Hdd, Hide_MCMMCE;
} SETTING;
static SETTING s, *setting = &s;
''' + boot[start:end] + r'''
int main(void) {
    strcpy(s.Misc_FileBrowser, "MISC/FileBrowser");
    strcpy(s.LK_Path[SETTING_LK_CIRCLE], "MISC/FileBrowser"); s.LK_Flag[SETTING_LK_CIRCLE] = 1;
    s.Init_Delay = 1; s.Hide_Hdd = HIDE_HDD_HDD1_ATA1;
    s.swapKeys = 1;                       /* non-Japanese console: Cross confirms */
    mapDefaultFileBrowserLaunchKeyToOkButton();
    applyFreshInstallDefaults();
    assert(!strcmp(s.LK_Path[SETTING_LK_CROSS], "MISC/FileBrowser") && s.LK_Flag[SETTING_LK_CROSS]);
    assert(s.LK_Path[SETTING_LK_CIRCLE][0] == 0 && !s.LK_Flag[SETTING_LK_CIRCLE]);
    assert(s.HOSTwrite == 1 && s.app_gameid == 1 && s.Init_Delay == 0);
    assert(s.Hide_Hdd == HIDE_HDD_SHOW_ALL && s.Hide_MCMMCE == 1);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="fresh-defaults-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: a new install starts from the chosen defaults; loaded configurations are untouched")
