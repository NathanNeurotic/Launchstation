#include "hdd_format.h"
#include <errno.h>
#include <string.h>

#define APA_MIN_SECTORS 262144u
#define APA_EXISTING_MIN_SECTORS 16384u

static uint32_t get32(const unsigned char *b)
{
    return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static int header(HddFsReadSector read, void *ctx, uint32_t lba, unsigned char *b)
{
    uint32_t sum = 0;
    unsigned i;
    int ret = read(ctx, lba, b);
    if (ret < 0) return ret;
    ret = read(ctx, lba + 1, b + 512);
    if (ret < 0) return ret;
    for (i = 4; i < 1024; i += 4) sum += get32(b + i);
    if (get32(b + 4) != 0x00415041 || get32(b) != sum || get32(b + 64) != lba)
        return -EIO;
    return 0;
}

int hddApaLimit(HddFsReadSector read, void *ctx, uint32_t sectors, uint32_t *limit)
{
    unsigned char b[1024] __attribute__((aligned(64)));
    uint64_t starts[4], ends[4];
    unsigned i, j;
    int ret;
    if (!read || !limit || sectors < APA_MIN_SECTORS || sectors == UINT32_MAX) return -EINVAL;
    *limit = 0;
    ret = header(read, ctx, 0, b);
    if (ret < 0) return ret;
    if (b[510] != 0x55 || b[511] != 0xaa) {
        if (!memcmp(b + 224, "APAJ-", 5)) return -EIO;
        *limit = sectors;
        return 0;
    }
    /* APA Jail A2: first MBR entry covers APA; following entries cover
     * recovery and FAT/exFAT. Reject unfamiliar/overlapping hybrid maps. */
    if (b[450] != 0x17 || get32(b + 454) != 2048) return -EIO;
    for (i = 0; i < 4; i++) {
        const unsigned char *e = b + 446 + i * 16;
        starts[i] = get32(e + 8);
        ends[i] = starts[i] + get32(e + 12);
        if (!e[4]) {
            if (starts[i] || ends[i]) return -EIO;
            continue;
        }
        if (!starts[i] || ends[i] <= starts[i] || ends[i] > sectors) return -EIO;
        for (j = 0; j < i; j++)
            if (ends[j] > starts[i] && ends[i] > starts[j]) return -EIO;
    }
    *limit = (uint32_t)ends[0] & ~(APA_MIN_SECTORS - 1);
    return *limit >= APA_MIN_SECTORS ? 0 : -EIO;
}

int hddApaCanAllocate(HddFsReadSector read, void *ctx, uint32_t sectors, uint32_t size)
{
    unsigned char b[1024] __attribute__((aligned(64)));
    uint32_t limit, lba = 0, prev = 0, next, length, bins[32] = {0};
    uint64_t end = 0, candidate;
    unsigned steps = 0, i;
    int ret = hddApaLimit(read, ctx, sectors, &limit);
    if (ret < 0) return ret;
    if (size < APA_MIN_SECTORS || (size & (size - 1))) return -EINVAL;
    do {
        if (++steps > sectors / APA_EXISTING_MIN_SECTORS || lba >= limit || lba + 1 >= sectors) return -EIO;
        ret = header(read, ctx, lba, b);
        if (ret < 0) return ret;
        length = get32(b + 68);
        next = get32(b + 8);
        /* Existing mini partitions may be read; requests to create them are
         * still rejected above. This does not enable mini APA allocation. */
        if (length < APA_EXISTING_MIN_SECTORS || (length & (length - 1)) || lba % length ||
            (uint64_t)lba + length > limit || (lba && get32(b + 12) != prev)) return -EIO;
        if (b[72] == 0 && b[73] == 0) {
            for (i = 18; i < 32; i++)
                if (length == (1u << i) && !bins[i]) bins[i] = lba;
        }
        end = (uint64_t)lba + length;
        if (next && (next < end || next % APA_EXISTING_MIN_SECTORS)) return -EIO;
        prev = lba;
        lba = next;
    } while (next);
    for (i = 18; i < 32; i++)
        if ((1u << i) >= size && bins[i])
            return (uint64_t)bins[i] + size <= limit ? 0 : -ENOSPC;
    /* The stock 128 MiB allocator cannot pad a mini-partition tail safely.
     * Reusing a properly aligned free block above is still possible. */
    if (end % APA_MIN_SECTORS) return -ENOTSUP;
    candidate = (end + size - 1) & ~((uint64_t)size - 1);
    return candidate + size <= limit ? 0 : -ENOSPC;
}
