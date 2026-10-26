# R3Z request implementation and validation

2026-10-25, branch `codex/r3z-review-changes`, based on
`b815d10da29274200572e61467d72a718004a708`.

| Request | Delivered behavior |
|---|---|
| R3Z-01 | Shared `consoleHasIlink` eligibility gate in classic/native device lists: retail ROM 1.70+ is hidden; PSX explicitly retains i.Link. Unknown/non-retail ROMs remain visible. Device-list rendering performs no model-discovery CDVD RPC. |
| R3Z-02 | Settings has a Built-in commands action. MISC is restored at the bottom of the Files tab root after device/VMC entries. Existing Tools and MISC/Configure access remains. |
| R3Z-03 | Per maintainer clarification, Home/launch-key, Tools and top Files navigation use the Files tab browser. Its four filename/title/detail modes retain ten rows at 25 virtual pixels each. Classic file/path selection dialogs remain distinct. |
| R3Z-04 | Adjacent movement wraps. Selection scrolls around the upper middle row of a ten-row viewport, clamped to valid list bounds. Both device and directory counts are handled. |
| R3Z-05 | D-pad left/right and existing L2/R2 page controls jump ten entries, stop at the first/last entry on overshoot, and wrap only when pressed again at that boundary. |
| R3Z-09 | Mode updates compare requested timing, interlace/field mode and framebuffer geometry with the current GS object in addition to cached TV mode. Outgoing GS work finishes before reinitialization; the new screen returns to ONESHOT and clears both buffers before drawing resumes. Existing generation-based texture-manager invalidation remains. |
| R3Z-10 | [Latest-upstream parity audit](R3Z-10-PARITY-AUDIT.md), pinned to upstream `06b06d0`. Audit does not introduce parity code changes. |
| R3Z-11 | [Four-cell Game ID audit](R3Z-11-GAME-ID-AUDIT.md). Marks POPStarter/MMCE selection not applicable because POPStarter uses its own VMC files; verifies required RetroGem output. Ember excluded. |
| R3Z-12 | Neutrino can start MMCE manager control without resetting/replacing USB, ATA/HDD, i.Link or UDPFS mounts. Resident/active MX4SIO refuses control bring-up. Existing source-card/ELF-slot protection and bounded status polling remain. RetroGem's display preference does not disable MMCE selection. |

## Video reference and evidence boundary

Inspected the working local POPSLoader reference:
`bin/POPSLDR/ui.lua` (`UI.ApplyVideoStandardFromRuntime`, video-standard table)
and `bin/POPSLDR/system.lua` (native-region capture). Its NTSC/PAL framebuffers
are 640×448 and 640×512, respectively, with interlaced FIELD output and native
region used for Auto. It checks current Screen mode rather than trusting only
the UI setting. Launchstation retains these geometry/timing conventions.

The C transition uses gsKit's normal initialization to reprogram CRTC timings;
it does not add a second direct SetGsCrt call. See
[gsKit GS initialization](https://github.com/ps2dev/gsKit/blob/master/ee/gs/src/gsInit.c).
The current GS object is software state, **not an independent hardware CRTC
readback**. Host tests exercise mismatched software mode/geometry and transition
ordering, not a television's signal lock. No claim is made that the reported
black screen was reproduced on a console in this change.

For i.Link generation context, Sony's
[SCPH-50000 announcement](https://sonyinteractive.com/en/press-releases/2003/sony-computer-entertainment-launches-playstation2-new-model-scph-50000/)
records removal of that port. The ROM cutoff avoids loading model-discovery
drivers during UI enumeration; ROM replacements/nonstandard identities are a
limitation of that heuristic. PSX is tested before the cutoff.

## Automated validation

All sixteen focused host scripts passed:

* `test_r3z_browser_contract.py`: exhaustive selection/page/centred viewport
  matrix over counts 1–39, empty page, device and directory lists; ROM/PSX
  eligibility; existing/new/error MMCE manager, MX4SIO veto and unified routing.
* `test_r3z_video_switch.py`: actual C transition code with mocked GS calls;
  repeated NTSC/PAL/VGA/480p/576p changes, Auto on PAL and NTSC, cached-mode and
  geometry/interlace drift, no unnecessary reset, completion/reset/clear order.
* `test_r3z_neutrino_gameid.py`: actual ISO parser and sender; malformed extents,
  fragmented/failed I/O, protected source slots, absent/rejecting/busy cards and
  bounded status handling.
* `test_navigation_wraparound.py`, `test_files_tab_lifecycle.py`,
  `test_unified_dashboard_navigation.py`, `test_display_feedback.py`,
  `test_dashboard_present_buffers.py`, `test_native_four_browser_modes.py`,
  `test_r3z_four_browser_modes.py`, `test_ilink_browser.py`,
  `test_neutrino_iso.py`, `test_neutrino_profiles.py`,
  `test_neutrino_menu_audio.py`, `test_r3z_neutrino_options.py`,
  `test_r3z_loader_disable.py`.

Standard and DS34 GCC PS2BUILD target builds passed, including ELF link and
packing, in `build-r3z-review` and `build-r3z-review-ds34`. The builds emit
warnings in existing code; this is not a warning-free baseline. Build outputs
are local validation artifacts rather than published releases.

The browser's list occupies virtual y=100 through y=350 within its 448-line
canvas. PAL maps that same virtual canvas into the 512-line framebuffer.
This establishes layout bounds in source; it is not a captured console screen.

## Remaining hardware checks

* PSX and pre-/post-SCPH-50000 device visibility.
* All File Browser shortcuts, ten visible rows with titles/details and long
  names, centred scrolling and page boundaries on PAL/NTSC displays.
* NTSC ↔ PAL ↔ Auto, progressive modes, repeated changes and retained offsets;
  no black screen and correct signal timing on the actual display/adapter.
* MMCE selection from USB, ATA/HDD, i.Link and UDPFS; Neutrino on each memory
  card, ISO on MMCE, no card, busy/error cards, and MX4SIO refusal. Standard and
  DS34 have separate controller/storage acceptance boundaries.
* The three applicable Game ID combinations retain separate recipient evidence; POPStarter/MMCE selection is not applicable.

No physical-console run, card-image change, HDD write or exploit installation
was performed during this work.

## Authorized audit follow-up (2026-10-26)

The complete configuration map covers all 61 upstream serialized key families.
Classic presentation controls are restored under System > Classic screen settings;
optional Left/Right launch-key editors are restored and target edits refresh
LK_Flag immediately. MISC naming strings remain persisted compatibility data.

Japanese save titles now use optional ELISA100.FNT through a managed T8 atlas
(512 KiB EE allocation plus existing shared CLUT). The font is loaded on the
serialized browser reader, checked for exact size, read fully before publication,
and retried on a new browser session after a failure. Filenames and localized
Latin text keep their existing encoding. Missing/invalid fonts or atlas allocation
failure retain the existing title transliteration. Clipping never splits a title
or pad glyph token. No external font is bundled.

Additional host tests cover actual POPStarter RetroGem handoff, complete/failed
font reads, atlas pixels and glyph drawing, token boundaries and allocation
failure. These are host checks, not physical RetroGem/display acceptance.

Follow-up validation uses 27 focused host scripts: the original 16 plus
`test_sjis_dashboard_titles`, `test_elisa_font_load`, `test_popstarter_retrogem`,
`test_classic_settings_persistence`, `test_r3z_config_inventory`,
`test_settings_navigation`, `test_settings_save_prompt`, `test_selector_centering`,
`test_gage_alignment`, `test_modal_animation`, and `test_dashboard_languages`.
The glyph mapper is checked against Python's independent Shift-JIS/EUC-JP codecs.
New Settings labels are included in all nine built-in language catalogs, with
legacy IDs preserved. Colour serialization now gives sprintf's %n an int pointer.
