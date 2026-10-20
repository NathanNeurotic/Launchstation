# Zack's October 19 follow-up

All changes in this checklist belong to one branch and [PR #188](https://github.com/NathanNeurotic/Launchstation/pull/188):
`fix/zack-issues-20261019`, based on `82082b6`. Keep follow-up commits here.
The user explicitly excluded #160 (stock PS2 sound effects).

| Issue | Work and evidence | Remaining acceptance |
| --- | --- | --- |
| #187 destructive ELF replacement | Write a separate sibling, close and checksum its readback before parking the original and publishing. Failed publication rolls back; failed rollback retains both recovery files. Never delete the existing destination before copying. Host fault injection covers both MC ports, ordinary filesystem, short reads, corruption, close failure, both rename failures, rollback failure and stale recovery files. | Console repeated ELF replacement and low-space cases. Replacement needs room for both files and rename support; unsupported operations preserve the original. |
| #184 transfer progress | Dedicated filename and remaining-MB lines, smoothly approached progress fill, one visual tick per frame. Native copies bypass legacy metric flattening. Slow MC/VMC operations use 16 KiB chunks; recursive deletion refreshes between entries and advances selected-item progress after completion. Readback verification also draws progress. | Console/theme inspection; a blocked synchronous filesystem RPC still prevents drawing until it returns. |
| #185 Home MASS label | The shared app-device label now reads MASS Storage in Home and app views; the library filter reads MASS. Filesystem paths and device matching stay unchanged. | Console inspection. |
| #182 summarized release changelog | Maintain concise user-facing entries in CHANGELOG.md. The release generator reads the Rolling section from the exact built commit, including on a rerun with no new commits. Existing commit summaries remain a fallback for older revisions. | Preview generated notes; publication remains part of the normal verified-master release workflow. |
| #125 software-clock persistence | Setting software time now offers an explicit Save clock and settings confirmation, waits for writes, and reports success only after completion. Settings writes require successful close and exact readback; a missing store path marks the save failed. Existing production CNF tests retain exact seconds through repeated cold boots. | Unplug/restart on dead-RTC console. No host reproduction of a fixed ten-second subtraction; unsaved runtime and unpowered elapsed time remain unavailable. |
| #173 MX4SIO loading stalls | Returned mount retries draw every ~16 ms instead of sleeping 100 ms per frame; directory-check cadence and the 2 s settling window are retained. Production-function tests cover absent and delayed-mounted devices. | Present/absent adapters on console. Synchronous module entry and filesystem RPC latency remain unresolved without console timing. |
| #174 MMCE loading/reset stalls | Restore the probed port's original SIO2 timing registers and acknowledge MMCE's pending IRQ before returning the bus to MCMAN/PADMAN, including timeout paths. Unwind hooks if filesystem registration fails. No new reset, unload, shortened protocol timeout or worker module load. | Present/absent adapters and audio on console; module entry remains synchronous and its startup handshakes can still pause drawing. |
| #175 MMCE cross-storage/slot 2/power-off | Power-off setup no longer loads an unused DEV9 stack; real HDD/network consumers still load it explicitly. Failed power-off module/RPC initialization is not marked ready, and the menu avoids calling an unavailable service. #174's bus-state restoration also applies to ordinary MC access after MMCE. | Mixed-storage transitions, both MC slots and power-off on console. Broad cross-storage reliability remains unverified. |

Host tests and builds establish software behavior under the tested conditions.
They do not establish adapter timing, CRT layout or physical-console reliability.
Keep hardware-dependent reports open until Zack verifies their remaining cases.

## Replacement recovery

Ordinary replacement files use unused `LS-COPY-NNN.TMP` and
`LS-OLD-NNN.BAK` sibling names (within the MC filename limit). They are never
reused while present. If power is interrupted during publication, inspect those
files before removing anything: the BAK contains the old file and the TMP the
replacement. Failed cleanup keeps the BAK. Failed rollback keeps both files.
New files and PSU container generation retain their existing paths; ordinary
files extracted from a PSU use the replacement protection too.

## Validation

- All 104 Python host checks pass, including six new production-function harnesses.
  This is the `54a7bc9` baseline; subsequent exact-head counts are recorded on the PR.
- Standard and DS34 variants compile, link and pack locally; controller-symbol
  separation and the embedded loader's low-memory boundary are checked separately.
- The documentation generator builds 35 pages and 262 search entries.
- Final package and GitHub Actions results are recorded on the PR.
- Review follow-up covers ambiguous libmc completion, false-success rename
  responses, long filenames, and a paste error message that no longer assumes
  the destination is read-only.
- Checked PS2SDK's `mcdev.c` error translation and `ps2mc_fio.c` against the
  replacement code: fileXio stat absence is `-ENOENT`; an interrupted or failed
  stat must fail closed. MC's create flag does not provide atomic exclusive
  creation, so the code does not claim protection against an external writer
  racing the same filename. Dashboard workers are drained before file operations.

## Console acceptance still required

1. On both MC slots, repeatedly replace an existing ELF with the same and newer
   files. Test insufficient space and cancel midway; the original must survive.
   Inspect recovery files after a deliberate interruption during publication.
2. Check copy/delete/readback progress with long filenames across light, dark
   and animated themes. Inspect the Home MASS label.
3. Set software time to `10:00:10`, accept Save, wait for **Clock saved**, then
   unplug/restart. The saved seconds must persist. Later unsaved runtime and
   elapsed unpowered time are not reconstructed.
4. Time present and absent MX4SIO/MMCE activation from the reported console.
   Separate module-entry delay, the MMCE ping calls, and returned mount waits.
   Synchronous module/RPC stalls remain an unresolved part of #173/#174.
5. After MMCE probing, read ordinary cards in both slots, switch storage stacks,
   verify PAD/audio/USB recovery, and exercise menu and button power-off. Repeat
   with real MMCE/MX4SIO hardware; cross-storage reliability in #175 is unproven.

No issues are automatically closed and no merge or release is requested by this PR.

## October 20 continuation - UDPFS-to-MC report and review

User reports that the newest Actions artifact acts as though a UDPFS source file
is no longer available at paste time. Destination confirmed as **mc0: / slot 1**.
The exact tested SHA, returned error and source/server trace are not available.
Do not call this report reproduced or fixed based on the following host fixtures.

- The production copy loop rejected positive short reads/writes. It now consumes
  the remaining bytes without replaying completed I/O, stops on zero/error/invalid
  counts, preserves negative driver errors, and checks the rewind after sizing.
  Fault injection executes the production dispatch over 131081 bytes with full,
  4096-byte, 17-byte and single-byte transfers, plus timeout/space/EOF failures.
  The old PR head fails the short-read case; corrected production dispatch passes.
- A separate preflight defect affects **slot 2 after MX4SIO**, not the user's
  confirmed mc0 case. Previously the late destination stat/open could reset the
  IOP after the source was opened. Preflight now releases that stack before
  loading either transfer device; copy uses that same policy before opening.
  Refused resets and MX4SIO-to-mc1 transfers fail closed. Tests cover UDPFS, USB
  and mc0 in both directions. Old preflight fails; corrected preflight passes.
- Paste failure text includes the numeric error, so a subsequent artifact retest
  can distinguish missing source, I/O, permission and space failures. Existing
  serial logs already record source, destination, item and returned code.
- CodeRabbit follow-up removes a pre-existing startup changelog item, falls back
  to commit changes when a new revision has unchanged Rolling notes, scopes Clock
  not saved to dashboard persistence failure, and removes stale transfer-only
  size/bar state on copy-to-delete transitions. A missing compiler is reported
  explicitly in the power-off harness.

The upstream UDPFS read implementation can return completed byte progress before
an error in a later internal request. This supports handling positive short reads,
but does not identify the behavior of the exact bundled binary/server pair here.
Reference: https://github.com/ps2max32/neutrino/blob/master/iop/udpfs/src/udpfs_core.c

No hardware execution was performed. Retest fresh and replacement UDPFS copies
to mc0 and mc1, including nested folders and a source larger than one chunk. Keep
all Zack issues open until he confirms acceptance. Stock sounds remain deferred.

## Compact test preparation (October 20)

- Paste failures now show stage, a bounded filename, source/destination devices,
  error, written/total bytes, IOP generation before/after and build ID. This uses
  one static record (96 bytes in the PS2 ELF; host budget assertion <=128 bytes), no history, allocation,
  worker, or diagnostic storage/network I/O. The dashboard displays it as the
  error body so a photograph contains the useful fields. Full paths remain in
  the existing serial diagnostics. Move-source deletion clears copy-only context.
- `test_copy_pipeline.py` compiles the intact production `copy`, `subfunc_Paste`,
  clipboard source wait, replacement and diagnostic functions with POSIX file
  adapters. It covers new/repeated replacement copies to both MC slots and USB,
  source disappearance, rewind/read/write/close errors, failed source checks,
  cancellation, recursive folders and preservation of the old destination.
  It does not run actual UDPFS or libmc. A failed source-directory listing now
  propagates its error rather than reporting an empty-folder copy as successful;
  HDD-only partition buffers are initialized before ordinary folder processing.
- `scripts/copy_test_pack.py --output LS-COPY-TEST.zip` creates nine deterministic
  data fixtures (246018 bytes total), boundary lengths around 16/64 KiB, a nested
  child and SHA256SUMS. It does not include an ELF or alter app packages. Verify
  returned files using `--verify <returned LS-COPY-TEST folder>`; expected hashes
  come from the generator rather than trusting the returned manifest. CI uploads
  this ZIP separately as `Launchstation-copy-test`.

Console pass: UDPFS -> mc0 new files; repeat over existing files twice; compare
USB -> mc0 and UDPFS -> USB with the same fixtures. Copy results back to a new PC
folder and verify. Photograph any failure including build/stage/error. Inspect
progress UI while these operations run. Test an ELF separately using the app ZIP,
then slot 2 and storage transitions if available. The reported UDPFS -> mc0
failure is still unconfirmed; these additions make the next session diagnostic.

Local size comparison against `72be137`: the packed standard ELF grows by 768
bytes and DS34 by 976 bytes. Host fixture code and the generator are not linked
into the PS2 program. Full-copy cases include destination stat/open refusal,
premature EOF, failed measurement, corrupt verification and publish failure.

## UDPFS directory error propagation (October 20 follow-up)

The earlier copy harness supplied negative listing errors, but the real
`readUDPFS` converted directory-open errors into zero entries and ignored
negative directory reads after returning some entries. The new production-reader
harness exercises the initializer and directory RPC adapter together. Open,
read and close failures now remain negative, including after a partial listing.
Copy aborts before reporting success or deleting move sources; recursive deletion
also stops before attempting to remove a directory whose listing failed.
Classic browsing clamps failed counts while retaining the parent-directory row.
This specifically covers UDPFS; other readers retain their existing contracts.

Neutrino's current upstream `udpfs_dread` distinguishes zero EOF from negative
errors: https://github.com/rickgaiser/neutrino/blob/master/iop/udpfs/src/udpfs_ioman.c
This reference is not proof of the exact bundled binary/server combination.
The bundled IRX files are unchanged. Normal listing limits and retry behavior
are retained; no new RPC, IOP reset, allocation or resident record is added.

An independent full-copy regression caught repeated empty-folder paste failing
when an optional game-title lookup failed. A successful empty listing now clears
that unrelated result. The copy harness also executes new/replacement files at
0, 1, 16383, 16384, 16385, 65535, 65536 and 65537 bytes.

Replacement readback retains the filename during progress, reports 100% only
after successful verification/close, and preserves actual negative read errors.
Focused host tests and standard/DS34 target builds validate these changes; the
PR records exact-head CI. No physical-console acceptance is claimed, and the
user's original single-file UDPFS-to-mc0 failure is still unconfirmed.
