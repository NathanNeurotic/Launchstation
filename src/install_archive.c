#include "install_archive.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t u16(const unsigned char *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return u16(p) | u16(p + 2) << 16; }

int installZipExtract(const InstallArchiveIo *io, uint32_t bytes, const char *name,
                        uint32_t maximum, uint32_t *written)
{
    unsigned char *memory, *input, *output, fixed[46], local[30], stored_name[256];
    uint32_t tail_size, tail_start, eocd = 0, directory, directory_size, pos, size = 0;
    uint32_t compressed = 0, crc = 0, offset = 0, local_offset = 0, actual_crc, done = 0;
    unsigned count, i, matches = 0, method = 0, flags = 0;
    z_stream stream;
    int ret = -EINVAL, zret = Z_OK, zready = 0, found = 0;
    if (written) *written = 0;
    if (!io || !io->read || !io->write || !name || !name[0] || strlen(name) > 255 || bytes < 22 || !maximum)
        return -EINVAL;
    memory = malloc(65557 + 32768);
    if (!memory) return -ENOMEM;
    input = memory; output = memory + 65557;
    tail_size = bytes < 65557 ? bytes : 65557;
    tail_start = bytes - tail_size;
    ret = io->read(io->context, tail_start, input, tail_size);
    if (ret < 0) goto done;
    for (i = tail_size - 22 + 1; i-- > 0;) {
        if (u32(input + i) == 0x06054b50 && i + 22 + u16(input + i + 20) == tail_size) {
            eocd = i; found = 1; break;
        }
    }
    ret = -EINVAL;
    if (!found || u16(input + eocd + 4) || u16(input + eocd + 6) ||
        u16(input + eocd + 8) != u16(input + eocd + 10)) goto done;
    count = u16(input + eocd + 10);
    directory_size = u32(input + eocd + 12); directory = u32(input + eocd + 16);
    if (!count || count > 1024 || (uint64_t)directory + directory_size != tail_start + eocd) goto done;
    pos = directory;
    for (i = 0; i < count; i++) {
        uint32_t n, extra, comment;
        if (pos > directory + directory_size || directory + directory_size - pos < 46) goto done;
        ret = io->read(io->context, pos, fixed, sizeof(fixed));
        if (ret < 0) goto done;
        ret = -EINVAL;
        n = u16(fixed + 28); extra = u16(fixed + 30); comment = u16(fixed + 32);
        if (u32(fixed) != 0x02014b50 || !n || n > 255 || u16(fixed + 34) ||
            (uint64_t)pos + 46 + n + extra + comment > (uint64_t)directory + directory_size) goto done;
        ret = io->read(io->context, pos + 46, stored_name, n);
        if (ret < 0) goto done;
        ret = -EINVAL;
        if (memchr(stored_name, 0, n)) goto done;
        stored_name[n] = 0;
        if (!strcmp((char *)stored_name, name)) {
            if (++matches > 1) goto done;
            flags = u16(fixed + 8); method = u16(fixed + 10); crc = u32(fixed + 16);
            compressed = u32(fixed + 20); size = u32(fixed + 24); local_offset = u32(fixed + 42);
            if ((flags & ~0x80eu) || (method != 0 && method != 8) || !size || size > maximum ||
                size == UINT32_MAX || compressed == UINT32_MAX ||
                ((u32(fixed + 38) >> 16) & 0170000) == 0120000 || local_offset >= directory) goto done;
        }
        pos += 46 + n + extra + comment;
    }
    ret = -EINVAL;
    if (matches != 1 || pos != directory + directory_size || local_offset > directory || directory - local_offset < 30) goto done;
    ret = io->read(io->context, local_offset, local, sizeof(local));
    if (ret < 0) goto done;
    ret = -EINVAL;
    if (u32(local) != 0x04034b50 || u16(local + 6) != flags || u16(local + 8) != method ||
        u16(local + 26) != strlen(name)) goto done;
    offset = local_offset + 30 + u16(local + 26) + u16(local + 28);
    if ((uint64_t)local_offset + 30 + u16(local + 26) + u16(local + 28) + compressed > directory) goto done;
    ret = io->read(io->context, local_offset + 30, stored_name, u16(local + 26));
    if (ret < 0) goto done;
    ret = -EINVAL;
    if (memcmp(stored_name, name, strlen(name))) goto done;
    if (!(flags & 8) && (u32(local + 14) != crc || u32(local + 18) != compressed || u32(local + 22) != size)) goto done;
    memset(&stream, 0, sizeof(stream));
    if (method == 8) {
        if (inflateInit2(&stream, -15) != Z_OK) { ret = -ENOMEM; goto done; }
        zready = 1;
    } else if (compressed != size) goto done;
    actual_crc = crc32(0, NULL, 0);
    pos = 0;
    while (pos < compressed) {
        unsigned n = compressed - pos > 32768 ? 32768 : compressed - pos;
        ret = io->read(io->context, offset + pos, input, n);
        if (ret < 0) goto done;
        pos += n;
        if (method == 0) {
            actual_crc = crc32(actual_crc, input, n);
            ret = io->write(io->context, input, n);
            if (ret < 0) goto done;
            done += n;
        } else {
            stream.next_in = input; stream.avail_in = n;
            do {
                unsigned produced;
                stream.next_out = output; stream.avail_out = 32768;
                zret = inflate(&stream, Z_NO_FLUSH);
                if (zret != Z_OK && zret != Z_STREAM_END) { ret = -EINVAL; goto done; }
                produced = 32768 - stream.avail_out;
                if (produced > size - done) { ret = -EFBIG; goto done; }
                actual_crc = crc32(actual_crc, output, produced);
                if (produced && (ret = io->write(io->context, output, produced)) < 0) goto done;
                done += produced;
                if (zret == Z_STREAM_END && (stream.avail_in || pos != compressed)) { ret = -EINVAL; goto done; }
            } while (zret != Z_STREAM_END && (stream.avail_in || !stream.avail_out));
            if (zret == Z_STREAM_END) break;
        }
    }
    ret = (done == size && actual_crc == crc && (method == 0 || zret == Z_STREAM_END)) ? 0 : -EINVAL;
    if (ret == 0 && written) *written = done;
done:
    if (zready) inflateEnd(&stream);
    free(memory);
    return ret;
}
