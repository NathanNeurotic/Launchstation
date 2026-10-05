#ifndef MMCE_BACKUP_H
#define MMCE_BACKUP_H

#include <stddef.h>
#include "mc_card_export.h"

enum {
	MMCE_BACKUP_HISTORY_UNCHANGED = 0,
	MMCE_BACKUP_HISTORY_CREATED = 1,
	MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN = 2,
	MMCE_BACKUP_HISTORY_DUPLICATE_REMOVE_WARN = 3,
	MMCE_BACKUP_ERR_NAME = -100,
	MMCE_BACKUP_ERR_STACK = -101,
	MMCE_BACKUP_ERR_STACK_INCOMPATIBLE = -102
};

#define MMCE_BACKUP_HISTORY_KEEP_DEFAULT 10

int mmceUnitFromPath(const char *path);

/* Back up the exact firmware-selected active card as a verified native .mcd.
 * History is timestamped, duplicates are suppressed, and the newest keep
 * entries are retained. */
int mmceBackupActiveHistory(int unit, const char *folder, int keep,
                            McCardExportProgress progress,
                            char *out_path, size_t out_path_size);

/* Export the exact firmware-selected active card directly to a verified
 * PCSX2 .ps2 file in folder. */
int mmceExportActivePcsx2ToFolder(int unit, const char *folder,
                                  McCardExportProgress progress,
                                  char *out_path, size_t out_path_size);

/* Create and return wLaunchELF's local automatic-backup directory on the
 * selected MMCE microSD. */
int mmceEnsureAutoBackupFolder(int unit, char *out, size_t out_size);

#endif
