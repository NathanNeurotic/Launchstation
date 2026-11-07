# Audit: File Browser cannot access SMB ("Device not found"), 2026-11-07

Scope: Launchstation `origin/master` at `0814f5f` ("Show SMB connection
failures in Files (#300)"). Isolated worktree:
`Launchstation.worktrees/smb-file-browser-audit-20261107-044232`.

No production source was changed. This report is the deliverable.

## 1. What the user sees

When an SMB entry is selected in Files, the old code path reported only a
generic error:

- `src/dash_browser.c:663-668` (`activate`) - after `dashDevicesStart(d, NULL, NULL)`,
  if `!dashDevicePathReady(path)`, it toasts `"Device not found"` and rebuilds
  the device list.
- `src/dash_browser.c:511-517` (`openDir`) - prepare failure was reported as
  `"Device could not be opened"`.
- `src/dash_files.c:37-52` (`deviceNotFound`) - produces "Device not found"
  if a probe/connection does not report ready.

Recent commits (`#300`, `codex/smb-device-not-found-diagnostics`) improve this
by adding `smbStatusMessage(...)` toasts, but the UI still discards root-cause
detail in some paths and does not show the share/device state directly.

## 2. Confirmed or strongly indicated defects

### A. "Ready" is based on IOP driver state, not on configured/reachable shares

- `src/dash_devices.c:248-250` - `dashDevicePathReady` for SMB falls through to
  `storageState(path)` and `isPathDeviceStackLoaded(path)`.
- `isPathDeviceStackLoaded` (`src/init.c:2981-2983`) returns only
  `isSmbDriverLoaded(...)`; it proves the driver is loaded, not that any share
  connected.
- `dashDevicePathReady` never asks `smbSharesConnectedCount`.
- Therefore an SMB device with a loaded driver but zero successfully connected
  shares can look ready at the dashboard layer while Files still reports failure.

### B. Probe failure is collapsed into "absent"

- `src/dash_devices.c:609-618` (`probeMount`, SMB branch) returns
  `ok = smbSharesConnectedCount(v) > 0`.
- `recordProbe` (`src/dash_devices.c:59-65`) stores only a present/absent bit,
  not `problem` or which share failed.
- `storageState` (`src/dash_devices.c:69-77`) returns `DASH_DEV_ABSENT`; the
  underlying reason is no longer recoverable from that bit.
- The dashboard can retry with `smbSharesStatus` only because it rereads
  config independently, but the readiness path does not expose this.

### C. Driver connection failure has two independent reasons that are not distinguished to the user

- `load_smb` (`src/init.c:899-946`) fails before share connection for several
  environment reasons:
  - PC CARD CF owns the bus (`pcCardCfOwnsBus`), `src/init.c:907-909`.
  - legacy Ethernet drivers are on (`setting->legacy_net_drivers`), 911-915.
  - network stack cannot switch to ETH because UDPFS is active, 916-921.
  - network stack cannot start (`load_ps2ip`), 928-931.
  - IRX cannot be loaded (`SifExecModuleBuffer`), 932-942.
- Only after all of these does `smbSharesConnect` run. It can still fail due to
  credentials, share, address, timeout, or driver errors.
- `smbStatusMessage` (`src/dash_browser.c:326-338`) does distinguish "driver not
  loaded" vs. share status, but it is only called in selected recent paths.
  The initial Files interaction before those calls remains generic.

### D. Config file discovery is fragile and has multiple precedence rules

- `smbReadConfigText` (`src/smb_shares.c:47-76`) tries `LaunchElfDir + "SMB2.CNF"`,
  then the `SYS-CONF` file on `mc1` followed by `mc0` when `LaunchElfDir` starts
  with `mc1`. Otherwise it tries `mc0` followed by `mc1`, skipping `mc1` when
  `mc1AccessRefused()`.
- A config saved manually or by another tool in a place not covered by those
  rules is invisible, even though the UI "configured correctly".
- `smb_cfg_path` is the only reported path, and multi-file mirroring (see
  recent `#297`) creates confusion about which file won.
- `smbReadConfig` keeps a pending mc1 state:
  - `smb_cfg_loaded = !mc1_refused || smb_cfg_path[0]` (line 131).
  - `smbSharesKnown(v)` (`src/smb_shares.c:149-151`) may return a cached count
    while mc1 is pending if an mc0 fallback was loaded; it returns 0 when no
    config has been loaded.
  - Dashboard V1 builds its entry with `smbSharesKnown(SMB_PATH_V1)`
    (`src/dash_devices.c:438`). Boot warm-up calls `smbSharesConfigured` for V1
    (`src/dash_main.c:1185`), which parses the shared `SMB2.CNF` for both
    versions and merely reports a V1-specific count. V2's displayed state still
    depends on cached counts and later connection state.
- mc1 pending logic can preserve a fallback mc0 file while also retaining the
  pending flag, making the "configured file" ambiguous.

### E. Config/reload race and rate limiting

- `smbSharesConnect` rate-limits failed retries to 30 seconds
  (`SMB_RETRY_MS`, `src/smb_shares.c:21`, lines 263-268).
- Config is reread only when `retry_failed && both connected counts are 0`
  (line 269), otherwise stale state can be retained.
- `smbSharesReload` (`src/smb_shares.c:370-378`) now returns 0 if
  `smbDisconnectAllSessions()` returns negative (lines 94-107), leaving config
  state unchanged. A failed disconnect therefore prevents a corrected config from
  being used.
- `smbDisconnectAllSessions` considers only count-gated calls; `fileXioDevctl`
  errors are treated as total failure even if one version succeeds. This can make
  a reload impossible until a manual retry > 30s later.

### F. Settings/dual-file writes can produce partial mirrors

Recent branch `fix/smb-config-sync-reconnect-20261106` addressed mirroring and
reports partial failures, but the underlying operation is still complex:

- `smbSharesReload` must retire sessions before reparsing, and failures block it.
- `smbReadConfig` retires sessions only when `smb_cfg_mc1_pending` and config
  changed, and itself can fail (`src/smb_shares.c:112-122`) without surfacing
  to the GUI except a retained old file.
- `SMB2.CNF` is kept in CWD and SYS-CONF. A user editing one, or a Partial
  mirror failure reported as success in older code, results in the PS2 reading a
  different config than the one the user believes to have saved.

### G. Parser constraints can reject otherwise valid user configs

- `splitSmb1Url` (`src/smb_cnf.c:134-185`) requires an IPv4 literal; DNS names
  are rejected (`isIpv4`). A user who configures a host name or a share using a
  DNS name will receive "invalid config" rather than a usable share.
- `checkName` (`src/smb_cnf.c:187-201`) allows duplicate names across invalid
  shares indirectly, and name uniqueness is checked case-insensitively only
  against non-invalid shares (line 197), so a rename from invalid to valid can
  collide.
- `smbCnfUpsertText` (`src/smb_cnf.c:433-518`) uses probe lines of 128 bytes
  (line 488) to identify block boundaries; long key lines are truncated
  before classification, which can misidentify a block boundary on very long lines.
  This is not a normal config case, but it is a defect in an already fragile
  editor.

### H. Host test fragility around recently added helper

- `tests/test_smb_connect_state.py` extracts `smbDisconnectAllSessions` along
  with the SMB connection functions, but its mock omits `isSmbDriverLoaded`.
  With `-Werror`, that missing declaration breaks host compilation against
  `0814f5f`. The mock also only understands SMB2 disconnects, not SMB1 logoff.
  Neither this test nor `test_smb_cnf_edit.py` was in the main CI workflow at
  the audited revision; passing CI did not exercise them.
- Host compile also requires `-fsanitize=...`, which is unavailable with the
  MinGW GCC present in this environment. Several SMB tests cannot be executed
  here even if they are conceptually correct.
- `tests/test_smb_shares.py` and `tests/test_smb_cnf_edit.py` compile with
  sanitizers and fail locally for toolchain reasons, not necessarily code defects.

### I. Inefficient/stale code needing cleanup

- Repeated `smbPathKind(path)` calls in `readSMB` and browser paths should be
  calculated once. Minor, but shows duplicated logic.
- Dashboard entries use a mix of:
  - IO-forcing `smbSharesConfigured` (byte 123)
  - non-IO `smbSharesKnown`
  - status probes
  This split makes behavior timing-dependent and hard to reason about.
- The boot warm-up calls `smbSharesConfigured(SMB_PATH_V1)`, which parses the
  shared `SMB2.CNF` for **both** versions; only the returned count is V1-only.
  An unfinished or refused-card read can still leave cached state pending.
- Device-less progress/toast state is mixed with File Browser state; the generic
  "Device not found" fallback remains a catch-all.

## 3. Recommended implementation order for the clean solution

1. Change File Browser to always show the specific failure reason for SMB before
   the generic fallback. Recent `smbStatusMessage` is a start, but it should be
   the only path for SMB device activation.
2. Reuse existing `SMB_STATE_*`, driver-load errors, and probe results for
   precise feedback. Do not add another five-state SMB model unless a specific
   unrecoverable state proves the existing information insufficient.
3. Add deterministic config-first validation before talking to IOP:
   - If zero configured shares, report that exactly.
   - If driver cannot load, report the specific environment reason.
   - Then connect and report per-share result.
4. Separate config discovery errors from network errors:
   - Distinguish "No SMB2.CNF found" from "SMB2.CNF has no valid share".
   - Report the file path that was read.
5. Measure and bound the underlying SMB2 connect timeout first: the existing
   driver can wait about 30 seconds **per failed share**. Preserve driver/IOP
   startup on its required thread, avoid unbounded UI waits, and only change
   explicit retry backoff once the transport has a safe bounded contract.
6. Preflight destination files, preserve unrelated share entries/comments, then
   write and verify each location independently. Report partial failures and
   required restarts; do **not** claim cross-device atomicity.
7. Retain one boot-time configuration parse for both versions; distinguish
   actual unread/pending state from an empty configuration without extra I/O.
8. Allow DNS hostnames or reject them with a precise message instead of a false
   config parse failure.
9. Fix host tests/mocks to track newly added helpers and run them in CI with a
   toolchain that supports sanitizers.

## 4. Verification needed for the future implementation

- Unit host tests for `readSMB`, `activate`, and `dashDevicePathReady` SMB paths
  with driver load success/failure and share connect success/failure.
- Integration test for config file discovery precedence and multi-file mirror.
- Integration test for reload after editing a share within 30 seconds.
- Integration test for a failed SMB2 disconnect followed by a successful SMB1
  logoff, including a later SMB1 connection attempt.
- Hardware smoke test: loaded driver but disconnected share should not read
  "Device not found" without the specific SMB status.

## 4b. Additional state, settings, and IOP-level findings

### P. `load_smb()` returns success even when `smbSharesConnect()` fails

- `src/init.c:944-945` calls `smbSharesConnect(version, 1);` and then `return 1;`
  without checking connected share count.
- Therefore `isSmbDriverLoaded()` becomes true even if every configured share
  failed.
- This conflates "driver IRX loaded" with "storage is usable", which is exactly
  why UI readiness predicates and error reporting are inconsistent.

### J. GUI config save can commit one location but not the other

- `smbedWriteFile` (`src/dash_settings.c:490-551`) reads the first config path
  (`smbedConfigPath`, line 504) and writes there first (line 536).
- SYS-CONF is mirrored only if it already exists (`genOpen` succeeds, line 541),
  and `dashWriteFile` on line 543 is not checked.
- If the first write succeeds while the SYS-CONF mirror fails, `smbSharesReload()`
  can still succeed on line 545, so `smbedWriteFile` returns 0 even though another
  location retains stale share entries.
- There is no rollback or atomic update across CWD and SYS-CONF. The actual
  order is: write primary, attempt existing SYS-CONF mirror, reload, snapshot.
  The mirror attempt is unchecked, so a partial write can still be reported
  as fully successful. Neither destination is created simply to satisfy a
  two-location synchronization guarantee.

### K. `smbSharesReload()` treats a single failed disconnect as total failure

- `smbDisconnectAllSessions` (`src/smb_shares.c:94-107`) returns 0 if either
  V1 or V2 devctl returns an error, even when the program-level intent was to
  retire sessions before reparsing. In the specific mismatch case, V2 disconnect
  can fail while V1 logoff succeeds, but cached V1/V2 states both remain
  `CONNECTED` despite V1's session being closed.
- Consequently `smbSharesReload()` (`src/smb_shares.c:370-378`) refuses to
  forget state and reload config (`ret = -6` path in settings). Later connection
  attempts can skip SMB1 because its stale state says `CONNECTED`, leaving it
  unavailable until a restart.
- Once this happens, the PS2 keeps using old shares until a manual reset,
  even though `SMB2.CNF` on disk may already have been updated.

### L. Network-stack switches can silently invalidate all access

- `load_smb()` requires Ethernet stack ownership (`src/init.c:916-921`).
- If UDPFS is active, it asks `switchNetworkStack(NETWORK_STACK_ETH)` and,
  if that fails, reports only `"The network adapter is busy (UDPFS)."`.
- `switchNetworkStack()` (`src/init.c:2230-2244`) resets the runtime device state
  when switching from another mode, and can refuse the switch entirely.
- Thus a seemingly correct SMB config cannot be reached while UDPFS or the
  PC CARD CF Ethernet path owns the DEV9/network bus, and the error is not
  attached to the SMB device's persisted state beyond a load string.

### M. State predicates are split between cached and uncached readers

- Device list code correctly avoids filesystem IO by calling `smbSharesKnown`
  instead of `smbSharesConfigured` (`src/smb_shares.c:149-151` and
  `src/dash_devices.c:438`).
- The warm-up invokes a V1-specific count but parses all configured versions.
  After it completes, V2 can use cached counts; it is **not** universally
  waiting for a second parse. A refused/pending memory card may still require
  a later read, so zero cached shares is not definitive.
- `smbSharesKnown` can return 0 while a config file did (or will) exist, so UI
  tests based on it cannot prove that the user's config is wrong.

### N. SMB1-only restriction and IPv4-only URLs are policy-level behavior, not user errors

- `smbConfigValidCount` and `closeShare` enforce only one SMB1 share
  (`src/smb_cnf.c:215-218`).
- `splitSmb1Url` rejects DNS names and requires IPv4 (`src/smb_cnf.c:167-172`),
  matching `smbman`. The GUI even documents this, but a user entering a host
  name receives an "invalid config" rather than a mountable share.
- The GUI rejects DNS by construction (`src/dash_settings.c:563`: "IPv4 address
  ... not valid"), so documented examples must always use IPv4.
- Any mismatch in examples/tests regarding DNS is a configuration defect, not a
  hardware limitation.

### O. Classic browser and dashboard duplicate SMB handling

- `filer_browser.c` contains its own `smb_dir_open_failed` handling and resets
  the cursor to the SMB parent/root.
- `dash_browser.c` has a separate path with `openDir`, `activate`, and
  `listDone` toasts.
- The two implementations maintain slightly different behaviors (reset,
  retry, warnings, copy/paste constraints), so a fix in one browser can leave
  the other stale. The cleanup should centralize SMB open-status reporting.

## 5. Worktree state

- Only the report file should be untracked.
- No source files were modified.

## 6. Second-pass validation and minimal implementation handoff

Confirmed against `master` at `0814f5f`; these source defects do not prove
which one caused a particular hardware SMB mount failure:

- **P0 / session-state correctness:** `smbDisconnectAllSessions` must clear the
  successfully disconnected version's cached states even if the other version
  fails. Keep a failed/uncertain version fail-closed and abort config replacement.
- **P0 / persistence:** the GUI editor must not report a complete CWD/SYS-CONF
  save after a missing or failed mirror. A safe repair preflights both intended
  targets, preserves each destination's unrelated share entries and comments,
  verifies writes, and explicitly reports partial results. The current
  `smbCnfUpsertText` preserves text **outside** a targeted share's key-to-URL
  span, but **deletes comments embedded inside that span** on edit/delete.
  Therefore it is not yet a fully comment-preserving primitive: either make
  edits preserve those in-block comment lines (and regression-test that
  behavior) or explicitly warn users before a potentially destructive edit.
  **CR-only line endings are a separate data-loss risk:** `smbConfigParse`
  recognizes bare `\\r` as a line separator, but `lineOffset` in the
  same source counts only `\\n`. For a CR-only file with multiple shares,
  editing an early share can compute an end offset at EOF and silently remove
  subsequent share blocks. Checking only the edited share after a write is
  insufficient. A follow-up fix must make offsets recognize CR, LF, and CRLF
  consistently with the parser, or reject unsupported line endings *before*
  any write. Add a regression fixture with **at least three CR-only shares**:
  edit the first and middle share, then remove one, verifying that all
  unrelated shares, their URLs, and their comments remain intact; verify the
  same with CRLF/LF/mixed endings and compare the full before/after text.
  Preserve the original file on failed validation. Until this is fixed,
  never claim editing is safe for CR-only files.
  Do not silently use the helper to promise lossless updates. Copying the
  entire primary text over another existing file could also lose the other
  file's separately configured shares and annotations.
- **P1 / connection identity:** `connectSmb2` previously converted
  `-EEXIST` into success with no proof that the driver's NAME corresponds to
  the current host/share/credentials. Prefer a clearly reported failure over
  falsely claiming a connection to the edited destination.
- **P1 / IOP state:** `clearIopModuleState` forgets loaded drivers but did not
  clear `smb_load_error[]`. The stale error must be reset with the IOP state.
- **P1 / browser status:** #300 already handles SMB-specific failures in the
  newer File Browser. `dash_files.c` still has a generic failed-network modal.
  Reuse the existing status/cause formatter rather than create an independent
  SMB status architecture.
- **P1 / actual mount proof:** record the effective config path, configured
  IP, **TCP port**, share name and dialect; IOP driver load ID/result; all
  version-specific connection devctl return values; the first directory
  probe's result; and matching server-side logs. Validate the exact requested
  export—not another Windows share. Use these **different** paths/operations:
  - **SMB1 / `VERSION = 1`:** load `smbman.irx`; call
    `SMB_DEVCTL_LOGON` followed by `SMB_DEVCTL_OPENSHARE`; then probe
    `smb1:/`. Neither `smb:/NAME` nor SMB2 CONNECT proves SMB1 worked.
  - **SMB2/3 / `VERSION = 2` or `3`:** load `smb2man.irx`; issue
    `SMB2_DEVCTL_CONNECT` for the configured `NAME` and URL; probe
    `smb:/NAME/`. These VERSION values share one negotiating driver; a
    successful devctl alone is not proof that the directory is accessible.
  Test both on physical PS2 hardware with normal and constrained IOP memory
  budgets. Record the server's chosen dialect and share separately; avoid
  logging passwords.
- **Do not conflate APIs:** `load_smb` returning success means the IOP driver
  loaded; `smbSharesConnectedCount` and the probe determine usable shares.
  Changing `load_smb` to fail whenever credentials are rejected would mix
  driver lifecycle with connection state and invite unnecessary reload attempts.
- **Do not conflate dialect selection:** `VERSION = 2` and `VERSION = 3`
  both use the same negotiated SMB2/3 driver; 3 is a label, not a forced
  SMB3-only negotiation.

An independent draft implementation PR, [#306](https://github.com/NathanNeurotic/Launchstation/pull/306),
addresses per-version disconnect, `-EEXIST` fail-closed, IOP-error cleanup
and missing host-test CI execution. The further source-level configuration
integrity fixes below were requested after this original investigation.
Server compatibility, protocol timeouts and physical-console read/write tests
remain unverified.

## 7. Independent Nemotron audit adjudication and implementation follow-up

The external report supplied on 2026-11-07 is a **preliminary plan**, not
demonstrated PS2 hardware validation. Its useful, source-verified findings and
their disposition are recorded here so subsequent agents do not repeat
superseded claims:

- **Accepted — CR-only multi-share data loss (SMB-001):** The parser recognizes
  bare CR separators, while the original `lineOffset` counted only LF and
  the splice also failed to consume a lone trailing CR. PR #306 corrects both
  boundary cases in `src/smb_cnf.c` (`a75ca25`) and executes first/middle
  replacement and first-block removal tests across CR-only, LF, CRLF and mixed
  endings (`adea564`). All unaffected later blocks must be byte-identical.
- **Accepted — independent CWD/SYS-CONF saves (SMB-004):** The original GUI
  wrote the whole primary text onto an existing SYS-CONF copy without checking
  that write. PR #306 now preflights the secondary content, applies a
  share-specific edit separately rather than replacing unrelated shares,
  initializes a missing SYS-CONF file from the validated primary on save,
  checks each `dashWriteFile` result, and distinguishes an unreadable
  secondary (no files changed) from a failed secondary after primary commit
  (partial save). See `8291bc1`, `bda8652`, host save test
  `tests/test_smb_save_integrity.py`, and the CI workflow.
- **Already addressed — SMB2 `-EEXIST` (SMB-002):** PR #306 returns the driver
  error instead of treating the old share NAME as successful connection;
  its host test extracts the actual production `connectSmb2` and checks the
  explicit failed state/error/message (`03c785f`).
- **Already addressed — asymmetric disconnect (SMB-003):** The successfully
  disconnected driver's EE states are reset even if the other driver returns
  an error. The failed driver's session status is *uncertain*, not proof it was
  disconnected; retaining its cached CONNECTED state is deliberate until
  device truth is known. Tests cover both permutations (`03c785f`).
- **Rejected as automatic fixes:** Moving SMB connection into another thread
  contradicts the present driver-preparation/worker-idle contract without
  evidence of thread safety. Adding a blanket disconnect-and-retry button
  could destroy an active transfer. Changing MX4SIO ownership checks on
  speculation risks a slot-2 bus reset. These require targeted hardware/driver
  evidence before implementation.

**Remaining limits:** `smbCnfUpsertText` still discards annotations embedded
*inside* the edited share's key-to-URL span; that must be preserved or rejected
with clear user-facing guidance, not described as lossless. Dual-device writes
are neither atomic nor rollback-safe if a write is interrupted. The preferred
SYS-CONF copy is not equivalent to synchronizing every possible memory-card
slot and CWD location. Driver compatibility, timeouts, active-transfer safety,
partial file reads, and real SMB1/2/3 hardware file operations still need
separate verification. Green CI must not be represented as proof of those
properties.
