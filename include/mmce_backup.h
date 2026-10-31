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
    /* 0: a card whose filesystem-index fingerprint matches the one stored
     *    beside its newest history copy (<copy>.FP) is unchanged, without
     *    reading the whole card. 1: always compare every byte, as before.
     *    Fingerprints are still written either way. */
    int verify;
    /* 1: after a card's history is current, keep each changed save on its
     *    newest copy as a .psu version in <card history>/WLE-Saves/<save>/
     *    (mmce_save_versions.h), keep versions per save. 0: cards only. */
    int per_save;
} MmceBackupOptions;

typedef struct {
    int candidates;
    int backed_up;
    int unchanged;
    int failed;
    int save_versions; /* per_save: new .psu versions written */
    int save_failed;   /* per_save: saves (or cards) whose version failed */
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

/* Filesystem-index fingerprint (mcViewFingerprint) of one card image, .mcd or
 * .ps2, read through the one file only. 0 on success, -1 if it cannot be read
 * or is not a PS2 card. Works on mmceN:/__active_card__.mcd too. */
int mmceCardFingerprint(const char *path, unsigned char out[32]);

#endif
