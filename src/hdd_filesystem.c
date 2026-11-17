#include "hdd_filesystem.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>

#define SECTOR_SIZE 512u
#define MAX_GPT_ENTRIES 128u
#define MAX_PROBE_READS (2u + MAX_GPT_ENTRIES * 2u)
#define MAX_EBR_LINKS 32u

typedef struct {
    HddFsReadSector read;
    void *context;
    uint32_t sectors;
    unsigned reads, format;
} FsProbe;

static uint32_t le16(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const unsigned char *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static uint64_t le64(const unsigned char *p)
{
    return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static uint32_t crc32(uint32_t crc, const unsigned char *p, unsigned length)
{
    unsigned i;
    while (length--) {
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static int readSector(FsProbe *p, uint64_t lba, unsigned char *sector)
{
    if (lba >= p->sectors)
        return -EINVAL;
    if (p->reads++ >= MAX_PROBE_READS)
        return -E2BIG;
    return p->read(p->context, (uint32_t)lba, sector);
}

/* Validate geometry, not the advisory FAT type/volume label strings.
 * exFAT layout: Microsoft exFAT specification, Main Boot Sector (section 3).
 * FAT32 classification uses the BPB's data-cluster count (at least 65525). */
static unsigned bootFormat(const unsigned char *b, uint64_t available)
{
    uint32_t sector_size, cluster_size, fats, reserved, total, fat_size, clusters;
    uint64_t overhead;
    if (b[510] != 0x55 || b[511] != 0xaa)
        return HDD_FS_UNKNOWN;
    if (!memcmp(b + 3, "EXFAT   ", 8)) {
        uint64_t length = le64(b + 72);
        uint32_t fat = le32(b + 80), size = le32(b + 84);
        uint32_t heap = le32(b + 88), count = le32(b + 92), root = le32(b + 96);
        unsigned i;
        if (b[0] != 0xeb || b[1] != 0x76 || b[2] != 0x90 ||
            le16(b + 104) != 0x100 || b[108] < 9 || b[108] > 12 ||
            b[109] > 25 - b[108] || (b[110] != 1 && b[110] != 2))
            return HDD_FS_UNKNOWN;
        for (i = 11; i < 64; i++)
            if (b[i]) return HDD_FS_UNKNOWN;
        if (length < 1 || length > (available >> (b[108] - 9)) ||
            fat < 24 || !size || !count || count > 0xfffffff5u ||
            root < 2 || (uint64_t)root >= (uint64_t)count + 2 ||
            (uint64_t)heap < fat + (uint64_t)size * b[110] ||
            heap + ((uint64_t)count << b[109]) > length ||
            ((uint64_t)size << b[108]) / 4 < (uint64_t)count + 2)
            return HDD_FS_UNKNOWN;
        return HDD_FS_EXFAT;
    }
    if (b[0] != 0xe9 && !(b[0] == 0xeb && b[2] == 0x90))
        return HDD_FS_UNKNOWN;
    sector_size = le16(b + 11);
    cluster_size = b[13];
    reserved = le16(b + 14);
    fats = b[16];
    total = le32(b + 32);
    fat_size = le32(b + 36);
    if (sector_size < 512 || sector_size > 4096 || (sector_size & (sector_size - 1)) ||
        !cluster_size || cluster_size > 128 || (cluster_size & (cluster_size - 1)) ||
        !reserved || (fats != 1 && fats != 2) || le16(b + 17) ||
        le16(b + 19) || le16(b + 22) || le16(b + 42) || !fat_size ||
        (uint64_t)total * (sector_size / 512) > available)
        return HDD_FS_UNKNOWN;
    overhead = reserved + (uint64_t)fats * fat_size;
    if (total <= overhead)
        return HDD_FS_UNKNOWN;
    clusters = (uint32_t)((total - overhead) / cluster_size);
    if (clusters < 65525 || clusters > 0x0ffffff5u ||
        le32(b + 44) < 2 || (uint64_t)le32(b + 44) >= (uint64_t)clusters + 2 ||
        (uint64_t)fat_size * sector_size / 4 < (uint64_t)clusters + 2)
        return HDD_FS_UNKNOWN;
    return HDD_FS_FAT32;
}

static int probeVolume(FsProbe *p, uint64_t start, uint64_t length)
{
    unsigned char boot[SECTOR_SIZE] __attribute__((aligned(64)));
    int ret;
    if (!start || !length || start >= p->sectors || length > p->sectors - start)
        return -EINVAL;
    ret = readSector(p, start, boot);
    if (ret < 0) return ret;
    p->format |= bootFormat(boot, length);
    return 0;
}

static int extendedType(unsigned type)
{
    return type == 0x05 || type == 0x0f || type == 0x85;
}

static int probeExtended(FsProbe *p, uint64_t base, uint64_t length)
{
    unsigned char ebr[SECTOR_SIZE] __attribute__((aligned(64)));
    uint64_t next = base, seen[MAX_EBR_LINKS];
    unsigned n = 0, i;
    int ret;
    if (!base || !length || base >= p->sectors || length > p->sectors - base)
        return -EINVAL;
    while (n < MAX_EBR_LINKS) {
        uint64_t start, size;
        for (i = 0; i < n; i++) if (seen[i] == next) return -EINVAL;
        seen[n++] = next;
        ret = readSector(p, next, ebr);
        if (ret < 0) return ret;
        if (ebr[510] != 0x55 || ebr[511] != 0xaa) return -EINVAL;
        start = next + le32(ebr + 454);
        size = le32(ebr + 458);
        if (ebr[450]) {
            if (start <= next || start < base || start >= base + length || size > base + length - start)
                return -EINVAL;
            ret = probeVolume(p, start, size);
            if (ret < 0) return ret;
        }
        if (!ebr[466]) return 0;
        if (!extendedType(ebr[466])) return -EINVAL;
        next = base + le32(ebr + 470);
        if (next < base || next >= base + length) return -EINVAL;
    }
    return -E2BIG;
}

static int probeGpt(FsProbe *p)
{
    unsigned char header[SECTOR_SIZE] __attribute__((aligned(64)));
    unsigned char entries[SECTOR_SIZE] __attribute__((aligned(64)));
    uint64_t table, first, last;
    uint32_t size, count, entry_size, expected_crc, header_crc, crc = ~0u;
    unsigned offset, used, i;
    uint64_t bytes, done;
    int ret = readSector(p, 1, header);
    if (ret < 0) return ret;
    if (memcmp(header, "EFI PART", 8) || le32(header + 8) != 0x10000 || le64(header + 24) != 1)
        return -EINVAL;
    size = le32(header + 12);
    header_crc = le32(header + 16);
    memset(header + 16, 0, 4);
    if (size < 92 || size > SECTOR_SIZE || (crc32(~0u, header, size) ^ ~0u) != header_crc)
        return -EINVAL;
    first = le64(header + 40); last = le64(header + 48);
    table = le64(header + 72); count = le32(header + 80); entry_size = le32(header + 84);
    expected_crc = le32(header + 88);
    if (!count || count > MAX_GPT_ENTRIES || entry_size < 128 || entry_size > SECTOR_SIZE ||
        (entry_size & (entry_size - 1)) || table < 2 || first > last || last >= p->sectors)
        return -EINVAL;
    bytes = (uint64_t)count * entry_size;
    if (table + (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE > first)
        return -EINVAL;
    for (done = 0; done < bytes; done += used) {
        ret = readSector(p, table + done / SECTOR_SIZE, entries);
        if (ret < 0) return ret;
        used = bytes - done < SECTOR_SIZE ? (unsigned)(bytes - done) : SECTOR_SIZE;
        crc = crc32(crc, entries, used);
        for (offset = 0; offset < used; offset += entry_size) {
            const unsigned char *e = entries + offset;
            uint64_t start = le64(e + 32), end = le64(e + 40);
            unsigned nonzero = 0;
            for (i = 0; i < 16; i++) nonzero |= e[i];
            if (!nonzero) continue;
            if (start < first || end > last || start > end) return -EINVAL;
            ret = probeVolume(p, start, end - start + 1);
            if (ret < 0) return ret;
        }
    }
    return (crc ^ ~0u) == expected_crc ? 0 : -EINVAL;
}

int hddProbeFilesystem(HddFsReadSector read_sector, void *context,
                       uint32_t sectors, unsigned *format)
{
    unsigned char mbr[SECTOR_SIZE] __attribute__((aligned(64)));
    FsProbe p = {read_sector, context, sectors, 0, 0};
    unsigned i;
    int ret;
    if (!format) return -EINVAL;
    *format = HDD_FS_UNKNOWN;
    if (!read_sector || !sectors) return -EINVAL;
    ret = readSector(&p, 0, mbr);
    if (ret < 0) return ret;
    p.format = bootFormat(mbr, sectors); /* A volume without a partition table. */
    if (p.format) { *format = p.format; return 0; }
    if (mbr[510] != 0x55 || mbr[511] != 0xaa) return 0;
    for (i = 0; i < 4; i++) {
        if (mbr[446 + i * 16 + 4] == 0xee) {
            ret = probeGpt(&p);
            if (ret == 0) *format = p.format;
            return ret;
        }
    }
    for (i = 0; i < 4; i++) {
        const unsigned char *entry = mbr + 446 + i * 16;
        unsigned type = entry[4];
        uint64_t start = le32(entry + 8), length = le32(entry + 12);
        if (!type) continue;
        if (entry[0] != 0 && entry[0] != 0x80) return -EINVAL;
        ret = extendedType(type) ? probeExtended(&p, start, length) : probeVolume(&p, start, length);
        if (ret < 0) return ret;
    }
    *format = p.format;
    return 0;
}

const char *hddFilesystemName(unsigned format)
{
    switch (format) {
        case HDD_FS_APA: return "APA";
        case HDD_FS_APA | HDD_FS_EXFAT: return "APA Jail (APA + exFAT)";
        case HDD_FS_APA | HDD_FS_FAT32: return "APA Jail (APA + FAT32)";
        case HDD_FS_APA | HDD_FS_EXFAT | HDD_FS_FAT32: return "APA Jail (APA + exFAT / FAT32)";
        case HDD_FS_EXFAT: return "exFAT";
        case HDD_FS_FAT32: return "FAT32";
        case HDD_FS_EXFAT | HDD_FS_FAT32: return "exFAT / FAT32";
        default: return "Unknown";
    }
}
