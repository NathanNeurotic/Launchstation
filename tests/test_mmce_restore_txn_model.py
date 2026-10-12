"""Compile and fault-inject the proposed firmware transaction model.

This tests a host-only model, not existing sd2psXtd firmware and not any PS2
restore operation. No attached card or firmware contents are modified.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "research/sd2psxtd/mmce_restore_txn_model.c"
HEADER = ROOT / "research/sd2psxtd/mmce_restore_txn_model.h"
README = ROOT / "research/sd2psxtd/README.md"
assert HEADER.exists() and MODEL.exists() and README.exists()
code = MODEL.read_text(encoding="utf-8")
assert "sd_rename(" not in code and "sd_remove(" not in code
assert "mmce_txn_recover_on_boot" in code
assert "MMCE_JOURNAL_COMMITTING" in code
assert "MMCE_TXN_RECOVERY_REQUIRED" in code
assert "#define MMCE_TXN_IMPLEMENTED_ON_DEVICE 0" in HEADER.read_text()

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mmce_restore_txn_model.h"

typedef struct {
    int locked, acquire_count, releases, drains, backup_checks, stage_checks;
    int journal_calls, commit_calls, verify_calls, recover_calls;
    int fail_acquire, fail_drain, fail_backup, fail_stage, fail_commit;
    int fail_verify, fail_recover, fail_journal_phase;
    int commit_without_log;
    unsigned unit;
    uint64_t generation;
    char identity[MMCE_TXN_ID_MAX];
    mmce_journal_phase last_phase;
} Mock;

static int acquire(void *ctx, unsigned unit) {
    Mock *f=ctx; f->acquire_count++;
    if (f->fail_acquire || f->locked) return -1;
    f->locked=1;f->unit=unit;return 0;
}
static int drain(void *ctx, unsigned unit) {
    Mock *f=ctx;assert(f->locked && f->unit==unit);
    f->drains++;return f->fail_drain ? -1 : 0;
}
static int identify(void *ctx, unsigned unit,char *buf,size_t len,uint64_t *gen) {
    Mock *f=ctx;assert(f->locked && f->unit==unit);
    assert(strlen(f->identity)+1 <= len);
    snprintf(buf,len,"%s",f->identity);*gen=f->generation;return 0;
}
static int backup(void *ctx,const mmce_txn_record *r) {
    Mock *f=ctx;assert(f->locked && r->unit==f->unit);
    f->backup_checks++;return f->fail_backup ? -1 : 0;
}
static int stage(void *ctx,const mmce_txn_record *r) {
    Mock *f=ctx;assert(f->locked && r->unit==f->unit);
    f->stage_checks++;return f->fail_stage ? -1 : 0;
}
static int journal(void *ctx,mmce_journal_phase phase,const mmce_txn_record *r) {
    Mock *f=ctx;assert(f->locked && r->unit==f->unit);
    f->journal_calls++;f->last_phase=phase;
    return f->fail_journal_phase==(int)phase ? -1 : 0;
}
static int commit(void *ctx,const mmce_txn_record *r) {
    Mock *f=ctx;assert(f->locked && r->unit==f->unit);
    if (f->last_phase!=MMCE_JOURNAL_COMMITTING)f->commit_without_log++;
    f->commit_calls++;return f->fail_commit ? -1 : 0;
}
static int verify(void *ctx,const mmce_txn_record *r) {
    Mock *f=ctx;assert(f->locked && r->unit==f->unit);
    f->verify_calls++;return f->fail_verify ? -1 : 0;
}
static int recover(void *ctx,mmce_journal_phase p,const mmce_txn_record *r) {
    Mock *f=ctx;assert(f->locked && r->unit==f->unit);
    assert(p==MMCE_JOURNAL_COMMITTING || p==MMCE_JOURNAL_PREPARED ||
           p==MMCE_JOURNAL_FINISHED || p==MMCE_JOURNAL_ABORTED);
    f->recover_calls++;return f->fail_recover ? -1 : 0;
}
static void release_lock(void *ctx,unsigned unit) {
    Mock *f=ctx;assert(f->locked && f->unit==unit);
    f->locked=0;f->releases++;
}
static const mmce_txn_ops ops = {
    acquire,drain,identify,backup,stage,journal,commit,verify,recover,release_lock
};
static void reset(Mock *f,mmce_txn *t) {
    memset(f,0,sizeof(*f));strcpy(f->identity,"/MemoryCards/Card1-1.mcd");
    f->generation=17;mmce_txn_init(t);
}
static int begin(Mock *f,mmce_txn *t,unsigned unit) {
    return mmce_txn_begin(t,&ops,f,unit,"/MemoryCards/Card1-1.mcd",17,42);
}
static int prepare(mmce_txn *t) {
    unsigned char hash[32];memset(hash,0x23,sizeof(hash));
    return mmce_txn_prepare(t,42,hash);
}
int main(void) {
    mmce_txn t, restart;
    Mock f;
    mmce_txn_record durable;
    assert(MMCE_TXN_IMPLEMENTED_ON_DEVICE==0);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && t.state==MMCE_TXN_HELD);
    assert(f.locked && f.backup_checks==1 && !f.commit_calls);
    { unsigned char unused_digest[32] = {0};
      assert(mmce_txn_prepare(&t,99,unused_digest)==MMCE_TXN_INVALID); }
    assert(t.state==MMCE_TXN_HELD && f.locked);
    assert(prepare(&t)==0 && t.state==MMCE_TXN_PREPARED);
    assert(mmce_txn_commit(&t,99)==MMCE_TXN_INVALID);
    assert(mmce_txn_commit(&t,42)==MMCE_TXN_OK);
    assert(t.state==MMCE_TXN_IDLE && !f.locked && f.releases==1);
    assert(f.commit_calls==1 && f.verify_calls==1 && !f.commit_without_log);
    assert(f.last_phase==MMCE_JOURNAL_FINISHED);

    reset(&f,&t);f.fail_acquire=1;
    assert(begin(&f,&t,0)==MMCE_TXN_UNSUPPORTED);
    assert(!f.locked && !f.releases && !f.commit_calls);
    reset(&f,&t);f.fail_drain=1;
    assert(begin(&f,&t,0)==MMCE_TXN_NEEDS_RECOVERY);
    assert(t.state==MMCE_TXN_RECOVERY_REQUIRED);
    assert(f.locked && f.releases==0 && !f.commit_calls);
    reset(&f,&t);f.fail_backup=1;
    assert(begin(&f,&t,0)==MMCE_TXN_UNSUPPORTED);
    assert(!f.locked && f.releases==1);
    reset(&f,&t);f.generation=18;
    assert(begin(&f,&t,0)==MMCE_TXN_IDENTITY_CHANGED);
    assert(!f.locked && !f.commit_calls);

    reset(&f,&t);
    assert(begin(&f,&t,1)==0 && f.unit==1);
    assert(mmce_txn_abort(&t,42)==MMCE_TXN_OK);
    assert(f.releases==1 && !f.commit_calls && !f.journal_calls);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0);f.fail_stage=1;
    assert(prepare(&t)==MMCE_TXN_IO_FAILURE);
    assert(t.state==MMCE_TXN_HELD && f.locked && !f.commit_calls);
    assert(mmce_txn_abort(&t,42)==0 && !f.locked);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0);f.generation++;
    assert(prepare(&t)==MMCE_TXN_NEEDS_RECOVERY);
    assert(t.state==MMCE_TXN_RECOVERY_REQUIRED && f.locked);
    assert(mmce_txn_abort(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(!f.releases);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0);f.fail_journal_phase=MMCE_JOURNAL_PREPARED;
    assert(prepare(&t)==MMCE_TXN_NEEDS_RECOVERY);
    assert(t.state==MMCE_TXN_RECOVERY_REQUIRED && f.locked && !f.commit_calls);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    assert(mmce_txn_abort(&t,42)==0 && !f.locked);
    assert(f.last_phase==MMCE_JOURNAL_ABORTED && !f.commit_calls);
    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    f.fail_journal_phase=MMCE_JOURNAL_ABORTED;
    assert(mmce_txn_abort(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(t.state==MMCE_TXN_RECOVERY_REQUIRED && f.locked);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    f.generation++;
    assert(mmce_txn_commit(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(t.state==MMCE_TXN_RECOVERY_REQUIRED && f.locked && !f.commit_calls);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    f.fail_journal_phase=MMCE_JOURNAL_COMMITTING;
    assert(mmce_txn_commit(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(!f.commit_calls && f.locked && !f.releases);
    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    f.fail_commit=1;
    assert(mmce_txn_commit(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(f.commit_calls==1 && f.locked && !f.releases);
    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    f.fail_verify=1;
    assert(mmce_txn_commit(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(f.commit_calls==1 && f.locked && !f.releases);
    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    f.fail_journal_phase=MMCE_JOURNAL_FINISHED;
    assert(mmce_txn_commit(&t,42)==MMCE_TXN_NEEDS_RECOVERY);
    assert(f.commit_calls==1 && f.locked && !f.releases);

    /* Power loss after journal COMMITTING: a brand-new boot must reconcile
       the authenticated persistent record BEFORE exposing the card. */
    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    durable=t.record;
    f.locked=0;mmce_txn_init(&restart); /* simulate a firmware restart */
    assert(mmce_txn_recover_on_boot(&restart,&ops,&f,
           MMCE_JOURNAL_COMMITTING,&durable)==MMCE_TXN_OK);
    assert(restart.state==MMCE_TXN_IDLE && f.recover_calls==1);
    assert(!f.locked && f.releases==1 && !f.commit_calls);
    assert(f.last_phase==MMCE_JOURNAL_FINISHED);

    reset(&f,&t);
    assert(begin(&f,&t,0)==0 && prepare(&t)==0);
    durable=t.record;f.locked=0;f.fail_recover=1;
    mmce_txn_init(&restart);
    assert(mmce_txn_recover_on_boot(&restart,&ops,&f,
           MMCE_JOURNAL_COMMITTING,&durable)==MMCE_TXN_NEEDS_RECOVERY);
    assert(restart.state==MMCE_TXN_RECOVERY_REQUIRED && f.locked && !f.releases);

    reset(&f,&t);
    assert(mmce_txn_begin(&t,&ops,&f,2,"/MemoryCards/Card1-1.mcd",17,42)==MMCE_TXN_INVALID);
    assert(!f.acquire_count);
    assert(mmce_txn_recover_on_boot(&t,&ops,&f,0,&durable)==MMCE_TXN_INVALID);
    assert(!f.acquire_count);
    puts("PASS: proposed MMCE transaction fences, journal ordering, fail-closed and boot recovery");
    return 0;
}
''';

CC = shutil.which("gcc") or shutil.which("clang")
assert CC, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="mmce-txn-model-") as temp:
    src = Path(temp) / "harness.c"
    exe = Path(temp) / "test_txn"
    src.write_text(harness, encoding="utf-8")
    subprocess.run([CC, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-I", str(HEADER.parent), str(src), str(MODEL), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: model stays unlinked and its firmware capability remains disabled")
