#ifndef HDD_INSTALL_RESUME_H
#define HDD_INSTALL_RESUME_H
#include "hdd_install.h"

/* Versioned, bounded little-endian journal. No pointers or native ABI data. */
#define HDD_INSTALL_RESUME_MAX 65536u
int hddInstallResumeEncode(const HddInstall *in, const HddFormatPlan *plan,
                          unsigned sequence, void *bytes, unsigned capacity);
int hddInstallResumeDecode(HddInstall *in, HddFormatPlan *plan, unsigned *sequence,
                          const void *bytes, unsigned size);
/* One serialized installer session; begin only after user confirmation. */
void hddInstallResumeBegin(HddInstall *in, const HddFormatPlan *plan, unsigned sequence);
void hddInstallResumeEnd(void);
int hddInstallResumeSave(HddInstall *in);
int hddInstallResumeLoad(HddInstall *in, HddFormatPlan *plan, unsigned *sequence);
/* Check resume eligibility using the caller's retained mount or ata0:. */
int hddInstallResumeAvailable(HddInstall *in, const char *device, int jail);
int hddInstallResumeRemove(HddInstall *in);
/* Each stage owner gets private APA names, with a separate name per PFS slot. */
static inline void hddInstallPendingPfsName(const HddInstall *in, unsigned slot, char name[32])
{
    static const char hex[] = "0123456789abcdef";
    name[0] = '+';
    name[1] = 'L';
    name[2] = 'S';
    name[3] = 'I';
    name[4] = 'N';
    name[5] = hex[slot];
    for (unsigned i = 0; i < 12; i++) {
        name[6 + i * 2] = hex[in->owner[i] >> 4];
        name[7 + i * 2] = hex[in->owner[i] & 15];
    }
    name[30] = 0;
}
#endif
