# Launchstation changelog

The `## Rolling` section below is the maintained summary shown on the GitHub [`latest`](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest) release page (`scripts/release_notes.py` reads only its `- ` bullets). Dated sections after it are a condensed history grouped by theme, newest first; PR numbers refer to <https://github.com/NathanNeurotic/Launchstation/pulls>. CI success is not console acceptance: items marked pending hardware validation still need testing on a real PS2.

## Rolling

- **Release downloads:** the `latest` release now also offers the packed ELFs on their own: `LAUNCHSTATION.ELF`, `LAUNCHSTATION-DS.ELF`, and `BOOT.ELF` / `BOOT2.ELF` (identical copies of the standard ELF). Publishing also refreshes the release page date.
- **Build toolchain:** release ELFs are now built with the PS2BUILD LLVM/Clang toolchain (EE and IOP). Mbed TLS, libjpeg-turbo and the audsrv RPC client are built from source with the same toolchain, and CI fails if a release ELF contains non-Clang objects. `PS2BUILD_TOOLCHAIN=gcc` remains available for comparison builds.
- **R3Z parity and Ember Game IDs:** Settings now cover the upstream wLaunchELF_R3Z persisted option families (including Left/Right launch keys and previously hidden classic options). Browser navigation is unified (MISC access, centered ten-row browsing, page jumps stop at list boundaries, i.Link stays visible on PSX). Ember CUE/BIN images can show the same RetroGem Game ID as an equivalent POPStarter VCD, and PS1 serials are normalized like OPL. Japanese save titles render with the optional ELISA font.
- **Dashboard languages:** navigation, Settings labels, common actions and transfer progress use the nine built-in language tables. External LNG overrides also apply to these labels. User filenames, paths and application metadata keep their original text; some extended help remains in English.
- **File-copy compatibility:** restore upstream wLaunchELF_R3Z direct overwrite behavior. Remove the custom destination-stat, staging, verification, backup and rename transaction so ordinary paste does not depend on those additional operations. Short reads and writes now fail with a reason instead of entering a custom retry loop.
- **Clearer file operations:** copy/delete popups give filenames their own full-width row, show remaining MB or completed selected items, and finish with a full progress bar. Paste failures include a readable reason, stage and numeric error.
- **Navigation and rendering:** wrapped list selections stay visible in the new viewport. Carousel rendering omits fully offscreen cards while retaining visible geometry and order. Physical-console acceptance remains unverified.
- **Neutrino options:** menu audio stays active until launch is committed, with ordinary ELF muting and failure recovery preserved. Optional per-game Neutrino profiles (checksum-verified A/B slots) are saved only from **Save for this game**.
- **Dashboard and display (#213 port):** 576i video mode (576p only on PS2 ROM 2.20+ or PSX 1.80J/2.10J); console temperature (Celsius/Fahrenheit) beside the clock with cached console identity; START opens the Dashboard menu from any native screen; MISC commands popup; applications can be hidden from Home/Applications (filter **All including hidden**); Applications start screen; header tabs and panes share the File Browser width; R3 starts the image slideshow; background-only R3 toggle is Home-only.
- **Clock sync:** SNTP now queries `0.pool.ntp.org` through a new vendored `iop/ps2ips_rpc` socket server (loaded on demand), with two attempts, specific failure messages and elapsed-time compensation.
- **Retained improvements:** concise release notes, dashboard free-space display, MASS display names, verified software-clock saves, lazy storage loading and power-off service initialization remain available.

## 2026-10-26 to 2026-10-25: toolchain, R3Z parity, restored file handling

- #210 (10-26): PS2BUILD LLVM/Clang is the default release toolchain; `ee_toolchain.sh` selects the compiler for the third-party archive scripts; libjpeg-turbo 3.1.3 and the audsrv EE client are built per toolchain; a toolchain-purity check runs in CI. A Claude Code cloud `SessionStart` hook installs ps2build and the LLVM toolchain.
- #208 (10-26): R3Z fixes, Settings and title parity, Ember RetroGem Game IDs, consistent PS1 RetroGem serial normalization.
- #206 (10-25): restore upstream R3Z direct file overwrite, finish restoring R3Z ordinary transfer behavior, correct FatFs copy status and memory-card destination handling, and restore dashboard feedback.

## 2026-10-24 to 2026-10-22: stabilization after regressions

- 10-24: the repository was reverted to `2e140ab` (10-22: wrap-around navigation and release-page summaries), removing the later changes from PRs #191 to #196, to recover known-working file copying and device behavior. Stable dashboard feedback, nine-language dashboard labels, carousel culling and Neutrino option audio were then reapplied selectively. Copy preflight fixes for `EISDIR` lookups on MC and MMCE, rejection of unsafe external format overrides, and committed-source identity stamping for ps2build diagnostics landed the same day.
- 10-23 (#192 to #196, reverted): transfer-progress and file-operation overlay redesign, insufficient-storage message on paste, Neutrino menu audio, PS2BUILD SDK documentation and credit. The progress layout and Neutrino audio behavior returned in the Rolling items above.
- 10-22 (#191): wrap-around navigation for dashboard lists and file-browser views, USB presentation without invented MASS aliases, compatible device-handoff choices, release-page summaries.

## 2026-10-21 to 2026-10-19: storage drivers, per-game profiles, copy fixes

- #190 (10-21): UDPFS SMAP driver built from source, odd-length receive tails preserved, SMAP startup checks.
- #188 (10-20): file, clock and storage fixes from tester reports: native copy and delete progress, memory-card stat and rename handling, verified software-clock saves, MASS labels, power-off without loading an unused DEV9 stack, MMCE SIO2 port restore after probes. Console acceptance outstanding.
- #181 (10-20): optional saved per-game Neutrino launch profiles with checksum-verified A/B storage and device-ownership guards.
- 10-19: maintained user-facing changelog published with each rolling release (#182).

## 2026-10-18: lifecycle, console summary, software clock

- #183: simplified release notes, quieter boot status, and clock-only Settings saves.
- #180: passive console summary on Home and Devices with explicit model lookup in Options.
- #179: refused DEV9/IP transitions stop dependent driver loads; power-off registration preserved when DEV9 fails.
- #178: UI polish, software clock for consoles without a CMOS battery, lower audio latency.
- #177: R3Z launcher controls and browser parity, including the four file display modes (name, name + details, title, title + details).
- #176: guards against incompatible storage stack transitions, bounded MMCE presence checks, generation-safe audio lifecycle.
- Neutrino ISO Game IDs and safe RetroGem / MMCE slot handoff ported from upstream R3Z (10-17).

## 2026-10-17 to 2026-10-15: PC CARD CF, display and upstream sync

- #166 (10-17): MASS labels, no launch-screen flash, unified file sizing.
- #165, #156 (10-16 and 10-15): PC CARD CompactFlash (`cf0:`) driver for early Japanese PS2 models integrated with the device list, file operations and exclusive DEV9/PC CARD bus ownership.
- #161 to #164 (10-16): interlaced text and wallpaper stability, shared VBLANK fade to black for launches and video-mode changes, orb blending, Coverflow overlay, button glyph and hint fixes, Continue-tile flicker and L1/R1 sound-drop fixes.
- #155 (10-15): selective October upstream R3Z browser updates: reversible sorting, two-line `icon.sys` titles, *Tuna date under Folder dates.

## 2026-10-13 to 2026-10-12: UI feedback rounds, MMCE recovery, zero-setup Drive

- #145 to #154 (10-13): modal navigation skips disabled rows, nested-scissor clipping for marquee text, real console region in system information, HDL title highlighting in the HDD Manager (loaded on explicit R1), centered selection-bar text, round-three tester feedback, ELF symbol-size report in CI, 4:3 wallpaper guide and example theme.
- #143, #144 (10-12): launch and UI feedback fixes; QR code with automatic polling on the Google Drive link screen.
- #131 (10-12): link Google Drive with only a device code, with Launchstation's own OAuth client embedded in official builds; privacy policy page.
- #116 to #122 (10-12): MMCE recovery on unmodified adapter firmware: store verified snapshots in `WLE-Recovered`, install a verified snapshot as a NEW numbered selectable card on stock sd2psXtd firmware, exclusive new-file creation and ownership guards, firmware restore-safety contract and fault-tested transaction model.

## 2026-10-11: documentation baseline

- #115: active-card namespace protection and bounded firmware mutation paths for MMCE.
- Wiki and Pages documentation synchronized with the runtime; version-controlled Markdown wiki synchronized to the GitHub Wiki by CI; full third-party credits.

Earlier history (the original wLaunchELF, uLaunchELF, LaunchELF and wLaunchELF_R3Z lineage, and Launchstation development before 2026-10-11) is not repeated here; see the merged pull requests and [docs/THIRD_PARTY_CREDITS.md](docs/THIRD_PARTY_CREDITS.md).
