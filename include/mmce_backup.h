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

typedef struct {
    int candidates;
    int pending;
    int unchanged;
    int failed;
} MmceBackupScanStats;

typedef void (*MmceBackupProgress)(const char *source, int done, int total);

int mmceBackupAllCards(int unit, const char *dest_root, const MmceBackupOptions *options,
                       MmceBackupStats *stats, MmceBackupProgress progress);

/* Back up one specific MMCE card to externally stored, verified history.
 * protect_history may refer to an existing snapshot for this same card;
 * it and the newly verified snapshot are exempt from retention pruning.
 * Does not write to, activate or replace any emulated MMCE image. */
int mmceBackupCardPath(int unit, const char *source, const char *dest_root,
                       const MmceBackupOptions *options,
                       const char *protect_history, MmceBackupStats *stats);

/* Read-only parity scan used by the Settings status row. It never creates,
 * converts, prunes or removes backup files. */
int mmceBackupScanCards(int unit, const char *dest_root, const MmceBackupOptions *options,
                        MmceBackupScanStats *stats, MmceBackupProgress progress);

#endif
