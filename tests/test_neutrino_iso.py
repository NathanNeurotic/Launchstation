from pathlib import Path

root = Path(__file__).resolve().parents[1]
launcher = (root / "src/neutrino_iso.c").read_text(encoding="utf-8")
main_actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
main_fileops = (root / "src/main_fileops.c").read_text(encoding="utf-8")
dash_browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
dash_settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
elf = (root / "src/elf.c").read_text(encoding="utf-8")
config = (root / "src/config.c").read_text(encoding="utf-8")
header = (root / "include/launchelf.h").read_text(encoding="utf-8")
makefile = (root / "ps2.yaml").read_text(encoding="utf-8")
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")

assert "char neutrino_file[MAX_PATH]" in header
assert "NEUTRINO_ELF = %s" in config
assert 'strcmp(name, "NEUTRINO_ELF")' in config
assert "setting->neutrino_file[0] = '\\0'" in config

assert 'genCmpFileExt(path, "ISO")' in launcher
assert '"CD001"' in launcher
assert "config/system.toml" in launcher
assert "system.toml" in launcher
assert "neutrinoElfReadable" in launcher
assert "openPathForRead(path" in launcher

# Deterministic loader discovery: explicit setting, game device, mc0, mc1.
order = [
    "setting->neutrino_file",
    "deviceRootCandidate(path",
    'findCompleteOnRoot("mc0:"',
    'findCompleteOnRoot("mc1:"',
]
pos = [launcher.index(x) for x in order]
assert pos == sorted(pos)

for form in (
    "%s/NEUTRINO/neutrino.elf",
    "%s/neutrino/neutrino.elf",
    "%s/NEUTRINO/NEUTRINO.ELF",
    "%s/neutrino/NEUTRINO.ELF",
):
    assert form in launcher

# Backends that Neutrino understands from File Browser paths.
for token in ('return "usb"', 'return "mx4sio"', 'return "mmce"', 'return "ilink"', 'return "ata"', 'return "udpfs"'):
    assert token in launcher

for arg in ('"-bsd=%s', '-dvd=%s', '-qb', '-cwd=%s'):
    assert arg in launcher

# Neutrino handoff is independent of the generic reset-IOP preference.
assert "RunLoaderElfKeepIOP" in launcher
assert "setting->reboot_iop_elf_load" not in launcher
assert "cleanup_hdd_launch = !preserve_devices" in elf

assert "IsNeutrinoIsoPath(path)" in main_actions
assert "LaunchNeutrinoIso(path" in main_actions
assert "IsNeutrinoIsoPath(path)" in main_fileops
assert 'genCmpFileExt(path, "ISO")' in dash_browser
assert "S_NEUTRINO_PATH" in dash_settings

assert "- src/neutrino_iso.c" in makefile
assert "- src/neutrino_iso.c" in yaml

print("Neutrino ISO launch contract: OK")
