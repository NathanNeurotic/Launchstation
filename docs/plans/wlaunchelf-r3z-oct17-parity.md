# wLaunchELF_R3Z parity audit — 2026-10-17

**Target:** Launchstation `master` at `ec3723f26c43e04cbfa7227e970302684aed4bbc`  
**Public reference:** [saildot4k/wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) `master` at [`8962830`](https://github.com/saildot4k/wLaunchELF_R3Z/commit/8962830e0cdbb0e0388731d79d3fd36593196241) (last public commit dated September 18, 2026).  
**Development source found:** [upstream `neutrino-and-options-2` at `2c3364a`](https://github.com/saildot4k/wLaunchELF_R3Z/tree/2c3364a3f12e1c5b170b7b49e36ce2e03e4f068d), **9 commits ahead of published `master`**. It contains `src/gui_neutrino.c`, `include/gui_neutrino.h`, new disable switches and the menu matching the October 17 screenshots. The photos are confirmation of this development behavior, not proof of a final release.

## Evidence and parity matrix

| Observed upstream behavior | Launchstation baseline | Work / verification needed |
| --- | --- | --- |
| Root file browser lists MC0/MC1, USB0, MMCE0/1, MX4SIO, APA HDD0/1, DVR HDD, PSX flash, BDM ATA0/1, CDFS, UDPFS | Already in `src/filer.c`, `src/dash_devices.c` and `src/filer_browser.c` with console/build gating | Do **not** duplicate drivers or force initialization. Audit labels, root ordering, PSX/Deckard gating, lazy mounting and real hardware presence. |
| Advanced options: `Disable EMBER support`, `Disable POPSTARTER support`, `Disable NEUTRINO support` (NO defaults) | Dashboard and classic settings now expose independent switches; `config.c` persists them and each loader rejects a disabled image before disk/IOP handoff | **Implemented, CI/hardware verification pending.** Adapted the upstream development-branch configuration design without changing loader-device initialization. |
| Neutrino ISO game-compatibility dialog: fast/synchronous disc reads, unhook system calls, dual-layer DVD emulation and buffer-overrun fix | Existing `src/neutrino_iso.c` handles ISO launch through a keep-IOP path; no per-game menu previously | **Implemented integration, test/hardware verification pending:** imported upstream menu and `-gc=02357` mode mapping, passing actual Neutrino CLI flags via existing `LaunchArgsLoadFromBuffer`, not rewriting Launchstation CDVD drivers. |
| Per-game video modes and compatibility, debug colors and PS2 logo switches | Launchstation has global dashboard screen calibration, distinct from Neutrino game options | **Implemented integration, test/hardware verification pending:** upstream `-gsm=...`, `-dbc`, `-logo` options for Neutrino only. Defaults reset every launch; no new persistent per-game settings. |
| Upstream revision and release status | Public `master` still `8962830`; `neutrino-and-options-2` ahead by 9 commits at `2c3364a` | Backport from explicitly pinned development source; refresh and compare before merge. Development branch is not an official release. |

## Implementation policy

1. Preserve the dashboard interface and storage ownership model, including dual USB, MMCE, MX4SIO, i.LINK, HDD, UDPFS and deferred probes.
2. Preserve existing configuration by adding explicit `Disable_EMBER`, `Disable_POPSTARTER`, `Disable_NEUTRINO` fields to `LAUNCHELF.CNF`; missing/old fields mean all loaders remain enabled.
3. A disabled dedicated launcher must refuse its matching `.CUE` / structured Ember path, `.VCD`, or `.ISO` with an actionable message. It must **not** fall through to an unrelated ELF handler, auto-reenable itself, scan devices, or delete settings. The switches are independent.
4. Expose choices under Launching → Disc & image loaders with clear **Enable/disable semantics**. Keep existing custom loader paths intact and use the current settings save/discard transaction.
5. Add regression checks for old CNFs, persistence, three independent gates, normal ELF launches, supported device paths, and unchanged lazy driver startup behavior.
6. Compatibility and video options now use R3Z's actual documented Neutrino CLI (`-gc`, `-gsm`, `-dbc`, `-logo`); do not confuse **passing flags to Neutrino** with patching native PS2 CDVD/IOP behavior inside Launchstation.
7. Review PCSX2 *and* physical PS2 devices; absence of real-console evidence is an explicit blocker to claiming full parity.

## Gate checklist

- [x] Disable switches available in dashboard and classic advanced UI and persisted in classic CNF
- [x] Disabled loader attempts rejected before module load or IOP teardown
- [x] Old configs and default values preserve existing behavior in code (CI/hardware verification pending)
- [x] No new eager driver probing/startup paths introduced
- [ ] Host/static regression checks and PS2SDK builds pass
- [x] R3Z development-branch game-compat/video UI and command-line behavior ported (physical-console verification pending)
- [ ] Device, game, and core parity verified on real hardware

This document distinguishes **published master**, **public development-branch source**, **screenshot evidence**, **committed changes**, and **unverified runtime/hardware parity**.
