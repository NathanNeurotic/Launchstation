# Current LAUNCHSTATION features and navigation

This guide reflects the code on `master`: the four visible tabs in `src/dash_main.c` (`visible_sections`), the Home/Files integration and the settings pages in `src/dash_settings.c`.

| Visible tab | User-facing behavior | Source |
| --- | --- | --- |
| **Home** | Continue (pinned, recent, favorites), nested Applications library (Grid/List/Carousel), Last folder and configured launch-key shortcuts | `src/dash_home.c`, `src/dash_apps.c` |
| **Files** | Direct persistent File Browser, remembered folder and selection across tabs; device roots | `src/dash_browser.c`, `src/dash_files.c`, `src/dash_devices.c` |
| **Tools** | File Browser, Text Editor, Start disc, PS1/PS2 image shortcuts, MISC commands, USB unit diagnostics, HDD Manager, memory card exploit installer, OPL IGR setup, PS2 browser, OSDSYS, FTP / Remote access, Google Drive backups, system and build information, Reboot IOP, Power off | `src/dash_tools.c` |
| **Settings** | Appearance, Home & applications, Sound, Input, Launching, Storage & devices (with MMCE backups), Network, System | `src/dash_settings.c` |

**Navigation:** L1/R1 switches tabs; START opens the Dashboard menu from any native screen. The header shows console temperature beside the clock on supported consoles (Celsius/Fahrenheit in Settings > System). Applications can be hidden from Home and the library (Hide from view; filter **All including hidden**), and Tools > MISC commands opens a popup menu. Video modes include 576i, and 576p only where the console ROM supports it. Internet clock sync uses SNTP to `0.pool.ntp.org` through the on-demand PS2IP socket server. SELECT changes Grid/List/Carousel in Home's Applications library. Lists wrap around, and page jumps stop at the boundaries before wrapping. The separate Apps and Devices internal section constants (`DASH_SEC_APPS`, `DASH_SEC_DEVICES`) are compatibility details, not extra visible top-level tabs. The dashboard is the only interface: there is no separate classic menu, though the classic wLaunchELF file, HDD and launch logic runs underneath it.

## Files and app roots

Default app scanning: `mc:/APPS`, `mc:/BOOT`, `mass:/APPS`, `mass:/BOOT`, each one subfolder deep. Adjust under Settings → Home & applications → Application library → Application folders. Other paths can still be browsed/launched from Files.

The File Browser's **Show** menu has four display modes (file names, names and details, game titles and details, game titles), **Sort by** offers As stored, Name, Game title, Size, Date-Time and Kind with an ascending/descending direction, and *Tuna date lives under **Folder dates** for eligible memory-card folders. Other actions include copy/cut/paste (also with new names), PSU create/extract, VMC mounting, **Card contents...** for saves inside card images, bookmarks (PathPad), Use as launch arguments, and an image viewer for PNG/JPG/JPEG.

For storage, do not equate `Not checked` with physically absent. Lazy drivers start when selected. Logical USB mount numbers can change. Use Tools → USB unit diagnostics deliberately; a manual fileXio probe can block in an IOP RPC.

## Devices

Memory cards (`mc0:`/`mc1:`), USB (`usb:`/`usbN:`), MMCE (`mmce0:`/`mmce1:`), MX4SIO, iLink (`ilink:`), internal HDD (`hdd0:`), exFAT ATA (`ata0:`), PC CARD CompactFlash (`cf0:`, early Japanese models), disc (`cdfs:`), UDPFS (`udpfs:`), PS2Link host (`host:`) and, on PSX, the flash and DVR HDD. Entries that cannot exist on the running console are hidden. MMCE and MX4SIO are mutually exclusive and cause an IOP reset when switched. PC CARD CF is integrated into the device list and file operations but has not been verified on a physical console.

## External game cores

- **PS2 ISO** → Neutrino (external ELF, configured or discovered with TOML sidecars), with the R3Z per-launch compatibility modes 0/2/3/5/7, GSM/field-flipping, debug colors and PS2 logo. Options default to launch-only unless the user explicitly saves a per-Game-ID Neutrino profile in the dashboard; separate checksum-verified A/B settings files preserve the last valid copy and never modify the ISO, global CNF, or active VMC. ISO `SYSTEM.CNF` game IDs also drive RetroGem display and best-effort active-MMCE card switching, without initializing or replacing live storage drivers.
- **Loader availability** → Three independent Disable Ember/POPStarter/Neutrino controls in Settings → Launching → Disc & image loaders, Off by default.
- **PS1 VCD** → POPStarter (external launcher, own VCD and HDD argument conventions).
- **PS1 CUE/BIN/EXE** → Ember (external ELF), only under a save-safe `<umbrella>/games/<Game>/` folder. The umbrella can be anywhere and can be named arbitrarily. Ember disc images show the same RetroGem Game ID as an equivalent VCD when the disc Game ID preference is on.
- For Ember on USB, the launcher rebuilds the storage IOP without resident PADMAN and confirms game identity after USB mount-number reassignment before final keep-IOP launch.
- Normal generic ELF reset preferences do not govern Neutrino or Ember's dedicated handoff.

These external programs, user BIOS and commercial games are not bundled. See the [full web documentation](https://nathanneurotic.github.io/Launchstation/) for per-core setup and troubleshooting.

## Backups, cloud and updates

- **MMCE backups:** Settings → Storage & devices → MMCE backups (backup folder, Sync all cards, automatic sync Off/Local/Local+Google, IGR helper, backups per card, `.mcd` or PCSX2 `.ps2`). Recovery always produces a new verified file; see [MMCE recovery](MMCE-Recovery.md). The automatic backup after IGR skips unchanged cards by a fingerprint of each card's directory, with a full compare at least every 8 backups.
- **Card contents:** File Browser → **Card contents...** on a `.mcd`, `.ps2`, `.bin`, `.vmc` or `.mc2` image lists its saves and can copy, move, delete, export or import (`.psu`) one save, or copy the whole card to a new 8-64 MB size. **All saves here...** in a folder lists the saves of every card image there and in its subfolders together (on MMCE, every card on the adapter), sorted by title with each save's card named, and opens the same actions for any of them; **Square** marks several saves and **Marked saves...** copies, moves or deletes them in one batch with one safety copy per card. **Save library here...** lists the `.psu` saves in a folder and its subfolders, copies them to a card without replacing anything, saves marked ones as a named set (a subfolder), and applies a set to a card after one confirmation (a set with files beyond the 500-file list cannot be applied, so no set is applied in part). Every card write starts with a verified safety copy in `WLE-Safety/`, and the card an MMCE adapter is serving is never written. In an `mmceN:` folder, **Which MMCE card is in use?** and **Make this the active MMCE card...** show and change the selected card, confirmed by content. Not yet tested on a console.
- **Google Drive:** linked with a device code (official builds embed Launchstation's OAuth client). Tools → Google Drive backups uploads missing history (copy-only) and browses cloud history.
- **Updates:** Settings → Storage & devices → MMCE backups → Check for updates; see [Releases and recovery](Releases-and-Recovery.md).
- **Networking:** FTP and PS2Link start on demand from Tools → FTP / Remote access, and the UDPFS client can auto-start from Settings → Storage & devices. Settings → Network holds the IP configuration, Legacy Ethernet drivers and Sync PS2 clock from Internet.

## Languages, themes and clock

- English, Spanish, Italian, French, German, Polish, Portuguese, Brazilian Portuguese and Hungarian (`Lang/`); external `.LNG` files override the same message IDs. Filenames, paths and application metadata keep their original text.
- 18 built-in theme presets plus custom colors, wallpapers, fonts, icons and clock; shareable `THEMES/<name>/theme.cfg` folders. See the [wallpaper guide](Theme-and-Wallpaper-Guide.md).
- Settings → System → Set software clock keeps a saved checkpoint for consoles with a dead CMOS battery.

## Upstream wLaunchELF_R3Z integration

Launchstation selectively incorporates upstream wLaunchELF_R3Z changes (the October 2026 browser and title compatibility updates from [upstream PR #56](https://github.com/saildot4k/wLaunchELF_R3Z/pull/56), the four browser display modes, Neutrino ISO Game IDs, and the persisted-settings families audited by `scripts/audit_r3z_settings.py`). Existing MMCE, USB, networking, dashboard and Ember launch paths were **not** replaced by upstream code. Ordinary file paste follows upstream R3Z direct-overwrite behavior.
