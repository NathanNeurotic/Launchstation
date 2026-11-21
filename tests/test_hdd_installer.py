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
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]

class Extent(c.Structure):
    _fields_ = [('start', c.c_uint32), ('sectors', c.c_uint32)]
class Install(c.Structure):
    _fields_ = [('flags', c.c_uint), ('jail', c.c_int), ('commit', c.c_char * 41),
                ('stage', c.c_char * 1024), ('ca_path', c.c_char * 1024), ('error', c.c_char * 256),
                ('sizes', c.c_uint32 * 10), ('hashes', (c.c_char * 41) * 10),
                ('archives', (c.c_char * 128) * 2), ('linux', Extent * 8), ('owner', c.c_ubyte * 20), ('owned', c.c_uint), ('ready', c.c_uint)]
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
                    '-fsanitize=undefined', '-I', str(ROOT / 'include'),
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

    image = d / 'linux.img'; disk = Disk(image, 128); fs = c.c_void_p()
    assert lib.installExt2Create(c.byref(fs), c.byref(disk.io), 0, disk.size // 512, 1700000000) == 0
    entries = [('etc', '5', ''), ('usr', '5', ''), ('usr/lib', '5', ''), ('bin', '5', ''),
               ('dev', '5', ''), ('etc/settings', '0', b'a' * 13000), ('bin/tool', '0', b'ELF fixture'),
               ('bin/tool-link', '1', 'bin/tool'), ('bin/tool-peer', '1', 'bin/tool'), ('usr/lib/lib:fixture', '0', b'colon name'),
               ('etc/symbolic', '2', '/usr/lib/libc.so'), ('etc/long-symbolic', '2', '/' + 'target/' * 20),
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
