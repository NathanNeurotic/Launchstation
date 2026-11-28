"""Check the production ATA block callbacks against word-aligned DMA."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "iop/ata_bd/src/ps2atad.c").read_text()
start = source.index("static int ata_bd_transfer_unaligned(")
end = source.index("static void ata_bd_flush(", start)
sector_size = re.search(r"^#define ATA_BD_SECTOR_SIZE\s+\d+$", source, re.M)[0]

code = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t uiptr;
struct block_device { int devNr; };
enum { ATA_DIR_READ, ATA_DIR_WRITE, SECTORS = 4 };
""" + sector_size + r"""

static const u64 first_lba = UINT64_C(0x100000007);
static u8 disk[ATA_BD_SECTOR_SIZE * SECTORS];
static void *direct_buffer;
static int calls, fail_call, expected_device;

static int ata_device_sector_io64(int device, void *buffer, u64 lba, u32 count, int direction)
{
    assert(device == expected_device && lba >= first_lba);
    u64 offset = lba - first_lba;
    assert(offset <= SECTORS && count <= SECTORS - offset);
    assert(!((uintptr_t)buffer & 3));
    if (direct_buffer) {
        assert(buffer == direct_buffer && offset == 0 && count == SECTORS);
    } else {
        assert(offset == (unsigned)calls && count == 1);
    }
    calls++;
    if (calls == fail_call) {
        /* Failed reads must not publish bytes from the failed DMA command. */
        if (direction == ATA_DIR_READ)
            memset(buffer, 0x99, count * ATA_BD_SECTOR_SIZE);
        return -1;
    }
    u8 *sector = disk + offset * ATA_BD_SECTOR_SIZE;
    if (direction == ATA_DIR_READ)
        memcpy(buffer, sector, count * ATA_BD_SECTOR_SIZE);
    else
        memcpy(sector, buffer, count * ATA_BD_SECTOR_SIZE);
    return 0;
}
""" + source[start:end] + r"""

static void check_value(const u8 *bytes, size_t count, u8 value)
{
    for (size_t i = 0; i < count; i++)
        assert(bytes[i] == value);
}

int main(void)
{
    struct block_device bd = {.devNr = 1};
    expected_device = bd.devNr;
    u8 memory[sizeof(disk) + 16] __attribute__((aligned(4)));
    u8 expected[sizeof(disk)];
    for (size_t i = 0; i < sizeof(expected); i++)
        expected[i] = i * 41 + i / 251;

    for (unsigned misalignment = 0; misalignment < 4; misalignment++) {
        u8 *buffer = memory + 4 + misalignment;
        int expected_calls = misalignment ? SECTORS : 1;
        direct_buffer = misalignment ? NULL : buffer;
        memset(memory, 0x55, sizeof(memory));
        memcpy(buffer, expected, sizeof(expected));
        calls = 0;
        assert(ata_bd_write(&bd, first_lba, buffer, SECTORS) == SECTORS);
        assert(calls == expected_calls && !memcmp(disk, expected, sizeof(disk)));
        assert(!memcmp(buffer, expected, sizeof(expected)));

        memset(buffer, 0, sizeof(expected));
        calls = 0;
        assert(ata_bd_read(&bd, first_lba, buffer, SECTORS) == SECTORS);
        assert(calls == expected_calls && !memcmp(buffer, expected, sizeof(expected)));
        check_value(memory, 4 + misalignment, 0x55);
        check_value(buffer + sizeof(expected), 12 - misalignment, 0x55);
    }

    u8 *unaligned = memory + 1;
    size_t sector = ATA_BD_SECTOR_SIZE;
    fail_call = 2;
    calls = 0;
    memset(unaligned, 0xcc, sizeof(expected));
    assert(ata_bd_read(&bd, first_lba, unaligned, SECTORS) == -EIO);
    assert(calls == 2 && !memcmp(unaligned, expected, sector));
    check_value(unaligned + sector, sizeof(expected) - sector, 0xcc);

    calls = 0;
    memset(disk, 0xcc, sizeof(disk));
    memcpy(unaligned, expected, sizeof(expected));
    assert(ata_bd_write(&bd, first_lba, unaligned, SECTORS) == -EIO);
    assert(calls == 2 && !memcmp(disk, expected, sector));
    check_value(disk + sector, sizeof(disk) - sector, 0xcc);
    assert(!memcmp(unaligned, expected, sizeof(expected)));

    direct_buffer = memory;
    fail_call = 1;
    calls = 0;
    assert(ata_bd_read(&bd, first_lba, memory, SECTORS) == -EIO && calls == 1);
    calls = 0;
    assert(ata_bd_write(&bd, first_lba, memory, SECTORS) == -EIO && calls == 1);
    calls = 0;
    assert(ata_bd_read(&bd, first_lba, unaligned, 0) == 0 && calls == 0);
    assert(ata_bd_write(&bd, first_lba, unaligned, 0) == 0 && calls == 0);
    puts("PASS: ATA DMA alignment for reads and writes, direct aligned transfers, 64-bit LBAs, and I/O errors");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="ata-bd-alignment-") as tmp:
    tmp = Path(tmp)
    (tmp / "test.c").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(tmp / "test.c"), "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test")], check=True)
