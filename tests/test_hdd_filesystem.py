"""Detect real BPB geometry through the production read-only HDD probe."""
from pathlib import Path
import ctypes
import errno
import random
import shutil
import struct
import subprocess
import tempfile
import zlib

root = Path(__file__).resolve().parents[1]
APA, EXFAT, FAT32 = 1, 2, 4


def u16(b, offset, value):
    struct.pack_into("<H", b, offset, value)


def u32(b, offset, value):
    struct.pack_into("<I", b, offset, value)


def u64(b, offset, value):
    struct.pack_into("<Q", b, offset, value)


def signed_sector():
    b = bytearray(512)
    b[510:] = b"\x55\xaa"
    return b


def exfat(total=1_000_000, sector_shift=9):
    b = signed_sector()
    b[:3] = b"\xeb\x76\x90"
    b[3:11] = b"EXFAT   "
    u64(b, 72, total)
    for offset, value in ((80, 24), (84, 8192), (88, 8216), (92, 100_000), (96, 2)):
        u32(b, offset, value)
    u16(b, 104, 0x100)
    b[108], b[109], b[110] = sector_shift, 0, 1
    return b


def fat32(total=1_000_000, sector_size=512):
    b = signed_sector()
    b[:3] = b"\xeb\x58\x90"
    b[3:11] = b"MSWIN4.1"
    u16(b, 11, sector_size)
    b[13], b[16] = 8, 2
    u16(b, 14, 32)
    u32(b, 32, total)
    u32(b, 36, 1024)
    u32(b, 44, 2)
    b[82:90] = b"FAT32   "
    return b


def entry(b, index, kind, start, length):
    offset = 446 + 16 * index
    b[offset + 4] = kind
    u32(b, offset + 8, start)
    u32(b, offset + 12, length)


def gpt(volumes, sectors=4_000_000, entry_size=128, count=128):
    mbr = signed_sector()
    entry(mbr, 0, 0xEE, 1, sectors - 1)
    table = bytearray(entry_size * count)
    for index, (start, length, _) in enumerate(volumes):
        offset = index * entry_size
        table[offset:offset + 16] = bytes.fromhex("a2a0d0ebe5b9334487c068b6b72699c7")
        table[offset + 16] = index + 1
        u64(table, offset + 32, start)
        u64(table, offset + 40, start + length - 1)
    header = bytearray(512)
    header[:8] = b"EFI PART"
    u32(header, 8, 0x10000)
    u32(header, 12, 92)
    u64(header, 24, 1)
    u64(header, 32, sectors - 1)
    u64(header, 40, 2 + (len(table) + 511) // 512)
    u64(header, 48, sectors - 34)
    u64(header, 72, 2)
    u32(header, 80, count)
    u32(header, 84, entry_size)
    u32(header, 88, zlib.crc32(table))
    u32(header, 16, zlib.crc32(header[:92]))
    image = {0: mbr, 1: header}
    image.update({2 + i // 512: table[i:i + 512] for i in range(0, len(table), 512)})
    image.update({start: boot for start, _, boot in volumes})
    return image


compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "A host C compiler is required"
with tempfile.TemporaryDirectory(prefix="hdd-filesystem-") as directory:
    library = Path(directory) / "probe.so"
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-Wformat-truncation=2", "-fPIC", "-shared", "-I", str(root / "include"),
                    str(root / "src/hdd_filesystem.c"), "-o", str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    Reader = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p)
    lib.hddProbeFilesystem.argtypes = [Reader, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint)]
    lib.hddProbeFilesystem.restype = ctypes.c_int
    lib.hddFilesystemName.argtypes = [ctypes.c_uint]
    lib.hddFilesystemName.restype = ctypes.c_char_p

    def probe(image, sectors=4_000_000, errors=None):
        calls, violations = [], []

        @Reader
        def read(context, lba, destination):
            calls.append(lba)
            if context != 123 or lba >= sectors or len(calls) > 258 or destination % 64:
                violations.append((context, lba, destination))
                return -errno.EINVAL
            if errors and lba in errors:
                return errors[lba]
            ctypes.memmove(destination, bytes(image.get(lba, bytearray(512))), 512)
            return 0

        result = ctypes.c_uint(0xFF)
        ret = lib.hddProbeFilesystem(read, 123, sectors, ctypes.byref(result))
        assert not violations, violations
        return ret, result.value, calls

    for boot, expected in ((exfat(), EXFAT), (fat32(), FAT32)):
        ret, fmt, calls = probe({0: boot})
        assert (ret, fmt, calls) == (0, expected, [0])
    # 4 KiB filesystem sectors are measured in physical 512-byte read units.
    assert probe({0: exfat(sector_shift=12)}, 8_000_000)[:2] == (0, EXFAT)
    assert probe({0: fat32(sector_size=4096)}, 8_000_000)[:2] == (0, FAT32)
    assert probe({0: exfat(sector_shift=12)}, 1_000_000)[:2] == (0, 0)
    unlabeled = fat32(); unlabeled[82:90] = b"IGNOREME"
    assert probe({0: unlabeled})[:2] == (0, FAT32)
    # Signatures and partition type bytes alone cannot establish the format.
    for offset, value in ((13, 3), (16, 0), (17, 1), (22, 1), (42, 1), (44, 0)):
        broken = fat32(); broken[offset] = value
        assert probe({0: broken})[:2] == (0, 0), offset
    for offset, value in ((11, 1), (108, 8), (108, 13), (109, 31), (110, 0), (96, 0)):
        broken = exfat(); broken[offset] = value
        assert probe({0: broken})[:2] == (0, 0), offset
    too_small = fat32(total=10000)
    assert probe({0: too_small})[:2] == (0, 0)
    mbr = signed_sector(); entry(mbr, 0, 0x07, 2048, 1_000_000)
    assert probe({0: mbr, 2048: exfat()})[:2] == (0, EXFAT)
    assert probe({0: mbr})[:2] == (0, 0)
    entry(mbr, 0, 0x83, 2048, 1_000_000)
    assert probe({0: mbr, 2048: fat32()})[:2] == (0, FAT32)
    entry(mbr, 1, 0x07, 1_100_000, 1_000_000)
    assert probe({0: mbr, 2048: fat32(), 1_100_000: exfat()})[:2] == (0, FAT32 | EXFAT)
    # APA-Jail A2: APA header + marker and an MBR with hidden APA/recovery
    # areas followed by the FAT/exFAT data volume (PSBBN installer layout).
    jail = bytearray(1024)
    jail[4:8] = b"APA\0"
    jail[224:232] = b"APAJ-A2\0"
    jail[510:512] = b"\x55\xaa"
    entry(jail, 0, 0x17, 2048, 262144)
    entry(jail, 1, 0x17, 264192, 65536)
    entry(jail, 2, 0x07, 329728, 1_000_000)
    u32(jail, 0, sum(struct.unpack("<255I", jail[4:])) & 0xFFFFFFFF)
    assert probe({0: jail[:512], 1: jail[512:], 329728: exfat()})[:2] == (0, EXFAT)
    assert probe({0: jail[:512], 1: jail[512:], 329728: fat32()})[:2] == (0, FAT32)
    invalid = signed_sector(); entry(invalid, 0, 7, 0xFFFFFFF0, 128)
    assert probe({0: invalid}, 0xFFFFFFFF) == (-errno.EINVAL, 0, [0])
    invalid = bytearray(mbr); invalid[446] = 0x42
    assert probe({0: invalid})[:2] == (-errno.EINVAL, 0)
    # Logical partition offsets are relative to each EBR, links to the base.
    extended = signed_sector(); entry(extended, 0, 0x0F, 2048, 3_000_000)
    ebr1 = signed_sector(); entry(ebr1, 0, 7, 63, 1_000_000)
    entry(ebr1, 1, 0x0F, 1_100_000, 1_900_000)
    ebr2 = signed_sector(); entry(ebr2, 0, 0x0C, 63, 1_000_000)
    image = {0: extended, 2048: ebr1, 2111: exfat(), 1_102_048: ebr2, 1_102_111: fat32()}
    assert probe(image)[:2] == (0, EXFAT | FAT32)
    entry(ebr2, 1, 0x0F, 0, 3_000_000)
    assert probe(image)[:2] == (-errno.EINVAL, 0)
    image = gpt([(2048, 1_000_000, exfat()), (1_100_000, 1_000_000, fat32())])
    assert probe(image)[:2] == (0, EXFAT | FAT32)
    damaged = dict(image); damaged[1] = bytearray(image[1]); damaged[1][16] ^= 1
    assert probe(damaged) == (-errno.EINVAL, 0, [0, 1])
    damaged = dict(image); damaged[33] = bytearray(image[33]); damaged[33][511] ^= 1
    assert probe(damaged)[:2] == (-errno.EINVAL, 0)
    for table, start, count, entry_size in ((0xFFFFFFFFFFFFFFFF, 2048, 128, 128),
                                           (2, 0x100000000, 128, 128),
                                           (2, 2048, 129, 128), (2, 2048, 128, 127)):
        damaged = gpt([(start, 1_000_000, exfat())])
        h = damaged[1]
        u64(h, 72, table); u32(h, 80, count); u32(h, 84, entry_size)
        u32(h, 16, 0); u32(h, 16, zlib.crc32(h[:92]))
        assert probe(damaged)[:2] == (-errno.EINVAL, 0)
    volumes = [(2048 + i * 1_100_000, 1_000_000, exfat()) for i in range(128)]
    ret, fmt, calls = probe(gpt(volumes, sectors=150_000_000, entry_size=512), 150_000_000)
    assert (ret, fmt, len(calls)) == (0, EXFAT, 258)
    assert probe({}, errors={0: -errno.EIO}) == (-errno.EIO, 0, [0])
    assert probe(image, errors={2048: -errno.ETIMEDOUT})[:2] == (-errno.ETIMEDOUT, 0)
    assert probe({}) == (0, 0, [0])
    assert probe({}, sectors=0) == (-errno.EINVAL, 0, [])
    # Random malformed tables must stay bounded and never wrap sector addresses.
    random.seed(0)
    for _ in range(500):
        data = bytearray(random.randbytes(512)); data[510:] = b"\x55\xaa"
        assert probe({0: data})[1] == 0
    for fmt, name in ((APA, b"APA"), (APA | EXFAT, b"APA Jail (APA + exFAT)"),
                      (APA | FAT32, b"APA Jail (APA + FAT32)"), (APA | EXFAT | FAT32, b"APA Jail (APA + exFAT / FAT32)"),
                      (EXFAT, b"exFAT"), (FAT32, b"FAT32"),
                      (EXFAT | FAT32, b"exFAT / FAT32"), (0, b"Unknown")):
        assert lib.hddFilesystemName(fmt) == name

print("PASS: read-only exFAT/FAT32 BPBs, MBR/GPT/extended layouts, mixed volumes, 4K sectors, CRCs, read errors and malformed-table bounds")
