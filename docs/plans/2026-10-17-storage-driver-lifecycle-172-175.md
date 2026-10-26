# Consolidated storage-driver lifecycle repair — #172–#175

> **Status as of 2026-10-26: partially implemented and merged (PR #176, `f29a28f`); the "not merge ready" banner below is obsolete.**
> In tree: `MMCE_PING_ATTEMPTS 2` with the unchanged 200 ms timeout (`iop/mmceman/src/mmceman.c`, the 100 ms
> option was *not* adopted), pre-RPC loading frames (`startSync()` in `src/dash_devices.c`), generation-safe
> audio shutdown before `Reset()` (`resetRuntimeDeviceState()` in `src/init.c`), no raw mount IDs in the
> generic missing-device modal, and refused-reset guards (`switchStorageDriverStack()` returns 0). Follow-ups:
> DEV9 refusal ([2026-10-18-dev9-transition-follow-up.md](2026-10-18-dev9-transition-follow-up.md)) and
> [2026-10-19-zack-issues.md](2026-10-19-zack-issues.md). **Still open:** continuous animation during blocked
> synchronous IOP calls is impossible by design; root-cause proof of #172-#175 stalls and mixed-storage,
> cold-boot and power-off behavior need real-console evidence.


> **Status: partial implementation on PR #176; not merge ready.** Safe initial EE fixes and focused host regression tests have been committed in this same branch. The critical MMCE residency/physical-device hang investigations remain open. Continue implementation in this one PR; request CodeRabbit review and hardware validation before marking ready.

## Adjudication of Gemini's alternate blueprint (2026-10-17)

This section **supersedes contradictory implementation instructions** from the two draft plans. The canonical handoff is **this file in PR #176**; do not create Gemini's suggested second branch `fix/zack-storage-driver-loading-and-audio` or a second PR.

| Proposal | Decision | Code evidence and rationale |
| --- | --- | --- |
| Two MMCE ping attempts, not one | **Adopt** | `mmce_init()` currently retries six times in both physical ports 2 and 3; a second attempt reasonably tolerates a missed handshake without six absent-device waits. |
| Reduce ping timeout to 100 ms | **Conditional optimization, not initial baseline** | `mmce_cmd_ping()` passes `&timeout_200ms` to `mmce_sio2_tx_rx_pio()`. The 100 ms value would be `0x384000` in the same IOP clock units as `0x708000` for 200 ms, but no physical trace here proves reliable responses within 100 ms on every supported card, boot mode, firmware and SIO2 configuration. |
| Claim live MMCE pings always reply in under 1 ms | **Unverified** | Neither issue reports nor code provides measured cold-boot/late-readiness response distributions. Instrument and test before relying on this to reduce timeout. |
| Claim `inl_sio2_ctrl_set(0x3ac)` drops the first command | **Plausible hypothesis; not established cause** | `mmce_sio2_tx_rx_pio()` resets SIO2/FIFOs with this value before transfers, including retries. Do not assert it loses a command without a hardware trace. |
| Claim two 100 ms attempts take *under* 400 ms overall | **Reject as a strict upper bound** | Two slots × two attempts × 100 ms = **400 ms nominal maximum alarm waiting**, plus IOP module startup, SIO2 work, file-system mounting and scheduling overhead. Real end-to-end duration may exceed 400 ms. |
| Unconditionally demote to `STORAGE_STACK_DEFAULT` when `mmce0:/` and `mmce1:/` absent | **Reject** | Unmounted storage != unloaded module; `SifExecModuleBuffer(mmceman_irx)` can install SIO2 hooks and filesystem registration while no card is attached. `switchStorageDriverStack()` needs accurate residency to reset or unload incompatible drivers safely. |
| Skip resets whenever prior stack has no verified media | **Reject as blanket rule** | Unpopulated MMCE/MX4SIO stacks can still own SIO2 resources. Avoid unnecessary resets **only** when an actual safe unload/no conflicting resident driver has been proven. |
| Show frames before/while a synchronous load is blocked | **Partial adopt** | Pre-render status and render again whenever module/RPC call returns. Calling `dashLoadingFrame()` in a worker or while the main thread is blocked cannot achieve continuous animation. No new scheduler or asynchronous IRX loader without evidence. |
| Shorten iLink/MX4SIO probe window to 1000 ms | **Do not default** | `probeRoot`'s window begins/ends around **returned** failed `fileXioDopen` calls, not a hard timeout for one hung RPC. Preserve startup tolerance until real delayed-mount measurements support shortening. |
| Set `DelayThread(100000)` to `DelayThread(25000)` | **Optional after measurement** | Speeds the interval between returned attempts but does not address time spent inside `SifExecModuleBuffer` or `fileXioDopen`; throttle draw calls rather than targeting 40–60 FPS without synchronous progress. |
| Teardown audsrv before IOP reset; recover after reinit | **Adopt with generation guards** | `dashSoundShutdown()` must run while old audsrv server exists, never after generation change. Use existing `dashSoundMaintain()`/BGM path after PAD/keyboard recovery and no worker contention; do not force immediate reinit during reset. |
| Pass `NULL` for generic device-not-found modal detail | **Adopt** | Avoid exposing raw `mmce0:`, `ilink:`, etc. in user-facing dialog. Keep useful device-name-specific guidance. |
| Add `tests/test_zack_storage_drivers.py` | **Adopt** | Include targeted **behavioral** driver-probe, safe-stack-transition, audio lifecycle, user-facing error and ping-budget tests rather than brittle source-only checks; update existing integration-style host stubs. |

**Final implementation default:** `MMCE_PING_ATTEMPTS = 2`, with the existing `timeout_200ms` for both attempts and each port. Nominal timeout waiting when both ports have no MMCE: **~800 ms maximum** instead of **~2400 ms** (not an end-to-end timing guarantee). An isolated 100 ms ping timeout is allowed only as a *separately validated follow-up commit in this same PR* when real SD2PSX, MemCard PRO2 and available PicoMemcard hardware pass repeated cold boot, first-try, slow-start and reconnect trials. If any regressions occur, retain 200 ms. Do not change the timeout for unrelated MMCE commands.

**Additional cross-check:** current vendored `iop/mmceman/src/sio2man_hook.c` already has `PORT_NR` disabled; upstream `ps2-mmce/mmceman` merged an mc1/unblock fix on 2026-08-01. Do not reintroduce port blocking or blindly cherry-pick an already-equivalent upstream fix. The upstream MMCEMAN `mmceman.c` still uses six attempts and `mmce_cmd_ping` still uses a 200 ms timeout; our reduction is Launchstation-specific and needs hardware verification. Upstream docs also caution about SIO2MAN version/driver compatibility. See [upstream MMCEMAN](https://github.com/ps2-mmce/mmceman) and [the mc1 fix](https://github.com/ps2-mmce/mmceman/commit/21dbc0a7e5640b4d77520ab362dab62bafccacd8).

## Changes already implemented in PR #176

- `src/dash_files.c`: the **generic missing-device error modal** passes `NULL` as detail, hiding raw mount IDs. The existing `deviceDisplayMount()` helper **must remain** for normal device panels and file-picker paths (including MASS unit labels).
- `src/dash_devices.c::startSync()`: pre-render a loading-status frame before synchronous `probeMount()` starts. This does **not** keep animation moving during a blocked IOP RPC.
- `src/init.c::resetRuntimeDeviceState()`: invoke generation-aware `dashSoundShutdown()` after the early blocked-reset guard and root libmc poll drain, **before** `Reset()` and PAD teardown; the main-loop maintenance path restores audio after the IOP generation advances.
- Updated the production-reset execution harness `tests/test_usb_iop_pad_handoff.py`, the retained MASS-label + modal contract `tests/test_zack_feedback.py`, and added the compiled host behavior harness `tests/test_zack_storage_drivers.py`, which runs in CI.
- No `MMCE_PING_ATTEMPTS`, transaction timeout, module-residency tracking, iLink/MX4SIO wait-window or mount-switch policy change has been committed. These require further implementation and/or real hardware evidence.
- GitHub Actions host tests and documentation checks passed on the safe-fix commit set. Standard/DS34 cross-compilation and packaging must be checked on the final branch SHA; prior CI failures from an obsolete modal test and an inadvertently deleted shared display helper have been corrected.

## Provenance and scope

- Reproduction reports: [#172](https://github.com/NathanNeurotic/Launchstation/issues/172) iLink missing-device loading stalls procedural backgrounds; [#173](https://github.com/NathanNeurotic/Launchstation/issues/173) MX4SIO missing-device loading stalls; [#174](https://github.com/NathanNeurotic/Launchstation/issues/174) MMCE missing-device loading stalls, resets IOP and leaves sound muted; [#175](https://github.com/NathanNeurotic/Launchstation/issues/175) MMCE activation interferes with subsequent storage activation and produces a raw/debug-looking dialog.
- Reporter: zackcage6; console: SCPH-30001R; all four reports cite `ec3723f`, the `master` head verified on 2026-10-17.
- Goal: safe, predictable absent-device behavior *without* breaking present MMCE/MX4SIO/iLink devices, PAD, USB (including dual-USB), storage switching, audio, or per-device discovery. No automatic device scanning on render.
- Exclusions: wholesale driver-stack rewrite, new generic scheduler, speculative timeout changes, and parallel IOP module starts.

## Verified source facts (not yet verified root causes)

1. `src/dash_devices.c`: `startSync()` calls `probeMount()` synchronously under a loading overlay. `probeRoot()` calls potentially blocking `fileXioDopen()` and then sleeps 100 ms between *returned* unsuccessful calls. iLink and MX4SIO pass nominal 3000/2000 ms wait windows, respectively; those are **not RPC deadlines**.
2. `src/dash_devices.c` explicitly explains that module-loading on the background worker previously hung on real hardware. It intentionally finishes workers and loads IOP modules on the UI thread. Do **not** move `SifExecModuleBuffer` onto `dash_worker` without first proving safety on hardware.
3. `src/init.c`: successful `SifExecModuleBuffer(mmceman_irx)` sets `have_mmce` and `STORAGE_STACK_MMCE`, regardless of a later `mmce0:/` / `mmce1:/` mount result. `switchStorageDriverStack()` resets the IOP when changing away from a non-default resident stack. Thus **loaded driver != attached media**; simply setting `storage_driver_stack_mode = STORAGE_STACK_DEFAULT` after an absent probe would lie about resident hooks and could corrupt a later stack switch.
4. `iop/mmceman/src/mmceman.c`: `mmce_init(2)` and `mmce_init(3)` each perform **six** pings. `iop/mmceman/src/mmce_cmds.c` uses `timeout_200ms` per ping, so absent cards can consume up to ~2.4 s in ping waits across two ports *before* other initialization overhead. This is a theoretical budget, not measured boot timing.
5. `src/init.c::resetRuntimeDeviceState()` waits for root-card polls, tears down keyboard/PAD, calls `Reset()`, then restores PAD/keyboard; there is presently no explicit `dashSoundShutdown()` in that function. `src/dash_sound.c` already has generation guards, stale-state clearing in `dashSoundShutdown()`, and deferred restoration in `dashSoundMaintain()`. Incorrectly issuing audsrv RPC **after** reset can hang.
6. `src/dash_files.c::deviceNotFound()` supplies `deviceDisplayMount(...)` as the fourth `dashModalMessage` argument, exposing mount identifiers in error UI.
7. iLink uses the independent BDM transport, not the MMCE/MX4SIO SIO2 stack. `src/init.c::loadIlinkModules()` already describes its required BDM -> iLinkman -> IEEE1394_bd order.
8. `embed.make` selects generated `iop/__generated/mmceman.irx` when local `iop/mmceman/Makefile` exists; ensure a source change really rebuilds and embeds that IRX. The bundled precompiled module must not silently mask results.

## Implementation sequence (one PR, separate reviewable commits)

### A. Reproduce and instrument FIRST (#172–#175)

- Capture timestamps and IOP reset generation immediately before/after `SifExecModuleBuffer`, `fileXioDopen`, stack transitions and `Reset()`, using existing guarded diagnostics; identify whether hangs happen inside a blocking RPC/module entry or the between-attempt polling loop.
- Record MMCE per-port ping outcomes/elapsed time, installed-vs-mounted state, and exact IOP-generation changes. Record audsrv state immediately before/after reset and after return to main loop. Do not flood on-screen UI or expose raw mount paths.
- Reproduce each case on `ec3723f` / SCPH-30001R: no hardware for each transport; valid MMCE in each supported slot; MX4SIO populated; real iLink storage; combinations. Retain before/after timings in PR, not as unsupported assumptions.
- Inspect `Reset()` / `iopResetNotify` and client lifetime; preserve the existing `iopResetBlocked` guard and root-card drain ordering. Distinguish cases where a call never returns: `probeRoot` wall-clock deadlines cannot terminate a blocked RPC.

### B. Loading feedback and bound *returned* probes (#172, #173, #174)

- In `startSync()`, present a final status frame **before** beginning each blocking operation, then resume rendering after control returns; use existing `dashLoadingBegin/Status/Frame` rather than a new UI framework. Preserve loading overlay and themes.
- Preserve existing UI-thread ownership for IOP-module loading; only main thread renders. Never call `dashLoadingFrame` from the IOP worker. If module execution truly blocks for seconds, explicitly document that a synchronous main-thread call *cannot* animate at 40–60 FPS until it returns. Do not claim changing `DelayThread(100000)` to 25000 fixes that.
- If instrumentation locates the delay in `probeRoot`'s **returned** unsuccessful calls, consider 25 ms sleeps and frame pacing/throttling for responsiveness. Avoid unconditional frame presents every iteration or busy spinning. Keep a short settling window for legitimate slow-to-mount BDM devices.
- Re-evaluate 3000/2000 ms iLink/MX4SIO root wait windows based on measured attachment latency. Do not immediately halve them to 1000 ms: legitimate devices might require startup time. These windows are *not* hard timeouts for `fileXioDopen`.
- Check all `dashDevicesStart` consumers (Files, browser, picker, auto-start) and avoid resetting or publishing a success callback for failed probes.

### C. MMCE absent-card and storage-stack semantics (#174, #175)

- **First change:** define a named retry-count constant (suggest `MMCE_PING_ATTEMPTS = 2`) and replace the 6-attempt `mmce_init()` ping loop with **2 attempts per port**, initially **retaining the known 200 ms transaction timeout**. This limits absent-device ping waiting to a theoretical ~800 ms across two ports. One attempt is too aggressive without measurements; 100 ms per transaction can be tested later on physical SD2PSX/MemCard PRO2/PicoMemcard devices, but should not be bundled blindly with retry-count reduction. Never claim `<200ms` for two ports at 2 × 100 ms.
- Ensure a successful response from a *real* MMCE device still goes through filesystem initialization. A temporary non-response must not permanently hide a card or create an endless retry loop. Provide a user-initiated reprobe/reconnect path; keep once-per-request and per-IOP-generation probe caching sensible.
- **Separate** `module installed / SIO2 hooks resident` from `a physical MMCE filesystem mounted`. Use `probeRoot("mmce0:/", 0)` and `probeRoot("mmce1:/", 0)` (or appropriate bounded readiness when justified) to establish the latter; do not interpret `have_mmce` alone as presence.
- **Never just demote `storage_driver_stack_mode` after an empty MMCE probe.** The IRX installed SIO2 hooks even without a card; changing its state flag alone does not unload hooks. Evaluate a *verified safe* driver unload/teardown of the MMCE module (its `__exit` invokes `mmce_fs_unregister()` and `mmce_sio2_deinit()`; check correct module ID retention, unwind ordering and whether PS2SDK actually supports unloading this module safely). If unload cannot be proven safe, keep honest residency tracking and perform **one guarded, fully recovered IOP reset only when an incompatible stack is actually requested**, not during mere absent-device browsing or enumeration. Do not reset just to cosmetically return to a default flag.
- Maintain the MMCE / MX4SIO mutual exclusion in `safeAutoStartMask()`. Ensure present boot-source MMCE/MX4SIO paths are not torn down, and ensure USB, ATA/HDD, BDM, UDPFS, keyboard and controller lifetimes recover after any necessary reset.
- Avoid re-triggering the same absent probe repeatedly within a single session or auto-start pass. Preserve `dashDevicesAutoProbe`'s generation replay (bounded to two passes) and do not manufacture false READY states.
- Verify both slot addresses and the default memory-card handling. This code touches SIO2/PAD, so regressions may manifest as dead wired controller input or analog LED.

### D. Correct audio lifecycle on every *legitimate* reset (#174)

- If a reset is really required, call `dashSoundShutdown()` **before** `Reset()`, while the old audsrv service is still alive and it is safe to execute its RPCs; also retain its existing stale-generation behavior (clear local buffers and state without calling dead IOP RPC). Coordinate ordering with `iopResetNotify`, root-card drain, worker gating and PAD closure. On blocked reset paths, do **not** tear down sound.
- Avoid duplicate audsrv unload/initialization or calling `audsrv_*()` while the IOP is resetting. Re-arm once-per-generation sound maintenance and restore UI SFX and BGM using the existing `dashSoundMaintain()`, `dashSoundApplyVolume()` and `dashBgmStart()` on the UI main loop *after* PAD/keyboard and driver work have settled.
- Respect `dashCfg.sound` and `dashCfg.bgm`; disabled audio must remain disabled, sidecar loading must remain deferred while workers own IO, and failed application launches must preserve the no-unnecessary-RPC-shutdown behavior.
- Add explicit checks that an absent MMCE probe alone does not cause an IOP reset or silence audio, that a necessary MMCE->MX4SIO transition safely recovers both, and that subsequent transitions do not endlessly reload audsrv.

### E. Fix user-facing device errors (#175)

- In `src/dash_files.c::deviceNotFound()`, eliminate the raw `mmce0:`, `mmce1:`, `ilink:` identifiers from the display's technical/path argument where not useful; for this generic 'device not found' dialog, pass `NULL` as the fourth argument after confirming `dashModalMessage` accepts a null path (other call sites do). Keep the descriptive `d->name` and targeted guidance; retain detailed diagnostics in logs, not user-visible message bodies.
- Ensure a later storage start failure returns a clean status, not a stale path from a prior device or a debug-like modal. Exercise absent/present UX for each listed transport.

## Regression and validation matrix (required before changing draft to ready)

| Scenario | Expected result |
| --- | --- |
| None of MMCE, MX4SIO, iLink connected | Each reports unavailable promptly; no unexplained MMCE-triggered reset, no raw mount strings, no sound loss or stuck PAD; theme draws around returned operations |
| Valid MMCE in slot 1 / slot 2 | Discovery works on first/manual reprobe, files can be browsed, controller and BGM remain stable |
| MMCE absent -> MX4SIO present; MX4SIO present -> MMCE | Correct true-residency handling, at most the necessary guarded reset, no false READY, actual files accessible, PAD and sound recover |
| MX4SIO absent / populated | Missing device returns; populated delayed-mount hardware is not accidentally timed out; no stale mount |
| iLink absent / populated | No cross-stack reset; true iLink data accessible on physically present device |
| Single USB / two USB drives, ATA/HDD, UDPFS | Remain discoverable after any stack reset; mount masks and auto-start recover; no regression in multi-device startup |
| Existing MMCE or MX4SIO boot source | Cannot unload/reset itself unsafely; booted browser and launched apps remain operable |
| Repeated manual probes + auto-start mixed configuration | Bounded attempts; no uncontrolled extra IOP resets, no persistent stale state |
| Audio off / SFX only / BGM only / both | Settings preserved; no stale audsrv RPC; BGM reloads after a legitimate IOP reset and never starts while intentionally off |
| DS34 input and wired PAD | No controller loss, stuck analog light, keyboard loss or async mcGetInfo lifetime breach |

### Host tests to update/add

- `tests/test_usb_iop_pad_handoff.py`: update the exact teardown/reinit ordering assertion if adding audio shutdown; assert the blocked-reset path performs *zero* side effects, and pre-reset sound teardown precedes `Reset()`. Do not lose existing root-card / PAD invariants.
- `tests/test_device_input.py`: prove absence does not imply resident module, failed probe stays ABSENT, cache invalidates on generation changes, correct port-specific MMCE results, and auto-start's MMCE/MX4SIO preference.
- `tests/test_bgm_sidecar.py`: generation-safe shutdown/reinit and SFX/BGM recovery after genuine stack switch, no audsrv use after generation change, no repeated attempts per frame.
- `tests/test_modal_animation.py` and `tests/test_usb_probe_status.py`: verify a frame/status is presented **before** a blocking driver call and after it returns, without asserting impossible concurrent animation during a synchronous call.
- Add `tests/test_zack_storage_drivers.py` for the combined four-issue contract; cover absent-slot ping iteration budget (6 -> 2), per-port response, SIO2 clock budget interpretation, honest module-vs-media residency, guarded reset + audio lifecycle, non-technical missing-device messages, and loading frames around returned calls. Prefer behavioral harnesses over source-only assertions; where IRX dependencies cannot run on host, assert narrow source contracts and test actual IRX on hardware. Existing focused tests must also continue passing.
- Existing `tests/test_mmce_* .py` (actual files use `test_mmce_*.py`), `tests/test_ilink_browser.py`, and relevant device/reset tests must remain green.
- Run Python host suite and required PS2SDK builds (at minimum standard MMCE/MX4SIO/iLink-enabled, relevant options disabled, and DS34 variant where supported). Check CI, capture command/results in the PR. **No suite result is claimed by this planning document.**
- Explicitly verify `embed.make` rebuilds the generated MMCEMAN IRX from changed sources and embeds it in the test ELF; no stale `iop/__precompiled/mmceman.irx` fallback. Inspect generated symbol/size or hashes to confirm.

## Review/merge gates

- [ ] Report exact slowdown/root cause per issue with before/after timings; clearly identify blocking RPC limitations.
- [ ] Patch C/IOP code in **this same PR**, not separate PRs; keep change set limited to verified lifecycle fixes.
- [ ] Use two MMCE ping attempts with initial 200 ms timeout; add a separately reviewed 100 ms experiment only if repeated real-hardware cold-boot/reconnect evidence establishes safety on supported MMCE types.
- [ ] No false stack-mode demotion, unsafe module unloading, hidden/unchecked `Reset()`, or background `SifExecModuleBuffer`.
- [ ] Validate audio, PAD, keyboard, IOP generation and multi-storage scenarios.
- [ ] Host tests + PS2 builds + actual SCPH-30001R cases documented.
- [ ] Address CodeRabbit comments individually and request review.
- [ ] Merge only after verified fixes, code review and CI. Close #172–#175 only if all corresponding behaviors are demonstrably corrected.

## Pointers

- `src/dash_devices.c`: `probeRoot`, `probeMount`, `startSync`, `dashDevicesAutoProbe`, `safeAutoStartMask`
- `src/init.c`: `resetRuntimeDeviceState`, `switchStorageDriverStack`, `loadMmceModules`, `loadMx4sioModules`, `loadIlinkModules`, `getIopResetGeneration`
- `src/dash_sound.c`: `soundAlive`, `dashSoundShutdown`, `dashSoundMaintain`, `dashBgmStart`
- `src/dash_ui.c`: `dashLoadingBegin`, `dashLoadingStatus`, `dashLoadingFrame`
- `src/dash_files.c`: `deviceNotFound`
- `iop/mmceman/src/mmceman.c`, `iop/mmceman/src/mmce_cmds.c`, `iop/mmceman/src/mmce_sio2.c`
- `embed.make` and `iop/mmceman/Makefile` for generated IRX contents.

## October 17 implementation follow-up

- MMCE startup now uses `MMCE_PING_ATTEMPTS = 2` per port, retaining the 200 ms ping timeout. The existing filesystem discovery/reconnect path remains available; this changes startup retries only. Host C tests execute absence, first-attempt success, and second-attempt success on both ports. Physical cold-boot and slow-card acceptance is pending.
- `switchStorageDriverStack()` returns failure when a required reset leaves the previous non-default stack resident. Both MMCE and MX4SIO loaders stop before loading another driver. Host C tests execute no-reset, same-stack, refused-reset and completed-reset transitions. This fixes a confirmed failure path; it does not prove the reported main-thread storage delays resolved.
- Local host reset/PAD and USB lifecycle harnesses pass. Initial local cross-build reached MMCEMAN compilation but stopped at absent mbedTLS headers; final complete-build evidence is recorded separately.
- The UI thread still owns module entry. No claim of animation during blocked IOP RPCs, shorter iLink/MX4SIO mount tolerance, or physical adapter validation.


## Follow-up: MMCE presence command and refused ETH/UDPFS reset

`mmce_fs_dopen()` may start a longer filesystem transaction for an
unresponsive or absent MMCE card. Dashboard probing now issues up to two
`MMCE_CMD_PING` devctl attempts to that physical unit (using the
existing 200 ms command timeout) before entering root-directory probing.
Absent cards bypass the longer root open. A positive ping is **not** a READY
state: the root still must open. A new explicit probe can find a card added
after an earlier absence. Resident SIO2 hooks are never falsely demoted.

`switchNetworkStack()` now propagates a blocked/refused IOP reset:
ETH and UDPFS launch paths return instead of relabeling resident modules
and loading an incompatible network stack. Focused production-function
host tests and the existing MMCE/IOP lifecycle tests cover these cases.
Real-world latency, poweroff, audio, USB/PAD recovery, and delayed mount
acceptance remain hardware acceptance gates; this does not make
synchronous IOP RPCs animate.

## Host-verifiable sound failure recovery

`tests/test_sound_recovery.py` executes the production initializer, maintenance
and shutdown functions with controlled module, RPC and sample-upload failures.
It covers IO/park deferral, one automatic attempt per IOP generation, recovery
on a new generation, and stale-server teardown with zero old-generation RPCs.

The harness exposed a real failure path: successful audsrv/ADPCM initialization
followed by failure of every SFX upload returned before marking the service
alive. That prevented independent BGM initialization and hid the live RPC client
from pre-reset shutdown. Keep the healthy service alive, leave rejected samples
silent, and warn once. Module/RPC initialization failures still return failure.
This is host evidence for lifecycle policy, not physical SPU2 acceptance.
