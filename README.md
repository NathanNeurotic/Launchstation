<p align="center">
  <img src="docs-site/static/assets/launchstation-logo.avif" alt="Launchstation — PlayStation 2 dashboard and file manager" width="800">
</p>

# __Launchstation__

**Launchstation** is a maintained PlayStation 2 file manager, executable launcher, dashboard and device toolkit built from **wLaunchELF_R3Z** and the wider wLaunchELF/uLaunchELF/LaunchELF lineage. It extends that base with a controller-first dashboard, modern storage/device workflows, Neutrino, POPStarter and Ember handoffs, MMCE backup and Google Drive features, networking tools and additional hardware support.

Repository: <https://github.com/NathanNeurotic/Launchstation> · Documentation: <https://nathanneurotic.github.io/Launchstation/> · Latest build: <https://github.com/NathanNeurotic/Launchstation/releases/tag/latest>

## Quick start

1. Download **`APP_LAUNCHSTATION-<build>.zip`** (standard PS2 controllers) or **`APP_LAUNCHSTATION-DS-<build>.zip`** (adds PS3/PS4 DualShock USB/Bluetooth support) from the [`latest` release](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest). Every ZIP name ends with the build number, for example `APP_LAUNCHSTATION-284.zip`, and the release title and notes state the same number. The matching `.psu` files are the same contents for memory-card installation.
2. Extract the `APP_LAUNCHSTATION` folder to a filesystem the console can boot from (memory card, USB, MMCE, MX4SIO, iLink, internal HDD/ATA), or copy the `.psu` in the File Browser and use **Extract PSU** (the PSU paste action) in a memory-card folder to install it.
3. Start `LAUNCHSTATION.ELF` (or `LAUNCHSTATION-DS.ELF`) from your usual launcher, FreeMcBoot/OPL/exploit entry or file manager. The packaged `title.cfg` is an OPL-style application descriptor (`title=`, `boot=`) so launchers that read it list the app as "Launchstation".
4. Use **L1/R1** to move between **Home, Files, Tools and Settings**. In Files, pick a device, then a folder.

Launchstation is a homebrew program for real PS2 hardware. A passing CI build proves that the code compiles, links, packages and passes its host tests; it does not replace console testing. Reports should include the console model, storage devices and commit hash (Tools > About Launchstation shows the version and commit).

## Documentation

The full Launchstation reference is a PS2-styled GitHub Pages site generated from `docs-site/`:

- **Online:** <https://nathanneurotic.github.io/Launchstation/>
- **Offline:** `python3 docs-site/build.py --out build/docs --zip build/Launchstation-Documentation.zip`
- **Markdown wiki (version-controlled):** [docs/wiki/Home.md](docs/wiki/Home.md), synchronized to the repository's GitHub Wiki by CI
- **Changes:** [CHANGELOG.md](CHANGELOG.md) (the `## Rolling` section is what the GitHub `latest` release page shows)

The site covers installation, dashboard controls, File Browser, Applications, Text Editor, storage and device support, APA/PFS, iLink, MMCE, networking, POPStarter VCD launching, Ember PS1 image launching, Neutrino ISO launching, IGR/MMCE backups, native Google Drive sync, settings, configuration files, themes, troubleshooting and developer notes.

## Release packages and updates

Every verified push to `master` publishes four assets to the single moving **`latest`** release (the tag is only ever advanced to a descendant commit):

| Asset | Contents | Use |
|---|---|---|
| `APP_LAUNCHSTATION-<build>.zip` | `APP_LAUNCHSTATION/LAUNCHSTATION.ELF`, `title.cfg`, `WLE-UPDATE-CA.PEM`, `BGM.ADP` | Extract to a filesystem |
| `APP_LAUNCHSTATION.psu` | Same four files | Copy, then Extract PSU onto a memory card |
| `APP_LAUNCHSTATION-DS-<build>.zip` | `APP_LAUNCHSTATION-DS/LAUNCHSTATION-DS.ELF` + the same three sidecars | DualShock (DS34) variant, filesystem |
| `APP_LAUNCHSTATION-DS.psu` | Same four files | DualShock variant, memory card |
| `LAUNCHSTATION.ELF`, `LAUNCHSTATION-DS.ELF` | The packed ELF alone (standard / DS34) | When you only need the executable |
| `BOOT.ELF`, `BOOT2.ELF` | Byte-identical copies of the standard `LAUNCHSTATION.ELF` | Boot paths that look for those names |

Both executables are packed (`ps2pack`). The ELF runs without its sidecars, but `WLE-UPDATE-CA.PEM` is needed for HTTPS update checks and `BGM.ADP` is the optional background music. The two variants share the same settings and optional sidecars.

**In-app updates.** *Settings > Storage & devices > MMCE backups > Check for updates* (builds with networking and native cloud, which includes the release packages) compares the running commit with the `latest` release using GitHub's commit-comparison API. A release that is not strictly newer, is divergent from your build, or whose ancestry cannot be verified is never offered. For a newer release the updater downloads the PSU matching your variant (standard or DS), checks it against the SHA-256 digest GitHub publishes for that asset, extracts and validates the ELF, and only then replaces the running executable, keeping the previous one as `<name>.ELF.BAK`. From a verified four-file PSU it also adds a **missing** `WLE-UPDATE-CA.PEM` or `BGM.ADP`; existing sidecars, including a custom BGM, are never overwritten. An existing `.UPDATE` or `.PACKAGE` file from an interrupted attempt is not overwritten either: inspect and clear it manually. Supported boot locations are `mc`, `mass`/`usb`, `mmce`, `mx4sio`, `ilink`, `ata` and `hdd` paths. Restart Launchstation to run the new build.

**Upgrading older builds.** Previous updaters expect standalone ELFs or two-file PSU packages and cannot install the four-file PSU automatically. Install the matching complete ZIP/PSU once by hand (keep your existing settings), after which in-app updates work. Legacy two-file PSU updates remain ELF-only.

**Rollback.** Keep the `.BAK` copy, or a known-good build; [the Releases and Recovery guide](docs/wiki/Releases-and-Recovery.md) has the details, including the preserved [2026-10-08 USB-tested baseline](https://github.com/NathanNeurotic/Launchstation/releases/tag/usb-second-drive-2026-10-08-58d4514).

Background music is **not embedded** in either ELF. For a standalone/local ELF build, copy `res/music.sad` unchanged beside the ELF as `BGM.ADP`, or omit it for a music-free install. Interface sound effects remain embedded. See [background music](docs/dashboard.md#optional-background-music).

## Dashboard

Launchstation starts in a controller-first dashboard with four tabs, switched with **L1/R1**:

| Tab | What it holds |
|---|---|
| **Home** | Continue, pinned/favorite/recent items and the **Applications** library (Grid, List or Carousel view with SELECT; applications can be hidden from view), artwork, and a Last folder shortcut |
| **Files** | The native File Browser (device roots, remembered folder and selection, PSU, VMC, MMCE and image tools) |
| **Tools** | File Browser, Text Editor, Start disc, PS1/PS2 image shortcuts, MISC commands, USB unit diagnostics, HDD Manager, memory card exploit installer, OPL IGR setup, PS2 browser, OSDSYS, FTP / Remote access, Google Drive backups, system/build information, Reboot IOP and Power off |
| **Settings** | Appearance, Home & applications, Sound, Input, Launching, Storage & devices (including MMCE backups), Network and System |

The dashboard is the only interface: Settings, the File Browser, the HDD Manager, the Text Editor, the exploit installer and the information screens are all dashboard screens, and most file browsing runs on a background worker. Some device initialization and USB filesystem probes are still synchronous IOP RPCs and can block on faulty or unresponsive USB hardware or drivers. See [docs/dashboard.md](docs/dashboard.md) for controls, metadata, artwork naming and the files it writes.

It is fully themeable from the controller: 18 built-in PS2-inspired theme presets, custom RGB colours, backgrounds, wallpapers, fonts, icons and clock, plus shareable theme folders. See [docs/themes.md](docs/themes.md) and the [wallpaper guide](docs/wiki/Theme-and-Wallpaper-Guide.md).

## Features

**Launching**
- Launch ELF, KELF (encrypted ELF), ISO, VCD and Ember disc-image entries from any browsed device; ELFs from the Applications library, favorites or the File Browser. Device-agnostic: Launchstation works from the device you browsed instead of assuming `mass0:` or `mc0:`.
- **Neutrino** for PS2 ISO images, with custom or discovered loader paths, the R3Z per-launch compatibility modes (0/2/3/5/7), GSM video, debug-colors and PS2-logo options. A valid ISO `SYSTEM.CNF` Game ID is used for RetroGem, and optionally for MMCE card switching with manager-only bring-up (excluding active MX4SIO and protected source-card slots).
- **POPStarter** for PS1 VCDs, with an optional custom POPStarter path and device-specific fallbacks.
- **Ember** for PS1 CUE/BIN/EXE images from any save-safe `<Ember directory>/games/<Game>/` umbrella. The umbrella name and location are configurable, and a configured Ember directory can supply `ember.elf` to a device that lacks it. Format selects the core automatically. Ember disc images can show the same [RetroGem Game ID](docs/EMBER-RETROGEM.md) as an equivalent VCD, controlled by the existing disc Game ID preference.
- Independent switches to disable Ember, POPStarter and Neutrino (Settings > Launching > Disc & image loaders), all Off by default.
- [LaunchELF with arguments](#launching-with-arguments), `<elf name>.arg` sidecars, and launch history written for disc launches (MMCE VMC change).
- X/O confirm default follows the console region when no config file exists (O is only the default for Japan). Applies deckard disc patches.
- RetroGem Game ID output for the [PIXEL FX RetroGem](https://www.pixelfx.co/hdmi-retro-gem) HDMI adapter (Settings > Launching > Game ID output); the colored identification bars before an ELF opens can be switched off there.

**Files and storage**
- Copy/cut/paste (also with new names), rename, delete, new folder, folder icons, `title.cfg`, size, multi-select, bookmarks (PathPad), sorting (name, game title, date or as stored, reversible) and name/title/details display modes, with progress popups that show the file name, remaining size and a clear failure reason.
- PSU create/extract, VMC mounting (`vmc0:`/`vmc1:` from `.bin`/`.vmc`/`.mc2`/`.ps2`), timestamp tools, and organizing the OSDSYS/HOSDSYS MC and `hdd0:__common:pfs:/*` browser order (Manual, A-Z, Z-A, SAS timestamps; the *Tuna date is under Folder dates for eligible memory-card folders).
- Native image viewer for PNG, JPG and JPEG opened from the File Browser: previous/next browsing, rotation, zoom/pan, fullscreen and slideshow.
- Text Editor with extra file-extension shortcuts.
- HDD Manager for APA partitions, [APA header injection](#hdd-apa-header-injection) and viewing/editing the partition `system.cnf`; dual internal HDD/ATA support is built in.
- Multi-USB support; drivers (everything except the core) load on demand for faster boot and broad device compatibility.
- Warnings when modifying or deleting exploit folders on PS2/PSX.

**Backups and cloud**
- MMCE backup & sync page: Sync all cards, Off/Local/Local+Google automatic sync, IGR helper management, backups per card, native `.mcd` or PCSX2 `.ps2` format.
- [Verified local and Google Drive recovery](#local-mmce-backup-recovery) to new files, never over an active card.
- Native Google Drive upload and history browsing over HTTPS (Mbed TLS), linked with a code at google.com/device, with no setup files in official builds.

**Network**
- FTP and PS2Link are in the standard build and start only when requested from Tools > FTP / Remote access (which also accepts staged, resumable ISO transfers from MrRexD-compatible PC tools). The UDPFS client for the [UDPFSD server](https://github.com/pcm720/udpfsd) can be auto-started or opened on demand; Sync PS2 clock from Internet is under Settings > Network.

**System**
- LoadBOOTer memory card exploit installer for retail PS2/PSX and DEX (DTL-HXXXXX), with [custom ELF binding](#bind-an-elf-to-a-memory-card-as-an-exploit-update).
- PS3/PS4 DualShock USB/Bluetooth support in the DS34 variant (drivers by Alex Parrado).
- [Software clock](#software-clock-for-consoles-with-a-dead-cmos-battery) for consoles with a dead CMOS battery.
- Device entries that cannot exist on the running console are hidden: internal HDD/ATA on deckard and PSX, XFROM and DVR HDD on non-PSX, iLink on consoles without the port (see the device table).
- On-screen keyboard layouts: QWERTY, DVORAK, AZERTY, QWERTZ, ABNT, ABC.
- Languages: English, Spanish, Italian, French, German, Polish, Portuguese, Brazilian Portuguese and Hungarian (`Lang/`). Dashboard navigation, Settings labels, common actions and transfer progress follow the selected language; external `.LNG` files override the same message IDs. File names, paths and application metadata keep their original text, and some extended help remains English.
- All configuration options are exposed in the Settings GUI.

## Supported devices

| Device | Description | Visibility |
|---|---|---|
| `mc0:/` `mc1:/` | Memory cards | Always |
| `usb:/` (`usbN:`, legacy `mass:`) | FAT/exFAT USB mass storage (BDM) | Always |
| `mmce0:/` `mmce1:/`[^1] | Multi-purpose memory card emulator: SD2PSX, PSxMemCard Gen2, MemCard PRO 2 | Always |
| `mx4sio:/`[^1] | SD card interface in a memory-card port | Always |
| `ilink:/` | IEEE1394 (i.Link/FireWire) storage | Hidden on retail PS2 ROMs 1.70 and newer (SCPH-50000 onward, which lack the port); hidden on PSX (its i.Link port is for Sony camcorders only); shown on earlier retail models and unrecognized consoles |
| `hdd0:/`[^2] | APA-formatted internal HDD | Hidden on deckard consoles (SCPH-75K+) and PSX; otherwise unit 1 is hidden by default (Settings > Storage & devices) |
| `ata0:/`[^2] | BDM/exFAT hard drive | Same visibility rules as `hdd0:/` |
| `cf0:/`[^3] | PC CARD CompactFlash (early Japanese PS2 models) | Retail JP ROMs 0100/0101/0110/0120 (SCPH-10000/15000/18000 family) or when show-all-devices is enabled |
| `xfrom0:/` | PSX DESR-XXXX flash storage | Hidden on non-PSX |
| `dvr_hdd0:/` | PSX DESR-XXXX digital video recorder HDD partition | Hidden on non-PSX |
| `cdfs:/` | CD/DVD file system | Always |
| `udpfs:/` | Network device used with [pcm720's UDPFSD server](https://github.com/pcm720/udpfsd) | Always |
| `host:/` | PS2Link host filesystem | When network support is built (standard release builds) |

Drivers load on demand; **Not checked** means the driver has not been started, not that nothing is connected. USB unit numbers follow attach order, so do not assume `usb0:` is a particular drive.

[^1]: MMCE and MX4SIO cause an IOP reboot when switching between them, because the two are incompatible.

[^2]: Dual HDD/ATA support is built in for future development.

[^3]: PC CARD CF (`cf0:/`) uses the PCMCIA slot on early Japanese consoles. The PCCF driver is embedded and integrated into the device list and file operations; direct ELF/game execution from `cf:/`/`cf0:/` is unsupported and DEV9 is mutually exclusive with the CF bus. It is only built in `EXFAT` configurations, and a physical-console test is still required before marking the hardware verified.

## Building

Release builds use the [PS2BUILD](https://git.techwritescode.dev/ps2) SDK with its **LLVM/Clang** toolchain (`toolchain: llvm` in `ps2.yaml`, for both EE and IOP code). Third-party EE archives must come from the same toolchain, so run these once in a fresh checkout (and again after changing toolchain):

```
ps2build update                  # SDK packages
ps2build update llvm-toolchain   # only if the Clang toolchain is missing
bash scripts/build_cloud_tls.sh  # Mbed TLS 3.6.7 -> .cloud-deps/
bash scripts/build_jpeg.sh       # libjpeg-turbo 3.1.3 -> .jpeg-deps/
python3 scripts/gen_cloud_client.py
python3 scripts/gen_githash.py --require-git
ps2build build
```

- Outputs: `build/bin/wLaunchELF-packed.elf` (the ps2pack-compressed release ELF) and `build/bin/wLaunchELF.elf`.
- `scripts/gen_cloud_client.py` embeds Launchstation's Google OAuth client from `WLE_GOOGLE_CLIENT_ID` and `WLE_GOOGLE_CLIENT_SECRET` (set both, or neither). Without them a local build still compiles, but Drive linking needs a custom client in `WLE-CLOUD.CNF`.
- `scripts/gen_githash.py` stamps the commit hash and date shown in About; `--require-git` fails instead of falling back to `local`.
- `build_cloud_tls.sh`, `build_jpeg.sh` and `ps2build` all pick the toolchain with the same precedence: `PS2BUILD_TOOLCHAIN`, then `ps2.yaml`. **GCC comparison build:** export `PS2BUILD_TOOLCHAIN=gcc`, run all of the above, and use a separate output directory (`ps2build build -o build-gcc`) so Clang and GCC archives never mix.
- `ps2.yaml` enables MMCE, MX4SIO, iLink, EXFAT, UDPFS, ETH (FTP/PS2Link, demand-started), DVRP, XFROM, TMANIP, native Google Drive cloud and custom colours; DS34 is off. Ethernet/FTP support is embedded but no network driver is started during normal boot. The ETH profile also builds the vendored `iop/ps2ips_rpc` PS2IP socket server (used by Internet clock sync).
- The PC CARD CF IOP module can also be built on its own with `ps2build build -c iop/pccf/ps2.yaml`.

**DualShock (DS34) variant** (retained wLaunchELF_R3Z drivers and `pad_ds34.c`):

```
python3 scripts/configure_ds34.py
ps2build build -c .ps2-ds34.yaml -o build-ds34
```

**Installable packages:** `python3 scripts/package_apps.py --standard build/bin/wLaunchELF-packed.elf --dualshock build-ds34/bin/wLaunchELF-packed.elf --output artifacts --revision <git sha> --epoch <commit time>` writes the four release assets.

**Build system.** The legacy `Makefile` build was retired on 2026-10-26; `ps2build` with `ps2.yaml` (LLVM/Clang) is the only build path. `PS2BUILD_TOOLCHAIN=gcc` still gives a GCC comparison build.

**Tests.** Host-side regression tests live in `tests/` and run with plain Python, for example `python3 tests/test_self_update.py`; CI runs the full list (see `.github/workflows/compile.yml`). Documentation changes should at least pass:

```
python3 docs-site/build.py --out /tmp/site
python3 tests/test_wallpaper_docs.py
python3 tests/test_project_branding.py
```

### Continuous integration

`.github/workflows/compile.yml` (**Automated-Build**) runs on pushes to `master`/`main`, pull requests and manual dispatch:

1. Host regression tests, the copy-test fixture pack and a documentation-site build.
2. Installs the ps2build SDK and LLVM toolchain; builds Mbed TLS, libjpeg-turbo and the PC CARD CF module.
3. Builds the standard and DS34 ELFs, then verifies the controller variants in the linked ELFs, that release ELFs contain only Clang-built objects (`scripts/check_toolchain_purity.py`), the UDPFS SMAP ABI, and the embedded loader's low-memory layout.
4. Packages the four app archives (`scripts/package_apps.py`), re-reads them, and uploads them as a build artifact.
5. **Only for pushes to `master` and manual dispatch on `master`:** uploads the four assets to the `latest` release, downloads them back to compare bytes, moves the `latest` tag to the tested commit (refused unless the old tag is an ancestor), and marks it as the GitHub Latest release. Pull requests build and test but never publish.

`.github/workflows/docs-pages.yml` rebuilds the site and force-publishes it to `gh-pages` when `docs-site/**` changes. `.github/workflows/sync-wiki.yml` mirrors `docs/wiki/` and the authored site pages into the GitHub Wiki.

## Neutrino per-game profiles (optional)

Valid PS2 ISO Game IDs can load a separately saved Neutrino compatibility/video preset from Launchstation's dashboard settings directory. Use **Save for this game** in the launch dialog; ordinary Run never persists edits. Two verified per-title slots preserve the last valid settings if writing is interrupted. Missing, corrupt or inaccessible profiles fall back to launch-only defaults, without rewriting any ISO, VMC or `LAUNCHELF.CNF`. The console may skip profile I/O when MMCE/MX4SIO currently owns incompatible memory-card access.

## Local MMCE backup recovery

Recovery never overwrites an existing or active MMCE card. From **Settings > Storage & devices > MMCE backups**, choose **Recover saved card image...** to select a history snapshot directly. In the File Browser you can also:

- select a verified local `.mcd` or PCSX2 `.ps2` snapshot and use **Recover as new .mcd...** (a fresh numbered `RECOVER-###.mcd` on USB/HDD, written through a verified staged copy);
- select a card's `.mcd` under `mmce0:/MemoryCards` or `mmce1:/MemoryCards` and choose **Back up selected MMCE card to history...**;
- choose **Store NEW snapshot on MMCE...** to keep a verified `.mcd`/`.ps2` on an adapter using its **existing firmware**, in `mmceN:/WLE-Recovered`, outside all firmware-selectable card paths (not activated as a game card);
- choose **Install as NEW selectable MMCE card...** on stock sd2psXtd firmware to allocate an unused `MemoryCards/PS2/CardN/CardN-1.mcd`, verified after writing. Launchstation never switches cards automatically, and failed or interrupted installations must not be selected before inspection.

**Cloud recovery:** in Tools > Google Drive backups, linked users can browse uploaded `WLE-MMCE-Backups` history and download an individually selected snapshot that carries its SHA-256 digest to a NEW verified `RECOVER-CLOUD-###.mcd` on USB/HDD. No Drive history is altered and no emulated card is replaced; items without an uploader digest stay view-only.

**Card contents:** **Card contents...** on a `.mcd`, `.ps2`, `.bin`, `.vmc` or `.mc2` image lists its saves and can copy, move, delete, export or import (`.psu`) a single save, or copy the whole card to a new 8-64 MB size. Every write starts with a verified safety copy in `WLE-Safety/` and ends with a read-back, and a card the MMCE adapter is serving is never written. In an `mmceN:` folder, **Which MMCE card is in use?** and **Make this the active MMCE card...** show and change the selected card, confirmed by content. The automatic IGR backup now skips unchanged cards by a fingerprint of the card's directory, with a full compare at least every 8 backups. Host-tested only; not yet tested on a console. Details: [Card contents](https://nathanneurotic.github.io/Launchstation/saves.html#card-contents).

Direct overwrite of an existing MMCE card remains disabled. See [the dashboard recovery notes](docs/dashboard.md#recovering-local-mmce-backup-images), the [MMCE recovery guide](docs/wiki/MMCE-Recovery.md) and the [direct-restore safety protocol](docs/MMCE_RESTORE_PROTOCOL.md).

## Launching with arguments

In the File Browser, highlight a text file (CNF/CFG/TXT/INI/argument file), open the actions menu and choose **Use as launch arguments**, then select the application to start. The Text Editor offers the same action for its current in-memory buffer, so unsaved edits are used. **Start with arguments...** on an ELF and **Clear launch arguments** are in the same menu.

For ordinary executable launches Launchstation also looks for a sidecar named `<elf name>.arg` in the same folder (for example `BOOT.arg` for `BOOT.ELF`). Arguments cached through the browser take priority over the sidecar. Each non-empty line becomes one argument; line endings are stripped and other characters are preserved. The target receives its normal path as `argv[0]`, followed by the cached lines.

| Limit | Value |
|---|---:|
| User arguments | 12 lines |
| Characters per argument | 255 |
| Total cached argument text | 2,048 bytes including terminators |

## HDD APA header injection

In the HDD Manager (Tools > HDD Manager), the partition actions include header injection for PFS partitions.
Source devices are `usb:`, `mmce0:`, `mmce1:`, and `udpfs:`.
After choosing a source device, choose one injection mode (the site's [HDD Manager page](https://nathanneurotic.github.io/Launchstation/hdd-manager.html) has the full workflow):

| Mode | Behavior |
|---|---|
| `This partition only` | Injects only the currently selected PFS partition from `device:/__Headers/<selected partition>/`. |
| `Matching partitions` | Scans `device:/__Headers/` and injects every existing PFS partition with a matching folder name. |
| `Matching and create missing partitions` | Injects existing matches, then prompts for each valid header folder that has no matching partition. `No` skips that folder and continues; cancel stops the bulk operation. |

Header files must be placed in a flat folder matching the partition name:

`device:/__Headers/<matching partition name>/`


| File | Requirement | Destination | Maximum File Size | Format / Purpose | Notes |
|---|---|---|---:|---|---|
| `system.cnf` | Required | APA header | 512 bytes | Partition configuration | See [example](#example-systemcnf). |
| `icon.sys` | Required on PS2 | APA header | 1,024 bytes | Icon metadata | Not listed as required for PSX/DVR environments. |
| `list.ico` | Required on PS2 | APA header | 1,112,064 bytes | HDD-OSD icon | [Icon Generator](https://github.com/CosmicScale/HDD-OSD-Icon-Generator)  |
| `boot.kelf` or `BOOT.KELF` | Optional | APA header | 978,944 bytes | Executable KELF | Use KELFTool. |
| `BOOT.ELF` | Optional | `pfs:/BOOT.ELF` | N/A | Partition executable | Useful when the injected `boot.kelf` or `BOOT.KELF` is a KELF forwarder such as [OSDMenu Launcher](https://github.com/pcm720/OSDMenu/tree/main/launcher) |
| `info.sys` | Optional | `pfs:/res/info.sys` | N/A | Partition information used by PSBBN or PSX XMB | See [example](#example-infosys). |
| `jkt_001.png` | Optional | `pfs0:/res/jkt_001.png` | N/A | PSBBN image | 256x256 PNG 8-bit indexed color 32-bit RGBA palette. |
| `jkt_002.png` | Optional | `pfs0:/res/jkt_002.png` | N/A | PSX XMB image | 76x108 PNG 8-bit indexed color 32-bit RGBA palette. |
| `jkt_cp.png` | Optional | `pfs0:/res/jkt_cp.png` | N/A | PSX XMB copyright image | 290 pixels wide, 46-300 pixels high, 32-bit RGBA PNG. |

### Example system.cnf

```ini
BOOT2 = pfs:/boot.kelf
VER = 1.00
VMODE = NTSC
HDDUNITPOWER = NICHDD
```

### Example info.sys

```ini
title = [SYS] R3CONFIGURATOR
title_id = SYS-R3CONFI
title_sub_id = 0
release_date =
developer_id =
publisher_id = pcm720, R3Z3N
note =
content_web =
image_topviewflag = 0
image_type = 0
image_count = 1
image_viewsec = 600
copyright_viewflag = 0
copyright_imgcount = 0
genre =
parental_lock = 1
effective_date = 0
expire_date = 0
violence_flag = 0
content_type = 255
content_subtype = 0
```

## Bind an ELF to a memory card as an exploit update

In **Tools > Memory card exploit installer**, choose a supported installation mode and then **Choose other ELF**. Select a native `ELF`; its accompanying `PS2KEYS.dat` must be in the same source folder, for example:

```text
usb0:/APPS/MyApp.ELF
usb0:/APPS/PS2KEYS.dat
```

`PS2KEYS.dat` is user-provided, is required to sign a custom ELF, and is intentionally not included in this repository. It stays at the source location; it is not copied to the memory card.

The installer chooses the signing configuration from the detected console model:

| Console model | Key section | Header | System type |
|---|---|---|---|
| `SCPH-*` | `retail` | DNASLOAD | PS2 |
| `DESR-*` | `retail` | FMCB | PSX |
| `DTL-H*` or `DTL-T*` | `dev` | DNASLOAD | PS2 |
| `COH-*`, System 246/256, or Python 1 | `arcade` | DONGLE | PS2 |
| `SCPH-50000MB` / Python 2 | `retail` | DONGLE | PS2 |

**`PS2KEYS.dat` format.** An INI-style file with an optional `[default]` section and one section per key set (`[retail]`, `[dev]`, `[arcade]`); the selected section is overlaid on `[default]`. Values are hexadecimal (whitespace allowed) and must be exactly the stated length; `;` and `#` start comments. Ten keys are required:

| Key | Length |
|---|---|
| `MG_SIG_MASTER_KEY`, `MG_SIG_HASH_KEY`, `MG_KBIT_IV`, `MG_KC_IV`, `MG_ROOTSIG_MASTER_KEY`, `MG_CONTENT_TABLE_IV`, `MG_CONTENT_IV` | 8 bytes (16 hex digits) |
| `MG_KBIT_MASTER_KEY`, `MG_KC_MASTER_KEY`, `MG_ROOTSIG_HASH_KEY` | 16 bytes (32 hex digits) |

The `arcade` key set additionally requires `OVERRIDE_KBIT` and `OVERRIDE_KC` (16 bytes each); for any key set these two must appear together or not at all.

## Software clock for consoles with a dead CMOS battery

In **Settings > System**, choose **Set software clock...** and enter the local time. Choose **Save changes** when leaving Settings to store a checkpoint in `DASHBOARD.CNF`. The clock advances while Launchstation runs. Before restarting, use **Settings changes > Save current clock** to retain additional runtime even when no other setting changed, and wait for the pending save to finish before cutting power. The next start resumes the last saved checkpoint.

Time spent powered off or unplugged is unknown; a working RTC or an Internet time source (Settings > Network > Sync PS2 clock from Internet) is required for accurate wall time across those intervals. **Use PS2 hardware clock** restores the normal RTC source. Discard restores the previous clock setting along with the rest of the Settings session.

## More documentation

- [Searchable site and wiki index](https://nathanneurotic.github.io/Launchstation/wiki.html) · [Markdown wiki](docs/wiki/Home.md)
- [Dashboard tabs and Applications library](https://nathanneurotic.github.io/Launchstation/dashboard.html) · [docs/dashboard.md](docs/dashboard.md)
- [Verified MMCE local/cloud recovery](docs/wiki/MMCE-Recovery.md) · [Native Google Drive notes](docs/NATIVE_CLOUD.md)
- [Rolling releases and update/rollback policy](docs/wiki/Releases-and-Recovery.md)
- [Themes](docs/themes.md) · [Wallpaper guide](docs/wiki/Theme-and-Wallpaper-Guide.md)
- [Full external project and third-party license inventory](docs/THIRD_PARTY_CREDITS.md)

This documentation is authored on `master` and generated to `gh-pages` by CI; do not edit the generated branch.

## Credits & Acknowledgments

**Direct lineage and maintainers:** [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) by R3Z3N / Saildot4K; earlier wLaunchELF, uLaunchELF and LaunchELF authors; [johnson-cooper](https://github.com/johnson-cooper), who created and transferred this fork; [NathanNeurotic / Ripto](https://github.com/NathanNeurotic), current maintainer; [techwritescode](https://github.com/techwritescode), TnA, oMrRexD, Zack and FitthFox for collaboration, insight or testing as noted in the detailed credits.

**Source/dependency credits:** the [PS2BUILD SDK](https://git.techwritescode.dev/ps2) and its LLVM/Clang toolchain (used to build releases), [PS2SDK/ps2dev](https://github.com/ps2dev/ps2sdk), [ps2-mmce/mmceman](https://github.com/ps2-mmce/mmceman), [RiptOPL usbd-ra](https://github.com/NathanNeurotic/Open-PS2-Loader), [ps2ftpd](https://github.com/ps2homebrew/ps2ftpd), inherited DS34 driver authors including Alex Parrado, [Mbed TLS](https://github.com/Mbed-TLS/mbedtls), [libjpeg-turbo](https://libjpeg-turbo.org/), [Xiph.org ogg/vorbis](https://xiph.org/), the PS2BUILD SDK audio package (`audsrv-compat`) and font contributors.

**PC CARD CompactFlash driver reference:** [mihhru](https://github.com/mihhru) and [PS2CF](https://github.com/mihhru/PS2CF) supplied the early-PS2 CF PIO/ATA foundation and host tests. The driver is integrated into this build; console compatibility is not yet verified on hardware.

**External launcher and reference projects:** [Neutrino](https://github.com/rickgaiser/neutrino), [POPStarter](https://www.psx-place.com/threads/popstarter.19139/), [Ember](https://github.com/Gageformer/Ember), [PS2Link](https://github.com/ps2dev/ps2link), [UDPFSD](https://github.com/pcm720/udpfsd), [OSDMenu](https://github.com/pcm720/OSDMenu), [KELFTool](https://github.com/xfwcfw/kelftool), [CosmicScale's HDD-OSD icon generator](https://github.com/CosmicScale/HDD-OSD-Icon-Generator), [PixelFX RetroGem](https://www.pixelfx.co/hdmi-retro-gem), the Google Drive APIs and the MMCE/PS2 homebrew ecosystem. External programs are **not** automatically bundled merely because Launchstation can launch or reference them.

For each project's exact role, paths, credit, bundled/unbundled status and license caveats, read [Third-Party Credits & License Inventory](docs/THIRD_PARTY_CREDITS.md) and the [website Credits page](https://nathanneurotic.github.io/Launchstation/credits.html). Original file-level notices remain authoritative; there is currently no single root LICENSE in the repository.
