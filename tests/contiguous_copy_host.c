/* Host harness for tests/test_contiguous_copy.py.
 *
 * Runs the FatFs sources of Launchstation's FAT/exFAT driver (iop/bdmfs_fatfs) and its
 * fs_reserve_contiguous() on a volume held in memory. Each scenario formats a volume, leaves holes
 * in it the way deleted files do, and copies one large file into it the way the file manager does:
 * create, optionally reserve, then 128 KB writes. It prints how many separate runs of clusters the
 * copy ended up in, counted the way the driver's get_frag_list() counts them for Neutrino.
 *
 * This is the allocator and the request, not the PS2 driver glue, a block device or a console. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64; /* the SDK's types, which its FatFs changes use */

#include "../iop/bdmfs_fatfs/src/fatfs/ff.c" /* one unit: the fragment walk needs the static get_fat() */
#include "../iop/bdmfs_fatfs/src/reserve.c"

#define SECTOR 512
static BYTE *disk;
static LBA_t disk_sectors;

DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (sector + count > disk_sectors) return RES_PARERR;
    memcpy(buff, disk + sector * SECTOR, (size_t)count * SECTOR);
    return RES_OK;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    if (sector + count > disk_sectors) return RES_PARERR;
    memcpy(disk + sector * SECTOR, buff, (size_t)count * SECTOR);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t *)buff = disk_sectors; return RES_OK;
        case GET_SECTOR_SIZE: *(WORD *)buff = SECTOR; return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    }
    return RES_PARERR;
}
DWORD get_fattime(void) { return ((DWORD)(2026 - 1980) << 25) | (1 << 21) | (1 << 16); }
int ff_mutex_create(int vol) { (void)vol; return 1; }
void ff_mutex_delete(int vol) { (void)vol; }
int ff_mutex_take(int vol) { (void)vol; return 1; }
void ff_mutex_give(int vol) { (void)vol; }

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define KB 1024u
#define MB (1024u * 1024u)
#define CHUNK (128 * KB) /* the file manager's write size for mass: and mx4sio: */

static BYTE pattern(unsigned long long offset, unsigned seed) { return (BYTE)(offset * 2654435761u >> 13 ^ seed); }

static void fill(BYTE *buf, unsigned long long offset, UINT len, unsigned seed)
{
    for (UINT i = 0; i < len; i++) buf[i] = pattern(offset + i, seed);
}

/* Same walk as get_frag_list() in fs_driver.c: a new run wherever the next cluster is not the
 * following one. For an exFAT file flagged contiguous, get_fat() answers without reading the FAT. */
static int fragments(const char *path)
{
    FIL f;
    int count = 0;
    CHECK(f_open(&f, path, FA_READ) == FR_OK);
    DWORD cur = f.obj.sclust;
    CHECK(cur >= 2);
    do {
        DWORD next = get_fat(&f.obj, cur);
        CHECK(next != 1 && next != 0xFFFFFFFF);
        if (next != cur + 1) count++;
        cur = next;
    } while (cur < f.obj.fs->n_fatent);
    f_close(&f);
    return count;
}

static void write_file(const char *path, unsigned long long size, unsigned seed, int reserve, int *reserved)
{
    static BYTE buf[CHUNK];
    FIL f;
    UINT done;
    CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    if (reserve) {
        int r = fs_reserve_contiguous(&f, size);
        CHECK(r == BDMFS_RESERVE_OK || r == 0);
        *reserved = r == BDMFS_RESERVE_OK;
        /* Reserved: the whole size is allocated and writing starts at 0. Refused: still empty. */
        CHECK(f_tell(&f) == 0 && f_size(&f) == (*reserved ? size : 0));
    }
    for (unsigned long long pos = 0; pos < size; pos += done) {
        UINT want = size - pos > CHUNK ? CHUNK : (UINT)(size - pos);
        fill(buf, pos, want, seed);
        CHECK(f_write(&f, buf, want, &done) == FR_OK && done == want);
    }
    CHECK(f_size(&f) == size);
    CHECK(f_close(&f) == FR_OK);
}

static void verify_file(const char *path, unsigned long long size, unsigned seed)
{
    static BYTE buf[CHUNK], want[CHUNK];
    FIL f;
    UINT done;
    CHECK(f_open(&f, path, FA_READ) == FR_OK);
    CHECK(f_size(&f) == size);
    for (unsigned long long pos = 0; pos < size; pos += done) {
        UINT len = size - pos > CHUNK ? CHUNK : (UINT)(size - pos);
        CHECK(f_read(&f, buf, len, &done) == FR_OK && done == len);
        fill(want, pos, len, seed);
        CHECK(!memcmp(buf, want, len));
    }
    f_close(&f);
}

static DWORD free_clusters(void)
{
    DWORD n;
    FATFS *fs;
    CHECK(f_getfree("0:", &n, &fs) == FR_OK);
    return n;
}

/* A used volume: 160 files of 256 KB (40 MB), a 32 MB file, then filler up to about 1 MB from full.
 * Every second small file and the 32 MB file are deleted. Free space is then 80 holes of 256 KB at
 * the front, one hole of 32 MB after them, and about 1 MB at the end, where the allocation hint
 * points, as it does on any volume that was filled once. */
static void make_used_volume(FATFS *fs, BYTE format, unsigned megabytes, unsigned cluster)
{
    static BYTE work[64 * KB];
    MKFS_PARM opt = {format | FM_SFD, 1, 0, 0, cluster}; /* no partition table: BDM hands the driver the partition */
    char name[32];
    int reserved;
    disk_sectors = (LBA_t)megabytes * MB / SECTOR;
    free(disk);
    disk = calloc((size_t)disk_sectors, SECTOR);
    CHECK(disk);
    CHECK(f_mkfs("0:", &opt, work, sizeof(work)) == FR_OK);
    CHECK(f_mount(fs, "0:", 1) == FR_OK);
    CHECK(fs->fs_type == (format == FM_EXFAT ? FS_EXFAT : FS_FAT32));
    for (int i = 0; i < 160; i++) {
        sprintf(name, "0:/SAVE%03d.BIN", i);
        write_file(name, 256 * KB, i, 0, &reserved);
    }
    write_file("0:/OLDGAME.ISO", 32 * MB, 500, 0, &reserved);
    unsigned long long left = (unsigned long long)free_clusters() * cluster;
    CHECK(left > 2 * MB);
    write_file("0:/FILLER.BIN", left - 1 * MB, 501, 0, &reserved);
    for (int i = 0; i < 160; i += 2) {
        sprintf(name, "0:/SAVE%03d.BIN", i);
        CHECK(f_unlink(name) == FR_OK);
    }
    CHECK(f_unlink("0:/OLDGAME.ISO") == FR_OK);
    /* Remount, as after the console is switched off and on: the hint comes back from the volume. */
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    CHECK(f_mount(fs, "0:", 1) == FR_OK);
}

static void scenario(const char *label, BYTE format, unsigned megabytes, unsigned cluster)
{
    static FATFS fs;
    int reserved = 0, plain, fixed, nofit;
    DWORD before, after;

    /* 1. As shipped: no reservation. */
    make_used_volume(&fs, format, megabytes, cluster);
    write_file("0:/GAME.ISO", 24 * MB, 900, 0, &reserved);
    verify_file("0:/GAME.ISO", 24 * MB, 900);
    plain = fragments("0:/GAME.ISO");

    /* 2. The same volume and copy, with the reservation. */
    make_used_volume(&fs, format, megabytes, cluster);
    before = free_clusters();
    write_file("0:/GAME.ISO", 24 * MB, 900, 1, &reserved);
    CHECK(reserved);
    verify_file("0:/GAME.ISO", 24 * MB, 900);
    fixed = fragments("0:/GAME.ISO");
    for (int i = 1; i < 160; i += 2) { /* the files that were there are untouched */
        char name[32];
        sprintf(name, "0:/SAVE%03d.BIN", i);
        verify_file(name, 256 * KB, i);
    }
    /* A second file still fits behind it in the same hole, and deleting both gives it all back. */
    write_file("0:/GAME2.ISO", 7 * MB, 901, 1, &reserved);
    CHECK(reserved && fragments("0:/GAME2.ISO") == 1);
    verify_file("0:/GAME.ISO", 24 * MB, 900);
    CHECK(f_unlink("0:/GAME.ISO") == FR_OK && f_unlink("0:/GAME2.ISO") == FR_OK);
    after = free_clusters();
    CHECK(after == before);
    /* The count survives a remount: it was not only in memory. */
    CHECK(f_mount(NULL, "0:", 0) == FR_OK && f_mount(&fs, "0:", 1) == FR_OK);
    CHECK(free_clusters() == before);

    /* 3. A cancelled copy: reserved, partly written, closed and removed, as copy() does. */
    {
        FIL f;
        UINT done;
        static BYTE buf[CHUNK];
        CHECK(f_open(&f, "0:/CANCEL.ISO", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        CHECK(fs_reserve_contiguous(&f, 24 * MB) == BDMFS_RESERVE_OK);
        CHECK(f_write(&f, buf, CHUNK, &done) == FR_OK && done == CHUNK);
        CHECK(f_close(&f) == FR_OK && f_unlink("0:/CANCEL.ISO") == FR_OK);
        CHECK(free_clusters() == before);
    }

    /* 4. No free run is long enough (40 MB wanted, the longest is 32 MB): the request is refused,
     * the file is still empty, and the ordinary copy completes with the right contents. */
    write_file("0:/BIG.ISO", 40 * MB, 902, 1, &reserved);
    CHECK(!reserved);
    verify_file("0:/BIG.ISO", 40 * MB, 902);
    nofit = fragments("0:/BIG.ISO");
    CHECK(nofit > 1);

    /* 5. Requests that must be refused without touching the file. */
    {
        FIL f;
        UINT done;
        CHECK(f_unlink("0:/BIG.ISO") == FR_OK);
        CHECK(f_open(&f, "0:/EDGE.BIN", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
        CHECK(fs_reserve_contiguous(&f, 0) == 0);
        if (format != FM_EXFAT)
            CHECK(fs_reserve_contiguous(&f, 0x100000000ULL) == 0); /* FAT stops at 4 GB */
        CHECK(fs_reserve_contiguous(&f, (unsigned long long)megabytes * 2 * MB) == 0); /* bigger than the volume */
        CHECK(f_size(&f) == 0);
        CHECK(f_write(&f, "x", 1, &done) == FR_OK && done == 1);
        CHECK(fs_reserve_contiguous(&f, 2 * MB) == 0); /* not empty any more */
        CHECK(f_size(&f) == 1);
        CHECK(f_close(&f) == FR_OK);
        CHECK(f_open(&f, "0:/EDGE.BIN", FA_READ) == FR_OK);
        CHECK(fs_reserve_contiguous(&f, 2 * MB) == 0); /* not open for writing */
        f_close(&f);
    }
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    printf("%s plain=%d reserved=%d nofit=%d\n", label, plain, fixed, nofit);
}

/* Free space at both ends of the volume and none long enough in between: 8 MB at the front, 6 MB
 * at the end, with the allocation hint just before the end. A 12 MB request must be refused. The
 * two ends are not one run, although a scan that wraps from the last cluster to the first sees
 * them one after the other. */
static void wrap_scenario(const char *label, BYTE format, unsigned megabytes, unsigned cluster)
{
    static FATFS fs;
    static BYTE work[64 * KB];
    MKFS_PARM opt = {format | FM_SFD, 1, 0, 0, cluster};
    int reserved;
    FIL f;
    DWORD before, head_start;
    unsigned long long mid;

    disk_sectors = (LBA_t)megabytes * MB / SECTOR;
    free(disk);
    disk = calloc((size_t)disk_sectors, SECTOR);
    CHECK(disk);
    CHECK(f_mkfs("0:", &opt, work, sizeof(work)) == FR_OK);
    CHECK(f_mount(&fs, "0:", 1) == FR_OK);
    write_file("0:/HEAD.BIN", 8 * MB, 700, 0, &reserved);
    mid = (unsigned long long)free_clusters() * cluster - 6 * MB;
    write_file("0:/MID.BIN", mid, 701, 0, &reserved);
    CHECK(f_open(&f, "0:/HEAD.BIN", FA_READ) == FR_OK);
    head_start = f.obj.sclust;
    f_close(&f);
    CHECK(f_unlink("0:/HEAD.BIN") == FR_OK);
    before = free_clusters();
    CHECK((unsigned long long)before * cluster == 14 * MB);

    CHECK(f_open(&f, "0:/GAME.ISO", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    CHECK(fs_reserve_contiguous(&f, 12 * MB) == 0);
    CHECK(f_size(&f) == 0 && f.obj.sclust == 0);
    /* The same with every cluster in front of the 8 MB hole free too, so the free space starts at
     * the volume's first cluster. Formatters put the root folder or the exFAT bitmap there, so
     * those are marked free by hand here and put back. The end of the volume and its start are
     * still not one run. */
    {
        DWORD saved[64], lead = head_start - 2;
        CHECK(lead >= 1 && lead <= 64);
        for (DWORD c = 0; c < lead; c++) {
            if (format == FM_EXFAT) CHECK(change_bitmap(&fs, 2 + c, 1, 0) == FR_OK);
            else CHECK((saved[c] = get_fat(&f.obj, 2 + c)) >= 2 && put_fat(&fs, 2 + c, 0) == FR_OK);
        }
        /* The longest real run is now those clusters plus 8 MB. */
        CHECK(fs_reserve_contiguous(&f, 8 * MB + (unsigned long long)(lead + 1) * cluster) == 0);
        CHECK(f_size(&f) == 0 && f.obj.sclust == 0);
        for (DWORD c = 0; c < lead; c++) {
            if (format == FM_EXFAT) CHECK(change_bitmap(&fs, 2 + c, 1, 1) == FR_OK);
            else CHECK(put_fat(&fs, 2 + c, saved[c]) == FR_OK);
        }
    }
    /* More clusters than the volume has, and a size whose cluster count does not fit 32 bits. */
    CHECK(fs_reserve_contiguous(&f, (unsigned long long)megabytes * MB) == 0);
    CHECK(fs_reserve_contiguous(&f, 0x100000000ULL * cluster + 1 * MB) == 0);
    CHECK(f_size(&f) == 0 && f.obj.sclust == 0);
    CHECK(f_close(&f) == FR_OK && f_unlink("0:/GAME.ISO") == FR_OK);
    CHECK(free_clusters() == before);

    /* Nothing was taken from either end: each still holds a file of exactly its size, in one run,
     * and the run at the end reaches the last cluster of the volume. */
    write_file("0:/TAIL.BIN", 6 * MB, 702, 1, &reserved);
    CHECK(reserved && fragments("0:/TAIL.BIN") == 1);
    CHECK(f_open(&f, "0:/TAIL.BIN", FA_READ) == FR_OK);
    CHECK(f.obj.sclust + 6 * MB / cluster == fs.n_fatent);
    f_close(&f);
    write_file("0:/HEAD2.BIN", 8 * MB, 703, 1, &reserved);
    CHECK(reserved && fragments("0:/HEAD2.BIN") == 1);
    CHECK(free_clusters() == 0);
    verify_file("0:/MID.BIN", mid, 701);
    verify_file("0:/TAIL.BIN", 6 * MB, 702);
    verify_file("0:/HEAD2.BIN", 8 * MB, 703);
    CHECK(f_mount(NULL, "0:", 0) == FR_OK);
    printf("%s ends-not-joined ok\n", label);
}

int main(void)
{
    scenario("FAT32", FM_FAT32, 112, 1024);
    scenario("exFAT", FM_EXFAT, 112, 4096);
    wrap_scenario("FAT32", FM_FAT32, 112, 1024);
    wrap_scenario("exFAT", FM_EXFAT, 112, 4096);
    free(disk);
    return 0;
}
