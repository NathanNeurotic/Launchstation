from pathlib import Path

root = Path(__file__).resolve().parents[1]
ember = (root / "src/ember.c").read_text(encoding="utf-8")
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
fileops = (root / "src/main_fileops.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
tools = (root / "src/dash_tools.c").read_text(encoding="utf-8")
elf = (root / "src/elf.c").read_text(encoding="utf-8")
header = (root / "include/launchelf.h").read_text(encoding="utf-8")
makefile = (root / "Makefile").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")

# POPStarter and Ember remain independent automatic PS1 routes.
assert "IsPopstarterVcdPath(path)" in actions
assert "IsEmberGamePath(path)" in actions
assert "LaunchPopstarterVcd(path" in actions
assert "LaunchEmberGame(path" in actions
assert "preferred PS1 core" not in tools.lower()

# Ember accepts only the canonical save-safe layout and its native image forms.
assert '"EMBER"' in ember
assert '"games"' in ember
for ext in ('"CUE"', '"BIN"', '"EXE"'):
    assert ext in ember
assert "image must be directly inside games/<Game>/" in ember
assert "EMBER_NAME_MAX 180" in ember

# Ember requires its own executable and user BIOS on the same game device.
assert "ember.elf" in ember
assert "bios.bin" in ember
assert "EMBER_BIOS_SIZE (512 * 1024)" in ember

# Launch contract: real ember.elf path is target argv[0], bare game folder is argv[1],
# and the IOP/device stack must stay alive.
assert "LaunchArgsLoadFromBuffer" in ember
assert "RunLoaderElfKeepIOPWithArgv0" in ember
assert "RunLoaderElfKeepIOPWithArgv0" in header
assert "child_argv0_override" in elf
assert "WLE_PFS_WRITETHROUGH" in elf

# Browser surfaces both cores independently.
assert 'PS1 VCDs (POPStarter)' in tools
assert 'PS1 images (Ember)' in tools
assert "IsEmberGamePath(path)" in fileops
assert "IsEmberGamePath(path)" in browser
for ext in ('"CUE"', '"BIN"'):
    assert ext in browser

assert "ember.o" in makefile
assert "- src/ember.c" in yaml

print("Ember + POPStarter parallel PS1 launch contract: OK")
