#include "install_ext2.h"
#include <errno.h>
#include <string.h>
static void put16(unsigned char *p, uint32_t n) { p[0] = n; p[1] = n >> 8; }
static void put32(unsigned char *p, uint32_t n) { put16(p, n); put16(p + 2, n >> 16); }
int installPsbbnSwap(const HddFormatIo *io, uint32_t start, uint32_t sectors)
{
    unsigned char data[4096] = {0};
    if (!io || !io->write || sectors < 16 || start > UINT32_MAX - sectors) return -EINVAL;
    put32(data + 1024, 1); put32(data + 1028, sectors / 8 - 1);
    memcpy(data + 4086, "SWAPSPACE2", 10);
    return io->write(io->context, start, 8, data);
}
static void entry(unsigned char *p, const char *name, unsigned attr, unsigned cluster)
{
    memcpy(p, name, 11); p[11] = attr; p[12] = 8; /* lowercase SFN base */
    put16(p + 16, 33); put16(p + 18, 33); put16(p + 24, 33); put16(p + 26, cluster);
}
int installPsbbnMusic(const HddFormatIo *io, uint32_t start, uint32_t sectors, uint32_t serial)
{
    unsigned char data[4096] = {0};
    uint32_t fats = 1, clusters, next, first;
    int ret;
    if (!io || !io->write || !io->zero || sectors < 512u * 2048 || start > UINT32_MAX - sectors) return -EINVAL;
    for (unsigned i = 0; i < 16; i++) {
        clusters = (sectors - 32 - 2 * fats) / 8;
        next = ((clusters + 2) * 4 + 511) / 512;
        if (next == fats) break;
        if (next < fats) break; /* current value safely covers the FAT */
        fats = next;
    }
    clusters = (sectors - 32 - 2 * fats) / 8; first = 32 + 2 * fats;
    if (clusters < 65525 || clusters > 0x0ffffff5 || fats < ((clusters + 2) * 4 + 511) / 512) return -EINVAL;
    ret = io->zero(io->context, start, first + 3 * 8); if (ret < 0) return ret;
    data[0] = 0xeb; data[1] = 0x58; data[2] = 0x90; memcpy(data + 3, "MSWIN4.1", 8);
    put16(data + 11, 512); data[13] = 8; put16(data + 14, 32); data[16] = 2;
    data[21] = 0xf8; put16(data + 24, 63); put16(data + 26, 255); put32(data + 32, sectors);
    put32(data + 36, fats); put32(data + 44, 2); put16(data + 48, 1); put16(data + 50, 6);
    data[64] = 0x80; data[66] = 0x29; put32(data + 67, serial); memcpy(data + 71, "PSBBNMUSIC ", 11); memcpy(data + 82, "FAT32   ", 8);
    data[510] = 0x55; data[511] = 0xaa;
    ret = io->write(io->context, start, 1, data); if (ret < 0) return ret;
    ret = io->write(io->context, start + 6, 1, data); if (ret < 0) return ret;
    memset(data, 0, 512); put32(data, 0x41615252); put32(data + 484, 0x61417272);
    put32(data + 488, clusters - 3); put32(data + 492, 5); put32(data + 508, 0xaa550000);
    ret = io->write(io->context, start + 1, 1, data); if (ret < 0) return ret;
    ret = io->write(io->context, start + 7, 1, data); if (ret < 0) return ret;
    memset(data, 0, 512); put32(data, 0x0ffffff8); put32(data + 4, 0xffffffff);
    for (unsigned i = 2; i < 5; i++) put32(data + i * 4, 0x0fffffff);
    ret = io->write(io->context, start + 32, 1, data); if (ret < 0) return ret;
    ret = io->write(io->context, start + 32 + fats, 1, data); if (ret < 0) return ret;
    memset(data, 0, sizeof(data)); entry(data, "PSBBNMUSIC ", 8, 0); entry(data + 32, "MUSICCH    ", 16, 3);
    ret = io->write(io->context, start + first, 8, data); if (ret < 0) return ret;
    memset(data, 0, sizeof(data)); entry(data, ".          ", 16, 3); entry(data + 32, "..         ", 16, 0); entry(data + 64, "CONTENTS   ", 16, 4);
    ret = io->write(io->context, start + first + 8, 8, data); if (ret < 0) return ret;
    memset(data, 0, sizeof(data)); entry(data, ".          ", 16, 4); entry(data + 32, "..         ", 16, 3);
    return io->write(io->context, start + first + 16, 8, data);
}
