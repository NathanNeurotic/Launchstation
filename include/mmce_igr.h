#ifndef MMCE_IGR_H
#define MMCE_IGR_H

typedef struct {
	int slots_present;
	int bootcards_restored;
	int backups_created;
	int backups_unchanged;
	int warnings;
	int failures;
} MmceIgrResult;

int mmceIgrRequested(int argc, char *argv[]);
int mmceIgrRunAutoBackup(MmceIgrResult *result);

#endif
