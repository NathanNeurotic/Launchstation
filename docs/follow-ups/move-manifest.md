# Move: delete only what the copy confirmed (task for an implementing agent)

Status: not started. NOT TESTED on hardware. Everything below was found by reading the code and CodeRabbit's review of #269; none of it was run on a console.

## The problem

`subfunc_Paste` (src/filer_browser.c) implements Move as Copy, then `delete()` (src/filer_actions.c, around line 1900) on the source. `delete()` lists the source folder again with `getDirComplete` and removes whatever that listing returns, batch after batch, until a listing says nothing more.

On FAT, exFAT and MMCE the directory readers return only a count and cannot report a transient read failure. So:

1. Copy lists the source and the listing is cut short (a flaky read, not an error the reader can report).
2. Copy's own recheck lists it again and is cut short the same way, so the two agree.
3. The copy finishes with the partial set.
4. `delete()` lists the folder a third time, this time complete, and removes every entry it finds, including entries Copy never copied.

Result: files that were never copied are deleted from the source. Before #269 `delete()` stopped after one listing; the multi-batch loop added there widens the reach.

Rare (needs the same short read twice, then a full one), but it is data loss, so the fix is a design change, not a patch.

## What to build

Move records what the copy confirmed and deletes only that.

- While `copy()` handles a folder, collect the source entries it actually copied and verified (path or name plus identity: size, and the modification time where the device gives one).
- After the whole copy succeeds, Move deletes exactly those entries, files first and then their folders. It does not list the source to decide what to delete.
- If the copy cannot show it saw every entry (the listing reports an error, hits the entry limit, or the device cannot say), Move leaves the source untouched and returns an error that names the step. A folder is removed only when it is empty after its confirmed entries are gone.
- An entry that was in the source but is not in the manifest (new or unseen) stays, and the Move reports that something was left behind.
- Keep the existing guarantees: a udpfs: Move deletes nothing until the read-back passes (`verifyUdpfsCopy`), a kept destination (`COPY_KEPT_DESTINATION`, `paste_kept`) keeps its source, and a PSU backup Move still deletes the save only after the published `.psu` is accepted (`psu_backup_settle`).
- The manifest has to fit in memory without the 900 KB stack frames the stack guard forbids: allocate it on the heap, bounded, and fail closed (source kept) if it cannot.

## Where to look

- `src/filer_browser.c`: `subfunc_Paste` (Move loop, `paste_kept`, `paste_kept_count`).
- `src/filer_actions.c`: `delete()` and its `getDirComplete` loop.
- `src/filer_copy.c`: `copy()` (where entries are copied; where a manifest would be filled).
- `src/filer.c`: `getDirComplete`, `getDir` and the per-device readers (which can and cannot report errors).
- Tests: `tests/test_copy_psu_listing.py`, `tests/test_listing_callers.py`, `tests/test_copy_pipeline.py`, `tests/test_get_size.py`. They extract the real functions and run them on POSIX fixtures; extend them rather than adding stubs.

## How to know it works

1. A new host test: a source folder whose second and third listings differ (the copy sees a partial folder twice, then a full listing at delete time). Move must leave every entry that was not copied, and must fail or report it, not return success.
2. A new host test: a source with a new file added after the copy listing. That file survives the Move.
3. The existing tests still pass (`python3 tests/<name>.py`, all registered in `.github/workflows/compile.yml`).
4. CI's stack guard still passes (`scripts/check_stack_realign.py` with the `--max-frame` and `--max-chain` entries in `compile.yml`): no big new stack buffers.
5. State clearly that it is NOT TESTED on hardware unless a console run was done.

## Rules for the PR

- Base: `master`. Squash-merge only after green CI on the exact head, a clean CodeRabbit review, and the owner's explicit OK.
- No Claude or AI attribution lines in commits or the PR.
