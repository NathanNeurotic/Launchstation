"""A large file copied to a FAT32 or exFAT volume gets one unbroken run of clusters.

Runs the FatFs sources of the FAT/exFAT driver Launchstation builds (iop/bdmfs_fatfs) on the
host, on a volume in memory with holes left by deleted files (tests/contiguous_copy_host.c).
Without the reservation the copy is spread over the holes, in more pieces than Neutrino's 64;
with it the copy is in one piece. Also checks that the build and the copy path use the request.
The only build difference from the PS2 module is FF_USE_MKFS, to format the test volume.
This does not run the PS2 driver glue, a real card, i.LINK or a console.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
driver = root / 'iop/bdmfs_fatfs/src'
conf = (driver / 'fatfs/include/ffconf.h').read_text()
assert re.search(r'^#define FF_USE_EXPAND\t1$', conf, re.M), 'f_expand is not built'
assert re.search(r'^#define FF_FS_EXFAT\t\t1$', conf, re.M)

# The request reaches the driver, and only for a file open for writing.
fs_driver = (driver / 'fs_driver.c').read_text()
case = fs_driver[fs_driver.index('case BDMFS_IOCTL2_RESERVE:'):]
case = case[:case.index('break;\n        }') + 6]
assert '!(fd->mode & O_WRONLY) || data == NULL || datalen != sizeof(bytes)' in case
assert 'ret = fs_reserve_contiguous(file, bytes);' in case
assert fs_driver.index('_fs_lock();', fs_driver.index('int fs_ioctl2(')) < fs_driver.index('case BDMFS_IOCTL2_RESERVE:')

# Launchstation embeds its own build of the driver, with every source file.
build = (root / 'ps2.yaml').read_text()
target = build[build.index('- name: bdmfs_fatfs_fork'):]
target = target[:target.index('\n\n')]
for source in ('main.c', 'fs_driver.c', 'reserve.c', 'fatfs/diskio.c', 'fatfs/ff.c', 'fatfs/ffsystem.c',
               'fatfs/ffunicode.c'):
    assert '- iop/bdmfs_fatfs/src/' + source + '\n' in target, source
assert build.count('- bdmfs_fatfs_fork:bdmfs_fatfs\n') == 1

# The file manager asks before the first write, never for a .psu being assembled, and both
# large-file writers fall back to the ordinary write when the answer is no.
copy = (root / 'src/filer_copy.c').read_text()
ask = copy.index('if (PM_flag[recurses] != PM_PSU_BACKUP && size >= GEN_RESERVE_MIN_BYTES)')
assert copy.index('out_fd = genOpen(out, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);') < ask
assert ask < copy.index('while (size > 0) {  // ----- The main copying loop starts here')
fileops = (root / 'src/filer_fileops.c').read_text()
gen = fileops[fileops.index('int genReserveContiguous('):fileops.index('//endfunc genReserveContiguous')]
assert 'fileXioIoctl2(fd, BDMFS_IOCTL2_RESERVE, &size, sizeof(size), NULL, 0) == BDMFS_RESERVE_OK' in gen
for device in ('"mass"', '"usb"', '"ata"', '"mx4sio"', '"cf"', '"ilink"'):
    assert device in gen, device
receiver = (root / 'src/pc_game_receiver.c').read_text()
assert 'if (!genReserveContiguous(out, dest, item->size)) {' in receiver

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    shutil.copy(driver / 'fatfs/include/ff.h', tmp)
    host_conf, changed = re.subn(r'^#define FF_USE_MKFS\t\t0$', '#define FF_USE_MKFS\t\t1', conf, flags=re.M)
    assert changed == 1
    (tmp / 'ffconf.h').write_text(host_conf)
    exe = tmp / 'contiguous_copy'
    subprocess.run(['cc', '-std=gnu99', '-O1', '-Wall', '-Wno-unused-function', '-I', str(tmp),
                    '-I', str(driver / 'fatfs/include'), '-I', str(driver / 'include'), '-I', str(root / 'include'),
                    str(root / 'tests/contiguous_copy_host.c'), str(driver / 'fatfs/ffunicode.c'), '-o', str(exe)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
print(out, end='')
results = {m[1]: tuple(map(int, m.groups()[1:])) for m in
           re.finditer(r'^(\S+) plain=(\d+) reserved=(\d+) nofit=(\d+)$', out, re.M)}
assert set(results) == {'FAT32', 'exFAT'}, out
for name, (plain, reserved, nofit) in results.items():
    assert plain > 64, (name, plain)   # the reported failure: more pieces than Neutrino accepts
    assert reserved == 1, (name, reserved)
    assert nofit > 1, (name, nofit)    # no long enough run: unchanged behaviour, not an error
print('contiguous copy OK')
