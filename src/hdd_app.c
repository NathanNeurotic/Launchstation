#include "hdd_app.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

int hddAppLeaf(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (!n || n > 32 || name[0] == '.' || name[n - 1] == '.' || name[n - 1] == ' ') return 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = name[i];
        if (c < 32 || c > 126 || strchr("\\/:*?\"<>|,", c)) return 0;
    }
    return 1;
}

static int elf(const SaveArchiveEntry *file)
{
    const unsigned char *b = file->data;
    /* Executable, little-endian ELF32 for MIPS. The loader does full segment
     * validation when launching; this rejects save data masquerading as ELF. */
    return file->size >= 52 && !memcmp(b, "\177ELF\1\1\1", 7) &&
           b[16] == 2 && !b[17] && b[18] == 8 && !b[19];
}

static void config(const SaveArchiveEntry *file, char title[65], char boot[33])
{
    size_t pos = 0;
    while (pos < file->size) {
        char line[256], *eq, *key, *value, *end;
        size_t start = pos;
        while (pos < file->size && file->data[pos] != '\n') pos++;
        size_t n = pos - start;
        if (pos < file->size) pos++;
        if (n >= sizeof(line)) continue;
        memcpy(line, file->data + start, n); line[n] = 0;
        if (memchr(line, 0, n)) continue;
        key = line; while (isspace((unsigned char)*key)) key++;
        eq = strchr(key, '='); if (!eq || *key == '#' || *key == ';') continue;
        end = eq; while (end > key && isspace((unsigned char)end[-1])) end--;
        *end = 0; value = eq + 1; while (isspace((unsigned char)*value)) value++;
        end = value + strlen(value); while (end > value && isspace((unsigned char)end[-1])) end--;
        *end = 0;
        if (*value == '"' && end > value + 1 && end[-1] == '"') { value++; end[-1] = 0; }
        if (!strcasecmp(key, "title") && strlen(value) <= 64) {
            int valid = *value != 0;
            for (char *p = value; *p; p++) if ((unsigned char)*p < 32 || *p == '$') valid = 0;
            if (valid) strcpy(title, value);
        }
        if (!strcasecmp(key, "boot") && hddAppLeaf(value)) strcpy(boot, value);
    }
}

int hddAppChooseBoot(const SaveArchive *save, HddApp *app, unsigned choice)
{
    if (!save || !app || choice >= app->elf_count) return -EINVAL;
    strcpy(app->boot, save->files[app->elf[choice]].name);
    return 0;
}

int hddAppInspect(const SaveArchive *save, HddApp *app)
{
    char preferred[33] = "";
    if (!save || !app || save->ps1 || !save->count || save->count > SAVE_ARCHIVE_FILES || !hddAppLeaf(save->folder.name)) return -EINVAL;
    memset(app, 0, sizeof(*app));
    strcpy(app->folder, save->folder.name); strcpy(app->title, save->folder.name);
    for (unsigned i = 0; i < save->count; i++) {
        const SaveArchiveEntry *f = &save->files[i];
        if (!hddAppLeaf(f->name) || !strcasecmp(f->name, "LSAPP.ID") || (f->mode & 0x30) != 0x10) return -EINVAL;
        for (unsigned j = 0; j < i; j++) if (!strcasecmp(f->name, save->files[j].name)) return -EINVAL;
        if (elf(f)) app->elf[app->elf_count++] = i;
        if (!strcasecmp(f->name, "title.cfg") && f->size <= 16384) config(f, app->title, preferred);
    }
    if (!app->elf_count) return -ENOEXEC;
    /* Use a bounded, case-stable identity for ownership and header names. */
    for (unsigned i = 0; app->folder[i]; i++) {
        unsigned char c = app->folder[i];
        app->asset[i] = isalnum(c) || c == '_' || c == '-' ? toupper(c) : '_';
    }
    if (!strcmp(app->asset, "APP_WLE-R3Z")) strcpy(app->asset, "APP_WLER3Z");
    if (!strcmp(app->asset, "SYS_R3CONFIGURATOR")) strcpy(app->asset, "SYS_R3CONFIG");
    /* The OSD driver's rename publishes at most 31 ID bytes. */
    if (strlen(app->folder) <= 28) snprintf(app->partition, sizeof(app->partition), "PP.%.28s", app->folder);
    else {
        char folded[33];
        for (unsigned i = 0; i <= strlen(app->folder); i++) folded[i] = toupper((unsigned char)app->folder[i]);
        snprintf(app->partition, sizeof(app->partition), "PP.%.19s.%08lx", app->folder, (unsigned long)crc32(0, (const unsigned char *)folded, strlen(folded)));
    }
    if (preferred[0]) for (unsigned i = 0; i < app->elf_count; i++)
        if (!strcasecmp(preferred, save->files[app->elf[i]].name)) return hddAppChooseBoot(save, app, i);
    return app->elf_count == 1 ? hddAppChooseBoot(save, app, 0) : 1;
}

int hddAppIconSettings(const char *script, const HddApp *app, char out[1024])
{
    const char *p, *end, *function_end;
    unsigned used = 0;
    if (!script || !app || !(p = strstr(script, "create_icon_sys() {")) ||
        !(function_end = strstr(p, "\n}")) || !(p = strstr(p, "<<EOL\nPS2X\n")) || p >= function_end) return -EINVAL;
    p += strlen("<<EOL\n");
    end = strstr(p, "\nEOL\n"); if (!end || end >= function_end || end - p > 1023) return -EINVAL;
    while (p <= end) {
        const char *text = NULL;
        if (!strncmp(p, "$title", 6)) { text = app->title; p += 6; }
        else if (!strncmp(p, "$publisher", 10)) { text = "Homebrew"; p += 10; }
        if (text) {
            size_t n = strlen(text); if (used + n >= 1024) return -EOVERFLOW;
            memcpy(out + used, text, n); used += n;
        } else {
            unsigned char c = *p++;
            /* No command/parameter substitution or shell control syntax. */
            if ((c < 32 && c != '\n') || c > 126 || strchr("$`\\;|&<>(){}", c) || used >= 1023) return -EINVAL;
            out[used++] = c;
        }
    }
    out[used] = 0;
    return used;
}

int hddAppInfo(const HddApp *app, char out[1024])
{
    char title_id[48], category[4] = "APP";
    static const char categories[] = "APP SYS EMU GME DST DBG PS1 RTE DEM";
    for (unsigned i = 0; i <= strlen(app->asset); i++) title_id[i] = app->asset[i] == '_' ? '-' : app->asset[i];
    if (strlen(app->asset) > 4 && app->asset[3] == '_') {
        for (unsigned i = 0; i + 3 <= sizeof(categories) - 1; i += 4)
            if (!memcmp(app->asset, categories + i, 3)) { memcpy(category, app->asset, 3); break; }
    }
    int n = snprintf(out, 1024,
        "title = [%s] %s\ntitle_id = %s\ntitle_sub_id = 0\nrelease_date = \ndeveloper_id = \n"
        "publisher_id = Homebrew\nnote = \ncontent_web = \nimage_topviewflag = 0\nimage_type = 0\n"
        "image_count = 1\nimage_viewsec = 600\ncopyright_viewflag = 0\ncopyright_imgcount = 0\ngenre = \n"
        "parental_lock = 1\neffective_date = 0\nexpire_date = 0\nviolence_flag = 0\ncontent_type = 255\ncontent_subtype = 0\n",
        category, app->title, title_id);
    return n > 0 && n < 1024 ? n : -EOVERFLOW;
}

int hddAppSystemCnf(const HddApp *app, char out[512])
{
    int n = snprintf(out, 512, "BOOT2 = PATINFO\nHDDUNITPOWER = NICHDD\npath = ata0:/APPS/%s/%s\ntitleid = %s\n",
                     app->folder, app->boot, app->asset);
    return n > 0 && n < 512 ? n : -EOVERFLOW;
}

static void put32(unsigned char *p, uint32_t v)
{ for (unsigned i = 0; i < 4; i++) p[i] = v >> (i * 8); }
static uint32_t get32(const unsigned char *p)
{ return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
int hddAppJournalEncode(const HddAppJournal *j, unsigned char b[HDD_APP_JOURNAL_SIZE])
{
    if (!j || !j->sequence || j->phase > HA_DONE || j->update > 1 || !j->package_size ||
        j->package_size > SAVE_ARCHIVE_LIMIT || !hddAppLeaf(j->boot)) return -EINVAL;
    memset(b, 0, HDD_APP_JOURNAL_SIZE); memcpy(b, "LSAPPJ1", 8);
    put32(b + 8, j->sequence); put32(b + 12, j->phase); put32(b + 16, j->update);
    put32(b + 20, j->package_crc); put32(b + 24, j->package_size); strcpy((char *)b + 32, j->boot);
    put32(b + 124, crc32(0, b, 124)); return 0;
}
int hddAppJournalDecode(HddAppJournal *j, const unsigned char b[HDD_APP_JOURNAL_SIZE])
{
    unsigned char checked[HDD_APP_JOURNAL_SIZE];
    if (memcmp(b, "LSAPPJ1", 8) || get32(b + 124) != crc32(0, b, 124) || !memchr(b + 32, 0, 33)) return -EINVAL;
    memset(j, 0, sizeof(*j));
    j->sequence = get32(b + 8); j->phase = get32(b + 12); j->update = get32(b + 16);
    j->package_crc = get32(b + 20); j->package_size = get32(b + 24); memcpy(j->boot, b + 32, 33);
    return hddAppJournalEncode(j, checked) < 0 || memcmp(checked, b, sizeof(checked)) ? -EINVAL : 0;
}
