#ifndef HDD_FORMAT_H
#define HDD_FORMAT_H

#include <stdint.h>
#include "hdd_filesystem.h"

enum { HDD_FORMAT_APA, HDD_FORMAT_EXFAT, HDD_FORMAT_APA_JAIL };
#define HDD_FORMAT_RECOVERY_MB 32u
#define HDD_FORMAT_CLUSTER_BYTES 32768u

typedef struct {
    uint32_t kind, sectors, reserve_mb, serial;
    uint32_t apa_sectors, recovery_start, exfat_start, exfat_sectors;
} HddFormatPlan;

/* Every operation returns zero or a negative errno. Sector size is 512 bytes.
 * Callbacks must check errors and must not silently truncate transfers. */
typedef struct {
    void *context;
    int (*read)(void *, uint32_t, unsigned, void *);
    int (*write)(void *, uint32_t, unsigned, const void *);
    int (*zero)(void *, uint32_t, uint32_t);
    int (*flush)(void *);
    int (*apa)(void *);
    void (*progress)(void *, const char *, uint32_t, uint32_t);
} HddFormatIo;

uint32_t hddFormatMaxReserve(uint32_t sectors);
int hddFormatPlan(HddFormatPlan *plan, unsigned kind, uint32_t sectors,
                  uint32_t reserve_mb, uint32_t serial);
int hddFormatExecute(const HddFormatPlan *plan, const HddFormatIo *io);
/* Internal recovery writer, also exercised with a host filesystem checker. */
int hddFormatRecovery(const HddFormatPlan *plan, const HddFormatIo *io);
uint32_t hddFormatUpper(uint32_t value);

/* Existing driver, read-only raw I/O: determine the APA allocation boundary
 * and predict the driver's first eligible free slot / aligned append. */
int hddApaLimit(HddFsReadSector read, void *context, uint32_t sectors, uint32_t *limit);
int hddApaCanAllocate(HddFsReadSector read, void *context, uint32_t sectors, uint32_t size);
/* EE adapter. Call only with the HDD stack loaded. */
int hddCheckAllocation(uint32_t sectors);
int hddGetApaCapacity(uint32_t *sectors);
int hddFormatLoad(void);
int hddFormatDisk(const HddFormatPlan *plan, int *started);
/* Commit a downloaded MBR only after its files/configs have been installed.
 * Also refresh A2 recovery metadata after new partitions. An optional populate
 * callback runs inside the raw session before activation. NULL mbr skips boot
 * activation. The UI reloads the IOP when started is set. */
int hddInstallFinish(const HddFormatPlan *plan, const char *mbr, int *started,
                     int (*populate)(void *, const HddFormatIo *), void *context);

#endif
