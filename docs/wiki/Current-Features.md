# Current Launchstation features and navigation

This guide reflects the code on `master`: the four visible tabs in `src/dash_main.c` (`visible_sections`), the Home/Files integration and the settings pages in `src/dash_settings.c`.

| Visible tab | User-facing behavior | Source |
| --- | --- | --- |
| **Home** | Continue (pinned, recent, favorites), nested Applications library (Grid/List/Carousel), Last folder and configured launch-key shortcuts | `src/dash_home.c`, `src/dash_apps.c` |
| **Files** | Direct persistent File Browser, remembered folder and selection across tabs; device roots | `src/dash_browser.c`, `src/dash_files.c`, `src/dash_devices.c` |
| **Tools** | File Browser shortcut to Files, Text Editor, Start disc, PS1/PS2 image shortcuts, MISC commands, USB unit diagnostics, HDD Manager, memory card exploit installer, OPL IGR setup, PS2 browser, OSDSYS, FTP / Remote access, Google Drive backups, system and build information, Reboot IOP, Power off | `src/dash_tools.c` |
| **Settings** | Appearance, Home & applications, Sound, Input, Launching, Storage & devices (with MMCE backups), Network, System | `src/dash_settings.c` |

L1/R1 switches tabs; START opens the Dashboard menu from native screens. SELECT changes Grid/List/Carousel in Home's Applications library. Lists wrap around, and page jumps stop at the boundaries before wrapping.

Applications can be hidden from Home and the library with **Hide from view**; choose the **All including hidden** filter to find them again. Tools → MISC commands opens a popup menu. The dashboard is the only interface; classic file, HDD and launch logic runs underneath it. `DASH_SEC_APPS` and `DASH_SEC_DEVICES` are internal compatibility constants, not extra tabs.

The header can show console temperature beside the clock on supported consoles (Celsius/Fahrenheit in Settings → System). Video modes include 576i, and 576p only where the console ROM supports it. Internet clock sync uses SNTP to `0.pool.ntp.org` through the on-demand PS2IP socket server.

## Files and app roots

Default app scanning: `mc:/APPS`, `mc:/BOOT`, `mass:/APPS`, `mass:/BOOT`, each one subfolder deep. Adjust under Settings → Home & applications → Application library → Application folders. Other paths can still be browsed/launched from Files.

The File Browser's **Show** menu offers four display modes: file names, names and details, game titles and details, or game titles.

**Sort by** defaults to **Type**: folders, executables, text files, then other files grouped by extension. As stored, Name, Game title, Size, Date-Time and Kind are also available. Ascending/descending applies to every mode except As stored and Type. *Tuna date lives under **Folder dates** for eligible memory-card folders.

Other actions include copy/cut/paste (also with new names), PSU create/extract, VMC mounting, **Card contents...** for saves inside card images, bookmarks (PathPad), Use as launch arguments, and an image viewer for PNG/JPG/JPEG.

For storage, do not equate `Not checked` with physically absent. Lazy drivers start when selected. Logical USB mount numbers can change. Use Tools → USB unit diagnostics deliberately; a manual fileXio probe can block in an IOP RPC.

## Devices

Memory cards (`mc0:`/`mc1:`), USB (`usb:`/`usbN:`), MMCE (`mmce0:`/`mmce1:`), MX4SIO, iLink (`ilink:`), internal HDD (`hdd0:`), exFAT ATA (`ata0:`), PC CARD CompactFlash (`cf0:`, early Japanese models), disc (`cdfs:`), UDPFS (`udpfs:`), SMB2/SMB3 shares (`smb:/<NAME>`) and an optional SMB1 share (`smb1:`), PS2Link host (`host:`) and, on PSX, the flash and DVR HDD. Entries that cannot exist on the running console are hidden. MMCE and MX4SIO are mutually exclusive and cause an IOP reset when switched. PC CARD CF is integrated into the device list and file operations but has not been verified on a physical console.

## External game cores

- **PS2 ISO** → Neutrino (external ELF, configured or discovered with TOML sidecars), with the R3Z per-launch compatibility modes 0/2/3/5/7, GSM/field-flipping, debug colors and PS2 logo. Options default to launch-only unless the user explicitly saves a per-Game-ID Neutrino profile in the dashboard; separate checksum-verified A/B settings files preserve the last valid copy and never modify the ISO, global CNF, or active VMC. ISO `SYSTEM.CNF` game IDs also drive RetroGem display and best-effort active-MMCE card switching, without initializing or replacing live storage drivers.
- **Loader availability** → Three independent Disable Ember/POPStarter/Neutrino controls in Settings → Launching → Disc & image loaders, Off by default.
- **PS1 VCD** → POPStarter (external launcher, own VCD and HDD argument conventions).
- **PS1 CUE/BIN/EXE** → Ember (external ELF), only under a save-safe `<umbrella>/games/<Game>/` folder. The umbrella can be anywhere and can be named arbitrarily. Ember disc images show the same RetroGem Game ID as an equivalent VCD when the disc Game ID preference is on.
- For Ember on USB, the launcher rebuilds the storage IOP without resident PADMAN and confirms game identity after USB mount-number reassignment before final keep-IOP launch.
- Normal generic ELF reset preferences do not govern Neutrino or Ember's dedicated handoff.

The app-only packages contain no external game loaders. The optional `LAUNCHSTATION-<build>.zip` full kit supplies POPStarter, Ember and Neutrino; no package supplies PlayStation BIOS or commercial game data. POPStarter still needs your own `POPS_IOX.PAK` (and any required `IOPRP.IMG`), and Ember needs your own 512 KB `bios.bin`. See [kit contents and required user files](https://nathanneurotic.github.io/Launchstation/install.html#kit) and the per-core guides before launching games.

## Backups, cloud and updates

- **MMCE backups:** Settings → Storage & devices → MMCE backups (backup folder, Sync all cards, automatic sync Off/Local/Local+Google, IGR helper, backups per card, `.mcd` or PCSX2 `.ps2`). Recovery always produces a new verified file; see [MMCE recovery](MMCE-Recovery.md). The automatic backup after IGR skips unchanged cards by a fingerprint of each card's directory, trusting a match for at most eight skips in a row before the next full byte comparison.
- **Google Drive:** linked with a device code (official builds embed Launchstation's OAuth client). Tools → Google Drive backups uploads missing history (copy-only) and browses cloud history. Optional **Per-save versions** (MMCE backups settings, Off by default) also keeps each changed save as a `.psu` in `WLE-Saves/<save>/` beside its card history, uploaded with the cards; a single version downloads from cloud history as a verified `.psu` (#232; pending hardware validation).
- **Updates:** Settings → Storage & devices → MMCE backups → Check for updates; see [Releases and recovery](Releases-and-Recovery.md).
- **Networking:** FTP and PS2Link start on demand from Tools → FTP / Remote access, and the UDPFS client can auto-start from Settings → Storage & devices. Settings → Network holds the IP configuration, Legacy Ethernet drivers and Sync PS2 clock from Internet.

## Save and card-image tools

| Action | What it does |
| --- | --- |
| **Card contents...** | Opens a `.mcd`, `.ps2`, `.bin`, `.vmc` or `.mc2` image. Copy, move, delete or export (`.psu`, a signed PS3 `.psv` or a CodeBreaker `.cbs`) a save, import one (`.psu`, a Blaze save from an `MCM` folder, or a `.mcm`, `.max`, `.cbs`, `.sps`, `.xps` or `.psv` export from another tool), or copy the whole card to a new 1–64 MB image (1, 2, 4, 8, 16, 32 or 64 MB). A PS1 card (a 128 KB POPStarter `.VMC`, sd2psx/MMCE PS1-mode, `.mcr` or `.vm1` image, or a DexDrive `.gme`, Connectix `.vgs`/`.mem`, PSP/Vita `.vmp` or PocketStation iOS `.mcx` card) opens a PS1 view: import `.mcs`, `.psv`, `.psx` or raw PS1 saves, export one as `.mcs`, signed PS3 `.psv`, `.psx` or raw, delete one, or save a copy of the card in another of those containers. The card is written back in the container it came in. |
| **Convert to .psu, .psv or .cbs...** | On a `.max`, `.cbs`, `.sps`, `.xps`, `.psv` or `.mcm` save from another tool, or any file in a Blaze `MCM` folder (memory cards included): writes it as a `.psu`, a signed PS3 `.psv` or a CodeBreaker `.cbs` beside the original (from a card: in a folder you pick) without replacing anything. On a `.psu`: **Convert to .psv or .cbs...**. The `.cbs` holds the data uncompressed (stored zlib blocks), so it is larger than one CodeBreaker writes itself. |
| **All saves here...** | Lists saves across card images in a folder and its immediate subfolders, within limits of 64 cards, 256 folders and 1,000 saves. Entries are sorted by title with the source card shown. Square marks saves; **Marked saves...** copies, moves or deletes them in one batch. |
| **Save library here...** | Lists `.psu` saves in a folder and its immediate subfolders and copies them to a card without replacing existing saves. Save marked entries as a named set (a subfolder), then apply the set after one confirmation. Sets beyond the 500-file list limit are refused rather than partly applied. |
| **Which MMCE card is in use?** | Shows the selected card from an `mmceN:` folder. **Make this the active MMCE card...** switches supported numbered cards and confirms the selection by content. |

Every card write starts with a verified copy in `WLE-Safety/`; batch operations make one safety copy per card. A card currently served by an MMCE adapter is never written. PCSX2 `.ps2` images and saves with subfolders are read-only here; unmount images from `vmc0:`/`vmc1:` before editing them. These tools are host-tested and still need console validation. See the [save-management guide](https://nathanneurotic.github.io/Launchstation/saves.html) for the full workflow and limits.

## Languages, themes and clock

- English, Spanish, Italian, French, German, Polish, Portuguese, Brazilian Portuguese and Hungarian (`Lang/`); external `.LNG` files override the same message IDs. Filenames, paths and application metadata keep their original text.
- 18 built-in theme presets plus custom colors, wallpapers, fonts, icons and clock; shareable `THEMES/<name>/theme.cfg` folders. See the [wallpaper guide](Theme-and-Wallpaper-Guide.md).
- Settings → System → Set software clock keeps a saved checkpoint for consoles with a dead CMOS battery.

## Upstream [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) integration

Launchstation selectively incorporates upstream [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) changes (the October 2026 browser and title compatibility updates from [upstream PR #56](https://github.com/saildot4k/wLaunchELF_R3Z/pull/56), the four browser display modes, Neutrino ISO Game IDs, and the persisted-settings families audited by `scripts/audit_r3z_settings.py`). Existing MMCE, USB, networking, dashboard and Ember launch paths were **not** replaced by upstream code. Ordinary file paste follows upstream R3Z direct-overwrite behavior.
