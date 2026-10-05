#ifndef MMCE_IGR_SCAN_H
#define MMCE_IGR_SCAN_H

typedef struct {
	int cards_seen;
	int cards_changed;
	int backups_created;
	int backups_unchanged;
	int warnings;
	int failures;
} MmceIgrScanResult;

/* Scan all sd2psXtd PS2 card roots for metadata changes since the last
 * successful IGR scan and keep verified per-card histories. On the first
 * run every discovered card is treated as changed, establishing a safe
 * complete baseline. */
int mmceIgrScanChangedCards(int unit, const char *backup_root,
                            MmceIgrScanResult *result);

#endif
