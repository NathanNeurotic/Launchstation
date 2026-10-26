# R3Z-10: upstream feature parity audit

> **Status as of 2026-10-26: historical audit; its findings are resolved or deliberately divergent.**
> * Resolved: the Neutrino/MMCE "active stack" gap (R3Z-12) is `ensureMmceGameIdControl()` in `src/init.c`
>   (PR #208); configuration/GUI parity is complete (61 key families, see [R3Z-CONFIG-PARITY.md](R3Z-CONFIG-PARITY.md),
>   `scripts/audit_r3z_settings.py`, `tests/test_r3z_config_inventory.py`); Ember RetroGem was added afterward
>   ([EMBER-RETROGEM.md](EMBER-RETROGEM.md)).
> * Correction: the Neutrino row says "explicit `-bs`"; the argument Launchstation actually passes is
>   `-bsd=<backend>` (`src/neutrino_iso.c`).
> * Still true: upstream was pinned at `06b06d0` (2026-10-25); later upstream commits were not compared, and
>   no row proves physical-console parity. Intentional downstream differences (Neutrino per-game profiles,
>   manager-only MMCE Game ID, native Settings) stand.


Audit date: 2026-10-25. This report changes no implementation to obtain parity.
The companion R3Z requests have their own implementation changes in this PR.

## Pinned scope

The requested baseline is **the latest upstream work**, per maintainer
clarification, rather than only the published 4.80 release:

* [saildot4k/wLaunchELF_R3Z master at 06b06d000072d358c93d98104d64d2f081e47ba7](https://github.com/saildot4k/wLaunchELF_R3Z/tree/06b06d000072d358c93d98104d64d2f081e47ba7),
  `Add support for launching disc images`.
* [Published 4.80](https://github.com/saildot4k/wLaunchELF_R3Z/releases/tag/4.80)
  points to `39fb5bc7700b0d9bf9ed8448f4b9f080b08d81d7`.
* Launchstation baseline: `b815d10da29274200572e61467d72a718004a708`.

Evidence is source inspection of upstream's README, release notes, and live
call paths in the pinned trees. A present implementation means source coverage,
not identical behavior on a physical console. No upstream ELF was executed.
Source paths below are relative to each repository's root.

## Parity table

| Upstream feature | Launchstation evidence | Assessment / remaining boundary |
|---|---|---|
| Neutrino ISO launching | `src/neutrino_iso.c`, `src/main_actions.c`, `src/main_fileops.c` | Present, with separate launcher module, validated ISO, complete-install discovery, explicit `-bs` and keep-IOP handoff. Upstream latest keeps `LaunchNeutrinoIso` inside `main_actions.c` and uses the configured ELF. This is behavioral divergence, not a verbatim import. Device/game acceptance remains unproven here. |
| Neutrino compatibility modes 0/2/3/5/7, video and video compatibility, debug colours, logo, Run/Cancel | `src/gui_neutrino.c`, `include/gui_neutrino.h` | Present. Launchstation additionally has per-game profiles; upstream README says choices reset per launch. Profile persistence is an intentional difference that must not be called exact UI parity. |
| MMCE card selection for Neutrino | `src/neutrino_gameid.c`, `src/init.c` | Baseline gap: selection required an already active MMCE stack. R3Z-12 addresses it separately without copying upstream's reset-capable stack bring-up. MX4SIO and source-slot exclusions remain explicit. See Game ID audit. |
| RetroGem ID for Neutrino | `src/neutrino_iso.c`, `src/main_gameid.c` | Present; independent of MMCE compilation/availability and suppressed by the disc RetroGem setting. Shared serial extraction does not mean a shared transport. |
| POPStarter VCD launching | `src/popstarter.c`, `src/popstarter_path.c`, `src/popstarter_elf.c`, `src/main_actions.c` | Present. Launchstation's candidate/preparation path has downstream changes; execution and device compatibility require hardware evidence. |
| RetroGem ID for POPStarter | `src/popstarter.c`, `src/popstarter_gameid.c`, `src/main_gameid.c` | Present. Serial from VCD SYSTEM.CNF, then generic timestamp identity, then existing filename/partition fallbacks. POPStarter uses its own VMC files; MMCE card selection is not applicable. |
| Enable/disable Neutrino, POPStarter, Ember routes | `src/config.c`, `src/dash_settings.c`, `src/main_actions.c`, `src/neutrino_iso.c` | Fields and UI are present. The requested follow-up adds Ember RetroGem output using the same disc identity rules; game/hardware acceptance remains unverified. |
| Device names in root rows instead of meaningless timestamps | `src/filer_browser.c`, `src/dash_devices.c`, `src/dash_browser.c` | Present using different native dashboard presentation. Physical availability/probe state is separate from a device-family label. |
| Forward/reverse sorting | `src/gui_sort.c`, `src/filer_browser.c`, `src/dash_browser.c` | Both paths support sort direction. Dashboard uses its actions menu; classic uses its existing sort dialog. Exact upstream L1 interaction is not dashboard UI parity. |
| Four filename/title/detail display modes | `include/filer_shared.h`, `src/filer_browser.c`, `src/dash_browser.c` | Present in both implementations. This PR routes visible dashboard browser shortcuts to the Files tab; classic path pickers remain separate. |
| Space between icon.sys title lines | `filerGetGameTitle` in `src/filer_copy.c` | Present; title loading feeds both browser implementations. Dashboard now renders supported Japanese save-title glyphs from optional ELISA100.FNT; the existing transliteration remains when the font is absent. |
| *Tuna date integrated into timestamp editing | `src/filer_actions.c`, `src/dash_browser.c` | Present. Dashboard nests it under Folder dates; classic uses the timestamp menu. No separate conflicting timestamp implementation added. |
| LoadBOOTer exploit installation; retail/PSX/DTL targets | `src/main_exploit_installer.c`, `src/kelf_signer.c`, `src/dash_tools.c` | Present. Target selection and key/device checks remain required; source coverage does not prove installation success. |
| Bind arbitrary ELF to memory card using user keys | `src/kelf_signer.c`, `src/filer_actions.c` | Present. Key files are user supplied and excluded from version control; no signing/installation was performed. |
| Disc launch history for MMCE and DECKARD patches | `src/main_actions.c`, `src/main_history.c`, xparam path | Present for physical-disc execution. **Do not extrapolate this to POPStarter VCD card selection.** |
| Region-native Cross/Circle when configuration is absent | `src/main_boot.c`, `src/main_startup.c` | Present; J/C choose Circle, other regions Cross. Existing configuration still governs configured consoles. |
| Keyboard layouts and nine built-in languages | `src/dash_widgets.c`, `src/config.c`, `Lang/`, `Lang/DASHBOARD.inc` | Present. Native dashboard localization differs from classic label layout and needs visual validation independently. |
| Configuration exposed in GUI | `src/dash_settings.c`, classic `src/config*.c` | Settings pages expose downstream options; MISC/Configure routes to Settings. All 61 upstream persisted key families are now mapped in [the configuration report](R3Z-CONFIG-PARITY.md), with reachable editors for user options. MISC naming strings remain CNF compatibility data. |
| Hide HDD/ATA on DECKARD; PSX flash/DVR visibility | `src/filer.c`, `src/dash_devices.c`, `src/init.c` | Guards are present, including PSX distinctions. R3Z-01 adds i.Link visibility separately. Show-all/device diagnostics remain distinct from working hardware. |
| Dual HDD/ATA, multiple USB units, lazy device drivers, MMCE/MX4SIO conflict handling | `src/init.c`, `src/filer.c`, `src/dash_devices.c` | Present. Conflict/reset policies intentionally differ in guarded downstream launch paths. Do not equate an enumerated second unit with proven dual-drive operation. |
| HDD header injection and partition SYSTEM.CNF editing | `src/hdd_header_injector.c`, `src/hdd.c`, `src/dash_hdd.c` | Present; writes require device-specific validation. No HDD writes were attempted. |
| Create/extract PSU, file operations, text-editor shortcuts | `src/filer_copy.c`, `src/filer_actions.c`, `src/dash_browser.c`, text editor sources | Present. Downstream transfer handling has its own recovery history; this PR does not restore or replace it as part of parity. |
| Launch ELF with cached/sidecar arguments and KELF execution | `src/elf.c`, `src/main_actions.c`, `src/gui_texteditor.c`, `src/dash_editor.c` | Present. Path aliases, bounded argument parsing and handoff behavior remain downstream code. |
| Folder timestamp ordering for OSDSYS/HOSDSYS | `src/filer_actions.c`, `src/dash_browser.c` | Present through shared operations and dashboard timestamp adapter. Save-folder safety must be validated on actual media. |
| DS3/DS4 USB/Bluetooth build | `src/pad_ds34.c`, `scripts/configure_ds34.py`, `iop/ds34*` | Present as a separate build. Upstream's warning about controller/storage conflicts remains relevant; a compiled DS34 ELF does not demonstrate controller/MC coexistence. |

## Audit findings

1. The active-stack requirement was a concrete Neutrino/MMCE gap; its correction
   belongs to R3Z-12, not an automatic parity import.
2. POPStarter RetroGem support is present and covered by an executable handoff
   test. MMCE selection is not applicable: POPStarter uses its own VMC files.
   The earlier audit incorrectly treated this as a missing feature.
3. Downstream browser navigation, Neutrino profiles, launch-device validation,
   keep-IOP behavior and native Settings intentionally diverge from upstream.
   Feature presence should not be reported as byte-for-byte or hardware parity.
4. The feature inventory above covers the upstream README and 4.80 release
   additions. It is not a proof that all possible device, media, language,
   hardware and build combinations are equivalent.

Following maintainer authorization, confirmed configuration-access and save-title
rendering gaps are implemented in this PR. See [the complete configuration
mapping](R3Z-CONFIG-PARITY.md). Hardware acceptance remains separate.
