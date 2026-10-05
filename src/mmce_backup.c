//--------------------------------------------------------------
// Full MMCE card-tree backup.
//--------------------------------------------------------------
#include "launchelf.h"
#include "main_title.h"
#include "mc_card_export.h"
#include "mmce_backup.h"

extern int fileMode;

#define MMCE_BACKUP_DEPTH 12
#define MMCE_BACKUP_IO 65536

static u8 compare_a[MMCE_BACKUP_IO] __attribute__((aligned(64)));
static u8 compare_b[MMCE_BACKUP_IO] __attribute__((aligned(64)));

static int bcd_to_int(u8 value)
{
    return ((value >> 4) * 10) + (value & 0x0f);
}

static void backup_stamp(char *out, size_t out_size)
{
    sceCdCLOCK clock;
    int year, month, day, hour, minute, second;

    memset(&clock, 0, sizeof(clock));
    if (sceCdReadClock(&clock) == 0 || clock.stat != 0) {
        snprintf(out, out_size, "00000000-000000-S%010llu",
                 (unsigned long long)(Timer() / 1000));
        return;
    }

    year = 2000 + bcd_to_int(clock.year);
    month = bcd_to_int(clock.month & 0x7f);
    day = bcd_to_int(clock.day);
    hour = bcd_to_int(clock.hour);
    minute = bcd_to_int(clock.minute);
    second = bcd_to_int(clock.second);
    menuTitleConvertTimestampToLocalTime(&year, &month, &day, &hour, &minute, &second);
    snprintf(out, out_size, "%04d%02d%02d-%02d%02d%02d",
             year, month, day, hour, minute, second);
}

static int join_path(char *out, size_t out_size, const char *a, const char *b)
{
    size_t n;

    if (out == NULL || out_size == 0 || a == NULL || b == NULL)
        return 0;
    n = strlen(a);
    if (n > 0 && a[n - 1] != '/' && a[n - 1] != ':')
        return snprintf(out, out_size, "%s/%s", a, b) < (int)out_size;
    return snprintf(out, out_size, "%s%s", a, b) < (int)out_size;
}

static int dir_exists(const char *path)
{
    char probe[MAX_PATH];
    int fd;

    snprintf(probe, sizeof(probe), "%s", path);
    fd = genDopen(probe);
    if (fd < 0)
        return 0;
    genDclose(fd);
    return 1;
}

static int ensure_dir_tree(const char *path)
{
    char tmp[MAX_PATH];
    char *p, *colon;

    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(tmp))
        return -1;
    snprintf(tmp, sizeof(tmp), "%s", path);

    colon = strchr(tmp, ':');
    if (colon == NULL)
        return -1;

    for (p = colon + 1; *p; p++) {
        if (*p != '/' || p == colon + 1)
            continue;
        *p = '\0';
        if (!dir_exists(tmp) && genMkdir(tmp, fileMode) < 0)
            return -1;
        *p = '/';
    }

    if (!dir_exists(tmp) && genMkdir(tmp, fileMode) < 0)
        return -1;
    return 0;
}

static const char *backup_ext(int format)
{
    return format == MMCE_BACKUP_FORMAT_PCSX2 ? "ps2" : "mcd";
}

static int history_name_match(const char *name, int format)
{
    return name != NULL && !strncmp(name, "WLE-", 4) &&
           genCmpFileExt(name, backup_ext(format));
}

static int newest_history(const char *dir, int format, const char *exclude,
                          char *name, size_t name_size)
{
    iox_dirent_t ent;
    char path[MAX_PATH];
    int fd;

    if (name == NULL || name_size == 0)
        return -1;
    name[0] = '\0';
    snprintf(path, sizeof(path), "%s", dir);
    fd = genDopen(path);
    if (fd < 0)
        return -1;

    memset(&ent, 0, sizeof(ent));
    while (fileXioDread(fd, &ent) > 0) {
        if (!history_name_match(ent.name, format) ||
            (exclude != NULL && !strcmp(ent.name, exclude))) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (name[0] == '\0' || strcmp(ent.name, name) > 0)
            snprintf(name, name_size, "%s", ent.name);
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
    return name[0] != '\0' ? 1 : 0;
}

static int read_chunk(const char *path, s64 offset, u8 *buf, int size)
{
    int fd, done = 0;

    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0)
        return -1;
    if (genLseek(fd, offset, SEEK_SET) != offset) {
        genClose(fd);
        return -1;
    }
    while (done < size) {
        int r = genRead(fd, buf + done, size - done);
        if (r <= 0) {
            genClose(fd);
            return -1;
        }
        done += r;
    }
    return genClose(fd) < 0 ? -1 : 0;
}

static s64 file_size(const char *path)
{
    int fd;
    s64 size;

    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0)
        return -1;
    size = genLseek(fd, 0, SEEK_END);
    if (genClose(fd) < 0)
        return -1;
    return size;
}

static int files_equal_serial(const char *a, const char *b)
{
    s64 sa = file_size(a), sb = file_size(b), pos = 0;

    if (sa < 0 || sb < 0)
        return -1;
    if (sa != sb)
        return 0;

    while (pos < sa) {
        int chunk = sa - pos > MMCE_BACKUP_IO ? MMCE_BACKUP_IO : (int)(sa - pos);
        if (read_chunk(a, pos, compare_a, chunk) < 0 ||
            read_chunk(b, pos, compare_b, chunk) < 0)
            return -1;
        if (memcmp(compare_a, compare_b, chunk) != 0)
            return 0;
        pos += chunk;
    }
    return 1;
}

static int prune_history(const char *dir, int keep, int format)
{
    iox_dirent_t ent;
    char scan[MAX_PATH], oldest[MAX_NAME], remove_path[MAX_PATH];
    int fd, count, guard = 0;

    while (guard++ < 256) {
        count = 0;
        oldest[0] = '\0';
        snprintf(scan, sizeof(scan), "%s", dir);
        fd = genDopen(scan);
        if (fd < 0)
            return -1;

        memset(&ent, 0, sizeof(ent));
        while (fileXioDread(fd, &ent) > 0) {
            if (history_name_match(ent.name, format)) {
                count++;
                if (oldest[0] == '\0' || strcmp(ent.name, oldest) < 0)
                    snprintf(oldest, sizeof(oldest), "%s", ent.name);
            }
            memset(&ent, 0, sizeof(ent));
        }
        genDclose(fd);

        if (keep == 0 || count <= keep)
            return 0;
        if (oldest[0] == '\0' ||
            !join_path(remove_path, sizeof(remove_path), dir, oldest))
            return -1;
        if (genRemove(remove_path) < 0)
            return -1;
    }
    return -1;
}

static int unique_history_path(const char *dir, int format,
                               char *name, size_t name_size,
                               char *path, size_t path_size)
{
    char stamp[32];
    int suffix;

    backup_stamp(stamp, sizeof(stamp));
    for (suffix = 0; suffix < 100; suffix++) {
        const char *ext = backup_ext(format);
        if (suffix == 0)
            snprintf(name, name_size, "WLE-%s.%s", stamp, ext);
        else
            snprintf(name, name_size, "WLE-%s-%02d.%s", stamp, suffix, ext);
        if (!join_path(path, path_size, dir, name))
            return 0;
        if (!wleExists(path))
            return 1;
    }
    return 0;
}

static int card_history_dir(const char *backup_root, const char *relative,
                            char *out, size_t out_size)
{
    char rel[MAX_PATH];
    char *dot;

    if (strlen(relative) >= sizeof(rel))
        return 0;
    snprintf(rel, sizeof(rel), "%s", relative);
    dot = strrchr(rel, '.');
    if (dot == NULL || stricmp(dot, ".mcd"))
        return 0;
    *dot = '\0';
    return join_path(out, out_size, backup_root, rel);
}

static int backup_one(const char *source, const char *relative,
                      const char *backup_root, const MmceBackupOptions *options,
                      MmceBackupStats *stats)
{
    char dir[MAX_PATH], name[MAX_NAME], dest[MAX_PATH];
    char previous[MAX_NAME], previous_path[MAX_PATH];
    int r, newest, same = -1;
    int format = options->format;

    if (!card_history_dir(backup_root, relative, dir, sizeof(dir)) ||
        ensure_dir_tree(dir) < 0) {
        stats->failed++;
        return -1;
    }

    newest = newest_history(dir, format, NULL, previous, sizeof(previous));
    if (newest > 0 &&
        join_path(previous_path, sizeof(previous_path), dir, previous) &&
        format == MMCE_BACKUP_FORMAT_MCD)
        same = files_equal_serial(source, previous_path);

    /* Native .mcd history can detect unchanged cards before any write. */
    if (same == 1) {
        stats->unchanged++;
        return 0;
    }

    if (!unique_history_path(dir, format, name, sizeof(name), dest, sizeof(dest))) {
        stats->failed++;
        return -1;
    }

    if (format == MMCE_BACKUP_FORMAT_PCSX2)
        r = mcCardExportPcsx2(source, dest, NULL);
    else
        r = mcCardBackupNoEcc(source, dest, NULL);
    if (r != MC_CARD_EXPORT_OK) {
        stats->failed++;
        return -1;
    }

    /* PCSX2 history is a converted representation, so compare the verified
     * converted file with the newest previous .ps2 after writing. */
    if (format == MMCE_BACKUP_FORMAT_PCSX2 && newest > 0 &&
        previous_path[0] != '\0') {
        same = files_equal_serial(dest, previous_path);
        if (same == 1) {
            if (genRemove(dest) < 0) {
                stats->failed++;
                return -1;
            }
            stats->unchanged++;
            return 0;
        }
    }

    stats->backed_up++;

    if (prune_history(dir, options->keep, format) < 0) {
        stats->failed++;
        return -1;
    }
    return 0;
}

static int count_candidates(const char *dir, int depth)
{
    iox_dirent_t ent;
    char scan[MAX_PATH], child[MAX_PATH];
    int fd, count = 0;

    if (depth > MMCE_BACKUP_DEPTH)
        return 0;
    snprintf(scan, sizeof(scan), "%s", dir);
    fd = genDopen(scan);
    if (fd < 0)
        return 0;

    memset(&ent, 0, sizeof(ent));
    while (fileXioDread(fd, &ent) > 0) {
        if (!strcmp(ent.name, ".") || !strcmp(ent.name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (!join_path(child, sizeof(child), dir, ent.name)) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (FIO_S_ISDIR(ent.stat.mode))
            count += count_candidates(child, depth + 1);
        else if (genCmpFileExt(ent.name, "MCD"))
            count++;
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
    return count;
}

static void process_tree(const char *dir, const char *source_root,
                         const char *backup_root, const MmceBackupOptions *options,
                         int depth, MmceBackupStats *stats,
                         MmceBackupProgress progress, int *done)
{
    iox_dirent_t ent;
    char scan[MAX_PATH], child[MAX_PATH];
    const char *relative;
    int fd;

    if (depth > MMCE_BACKUP_DEPTH)
        return;
    snprintf(scan, sizeof(scan), "%s", dir);
    fd = genDopen(scan);
    if (fd < 0)
        return;

    memset(&ent, 0, sizeof(ent));
    while (fileXioDread(fd, &ent) > 0) {
        if (!strcmp(ent.name, ".") || !strcmp(ent.name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (!join_path(child, sizeof(child), dir, ent.name)) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }

        if (FIO_S_ISDIR(ent.stat.mode)) {
            process_tree(child, source_root, backup_root, options, depth + 1,
                         stats, progress, done);
        } else if (genCmpFileExt(ent.name, "MCD")) {
            (*done)++;
            if (progress)
                progress(child, *done, stats->candidates);
            relative = child + strlen(source_root);
            while (*relative == '/')
                relative++;
            backup_one(child, relative, backup_root, options, stats);
        }
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
}

int mmceBackupAllCards(int unit, const char *dest_root, const MmceBackupOptions *options,
                       MmceBackupStats *stats, MmceBackupProgress progress)
{
    static const MmceBackupOptions defaults = {10, MMCE_BACKUP_FORMAT_MCD};
    char source_root[MAX_PATH], history_root[MAX_PATH], tmp[MAX_PATH], unit_name[16];
    int done = 0;

    if (options == NULL)
        options = &defaults;
    if (stats == NULL || dest_root == NULL || unit < 0 || unit > 1 ||
        options->keep < 0 || options->keep > 99 ||
        options->format < 0 || options->format >= MMCE_BACKUP_FORMAT_COUNT)
        return -1;
    memset(stats, 0, sizeof(*stats));

    snprintf(source_root, sizeof(source_root), "mmce%d:/MemoryCards", unit);
    if (!dir_exists(source_root))
        return -1;

    {
        size_t source_len = strlen(source_root);
        if (!strncmp(dest_root, source_root, source_len) &&
            (dest_root[source_len] == '\0' || dest_root[source_len] == '/'))
            return -1;
    }

    if (!join_path(history_root, sizeof(history_root), dest_root, "WLE-MMCE-Backups"))
        return -1;
    snprintf(unit_name, sizeof(unit_name), "mmce%d", unit);
    snprintf(tmp, sizeof(tmp), "%s", history_root);
    if (!join_path(history_root, sizeof(history_root), tmp, unit_name))
        return -1;
    if (ensure_dir_tree(history_root) < 0)
        return -1;

    stats->candidates = count_candidates(source_root, 0);
    process_tree(source_root, source_root, history_root, options, 0,
                 stats, progress, &done);
    return 0;
}
