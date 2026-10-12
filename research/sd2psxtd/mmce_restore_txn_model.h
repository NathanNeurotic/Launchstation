/* Reference model for a FUTURE sd2psXtd firmware restore transaction.
 * NOT included in production builds. NOT a firmware protocol implementation.
 * Source-of-truth firmware APIs do not expose the required lease/journal.
 */
#ifndef MMCE_RESTORE_TXN_MODEL_H
#define MMCE_RESTORE_TXN_MODEL_H
#include <stddef.h>
#include <stdint.h>

#define MMCE_TXN_ID_MAX 192
#define MMCE_TXN_DIGEST_SIZE 32
/* Explicit build-time feature gate. The production firmware lacks this. */
#define MMCE_TXN_IMPLEMENTED_ON_DEVICE 0

typedef enum {
    MMCE_TXN_IDLE = 0,
    MMCE_TXN_HELD,
    MMCE_TXN_PREPARED,
    MMCE_TXN_RECOVERY_REQUIRED
} mmce_txn_state;

typedef enum {
    MMCE_JOURNAL_PREPARED = 1,
    MMCE_JOURNAL_COMMITTING,
    MMCE_JOURNAL_FINISHED,
    MMCE_JOURNAL_ABORTED
} mmce_journal_phase;

typedef enum {
    MMCE_TXN_OK = 0,
    MMCE_TXN_INVALID = -1,
    MMCE_TXN_UNSUPPORTED = -2,
    MMCE_TXN_IDENTITY_CHANGED = -3,
    MMCE_TXN_IO_FAILURE = -4,
    MMCE_TXN_NEEDS_RECOVERY = -5
} mmce_txn_result;

/* The journal must be durable BEFORE the first target mutation. This struct
 * is conceptual: actual serialized records need magic, version, length, CRC,
 * generation, and redundant storage with power-loss recovery. Do not memcpy
 * this compiler-dependent structure directly to SD. */
typedef struct {
    uint8_t unit;
    uint64_t generation;
    uint64_t nonce;
    char identity[MMCE_TXN_ID_MAX];
    uint8_t image_sha256[MMCE_TXN_DIGEST_SIZE];
} mmce_txn_record;

/* All callbacks must run under the SAME exclusive firmware lease.
 * 'acquire': blocks emulated reads/writes AND every card switching source
 * including game ID, buttons and MMCEMAN; failure means nothing was locked.
 * 'drain': flushes PSRAM dirty map, filesystem, and backing fd, and waits
 * for all asynchronous card writes.
 * 'identify': resolves the exact canonical backing object and generation.
 * 'external_backup_verified': confirms a durable, verified copy OUTSIDE MMCE
 * of this exact identity/generation, not merely any .mcd backup.
 * 'stage_verified': validates a separate completed image and its SHA-256.
 * 'journal': persists and flushes a recoverable phase record; must detect
 * torn records and publish a sequence newer than the previous version.
 * 'commit_recoverably': replaces only using independently proven durable
 * rollback/replay semantics; plain sd_rename() does NOT satisfy this.
 * 'recover_before_expose': runs on firmware boot while PS2 card emulation
 * remains disabled, before ANY emulated card is opened.
 * 'release': lifts the lease only after the recorded transaction is stable.
 * This model does not implement these device-specific guarantees.
 */
typedef struct {
    int (*acquire)(void *ctx, unsigned unit);
    int (*drain)(void *ctx, unsigned unit);
    int (*identify)(void *ctx, unsigned unit, char *id, size_t id_size,
                    uint64_t *generation);
    int (*external_backup_verified)(void *ctx, const mmce_txn_record *r);
    int (*stage_verified)(void *ctx, const mmce_txn_record *r);
    int (*journal)(void *ctx, mmce_journal_phase phase, const mmce_txn_record *r);
    int (*commit_recoverably)(void *ctx, const mmce_txn_record *r);
    int (*verify_committed)(void *ctx, const mmce_txn_record *r);
    int (*recover_before_expose)(void *ctx, mmce_journal_phase phase,
                                 const mmce_txn_record *r);
    void (*release)(void *ctx, unsigned unit);
} mmce_txn_ops;

typedef struct {
    mmce_txn_state state;
    mmce_txn_record record;
    const mmce_txn_ops *ops;
    void *context;
} mmce_txn;

void mmce_txn_init(mmce_txn *t);
int mmce_txn_begin(mmce_txn *t, const mmce_txn_ops *ops, void *ctx,
                   unsigned unit, const char *expected_identity,
                   uint64_t expected_generation, uint64_t nonce);
int mmce_txn_prepare(mmce_txn *t, uint64_t nonce,
                     const uint8_t sha256[MMCE_TXN_DIGEST_SIZE]);
int mmce_txn_commit(mmce_txn *t, uint64_t nonce);
int mmce_txn_abort(mmce_txn *t, uint64_t nonce);
/* Cold-boot replay: caller must first read and authenticate a journal, and
 * must prevent card emulation from starting until this returns success. */
int mmce_txn_recover_on_boot(mmce_txn *t, const mmce_txn_ops *ops, void *ctx,
                             mmce_journal_phase phase,
                             const mmce_txn_record *durable_record);

#endif
