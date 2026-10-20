# Current LAUNCHSTATION features and navigation

This guide reflects the four visible tabs implemented in `src/dash_main.c` and the Home/Files integration on `master` after PRs #104 and #107.

| Visible tab | User-facing behavior | Source |
| --- | --- | --- |
| **Home** | Continue, pins, recents, favorites, nested Applications library and Last folder | `src/dash_home.c`, `src/dash_apps.c` |
| **Files** | Direct persistent File Browser, remembered folder and selection across tabs; device roots | `src/dash_browser.c` |
| **Tools** | MISC commands, USB unit diagnostics, Text Editor, HDD Manager, exploit installer, PS2Link/FTP, IGR and cloud actions where enabled | `src/dash_tools.c` |
| **Settings** | Appearance, app library folders, sound, input, launching, devices and backup/Drive options | `src/dash_settings.c` |

**Navigation:** L1/R1 switches tabs. SELECT changes Grid/List/Carousel in Home's Applications library. The separate Apps and Devices internal screen constants are compatibility details, not extra visible top-level tabs.

## Upstream wLaunchELF_R3Z integration (October 15, 2026)

Launchstation selectively incorporates [upstream PR #56](https://github.com/saildot4k/wLaunchELF_R3Z/pull/56) from the **ember-support** development branch. The classic L1 file-browser mode menu allows reverse filename/title/date sorting; memory-card save and PSU titles honor valid two-line `icon.sys` separation; the *Tuna Date action is grouped inside Folder dates and only appears for eligible selected real MC folders. Launchstation's dashboard Files tab already provides independent six-criterion reversible sorting. Existing MMCE, USB, networking, dashboard, and Ember launch paths were **not** replaced by upstream code.

## Classic File Browser display modes

The upstream R3Z four-mode layout is now available in the **classic** browser's L1 mode menu: filename, filename + details, game title, or game title + details. Circle toggles filename variants; Square toggles title variants; sorting is unchanged. Companion PR #178 adds the corresponding title-only choice to the dashboard Files tab. That native UI change remains in its own unmerged PR; console acceptance of the combined changes is pending.

## Files and app roots

Default app scanning: `mc:/APPS`, `mc:/BOOT`, `mass:/APPS`, `mass:/BOOT`, each one subfolder deep. Adjust under Settings → Home & applications → Application library → Application folders. Other paths can still be browsed/launched from Files.

For storage, do not equate `Not checked` with physically absent. Lazy drivers start when selected. Logical USB mount numbers can change. Use Tools → USB unit diagnostics deliberately; a manual fileXio probe can block in an IOP RPC.

## External game cores

- **PS2 ISO** → Neutrino (external ELF, configured or discovered with TOML sidecars), with the R3Z per-launch compatibility modes 0/2/3/5/7, GSM/field-flipping, debug colors and PS2 logo. Options default to launch-only unless the user explicitly saves a per-Game-ID Neutrino profile in the dashboard; separate checksum-verified A/B settings files preserve the last valid copy and never modify the ISO, global CNF, or active VMC. ISO `SYSTEM.CNF` game IDs also drive RetroGem display and best-effort active-MMCE card switching, without initializing or replacing live storage drivers.
- **Loader availability** → Three independent Disable Ember/POPStarter/Neutrino controls in Launching settings and the classic Advanced Settings screen, Off by default.
- **PS1 VCD** → POPStarter (external launcher, own VCD and HDD argument conventions).
- **PS1 CUE/BIN/EXE** → Ember (external ELF), only under a save-safe `<umbrella>/games/<Game>/` folder. The umbrella can be anywhere and can be named arbitrarily.
- For Ember on USB, the launcher rebuilds the storage IOP without resident PADMAN and confirms game identity after USB mount-number reassignment before final keep-IOP launch.
- Normal generic ELF reset preferences do not govern Neutrino or Ember's dedicated handoff.

These external programs, user BIOS and commercial games are not bundled. See the [full web documentation](https://nathanneurotic.github.io/Launchstation/) for per-core setup and troubleshooting.
