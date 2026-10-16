# Plan: early PS2 PC CARD CompactFlash support (Launchstation)

**Status:** Driver foundation implemented; runtime integration pending. **Scope:** Launchstation native Files and Devices integration; independent from RiptOPL game loader. **Target base:** `master`.

## Purpose and non-goals
Plan-only draft PR for native CompactFlash/SD-to-CF access via the *original PS2 PC CARD slot*, based on [mihhru/PS2CF 1.0](https://github.com/mihhru/PS2CF). Other agents should add **implementation commits to this same branch/PR**, updating this checklist as evidence accumulates. **No production code is implemented by this initial commit**. No firmware changes, memory-card image installation, FMCB packaging, blanket storage replacement, or existing save modification are in scope.

Target hardware: SCPH-10000 (PS2CF hardware tested), SCPH-15000 / SCPH-18000 (targeted but unconfirmed separately). Exclude expansion-bay PS2, Slim, and DESR unless separate hardware evidence establishes compatibility. CF/ATA through a compatible PCMCIA Type II adapter, optionally MicroSD via SD-to-CF bridge. Supported storage filesystem goal: MBR FAT32/exFAT, ordinary OPL/launcher file semantics. Hardware/bridge/card combinations remain unverified until independently tested.

## Shared architecture / detection contract
1. Reuse cached `rom0:ROMVER` and, where available, actual console model as a **cheap eligibility hint**, not proof of PCMCIA or storage. Do not assume all ROM 1.x machines are PC CARD; unknown ROMVER must not create a false positive. Preserve existing `rom0:PSXVER` special casing.
2. Do a read-only, bounded hardware-family check of DEV9 revision (`0xBF80146E` / `REV & 0xF0`): PS2CF's retail PCMCIA path expects `0x20`, whereas expansion-bay `0x30` is not this driver. Only perform register access on a compatible mapped/known-safe hardware path; reject unknown statuses rather than touching control registers.
3. **PCMCIA != CF**: an early LAN PC CARD or empty slot is not a CF device. Before any power, reset, CIS/configuration, or ATA probe, determine ownership. Never seize an active DEV9/network/HDD/other driver. Build an explicit mutually exclusive ownership policy for the PC CARD bus; if busy, report unavailable without changing it. Investigate how to obtain a truthful owner state rather than depending on ROMVER alone.
4. On the permitted path, perform a bounded, recoverable, **driver-owned** PC CARD presence + CIS/CF memory profile and ATA IDENTIFY probe; validate sector capacity, supported LBA28/48, and a FAT32/exFAT MBR partition. Reuse PS2CF's conservative timings, bounded transfers, read fallback, and write verification where applicable. Restore all modified registers on failure; never reset unrelated hardware. Only register `cf:/`/CF BDM when a compatible card is ready.
5. Device lifecycle: absent/unrecognized media -> disabled/unavailable; failed mount -> explicit reason; failed read/write/removed card -> clean error, invalidate handles, no aliasing a different storage device. Power-off/reboot/IGR paths must not leave stale registration or an active bus owner. Do not silently auto-retry destructive initialization. Support manual refresh after a failed detection; preserve other device stacks.
6. Test cross-device interaction and *boot recovery* before enabling auto-start. No global disabling of Ethernet, UDPFS, SMB, HDD, MMCE, MX4SIO, USB, iLink, Ember, or RetroAchievements. Hardware-specific conflicts may require explicitly unavailable combinations and informative UI rather than pretending unsupported concurrency works.
7. Treat PS2CF's v1 dedicated session as a compatibility baseline, **not** a drop-in global patch. Its OPL has `sysInitDev9()` short-circuited and its CF session excludes competing DEV9/HDD/SMB; our integrated builds cannot adopt that unconditionally.

## Source references and licensing
- Upstream release: https://github.com/mihhru/PS2CF
- PC CARD hardware/ownership/reset and conservative PIO: https://github.com/mihhru/PS2CF/blob/main/src/opl/modules/pccf/pccf_hw.c
- Block device and game variant: https://github.com/mihhru/PS2CF/blob/main/src/opl/modules/pccf/main.c
- Validation: https://github.com/mihhru/PS2CF/blob/main/VALIDATION.md
- Build requirements: https://github.com/mihhru/PS2CF/blob/main/BUILDING.md
- Origin and license inventory: https://github.com/mihhru/PS2CF/blob/main/THIRD_PARTY_NOTICES.md
- Preserve file-level author notices and original component licenses (OPL AFL-3.0; PS2SDK-derived PC CARD hardware AFL-2.0; FatFs its own terms; wLaunchELF upstream notices). Import only needed compatible source; do not transplant PS2CF's FMCB/Gen2 image or bundled GPLv3 mymc++ tooling into runtime.
- Keep PS2CF author credited in code, documentation, releases, and relevant GUI credit screens.

## Agent handoff discipline
- Continue this branch; use one draft PR per repository, not a PR per subfeature. Coordinate the shared low-level driver ABI and lifecycle with the companion PR in the other repository without coupling release schedules.
- Track implementation commits and host test evidence in this plan; state explicitly what is modeled, what was run on PCSX2, and what requires real early-PS2 hardware.
- Keep unrelated regressions and refactors out. Do not merge or publish CF compatibility claims without a review of conflicts, successful builds, and actual hardware evidence.


## Launchstation implementation map
- Detection: `src/main_boot.c` currently detects DESR via `rom0:PSXVER`; `src/main_startup.c` caches `rom0:ROMVER`; `src/main_console_info.c` resolves model names. Add a separate PC CARD eligibility/owner service rather than reusing `console_is_PSX` or guessing from `0100`. Model inference is imperfect, so validate hardware.
- Device surface: `src/dash_devices.c`, `src/filer.c`, `src/filer_browser.c`, and other route/path-normalization helpers. Add clear **PC CARD CF** device presentation and `cf:/` browsing only after successful mount. Respect `boot_show_all_devices` and `boot_safe_mode`: a recovery boot must not power/reset CF automatically merely because a device is shown.
- Driver: adapt the minimal hardware/ATA and BDM/FatFs stack from PS2CF's wLaunchELF component: `src/wlaunchelf/iop/pccf/*` and required pieces, reconciling with Launchstation's existing `ps2.yaml`, `Makefile`, PS2SDK `bdm` and `bdmfs_fatfs` ABI. Prefer one isolated on-demand IOP module owner. Do not unconditionally initialize legacy `ps2dev9`, ATA, LAN or conflicting storage drivers before CF. Preserve boot times and multi-USB stability.
- Browser operations: mount, list, navigate, read/copy, create folder, rename and delete with existing confirmations and errors. Confirm BDM/FatFs `cf:` path mapping; do not advertise direct ELF boot from CF until launch/handoff is implemented and validated. Avoid writes until the exact flush, read-back verification and interrupted-operation recovery path is reviewed.
- Device settings: expose compatibility state / bus busy / no adapter / unsupported adapter / filesystem invalid / I/O fault; controlled rescan and shutdown. Do not enumerate CF as USB or internal APA disk; no surprise formatting.
- Program launching / PS2 game launch: separate follow-on work. Existing Neutrino/Ember/POPStarter handoff does **not** prove `cf:/` as a supported launch device. Document the limitation instead of offering broken launch buttons. Native launching needs an explicit handoff path, game loader backend and lifecycle proof.
- Recovery/UX: user should still boot Launchstation on non-PC-CARD PS2 and on PC CARD LAN adapters, with no stalls; manual rescan should not disrupt an established connection or touch unrelated storage. Make fail states visible without repetitive polling.
- Docs: `README.md`, special-hardware docs page, dashboard/storage docs, wiki if used, release notes, credits; distinguish supported/verified hardware, usable filesystems, supported operations, unsupported game launch, and safe-removal rules.

## Work stages and acceptance
- [ ] L0: Record current driver ownership and device-lifecycle wiring; create host fixtures for ROMVER + DEV9 family (PCMCIA 0x20, expansion-bay 0x30, absent/unknown) and network-card rejection. No register writes during classification.
- [ ] L1: Port/import minimum licensed CF hardware + ATA code and build isolated on-demand module(s). Reuse PS2CF read timings/profile validation, bounds checks, fallbacks, LBA48 and conservative writes/flush/verify without replacing existing BDM drivers. Build standard and DualShock configurations.
- [ ] L2: Implement correct IOP/EE `cf:/` mount registration/unmount + failure cleanup + safe recovery semantics; no early init on unrelated consoles. Verify FAT32/exFAT and >4-GiB read/copy, file enumeration, write/read-back, interrupted write, missing/removed card.
- [ ] L3: Integrate Files/Devices, icons/labels and manual refresh, error states, path mapping; ensure no false direct ELF or Neutrino/Ember claims. Keep Settings > Storage & devices coherent.
- [ ] L4: Verify zero regression to USB (single and dual USB), MMCE, MX4SIO, internal ATA/APA, iLink, UDPFS, Ethernet/FTP, DESR/xfrom and Deckard visibility; PS2CF ownership conflicts must fail closed. Verify existing safe-mode boot remains functional.
- [ ] L5: Publish documentation and credits. Separate host tests / PCSX2 sanity checks / real hardware evidence (SCPH-10000 required before marking verified; 15000 and 18000 separately). Confirm card variants, empty slot, PC CARD network adapter, repeated cold boots.
- [ ] L6 (optional independent scope after L1-L5): define and test direct ELF launch or external PS1/PS2 loader handoff from `cf:/`; do not block safe file browsing on unsupported launch modes.

**Merge gate:** unchanged behavior on non-target systems; no ownership theft or unsafe media writes; passing relevant builds/tests; at least one documented real PC CARD CF hardware run; no lingering placeholders represented as implemented.


## Implementation evidence (2026-10-15)

Imported the minimal PS2CF PIO/ATA/BDM module from pinned upstream commit
`9b6ed3702b59a87512426952e003125a5b4f0e3c` into `iop/pccf/` with the
PS2SDK license and an explicit origin/adaptation inventory in `VENDORED.md`.
Removed the dedicated-session forced power reclaim. The module rejects a
resident DEV9 export library before MMIO, including idle/powered-down DEV9;
unknown/cyclic loadcore library state fails closed. The hardware layer also
rejects incompatible revision, absent cards, nonzero power and active mode.
CIS validation rejects LAN/non-fixed-disk profiles before ATA commands.

Validation:
- Standalone IOP module built successfully with ps2build v2026.10.13 / GCC.
- Production C host harnesses cover bounded CIS parsing, fixed-disk profile
  checks, ATA IDENTIFY/transfers, LBA48, flush/read-back errors, offline latching,
  real MMIO power/reset/restoration with modeled registers, and module ownership.
- CI now runs these host harnesses and compiles the standalone module.
- GitHub Actions run 38838799371 passed on cdcafb1d1fd097af55c111986ba95f11fbbc9385,
  including the host harnesses, standalone driver and standard/DS34 builds.
- Full application integration, emulator runtime and early-console acceptance
  remain unverified.

The L0/L1 checkboxes remain open: no EE eligibility/owner service is wired yet,
and standard/DS34 embedding is not complete. No CF driver is auto-loaded and no
Files/Devices entry is advertised by this foundation commit. Remaining stages
L2-L5 retain their original scope, including FAT32/exFAT filesystem acceptance,
manual rescan, handle invalidation, safe removal, and cross-device regression.

Confirmed integration boundaries to resolve next:
- `setupPowerOff()` in `src/init.c` calls `load_ps2dev9()`. A CF owner must register
  poweroff handling without loading a competing driver, and DEV9 requests must
  fail explicitly while CF owns the slot.
- IOP reboot alone does not prove that the card is powered off. Add an explicit
  CF-only shutdown channel before reset/launch teardown; never substitute a
  global BDM STOP_ALL call which can affect other devices.
- The installed BDM header supports `block_device.path` typed prefixes. Validate
  `cf:/` mapping against the installed filesystem module; do not assume the
  generic `massN:` namespace identifies CF or permits writes safely.
- Keep direct ELF, Neutrino, Ember and POPStarter handoffs disabled for CF until
  their driver-lifetime and loader backend paths are implemented and tested.
