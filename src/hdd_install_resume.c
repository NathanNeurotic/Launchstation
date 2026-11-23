#include "hdd_install_resume.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct { unsigned char *data; unsigned offset, size; int error; } Wire;
static void put(Wire *w, const void *data, unsigned n)
{
    if (w->error || n > w->size - w->offset) { w->error = 1; return; }
    memcpy(w->data + w->offset, data, n); w->offset += n;
}
static void get(Wire *w, void *data, unsigned n)
{
    if (w->error || n > w->size - w->offset) { w->error = 1; return; }
    memcpy(data, w->data + w->offset, n); w->offset += n;
}
static void number(Wire *w, uint32_t *n, int reading)
{
    unsigned char b[4];
    if (!reading) { for (unsigned i = 0; i < 4; i++) b[i] = *n >> (8 * i); put(w, b, 4); }
    else { get(w, b, 4); if (!w->error) *n = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
}
static unsigned checksum(const unsigned char *p, unsigned size)
{
    const unsigned char zero[4] = {0};
    uLong crc = crc32(0, p, 20);
    crc = crc32(crc, zero, 4);
    return crc32(crc, p + 24, size - 24);
}
static int text(const char *s, unsigned capacity, const char *allowed, int empty)
{
    const char *end = memchr(s, 0, capacity);
    if (!end || (!empty && end == s)) return 0;
    for (const char *p = s; p != end; p++)
        if (allowed ? !strchr(allowed, *p) : ((unsigned char)*p < 32 || (unsigned char)*p == 127)) return 0;
    return 1;
}
#define SAFE "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-"
static int runtimePath(const char *path)
{
    if (!text(path, 192, SAFE "/", 0)) return 0;
    if (strncmp(path, "scripts/assets/OPL/", 19) && strncmp(path, "scripts/assets/NHDDL/", 21) &&
        strncmp(path, "scripts/assets/neutrino/", 24) && strcmp(path, "scripts/licenses/OPL") &&
        strcmp(path, "scripts/licenses/nhddl") && strcmp(path, "scripts/licenses/neutrino") &&
        strcmp(path, "icons/ico/APP_OPL_LST.ico") && strcmp(path, "icons/ico/APP_NHDDL_LST.ico") &&
        strcmp(path, "icons/art/APP_OPL.png") && strcmp(path, "icons/art/APP_NHDDL.png")) return 0;
    for (const char *p = path; *p;) {
        const char *end = strchr(p, '/'); unsigned n = end ? (unsigned)(end - p) : strlen(p);
        if (!n || (n == 1 && *p == '.') || (n == 2 && !memcmp(p, "..", 2)) || (end && !end[1])) return 0;
        p += n + !!end;
    }
    return 1;
}
static int valid(const HddInstall *in, const HddFormatPlan *p)
{
    HddFormatPlan expected;
    if (p->kind == HDD_FORMAT_EXFAT || hddFormatPlan(&expected, p->kind, p->sectors, p->reserve_mb, p->serial) < 0 ||
        memcmp(p, &expected, sizeof(expected)) || in->jail != (p->kind == HDD_FORMAT_APA_JAIL) ||
        !in->flags || (in->flags & ~15u) || ((in->flags & HDD_INSTALL_PS2LINUX) && !(in->flags & HDD_INSTALL_PSBBN)) ||
        ((in->flags & HDD_INSTALL_PSBBN) && (!in->jail || (in->flags & 7) != 7)) ||
        in->game_launcher > HDD_GAME_NEUTRINO || in->stage_ready != 1 || in->manifest_ready > 1 ||
        in->ready > 1 || in->applied > 1 || in->populated > 1 || in->committed > 1 ||
        (in->downloaded_assets >> HDD_INSTALL_ASSETS) || in->launcher_count > HDD_INSTALL_LAUNCHER_FILES ||
        in->downloaded_launchers > in->launcher_count || (in->launcher_count && !in->launcher_files) ||
        (in->populated && !in->applied) || (in->committed && !in->applied) ||
        ((in->applied || in->ready) && !in->manifest_ready)) return 0;
    if (!text(in->commit, sizeof(in->commit), "0123456789abcdefABCDEF", !in->manifest_ready) ||
        (in->commit[0] && strlen(in->commit) != 40) || !text(in->archive_item, sizeof(in->archive_item), SAFE, 1) ||
        !text(in->channels, sizeof(in->channels), SAFE, 1) || !text(in->language.name, sizeof(in->language.name), NULL, 1) ||
        !text(in->language.code, sizeof(in->language.code), "abcdefghijklmnopqrstuvwxyz", !(in->flags & HDD_INSTALL_PSBBN))) return 0;
    if ((in->flags & HDD_INSTALL_PSBBN) &&
        (strlen(in->language.code) != 3 || !in->language.name[0] || !in->archive_item[0] ||
         in->music_mb < 1024 || in->music_mb > 131072 || (in->music_mb & (in->music_mb - 1)) ||
         in->contents_mb < 1024 || in->contents_mb > 131072 || (in->contents_mb & (in->contents_mb - 1)) ||
         ((in->flags & HDD_INSTALL_PS2LINUX) ? in->linux_home_mb < 1024 || in->linux_home_mb > HDD_INSTALL_HOME_MAX_MB ||
            (in->linux_home_mb & (in->linux_home_mb - 1)) : in->linux_home_mb != 0) ||
         in->reserved_mb > 131072 || (in->reserved_mb & 127))) return 0;
    for (unsigned i = 0; i < 2; i++) if (!text(in->archives[i], sizeof(in->archives[i]), SAFE, 1)) return 0;
    for (unsigned i = 0; i < HDD_INSTALL_ASSETS; i++)
        if (in->sizes[i] > 1024u * 1024 * 1024 || !text(in->hashes[i], sizeof(in->hashes[i]), "0123456789abcdefABCDEF", 1)) return 0;
    uint64_t total = 0;
    for (unsigned i = 0; i < in->launcher_count; i++) {
        const HddInstallFile *f = &in->launcher_files[i];
        total += f->size;
        if (total > 64u * 1024 * 1024 || !runtimePath(f->path) || !text(f->hash, sizeof(f->hash), "0123456789abcdefABCDEF", 1)) return 0;
        for (unsigned j = 0; j < i; j++) if (!strcmp(f->path, in->launcher_files[j].path)) return 0;
    }
    return 1;
}
/* The explicit order is the on-disk v1 format; additions require a new version. */
static void body(Wire *w, HddInstall *in, HddFormatPlan *p, int reading)
{
#define N(value) do { uint32_t v = (value); number(w, &v, reading); if (reading) (value) = v; } while (0)
#define B(value) do { if (reading) get(w, value, sizeof(value)); else put(w, value, sizeof(value)); } while (0)
    N(p->kind); N(p->sectors); N(p->reserve_mb); N(p->serial); N(p->apa_sectors); N(p->recovery_start); N(p->exfat_start); N(p->exfat_sectors);
    N(in->flags); N(in->jail); N(in->music_mb); N(in->contents_mb); N(in->linux_home_mb); N(in->reserved_mb); N(in->game_launcher);
    N(in->stage_ready); N(in->manifest_ready); N(in->downloaded_assets); N(in->downloaded_launchers);
    N(in->ready); N(in->applied); N(in->populated); N(in->committed); N(in->launcher_count);
    B(in->owner); B(in->commit); B(in->archives); B(in->hashes); B(in->language.code); B(in->language.name); B(in->archive_item); B(in->channels);
    for (unsigned i = 0; i < HDD_INSTALL_ASSETS; i++) N(in->sizes[i]);
    for (unsigned i = 0; i < HDD_INSTALL_LINUX_PARTITIONS; i++) { N(in->linux[i].start); N(in->linux[i].sectors); }
    if (in->launcher_count > HDD_INSTALL_LAUNCHER_FILES) { w->error = 1; return; }
    if (reading && in->launcher_count) {
        in->launcher_files = calloc(in->launcher_count, sizeof(*in->launcher_files));
        if (!in->launcher_files) { w->error = 1; return; }
    }
    for (unsigned i = 0; i < in->launcher_count; i++) { B(in->launcher_files[i].path); B(in->launcher_files[i].hash); N(in->launcher_files[i].size); }
#undef N
#undef B
}
int hddInstallResumeEncode(const HddInstall *in, const HddFormatPlan *p, unsigned seq, void *bytes, unsigned capacity)
{
    Wire w = {bytes, 24, capacity, 0};
    if (!in || !p || !bytes || capacity < 24 || capacity > HDD_INSTALL_RESUME_MAX || !seq || seq >= 0x7fffffff || !valid(in, p)) return -EINVAL;
    body(&w, (HddInstall *)in, (HddFormatPlan *)p, 0);
    if (w.error) return -ENOSPC;
    Wire header = {bytes, 0, 24, 0};
    uint32_t version = 1, size = w.offset, crc = 0, sequence_value = seq;
    put(&header, "LSINST1", 8); number(&header, &version, 0); number(&header, &sequence_value, 0); number(&header, &size, 0); number(&header, &crc, 0);
    header.offset = 20; crc = checksum(bytes, size); number(&header, &crc, 0);
    return size;
}
int hddInstallResumeDecode(HddInstall *in, HddFormatPlan *plan, unsigned *sequence, const void *bytes, unsigned size)
{
    uint32_t version = 0, seq = 0, length = 0, crc = 0;
    if (!in || !plan || !sequence || !bytes || size < 24 || size > HDD_INSTALL_RESUME_MAX || memcmp(bytes, "LSINST1", 8)) return -EINVAL;
    Wire w = {(unsigned char *)bytes, 8, size, 0};
    number(&w, &version, 1); number(&w, &seq, 1); number(&w, &length, 1); number(&w, &crc, 1);
    if (version != 1 || length != size || !seq || seq >= 0x7fffffff || checksum(bytes, size) != crc) return -EINVAL;
    HddInstall *decoded = calloc(1, sizeof(*decoded)); HddFormatPlan p = {0};
    if (!decoded) return -ENOMEM;
    body(&w, decoded, &p, 1);
    int ret = w.error || w.offset != size || !valid(decoded, &p) ? -EINVAL : 0;
    if (!ret) { *in = *decoded; *plan = p; *sequence = seq; }
    else free(decoded->launcher_files);
    free(decoded); return ret;
}
