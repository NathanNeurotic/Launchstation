"""Guard the WLE IGR trampoline/setup contract."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
helper = (root / "igr/wle_igr.c").read_text()
setup = (root / "src/mmce_igr.c").read_text()
main = (root / "src/main.c").read_text()
yaml = (root / "ps2.yaml").read_text()

assert "WLE_IGR_MAGIC" in helper
assert '"-wle-igr"' in helper
assert '"-wle-igr-backup=%s"' in helper
assert "ExecPS2" in helper
assert "mmceman_irx" in helper
assert "usbmass_bd_irx" in helper
assert "mcman_irx" in helper and "mcserv_irx" in helper

assert "memcmp(cfg->magic, WLE_IGR_MAGIC" in setup
assert "filerMountVmc" in setup and "filerUnmountVmc" in setup
assert '"/BOOT/WLE-IGR.ELF"' not in setup, "helper target should be built from shared helper-name constant"
assert "collect_boot_images" in setup
assert "write_verified" in setup
assert "wleIgrRemove" in setup
assert "strnicmp" not in setup

igr_start = main.index("static void runWleIgrBackup(")
igr_end = main.index("//---------------------------------------------------------------------------", igr_start)
igr = main[igr_start:igr_end]
assert "for (unit = 0; unit < 2; unit++)" in igr
assert "mmceBackupAllCards(unit, backup_root" in igr
assert "named/GameID" in igr

assert "- name: wle_igr" in yaml
for module in ("iomanx", "filexio", "sio2man", "mcman", "mcserv", "mmceman", "usbd", "bdm", "bdmfs_fatfs", "usbmass_bd"):
    assert f"      - {module}" in yaml

print("PASS: WLE IGR helper passes backup args, embeds MC/USB/MMCE stacks, and setup verifies install/remove paths")
