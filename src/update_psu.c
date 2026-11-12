// Extract the selected ELF from verified legacy (2-file), sidecar (4-file), or icon/metadata (8/9-file) app PSUs.
#include "launchelf.h"
#include "self_update.h"

static unsigned int psu32(const unsigned char *p)
{
    return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* A verified package is read from its file or, when the updater had RAM
 * for it, straight from the download buffer. */
typedef struct {
    int fd;
    const unsigned char *mem;
    int size, pos;
} PsuIn;

static int read_exact(PsuIn *in, unsigned char *buf, int size)
{
    if (in->mem != NULL) {
        if (size < 0 || in->pos > in->size - size)
            return 0;
        memcpy(buf, in->mem + in->pos, (size_t)size);
        in->pos += size;
        return 1;
    }
    while (size > 0) {
        int n = genRead(in->fd, buf, size);
        if (n <= 0 || n > size)
            return 0;
        buf += n;
        size -= n;
    }
    return 1;
}

static int in_seek(PsuIn *in, int position)
{
    if (in->mem == NULL)
        return genLseek(in->fd, position, SEEK_SET);
    if (position < 0 || position > in->size)
        return -1;
    in->pos = position;
    return position;
}

/* Open a package file and check its length; the caller closes in->fd. */
static int in_open(PsuIn *in, const char *package, int size)
{
    in->mem = NULL;
    in->size = size;
    in->pos = 0;
    in->fd = genOpen(package, FIO_O_RDONLY);
    if (in->fd < 0)
        return 0;
    return genLseek(in->fd, 0, SEEK_END) == size && genLseek(in->fd, 0, SEEK_SET) == 0;
}

static int record_is(const unsigned char *record, unsigned int mode, const char *name)
{
    return (record[0] | ((unsigned int)record[1] << 8)) == mode &&
           memchr(record + 64, 0, 448) != NULL && !strcmp((const char *)record + 64, name);
}

static int extract_elf(PsuIn *input, const char *stage, int size)
{
    unsigned char directories[1536], entry[512], buffer[4096];
    int output = -1, position = 1536, seen = 0, elf_size = -1, i, entries;
    char remove_path[MAX_PATH];

    if (!read_exact(input, directories, 1536))
        goto done;
    entries = (int)psu32(directories + 4);
    /* A root entry counts itself, "." and ".." plus 2, 4 or 8 files.
     * Preserve compatibility with older packages during migration. */
    if (!record_is(directories, 0x8027, SELF_UPDATE_FOLDER) ||
        (entries != 4 && entries != 6 && entries != 10 && entries != 11) ||
        !record_is(directories + 512, 0x8027, ".") ||
        !record_is(directories + 1024, 0x8027, ".."))
        goto done;

    for (i = 0; i < entries - 2; i++) {
        unsigned int length, padded;
        int is_elf;
        if (position > size - 512 || in_seek(input, position) != position ||
            !read_exact(input, entry, 512))
            goto done;
        position += 512;
        length = psu32(entry + 4);
        if (length == 0 || length > (unsigned int)(size - position))
            goto done;
        padded = (length + 1023) & ~1023u;
        if (padded > (unsigned int)(size - position))
            goto done;
        is_elf = record_is(entry, 0x8017, SELF_UPDATE_ELF);
        if (is_elf) {
            unsigned int remaining = length;
            if ((seen & 1) || length < 52)
                goto done;
            output = genOpen(stage, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
            if (output < 0)
                goto done;
            while (remaining) {
                int count = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
                int written = 0;
                if (!read_exact(input, buffer, count))
                    goto done;
                while (written < count) {
                    int n = genWrite(output, buffer + written, count - written);
                    if (n <= 0 || n > count - written)
                        goto done;
                    written += n;
                }
                remaining -= count;
            }
            if (genClose(output) < 0) {
                output = -1;
                goto done;
            }
            output = -1;
            seen |= 1;
            elf_size = length;
        } else if (record_is(entry, 0x8017, "title.cfg") && !(seen & 2) && length <= 4096) {
            seen |= 2;
        } else if ((entries == 6 || entries >= 10) &&
                   record_is(entry, 0x8017, "WLE-UPDATE-CA.PEM") &&
                   !(seen & 4) && length <= 32768) {
            seen |= 4;
        } else if ((entries == 6 || entries >= 10) &&
                   record_is(entry, 0x8017, "BGM.ADP") &&
                   !(seen & 8) && length >= 32 && length <= 1024 * 1024 &&
                   !(length & 15)) {
            seen |= 8;
        } else if (entries >= 10 &&
                   record_is(entry, 0x8017, "icon.sys") &&
                   !(seen & 16) && length == 964) {
            seen |= 16;
        } else if (entries >= 10 &&
                   record_is(entry, 0x8017, "list.icn") &&
                   !(seen & 32) && length >= 20 && length <= 1024 * 1024) {
            seen |= 32;
        } else if (entries >= 10 &&
                   record_is(entry, 0x8017, "copy.icn") &&
                   !(seen & 64) && length >= 20 && length <= 1024 * 1024) {
            seen |= 64;
        } else if (entries >= 10 &&
                   record_is(entry, 0x8017, "del.icn") &&
                   !(seen & 128) && length >= 20 && length <= 1024 * 1024) {
            seen |= 128;
        } else if (entries == 11 &&
                   record_is(entry, 0x8017, "APPINFO.PBT") &&
                   !(seen & 256) && length >= 32 && length <= 65536) {
            seen |= 256;
        } else {
            goto done;
        }
        position += padded;
    }
    if (seen == (entries == 11 ? 511 : entries >= 10 ? 255 : entries == 6 ? 15 : 3) && position == size)
        return elf_size;
done:
    if (output >= 0)
        genClose(output);
    snprintf(remove_path, sizeof(remove_path), "%s", stage);
    genRemove(remove_path);
    return -1;
}

int selfUpdateExtractPsu(const char *package, const char *stage, int size)
{
    PsuIn in;
    int result = -1;
    char remove_path[MAX_PATH];

    if (size < 1536 || size > 16 * 1024 * 1024)
        return -1;
    if (in_open(&in, package, size))
        result = extract_elf(&in, stage, size);
    if (in.fd >= 0 && genClose(in.fd) < 0 && result >= 0) {
        snprintf(remove_path, sizeof(remove_path), "%s", stage);
        genRemove(remove_path);
        result = -1;
    }
    return result;
}

int selfUpdateExtractPsuMem(const unsigned char *package, int size, const char *stage)
{
    PsuIn in = {-1, package, size, 0};

    if (package == NULL || size < 1536 || size > 16 * 1024 * 1024)
        return -1;
    return extract_elf(&in, stage, size);
}


/* Extract one missing runtime sidecar from a package already verified with
 * its GitHub SHA-256 and selfUpdateExtractPsu(). Do not call on an unchecked
 * archive or overwrite an existing staging filename. A legacy two-file PSU
 * returns 0 without creating a file. */
static int extract_sidecar(PsuIn *input, const char *name, const char *stage, int size)
{
    unsigned char directories[1536], entry[512], buffer[4096];
    int output = -1, position = 1536, i, entries;
    int result = -1, created = 0;
    unsigned int length = 0;
    char remove_path[MAX_PATH];
    int is_ca = name != NULL && !strcmp(name, "WLE-UPDATE-CA.PEM");
    int is_bgm = name != NULL && !strcmp(name, "BGM.ADP");

    if ((!is_ca && !is_bgm) || stage == NULL)
        return -1;
    if (!read_exact(input, directories, sizeof(directories)))
        goto done;
    entries = (int)psu32(directories + 4);
    if (!record_is(directories, 0x8027, SELF_UPDATE_FOLDER) ||
        !record_is(directories + 512, 0x8027, ".") ||
        !record_is(directories + 1024, 0x8027, ".."))
        goto done;
    if (entries == 4) {
        result = 0; /* compatible, older package without sidecars */
        goto done;
    }
    if (entries != 6 && entries != 10 && entries != 11)
        goto done;

    /* Validate the whole archive before returning an early record. Sidecars
     * are no longer necessarily the last files in the package. */
    for (i = 0; i < entries - 2; i++) {
        unsigned int padded;
        if (position > size - 512 ||
            in_seek(input, position) != position ||
            !read_exact(input, entry, sizeof(entry)))
            goto done;
        position += 512;
        length = psu32(entry + 4);
        if (length == 0 || length > (unsigned int)(size - position))
            goto done;
        padded = (length + 1023u) & ~1023u;
        if (padded > (unsigned int)(size - position))
            goto done;
        position += padded;
    }
    if (position != size)
        goto done;
    position = 1536;

    for (i = 0; i < entries - 2; i++) {
        unsigned int padded;
        if (position > size - 512 ||
            in_seek(input, position) != position ||
            !read_exact(input, entry, sizeof(entry)))
            goto done;
        position += 512;
        length = psu32(entry + 4);
        if (length == 0 || length > (unsigned int)(size - position))
            goto done;
        padded = (length + 1023u) & ~1023u;
        if (padded > (unsigned int)(size - position))
            goto done;
        if (record_is(entry, 0x8017, name)) {
            unsigned int remaining = length;
            if ((is_ca && (length < sizeof("-----BEGIN CERTIFICATE-----") - 1 ||
                           length > 32768)) ||
                (is_bgm && (length < 32 || length > 1024 * 1024 ||
                            (length & 15))))
                goto done;
            output = genOpen(stage, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
            if (output < 0)
                goto done;
            created = 1;
            while (remaining > 0) {
                int count = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
                int written = 0;
                if (!read_exact(input, buffer, count))
                    goto done;
                if (remaining == length &&
                    ((is_ca && memcmp(buffer, "-----BEGIN CERTIFICATE-----", 27)) ||
                     (is_bgm && memcmp(buffer, "APCM\x01\x01\x01\x00", 8))))
                    goto done;
                while (written < count) {
                    int n = genWrite(output, buffer + written, count - written);
                    if (n <= 0 || n > count - written)
                        goto done;
                    written += n;
                }
                remaining -= count;
            }
            if (genClose(output) < 0) {
                output = -1;
                goto done;
            }
            output = -1;
            result = (int)length;
            goto done;
        }
        position += padded;
    }
done:
    if (output >= 0)
        genClose(output);
    if (result < 0 && created) {
        snprintf(remove_path, sizeof(remove_path), "%s", stage);
        genRemove(remove_path);
    }
    return result;
}

int selfUpdateExtractPsuSidecar(const char *package, const char *name,
                                const char *stage, int size)
{
    PsuIn in;
    int result = -1;
    char remove_path[MAX_PATH];

    if (package == NULL || size < 1536 || size > 16 * 1024 * 1024)
        return -1;
    if (in_open(&in, package, size))
        result = extract_sidecar(&in, name, stage, size);
    if (in.fd >= 0 && genClose(in.fd) < 0 && result != -1) {
        if (result > 0) {
            snprintf(remove_path, sizeof(remove_path), "%s", stage);
            genRemove(remove_path);
        }
        result = -1;
    }
    return result;
}

int selfUpdateExtractPsuSidecarMem(const unsigned char *package, int size,
                                   const char *name, const char *stage)
{
    PsuIn in = {-1, package, size, 0};

    if (package == NULL || size < 1536 || size > 16 * 1024 * 1024)
        return -1;
    return extract_sidecar(&in, name, stage, size);
}
