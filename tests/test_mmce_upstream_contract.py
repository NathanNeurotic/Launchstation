"""Regression guard for the pinned sd2psXtd firmware MMCEMAN contract.

Reference: sd2psXtd/firmware @ 8a20bd991b43ac731882f5f85030b19e94c23a3b
             src/ps2/mmceman/ps2_mmceman_commands.h
This verifies Launchstation's source against known firmware command IDs
without depending on GitHub/network availability in CI.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
header = (root / "iop/mmceman/include/mmce_cmds.h").read_text()
driver = (root / "iop/mmceman/src/mmce_fs.c").read_text()
cmd = (root / "iop/mmceman/src/mmce_cmds.c").read_text()
browser = (root / "src/dash_browser.c").read_text()
cloud = (root / "src/cloud_google.c").read_text()
protocol = (root / "docs/MMCE_RESTORE_PROTOCOL.md").read_text()
audit = (root / "docs/MMCE_SD2PSXTD_FIRMWARE_AUDIT.md").read_text()

def get_constants(text):
    values = {}
    for enum in re.finditer(r"enum\s+mmce_cmds(?:_fs)?\s*\{(.*?)\}", text, re.S):
        index = -1
        for part in enum.group(1).split(","):
            part = part.split("//")[0].strip()
            if not part:
                continue
            match = re.fullmatch(r"(MMCE_[A-Z0-9_]+)\s*(?:=\s*(0x[0-9a-fA-F]+|\d+))?", part)
            assert match, "Cannot decode firmware opcode enum: " + part
            name, explicit = match.groups()
            index = int(explicit, 0) if explicit else index + 1
            values[name] = index
    return values

ops = get_constants(header)
# Values sourced from sd2psXtd firmware, not from Launchstation's assumptions.
reference = {
    "MMCE_CMD_PING": 0x01,
    "MMCE_CMD_GET_STATUS": 0x02,
    "MMCE_CMD_GET_CARD": 0x03,
    "MMCE_CMD_SET_CARD": 0x04,
    "MMCE_CMD_GET_CHANNEL": 0x05,
    "MMCE_CMD_SET_CHANNEL": 0x06,
    "MMCE_CMD_GET_GAMEID": 0x07,
    "MMCE_CMD_SET_GAMEID": 0x08,
    "MMCE_CMD_RESET": 0x09,
    "MMCE_CMD_SET_CARD_CHANNEL": 0x0A,
    "MMCE_CMD_UNMOUNT_BOOTCARD": 0x30,
    "MMCE_CMD_FS_OPEN": 0x40,
    "MMCE_CMD_FS_CLOSE": 0x41,
    "MMCE_CMD_FS_READ": 0x42,
    "MMCE_CMD_FS_WRITE": 0x43,
    "MMCE_CMD_FS_REMOVE": 0x46,
    "MMCE_CMD_FS_RENAME": 0x4E,
    "MMCE_CMD_FS_LSEEK64": 0x53,
    "MMCE_CMD_FS_READ_SECTOR": 0x58,
}
for name, expected in reference.items():
    assert ops.get(name) == expected, \
        f"Firmware opcode drift: {name}={ops.get(name)} not {expected:#x}"

assert "mmce_cmd_get_status()" in driver
assert "mmce_cmd_get_card()" in driver
assert "mmce_cmd_reset()" in driver
assert "mmce_fs_discover_active_fd" in driver
assert "MMCE_FS_ACTIVE_HANDLE" in driver
assert "return -EROFS;" in driver
assert "mmce_fs_reserved_component" in driver
assert "fileXioRename(stage, dest)" in browser, "Local copies must remain staged"
assert "mcCardValidateNoEcc(stage)" in cloud
assert "fileXioRename(stage, result)" in cloud
assert "MMCE_RESTORE_PROTOCOL" in audit
assert "firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b" in audit
assert "**Not enabled.**" in protocol
assert "not implemented upstream" in audit
# None of these check paths may be reinterpreted as an atomic, exclusive lease.
assert "GET_STATUS" in audit and "GET_CARD" in audit
assert "FS_RENAME" in audit and "GET_STATUS == 0" not in protocol
print("PASS: firmware MMCEMAN command parity and safe-restore unsupported gate")
