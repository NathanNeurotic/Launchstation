//--------------------------------------------------------------
// File name: mmce_igr.c
// Headless automatic MMCE backup path used after OPL IGR.
//--------------------------------------------------------------
#include "launchelf.h"
#include "init.h"
#include "mmce_backup.h"
#include "mmce_control.h"
#include "mmce_igr.h"

int mmceIgrRequested(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i] != NULL &&
		    (!strcmp(argv[i], "-igr") || !strcmp(argv[i], "--igr-auto-backup")))
			return 1;
	}
	return 0;
}

int mmceIgrRunAutoBackup(MmceIgrResult *result)
{
	MmceIgrResult local;
	char folder[MAX_PATH], saved[MAX_PATH];
	int unit, restore, r;

	memset(&local, 0, sizeof(local));

#ifdef MMCE
	if (!loadMmceModules()) {
		local.failures++;
		if (result != NULL)
			*result = local;
		return -1;
	}

	for (unit = 0; unit < 2; unit++) {
		if (mmceControlProbe(unit) < 0)
			continue;

		local.slots_present++;
		restore = mmceControlRestoreFromBootCard(unit);
		if (restore < 0) {
			/* Older/unsupported firmware or a failed switch: never guess at
			 * the card after IGR. The changed-card fallback layer can still
			 * cover independently addressable images. */
			local.warnings++;
			continue;
		}
		if (restore > 0)
			local.bootcards_restored++;

		if (mmceEnsureAutoBackupFolder(unit, folder, sizeof(folder)) < 0) {
			local.failures++;
			continue;
		}

		saved[0] = '\0';
		r = mmceBackupActiveHistory(unit, folder,
		                            MMCE_BACKUP_HISTORY_KEEP_DEFAULT,
		                            NULL, saved, sizeof(saved));
		switch (r) {
			case MMCE_BACKUP_HISTORY_CREATED:
				local.backups_created++;
				break;
			case MMCE_BACKUP_HISTORY_UNCHANGED:
				local.backups_unchanged++;
				break;
			case MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN:
			case MMCE_BACKUP_HISTORY_DUPLICATE_REMOVE_WARN:
				local.warnings++;
				break;
			default:
				local.failures++;
				break;
		}
	}
#else
	local.failures++;
#endif

	if (result != NULL)
		*result = local;
	return local.failures ? -1 : 0;
}
