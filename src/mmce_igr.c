//--------------------------------------------------------------
// File name: mmce_igr.c
// Headless automatic MMCE backup path used after OPL IGR.
//--------------------------------------------------------------
#include "launchelf.h"
#include "init.h"
#include "mmce_backup.h"
#include "mmce_control.h"
#include "mmce_igr.h"
#include "mmce_igr_scan.h"

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

		if (mmceEnsureAutoBackupFolder(unit, folder, sizeof(folder)) < 0) {
			local.failures++;
			continue;
		}

		restore = mmceControlRestoreFromBootCard(unit);
		if (restore > 0) {
			MmceIgrScanResult scan;
			local.bootcards_restored++;
			/* OPL switched through BootCard. The restored active card covers
			 * GameID/default behavior; the changed-card scan also catches the
			 * previously selected named/manual card. First scan backs up all
			 * discovered images to establish a safe complete baseline. */
			if (mmceIgrScanChangedCards(unit, folder, &scan) < 0)
				local.warnings++;
			local.backups_created += scan.backups_created;
			local.backups_unchanged += scan.backups_unchanged;
			local.warnings += scan.warnings;
			local.failures += scan.failures;
		} else if (restore < 0) {
			MmceIgrScanResult scan;
			/* Do not back up the active image here: older/failed firmware may
			 * still be presenting BootCard. Scan independent card files instead. */
			local.warnings++;
			if (mmceIgrScanChangedCards(unit, folder, &scan) < 0)
				local.warnings++;
			local.backups_created += scan.backups_created;
			local.backups_unchanged += scan.backups_unchanged;
			local.warnings += scan.warnings;
			local.failures += scan.failures;
			continue;
		}

		/* No BootCard switch, or a successful unmount: the hidden synthetic
		 * source is the exact card firmware is currently presenting. */
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
