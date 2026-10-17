# wLaunchELF_R3Z parity audit — 2026-10-17

**Target:** Launchstation `master` at `ec3723f26c43e04cbfa7227e970302684aed4bbc`  
**Public reference:** [saildot4k/wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) `master` at [`8962830`](https://github.com/saildot4k/wLaunchELF_R3Z/commit/8962830e0cdbb0e0388731d79d3fd36593196241) (last public commit dated September 18, 2026).  
**Unpublished reference:** Photos and Discord conversation supplied by R3Z3N on October 17, 2026, showing `wLaunchELF v4.79_R3Z` features. Screenshots establish **UI observations**, not implementations or published commit identities.

## Evidence and parity matrix

| Observed upstream behavior | Launchstation baseline | Work / verification needed |
| --- | --- | --- |
| Root file browser lists MC0/MC1, USB0, MMCE0/1, MX4SIO, APA HDD0/1, DVR HDD, PSX flash, BDM ATA0/1, CDFS, UDPFS | Already in `src/filer.c`, `src/dash_devices.c` and `src/filer_browser.c` with console/build gating | Do **not** duplicate drivers or force initialization. Audit labels, root ordering, PSX/Deckard gating, lazy mounting and real hardware presence. |
| Advanced options: `Disable EMBER support`, `Disable POPSTARTER support`, `Disable NEUTRINO support` (photo shows NO defaults) | Loader paths in dashboard (`src/dash_settings.c`) and automatic dispatch (`src/main_actions.c`) exist, but no comparable feature-disable settings | First implementation slice: persistent, independently controlled disable switches (default NO) and fail-closed guards at the three automatic launch entry points. Leave direct ELF browsing/launching and normal storage untouched. |
| Disc game-compatibility dialog: fast/synchronous disc reads, unhook system calls, dual-layer DVD emulation and buffer-overrun fix | No equivalent dialog established in published upstream source or in Launchstation's dashboard | **Not verified**. Obtain new upstream source or precise behavior; avoid representing UI placeholders as working syscall/CDVD patches. Trace PS2Disc handoff and design real per-disc implementation with hardware regressions. |
| Video mode `Original (480i/576i)`, video compatibility Off, debug colors NO, PS2 logo NO in the game dialog | Launchstation has its own screen calibration and launch pipeline | **Not verified** for per-game settings. Keep dashboard display settings distinct from per-game video patching; investigate new upstream implementation before porting. |
| Latest private R3Z revisions / release status | Public `master` still `8962830` | Refresh upstream before merge and cross-reference each new commit. Do not claim private code equivalence from photos alone. |

## Implementation policy

1. Preserve the dashboard interface and storage ownership model, including dual USB, MMCE, MX4SIO, i.LINK, HDD, UDPFS and deferred probes.
2. Preserve existing configuration by adding explicit `Disable_EMBER`, `Disable_POPSTARTER`, `Disable_NEUTRINO` fields to `LAUNCHELF.CNF`; missing/old fields mean all loaders remain enabled.
3. A disabled dedicated launcher must refuse its matching `.CUE` / structured Ember path, `.VCD`, or `.ISO` with an actionable message. It must **not** fall through to an unrelated ELF handler, auto-reenable itself, scan devices, or delete settings. The switches are independent.
4. Expose choices under Launching → Disc & image loaders with clear **Enable/disable semantics**. Keep existing custom loader paths intact and use the current settings save/discard transaction.
5. Add regression checks for old CNFs, persistence, three independent gates, normal ELF launches, supported device paths, and unchanged lazy driver startup behavior.
6. Only implement disc compatibility modes after actual R3Z source or tested behavior is available; compatibility bits must have a documented native CDVD/IOP/EE effect, not just a visual checkbox.
7. Review PCSX2 *and* physical PS2 devices; absence of real-console evidence is an explicit blocker to claiming full parity.

## Gate checklist

- [ ] Disable switches available in dashboard and persisted in classic CNF
- [ ] Disabled loader attempts rejected before module load or IOP teardown
- [ ] Old configs and default values preserve existing behavior
- [ ] No driver probing/startup timing changes
- [ ] Host/static regression checks and PS2SDK builds pass
- [ ] Unpublished game-compat/video behavior clarified and implemented
- [ ] Device, game, and core parity verified on real hardware

This document deliberately differentiates **source-verified behavior**, **screenshots**, **work implemented in the PR**, and **future validation**.
