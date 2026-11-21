/* Focused ext2 builder for PSBBN's freshly created, single-extent volumes.
 * Fixed 4 KiB blocks, 128-byte inodes, revision 1 with no optional features.
 * This is an installer sink, not a general filesystem driver. */
#include "install_ext2.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define BS 4096u
#define BPG 32768u
#define IPG 8192u
#define META 260u
#define CACHE 32
#define BLOOM 65536u
static uint16_t r16(const unsigned char *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t r32(const unsigned char *p) { return r16(p) | (uint32_t)r16(p + 2) << 16; }
static void w16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void w32(unsigned char *p, uint32_t v) { w16(p, v); w16(p + 2, v >> 16); }
struct InstallExt2 {
    HddFormatIo io;
    uint32_t start, blocks, groups, serial, freeblocks, freeinodes;
    unsigned char *gdt, *bloom;
    unsigned char bb[BS], ib[BS], it[BS], file[BS], inode[128];
    uint32_t bbgroup, ibgroup, itblock, ino, position, size;
    int bbdirty, ibdirty, itdirty, active;
    struct { char path[INSTALL_TAR_PATH]; uint32_t ino; } cache[CACHE];
    unsigned nextcache;
    unsigned cursor[16][2];
};
static int readBlock(InstallExt2 *f, uint32_t b, void *p)
{ return b < f->blocks ? f->io.read(f->io.context, f->start + b * 8, 8, p) : -EINVAL; }
static int writeBlock(InstallExt2 *f, uint32_t b, const void *p)
{ return b < f->blocks ? f->io.write(f->io.context, f->start + b * 8, 8, p) : -EINVAL; }
static uint32_t groupSize(InstallExt2 *f, uint32_t g)
{ uint32_t n = f->blocks - g * BPG; return n < BPG ? n : BPG; }
static int bitmap(InstallExt2 *f, int inode, uint32_t g)
{
    unsigned char *data = inode ? f->ib : f->bb;
    uint32_t *old = inode ? &f->ibgroup : &f->bbgroup;
    int *dirty = inode ? &f->ibdirty : &f->bbdirty, ret;
    if (*old == g) return 0;
    if (*dirty) { ret = writeBlock(f, *old * BPG + (inode ? 3 : 2), data); if (ret < 0) return ret; *dirty = 0; }
    ret = readBlock(f, g * BPG + (inode ? 3 : 2), data);
    if (ret >= 0) *old = g;
    return ret;
}
static int inodeBlock(InstallExt2 *f, uint32_t b)
{
    int ret;
    if (f->itblock == b) return 0;
    if (f->itdirty) { ret = writeBlock(f, f->itblock, f->it); if (ret < 0) return ret; f->itdirty = 0; }
    ret = readBlock(f, b, f->it); if (ret >= 0) f->itblock = b; return ret;
}
static int getInode(InstallExt2 *f, uint32_t ino, unsigned char *data, int put)
{
    uint32_t g, index, b;
    int ret;
    if (!ino || ino > f->groups * IPG) return -EINVAL;
    g = (ino - 1) / IPG; index = (ino - 1) % IPG; b = g * BPG + 4 + index / 32;
    ret = inodeBlock(f, b); if (ret < 0) return ret;
    if (put) { memcpy(f->it + (index % 32) * 128, data, 128); f->itdirty = 1; }
    else memcpy(data, f->it + (index % 32) * 128, 128);
    return 0;
}
static int alloc(InstallExt2 *f, int inode, uint32_t *value)
{
    unsigned char zero[BS] = {0};
    for (uint32_t g = 0; g < f->groups; g++) {
        unsigned char *desc = f->gdt + g * 32, *data = inode ? f->ib : f->bb;
        unsigned limit = inode ? IPG : groupSize(f, g), off = inode ? 14 : 12;
        int ret;
        if (!r16(desc + off)) continue;
        ret = bitmap(f, inode, g); if (ret < 0) return ret;
        for (unsigned i = f->cursor[g][inode]; i < limit; i++) if (!(data[i / 8] & (1u << (i % 8)))) {
            f->cursor[g][inode] = i + 1;
            data[i / 8] |= 1u << (i % 8); w16(desc + off, r16(desc + off) - 1);
            if (inode) { f->ibdirty = 1; f->freeinodes--; *value = g * IPG + i + 1; return 0; }
            f->bbdirty = 1; f->freeblocks--; *value = g * BPG + i;
            return writeBlock(f, *value, zero);
        }
        return -EIO;
    }
    return -ENOSPC;
}
static int release(InstallExt2 *f, int inode, uint32_t value)
{
    uint32_t g, i;
    unsigned char *data = inode ? f->ib : f->bb, *desc;
    int ret;
    if (inode) { if (value < 11 || value > f->groups * IPG) return -EINVAL; value--; }
    else if (value >= f->blocks || value % BPG < META) return -EINVAL;
    g = value / (inode ? IPG : BPG); i = value % (inode ? IPG : BPG); desc = f->gdt + g * 32;
    ret = bitmap(f, inode, g); if (ret < 0) return ret;
    if (!(data[i / 8] & (1u << (i % 8)))) return -EIO;
    data[i / 8] &= ~(1u << (i % 8));
    if (i < f->cursor[g][inode]) f->cursor[g][inode] = i;
    w16(desc + (inode ? 14 : 12), r16(desc + (inode ? 14 : 12)) + 1);
    if (inode) { f->ibdirty = 1; f->freeinodes++; } else { f->bbdirty = 1; f->freeblocks++; }
    return 0;
}
static int mapped(InstallExt2 *f, unsigned char *ino, uint32_t index, int create, uint32_t *block)
{
    unsigned char indirect[BS];
    unsigned levels, slot;
    uint32_t b, offsets[2];
    int ret;
    if (index < 12) { levels = 0; slot = index; }
    else if ((index -= 12) < 1024) { levels = 1; slot = 12; offsets[0] = index; }
    else { index -= 1024; if (index >= 1024 * 1024) return -EFBIG; levels = 2; slot = 13; offsets[0] = index / 1024; offsets[1] = index % 1024; }
    b = r32(ino + 40 + slot * 4);
    if (!b && create) {
        ret = alloc(f, 0, &b); if (ret < 0) return ret;
        w32(ino + 40 + slot * 4, b); w32(ino + 28, r32(ino + 28) + 8);
    }
    for (unsigned level = 0; b && level < levels; level++) {
        uint32_t next;
        ret = readBlock(f, b, indirect); if (ret < 0) return ret;
        next = r32(indirect + offsets[level] * 4);
        if (!next && create) {
            ret = alloc(f, 0, &next); if (ret < 0) return ret;
            w32(indirect + offsets[level] * 4, next);
            ret = writeBlock(f, b, indirect); if (ret < 0) return ret;
            w32(ino + 28, r32(ino + 28) + 8);
        }
        b = next;
    }
    *block = b; return 0;
}
static int freeTree(InstallExt2 *f, uint32_t block, unsigned depth)
{
    unsigned char data[BS];
    int ret;
    if (!block) return 0;
    if (depth) {
        ret = readBlock(f, block, data); if (ret < 0) return ret;
        for (unsigned i = 0; i < 1024; i++) if (r32(data + i * 4)) {
            ret = freeTree(f, r32(data + i * 4), depth - 1); if (ret < 0) return ret;
        }
    }
    return release(f, 0, block);
}
static int truncateInode(InstallExt2 *f, unsigned char *ino)
{
    int ret;
    unsigned type = r16(ino) & 0170000;
    if (type == 0100000 || type == 0040000 || (type == 0120000 && r32(ino + 28))) {
        for (unsigned i = 0; i < 15; i++) {
            ret = freeTree(f, r32(ino + 40 + 4 * i), i < 12 ? 0 : i - 11); if (ret < 0) return ret;
        }
    }
    memset(ino + 40, 0, 60); w32(ino + 4, 0); w32(ino + 28, 0); return 0;
}
static unsigned record(unsigned n) { return (8 + n + 3) & ~3u; }
static void dirent(unsigned char *p, uint32_t ino, unsigned len, const char *name)
{ unsigned n = strlen(name); w32(p, ino); w16(p + 4, len); w16(p + 6, n); memcpy(p + 8, name, n); }
/* Replace an existing entry, or append without scanning when the caller's
 * path bloom filter proves it new. Directories are never removed/replaced. */
static int directory(InstallExt2 *f, uint32_t dir, const char *name, uint32_t *ino, int add, int scan)
{
    unsigned char node[128], data[BS];
    uint32_t count, b = 0;
    unsigned n = strlen(name), at = 0, last = 0;
    int ret;
    if (!n || n > 255) return -EINVAL;
    ret = getInode(f, dir, node, 0); if (ret < 0) return ret;
    if ((r16(node) & 0170000) != 0040000) return -ENOTDIR;
    count = (r32(node + 4) + BS - 1) / BS;
    if (scan) for (uint32_t i = 0; i < count; i++) {
        ret = mapped(f, node, i, 0, &b); if (ret < 0 || !b) return ret < 0 ? ret : -EIO;
        ret = readBlock(f, b, data); if (ret < 0) return ret;
        for (at = 0; at < BS; at += r16(data + at + 4)) {
            unsigned len = r16(data + at + 4), chars = r16(data + at + 6);
            if (len < 8 || len % 4 || len > BS - at || chars > len - 8) return -EIO;
            if (r32(data + at) && chars == n && !memcmp(data + at + 8, name, n)) {
                if (!add) { *ino = r32(data + at); return 0; }
                w32(data + at, *ino); return writeBlock(f, b, data);
            }
        }
    }
    if (!add) return -ENOENT;
    if (count) {
        ret = mapped(f, node, count - 1, 0, &b); if (ret < 0 || !b) return ret < 0 ? ret : -EIO;
        ret = readBlock(f, b, data); if (ret < 0) return ret;
        for (at = 0; at < BS;) {
            unsigned len = r16(data + at + 4), chars = r16(data + at + 6);
            if (len < 8 || len % 4 || len > BS - at || chars > len - 8) return -EIO;
            last = at; at += len;
        }
        at = last + record(r16(data + last + 6));
        if (BS - at >= record(n)) {
            w16(data + last + 4, at - last); dirent(data + at, *ino, BS - at, name);
            return writeBlock(f, b, data);
        }
    }
    ret = mapped(f, node, count, 1, &b); if (ret < 0) return ret;
    memset(data, 0, BS); dirent(data, *ino, BS, name);
    ret = writeBlock(f, b, data); if (ret < 0) return ret;
    w32(node + 4, (count + 1) * BS); return getInode(f, dir, node, 1);
}
static int seen(InstallExt2 *f, const char *path, int add)
{
    uint32_t a = 2166136261u, b = 5381;
    int yes;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) { a = (a ^ *p) * 16777619u; b = b * 33 ^ *p; }
    a %= BLOOM * 8; b %= BLOOM * 8;
    yes = (f->bloom[a / 8] & (1 << (a % 8))) && (f->bloom[b / 8] & (1 << (b % 8)));
    if (add) { f->bloom[a / 8] |= 1 << (a % 8); f->bloom[b / 8] |= 1 << (b % 8); }
    return yes;
}
static int lookup(InstallExt2 *f, const char *path, uint32_t *ino)
{
    char copy[INSTALL_TAR_PATH], *part, *next;
    uint32_t dir = 2;
    int ret;
    if (!*path) { *ino = 2; return 0; }
    for (unsigned i = 0; i < CACHE; i++) if (f->cache[i].ino && !strcmp(f->cache[i].path, path)) { *ino = f->cache[i].ino; return 0; }
    if (strlen(path) >= sizeof(copy)) return -ENAMETOOLONG;
    strcpy(copy, path); part = copy;
    while (*part) {
        next = strchr(part, '/'); if (next) *next = 0;
        ret = directory(f, dir, part, &dir, 0, 1); if (ret < 0) return ret;
        if (!next) break;
        part = next + 1;
    }
    *ino = dir; return 0;
}
static void attributes(unsigned char *ino, const InstallTarEntry *e)
{
    w16(ino, (r16(ino) & 0170000) | (e->mode & 07777)); w16(ino + 2, e->uid); w16(ino + 24, e->gid);
    w16(ino + 120, e->uid >> 16); w16(ino + 122, e->gid >> 16);
    w32(ino + 8, e->mtime); w32(ino + 12, e->mtime); w32(ino + 16, e->mtime);
}
static int makeDir(InstallExt2 *f, uint32_t ino, uint32_t parent, const InstallTarEntry *e)
{
    unsigned char node[128] = {0}, par[128], data[BS] = {0};
    uint32_t b;
    int ret;
    w16(node, 0040755); attributes(node, e); w16(node + 26, 2);
    ret = mapped(f, node, 0, 1, &b); if (ret < 0) return ret;
    dirent(data, ino, 12, "."); dirent(data + 12, parent, BS - 12, "..");
    ret = writeBlock(f, b, data); if (ret < 0) return ret;
    w32(node + 4, BS); ret = getInode(f, ino, node, 1); if (ret < 0) return ret;
    w16(f->gdt + ((ino - 1) / IPG) * 32 + 16, r16(f->gdt + ((ino - 1) / IPG) * 32 + 16) + 1);
    if (ino != parent) {
        ret = getInode(f, parent, par, 0); if (ret < 0) return ret;
        if (r16(par + 26) == 65535) return -EMLINK;
        w16(par + 26, r16(par + 26) + 1); return getInode(f, parent, par, 1);
    }
    return 0;
}
int installExt2Begin(InstallExt2 *f, const InstallTarEntry *e)
{
    char parent[INSTALL_TAR_PATH], *leaf;
    uint32_t dir, ino = 0, old = 0;
    unsigned char node[128];
    int ret, exists, possible, replace = 0;
    if (!f || !e || f->active || strlen(e->name) >= sizeof(parent)) return -EINVAL;
    if (!*e->name) {
        if (e->type != '5') return -EINVAL;
        ret = getInode(f, 2, node, 0); if (ret < 0) return ret;
        attributes(node, e); return getInode(f, 2, node, 1);
    }
    strcpy(parent, e->name); leaf = strrchr(parent, '/');
    if (leaf) { *leaf++ = 0; ret = lookup(f, parent, &dir); if (ret < 0) return ret; }
    else { leaf = parent; dir = 2; }
    possible = seen(f, e->name, 0);
    ret = possible ? directory(f, dir, leaf, &old, 0, 1) : -ENOENT;
    if (ret < 0 && ret != -ENOENT) return ret;
    exists = ret == 0;
    if (exists) {
        ret = getInode(f, old, node, 0); if (ret < 0) return ret;
        if (e->type == '5') {
            if ((r16(node) & 0170000) != 0040000) return -ENOTDIR;
            attributes(node, e); return getInode(f, old, node, 1);
        }
        if ((r16(node) & 0170000) == 0040000) return -EISDIR;
    }
    if (e->type == '1') {
        unsigned char target[128];
        ret = lookup(f, e->link, &ino); if (ret < 0) return ret;
        if (exists && old == ino) return 0;
        ret = getInode(f, ino, target, 0); if (ret < 0) return ret;
        if ((r16(target) & 0170000) == 0040000 || r16(target + 26) == 65535) return -EINVAL;
        w16(target + 26, r16(target + 26) + 1); ret = getInode(f, ino, target, 1); if (ret < 0) return ret;
        if (exists) {
            unsigned links = r16(node + 26); if (!links) return -EIO;
            w16(node + 26, --links);
            if (!links) {
                ret = truncateInode(f, node); if (ret < 0) return ret;
                memset(node, 0, 128); ret = release(f, 1, old); if (ret < 0) return ret;
            }
            ret = getInode(f, old, node, 1); if (ret < 0) return ret;
        }
    } else {
        if (exists && r16(node + 26) > 1) {
            /* A tar replacement breaks this directory entry's hard link;
             * other names must retain their original contents. */
            ret = alloc(f, 1, &ino); if (ret < 0) return ret;
            w16(node + 26, r16(node + 26) - 1);
            ret = getInode(f, old, node, 1); if (ret < 0) return ret;
            memset(node, 0, 128); w16(node + 26, 1); replace = 1;
        } else if (exists) { ino = old; ret = truncateInode(f, node); if (ret < 0) return ret; }
        else { ret = alloc(f, 1, &ino); if (ret < 0) return ret; memset(node, 0, 128); w16(node + 26, 1); }
        if (e->type == '5') {
            ret = makeDir(f, ino, dir, e); if (ret < 0) return ret;
            unsigned slot = f->nextcache++ % CACHE;
            strcpy(f->cache[slot].path, e->name); f->cache[slot].ino = ino;
        } else {
            unsigned type = e->type == '0' ? 0100000 : e->type == '2' ? 0120000 : e->type == '3' ? 0020000 : e->type == '4' ? 0060000 : e->type == '6' ? 0010000 : 0;
            if (!type) return -EINVAL;
            w16(node, type); attributes(node, e);
            if (e->type == '3' || e->type == '4') {
                if (e->major > 4095 || e->minor > 1048575) return -EINVAL;
                if (e->major < 256 && e->minor < 256) w32(node + 40, e->major << 8 | e->minor);
                else w32(node + 44, (e->minor & 255) | e->major << 8 | (e->minor & ~255u) << 12);
            }
            if (e->type == '0' || e->type == '2') {
                f->ino = ino; memcpy(f->inode, node, 128); f->position = 0;
                f->size = e->type == '0' ? e->size : strlen(e->link);
                w32(f->inode + 4, f->size); f->active = 1; memset(f->file, 0, BS);
                if (e->type == '2') {
                    if (f->size <= 60) { memcpy(f->inode + 40, e->link, f->size); f->position = f->size; }
                    else { ret = installExt2Data(f, e->link, f->size); if (ret < 0) return ret; }
                    ret = installExt2End(f); if (ret < 0) return ret;
                }
            } else { ret = getInode(f, ino, node, 1); if (ret < 0) return ret; }
        }
    }
    if (!exists || replace || e->type == '1') {
        ret = directory(f, dir, leaf, &ino, 1, possible); if (ret < 0) return ret;
    }
    seen(f, e->name, 1); return 0;
}
int installExt2Data(InstallExt2 *f, const void *data, unsigned size)
{
    const unsigned char *p = data;
    if (!f || !f->active || size > f->size - f->position) return -EINVAL;
    while (size) {
        unsigned offset = f->position % BS, n = BS - offset;
        uint32_t b; int ret;
        if (n > size) n = size;
        memcpy(f->file + offset, p, n); p += n; size -= n; f->position += n;
        if (offset + n == BS || f->position == f->size) {
            ret = mapped(f, f->inode, (f->position - 1) / BS, 1, &b); if (ret < 0) return ret;
            ret = writeBlock(f, b, f->file); if (ret < 0) return ret;
            memset(f->file, 0, BS);
        }
    }
    return 0;
}
int installExt2End(InstallExt2 *f)
{
    int ret;
    if (!f || !f->active) return 0;
    if (f->position != f->size) return -EINVAL;
    ret = getInode(f, f->ino, f->inode, 1); if (ret >= 0) f->active = 0; return ret;
}
static int superblocks(InstallExt2 *f, unsigned state)
{
    unsigned char data[BS] = {0}, super[1024] = {0};
    int ret;
    w32(super, f->groups * IPG); w32(super + 4, f->blocks); w32(super + 12, f->freeblocks); w32(super + 16, f->freeinodes);
    w32(super + 24, 2); w32(super + 28, 2); w32(super + 32, BPG); w32(super + 36, BPG); w32(super + 40, IPG);
    w32(super + 48, f->serial); w16(super + 54, 20); w16(super + 56, 0xef53); w16(super + 58, state); w16(super + 60, 1);
    w32(super + 64, f->serial); w32(super + 68, 15552000); w32(super + 76, 1); w32(super + 84, 11); w16(super + 88, 128);
    for (unsigned i = 0; i < 4; i++) w32(super + 104 + 4 * i, (f->serial + i * 2654435761u) ^ f->start);
    memcpy(super + 120, "PSBBN", 5);
    for (uint32_t g = 0; g < f->groups; g++) {
        memset(data, 0, BS); w16(super + 90, g); memcpy(data + (g ? 0 : 1024), super, sizeof(super));
        ret = writeBlock(f, g * BPG, data); if (ret < 0) return ret;
        memset(data, 0, BS); memcpy(data, f->gdt, f->groups * 32);
        ret = writeBlock(f, g * BPG + 1, data); if (ret < 0) return ret;
    }
    return 0;
}
int installExt2Finish(InstallExt2 *f)
{
    int ret;
    if (!f || f->active) return -EINVAL;
    if (f->bbdirty) { ret = writeBlock(f, f->bbgroup * BPG + 2, f->bb); if (ret < 0) return ret; f->bbdirty = 0; }
    if (f->ibdirty) { ret = writeBlock(f, f->ibgroup * BPG + 3, f->ib); if (ret < 0) return ret; f->ibdirty = 0; }
    if (f->itdirty) { ret = writeBlock(f, f->itblock, f->it); if (ret < 0) return ret; f->itdirty = 0; }
    ret = f->io.flush(f->io.context); if (ret < 0) return ret;
    ret = superblocks(f, 1); return ret < 0 ? ret : f->io.flush(f->io.context);
}
void installExt2Free(InstallExt2 *f)
{ if (f) { free(f->gdt); free(f->bloom); free(f); } }
int installExt2Create(InstallExt2 **out, const HddFormatIo *io, uint32_t start, uint32_t sectors, uint32_t serial)
{
    InstallExt2 *f;
    unsigned char data[BS];
    InstallTarEntry root;
    int ret;
    if (!out) return -EINVAL;
    *out = NULL;
    if (!io || !io->read || !io->write || !io->zero || !io->flush || sectors < 64 * 2048 || sectors > 2048u * 2048 || start > UINT32_MAX - sectors) return -EINVAL;
    f = calloc(1, sizeof(*f)); if (!f) return -ENOMEM;
    f->io = *io; f->start = start; f->blocks = sectors / 8; f->groups = (f->blocks + BPG - 1) / BPG; f->serial = serial;
    f->bbgroup = f->ibgroup = f->itblock = UINT32_MAX;
    f->gdt = calloc(f->groups, 32); f->bloom = calloc(1, BLOOM);
    if (!f->gdt || !f->bloom) { ret = -ENOMEM; goto failed; }
    for (uint32_t g = 0; g < f->groups; g++) {
        unsigned char *desc = f->gdt + g * 32;
        uint32_t count = groupSize(f, g);
        if (count <= META) { ret = -EINVAL; goto failed; }
        f->cursor[g][0] = META; f->cursor[g][1] = g ? 0 : 10;
        w32(desc, g * BPG + 2); w32(desc + 4, g * BPG + 3); w32(desc + 8, g * BPG + 4);
        w16(desc + 12, count - META); w16(desc + 14, IPG - (g ? 0 : 10));
        f->freeblocks += count - META; f->freeinodes += IPG - (g ? 0 : 10);
        ret = io->zero(io->context, start + (g * BPG) * 8, META * 8); if (ret < 0) goto failed;
        memset(data, 0, BS);
        for (unsigned i = 0; i < META; i++) data[i / 8] |= 1 << (i % 8);
        for (unsigned i = count; i < BPG; i++) data[i / 8] |= 1 << (i % 8);
        ret = writeBlock(f, g * BPG + 2, data); if (ret < 0) goto failed;
        memset(data, 255, BS); memset(data, 0, IPG / 8);
        if (!g) { data[0] = 255; data[1] = 3; }
        ret = writeBlock(f, g * BPG + 3, data); if (ret < 0) goto failed;
    }
    ret = superblocks(f, 2); if (ret < 0) goto failed;
    memset(&root, 0, sizeof(root)); root.mode = 0755; root.mtime = serial;
    ret = makeDir(f, 2, 2, &root); if (ret < 0) goto failed;
    *out = f; return 0;
failed:
    installExt2Free(f); return ret;
}
