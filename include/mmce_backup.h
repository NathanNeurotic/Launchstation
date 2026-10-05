#ifndef MMCE_BACKUP_H
#define MMCE_BACKUP_H

typedef struct {
    int candidates;
    int backed_up;
    int unchanged;
    int failed;
} MmceBackupStats;

typedef void (*MmceBackupProgress)(const char *source, int done, int total);

int mmceBackupAllCards(int unit, const char *dest_root, MmceBackupStats *stats,
                       MmceBackupProgress progress);

#endif
