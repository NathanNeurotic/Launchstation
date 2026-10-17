# Consolidated storage-driver lifecycle repair — #172–#175

> **Status: implementation handoff; no production fixes in this plan-only PR.** Do not merge this planning PR as a bug fix. Implement all four issues in this single branch/PR, update this checklist with evidence, and request CodeRabbit review before marking ready. No background test run was independently verified while preparing this plan.

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

- **First change:** replace the 6-attempt `mmce_init()` ping loop with **2 attempts per port**, initially **retaining the known 200 ms transaction timeout**. This limits absent-device ping waiting to a theoretical ~800 ms across two ports. One attempt is too aggressive without measurements; 100 ms per transaction can be tested later on physical SD2PSX/MemCard PRO2/PicoMemcard devices, but should not be bundled blindly with retry-count reduction. Never claim `<200ms` for two ports at 2 × 100 ms.
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
- New focused tests for MMCE ping iteration budget (six -> two), real-card response and two-port handling; if C host stubs cannot model the IOP protocol, make assertions on extracted source and test the actual IRX with hardware.
- Existing `tests/test_mmce_* .py` (actual files use `test_mmce_*.py`), `tests/test_ilink_browser.py`, and relevant device/reset tests must remain green.
- Run Python host suite and required PS2SDK builds (at minimum standard MMCE/MX4SIO/iLink-enabled, relevant options disabled, and DS34 variant where supported). Check CI, capture command/results in the PR. **No suite result is claimed by this planning document.**
- Explicitly verify `embed.make` rebuilds the generated MMCEMAN IRX from changed sources and embeds it in the test ELF; no stale `iop/__precompiled/mmceman.irx` fallback. Inspect generated symbol/size or hashes to confirm.

## Review/merge gates

- [ ] Report exact slowdown/root cause per issue with before/after timings; clearly identify blocking RPC limitations.
- [ ] Patch C/IOP code in **this same PR**, not separate PRs; keep change set limited to verified lifecycle fixes.
- [ ] Use two MMCE ping attempts with initial 200 ms timeout; further reduction requires populated-device hardware evidence.
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
