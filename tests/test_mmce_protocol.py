"""Guard the sparse sd2psXtd MMCE command IDs used by wLaunchELF."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
header = (root / "iop/mmceman/include/mmce_cmds.h").read_text()
cmds = (root / "iop/mmceman/src/mmce_cmds.c").read_text()
fs = (root / "iop/mmceman/src/mmce_fs.c").read_text()

assert re.search(r"MMCE_CMD_UNMOUNT_BOOTCARD\s*=\s*0x30", header), \
    "firmware UNMOUNT_BOOTCARD command must remain sparse ID 0x30"
assert "int mmce_cmd_unmount_bootcard(void);" in header
assert "wrbuf[0x1] = MMCE_CMD_UNMOUNT_BOOTCARD;" in cmds
assert "u8 wrbuf[0x3];" in cmds[cmds.index("int mmce_cmd_unmount_bootcard"):cmds.index("int mmce_cmd_reset")]
assert "case MMCE_CMD_UNMOUNT_BOOTCARD:" in fs
assert "res = mmce_cmd_unmount_bootcard();" in fs

print("PASS: MMCE boot-card unmount command stays at firmware ID 0x30 and is exposed through devctl")
