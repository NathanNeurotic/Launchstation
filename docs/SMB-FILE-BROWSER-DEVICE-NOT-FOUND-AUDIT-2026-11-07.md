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

### C. Driver connection failure has two independent reasons that are not
   distinguished to the user

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
  then `mc1`, then `mc0` (swapping starting slot), and skips an mc1 read when
  `mc1AccessRefused()`.
- A config saved manually or by another tool in a place not covered by those
  rules is invisible, even though the UI "configured correctly".
- `smb_cfg_path` is the only reported path, and multi-file mirroring (see
  recent `#297`) creates confusion about which file won.
- `smbReadConfig` keeps a pending mc1 state:
  - `smb_cfg_loaded = !mc1_refused || smb_cfg_path[0]` (line 131).
  - `smbSharesKnown(v)` (`src/smb_shares.c:149-151`) returns 0 while pending.
  - Dashboard V1 entry is therefore hidden/missing on first pass:
    `src/dash_devices.c:438` uses `smbSharesKnown(SMB_PATH_V1)`.
    Warm-up calls `smbSharesConfigured` for V1 only:
    `src/dash_main.c:1185`, while V2 is never warmed at startup.
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

- `tests/test_smb_connect_state.py` extracts `smbSharesConnect` but not the
  newly added static helper `smbDisconnectAllSessions`. Current source calls
  `isSmbDriverLoaded` from that helper, which the test mock does not declare.
  The test therefore fails to compile against `0814f5f`.
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
- Warm-up only initializes V1 (`src/dash_main.c:1185`); V2 relies on later
  access, so "configured" state appears asynchronously.
- Device-less progress/toast state is mixed with File Browser state; the generic
  "Device not found" fallback remains a catch-all.

## 3. Recommended implementation order for the clean solution

1. Change File Browser to always show the specific failure reason for SMB before
   the generic fallback. Recent `smbStatusMessage` is a start, but it should be
   the only path for SMB device activation.
2. Make an explicit SMB device state enum: `unconfigured`, `driver unavailable`,
   `configured but disconnected`, `connected`, `failed`, with `problem`.
   Replace the present/absent bit for SMB.
3. Add deterministic config-first validation before talking to IOP:
   - If zero configured shares, report that exactly.
   - If driver cannot load, report the specific environment reason.
   - Then connect and report per-share result.
4. Separate config discovery errors from network errors:
   - Distinguish "No SMB2.CNF found" from "SMB2.CNF has no valid share".
   - Report the file path that was read.
5. Remove 30-second suppression from explicit user-initiated Open; retain backoff
   only for asynchronous retries.
6. Make reload atomic with error reporting; never leave a failed reload looking
   successful.
7. Warm both SMB versions at startup or make the UI clearly "not read yet".
8. Allow DNS hostnames or reject them with a precise message instead of a false
   config parse failure.
9. Fix host tests/mocks to track newly added helpers and run them in CI with a
   toolchain that supports sanitizers.

## 4. Verification needed for the future implementation

- Unit host tests for `readSMB`, `activate`, and `dashDevicePathReady` SMB paths
  with driver load success/failure and share connect success/failure.
- Integration test for config file discovery precedence and multi-file mirror.
- Integration test for reload after editing a share within 30 seconds.
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

- `smbedWriteFile` (`src/dash_settings.c:490-551`) reads only the first config path
  (`smbedConfigPath`, line 504) and writes there first (line 536).
- SYS-CONF is written only if it already exists (`genOpen` succeeds, line 541).
- If the first write succeeds and the second mirror fails, the PS2 can later read a
  stale `SMB2.CNF` from either location.
- There is no rollback or atomic update across the two locations.
- In particular, if CWD has a readable file but SYS-CONF has an older readable
  file, `smbSharesReload()` may load one while `smbedWriteFile` has updated only
  the other before the mirror step takes place.

### K. `smbSharesReload()` treats a single failed disconnect as total failure

- `smbSharesDisconnectAllSessions` (`src/smb_shares.c:94-107`) returns 0 if either
  V1 or V2 devctl returns an error, even when the program-level intent was to
  retire sessions before reparsing.
- Consequently `smbSharesReload()` (`src/smb_shares.c:370-378`) refuses to
  forget state and reload config (`ret = -6` path in settings).
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
- But warming at boot only reads V1 (`src/dash_main.c:1185`), not V2.
- V2 device entries therefore remain in an unknown/read-not-done state until a
  path is opened, which is indistinguishable from "no shares configured" in
  some UI states.
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