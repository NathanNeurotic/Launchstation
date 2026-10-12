/* Safety model only: DO NOT link into a production PS2 firmware/ELF.
 * Firmware implementation requires an actual exclusive emulation fence,
 * durable authenticated journal and power-cut verified SD replacement.
 */
#include "mmce_restore_txn_model.h"
#include <string.h>

static int ops_complete(const mmce_txn_ops *o)
{
    return o && o->acquire && o->drain && o->identify &&
           o->external_backup_verified && o->stage_verified &&
           o->journal && o->commit_recoverably && o->verify_committed &&
           o->recover_before_expose && o->release;
}

static int valid_identity(const char *id)
{
    size_t n;
    if (!id || !id[0])
        return 0;
    for (n = 0; n < MMCE_TXN_ID_MAX; ++n)
        if (id[n] == '\0')
            return 1;
    return 0;
}

static void clear_transaction(mmce_txn *t)
{
    memset(&t->record, 0, sizeof(t->record));
    t->ops = NULL;
    t->context = NULL;
    t->state = MMCE_TXN_IDLE;
}

void mmce_txn_init(mmce_txn *t)
{
    if (t)
        memset(t, 0, sizeof(*t));
}

static int unchanged_under_fence(mmce_txn *t)
{
    char current[MMCE_TXN_ID_MAX];
    uint64_t generation = 0;
    if (t->ops->identify(t->context, t->record.unit, current,
                         sizeof(current), &generation) != 0 ||
        !valid_identity(current) ||
        strcmp(current, t->record.identity) != 0 ||
        generation != t->record.generation)
        return 0;
    return 1;
}

/* This function models the proposed firmware feature but does not advertise
 * availability through the existing MMCEMAN protocol. No existing hardware
 * implements the callbacks as specified by this module. */
int mmce_txn_begin(mmce_txn *t, const mmce_txn_ops *ops, void *ctx,
                   unsigned unit, const char *expected_identity,
                   uint64_t expected_generation, uint64_t nonce)
{
    char actual[MMCE_TXN_ID_MAX];
    uint64_t actual_generation = 0;
    size_t n;
    if (!t || t->state != MMCE_TXN_IDLE || !ops_complete(ops) || unit > 1 ||
        !valid_identity(expected_identity) || !expected_generation || !nonce)
        return MMCE_TXN_INVALID;
    if (ops->acquire(ctx, unit) != 0)
        return MMCE_TXN_UNSUPPORTED;
    if (ops->drain(ctx, unit) != 0 ||
        ops->identify(ctx, unit, actual, sizeof(actual), &actual_generation) != 0 ||
        !valid_identity(actual) ||
        strcmp(actual, expected_identity) ||
        actual_generation != expected_generation) {
        ops->release(ctx, unit);
        return MMCE_TXN_IDENTITY_CHANGED;
    }
    memset(&t->record, 0, sizeof(t->record));
    n = strlen(actual);
    memcpy(t->record.identity, actual, n + 1);
    t->record.unit = (uint8_t)unit;
    t->record.generation = actual_generation;
    t->record.nonce = nonce;
    t->ops = ops;
    t->context = ctx;
    t->state = MMCE_TXN_HELD;
    if (ops->external_backup_verified(ctx, &t->record) != 0) {
        ops->release(ctx, unit);
        clear_transaction(t);
        return MMCE_TXN_UNSUPPORTED;
    }
    return MMCE_TXN_OK;
}

int mmce_txn_prepare(mmce_txn *t, uint64_t nonce,
                     const uint8_t sha256[MMCE_TXN_DIGEST_SIZE])
{
    if (!t || t->state != MMCE_TXN_HELD || !nonce ||
        t->record.nonce != nonce || !sha256)
        return MMCE_TXN_INVALID;
    if (!unchanged_under_fence(t))
        return MMCE_TXN_IDENTITY_CHANGED; /* stay fenced */
    memcpy(t->record.image_sha256, sha256, MMCE_TXN_DIGEST_SIZE);
    if (t->ops->stage_verified(t->context, &t->record) != 0)
        return MMCE_TXN_IO_FAILURE; /* stay fenced */
    /* On ambiguous journal errors, never assume there was no durable write. */
    if (t->ops->journal(t->context, MMCE_JOURNAL_PREPARED, &t->record) != 0) {
        t->state = MMCE_TXN_RECOVERY_REQUIRED;
        return MMCE_TXN_NEEDS_RECOVERY;
    }
    t->state = MMCE_TXN_PREPARED;
    return MMCE_TXN_OK;
}

int mmce_txn_commit(mmce_txn *t, uint64_t nonce)
{
    if (!t || t->state != MMCE_TXN_PREPARED || !nonce ||
        t->record.nonce != nonce)
        return MMCE_TXN_INVALID;
    if (!unchanged_under_fence(t))
        return MMCE_TXN_IDENTITY_CHANGED; /* lease stays held */
    /* Recovery becomes mandatory BEFORE any destructive operation. */
    t->state = MMCE_TXN_RECOVERY_REQUIRED;
    if (t->ops->journal(t->context, MMCE_JOURNAL_COMMITTING,
                        &t->record) != 0)
        return MMCE_TXN_NEEDS_RECOVERY;
    if (t->ops->commit_recoverably(t->context, &t->record) != 0)
        return MMCE_TXN_NEEDS_RECOVERY;
    if (t->ops->verify_committed(t->context, &t->record) != 0)
        return MMCE_TXN_NEEDS_RECOVERY;
    if (t->ops->journal(t->context, MMCE_JOURNAL_FINISHED,
                        &t->record) != 0)
        return MMCE_TXN_NEEDS_RECOVERY;
    t->ops->release(t->context, t->record.unit);
    clear_transaction(t);
    return MMCE_TXN_OK;
}

int mmce_txn_abort(mmce_txn *t, uint64_t nonce)
{
    if (!t || !nonce || t->record.nonce != nonce)
        return MMCE_TXN_INVALID;
    if (t->state == MMCE_TXN_HELD) {
        t->ops->release(t->context, t->record.unit);
        clear_transaction(t);
        return MMCE_TXN_OK;
    }
    if (t->state == MMCE_TXN_PREPARED) {
        if (t->ops->journal(t->context, MMCE_JOURNAL_ABORTED,
                            &t->record) != 0) {
            t->state = MMCE_TXN_RECOVERY_REQUIRED;
            return MMCE_TXN_NEEDS_RECOVERY;
        }
        t->ops->release(t->context, t->record.unit);
        clear_transaction(t);
        return MMCE_TXN_OK;
    }
    /* A potentially interrupted commit cannot be aborted from RAM. */
    return MMCE_TXN_NEEDS_RECOVERY;
}

int mmce_txn_recover_on_boot(mmce_txn *t, const mmce_txn_ops *ops, void *ctx,
                             mmce_journal_phase phase,
                             const mmce_txn_record *durable_record)
{
    if (!t || t->state != MMCE_TXN_IDLE || !ops_complete(ops) ||
        !durable_record || durable_record->unit > 1 ||
        !durable_record->nonce || !durable_record->generation ||
        !valid_identity(durable_record->identity) ||
        (phase != MMCE_JOURNAL_PREPARED &&
         phase != MMCE_JOURNAL_COMMITTING &&
         phase != MMCE_JOURNAL_FINISHED &&
         phase != MMCE_JOURNAL_ABORTED))
        return MMCE_TXN_INVALID;
    /* Caller must read journal BEFORE enabling emulation. Acquire must fail
     * closed; no recovery operation is allowed without the same fence. */
    if (ops->acquire(ctx, durable_record->unit) != 0)
        return MMCE_TXN_UNSUPPORTED;
    t->ops = ops;
    t->context = ctx;
    t->record = *durable_record;
    t->state = MMCE_TXN_RECOVERY_REQUIRED;
    if (ops->drain(ctx, t->record.unit) != 0 ||
        ops->recover_before_expose(ctx, phase, &t->record) != 0 ||
        ops->journal(ctx, MMCE_JOURNAL_FINISHED, &t->record) != 0)
        return MMCE_TXN_NEEDS_RECOVERY; /* KEEP EMULATION FENCED */
    ops->release(ctx, t->record.unit);
    clear_transaction(t);
    return MMCE_TXN_OK;
}
