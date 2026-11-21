"""Exercise the production formatter on sparse disk images, never real disks.

Optional independent validators: fsck.exfat, e2fsck and debugfs. The structural
checks always run. Supply HDD_FORMAT_TEST_INCLUDE/HDD_FORMAT_TEST_LIBS when the
host has zlib runtime libraries but no development package.
"""
from pathlib import Path
import ctypes as c
import errno
import gzip
import os
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MB = 2048
APA, EXFAT, JAIL = range(3)


class Plan(c.Structure):
    _fields_ = [(n, c.c_uint32) for n in
                ("kind", "sectors", "reserve_mb", "serial", "apa_sectors",
                 "recovery_start", "exfat_start", "exfat_sectors")]


READ = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint, c.c_void_p)
WRITE = READ
ZERO = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint32)
FLUSH = c.CFUNCTYPE(c.c_int, c.c_void_p)
PROGRESS = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_uint32, c.c_uint32)
READ_ONE = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_void_p)


class Io(c.Structure):
    _fields_ = [("context", c.c_void_p), ("read", READ), ("write", WRITE),
                ("zero", ZERO), ("flush", FLUSH), ("apa", FLUSH), ("progress", PROGRESS)]


def get32(b, pos=0):
    return struct.unpack_from("<I", b, pos)[0]


def set32(b, pos, value):
    struct.pack_into("<I", b, pos, value)


def fix_apa(b):
    set32(b, 0, sum(struct.unpack_from("<255I", b, 4)) & 0xffffffff)


def apa_header(start, length, next_lba=0, prev=0, kind=0x100):
    b = bytearray(1024)
    for pos, value in ((4, 0x00415041), (8, next_lba), (12, prev), (64, start), (68, length)):
        set32(b, pos, value)
    struct.pack_into("<H", b, 72, kind)
    fix_apa(b)
    return b


def checksum(data):
    value = 0
    for byte in data:
        value = (((value >> 1) | (value << 31)) + byte) & 0xffffffff
    return value


class Image:
    def __init__(self, directory, sectors):
        self.path = directory / "disk.img"
        self.fd = os.open(self.path, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o600)
        os.ftruncate(self.fd, sectors * 512)
        self.sectors = sectors
        self.calls = self.flushes = self.apa_calls = 0
        self.fail_at = None
        self.errors = []
        self.stages = set()
        self.read = READ(self._read)
        self.write = WRITE(self._write)
        self.zero = ZERO(self._zero)
        self.flush = FLUSH(self._flush)
        self.apa = FLUSH(self._apa)
        self.progress = PROGRESS(self._progress)
        self.one = READ_ONE(lambda ctx, lba, data: self._read(ctx, lba, 1, data))
        self.io = Io(None, self.read, self.write, self.zero, self.flush, self.apa, self.progress)

    def check(self, lba, count):
        if count <= 0 or lba + count > self.sectors:
            self.errors.append((lba, count))
            return -errno.EINVAL
        self.calls += 1
        return -errno.EIO if self.calls == self.fail_at else 0

    def _read(self, ctx, lba, count, data):
        ret = self.check(lba, count)
        if ret:
            return ret
        payload = os.pread(self.fd, count * 512, lba * 512)
        c.memmove(data, payload, len(payload))
        return 0 if len(payload) == count * 512 else -errno.EIO

    def _write(self, ctx, lba, count, data):
        ret = self.check(lba, count)
        if ret:
            return ret
        return 0 if os.pwrite(self.fd, c.string_at(data, count * 512), lba * 512) == count * 512 else -errno.EIO

    def _zero(self, ctx, lba, count):
        ret = self.check(lba, count)
        if ret:
            return ret
        return 0 if os.pwrite(self.fd, bytes(count * 512), lba * 512) == count * 512 else -errno.EIO

    def _flush(self, ctx):
        self.flushes += 1
        return 0

    def _progress(self, ctx, stage, done, total):
        if total and done > total:
            self.errors.append((stage, done, total))
        self.stages.add(stage.decode())

    def _apa(self, ctx):
        # Test substitute for the existing, unchanged APA/PFS formatter.
        # Use precisely its system partition locations and lengths.
        self.apa_calls += 1
        blocks = [(0, 128, 1), (128, 128, 0x100), (256, 256, 0x100),
                  (512, 512, 0x100), (1024, 1024, 0x100)]
        for i, (start, length, kind) in enumerate(blocks):
            nxt = blocks[i + 1][0] * MB if i + 1 < len(blocks) else 0
            prev = blocks[i - 1][0] * MB if i else blocks[-1][0] * MB
            os.pwrite(self.fd, apa_header(start * MB, length * MB, nxt, prev, kind), start * MB * 512)
        return 0

    def bytes(self, lba, count=1):
        return os.pread(self.fd, count * 512, lba * 512)

    def extract(self, directory, name, lba, sectors):
        path = directory / name
        with path.open("wb") as f:
            for offset in range(0, sectors, 2048):
                f.write(self.bytes(lba + offset, min(2048, sectors - offset)))
        return path

    def close(self):
        os.close(self.fd)


compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "A host C compiler is required"
with tempfile.TemporaryDirectory(prefix="hdd-format-") as temp:
    directory = Path(temp)
    library = directory / "format.so"
    includes = ["-I" + os.environ["HDD_FORMAT_TEST_INCLUDE"]] if os.environ.get("HDD_FORMAT_TEST_INCLUDE") else []
    libs = os.environ.get("HDD_FORMAT_TEST_LIBS", "-lz").split()
    subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                    "-I", str(ROOT / "include"), *includes,
                    *[str(ROOT / "src" / n) for n in
                      ("hdd_format.c", "hdd_format_upcase.c", "hdd_format_recovery.c", "hdd_apa_boundary.c")],
                    *libs, "-o", str(library)], check=True)
    lib = c.CDLL(str(library))
    lib.hddFormatPlan.argtypes = [c.POINTER(Plan), c.c_uint, c.c_uint32, c.c_uint32, c.c_uint32]
    lib.hddFormatExecute.argtypes = [c.POINTER(Plan), c.POINTER(Io)]
    lib.hddApaLimit.argtypes = [READ_ONE, c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32)]
    lib.hddApaCanAllocate.argtypes = [READ_ONE, c.c_void_p, c.c_uint32, c.c_uint32]
    lib.hddFormatUpper.argtypes = [c.c_uint32]
    lib.hddFormatUpper.restype = c.c_uint32

    p = Plan()
    for kind, size, reserve in [(3, 8 * 1024 * MB, 0), (EXFAT, 0xffffffff, 0),
                               (EXFAT, 32 * MB, 0), (APA, 1024 * MB, 0),
                               (JAIL, 8 * 1024 * MB, 8), (JAIL, 8 * 1024 * MB, 4096),
                               (EXFAT, 8 * 1024 * MB, 128)]:
        assert lib.hddFormatPlan(c.byref(p), kind, size, reserve, 1) < 0
    assert lib.hddFormatPlan(c.byref(p), JAIL, 8 * 1024 * MB, 512, 1) == 0
    assert p.apa_sectors == (640 + 512) * MB
    # Largest representable drive, non-aligned end, and capacities above 1 TiB.
    for size in (0x80001000, 0xc0000000, 0xfffffff0):
        for kind in (EXFAT, JAIL):
            assert lib.hddFormatPlan(c.byref(p), kind, size, 0, 1) == 0
            assert p.exfat_start + p.exfat_sectors <= size
            assert p.exfat_start % 2048 == p.exfat_sectors % 2048 == 0

    for kind in (APA, EXFAT, JAIL):
        sectors = 64 * 1024 * MB + 99
        image = Image(directory, sectors)
        reserve = 512 if kind == JAIL else 0
        assert lib.hddFormatPlan(c.byref(p), kind, sectors, reserve, 1780000000) == 0
        os.pwrite(image.fd, b"old metadata" * 80, 0)
        os.pwrite(image.fd, b"old GPT" * 50, (sectors - 1) * 512)
        assert lib.hddFormatExecute(c.byref(p), c.byref(image.io)) == 0
        assert image.flushes and not image.errors
        assert image.bytes(sectors - 34, 34) == bytes(34 * 512)
        assert image.apa_calls == (kind != EXFAT)
        if kind == APA:
            assert get32(image.bytes(0), 4) == 0x00415041
            image.close()
            continue
        mbr = image.bytes(0, 2)
        assert mbr[510:512] == b"\x55\xaa"
        entries = [(mbr[450 + i * 16], get32(mbr, 454 + i * 16), get32(mbr, 458 + i * 16)) for i in range(4)]
        if kind == EXFAT:
            assert entries == [(7, 2048, p.exfat_sectors), (0, 0, 0), (0, 0, 0), (0, 0, 0)]
        else:
            assert mbr[224:232] == b"APAJ-A2\0"
            assert get32(mbr) == sum(struct.unpack_from("<255I", mbr, 4)) & 0xffffffff
            assert entries[:3] == [(0x17, 2048, p.apa_sectors),
                                    (0x17, p.recovery_start, 32 * MB), (7, p.exfat_start, p.exfat_sectors)]
            limit = c.c_uint32()
            assert lib.hddApaLimit(image.one, None, sectors, c.byref(limit)) == 0
            assert limit.value == p.apa_sectors
            # Appending 512 MiB fits exactly; a 1 GiB aligned append crosses
            # the boundary even though the aggregate free space looks sufficient.
            assert lib.hddApaCanAllocate(image.one, None, sectors, 512 * MB) == 0
            assert lib.hddApaCanAllocate(image.one, None, sectors, 1024 * MB) == -errno.ENOSPC
            assert lib.hddApaCanAllocate(image.one, None, sectors, 8 * MB) == -errno.EINVAL
            common = bytearray(image.bytes(1024 * MB, 2))
            original_common = bytes(common)
            # Reuse an eligible free block before considering an append.
            struct.pack_into("<H", common, 72, 0)
            fix_apa(common)
            os.pwrite(image.fd, common, 1024 * MB * 512)
            assert lib.hddApaCanAllocate(image.one, None, sectors, 1024 * MB) == 0
            # Damaged APA headers fail closed.
            common[16] ^= 1
            os.pwrite(image.fd, common, 1024 * MB * 512)
            assert lib.hddApaCanAllocate(image.one, None, sectors, 128 * MB) == -errno.EIO
            os.pwrite(image.fd, original_common, 1024 * MB * 512)
            # Overlapping MBR ranges fail even with a correct APA checksum.
            bad_mbr = bytearray(mbr)
            set32(bad_mbr, 470, 2048)
            fix_apa(bad_mbr)
            os.pwrite(image.fd, bad_mbr, 0)
            assert lib.hddApaLimit(image.one, None, sectors, c.byref(limit)) == -errno.EIO
            assert limit.value == 0
            os.pwrite(image.fd, mbr, 0)
            # Existing 8 MiB partitions can be inspected, but a stock-driver
            # append after a mini tail must not enter its unsafe padding loop.
            mini_start = 2048 * MB
            mini_mbr = bytearray(mbr)
            set32(mini_mbr, 12, mini_start)
            fix_apa(mini_mbr)
            os.pwrite(image.fd, mini_mbr, 0)
            common = bytearray(original_common)
            set32(common, 8, mini_start)
            fix_apa(common)
            os.pwrite(image.fd, common, 1024 * MB * 512)
            os.pwrite(image.fd, apa_header(mini_start, 8 * MB, 0, 1024 * MB), mini_start * 512)
            assert lib.hddApaCanAllocate(image.one, None, sectors, 128 * MB) == -errno.ENOTSUP
            struct.pack_into("<H", common, 72, 0)
            fix_apa(common)
            os.pwrite(image.fd, common, 1024 * MB * 512)
            assert lib.hddApaCanAllocate(image.one, None, sectors, 128 * MB) == 0
            os.pwrite(image.fd, original_common, 1024 * MB * 512)
            os.pwrite(image.fd, bytes(1024), mini_start * 512)
            os.pwrite(image.fd, mbr, 0)

        boot = image.bytes(p.exfat_start, 24)
        assert boot[:12 * 512] == boot[12 * 512:]
        assert boot[:11] == b"\xeb\x76\x90EXFAT   "
        assert boot[108:111] == bytes([9, 6, 1])
        check = checksum(b for i, b in enumerate(boot[:11 * 512]) if i not in (106, 107, 112))
        assert boot[11 * 512:12 * 512] == struct.pack("<I", check) * 128
        assert struct.unpack_from("<Q", boot, 64)[0] == p.exfat_start
        assert struct.unpack_from("<Q", boot, 72)[0] == p.exfat_sectors
        fat, fatsize, heap, clusters, root = struct.unpack_from("<5I", boot, 80)
        assert heap + clusters * 64 <= p.exfat_sectors
        assert fatsize * 128 >= clusters + 2
        root_data = image.bytes(p.exfat_start + heap + (root - 2) * 64, 64)
        assert root_data[0:2] == b"\x83\x03" and root_data[2:8].decode("utf-16-le") == "PS2"
        assert root_data[32] == 0x81 and root_data[64] == 0x82
        bitmap_first = get32(root_data, 52)
        bitmap_size = struct.unpack_from("<Q", root_data, 56)[0]
        bitmap = image.bytes(p.exfat_start + heap + (bitmap_first - 2) * 64, (bitmap_size + 511) // 512)[:bitmap_size]
        allocated = root - 1
        assert sum(byte.bit_count() for byte in bitmap) == allocated
        assert bitmap[0] & ((1 << min(allocated, 8)) - 1) == (1 << min(allocated, 8)) - 1
        upcase_first = get32(root_data, 84)
        upcase_size = struct.unpack_from("<Q", root_data, 88)[0]
        upcase = image.bytes(p.exfat_start + heap + (upcase_first - 2) * 64, (upcase_size + 511) // 512)[:upcase_size]
        assert checksum(upcase) == get32(root_data, 68)
        words = iter(struct.unpack("<" + "H" * (len(upcase) // 2), upcase))
        mapping = []
        for word in words:
            if word == 0xffff and len(mapping) < 65535:
                run = next(words)
                mapping.extend(range(len(mapping), len(mapping) + run))
            else:
                mapping.append(word)
        assert len(mapping) == 65536
        assert all(mapping[cp] == lib.hddFormatUpper(cp) for cp in range(65536))
        assert mapping[97:123] == list(range(65, 91))
        fsck = os.environ.get("HDD_FORMAT_FSCK_EXFAT") or shutil.which("fsck.exfat")
        if fsck:
            # Sparse extraction retains large-volume geometry without copying
            # gigabytes of empty data into the host test directory.
            volume = directory / "exfat.img"
            with volume.open("wb") as f:
                f.truncate(p.exfat_sectors * 512)
                for start, length in ((0, 24), (fat, fatsize), (heap, allocated * 64)):
                    f.seek(start * 512)
                    f.write(image.bytes(p.exfat_start + start, length))
            subprocess.run([fsck, "-n", str(volume)], check=True, timeout=30)
            volume.unlink()
        if kind == JAIL:
            recovery = image.extract(directory, "recovery.img", p.recovery_start, 32 * MB)
            if shutil.which("e2fsck"):
                subprocess.run(["e2fsck", "-fn", str(recovery)], check=True, timeout=30)
            # Read the ext2 inode's direct/singly-indirect gzip block chain.
            table = image.bytes(p.recovery_start + 4 * 8, 64)
            node = table[11 * 128:12 * 128]
            size = get32(node, 4)
            pointers = list(struct.unpack_from("<12I", node, 40))
            if get32(node, 88):
                pointers.extend(struct.unpack("<1024I", image.bytes(p.recovery_start + get32(node, 88) * 8, 8)))
            compressed = b"".join(image.bytes(p.recovery_start + n * 8, 8) for n in pointers if n)[:size]
            snapshot = gzip.decompress(compressed)
            assert len(snapshot) == 128 * 1024 * 1024
            assert snapshot[:1024] == mbr
            assert snapshot[1024:] == bytes(len(snapshot) - 1024)
            recovery.unlink()
            assert "Backing up APA recovery metadata" in image.stages
        image.close()

    # Validate forged plans before the first write; exercise every metadata
    # I/O failure in a small exFAT format, including its read-back verification.
    image = Image(directory, 128 * MB)
    assert lib.hddFormatPlan(c.byref(p), EXFAT, image.sectors, 0, 1) == 0
    p.exfat_start += 1
    assert lib.hddFormatExecute(c.byref(p), c.byref(image.io)) == -errno.EINVAL
    assert image.calls == 0
    p.exfat_start -= 1
    assert lib.hddFormatExecute(c.byref(p), c.byref(image.io)) == 0
    calls = image.calls
    for fail in range(1, calls + 1):
        image.calls = 0
        image.fail_at = fail
        assert lib.hddFormatExecute(c.byref(p), c.byref(image.io)) == -errno.EIO, fail
    assert not image.errors
    image.close()

print("PASS: APA/exFAT/A2 layouts, PS2 label, 32 KiB clusters, boot/upcase checksums, recovery backup, allocation boundary and I/O failures")
