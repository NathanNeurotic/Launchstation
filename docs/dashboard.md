# wLaunchELF dashboard

The dashboard is the only shell of this build; the classic launch-key
menu and the classic screens are gone. Underneath it still uses the proven
wLaunchELF code: every launch goes through `ExecuteMainAction()`, and file
operations, the HDD tools and the exploit installer run the same logic as
before. Settings, the File Browser, the Text Editor, the HDD Manager (PSX
included), the memory card exploit installer, the information screens and
the file/folder picker are all dashboard screens, and the questions those
tools ask (yes/no, names, progress) are dashboard dialogs. Commands with no
screen of their own (start disc, OSDSYS, PS2 browser, network, power off,
reboot IOP) run behind the loading overlay.

File Browser: Confirm opens a folder, starts an ELF/KELF/VCD or opens a
text file in the Text Editor; Square selects; Triangle has copy, cut,
paste (also with new names), PSU create/extract, delete, rename, new
folder, folder icon (icon.sys), size, start with arguments, title.cfg,
mount as vmc0:/vmc1: (for .bin/.vmc/.mc2/.ps2 images; mounted images
appear in the device list), the memory card *Tuna date and *Folder
dates* (memory card roots and `hdd0:/__common`: edit dates by hand or
order the saves A-Z, Z-A or the SAS way). The offered operations follow
the classic rules (nothing that writes on a disc or read-only host, no
file operations in the partition list).

Also in the File Browser menu, as in the classic browser:

* *Show*: file names, names and details (size and date), or game titles
  and details (the title of each save or PSU file).
* *Sort by*: name, game title, date, or as stored. Folders come first,
  then ELF files.
* *Bookmarks...*: the PathPad. Jump to a bookmarked folder, bookmark the
  current one or remove one (saved in LAUNCHELF.CNF; *Lock launch keys*
  in Settings locks them).
* *Go to wLaunchELF's folder*, *Invert selection*.
* *Use as launch arguments* on a text file: its lines become the
  arguments of the application you start next from the browser.
* The header shows the free space of memory cards and HDD partitions.

In file pickers (Settings, launch keys) R3 chooses the highlighted file as
a `uLE:/` path, relative to wLaunchELF's own folder, like the classic R3;
elsewhere R3 opens that folder.

The cursor never waits for the memory card or a slow device: file reads
and writes (artwork, saves, library scans, folder listings) run on a
background thread, and a small indicator above the button hints shows
what is being worked on. Starting a device's drivers happens in the
foreground behind a short loading overlay.

## Sections

| Section  | What it does |
|----------|--------------|
| HOME     | Hero panel for the focused item, a *Continue* row (pinned, recent, favorite apps), quick-access chips (library, File Browser, start disc, devices, tools, settings and every launch key from LAUNCHELF.CNF) and live device status. |
| APPS     | ELF library in **Grid**, **List** or **Carousel** view (SELECT switches, saved). Filters: all, favorites, memory cards, USB, HDD, other. |
| FILES    | Entry points into the full FileBrowser: device list, last folder, MISC commands, each detected device and your PathPad bookmarks. |
| DEVICES  | Cards for memory cards (type, free space), USB, MMCE, MX4SIO, HDD, exFAT HDD, disc, UDPFS, PSX flash/DVR HDD and mounted VMCs. |
| TOOLS    | HDD Manager, Text Editor, start disc, POPStarter, exploit installer, OSDSYS, PS2 browser, system/build info, font viewer, about, reboot IOP, power off. |
| SETTINGS | Display, Theme, Dashboard, Launch keys, Controls, Startup, Devices, Network, Advanced: every dashboard and wLaunchELF option, edited in place (no classic configuration screens). Changes apply at once; when you leave SETTINGS with unsaved changes you are asked to *Save*, *Discard* or *Keep editing*. LAUNCHELF.CNF is saved where it was loaded from (*Save wLaunchELF settings to...* chooses another place), IPCONFIG.DAT likewise. |

## Controls

Confirm/cancel follow the existing `GUI_Swap_Keys` setting (Settings >
Controls > Confirm button), as wLaunchELF always has.

| Button | Action |
|--------|--------|
| D-pad / left stick | Navigate |
| Confirm (Circle or Cross) | Start / open / select |
| Cancel | Back / close |
| Triangle | Options for the focused item |
| Square | Add to / remove from Favorites |
| L1 / R1 | Previous / next section |
| L2 / R2 | Page jump |
| START | Dashboard menu (home, refresh library, File Browser, settings, reboot IOP, power off) |
| SELECT | Tap: switch application view (APPS). Hold: your launch keys appear; press a button to start its application |

wLaunchELF's boot behaviour is kept: `Init_Delay` is honoured,
a launch key held when the delay ends starts its application, and a
configured `LK_auto` path starts after the classic timeout unless a button
is pressed (a countdown banner is shown).

## Themes

Settings > Theme chooses among 19 built-in themes (OSD Classic, PS2
Towers, Disc Error Red, System Clock and more) and opens the theme editor,
where the background, every colour, selection style, corners, font, text
size, icons, clock and app-artwork wallpaper can be changed with the
controller. Themes can be saved to and loaded from
`THEMES/<name>/theme.cfg` folders. See **[themes.md](themes.md)**.

## Application discovery

Only the configured folders are listed (Settings > Dashboard > Application
folders). Defaults: `mc:/APPS/`, `mc:/BOOT/`, `mass:/APPS/`, `mass:/BOOT/`.
Each folder is searched one sub-folder deep (`APPS/<folder>/*.ELF`).

* At startup only memory cards and devices whose driver is already running
  are refreshed (Settings > Dashboard > Refresh at startup). Nothing else is
  initialised for the dashboard.
* *Refresh library* (APPS > Triangle, START menu or Settings) scans every
  folder, starting the drivers it needs first; the search itself runs in
  the background while you keep using the dashboard.
* Results are cached in `DASHLIB.CNF` and shown instantly next boot.
  Apps that disappeared (device removed) stay usable as shortcuts and fail
  with a friendly message instead of a crash.

## Metadata (optional)

Plain `key = value` text; every key is optional; `#` starts a comment.

* `<dir>/<ELFNAME>.cfg` (e.g. `OPNPS2LD.cfg` next to `OPNPS2LD.ELF`)
* `<dir>/title.cfg` in OPL application format (`title=`, `boot=`); applied
  only when `boot=` matches the ELF

```
title = Open PS2 Loader
description = Game loader for USB, HDD, SMB and MX4SIO.
author = ifcaro & contributors
version = 1.2.0
category = Loader
cover = cover.png          # relative to the ELF folder, or a full path
arg = -some-option         # one argument per line, repeatable
pinned = 1                 # show on HOME
```

Arguments: a classic `<ELFNAME>.arg` sidecar always wins; metadata `arg`
lines are used only when no `.arg` exists. *Start with arguments...* uses
the Text Editor's "Launch with args" flow. APPS > Triangle > *Edit metadata*
creates a starter file and opens it in the Text Editor.

## Artwork (optional)

First match wins; nothing is required.

1. `cover =` from the metadata
2. `<dir>/<ELFNAME>.ELF_COV.png` (OPL naming, e.g. `OPNPS2LD.ELF_COV.png`)
3. `<dir>/<ELFNAME>_COV.png`, `<dir>/<ELFNAME>.png`
4. `<dir>/cover.png`, `<dir>/icon.png` (only for per-app folders)
5. `<dir>/<ELFNAME>_ICO.png`
6. `<device>/ART/<ELFNAME>.ELF_COV.png`, `<device>/ART/<ELFNAME>_COV.png`
   (OPL ART folder; `hdd0:/+OPL/ART/` on the internal HDD)
7. `<dashboard folder>/ART/` with the same names
8. the theme's default application icon (a PS2-style program tile with a
   device badge; a theme may use its own PNG, or the device icon or the
   old initials tile instead, see [themes.md](themes.md#icons))

Any PNG size up to 2048x2048 / 1.5 MB is accepted and box-filtered down to
at most 128x128 while decoding. Artwork on a device whose driver is not
running is looked up later, when that device is opened.

## Files written by the dashboard

Next to LAUNCHELF.CNF when it was loaded from a writable device, otherwise
in `mc0:/SYS-CONF/`: `DASHBOARD.CNF` (settings, scan folders),
`DASHAPPS.CNF` (favorites, pins, recents), `DASHLIB.CNF` (library cache).
The active theme is part of DASHBOARD.CNF (`Theme_*` keys); saved themes
go to `THEMES/<name>/theme.cfg`, only when you choose
*Save theme to folder* — in the ELF's own folder when writable, else the
dashboard folder (see [themes.md](themes.md#theme-folders-user-created-themes)).
LAUNCHELF.CNF itself is never modified behind your
back.

## Interface sounds

The dashboard plays short sound effects through the ps2sdk `audsrv`
audio server: cursor moves, confirm/cancel in dialogs, section switches,
toasts, the startup boot sound, and device connect/disconnect. They are
Sony ADP samples embedded in the ELF (no files needed at runtime).

Settings > Dashboard has *Interface sounds* (the UI effects) and
*Background music* (the music track), each with its own volume
(`Dash_Sound` / `Dash_SoundVolume` / `Dash_BGM` / `Dash_BGMVolume` in
DASHBOARD.CNF, all on at 80% by default). *Interface sounds* off silences
the whole audio driver, music included. When the audio driver cannot
start, everything stays silent — the dashboard works normally without
sound (a toast says why).

## Devices and the applications filter

Settings > Devices has an *Auto-start* switch per device family (USB
storage, internal HDD, exFAT HDD, MMCE SD, MX4SIO, UDPFS network): ON
loads that device's drivers when the dashboard starts, so the device is
ready immediately and its applications are found by the startup scan
(`Dash_DevAuto` bitmask, all off by default). OFF never loads the driver
by itself — the DEVICES screen can still start it manually, and drivers
already loaded stay loaded until the next IOP reboot. The device the ELF
booted from is always on (its driver is already running) and cannot be
switched off.

On APPS, the *Show* choice (Triangle menu) filters the list — All,
**Favorites**, Memory Cards, USB, HDD, Other devices — and the choice is
remembered across restarts (`Dash_AppFilter`).

## Optional PSBBN layers (Crystal background)

With the *Crystal* background, `<dashboard folder>/THEME/background.png`,
`stars1.png`, `stars2.png` are used when present (backdrop plus two
additive star layers). Build them from a PSBBN texture pack you own with:

```
python scripts/prepare_psbbn_theme.py PSBBN-Texture-PNGs.zip
```

No third-party artwork is embedded in the ELF.

## Typography

Dashboard text uses Roboto Regular (Copyright 2015 Google Inc., Apache
License 2.0, see `docs/licenses/Roboto-COPYRIGHT.txt`). It is rendered on the
host by `scripts/gen_dash_font.py` (4x supersampled) into three PSMT8 alpha
atlases (14, 20 and 32 px em; 35 + 57 + 123 KB) committed as
`src/dash_font_data.c`; the PS2 needs no font engine. Sizes run from 12 px
captions to 28 px titles. Themes can switch the dashboard to that bitmap font as well
(*System* font, see [themes.md](themes.md#fonts)), scale text from 90 to
115 % and draw it bold. Regenerate the Roboto atlases with another TTF/OTF:

```
python scripts/gen_dash_font.py path/to/Font.ttf
```

## Memory and VRAM budget

* Framebuffer: unchanged (single buffered, CT32, as the classic UI).
* Dashboard font atlases ~215 KB, bitmap-font atlas 512x340 PSMT8 (~170 KB,
  only with the *System* font), icon atlas 512x408 PSMT8 (~204 KB),
  five tiny CT32 helper textures. All textures are owned by gsKit's texture
  manager (inline uploads, LRU eviction) above the framebuffer.
* Artwork: at most 40 thumbnails (<= 64 KB each) in EE RAM; one probe or
  one decode per frame, only after the pad has been idle ~110 ms (focused
  item) or ~260 ms (neighbours). Slots drawn in the current or previous
  frame are never evicted.
* Library: 512 entries x ~850 bytes. Scans temporarily allocate one
  FileBrowser listing buffer (~800 KB) and free it afterwards.
* Themes: the theme itself is ~700 bytes. Theme images (wallpaper <= 320 KB,
  default app icon <= 64 KB, PSBBN layers) share 8 slots and are loaded
  once on first use. The Brushed background builds one 32 KB texture on
  first use; every other background is pure geometry. Listing theme
  folders temporarily allocates one FileBrowser listing buffer.

## Developer aid

Defining `DASH_AUTOTEST` (add it to the `wLaunchELF` target's `defines` in
ps2.yaml) replays the timed button script in `include/dash_autotest.h` and
seeds a sample library, so every screen can be exercised in an emulator.
`DASH_THEME_TOUR` shows every theme preset for five seconds. Never ship
such builds.
