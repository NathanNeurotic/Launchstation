#ifndef HDD_FORMAT_RPC_H
#define HDD_FORMAT_RPC_H

#include <stddef.h>

/* Private formatter service; no changes to the APA/ATA driver ABI. */
#define HDD_FORMAT_RPC 0x4c53464d
#define HDD_FORMAT_CHUNK 64
enum { HF_BEGIN = 1, HF_READ, HF_WRITE, HF_ZERO, HF_FLUSH, HF_FINISH };
typedef struct {
    int result;
    unsigned int lba, count, sectors;
    unsigned int reserved[12];
    unsigned char data[512 * HDD_FORMAT_CHUNK];
} HddFormatPacket;
typedef char HddFormatPacketHeaderSize[(offsetof(HddFormatPacket, data) == 64) ? 1 : -1];

#endif
