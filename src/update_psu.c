// Extract the selected ELF from verified legacy (2-file) or sidecar-complete (4-file) app PSUs.
#include "launchelf.h"
#include "self_update.h"

static unsigned int psu32(const unsigned char *p)
{
    return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int read_exact(int fd, unsigned char *buf, int size)
{
    while (size > 0) {
        int n = genRead(fd, buf, size);
        if (n <= 0 || n > size)
            return 0;
        buf += n;
        size -= n;
    }
    return 1;
}

static int record_is(const unsigned char *record, unsigned int mode, const char *name)
{
    return (record[0] | ((unsigned int)record[1] << 8)) == mode &&
           memchr(record + 64, 0, 448) != NULL && !strcmp((const char *)record + 64, name);
}

int selfUpdateExtractPsu(const char *package, const char *stage, int size)
{
    unsigned char directories[1536], entry[512], buffer[4096];
    int input = -1, output = -1, position = 1536, seen = 0, elf_size = -1, i, entries;
    char remove_path[MAX_PATH];

    if (size < 1536 || size > 16 * 1024 * 1024)
        return -1;
    input = genOpen(package, FIO_O_RDONLY);
    if (input < 0 || genLseek(input, 0, SEEK_END) != size ||
        genLseek(input, 0, SEEK_SET) != 0 || !read_exact(input, directories, 1536))
        goto done;
    entries = (int)psu32(directories + 4);
    /* A root entry counts itself, "." and ".." plus 2 or 4 files.
     * Preserve compatibility with older packages during migration. */
    if (!record_is(directories, 0x8027, SELF_UPDATE_FOLDER) ||
        (entries != 4 && entries != 6) ||
        !record_is(directories + 512, 0x8027, ".") ||
        !record_is(directories + 1024, 0x8027, ".."))
        goto done;

    for (i = 0; i < entries - 2; i++) {
        unsigned int length, padded;
        int is_elf;
        if (position > size - 512 || genLseek(input, position, SEEK_SET) != position ||
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
        } else if (entries == 6 &&
                   record_is(entry, 0x8017, "WLE-UPDATE-CA.PEM") &&
                   !(seen & 4) && length <= 32768) {
            seen |= 4;
        } else if (entries == 6 &&
                   record_is(entry, 0x8017, "BGM.ADP") &&
                   !(seen & 8) && length >= 32 && length <= 1024 * 1024 &&
                   !(length & 15)) {
            seen |= 8;
        } else {
            goto done;
        }
        position += padded;
    }
    if (seen == (entries == 6 ? 15 : 3) && position == size) {
        if (genClose(input) < 0)
            goto remove_stage;
        return elf_size;
    }
done:
    if (output >= 0)
        genClose(output);
    if (input >= 0)
        genClose(input);
remove_stage:
    snprintf(remove_path, sizeof(remove_path), "%s", stage);
    genRemove(remove_path);
    return -1;
}


/* Extract one missing runtime sidecar from a package already verified with
 * its GitHub SHA-256 and selfUpdateExtractPsu(). Do not call on an unchecked
 * archive or overwrite an existing staging filename. A legacy two-file PSU
 * returns 0 without creating a file. */
int selfUpdateExtractPsuSidecar(const char *package, const char *name,
                                const char *stage, int size)
{
    unsigned char directories[1536], entry[512], buffer[4096];
    int input = -1, output = -1, position = 1536, i, entries;
    int result = -1, created = 0;
    unsigned int length = 0;
    char remove_path[MAX_PATH];
    int is_ca = name != NULL && !strcmp(name, "WLE-UPDATE-CA.PEM");
    int is_bgm = name != NULL && !strcmp(name, "BGM.ADP");

    if ((!is_ca && !is_bgm) || package == NULL || stage == NULL ||
        size < 1536 || size > 16 * 1024 * 1024)
        return -1;

    input = genOpen(package, FIO_O_RDONLY);
    if (input < 0 || genLseek(input, 0, SEEK_END) != size ||
        genLseek(input, 0, SEEK_SET) != 0 ||
        !read_exact(input, directories, sizeof(directories)))
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
    if (entries != 6)
        goto done;

    for (i = 0; i < entries - 2; i++) {
        unsigned int padded;
        if (position > size - 512 ||
            genLseek(input, position, SEEK_SET) != position ||
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
            if ((is_ca && length > 32768) ||
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
    if (input >= 0 && genClose(input) < 0)
        result = -1;
    if (result < 0 && created) {
        snprintf(remove_path, sizeof(remove_path), "%s", stage);
        genRemove(remove_path);
    }
    return result;
}
