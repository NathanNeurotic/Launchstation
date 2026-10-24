# wLaunchELF_R3Z parity audit — 2026-10-17

**Target:** Launchstation `master` at `ec3723f26c43e04cbfa7227e970302684aed4bbc`  
**Current public reference:** [saildot4k/wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) `master` at [`39fb5bc`](https://github.com/saildot4k/wLaunchELF_R3Z/commit/39fb5bc7700b0d9bf9ed8448f4b9f080b08d81d7) (October 17). [Upstream PR #60](https://github.com/saildot4k/wLaunchELF_R3Z/pull/60) merged Ember and Neutrino options; [PR #61](https://github.com/saildot4k/wLaunchELF_R3Z/pull/61) added Neutrino ISO Game IDs for RetroGem and MMCE.  
**Development provenance:** [former `neutrino-and-options-2` commit `2c3364a`](https://github.com/saildot4k/wLaunchELF_R3Z/commit/2c3364a3f12e1c5b170b7b49e36ce2e03e4f068d) supplied the menu matching the October 17 screenshots. This development work is now merged into public `master`; former branch names can disappear after merge.

## Evidence and parity matrix

| Observed upstream behavior | Launchstation baseline | Work / verification needed |
| --- | --- | --- |
| Root file browser lists MC0/MC1, USB0, MMCE0/1, MX4SIO, APA HDD0/1, DVR HDD, PSX flash, BDM ATA0/1, CDFS, UDPFS | Already in `src/filer.c`, `src/dash_devices.c` and `src/filer_browser.c` with console/build gating | Do **not** duplicate drivers or force initialization. Audit labels, root ordering, PSX/Deckard gating, lazy mounting and real hardware presence. |
| Advanced options: `Disable EMBER support`, `Disable POPSTARTER support`, `Disable NEUTRINO support` (NO defaults) | Dashboard and classic settings now expose independent switches; `config.c` persists them and each loader rejects a disabled image before disk/IOP handoff | **Implemented, CI/hardware verification pending.** Adapted the upstream development-branch configuration design without changing loader-device initialization. |
| Neutrino ISO game-compatibility dialog: fast/synchronous disc reads, unhook system calls, dual-layer DVD emulation and buffer-overrun fix | Existing `src/neutrino_iso.c` handles ISO launch through a keep-IOP path; no per-game menu previously | **Implemented integration, test/hardware verification pending:** imported upstream menu and `-gc=02357` mode mapping, passing actual Neutrino CLI flags via existing `LaunchArgsLoadFromBuffer`, not rewriting Launchstation CDVD drivers. |
| Per-game video modes and compatibility, debug colors and PS2 logo switches | Launchstation has global dashboard screen calibration, distinct from Neutrino game options | **Implemented integration, test/hardware verification pending:** upstream `-gsm=...`, `-dbc`, `-logo` options for Neutrino only. Defaults reset every launch; no new persistent per-game settings. |
| Neutrino ISO `SYSTEM.CNF` Game ID and RetroGem/MMCE | October 17 upstream `master` #61 parses `BOOT2`, signals RetroGem and sends Game ID to MMCE | **Implemented with Launchstation-specific safety gates**: validated ISO parser and RetroGem launch signal; MMCE uses only a pre-existing MMCE stack, excludes ISO/ELF source slots and skips MX4SIO. The upstream unconditional MMCE initialization and driver-reset risks were intentionally not copied. Host test and build verification pending. |
| Upstream revision and release status | Public `master` advanced to `39fb5bc`; feature branches are merged/deleted | Use pinned October 17 `master` commits #60 and #61 for final source comparison; check for new changes before merge. |

## Classic File Browser four-mode parity (PR #177 follow-up)

- Added upstream-compatible numeric display-mode values 0=filenames only, 1=filenames with details, 2=titles with details, 3=titles only. Existing dashboard choices 0/1/2 are preserved.
- Added shared `fileShowTitles()` and `fileShowDetails()` predicates. Classic Circle toggles filename-only/details and Square toggles title-only/details. Title extraction, Shift-JIS row height, PSUs and sorting use the correct predicate rather than `file_show == 2`. No device probing changes.
- Added executable host four-state predicate test and updated the older upstream sorting harness so it compiles the new *production* predicates.
- **Concurrent PR #178 owns `src/dash_browser.c`.** Companion PR #178 now implements the fourth native Files mode at `2765787`, including title loading, independent details visibility, filename fallback and legacy-operation mapping. This branch alone retains three native choices; the combined source is ready for integration checks, while merge and console acceptance remain pending.
- Follow-up real hardware acceptance: both classic title-only/no-details and title+details with MC/PSU/USB/MMCE sources, font failures, date sort, selection and long names.

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
- [ ] Host/static regression checks and PS2SDK builds pass on the final head (CI pending)
- [x] R3Z development-branch game-compat/video UI and command-line behavior ported (physical-console verification pending)
- [ ] Device, game, and core parity verified on real hardware
- [x] Game-ID parser and opt-in RetroGem/MMCE integration ported from upstream #61, without automatic MMCE driver-stack changes

This document distinguishes **published master**, **public development-branch source**, **screenshot evidence**, **committed changes**, and **unverified runtime/hardware parity**.

## Dashboard presentation follow-up

Dashboard launches now use the existing dashboard modal menu for per-launch Neutrino options, preserving the scene, input mapping and normal modal maintenance. Classic launches retain the upstream dialog. A compiled host harness exercises all option types, Run, Cancel and submenu cancellation. Standard local PS2BUILD v2026.10.13 compilation and ELF packing pass; console launch and display acceptance remain pending.

## Upstream October 17 follow-up: #60 and #61

- **#60 Ember/options** is now merged upstream. Launchstation already had Ember, POPStarter, Neutrino, MMCE/USB, and guarded image-format routing; avoid replacing this with upstream's less integrated launch stack. The three new disable toggles, Neutrino options, and classic UI were ported under this PR.
- **#61 ISO Game ID** uses bounded ISO9660 parsing to find root `SYSTEM.CNF` and `BOOT2`. Launchstation now uses that parser for RetroGem independently of MMCE compilation. The read occurs after per-launch confirmation and before candidate preparation, so image paths are still stable. Unknown/malformed IDs are treated as absent.
- **MMCE safety divergence**: R3Z calls `loadMmceModules()` even when other drivers may own SIO2, and on a valid title polls for up to 15 seconds per slot. Launchstation never automatically loads/switches the stack solely for Game ID; it only issues optional game-ID commands on a pre-existing MMCE stack. It skips the card containing the selected ISO or Neutrino ELF (including ambiguous path aliases), and any MX4SIO sources. The bounded wait happens only after a responding card accepts SET_GAMEID.
- This preserves MMCE/game-ID capability when the relevant stack is already active, without introducing startup probes or taking down dual-USB/network mounts. It is **intentionally not byte-for-byte upstream parity**, pending console validation of IOP lifetime and card switching behavior.
- Automated executable host fixture: `tests/test_r3z_neutrino_gameid.py` covers successful ID, malformed ISO, missing/invalid title, and MMCE slot exclusion/driver-presence behavior.
