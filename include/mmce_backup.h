#ifndef MMCE_BACKUP_H
#define MMCE_BACKUP_H

enum {
    MMCE_BACKUP_FORMAT_MCD = 0,
    MMCE_BACKUP_FORMAT_PCSX2,
    MMCE_BACKUP_FORMAT_COUNT
};

typedef struct {
    int keep;   /* 0 = keep all, otherwise 1..99 */
    int format; /* MMCE_BACKUP_FORMAT_* */
} MmceBackupOptions;

typedef struct {
    int candidates;
    int backed_up;
    int unchanged;
    int failed;
} MmceBackupStats;

typedef void (*MmceBackupProgress)(const char *source, int done, int total);

int mmceBackupAllCards(int unit, const char *dest_root, const MmceBackupOptions *options,
                       MmceBackupStats *stats, MmceBackupProgress progress);

#endif
