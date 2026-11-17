#ifndef HDD_FILESYSTEM_H
#define HDD_FILESYSTEM_H

#include <stdint.h>

enum {
    HDD_FS_UNKNOWN = 0,
    HDD_FS_APA = 1,
    HDD_FS_EXFAT = 2,
    HDD_FS_FAT32 = 4
};

/* Read exactly one physical 512-byte sector; return 0 or a negative error.
 * No mounts or writes are needed. APA is identified by the HDD driver. */
typedef int (*HddFsReadSector)(void *context, uint32_t lba, void *sector);
int hddProbeFilesystem(HddFsReadSector read_sector, void *context,
                       uint32_t sectors, unsigned *format);
const char *hddFilesystemName(unsigned format);

#endif
