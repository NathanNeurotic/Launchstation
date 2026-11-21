#include "install_tar.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    const InstallArchiveIo *io;
    uint32_t size, offset, expanded;
    z_stream z;
    unsigned char input[32768], output[32768];
    unsigned pos, count;
    int ended;
} Reader;
static int fill(Reader *r)
{
    int ret;
    if (r->ended) return 0;
    if (!r->z.avail_in && r->offset < r->size) {
        unsigned n = r->size - r->offset;
        if (n > sizeof(r->input)) n = sizeof(r->input);
        ret = r->io->read(r->io->context, r->offset, r->input, n);
        if (ret < 0) return ret;
        r->offset += n; r->z.next_in = r->input; r->z.avail_in = n;
    }
    r->z.next_out = r->output; r->z.avail_out = sizeof(r->output);
    ret = inflate(&r->z, Z_NO_FLUSH);
    r->pos = 0; r->count = sizeof(r->output) - r->z.avail_out;
    if (r->count > 1024u * 1024 * 1024 - r->expanded) return -EFBIG;
    r->expanded += r->count;
    if (ret == Z_STREAM_END) {
        r->ended = 1;
        if (r->z.avail_in || r->offset != r->size) return -EINVAL;
    } else if (ret != Z_OK || (!r->count && !r->z.avail_in && r->offset == r->size)) return -EINVAL;
    return 1;
}
static int take(Reader *r, void *data, unsigned size)
{
    unsigned char *p = data;
    while (size) {
        unsigned n;
        int ret;
        if (r->pos == r->count) {
            ret = fill(r);
            if (ret <= 0) return ret < 0 ? ret : -EINVAL;
        }
        n = r->count - r->pos; if (n > size) n = size;
        if (p) { memcpy(p, r->output + r->pos, n); p += n; }
        r->pos += n; size -= n;
    }
    return 0;
}
static int number(const unsigned char *p, unsigned n, uint32_t *value)
{
    uint64_t v = 0;
    unsigned i = 0;
    if (p[0] & 0x80) {
        if (p[0] != 0x80) return -EINVAL; /* positive base-256 only */
        for (i = 1; i < n; i++) { v = (v << 8) | p[i]; if (v > UINT32_MAX) return -EOVERFLOW; }
    } else {
        while (i < n && p[i] == ' ') i++;
        for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
            v = (v << 3) + p[i] - '0'; if (v > UINT32_MAX) return -EOVERFLOW;
        }
        for (; i < n; i++) if (p[i] && p[i] != ' ') return -EINVAL;
    }
    *value = (uint32_t)v; return 0;
}
static int field(char *out, const unsigned char *in, unsigned n)
{
    unsigned len = 0;
    while (len < n && in[len]) len++;
    if (len >= INSTALL_TAR_PATH) return -ENAMETOOLONG;
    memcpy(out, in, len); out[len] = 0; return 0;
}
static int path(char *name)
{
    char *p = name, *part;
    unsigned n;
    while (p[0] == '.' && p[1] == '/') p += 2;
    if (!strcmp(p, ".")) p++;
    if (*p == '/') return -EINVAL;
    memmove(name, p, strlen(p) + 1);
    n = strlen(name); while (n && name[n - 1] == '/') name[--n] = 0;
    p = name;
    while (*p) {
        part = p;
        while (*p && *p != '/') { if ((unsigned char)*p < 32 || *p == '\\') return -EINVAL; p++; }
        n = p - part;
        if (!n || n > 255 || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return -EINVAL;
        if (*p) p++;
    }
    return 0;
}
static int decimal(const char *s, unsigned n, uint32_t *out)
{
    uint64_t v = 0;
    unsigned i = 0;
    if (!n) return -EINVAL;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + s[i++] - '0'; if (v > UINT32_MAX) return -EOVERFLOW;
    }
    /* PAX timestamps may include fractional seconds. */
    if (i < n && s[i] == '.') { i++; while (i < n && s[i] >= '0' && s[i] <= '9') i++; }
    if (i != n) return -EINVAL;
    *out = (uint32_t)v; return 0;
}
static int pax(InstallTarEntry *e, const char *text, unsigned size, int global)
{
    unsigned at = 0;
    while (at < size) {
        uint32_t len = 0;
        unsigned k = at, start, eq, end;
        while (k < size && text[k] >= '0' && text[k] <= '9') {
            if (len > 8192) return -EINVAL;
            len = len * 10 + text[k++] - '0';
        }
        if (k == at || k >= size || text[k++] != ' ' || len < k - at + 3 || len > size - at) return -EINVAL;
        start = k; end = at + len - 1;
        if (text[end] != '\n') return -EINVAL;
        for (eq = start; eq < end && text[eq] != '='; eq++);
        if (eq == end) return -EINVAL;
#define KEY(s) (eq - start == sizeof(s) - 1 && !memcmp(text + start, s, sizeof(s) - 1))
        if (KEY("path") || KEY("linkpath")) {
            char *dst = KEY("path") ? e->name : e->link;
            if (global || end - eq - 1 >= INSTALL_TAR_PATH || memchr(text + eq + 1, 0, end - eq - 1)) return -EINVAL;
            memcpy(dst, text + eq + 1, end - eq - 1); dst[end - eq - 1] = 0;
        } else if (KEY("size") || KEY("uid") || KEY("gid") || KEY("mtime")) {
            uint32_t *dst = KEY("size") ? &e->size : KEY("uid") ? &e->uid : KEY("gid") ? &e->gid : &e->mtime;
            if (global || decimal(text + eq + 1, end - eq - 1, dst) < 0) return -EINVAL;
        } else if ((eq - start >= 10 && !memcmp(text + start, "GNU.sparse", 10)) || KEY("SCHILY.filetype")) return -ENOTSUP;
#undef KEY
        at += len;
    }
    return 0;
}
int installTarGzip(const InstallArchiveIo *io, uint32_t bytes, const InstallTarSink *sink)
{
    Reader *r;
    InstallTarEntry *e;
    unsigned char header[512], buffer[32768];
    char *extra, longname[INSTALL_TAR_PATH] = "", longlink[INSTALL_TAR_PATH] = "";
    unsigned paxsize = 0, entries = 0;
    int ret = -EINVAL, initialized = 0;
    if (!io || !io->read || !sink || !sink->begin || !sink->data || !sink->end || !bytes || bytes > 256u * 1024 * 1024) return -EINVAL;
    r = calloc(1, sizeof(*r)); e = calloc(1, sizeof(*e)); extra = malloc(8193);
    if (!r || !e || !extra) { ret = -ENOMEM; goto done; }
    r->io = io; r->size = bytes;
    if (inflateInit2(&r->z, 15 + 16) != Z_OK) { ret = -ENOMEM; goto done; }
    initialized = 1;
    for (;;) {
        uint32_t checksum, sum = 0, remaining;
        unsigned padding;
        int zero = 1;
        ret = take(r, header, 512); if (ret < 0) goto done;
        for (unsigned i = 0; i < 512; i++) if (header[i]) { zero = 0; break; }
        if (zero) {
            ret = take(r, header, 512); if (ret < 0) goto done;
            for (unsigned i = 0; i < 512; i++) if (header[i]) { ret = -EINVAL; goto done; }
            if (paxsize || *longname || *longlink) { ret = -EINVAL; goto done; }
            for (;;) {
                while (r->pos < r->count) if (r->output[r->pos++]) { ret = -EINVAL; goto done; }
                ret = fill(r); if (ret <= 0) goto done;
            }
        }
        if (++entries > 150000 || number(header + 148, 8, &checksum) < 0) { ret = -EINVAL; goto done; }
        for (unsigned i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : header[i];
        if (checksum != sum) { ret = -EINVAL; goto done; }
        memset(e, 0, sizeof(*e));
        field(e->name, header, 100); field(e->link, header + 157, 100);
        e->type = header[156] ? header[156] : '0';
        if (number(header + 100, 8, &e->mode) < 0 || number(header + 108, 8, &e->uid) < 0 ||
            number(header + 116, 8, &e->gid) < 0 || number(header + 124, 12, &e->size) < 0 ||
            number(header + 136, 12, &e->mtime) < 0 || number(header + 329, 8, &e->major) < 0 ||
            number(header + 337, 8, &e->minor) < 0) { ret = -EINVAL; goto done; }
        if (!memcmp(header + 257, "ustar\0", 6) && header[345]) {
            char prefix[156]; unsigned n = 0;
            while (n < 155 && header[345 + n]) { prefix[n] = header[345 + n]; n++; } prefix[n] = 0;
            memmove(e->name + n + 1, e->name, strlen(e->name) + 1); memcpy(e->name, prefix, n); e->name[n] = '/';
        }
        if (e->type == 'L' || e->type == 'K' || e->type == 'x' || e->type == 'g') {
            char *dst = e->type == 'L' ? longname : longlink;
            unsigned limit = e->type == 'L' || e->type == 'K' ? INSTALL_TAR_PATH - 1 : 8192;
            if (!e->size || e->size > limit || ((e->type == 'x' || e->type == 'g') && paxsize)) { ret = -EINVAL; goto done; }
            ret = take(r, buffer, e->size); if (ret < 0) goto done;
            buffer[e->size] = 0;
            if (e->type == 'x') { memcpy(extra, buffer, e->size + 1); paxsize = e->size; }
            else if (e->type == 'g') { ret = pax(e, (char *)buffer, e->size, 1); if (ret < 0) goto done; }
            else {
                unsigned n = strlen((char *)buffer);
                if (n + 1 < e->size || n >= INSTALL_TAR_PATH) { ret = -EINVAL; goto done; }
                memcpy(dst, buffer, n + 1);
            }
            ret = take(r, NULL, (512 - e->size % 512) % 512); if (ret < 0) goto done;
            continue;
        }
        if (*longname) { strcpy(e->name, longname); *longname = 0; }
        if (*longlink) { strcpy(e->link, longlink); *longlink = 0; }
        if (paxsize) { ret = pax(e, extra, paxsize, 0); paxsize = 0; if (ret < 0) goto done; }
        if (path(e->name) < 0 || (e->type == '1' && path(e->link) < 0) ||
            !strchr("0123456", e->type) || (e->type != '0' && e->size) || e->size > 16u * 1024 * 1024) { ret = -EINVAL; goto done; }
        ret = sink->begin(sink->context, e); if (ret < 0) goto done;
        remaining = e->size; padding = (512 - e->size % 512) % 512;
        while (remaining) {
            unsigned n = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
            ret = take(r, buffer, n); if (ret < 0) goto done;
            ret = sink->data(sink->context, buffer, n); if (ret < 0) goto done;
            remaining -= n;
        }
        ret = sink->end(sink->context); if (ret < 0) goto done;
        ret = take(r, NULL, padding); if (ret < 0) goto done;
    }
done:
    if (initialized) inflateEnd(&r->z);
    free(extra); free(e); free(r); return ret;
}
