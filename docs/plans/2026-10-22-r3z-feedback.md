# R3Z feedback follow-up

Approved scope, based on master 29e5aaf. Keep the work in draft PR #192; no CodeRabbit request or ready-for-review transition during implementation. Split Part 2 only above 100 changed files. Preserve specialized game-loader contracts and compatible saved settings.

- [x] Add video-mode preview with a **5-second** confirmation/rollback before Save, including display-reset actions that change the mode. Restore screen offsets on rollback and refresh dashboard VRAM after a GS reset.
- [x] Gate i.Link browser presentation, automatic probing and module loading by the existing driver's supported BIOS range; explicitly exclude PSX.
- [x] Route Configure to Settings while the dashboard is active; retain classic configuration elsewhere.
- [x] Give native browser/path picker ten rows with centered scrolling. Pages reach an endpoint before wrapping on a subsequent press; retain classic browser capacity and existing paging.
- [x] Show friendly device names plus mount IDs, using USB Mass Storage with mass-style presentation. Keep status/error feedback when opening and in diagnostic tools.
- [x] Audit R3Z, RiptOPL and NHDDL Game ID paths. Initialize MMCE on demand only without replacing the source stack, exclude MX4SIO and protect source slots. Keep RetroGEM signaling distinct from MMCE selection.
- [x] Label the tool FTP server / Remote access and the MISC entry FTP server without changing saved/localized PS2Net command paths.
- [x] Label the normal-ELF path choice ELF handoff paths: Legacy mass (default) / Device-specific. Retain Modern_Device_Handoff persistence/default.
- [x] Evaluate automatic startup removal: **retain opt-in controls**. Background scans use cheapToScan() and skip unloaded stacks; launch-key/autoboot demand loading does not populate apps on every device. No new storage scheduler is introduced.
- [x] Identify and promote **TechWritesCode's PS2BUILD SDK** across README, site, Wiki source and contributor credits; preserve upstream component/license attribution. Correct old development instructions that called the build environment PS2SDK/ps2dev packages.

## Source comparison

- R3Z master pinned at [06b06d000072d358c93d98104d64d2f081e47ba7](https://github.com/saildot4k/wLaunchELF_R3Z/tree/06b06d000072d358c93d98104d64d2f081e47ba7). `main_gameid.c` matches apart from a trailing blank line. All 124 timestamp-to-PS1-ID mappings in `main_gameid_table.c` match. NTSC/PAL GS Mode, Interlace and Field settings match; Launchstation additionally supports 576p and tracks screen generations. Mode changes call gsKit_init_screen(), not only resolution assignment. The reported physical black screen has no confirmed root cause from this comparison.
- RiptOPL clean local source at [40c52ceb457847a3006d640ef302f5a515d4b6c0](https://github.com/NathanNeurotic/Open-PS2-Loader/tree/40c52ceb457847a3006d640ef302f5a515d4b6c0), `src/mmcesupport.c`: transport arming is separate from final launch; source/VMC protection and bounded settling are explicit. Copying that whole launch policy would not provide the requested on-demand Launchstation behavior.
- NHDDL legacy pinned at [89141d470ea2e0a2f81ad485874869f057bca082](https://github.com/pcm720/nhddl/blob/89141d470ea2e0a2f81ad485874869f057bca082/src/devices/mmce.c): ping 0x1, SET_GAMEID 0x8, GET_STATUS 0x2, protect the mcN loader slot, wait one second before status, at most 15 polls. Launchstation retains its stricter ISO/loader source exclusions and negative-status exit.
- iLinkman's REQ_CHECK_CONSOLE_VERSION guard rejects BIOS versions above 0x160 before touching hardware. Launchstation mirrors that supported stock-ROM range for presentation/probing, rejects unknown/malformed versions, and excludes PSX's camcorder-only port. This is a supported-ROM eligibility gate, not physical-port detection on modified hardware.

RetroGEM renders an optical identifier; MMCE uses the canonical title ID for emulated-card selection. Neutrino reads BOOT2 from the ISO's SYSTEM.CNF. POPStarter has its separate VCD identity path and emits RetroGEM output before handoff. **Ember's specialized image launch does not currently emit a per-game RetroGEM ID.** Upstream also does not provide that implementation; no filename-derived claim or speculative Ember parser is added here.

## Validation and remaining acceptance

Host checks exercise production navigation, timeout/rollback, source guards and MMCE commands. Standard and DualShock target builds use TechWritesCode's PS2BUILD SDK. The site/Wiki generators validate authored documentation before publication; live pages remain on the released branch until this draft is merged and its publication workflows succeed.

CI status and final local build results belong to the exact PR head. Builds and host tests do not establish physical PAL/NTSC transitions, unsupported-display recovery, RetroGEM reception or MMCE switching acceptance. Keep hardware issues open pending console reports. Stock PS2 sounds (#160) remain pending contributed assets; Jellyfin (#186) remains research unless a reproducible defect is identified.
