#ifndef HDD_APP_H
#define HDD_APP_H

#include "save_archive.h"

typedef struct {
    char folder[33], title[65], boot[33], asset[48], partition[33];
    unsigned elf[SAVE_ARCHIVE_FILES], elf_count;
} HddApp;

/* PSU/title.cfg are data. APPINFO.PBT and memory-card icon metadata are not
 * interpreted. A positive result asks the UI to choose an ELF. */
int hddAppInspect(const SaveArchive *save, HddApp *app);
int hddAppChooseBoot(const SaveArchive *save, HddApp *app, unsigned choice);
int hddAppLeaf(const char *name);
/* Extract DP's literal HDD icon template without evaluating shell code. */
int hddAppIconSettings(const char *script, const HddApp *app, char out[1024]);
int hddAppInfo(const HddApp *app, char out[1024]);
int hddAppSystemCnf(const HddApp *app, char out[512]);
#define HDD_APP_JOURNAL_SIZE 128
enum { HA_STAGED, HA_FILES, HA_CREATE, HA_PARTITION, HA_MOVE_OLD, HA_PUBLISH, HA_HEADER, HA_RENAME, HA_DONE };
typedef struct {
    uint32_t sequence, phase, update, package_crc, package_size;
    char boot[33];
} HddAppJournal;
int hddAppJournalEncode(const HddAppJournal *journal, unsigned char bytes[HDD_APP_JOURNAL_SIZE]);
int hddAppJournalDecode(HddAppJournal *journal, const unsigned char bytes[HDD_APP_JOURNAL_SIZE]);
void dashHddInstallApp(const char *psu_path);

#endif
