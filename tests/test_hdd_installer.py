"""Installer parser/filesystem regression tests on sparse host images only.

No network or bundled payloads needed. e2fsck/debugfs/fsck.fat independently
check generated metadata when installed (CI installs these tools).
"""
from pathlib import Path
import ctypes as c
import errno
import gzip
import io
import json
import os
import re
import shutil
import subprocess
import struct
import tarfile
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]

class Extent(c.Structure):
    _fields_ = [('start', c.c_uint32), ('sectors', c.c_uint32)]
class Install(c.Structure):
    _fields_ = [('flags', c.c_uint), ('jail', c.c_int), ('commit', c.c_char * 41),
                ('stage', c.c_char * 1024), ('ca_path', c.c_char * 1024), ('error', c.c_char * 256),
                ('sizes', c.c_uint32 * 15), ('hashes', (c.c_char * 41) * 15),
                ('archives', (c.c_char * 128) * 2), ('linux', Extent * 10), ('owner', c.c_ubyte * 20), ('owned', c.c_uint), ('ready', c.c_uint), ('ca_data', c.c_void_p), ('ca_size', c.c_uint), ('music_mb', c.c_uint), ('contents_mb', c.c_uint), ('linux_home_mb', c.c_uint), ('reserved_mb', c.c_uint), ('game_launcher', c.c_uint), ('launcher_files', c.c_void_p), ('launcher_count', c.c_uint), ('language', c.c_char * 52), ('archive_item', c.c_char * 96), ('channels', c.c_char * 128)]
class Entry(c.Structure):
    _fields_ = [('name', c.c_char * 1024), ('link', c.c_char * 1024)] + [
        (n, c.c_uint32) for n in ('size', 'mode', 'uid', 'gid', 'mtime', 'major', 'minor')] + [('type', c.c_char)]
READ = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint, c.c_void_p)
ZERO = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint32)
FLUSH = c.CFUNCTYPE(c.c_int, c.c_void_p)
class DiskIo(c.Structure):
    _fields_ = [('context', c.c_void_p), ('read', READ), ('write', READ), ('zero', ZERO),
                ('flush', FLUSH), ('apa', c.c_void_p), ('progress', c.c_void_p)]
AREAD = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_void_p, c.c_uint)
DATA = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p, c.c_uint)
BEGIN = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(Entry))
class ArchiveIo(c.Structure):
    _fields_ = [('context', c.c_void_p), ('read', AREAD), ('write', DATA)]
class Sink(c.Structure):
    _fields_ = [('context', c.c_void_p), ('begin', BEGIN), ('data', DATA), ('end', FLUSH)]

class Disk:
    def __init__(self, path, mib):
        self.fd = os.open(path, os.O_CREAT | os.O_TRUNC | os.O_RDWR, 0o600)
        self.size = mib * 1024 * 1024
        os.ftruncate(self.fd, self.size)
        self.fail = False
        def read(_, lba, n, target):
            if self.fail or (lba + n) * 512 > self.size: return -errno.EIO
            data = os.pread(self.fd, n * 512, lba * 512)
            if len(data) != n * 512: return -errno.EIO
            c.memmove(target, data, len(data)); return 0
        def write(_, lba, n, data):
            if self.fail or (lba + n) * 512 > self.size: return -errno.EIO
            return 0 if os.pwrite(self.fd, c.string_at(data, n * 512), lba * 512) == n * 512 else -errno.EIO
        def zero(_, lba, n):
            if self.fail or (lba + n) * 512 > self.size: return -errno.EIO
            offset = lba * 512
            while n:
                amount = min(n, 128)
                if os.pwrite(self.fd, bytes(amount * 512), offset) != amount * 512: return -errno.EIO
                offset += amount * 512; n -= amount
            return 0
        self.callbacks = READ(read), READ(write), ZERO(zero), FLUSH(lambda _: 0)
        self.io = DiskIo(None, *self.callbacks, None, None)
    def close(self): os.close(self.fd)

def archive_io(data, output):
    def read(_, offset, target, size):
        if offset + size > len(data): return -errno.EIO
        c.memmove(target, data[offset:offset + size], size); return 0
    def write(_, source, size): output.append(c.string_at(source, size)); return 0
    callbacks = AREAD(read), DATA(write)
    return ArchiveIo(None, *callbacks), callbacks

def tar(entries, fmt=tarfile.GNU_FORMAT):
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode='w', format=fmt) as t:
        for name, kind, data in entries:
            e = tarfile.TarInfo(name); e.type = kind.encode(); e.uid = 100; e.gid = 200
            e.mode = 0o751; e.mtime = 1700000000
            if kind == '0': e.size = len(data); t.addfile(e, io.BytesIO(data))
            else:
                if kind in ('1', '2'): e.linkname = data
                if kind in ('3', '4'): e.devmajor = 8; e.devminor = 1
                t.addfile(e)
    return gzip.compress(out.getvalue(), mtime=0)

with tempfile.TemporaryDirectory(prefix='hdd-installer-') as directory:
    d = Path(directory)
    libpath = d / 'installer.so'
    subprocess.run(['gcc', '-shared', '-fPIC', '-O1', '-g', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-fno-sanitize-recover=undefined', '-I', str(ROOT / 'include'),
                    *[str(ROOT / 'src' / (n + '.c')) for n in
                      ('hdd_install_plan', 'install_archive', 'install_tar', 'install_ext2', 'install_psbbn_media')],
                    '-lz', '-o', str(libpath)], check=True)
    lib = c.CDLL(str(libpath))
    lib.hddInstallCommit.argtypes = [c.c_char_p, c.c_void_p]
    lib.hddInstallAssetMetadata.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p, c.c_void_p]
    lib.hddInstallArchiveMetadata.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p, c.c_void_p, c.c_void_p]
    lib.hddInstallConfig.argtypes = [c.POINTER(Install), c.c_int, c.c_void_p, c.c_size_t]
    lib.installZipExtract.argtypes = [c.POINTER(ArchiveIo), c.c_uint32, c.c_char_p, c.c_uint32, c.c_void_p]
    lib.installTarGzip.argtypes = [c.POINTER(ArchiveIo), c.c_uint32, c.POINTER(Sink)]
    lib.installExt2Create.argtypes = [c.c_void_p, c.POINTER(DiskIo), c.c_uint32, c.c_uint32, c.c_uint32]
    lib.installExt2Begin.argtypes = [c.c_void_p, c.POINTER(Entry)]
    lib.installExt2Data.argtypes = [c.c_void_p, c.c_void_p, c.c_uint]
    for n in ('End', 'Finish', 'Free'): getattr(lib, 'installExt2' + n).argtypes = [c.c_void_p]
    lib.installPsbbnMusic.argtypes = [c.POINTER(DiskIo), c.c_uint32, c.c_uint32, c.c_uint32]
    lib.installPsbbnSwap.argtypes = [c.POINTER(DiskIo), c.c_uint32, c.c_uint32]
    hashbuf, namebuf, size = c.create_string_buffer(41), c.create_string_buffer(128), c.c_uint32()
    sha = 'a' * 40
    assert lib.hddInstallCommit(json.dumps({'object': {'sha': sha, 'type': 'commit'}}).encode(), hashbuf) == 1
    assert hashbuf.value == sha.encode()
    escaped = b'{"object":{"\\u0073ha":"' + sha.encode() + b'","type":"commit"}}'
    assert lib.hddInstallCommit(escaped, hashbuf) == 1 and hashbuf.value == sha.encode()
    duplicate = b'{"object":{"sha":"' + sha.encode() + b'","\\u0073ha":"' + sha.encode() + b'","type":"commit"}}'
    assert lib.hddInstallCommit(duplicate, hashbuf) == 0
    for bad in (b'{}', b'{"object":{"sha":"a","type":"commit"}}', b'{"object":{"sha":"x"},', b'[]', b'"\\'):
        assert lib.hddInstallCommit(bad, hashbuf) == 0
    meta = [{'name': 'target', 'type': 'file', 'sha': sha, 'size': 200}]
    assert lib.hddInstallAssetMetadata(json.dumps(meta).encode(), b'target', hashbuf, c.byref(size)) == 1
    assert size.value == 200
    assert lib.hddInstallAssetMetadata(json.dumps(meta * 2).encode(), b'target', hashbuf, c.byref(size)) == 0
    assert lib.hddInstallAssetMetadata(json.dumps([{'name':'target','nested':meta[0]}]).encode(), b'target', hashbuf, c.byref(size)) == 0
    files = [{'name': 'pack-v' + v + '.tar.gz', 'sha1': sha, 'size': '140000000'} for v in ('4.9.9', '4.10.1', '4.10.0')]
    assert lib.hddInstallArchiveMetadata(json.dumps({'files': files}).encode(), b'pack-v', namebuf, hashbuf, c.byref(size)) == 1
    assert namebuf.value == b'pack-v4.10.1.tar.gz' and size.value == 140000000
    files[0]['sha1'] = 'bad'  # an invalid older candidate must not stop selection
    assert lib.hddInstallArchiveMetadata(json.dumps({'files': files}).encode(), b'pack-v', namebuf, hashbuf, c.byref(size)) == 1
    assert namebuf.value == b'pack-v4.10.1.tar.gz'
    files[0]['sha1'] = sha
    files[1]['sha1'] = 'bad'
    assert lib.hddInstallArchiveMetadata(json.dumps({'files': files}).encode(), b'pack-v', namebuf, hashbuf, c.byref(size)) == 0
    for jail, flags in ((0, 1), (0, 3), (1, 7)):
        config = c.create_string_buffer(4096); settings = Install(flags=flags, jail=jail, commit=sha.encode())
        assert lib.hddInstallConfig(c.byref(settings), 1, config, len(config)) > 0
        text = config.value.decode()
        assert ('boot_auto = $PSBBN\n' in text) == bool(jail)
        assert ('-dev9=NICHDD' in text) == bool(jail)
        assert ('ata0:/APPS/' in text) == bool(jail)
        assert ('hdd0:__common:pfs:/APPS/' in text) == bool(flags & 2 and not jail)
        assert lib.hddInstallConfig(c.byref(settings), 0, config, 12) < 0
    print('PASS: commit/asset/version metadata bounds and layout-specific boot settings')

    disk = Disk(d / 'batch-failure.img', 128); fs = c.c_void_p()
    assert lib.installExt2Create(c.byref(fs), c.byref(disk.io), 0, disk.size // 512, 1) == 0
    e = Entry(name=b'payload', type=b'0', size=3, mode=0o644)
    assert lib.installExt2Begin(fs, c.byref(e)) == 0
    assert lib.installExt2Data(fs, b'old', e.size) == 0
    assert lib.installExt2End(fs) == 0
    root_inode = os.pread(disk.fd, 128, 4 * 4096 + 128)
    root_dir = int.from_bytes(root_inode[40:44], 'little') * 4096
    original_entries = os.pread(disk.fd, 4096, root_dir)
    assert b'payload' in original_entries
    e.size = 32768
    assert lib.installExt2Begin(fs, c.byref(e)) == 0
    assert lib.installExt2Data(fs, b'Z' * e.size, e.size) == 0
    disk.fail = True
    assert lib.installExt2End(fs) == -errno.EIO and lib.installExt2Finish(fs) < 0
    assert os.pread(disk.fd, 2, 1024 + 58) == b'\x02\x00'  # Never marked clean.
    assert os.pread(disk.fd, 4096, root_dir) == original_entries
    lib.installExt2Free(fs); disk.close()
    if shutil.which('debugfs'):
        original = subprocess.run(['debugfs', '-R', 'cat payload', str(d / 'batch-failure.img')], check=True, capture_output=True)
        assert original.stdout == b'old'
    print('PASS: a failed ext2 file replacement keeps the old directory entry and contents')

    # Both archive formats: exact payload, CRC failures, traversal and truncated input.
    out = io.BytesIO()
    expected = b'font and OSD resources' * 2000
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z: z.writestr('extras/FNTOSD', expected)
    zipped = out.getvalue(); extracted = []
    zi, keep = archive_io(zipped, extracted)
    assert lib.installZipExtract(c.byref(zi), len(zipped), b'extras/FNTOSD', 100000, None) == 0
    assert b''.join(extracted) == expected
    assert lib.installZipExtract(c.byref(zi), len(zipped), b'extras/FNTOSD', 10, None) < 0
    assert lib.installZipExtract(c.byref(zi), len(zipped), b'extras/missing', 100000, None) < 0
    broken = bytearray(zipped); broken[50] ^= 0x55
    zi, keep = archive_io(bytes(broken), [])
    assert lib.installZipExtract(c.byref(zi), len(broken), b'extras/FNTOSD', 100000, None) < 0

    def stored_with_descriptor(signed=True, include=True, valid=True):
        payload = b'descriptor payload'; name = b'extras/STORED'; crc = zlib.crc32(payload)
        local = (struct.pack('<I', 0x04034b50) + struct.pack('<5H', 20, 8, 0, 0, 0) +
                 struct.pack('<3I', 0, 0, 0) + struct.pack('<2H', len(name), 0) + name + payload)
        if include:
            local += (struct.pack('<I', 0x08074b50) if signed else b'') + struct.pack('<3I', crc if valid else crc ^ 1, len(payload), len(payload))
        central = struct.pack('<I6H3I5H2I', 0x02014b50, 20, 20, 8, 0, 0, 0, crc, len(payload), len(payload),
                              len(name), 0, 0, 0, 0, 0, 0) + name
        end = struct.pack('<I4H2IH', 0x06054b50, 0, 0, 1, 1, len(central), len(local), 0)
        return local + central + end
    for signed in (False, True):
        packed = stored_with_descriptor(signed)
        extracted = []
        zi, keep = archive_io(packed, extracted)
        assert lib.installZipExtract(c.byref(zi), len(packed), b'extras/STORED', 1000, None) == 0
        assert b''.join(extracted) == b'descriptor payload'
    packed = stored_with_descriptor(include=False)
    zi, keep = archive_io(packed, [])
    assert lib.installZipExtract(c.byref(zi), len(packed), b'extras/STORED', 1000, None) < 0
    packed = stored_with_descriptor(valid=False)
    zi, keep = archive_io(packed, [])
    assert lib.installZipExtract(c.byref(zi), len(packed), b'extras/STORED', 1000, None) < 0

    image = d / 'linux.img'; disk = Disk(image, 128); fs = c.c_void_p()
    assert lib.installExt2Create(c.byref(fs), c.byref(disk.io), 0, disk.size // 512, 1700000000) == 0
    entries = [('late/sub/child', '5', ''), ('late', '5', ''), ('late/sub', '5', ''), ('etc', '5', ''), ('usr', '5', ''), ('usr/lib', '5', ''), ('bin', '5', ''),
               ('dev', '5', ''), ('etc/settings', '0', b'a' * 13000), ('bin/tool', '0', b'ELF fixture'),
               ('bin/tool-link', '1', 'bin/tool'), ('bin/tool-peer', '1', 'bin/tool'), ('usr/lib/lib:fixture', '0', b'colon name'),
               ('etc/symbolic', '2', '/usr/lib/libc.so'), ('etc/exact60', '2', '/' + 'x' * 59),
               ('etc/long-symbolic', '2', '/' + 'target/' * 20),
               ('dev/sda1', '4', ''), ('dev/console', '3', ''), ('dev/pipe', '6', ''),
               ('usr/lib/' + 'long-name' * 20, '0', b'GNU/PAX long paths'),
               ('usr/large', '0', b'0123456789ABCDEF' * (6 * 1024 * 1024 // 16))]
    seen = []
    def begin(_, e):
        seen.append(e.contents.name.decode()); return lib.installExt2Begin(fs, e)
    callbacks = BEGIN(begin), DATA(lambda _, p, n: lib.installExt2Data(fs, p, n)), FLUSH(lambda _: lib.installExt2End(fs))
    sink = Sink(None, *callbacks)
    def extract(payload, dest=sink):
        ti, refs = archive_io(payload, [])
        return lib.installTarGzip(c.byref(ti), len(payload), c.byref(dest))
    assert extract(tar(entries)) == 0
    overlay = [('etc', '5', ''), ('etc/settings', '0', b'new settings'), ('usr/large', '0', b'z' * (2 * 1024 * 1024)),
               ('etc/symbolic', '0', b'replaced symlink'), ('etc/long-symbolic', '2', '/short'),
               ('bin/tool', '0', b'replaced binary'), ('bin/other', '0', b'other binary'), ('bin/tool-link', '1', 'bin/other')]
    assert extract(tar(overlay, tarfile.PAX_FORMAT)) == 0
    # The installer replaces archive-provided Navigator shortcuts with the
    # chosen launcher and the apps that were actually installed.
    assert extract(tar([('bn/sysconf/shortcut_0', '0', b'old launcher')])) == 0
    settings = Install(flags=7, jail=1, game_launcher=1)
    shortcut = c.create_string_buffer(1024)
    length = lib.hddInstallGameShortcut(c.byref(settings), shortcut, len(shortcut))
    assert length > 0 and b'NHDDL file%3A' in shortcut.value and b'PP.LAUNCHER' in shortcut.value
    entry = Entry(name=b'bn/sysconf/shortcut_0', size=length, mode=0o644, type=b'0')
    assert lib.installExt2Begin(fs, c.byref(entry)) == 0
    assert lib.installExt2Data(fs, shortcut, length) == 0 and lib.installExt2End(fs) == 0
    assert lib.installExt2Finish(fs) == 0
    lib.installExt2Free(fs); disk.close()
    if shutil.which('e2fsck'):
        subprocess.run(['e2fsck', '-fn', str(image)], check=True, capture_output=True)
    if shutil.which('debugfs'):
        def debug(command): return subprocess.run(['debugfs', '-R', command, str(image)], check=True, capture_output=True).stdout.decode()
        target = d / 'settings'
        debug('dump etc/settings ' + str(target)); assert target.read_bytes() == b'new settings'
        target.unlink(); debug('dump usr/large ' + str(target)); assert target.read_bytes() == b'z' * (2 * 1024 * 1024)
        target.unlink(); debug('dump bin/tool-peer ' + str(target)); assert target.read_bytes() == b'ELF fixture'
        target.unlink(); debug('dump bin/tool ' + str(target)); assert target.read_bytes() == b'replaced binary'
        target.unlink(); debug('dump bn/sysconf/shortcut_0 ' + str(target)); assert target.read_bytes() == shortcut.value
        assert '0751' in debug('stat late/sub')
        stat = debug('stat bin/tool')
        assert '0751' in stat and 'User:   100' in stat and 'Group:   200' in stat
        other = re.search(r'Inode: (\d+)', debug('stat bin/other')).group(1)
        assert re.search(r'Inode: (\d+)', debug('stat bin/tool-link')).group(1) == other
        assert 'block special' in debug('stat dev/sda1') and '08:01' in debug('stat dev/sda1')
        assert 'character special' in debug('stat dev/console')
    print('PASS: ext2 permissions, links, devices, GNU/PAX names, double indirection and language replacement; independent fsck')

    noop = (BEGIN(lambda _, e: 0), DATA(lambda _, p, n: 0), FLUSH(lambda _: 0))
    discard = Sink(None, *noop)
    valid = tar([('safe', '0', b'payload')])
    def pax_tar(headers):
        out = io.BytesIO()
        with tarfile.open(fileobj=out, mode='w', format=tarfile.PAX_FORMAT) as archive:
            entry = tarfile.TarInfo('safe'); entry.size = 1; entry.pax_headers = headers
            archive.addfile(entry, io.BytesIO(b'x'))
        return gzip.compress(out.getvalue(), mtime=0)
    mtime_result = extract(pax_tar({'path': 'safe', 'mtime': '1.5'}), discard)
    assert mtime_result == 0
    assert extract(pax_tar({'size': '1.5'}), discard) < 0
    damaged = bytearray(valid); damaged[-8] ^= 1
    for payload in (valid[:-8], bytes(damaged), valid + b'junk', tar([('../escape', '0', b'x')]),
                    tar([('/absolute', '0', b'x')]), tar([('escape', '1', '../target')])):
        assert extract(payload, discard) < 0
    disk = Disk(d / 'fail.img', 64); disk.fail = True; fs = c.c_void_p()
    assert lib.installExt2Create(c.byref(fs), c.byref(disk.io), 0, disk.size // 512, 1) < 0 and not fs.value
    disk.close()
    print('PASS: truncated/corrupt archives, traversal, output limits and disk I/O errors are rejected')

    music = d / 'music.img'; disk = Disk(music, 1020)
    assert lib.installPsbbnMusic(c.byref(disk.io), 0, disk.size // 512, 123) == 0
    disk.close()
    if shutil.which('fsck.fat'): subprocess.run(['fsck.fat', '-vn', str(music)], check=True, capture_output=True)
    swap = d / 'swap.img'; disk = Disk(swap, 124)
    assert lib.installPsbbnSwap(c.byref(disk.io), 0, disk.size // 512) == 0
    assert os.pread(disk.fd, 10, 4086) == b'SWAPSPACE2'; disk.close()
    print('PASS: PSBBN music FAT32 and swap volumes')
