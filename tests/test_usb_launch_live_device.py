"""Issue 439: an ELF launched from a USB stick must be read from that stick.

The browser path is usbN:/..., the live USB volume. The child's argv[0] is the
legacy massN:/... name, but massN: counts every FAT/exFAT volume in mount order
(iop/bdmfs_fatfs fs_driver_resolve_volume), so with an internal exFAT drive
mounted first mass0: is that drive. The loader reads argv[0] of its own argv
(exec_target), so exec_target must stay the live path and never become massN:.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
elf = (root / 'src/elf.c').read_text()
fs = (root / 'iop/bdmfs_fatfs/src/fs_driver.c').read_text()

start = elf.index('static void RunLoaderElfInternal(')
end = elf.index('\nvoid RunLoaderElf(', start)
body = elf[start:end]

# exec_target is written once, from the live filename, and nowhere else.
writes = re.findall(r'snprintf\(exec_target,[^;]*;', body)
assert writes == ['snprintf(exec_target, sizeof(exec_target), "%s", filename);'], writes
assert 'exec_target, sizeof(exec_target), "%s", handoff_path' not in body

# The loader still receives exec_target first and the massN: name for the child.
assert body.count('argv[0] = exec_target;') >= 2
assert 'argv[1] = (char *)((handoff_path != NULL) ? handoff_path : filename);' in body

# The premise: numbered mass slots are mount order, typed names match the device.
assert 'if (strcmp(driver_name, "mass") == 0) {' in fs
assert 'strcmp(bd->path, driver_name) == 0' in fs

print('ok: USB launches read the ELF from the live usbN: path')
