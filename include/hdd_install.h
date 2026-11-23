#ifndef HDD_INSTALL_H
#define HDD_INSTALL_H

#include <stddef.h>
#include <stdint.h>
#include "hdd_format.h"
#include "hdd_install_recovery.h"

#define HDD_INSTALL_REPOSITORY "CosmicScale/PSBBN-Definitive-Project"
#define HDD_INSTALL_API "/repos/" HDD_INSTALL_REPOSITORY
#define HDD_INSTALL_BROWSER_ATTR 0xc4a7u
#define HDD_INSTALL_ASSETS 15
#define HDD_INSTALL_LANGUAGES 32
#define HDD_INSTALL_LINUX_PARTITIONS 10
#define HDD_INSTALL_HOME_MAX_MB 16384u
#define HDD_INSTALL_LAUNCHER_FILES 160

enum { HDD_GAME_OPL, HDD_GAME_NEUTRINO };
enum { HDD_INSTALL_PFS_CONTENTS, HDD_INSTALL_PFS_APP, HDD_INSTALL_PFS_CONFIG,
       HDD_INSTALL_PFS_GAME, HDD_INSTALL_PFS_COUNT };
typedef struct {
    char path[192], hash[41];
    uint32_t size;
} HddInstallFile;
typedef struct { char code[4], name[48]; } HddInstallLanguage;

enum { HDD_INSTALL_HOSD = 1, HDD_INSTALL_APPS = 2, HDD_INSTALL_PSBBN = 4, HDD_INSTALL_PS2LINUX = 8 };
enum { HI_MBR, HI_HOSD, HI_WLE, HI_R3CONFIG, HI_EXTRAS, HI_VERSION, HI_SYSTEM, HI_LANGUAGE, HI_WLE_ART, HI_R3_ART,
       HI_PS2LINUX, HI_KERNEL_NTSC, HI_KERNEL_VGA, HI_CHANNELS, HI_KERNEL_JPN };

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
    struct { uint32_t start, sectors; } linux[HDD_INSTALL_LINUX_PARTITIONS];
    unsigned char owner[20];
    unsigned owned, ready;
    unsigned char *ca_data;
    unsigned ca_size;
    unsigned music_mb, contents_mb, linux_home_mb, reserved_mb;
    unsigned game_launcher;
    HddInstallFile *launcher_files;
    unsigned launcher_count;
    HddInstallLanguage language;
    char archive_item[96], channels[128];
    uint64_t download_bytes;
    unsigned cleanup_pending;
    /* EE checkpoints survive an IOP reset. Never repeat whole-drive format. */
    unsigned recovery_attempts, stage_ready, manifest_ready, downloaded_assets, downloaded_launchers;
    unsigned applied, populated, committed;
    unsigned apa_sectors;
    unsigned resumed;
    uint32_t pfs_start[HDD_INSTALL_PFS_COUNT];
} HddInstall;

unsigned hddInstallPartitionMb(const HddInstall *install, unsigned index);
/* Minimum additional APA allocation in MiB beyond the standard system partitions,
 * including allocator alignment, apps and explicitly reserved free space. */
int hddInstallRequiredMb(const HddInstall *install, unsigned base_mb);
int dashHddInstallConfigure(HddInstall *install, unsigned max_extent_mb);

const char *hddInstallAssetPath(unsigned asset);
const char *hddInstallAssetName(unsigned asset);
int hddInstallNeedsAsset(unsigned flags, unsigned asset);
int hddInstallSelectedAsset(const HddInstall *install, unsigned asset);
/* Read upstream language choices as data; never execute downloaded scripts. */
int hddInstallLanguages(const char *script, const char *english, HddInstallLanguage *languages, unsigned capacity);
int hddInstallArchiveItem(const char *script, char item[96]);
int hddInstallLanguageChoices(HddInstall *install, HddInstallLanguage *languages, unsigned capacity);
int hddInstallHex(const char *text, unsigned length);
/* Strict, bounded JSON readers for authenticated upstream manifests. */
int hddInstallCommit(const char *json, char commit[41]);
int hddInstallAssetMetadata(const char *json, const char *name,
                             char hash[41], uint32_t *size);
int hddInstallArchiveMetadata(const char *json, const char *prefix,
                              char name[128], char hash[41], uint32_t *size);
int hddInstallLinuxMetadata(const char *json, char hash[41], uint32_t *size);
/* Bounded directory metadata; callback receives leaf names, never remote URLs. */
int hddInstallDirectory(const char *json, int (*entry)(void *, const char *, const char *, uint32_t, int), void *context);
const char *hddInstallGameName(unsigned launcher);
const char *hddInstallGameElf(unsigned launcher);
int hddInstallGameShortcut(const HddInstall *install, char *text, size_t capacity);
/* Config generation is based on installed components and the actual layout. */
int hddInstallConfig(const HddInstall *install, int mbr, char *text, size_t capacity);
extern const char *const hddInstallOsdFiles[7];

/* Native frontend/backend. No downloaded payload is embedded in the ELF. */
int dashHddInstallChoose(HddInstall *install, int jail);
int dashHddInstallPrepare(HddInstall *install);
int dashHddInstallPrepareStorage(HddInstall *install);
int hddInstallLoadCa(HddInstall *install);
void hddInstallRelease(HddInstall *install);
int hddInstallMountStage(HddInstall *install);
int hddInstallUnmountStage(HddInstall *install);
int hddInstallCreateStage(HddInstall *install);
int hddInstallResolve(HddInstall *install);
int hddInstallDownload(HddInstall *install);
int hddInstallWriteConfigs(HddInstall *install);
int hddInstallApply(HddInstall *install);
/* Call only after successful software/MBR commit. Preserves unrelated console
 * configuration and verifies the persistent HDD boot setting; HOSD only. */
int hddInstallEnableBoot(HddInstall *install);
int hddInstallPsbbnPartitions(HddInstall *install);
int hddInstallPsbbnPayload(void *install, const HddFormatIo *io);
int hddInstallClaimStage(HddInstall *install);
int hddInstallFindStage(HddInstall *install);
int hddInstallPfsPartition(HddInstall *install, unsigned mb, const char *name, unsigned slot);
int hddInstallCleanup(HddInstall *install);

/* Installer progress/log: one serialized writer, UI snapshots, no
 * file I/O while the log lock is held. Times are elapsed milliseconds. */
enum {
    HDD_INSTALL_PHASE_PREPARE, HDD_INSTALL_PHASE_FORMAT, HDD_INSTALL_PHASE_DOWNLOAD,
    HDD_INSTALL_PHASE_CHECK, HDD_INSTALL_PHASE_INSTALL, HDD_INSTALL_PHASE_FINISH,
    HDD_INSTALL_PHASE_COMPLETE
};
#define HDD_INSTALL_LOG_ROWS 256
#define HDD_INSTALL_LOG_COLS 80
typedef struct {
    unsigned first, next;
    uint64_t elapsed, idle;
    char status[192], path[1024], io[112];
    int done, total, disk_error;
    unsigned overall; /* 0..1000, weighted work estimate; 1000 only on success. */
    char phase[48];
} HddInstallLogSnapshot;
void hddInstallLogStart(void);
void hddInstallLogStop(void);
int hddInstallLogEnabled(void);
void hddInstallLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
void hddInstallStatus(const char *text, int done, int total);
/* Update live progress without adding a history entry. */
void hddInstallProgress(const char *text, int done, int total);
void hddInstallPhase(unsigned phase);
void hddInstallOverall(uint64_t done, uint64_t total);
void hddInstallRawTrace(int command, uint32_t lba, unsigned count, int pending, int result);
void hddInstallLogSnapshot(HddInstallLogSnapshot *snapshot);
int hddInstallLogRead(unsigned index, char line[HDD_INSTALL_LOG_COLS]);
void hddInstallLogFlush(HddInstall *install);
void dashHddInstallDebugBegin(HddInstall *install, const HddFormatPlan *plan);
void dashHddInstallDebugEnd(HddInstall *install, int result);
int dashHddInstallAwait(int id, const char *title);
void dashHddInstallRecoveryFailed(void) __attribute__((noreturn));

#endif
