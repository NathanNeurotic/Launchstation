// Extract only the current controller variant's ELF from a verified app PSU.
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
    int input = -1, output = -1, position = 1536, seen = 0, elf_size = -1, i;
    char remove_path[MAX_PATH];

    if (size < 1536 || size > 16 * 1024 * 1024)
        return -1;
    input = genOpen(package, FIO_O_RDONLY);
    if (input < 0 || genLseek(input, 0, SEEK_END) != size ||
        genLseek(input, 0, SEEK_SET) != 0 || !read_exact(input, directories, 1536))
        goto done;
    if (!record_is(directories, 0x8027, SELF_UPDATE_FOLDER) || psu32(directories + 4) != 4 ||
        !record_is(directories + 512, 0x8027, ".") ||
        !record_is(directories + 1024, 0x8027, ".."))
        goto done;

    for (i = 0; i < 2; i++) {
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
        } else {
            goto done;
        }
        position += padded;
    }
    if (seen == 3 && position == size) {
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
