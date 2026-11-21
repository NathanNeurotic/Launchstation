#ifndef HDD_INSTALL_H
#define HDD_INSTALL_H

#include <stddef.h>
#include <stdint.h>
#include "hdd_format.h"

#define HDD_INSTALL_REPOSITORY "CosmicScale/PSBBN-Definitive-Project"
#define HDD_INSTALL_API "/repos/" HDD_INSTALL_REPOSITORY
#define HDD_INSTALL_BROWSER_ATTR 0xc4a7u
#define HDD_INSTALL_ASSETS 10

enum { HDD_INSTALL_HOSD = 1, HDD_INSTALL_APPS = 2, HDD_INSTALL_PSBBN = 4 };
enum { HI_MBR, HI_HOSD, HI_WLE, HI_R3CONFIG, HI_EXTRAS, HI_VERSION, HI_SYSTEM, HI_LANGUAGE, HI_WLE_ART, HI_R3_ART };

typedef struct {
    unsigned flags;
    int jail;
    char commit[41];
    char stage[1024];
    char ca_path[1024];
    char error[256];
    uint32_t sizes[HDD_INSTALL_ASSETS];
    char hashes[HDD_INSTALL_ASSETS][41];
    char archives[2][128];
    struct { uint32_t start, sectors; } linux[8];
    unsigned char owner[20];
    unsigned owned, ready;
} HddInstall;

const char *hddInstallAssetPath(unsigned asset);
const char *hddInstallAssetName(unsigned asset);
int hddInstallNeedsAsset(unsigned flags, unsigned asset);
int hddInstallHex(const char *text, unsigned length);
/* Strict, bounded JSON readers for authenticated upstream manifests. */
int hddInstallCommit(const char *json, char commit[41]);
int hddInstallAssetMetadata(const char *json, const char *name,
                             char hash[41], uint32_t *size);
int hddInstallArchiveMetadata(const char *json, const char *prefix,
                              char name[128], char hash[41], uint32_t *size);
/* Config generation is based on installed components and the actual layout. */
int hddInstallConfig(const HddInstall *install, int mbr, char *text, size_t capacity);
extern const char *const hddInstallOsdFiles[7];

/* Native frontend/backend. No downloaded payload is embedded in the ELF. */
int dashHddInstallChoose(HddInstall *install, int jail);
int dashHddInstallPrepare(HddInstall *install);
int hddInstallDownload(HddInstall *install);
int hddInstallWriteConfigs(HddInstall *install);
int hddInstallApply(HddInstall *install);
int hddInstallPsbbnPreflight(HddInstall *install);
int hddInstallPsbbnPartitions(HddInstall *install);
int hddInstallPsbbnPayload(void *install, const HddFormatIo *io);
int hddInstallClaimStage(HddInstall *install);
int hddInstallFindStage(HddInstall *install);
void hddInstallCleanup(HddInstall *install);

#endif
