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
assert "mx4sio_bd_irx" in helper
assert "iLinkman_irx" in helper and "IEEE1394_bd_irx" in helper
assert "ps2dev9_irx" in helper and "ata_bd_irx" in helper
assert "mcman_irx" in helper and "mcserv_irx" in helper

assert "memcmp(cfg->magic, WLE_IGR_MAGIC" in setup
assert "filerMountVmc" in setup and "filerUnmountVmc" in setup
assert '"/BOOT/WLE-IGR.ELF"' not in setup, "helper target should be built from shared helper-name constant"
assert "collect_boot_images" in setup
assert "write_verified" in setup
assert "wleIgrRemove" in setup
assert "install_helper_on_any_mc" in setup
assert '"mx4sio"' in setup and '"ilink"' in setup and '"ata"' in setup
assert "strnicmp" not in setup

assert "int wleIgrStatus(" in setup
assert "inspect_helper_path" in setup
assert "inspect_boot_image_helper" in setup
assert "WLE_IGR_STATUS_CURRENT" in (root / "include/mmce_igr.h").read_text()
assert "WLE_IGR_STATUS_OUTDATED" in (root / "include/mmce_igr.h").read_text()
assert "isPathDeviceStackLoaded" in setup

igr_start = main.index("static int runWleIgrBackup(")
igr_end = main.index("//---------------------------------------------------------------------------", igr_start)
igr = main[igr_start:igr_end]
assert "for (unit = 0; unit < 2; unit++)" in igr
assert "mmceBackupAllCards(unit, backup_root" in igr
assert "named/GameID" in igr

assert "- name: wle_igr" in yaml
for module in ("iomanx", "filexio", "sio2man", "mcman", "mcserv", "mmceman", "usbd", "bdm", "bdmfs_fatfs", "usbmass_bd", "mx4sio_bd", "iLinkman", "ata_bd"):
    assert f"      - {module}" in yaml
assert "IEEE1394_bd_mini:IEEE1394_bd" in yaml
assert "dev9:ps2dev9" in yaml

print("PASS: WLE IGR helper passes backup args, covers MC/USB/MMCE/MX4SIO/iLink/ATA relaunch stacks, verifies install/remove paths, and reports current/outdated helper state")
