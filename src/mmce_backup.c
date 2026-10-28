//--------------------------------------------------------------
// Full MMCE card-tree backup.
//--------------------------------------------------------------
#include "launchelf.h"
#include "main_title.h"
#include "mc_card_export.h"
#include "mc_card_view.h"
#include "mmce_backup.h"

extern int fileMode;

#define MMCE_BACKUP_DEPTH 12
#define MMCE_BACKUP_IO 65536
#define MMCE_COMPARE_PAGES 64

static u8 compare_a[MMCE_BACKUP_IO] __attribute__((aligned(64)));
static u8 compare_b[MMCE_BACKUP_IO] __attribute__((aligned(64)));

/* Fingerprint sidecar beside a history copy: "LSF1" + SHA-256 of the copy's
 * filesystem index (mcViewFingerprint). */
#define MMCE_FP_TAG "LSF1"
#define MMCE_FP_TAG_LEN 4
#define MMCE_FP_FILE (MMCE_FP_TAG_LEN + MCV_FINGERPRINT_SIZE)
#define MMCE_FP_BLOCK 4096
#define MMCE_FP_WAYS 4

typedef struct {
    int fd;
    s64 size;
    s64 tag[MMCE_FP_WAYS];
    int valid[MMCE_FP_WAYS];
    int next;
} FpReader;

static u8 fp_cache[MMCE_FP_WAYS][MMCE_FP_BLOCK] __attribute__((aligned(64)));
static McView fp_view;

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

/* Index reads are 512-byte pages scattered over the first part of the card;
 * a few 4 KiB blocks turn them into far fewer device reads over MMCE. */
static int fp_read(void *ctx, uint64_t offset, void *buf, uint32_t len)
{
    FpReader *r = (FpReader *)ctx;
    u8 *out = (u8 *)buf;

    while (len > 0) {
        s64 base = (s64)(offset / MMCE_FP_BLOCK) * MMCE_FP_BLOCK;
        u32 in = (u32)(offset - (uint64_t)base), n = MMCE_FP_BLOCK - in;
        int way = -1, i;

        if (n > len)
            n = len;
        for (i = 0; i < MMCE_FP_WAYS; i++)
            if (r->tag[i] == base)
                way = i;
        if (way < 0) {
            int want = MMCE_FP_BLOCK, done = 0;

            if (base >= r->size)
                return -1;
            if (r->size - base < want)
                want = (int)(r->size - base);
            way = r->next;
            r->next = (r->next + 1) % MMCE_FP_WAYS;
            r->tag[way] = -1;
            if (genLseek(r->fd, base, SEEK_SET) != base)
                return -1;
            while (done < want) {
                int got = genRead(r->fd, fp_cache[way] + done, want - done);
                if (got <= 0)
                    return -1;
                done += got;
            }
            r->tag[way] = base;
            r->valid[way] = want;
        }
        if ((int)(in + n) > r->valid[way])
            return -1;
        memcpy(out, fp_cache[way] + in, n);
        out += n;
        offset += n;
        len -= n;
    }
    return 0;
}

/* Fingerprint of a card image (MMCE card or history copy, .mcd or .ps2).
 * Opens only this one file, so it is safe under MMCE's one-open-file rule. */
static int card_fingerprint(const char *path, u8 out[MCV_FINGERPRINT_SIZE])
{
    FpReader rd;
    McViewSource src;
    int i, r;

    memset(&rd, 0, sizeof(rd));
    for (i = 0; i < MMCE_FP_WAYS; i++)
        rd.tag[i] = -1;
    rd.fd = genOpen(path, FIO_O_RDONLY);
    if (rd.fd < 0)
        return -1;
    rd.size = genLseek(rd.fd, 0, SEEK_END);
    if (rd.size <= 0) {
        genClose(rd.fd);
        return -1;
    }
    src.read = fp_read;
    src.ctx = &rd;
    src.size = (uint64_t)rd.size;
    r = mcViewOpen(&fp_view, &src);
    if (r == MCV_OK)
        r = mcViewFingerprint(&fp_view, out);
    if (genClose(rd.fd) < 0)
        return -1;
    return r == MCV_OK ? 0 : -1;
}

int mmceCardFingerprint(const char *path, unsigned char out[32])
{
    return card_fingerprint(path, out);
}

static int sidecar_path(const char *history, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "%s.FP", history);
    return n > 0 && n < (int)out_size;
}

/* 0 and the stored fingerprint, or -1 if missing, short, long or foreign. */
static int read_sidecar(const char *history, u8 out[MCV_FINGERPRINT_SIZE])
{
    char path[MAX_PATH];
    u8 raw[MMCE_FP_FILE + 1];
    int fd, done = 0, got;

    if (!sidecar_path(history, path, sizeof(path)))
        return -1;
    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0)
        return -1;
    while (done < (int)sizeof(raw)) {
        got = genRead(fd, raw + done, sizeof(raw) - done);
        if (got <= 0)
            break;
        done += got;
    }
    if (genClose(fd) < 0 || done != MMCE_FP_FILE ||
        memcmp(raw, MMCE_FP_TAG, MMCE_FP_TAG_LEN) != 0)
        return -1;
    memcpy(out, raw + MMCE_FP_TAG_LEN, MCV_FINGERPRINT_SIZE);
    return 0;
}

/* Records the fingerprint of a committed history copy, computed from the copy
 * itself so it describes exactly those bytes. Best effort: without a valid
 * sidecar the next run falls back to the full compare. */
static void write_sidecar(const char *history)
{
    char path[MAX_PATH];
    u8 fp[MCV_FINGERPRINT_SIZE], check[MCV_FINGERPRINT_SIZE], raw[MMCE_FP_FILE];
    int fd, ok;

    if (!sidecar_path(history, path, sizeof(path)) || card_fingerprint(history, fp) < 0)
        return;
    memcpy(raw, MMCE_FP_TAG, MMCE_FP_TAG_LEN);
    memcpy(raw + MMCE_FP_TAG_LEN, fp, MCV_FINGERPRINT_SIZE);
    fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    if (fd < 0)
        return;
    ok = genWrite(fd, raw, sizeof(raw)) == (int)sizeof(raw);
    if (genClose(fd) < 0)
        ok = 0;
    if (!ok || read_sidecar(history, check) < 0 ||
        memcmp(check, fp, MCV_FINGERPRINT_SIZE) != 0)
        genRemove(path);
}

/* 1 = same index as the history copy, 0 = different, -1 = cannot tell
 * (no sidecar, or either side could not be parsed): use the full compare. */
static int fingerprint_matches(const char *source, const char *history)
{
    u8 now[MCV_FINGERPRINT_SIZE], then[MCV_FINGERPRINT_SIZE];

    if (read_sidecar(history, then) < 0 || card_fingerprint(source, now) < 0)
        return -1;
    return memcmp(now, then, MCV_FINGERPRINT_SIZE) == 0;
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

/* Compare a live/native 512-byte-page MMCE image with an existing PCSX2
 * 528-byte-page history file without creating a temporary converted image. */
static int files_equal_pcsx2_history(const char *source, const char *history)
{
    s64 raw_size, encoded_size, src_pos = 0, dst_pos = 0;

    if (mcCardValidateNoEcc(source) != MC_CARD_EXPORT_OK ||
        mcCardValidatePcsx2(history) != MC_CARD_EXPORT_OK)
        return -1;

    raw_size = file_size(source);
    encoded_size = file_size(history);
    if (raw_size <= 0 || encoded_size <= 0 || (raw_size % 512) != 0)
        return -1;
    if (encoded_size != (raw_size / 512) * 528)
        return 0;

    while (src_pos < raw_size) {
        u8 page[528];
        int pages = (int)((raw_size - src_pos) / 512);
        int raw_bytes, encoded_bytes, p;

        if (pages > MMCE_COMPARE_PAGES)
            pages = MMCE_COMPARE_PAGES;
        raw_bytes = pages * 512;
        encoded_bytes = pages * 528;
        if (read_chunk(source, src_pos, compare_a, raw_bytes) < 0 ||
            read_chunk(history, dst_pos, compare_b, encoded_bytes) < 0)
            return -1;
        for (p = 0; p < pages; p++) {
            mcCardPcsx2Page(compare_a + p * 512, page);
            if (memcmp(page, compare_b + p * 528, 528) != 0)
                return 0;
        }
        src_pos += raw_bytes;
        dst_pos += encoded_bytes;
    }
    return 1;
}

static int prune_history(const char *dir, int keep, int format,
                         const char *protect_name, const char *fresh_name)
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
                /* A pre-restore copy must not prune either the selected restore
                 * snapshot or the verified safety copy just published. */
                if ((protect_name == NULL || strcmp(ent.name, protect_name)) &&
                    (fresh_name == NULL || strcmp(ent.name, fresh_name)) &&
                    (oldest[0] == '\0' || strcmp(ent.name, oldest) < 0))
                    snprintf(oldest, sizeof(oldest), "%s", ent.name);
            }
            memset(&ent, 0, sizeof(ent));
        }
        genDclose(fd);

        if (keep == 0 || count <= keep)
            return 0;
        /* Retention is a limit, not permission to delete a protected copy.
         * Two protected files can legitimately exceed keep == 1. */
        if (oldest[0] == '\0')
            return 0;
        if (!join_path(remove_path, sizeof(remove_path), dir, oldest))
            return -1;
        if (genRemove(remove_path) < 0)
            return -1;
        {
            char fp_path[MAX_PATH];
            if (sidecar_path(remove_path, fp_path, sizeof(fp_path)))
                genRemove(fp_path); /* may not exist */
        }
    }
    return -1;
}

static int unique_history_path(const char *dir, int format,
                               char *name, size_t name_size,
                               char *path, size_t path_size)
{
    char stamp[32];
    int suffix, written;

    if (name_size == 0 || path_size == 0)
        return 0;
    backup_stamp(stamp, sizeof(stamp));
    for (suffix = 0; suffix < 100; suffix++) {
        const char *ext = backup_ext(format);
        if (suffix == 0)
            written = snprintf(name, name_size, "WLE-%s.%s", stamp, ext);
        else
            written = snprintf(name, name_size, "WLE-%s-%02d.%s", stamp, suffix, ext);
        if (written < 0 || (size_t)written >= name_size)
            return 0;
        if (!join_path(path, path_size, dir, name))
            return 0;
        {
            char staged[MAX_PATH];
            int n = snprintf(staged, sizeof(staged), "%s.PART", path);
            if (n < 0 || n >= (int)sizeof(staged))
                return 0;
            /* Do not truncate a previous interrupted staged backup. */
            if (!wleExists(path) && !wleExists(staged))
                return 1;
        }
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
                      const char *protect_name, MmceBackupStats *stats)
{
    char dir[MAX_PATH], name[MAX_NAME], dest[MAX_PATH], staged[MAX_PATH];
    char previous[MAX_NAME], previous_path[MAX_PATH];
    int r, newest, same = -1, fp_same = -1;
    int format = options->format;

    if (!card_history_dir(backup_root, relative, dir, sizeof(dir)) ||
        ensure_dir_tree(dir) < 0) {
        stats->failed++;
        return -1;
    }

    previous_path[0] = '\0';
    newest = newest_history(dir, format, NULL, previous, sizeof(previous));
    if (newest > 0)
        join_path(previous_path, sizeof(previous_path), dir, previous);

    /* Index fingerprint first: a match means unchanged without reading the
     * whole card; a mismatch proves a change, so the full read is spent on the
     * copy instead of a compare. Without a usable sidecar, compare as before. */
    if (previous_path[0] != '\0' && !options->verify) {
        fp_same = fingerprint_matches(source, previous_path);
        if (fp_same == 1) {
            stats->unchanged++;
            return 0;
        }
    }
    if (previous_path[0] != '\0' && format == MMCE_BACKUP_FORMAT_MCD && fp_same != 0) {
        same = files_equal_serial(source, previous_path);
        if (same == 1)
            write_sidecar(previous_path); /* first run after an update: seed it */
    }

    /* Native .mcd history can detect unchanged cards before any write.
     * A failed comparison is not proof of a changed card: do not prune
     * previous history if an existing snapshot cannot be read. */
    if (same == 1) {
        stats->unchanged++;
        return 0;
    }
    if (newest < 0 || (newest > 0 && (previous_path[0] == '\0' ||
        (format == MMCE_BACKUP_FORMAT_MCD && same < 0 && fp_same != 0)))) {
        stats->failed++;
        return -1;
    }

    if (!unique_history_path(dir, format, name, sizeof(name), dest, sizeof(dest)) ||
        snprintf(staged, sizeof(staged), "%s.PART", dest) >= (int)sizeof(staged)) {
        stats->failed++;
        return -1;
    }

    /* Incomplete writes must never be visible as .mcd/.ps2 history to the
     * pending scan, retention or Google Drive uploader. The export routine
     * fully verifies its output against the source before we publish it. */
    if (format == MMCE_BACKUP_FORMAT_PCSX2)
        r = mcCardExportPcsx2(source, staged, NULL);
    else
        r = mcCardBackupNoEcc(source, staged, NULL);
    if (r != MC_CARD_EXPORT_OK) {
        genRemove(staged);
        stats->failed++;
        return -1;
    }

    /* Compare converted history before publishing. A comparison I/O error
     * must not be treated as a changed card and trigger retention pruning. */
    if (format == MMCE_BACKUP_FORMAT_PCSX2 && newest > 0) {
        same = files_equal_serial(staged, previous_path);
        if (same == 1) {
            if (genRemove(staged) < 0) {
                stats->failed++;
                return -1;
            }
            write_sidecar(previous_path);
            stats->unchanged++;
            return 0;
        }
        if (same < 0) {
            genRemove(staged);
            stats->failed++;
            return -1;
        }
    }

    /* Both paths are in the same directory/filesystem. Never overwrite a
     * history entry if a concurrent writer claimed the selected name. */
    if (wleExists(dest) || fileXioRename(staged, dest) < 0) {
        genRemove(staged);
        stats->failed++;
        return -1;
    }
    stats->backed_up++;
    write_sidecar(dest);

    /* Retention only operates on committed, verified history entries. */
    if (prune_history(dir, options->keep, format, protect_name, name) < 0) {
        stats->failed++;
        return -1;
    }
    return 0;
}


static int scan_one(const char *source, const char *relative,
                    const char *backup_root, const MmceBackupOptions *options,
                    MmceBackupScanStats *stats)
{
    char dir[MAX_PATH], previous[MAX_NAME], previous_path[MAX_PATH];
    int newest, same;

    if (!card_history_dir(backup_root, relative, dir, sizeof(dir))) {
        stats->failed++;
        return -1;
    }

    /* No history folder means this card has never been backed up. */
    if (!dir_exists(dir)) {
        stats->pending++;
        return 0;
    }

    newest = newest_history(dir, options->format, NULL, previous, sizeof(previous));
    if (newest == 0) {
        stats->pending++;
        return 0;
    }
    if (newest < 0 ||
        !join_path(previous_path, sizeof(previous_path), dir, previous)) {
        stats->failed++;
        return -1;
    }

    same = options->verify ? -1 : fingerprint_matches(source, previous_path);
    if (same < 0)
        same = options->format == MMCE_BACKUP_FORMAT_PCSX2
                   ? files_equal_pcsx2_history(source, previous_path)
                   : files_equal_serial(source, previous_path);
    if (same > 0)
        stats->unchanged++;
    else if (same == 0)
        stats->pending++;
    else
        stats->failed++;
    return same < 0 ? -1 : 0;
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
            backup_one(child, relative, backup_root, options, NULL, stats);
        }
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
}

int mmceBackupAllCards(int unit, const char *dest_root, const MmceBackupOptions *options,
                       MmceBackupStats *stats, MmceBackupProgress progress)
{
    static const MmceBackupOptions defaults = {10, MMCE_BACKUP_FORMAT_MCD, 0};
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


/* Pre-restore safeguard: back up precisely one MMCE image without scanning
 * other cards. This is a backup API, not an MMCE restore/write operation.
 * protect_history must name an existing snapshot for this same card, in its
 * own history directory. A protected source and the new verified snapshot
 * are excluded from retention pruning even when keep == 1. */
int mmceBackupCardPath(int unit, const char *source, const char *dest_root,
                       const MmceBackupOptions *options,
                       const char *protect_history, MmceBackupStats *stats)
{
    static const MmceBackupOptions defaults = {10, MMCE_BACKUP_FORMAT_MCD, 0};
    char source_root[MAX_PATH], history_root[MAX_PATH], card_dir[MAX_PATH];
    char tmp[MAX_PATH], unit_name[16];
    const char *relative, *p, *protect_name = NULL;
    size_t source_len, dir_len;

    if (options == NULL)
        options = &defaults;
    if (stats == NULL || source == NULL || dest_root == NULL ||
        unit < 0 || unit > 1 || options->keep < 0 || options->keep > 99 ||
        options->format < 0 || options->format >= MMCE_BACKUP_FORMAT_COUNT ||
        !strchr(dest_root, ':') ||
        (strlen(dest_root) >= 4 &&
         (dest_root[0] == 'm' || dest_root[0] == 'M') &&
         (dest_root[1] == 'm' || dest_root[1] == 'M') &&
         (dest_root[2] == 'c' || dest_root[2] == 'C') &&
         (dest_root[3] == 'e' || dest_root[3] == 'E')))
        return -1;
    memset(stats, 0, sizeof(*stats));
    stats->candidates = 1;

    snprintf(source_root, sizeof(source_root), "mmce%d:/MemoryCards", unit);
    source_len = strlen(source_root);
    if (strncmp(source, source_root, source_len) ||
        source[source_len] != '/' || source[source_len + 1] == '\0' ||
        !genCmpFileExt(source, "MCD"))
        goto reject;
    relative = source + source_len + 1;

    /* Reject traversal or empty components before constructing a history path. */
    for (p = relative; *p;) {
        const char *end = strchr(p, '/');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (len == 0 || (len == 1 && p[0] == '.') ||
            (len == 2 && p[0] == '.' && p[1] == '.') ||
            memchr(p, '\\', len) != NULL)
            goto reject;
        if (end == NULL)
            break;
        p = end + 1;
        if (*p == '\0')
            goto reject;
    }

    if (mcCardValidateNoEcc(source) != MC_CARD_EXPORT_OK)
        goto reject;
    if (!join_path(history_root, sizeof(history_root), dest_root, "WLE-MMCE-Backups"))
        goto reject;
    snprintf(unit_name, sizeof(unit_name), "mmce%d", unit);
    snprintf(tmp, sizeof(tmp), "%s", history_root);
    if (!join_path(history_root, sizeof(history_root), tmp, unit_name) ||
        !card_history_dir(history_root, relative, card_dir, sizeof(card_dir)))
        goto reject;

    if (protect_history != NULL && protect_history[0] != '\0') {
        dir_len = strlen(card_dir);
        if (strncmp(protect_history, card_dir, dir_len) ||
            protect_history[dir_len] != '/' ||
            !history_name_match(protect_history + dir_len + 1, options->format) ||
            strchr(protect_history + dir_len + 1, '/') != NULL ||
            !wleExists(protect_history))
            goto reject;
        protect_name = protect_history + dir_len + 1;
    }

    {
        /* A pre-restore copy must be byte exact: never skip by fingerprint. */
        MmceBackupOptions exact = *options;
        exact.verify = 1;
        return backup_one(source, relative, history_root, &exact,
                          protect_name, stats);
    }
reject:
    stats->failed++;
    return -1;
}

static void scan_tree(const char *dir, const char *source_root,
                      const char *backup_root, const MmceBackupOptions *options,
                      int depth, MmceBackupScanStats *stats,
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
            scan_tree(child, source_root, backup_root, options, depth + 1,
                      stats, progress, done);
        } else if (genCmpFileExt(ent.name, "MCD")) {
            (*done)++;
            if (progress)
                progress(child, *done, stats->candidates);
            relative = child + strlen(source_root);
            while (*relative == '/')
                relative++;
            scan_one(child, relative, backup_root, options, stats);
        }
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
}

int mmceBackupScanCards(int unit, const char *dest_root, const MmceBackupOptions *options,
                        MmceBackupScanStats *stats, MmceBackupProgress progress)
{
    static const MmceBackupOptions defaults = {10, MMCE_BACKUP_FORMAT_MCD, 0};
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

    stats->candidates = count_candidates(source_root, 0);
    scan_tree(source_root, source_root, history_root, options, 0,
              stats, progress, &done);
    return 0;
}
