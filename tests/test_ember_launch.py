from pathlib import Path

root = Path(__file__).resolve().parents[1]
ember = (root / "src/ember.c").read_text(encoding="utf-8")
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
fileops = (root / "src/main_fileops.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
tools = (root / "src/dash_tools.c").read_text(encoding="utf-8")
dash_main = (root / "src/dash_main.c").read_text(encoding="utf-8")
elf = (root / "src/elf.c").read_text(encoding="utf-8")
header = (root / "include/launchelf.h").read_text(encoding="utf-8")
makefile = (root / "Makefile").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")
init = (root / "src/init.c").read_text(encoding="utf-8")
init_h = (root / "include/init.h").read_text(encoding="utf-8")

# POPStarter and Ember remain independent automatic PS1 routes.
assert "IsPopstarterVcdPath(path)" in actions
assert 'genCmpFileExt(path, "CUE") || IsEmberGamePath(path)' in actions
assert "LaunchPopstarterVcd(path" in actions
assert "LaunchEmberGame(path" in actions
assert "preferred PS1 core" not in tools.lower()

# Ember and POPStarter coexist. The selected format determines the core.
assert '"games"' in ember
for ext in ('"CUE"', '"BIN"', '"EXE"'):
    assert ext in ember
assert "image must be directly inside games/<Game>/" in ember
assert "EMBER_NAME_MAX 180" in ember
assert "preferred PS1 core" not in tools.lower()

# Ember's umbrella is dynamic/configurable rather than hard-coded to /EMBER/.
assert "setting->ember_dir" in ember
assert "configuredEmberElf" in ember
assert "ensureLocalEmber" in ember
assert "copyElf(configured, ember_elf)" in ember
assert "set an Ember directory in Settings" in ember

# The target umbrella owns the BIOS and game/save tree.
assert "ember.elf" in ember
assert "bios.bin" in ember
assert "EMBER_BIOS_SIZE (512 * 1024)" in ember
assert "game's umbrella" in ember

# Launch contract: real ember.elf path is target argv[0], bare game folder is argv[1],
# and the IOP/device stack must stay alive.
assert "LaunchArgsLoadFromBuffer" in ember
assert "CleanUpForEmberExec()" in ember
assert "RunLoaderElfKeepIOPWithArgv0" in ember
assert "prepareEmberUsbHandoffIop(ember_elf, path," in ember
assert "remounted_elf" in ember
assert "prepareEmberUsbHandoffIop" in init_h
assert '"SIO2MAN"' in init and '"MCMAN"' in init and '"MCSERV"' in init
assert '"BDMFS_FATFS"' in init and '"USBMASS_BD"' in init
assert "CleanUpForEmberExec" in header
assert "RunLoaderElfKeepIOPWithArgv0" in header
assert "DashPrepareKeepIopHandoff" in actions
assert "padEnd()" in actions
assert "DashPrepareKeepIopHandoff" in dash_main
assert "dashSoundShutdown()" in dash_main
assert "dashStoreFlushWait" in dash_main
assert "child_argv0_override" in elf
assert "WLE_PFS_WRITETHROUGH" in elf

# Browser surfaces both cores independently.
assert 'PS1 VCDs (POPStarter)' in tools
assert 'PS1 images (Ember)' in tools
assert 'genCmpFileExt(path, "CUE")' in fileops
assert "IsEmberGamePath(path)" in fileops
assert 'genCmpFileExt(path, "CUE")' in browser
assert "IsEmberGamePath(path)" in browser
for ext in ('"CUE"', '"BIN"'):
    assert ext in browser

assert "ember.o" in makefile
assert "- src/ember.c" in yaml

# Classic config persists the Ember directory and Settings exposes a folder picker.
config = (root / "src/config.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
assert "char ember_dir[MAX_PATH]" in header
assert "EMBER_DIR = %s" in config
assert 'strcmp(name, "EMBER_DIR")' in config
assert "S_EMBER_DIR" in settings
assert "DASH_PICK_FOLDER" in settings

print("Ember + POPStarter parallel PS1 launch contract: OK")
