/* Minimal, single-group 32 MiB ext2 recovery volume. No ext2 library or
 * filesystem driver is added to the resident application. */
#include "hdd_format.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BLOCK 4096u
#define BLOCKS 8192u
#define DATA_FIRST 16u
#define BACKUP_BLOCK 8180u
#define NOTE_BLOCK 8185u
#define MAX_GZIP_BLOCKS (12u + 1024u)

static const char recovery_note[] =
    "Launchstation APA Jail A2 recovery\n"
    "apa_index.gz: gzip backup of the first 128 MiB, including the APA root header.\n"
    "gpt_2nd.bin: raw backup of the final 33 sectors after removal of stale GPT metadata.\n"
    "These are creation-time snapshots, not backups of later partition changes.\n"
    "Unlike PSBBN DP, Launchstation uses gzip rather than xz for apa_index.\n"
    "Decompress with gzip -dk apa_index.gz before using raw disk recovery tools.\n"
    "Restoring these files overwrites disk metadata; make a full disk image first.\n";

static void p16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void p32(unsigned char *p, uint32_t v) { p16(p, v); p16(p + 2, v >> 16); }

static int writeBlock(const HddFormatPlan *p, const HddFormatIo *io, uint32_t block, const void *data)
{
    if (block >= BLOCKS) return -EINVAL;
    return io->write(io->context, p->recovery_start + block * 8, 8, data);
}

typedef struct {
    const HddFormatPlan *plan;
    const HddFormatIo *io;
    unsigned char *block;
    uint32_t length, blocks, fill;
} GzipFile;

static int append(GzipFile *f, const unsigned char *data, unsigned size)
{
    int ret;
    while (size) {
        unsigned n = BLOCK - f->fill;
        if (n > size) n = size;
        if (f->blocks >= MAX_GZIP_BLOCKS) return -ENOSPC;
        memcpy(f->block + f->fill, data, n);
        f->fill += n; f->length += n; size -= n; data += n;
        if (f->fill == BLOCK) {
            ret = writeBlock(f->plan, f->io, DATA_FIRST + f->blocks, f->block);
            if (ret < 0) return ret;
            f->blocks++; f->fill = 0;
        }
    }
    return 0;
}

static int backupIndex(GzipFile *f, unsigned char *input, unsigned char *output)
{
    z_stream stream;
    uint32_t lba;
    int zret, ret = 0;
    memset(&stream, 0, sizeof(stream));
    zret = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (zret != Z_OK) return -ENOMEM;
    for (lba = 0; lba < 128u * 2048; lba += 64) {
        if (f->io->progress && !(lba % 2048))
            f->io->progress(f->io->context, "Backing up APA recovery metadata", lba, 128u * 2048);
        ret = f->io->read(f->io->context, lba, 64, input);
        if (ret < 0) break;
        stream.next_in = input; stream.avail_in = 32768;
        do {
            stream.next_out = output; stream.avail_out = BLOCK;
            zret = deflate(&stream, Z_NO_FLUSH);
            if (zret != Z_OK) { ret = -EIO; break; }
            ret = append(f, output, BLOCK - stream.avail_out);
            if (ret < 0) break;
        } while (stream.avail_in || !stream.avail_out);
        if (ret < 0) break;
    }
    if (ret >= 0) do {
        stream.next_out = output; stream.avail_out = BLOCK;
        zret = deflate(&stream, Z_FINISH);
        if (zret != Z_OK && zret != Z_STREAM_END) { ret = -EIO; break; }
        ret = append(f, output, BLOCK - stream.avail_out);
    } while (ret >= 0 && zret != Z_STREAM_END);
    deflateEnd(&stream);
    if (ret >= 0 && f->fill) {
        memset(f->block + f->fill, 0, BLOCK - f->fill);
        ret = writeBlock(f->plan, f->io, DATA_FIRST + f->blocks, f->block);
        if (ret >= 0) f->blocks++;
    }
    return ret;
}

static void inode(unsigned char *table, unsigned number, unsigned mode, uint32_t size,
                   unsigned links, uint32_t time, unsigned first, unsigned count)
{
    unsigned i;
    unsigned char *p = table + (number - 1) * 128;
    p16(p, mode); p32(p + 4, size);
    p32(p + 8, time); p32(p + 12, time); p32(p + 16, time);
    p16(p + 26, links); p32(p + 28, (count + (count > 12)) * 8);
    for (i = 0; i < count && i < 12; i++) p32(p + 40 + i * 4, first + i);
    if (count > 12) p32(p + 88, 14);
}

static unsigned dirEntry(unsigned char *b, unsigned pos, unsigned ino, const char *name, unsigned type, int last)
{
    unsigned n = strlen(name), length = (8 + n + 3) & ~3u;
    p32(b + pos, ino); p16(b + pos + 4, last ? BLOCK - pos : length);
    b[pos + 6] = n; b[pos + 7] = type; memcpy(b + pos + 8, name, n);
    return pos + length;
}

int hddFormatRecovery(const HddFormatPlan *p, const HddFormatIo *io)
{
    unsigned char *memory = calloc(1, 32768 + BLOCK * 2), *input, *out, *block, *table;
    GzipFile file;
    unsigned i, used, free_blocks, pos;
    int ret;
    if (!memory) return -ENOMEM;
    input = memory; out = memory + 32768; block = out + BLOCK;
    memset(&file, 0, sizeof(file));
    file.plan = p; file.io = io; file.block = block;
    /* Start with invalid superblocks. Only publish a clean filesystem after
     * every recovery file and metadata block has been written. */
    ret = io->zero(io->context, p->recovery_start, 16 * 8);
    if (ret < 0) goto done;
    ret = backupIndex(&file, input, out);
    if (ret < 0) goto done;
    if (file.blocks > 12) {
        memset(block, 0, BLOCK);
        for (i = 12; i < file.blocks; i++) p32(block + (i - 12) * 4, DATA_FIRST + i);
        ret = writeBlock(p, io, 14, block);
        if (ret < 0) goto done;
    }
    memset(input, 0, 5 * BLOCK);
    ret = io->read(io->context, p->sectors - 33, 33, input);
    if (ret < 0) goto done;
    ret = io->write(io->context, p->recovery_start + BACKUP_BLOCK * 8, 40, input);
    if (ret < 0) goto done;
    memset(block, 0, BLOCK); memcpy(block, recovery_note, sizeof(recovery_note) - 1);
    ret = writeBlock(p, io, NOTE_BLOCK, block);
    if (ret < 0) goto done;

    /* Inode table: reserved inodes 1..10, lost+found=11, backups=12/13, note=14. */
    memset(input, 0, 32768);
    inode(input, 2, 0040755, BLOCK, 3, p->serial, 12, 1);
    inode(input, 11, 0040755, BLOCK, 2, p->serial, 13, 1);
    inode(input, 12, 0100644, file.length, 1, p->serial, DATA_FIRST, file.blocks);
    inode(input, 13, 0100644, 33 * 512, 1, p->serial, BACKUP_BLOCK, 5);
    inode(input, 14, 0100644, sizeof(recovery_note) - 1, 1, p->serial, NOTE_BLOCK, 1);
    ret = io->write(io->context, p->recovery_start + 4 * 8, 64, input);
    if (ret < 0) goto done;
    memset(block, 0, BLOCK);
    pos = dirEntry(block, 0, 2, ".", 2, 0);
    pos = dirEntry(block, pos, 2, "..", 2, 0);
    pos = dirEntry(block, pos, 11, "lost+found", 2, 0);
    pos = dirEntry(block, pos, 12, "apa_index.gz", 1, 0);
    pos = dirEntry(block, pos, 13, "gpt_2nd.bin", 1, 0);
    dirEntry(block, pos, 14, "README.txt", 1, 1);
    ret = writeBlock(p, io, 12, block);
    if (ret < 0) goto done;
    memset(block, 0, BLOCK);
    pos = dirEntry(block, 0, 11, ".", 2, 0);
    dirEntry(block, pos, 2, "..", 2, 1);
    ret = writeBlock(p, io, 13, block);
    if (ret < 0) goto done;

    memset(block, 0xff, BLOCK);
    memset(block, 0, BLOCKS / 8);
    used = 0;
    for (i = 0; i < BLOCKS; i++) {
        int allocated = i <= 13 || (i == 14 && file.blocks > 12) ||
            (i >= DATA_FIRST && i < DATA_FIRST + file.blocks) ||
            (i >= BACKUP_BLOCK && i <= NOTE_BLOCK);
        if (allocated) { block[i / 8] |= 1u << (i % 8); used++; }
    }
    free_blocks = BLOCKS - used;
    ret = writeBlock(p, io, 2, block);
    if (ret < 0) goto done;
    memset(block, 0xff, BLOCK); memset(block, 0, 256 / 8);
    for (i = 0; i < 14; i++) block[i / 8] |= 1u << (i % 8);
    ret = writeBlock(p, io, 3, block);
    if (ret < 0) goto done;
    memset(block, 0, BLOCK);
    p32(block, 2); p32(block + 4, 3); p32(block + 8, 4);
    p16(block + 12, free_blocks); p16(block + 14, 256 - 14); p16(block + 16, 2);
    ret = writeBlock(p, io, 1, block);
    if (ret < 0) goto done;
    memset(block, 0, BLOCK); table = block + 1024;
    p32(table, 256); p32(table + 4, BLOCKS); p32(table + 12, free_blocks); p32(table + 16, 256 - 14);
    p32(table + 24, 2); p32(table + 28, 2); /* 4 KiB blocks/fragments */
    p32(table + 32, 32768); p32(table + 36, 32768); p32(table + 40, 256);
    p32(table + 48, p->serial); p16(table + 52, 0); p16(table + 54, 0xffff);
    p16(table + 56, 0xef53); p16(table + 58, 1); p16(table + 60, 1);
    p32(table + 64, p->serial); p32(table + 72, 0); p32(table + 76, 1);
    p32(table + 84, 11); p16(table + 88, 128); p32(table + 96, 2); /* FILETYPE */
    p32(table + 104, p->serial); p32(table + 108, p->sectors);
    p32(table + 112, p->apa_sectors); p32(table + 116, 0x4c534a32);
    table[110] = (table[110] & 15) | 0x40; table[112] = (table[112] & 63) | 0x80;
    memcpy(table + 120, "RECOVERY", 8);
    ret = writeBlock(p, io, 0, block);
done:
    free(memory);
    return ret;
}
