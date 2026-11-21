/* MBR/exFAT writer. Geometry follows Microsoft's exFAT specification:
 * https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification
 * APA Jail A2 layout follows PSBBN Definitive Project's installer. */
#include "hdd_format.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define MB 2048u
#define CLUSTER 64u
#define ALIGN 2048u
#define MIN_EXFAT_MB 64u

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static void put64(unsigned char *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t get32(const unsigned char *p)
{ return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t checksum(uint32_t sum, unsigned char byte)
{ return (sum >> 1 | sum << 31) + byte; }

/* Match the unmodified driver's apaGetPartitionMax and formatPartList.
 * Small drives use smaller system partitions; most drives use 2 GiB total.
 * Below 8 GiB its size cap can fall below our 128 MiB partition minimum. */
static uint32_t apaBaseMb(uint32_t sectors)
{
    uint32_t maximum = 1, target = sectors >> 6, total = 128, size = 128;
    unsigned i;
    while (maximum < target) maximum <<= 1;
    maximum /= MB;
    if (sectors < 8u * 1024 * MB || maximum < 128) return 0;
    for (i = 0; i < 4; i++) {
        total += size;
        size *= 2;
        if (size > maximum) size = maximum;
    }
    return total;
}

uint32_t hddFormatMaxReserve(uint32_t sectors)
{
    uint32_t half_mb = sectors / (MB * 2), base = apaBaseMb(sectors);
    if (sectors == UINT32_MAX || !base || half_mb < base) return 0;
    return (half_mb - base) & ~127u;
}

int hddFormatPlan(HddFormatPlan *p, unsigned kind, uint32_t sectors,
                  uint32_t reserve_mb, uint32_t serial)
{
    uint32_t end, base = apaBaseMb(sectors);
    if (!p) return -EINVAL;
    memset(p, 0, sizeof(*p));
    if (kind > HDD_FORMAT_APA_JAIL || sectors == UINT32_MAX || sectors < MIN_EXFAT_MB * MB + ALIGN)
        return -EINVAL;
    if (kind != HDD_FORMAT_APA_JAIL && reserve_mb) return -EINVAL;
    if (kind != HDD_FORMAT_EXFAT && !base) return -ENOSPC;
    if (kind == HDD_FORMAT_APA_JAIL &&
        ((reserve_mb & 127) || reserve_mb > hddFormatMaxReserve(sectors))) return -EINVAL;
    p->kind = kind; p->sectors = sectors; p->serial = serial;
    p->reserve_mb = reserve_mb;
    if (kind == HDD_FORMAT_APA) return 0;
    p->exfat_start = ALIGN;
    if (kind == HDD_FORMAT_APA_JAIL) {
        p->apa_sectors = (base + reserve_mb) * MB;
        p->recovery_start = ALIGN + p->apa_sectors;
        p->exfat_start = p->recovery_start + HDD_FORMAT_RECOVERY_MB * MB;
    }
    end = sectors & ~(ALIGN - 1);
    if (end <= p->exfat_start || end - p->exfat_start < MIN_EXFAT_MB * MB) return -ENOSPC;
    p->exfat_sectors = end - p->exfat_start;
    return 0;
}

static int zero(const HddFormatIo *io, uint32_t lba, uint32_t count, const char *stage)
{
    uint32_t done = 0;
    int ret;
    while (done < count) {
        uint32_t n = count - done > 2048 ? 2048 : count - done;
        if (io->progress) io->progress(io->context, stage, done, count);
        ret = io->zero(io->context, lba + done, n);
        if (ret < 0) return ret;
        done += n;
    }
    return 0;
}

static void mbrEntry(unsigned char *b, unsigned index, unsigned type, uint32_t lba, uint32_t size)
{
    unsigned char *e = b + 446 + index * 16;
    memset(e, 0, 16);
    /* Saturated CHS: LBA fields are authoritative. */
    e[1] = e[5] = 0xfe; e[2] = e[6] = 0xff; e[3] = e[7] = 0xff;
    e[4] = (unsigned char)type;
    put32(e + 8, lba); put32(e + 12, size);
}

static unsigned makeUpcase(unsigned char *out, uint32_t *sum)
{
    unsigned cp = 0, length = 0, i;
    while (cp < 65536) {
        unsigned run = 0;
        while (cp + run < 65536 && run < 65535 && hddFormatUpper(cp + run) == cp + run) run++;
        if (run >= 2) {
            put16(out + length, 0xffff); put16(out + length + 2, run);
            length += 4; cp += run;
        } else {
            put16(out + length, hddFormatUpper(cp)); length += 2; cp++;
        }
    }
    *sum = 0;
    for (i = 0; i < length; i++) *sum = checksum(*sum, out[i]);
    return length;
}

static int validApa(const unsigned char *b)
{
    unsigned i;
    uint32_t sum = 0;
    for (i = 4; i < 1024; i += 4) sum += get32(b + i);
    return get32(b + 4) == 0x00415041 && get32(b) == sum;
}

static int exfat(const HddFormatPlan *p, const HddFormatIo *io, unsigned char *work,
                  const unsigned char *upcase, unsigned upcase_size, uint32_t upcase_sum)
{
    uint32_t fat_size, heap, clusters, bitmap_size, bitmap_clusters, upcase_clusters;
    uint32_t upcase_first, root, allocated, i, j, sum = 0, lba;
    uint32_t max_clusters = p->exfat_sectors / CLUSTER;
    int ret;
    /* A conservative FAT size avoids geometry oscillations at alignment boundaries. */
    fat_size = (uint32_t)(((uint64_t)max_clusters + 2) * 4 + 511) / 512;
    heap = (ALIGN + fat_size + ALIGN - 1) & ~(ALIGN - 1);
    if (heap >= p->exfat_sectors) return -ENOSPC;
    clusters = (p->exfat_sectors - heap) / CLUSTER;
    bitmap_size = (clusters + 7) / 8;
    bitmap_clusters = (bitmap_size + 32767) / 32768;
    upcase_clusters = (upcase_size + 32767) / 32768;
    upcase_first = 2 + bitmap_clusters;
    root = upcase_first + upcase_clusters;
    allocated = root - 1;
    if (clusters <= allocated) return -ENOSPC;
    ret = zero(io, p->exfat_start + ALIGN, fat_size, "Clearing exFAT allocation table");
    if (ret < 0) return ret;
    /* Only the allocated metadata chains have FAT entries. */
    for (i = 0; i <= root / 128; i++) {
        memset(work, 0, 512);
        for (j = 0; j < 128; j++) {
            uint32_t entry = i * 128 + j, value = 0;
            if (entry == 0) value = 0xfffffff8u;
            else if (entry == 1 || entry == root || entry + 1 == upcase_first || entry + 1 == root)
                value = 0xffffffffu;
            else if (entry >= 2 && entry < root) value = entry + 1;
            put32(work + j * 4, value);
        }
        ret = io->write(io->context, p->exfat_start + ALIGN + i, 1, work);
        if (ret < 0) return ret;
    }
    for (i = 0; i < bitmap_clusters; i++) {
        memset(work, 0, 32768);
        for (j = 0; j < 32768 && (uint64_t)i * 32768 + j < bitmap_size; j++) {
            uint32_t bit = (i * 32768 + j) * 8;
            if (bit < allocated) work[j] = (unsigned char)(allocated - bit >= 8 ? 255 : (1u << (allocated - bit)) - 1);
        }
        ret = io->write(io->context, p->exfat_start + heap + i * CLUSTER, CLUSTER, work);
        if (ret < 0) return ret;
    }
    for (i = 0; i < upcase_clusters; i++) {
        unsigned n = upcase_size - i * 32768;
        if (n > 32768) n = 32768;
        memset(work, 0, 32768); memcpy(work, upcase + i * 32768, n);
        ret = io->write(io->context, p->exfat_start + heap + (upcase_first - 2 + i) * CLUSTER, CLUSTER, work);
        if (ret < 0) return ret;
    }
    memset(work, 0, 32768);
    work[0] = 0x83; work[1] = 3;
    put16(work + 2, 'P'); put16(work + 4, 'S'); put16(work + 6, '2');
    work[32] = 0x81; put32(work + 52, 2); put64(work + 56, bitmap_size);
    work[64] = 0x82; put32(work + 68, upcase_sum);
    put32(work + 84, upcase_first); put64(work + 88, upcase_size);
    ret = io->write(io->context, p->exfat_start + heap + (root - 2) * CLUSTER, CLUSTER, work);
    if (ret < 0) return ret;

    /* Commit both complete boot regions after the metadata. */
    memset(work, 0, 12 * 512);
    memcpy(work, "\xeb\x76\x90" "EXFAT   ", 11);
    put64(work + 64, p->exfat_start); put64(work + 72, p->exfat_sectors);
    put32(work + 80, ALIGN); put32(work + 84, fat_size); put32(work + 88, heap);
    put32(work + 92, clusters); put32(work + 96, root); put32(work + 100, p->serial);
    put16(work + 104, 0x100);
    work[108] = 9; work[109] = 6; work[110] = 1; work[111] = 0x80;
    work[112] = (unsigned char)((uint64_t)allocated * 100 / clusters);
    work[510] = 0x55; work[511] = 0xaa;
    for (i = 1; i <= 8; i++) put32(work + i * 512 + 508, 0xaa550000u);
    for (i = 0; i < 11 * 512; i++)
        if (i != 106 && i != 107 && i != 112) sum = checksum(sum, work[i]);
    for (i = 11 * 512; i < 12 * 512; i += 4) put32(work + i, sum);
    ret = io->write(io->context, p->exfat_start + 12, 12, work);
    if (ret >= 0) ret = io->write(io->context, p->exfat_start, 12, work);
    if (ret < 0) return ret;
    /* Verify both boot regions against the generated bytes before advertising
     * the partition in the MBR. The read buffer is separate from the work area. */
    for (lba = 0; lba < 24; lba++) {
        unsigned char actual[512];
        ret = io->read(io->context, p->exfat_start + lba, 1, actual);
        if (ret < 0) return ret;
        if (memcmp(actual, work + (lba % 12) * 512, 512)) return -EIO;
    }
    return 0;
}

int hddFormatExecute(const HddFormatPlan *p, const HddFormatIo *io)
{
    HddFormatPlan expected;
    unsigned char *work = NULL, *upcase = NULL, header[1024];
    unsigned upcase_size = 0, i;
    uint32_t upcase_sum = 0, sum = 0;
    int ret;
    if (!p || !io || !io->read || !io->write || !io->zero || !io->flush || !io->apa) return -EINVAL;
    ret = hddFormatPlan(&expected, p->kind, p->sectors, p->reserve_mb, p->serial);
    if (ret < 0 || memcmp(&expected, p, sizeof(expected))) return ret < 0 ? ret : -EINVAL;
    work = malloc(32768);
    if (!work) return -ENOMEM;
    if (p->kind != HDD_FORMAT_APA) {
        upcase = malloc(131076);
        if (!upcase) { ret = -ENOMEM; goto done; }
        upcase_size = makeUpcase(upcase, &upcase_sum);
    }
    /* Clear the boot/journal area and old GPT backup. For A2, clear the
     * complete __mbr area so its 128 MiB recovery snapshot compresses well. */
    ret = zero(io, 0, p->kind == HDD_FORMAT_APA_JAIL ? 128u * MB : ALIGN, "Clearing old disk metadata");
    if (ret >= 0) ret = zero(io, p->sectors - 34, 34, "Clearing old GPT backup");
    if (ret < 0) goto done;
    if (p->kind != HDD_FORMAT_EXFAT) {
        if (io->progress) io->progress(io->context, "Formatting APA system partitions", 0, 0);
        ret = io->apa(io->context);
        if (ret < 0) goto done;
        ret = io->read(io->context, 0, 2, work);
        if (ret < 0) goto done;
        if (!validApa(work) || get32(work + 64) != 0 || get32(work + 68) != 128u * MB ||
            get32(work + 12) > p->sectors - 2) { ret = -EIO; goto done; }
        ret = io->read(io->context, get32(work + 12), 2, header);
        if (ret < 0) goto done;
        if (!validApa(header) || get32(header + 64) != get32(work + 12) || get32(header + 8) ||
            (uint64_t)get32(header + 64) + get32(header + 68) != (uint64_t)apaBaseMb(p->sectors) * MB) {
            ret = -EIO; goto done;
        }
        if (p->kind == HDD_FORMAT_APA) goto done;
    }
    ret = exfat(p, io, work, upcase, upcase_size, upcase_sum);
    if (ret < 0) goto done;
    memset(header, 0, sizeof(header));
    if (p->kind == HDD_FORMAT_APA_JAIL) {
        ret = io->read(io->context, 0, 2, header);
        if (ret < 0) goto done;
        for (i = 4; i < 1024; i += 4) sum += get32(header + i);
        if (get32(header + 4) != 0x00415041 || get32(header) != sum) { ret = -EIO; goto done; }
        memcpy(header + 224, "APAJ-A2\0", 8);
        memset(header + 446, 0, 64);
        mbrEntry(header, 0, 0x17, ALIGN, p->apa_sectors);
        mbrEntry(header, 1, 0x17, p->recovery_start, HDD_FORMAT_RECOVERY_MB * MB);
        mbrEntry(header, 2, 0x07, p->exfat_start, p->exfat_sectors);
    } else mbrEntry(header, 0, 0x07, p->exfat_start, p->exfat_sectors);
    header[510] = 0x55; header[511] = 0xaa;
    if (p->kind == HDD_FORMAT_APA_JAIL) {
        sum = 0;
        for (i = 4; i < 1024; i += 4) sum += get32(header + i);
        put32(header, sum);
    }
    ret = io->write(io->context, 0, p->kind == HDD_FORMAT_APA_JAIL ? 2 : 1, header);
    if (ret < 0) goto done;
    if (p->kind == HDD_FORMAT_APA_JAIL) ret = hddFormatRecovery(p, io);
    if (ret >= 0) {
        ret = io->read(io->context, 0, p->kind == HDD_FORMAT_APA_JAIL ? 2 : 1, work);
        if (ret >= 0 && memcmp(work, header, p->kind == HDD_FORMAT_APA_JAIL ? 1024 : 512)) ret = -EIO;
    }
done:
    /* Flush on failures too: the caller must reload the storage stack before
     * allowing any further disk access, irrespective of the final result. */
    if (ret >= 0) ret = io->flush(io->context);
    else io->flush(io->context);
    free(upcase); free(work);
    return ret;
}
