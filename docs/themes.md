# Dashboard themes

The dashboard has a theme engine: one theme holds every visual value the
dashboard uses (background, colours, selection style, corners, font,
icons, clock and the app-artwork wallpaper). You can pick a built-in
preset, change any value with the controller, save the result as a theme
folder, and load theme folders that you or others made on a PC. Nothing
needs recompiling.

The classic screens (File Browser, HDD Manager, Text Editor, classic
configuration) follow the theme too when *Modern style for classic
screens* is on (Settings > Display).

## Built-in themes

| Theme | Background | Look |
|-------|------------|------|
| **OSD Classic** (default) | System tunnel | Indigo tunnel like the PS2 system menu backdrop, OSDSYS highlight blue, accent-bar selection |
| Classic Black | Solid colour | Plain black; the lightest background. Change its colour to get any solid colour |
| PS2 Towers | PS2 towers | The opening's tower field seen from above, blue fog pool, three small drifting lights |
| Disc Error Red | Disc error red | Dark red clouds streaming past, a pulsing flare, a slowly turning glass cube |
| PS2 Blue Mist | Blue mist | Dark navy with slow translucent blue forms |
| Midnight Gradient | Gradient | Blue to black |
| OSD Minimal | Orbit lights | Near black, the main menu's ring of eight lights; no panels, selection by text colour only |
| System Clock | Clock ring | System Configuration's twelve-rod clock: the ring turns with the seconds, the current hour's rod is lit and fills with the minutes |
| Memory Card Grid | Memory card grid | Sparse block grid in thin lines |
| Boot Lines | Boot lines | Soft light traces |
| Wireframe Cubes | Wireframe cubes | A few blue wireframe cubes drifting in depth |
| Data Stream | Data stream | Abstract device activity in thin columns |
| Memory Card Blue | Memory card tiles | Muted blue-grey with a tiled memory card motif |
| Brushed Console | Brushed console | Charcoal with a fine matte finish |
| BIOS Blue | Gradient | Clean blue system screen, white text, cyan focus |
| Monochrome | Solid colour | Black, white and grey; only the selection has colour |
| CRT | Gradient + scanlines | Understated scanlines over the background only |
| PSX | Ribbons | Charcoal with silver ribbons |
| Crystal | Crystal | The previous dashboard look (crystal towers, stars, glow, initials tiles) |
| Light Show | Light streaks | Light pillars sweeping across an indigo night |

Every background is drawn procedurally from a few GS primitives. None of
them uses or needs Sony BIOS data. The PS2-inspired scenes were designed
from the behaviour documented by the osdbits OSDSYS reverse-engineering
notes: the menu backdrop tunnel, the tower heightfield and its look-down
camera, the illegal-disc clouds, the System Configuration clock ring, and
the colour-swap selection.

## Settings

**Settings > Theme**

* **Theme**: Left/Right switches presets immediately. Press the confirm
  button to open the list: each preset is previewed as you move the
  cursor; confirm keeps it, cancel restores your previous theme (even an
  edited one).
* **Customize theme...** opens the theme editor.
* **Background motion**: Full / Subtle / Off. Off shows a still frame of
  the same background, so every background has a static variant.
* **Load theme from folder...**, **Save theme to folder...**,
  **Restore default theme**.

**Theme editor.** The left column shows the groups; BACK returns to Settings.
Every change applies immediately. Each group ends with *Reset ...*, which
puts that group back to the values of the preset the theme started from.

| Group | Options |
|-------|---------|
| Background | Background type, Colour, Second colour, Brightness (10-100 %), Motion, Animations, CRT scanlines, Wallpaper image... |
| Colours | Text, Secondary text, Muted text, Selected text, Accent, Selection (with opacity), Panels (with opacity), Borders (with opacity), Warnings |
| Style | Selection style (solid bar, accent bar, outline, text colour), Corners (square, soft, round), Glow, Panel opacity |
| Text | Font (Roboto, System), Text size (90-115 %), Bold, Text shadow |
| Icons | Icon colour (text colours, accent, monochrome), Default app icon (application, device, initials), Custom app icon image... |
| Clock | Clock (off, standard, system), Position (header, footer), Hours (system, 24-hour, 12-hour), Seconds, Date (system, YYYY/MM/DD, MM/DD/YYYY, DD/MM/YYYY, off) |
| Wallpaper | App artwork background (off, focused app), Artwork dimming, Load artwork |
| Manage | Theme name..., Save theme to folder..., Load theme from folder..., Reset entire theme, Restore default theme |

### Colour editor

Any colour row opens the colour editor, drawn next to the live dashboard:

| Button | Action |
|--------|--------|
| Up / Down | Red, Green, Blue, (Opacity), quick picks |
| Left / Right | -1 / +1 (on the quick-pick row: previous / next colour) |
| L1 / R1 (or L2 / R2) | -16 / +16 |
| Confirm | Keep the colour |
| Cancel | Restore the previous colour |

The quick picks are PS2 system-menu colours (OSDSYS highlight blue, its
cyan, the menu indigo, clock-rod blue, disc-error red, the configuration
title yellow...) and neutral greys. Opacity is offered for Selection,
Panels and Borders; a checker board behind the preview shows it.

When you confirm, the theme is checked: a text colour too close to its
background is replaced so the menus stay readable, and a message tells you
when this happened.

## Background, wallpaper and artwork

* **Solid colour / gradient**: *Colour* is the solid colour or the
  gradient top, *Second colour* the gradient bottom.
* **Scenes**: *Colour* tints the scene (tower walls, clouds, lights, grid
  lines...), *Second colour* is its base.
* **Wallpaper image**: pick a PNG with the File Browser. It is
  cover-fitted (aspect ratio kept, excess cropped) to the 4:3 screen. When
  the file is missing or cannot be read, the gradient is shown instead.
* **App artwork background**: while an application with artwork is
  focused on HOME or APPS, its artwork is laid over the background,
  cross-faded on focus changes and dimmed by *Artwork dimming*. Artwork
  comes from the existing artwork cache, so nothing extra is loaded and
  applications without artwork just show the theme's background.

## Fonts

* **Roboto** (default): the bundled smooth proportional font, in three
  pre-rasterised sizes (see [dashboard.md](dashboard.md#typography)).
* **System**: wLaunchELF's own 8x16 font, smoothed. It follows a custom
  `.fnt` chosen in the classic startup settings and draws the Latin-1
  characters used by the language files, and at the dashboard's body size
  it maps almost 1:1 to screen pixels, which is crisp on a CRT.

Arbitrary TTF files are not loaded on the console: a font engine and its
glyph cache would cost too much memory and time. To use another typeface
for *Roboto*, regenerate the atlas on a PC with `scripts/gen_dash_font.py`.

## Icons

Applications that have artwork keep using it. The artwork lookup order is
unchanged (see [dashboard.md](dashboard.md#artwork-optional)). Applications
without artwork get:

* **Application** (default): a PS2-style program icon (a window with a
  disc emblem) on a tile in the theme's colours, with the device as a
  small badge.
* **Device**: the icon of the device the application is on.
* **Initials**: the previous coloured tile with the application's initials.

A theme can provide its own default application icon PNG (*Custom app icon
image...* or `app_icon =` in theme.cfg). Up to 128x128 is kept; alpha is
supported. If the file is missing or broken, the built-in icon is used.

## Clock

*System* follows the PS2 system menus: light grey, even weight, seconds,
date in the console's format. *Standard* is a larger time over a small
date. In the footer, the clock sits at the left end of the button hints
and hides itself on screens that need that space for hints. With *Hours*
and *Date* on *System*, the console's own clock settings (12/24-hour, date
order) are used.

## Theme folders (user-created themes)

```
<dashboard folder>/THEMES/
  MyTheme/
    theme.cfg          required
    background.png     optional (name it anything; see image =)
    app.png            optional default application icon (app_icon =)
```

`<dashboard folder>` is where DASHBOARD.CNF lives: next to LAUNCHELF.CNF
when it was loaded from a writable device, otherwise `mc0:/SYS-CONF/`.
The easiest start is *Save theme to folder...*, which writes a complete
`theme.cfg` for the current theme; copy that folder to a PC, edit it, copy
it back.

Theme folders are looked up in two places, in this order:

1. `THEMES/` in the ELF's own folder (the folder wLaunchELF was launched
   from), so themes on a USB stick, memory card or HDD partition work
   wherever the ELF goes, with no device handling.
2. `THEMES/` in the dashboard folder described above.

Both are only read from devices whose driver is already running. *Save
theme to folder...* writes to the first of these that is writable.

Theme folders are only read when you choose *Load theme from folder...*,
never at startup, and their device must already be available (loading a
theme never starts a device driver). The loaded theme is copied into
DASHBOARD.CNF, so it keeps working if the folder is removed later, except
for its images, which are read from the folder.

### theme.cfg

Plain `key = value` lines, like the dashboard's other files. `#` starts a
comment. Every key is optional: missing keys keep the value of the
default theme (or of `preset =`), unknown keys are ignored, and invalid
values are ignored or clamped.

```
# My theme
name = Night Blue
author = me
preset = midnight            # start from a built-in preset (put it first)
background = towers
background_color = #9694BE
background_color2 = #284CAA
brightness = 80
text = #ECEEF4
text_selected = #76D6FF
accent = #54C4FA
selection = #1E6E9C96        # #RRGGBBAA: the last pair is the opacity
panel = #06061096
highlight = bar
corners = soft
font = roboto
clock = system
clock_position = header
```

| Key | Values |
|-----|--------|
| `name`, `author` | text (up to 31 characters) |
| `preset` | a built-in preset id (see below); applied where it appears, so put it first |
| `background` | `solid`, `gradient`, `tunnel`, `mist`, `towers`, `disc`, `orbit`, `clockring`, `grid`, `lines`, `cubes`, `stream`, `tiles`, `brushed`, `wave`, `crystal`, `streaks`, `clouds`, `image` |
| `image` | wallpaper PNG for `background = image` |
| `background_color`, `background_color2` | colour |
| `brightness` | 10-100 (%) |
| `scanlines` | 0 / 1 |
| `text`, `text_secondary`, `text_muted`, `text_selected` | colour |
| `accent`, `accent2`, `warning`, `error`, `success` | colour |
| `selection`, `panel`, `border` | colour with opacity (`#RRGGBBAA`) |
| `highlight` | `fill`, `bar`, `outline`, `text` |
| `corners` | `square`, `soft`, `round` |
| `glow`, `bold`, `text_shadow` | 0 / 1 |
| `font` | `roboto`, `system` |
| `font_scale` | 90-115 (%) |
| `icon_tint` | `text`, `accent`, `mono` |
| `app_icon_style` | `system`, `device`, `monogram` |
| `app_icon` | default application icon PNG |
| `clock` | `off`, `standard`, `system` |
| `clock_position` | `header`, `footer` |
| `clock_hours` | `system`, `24`, `12` |
| `clock_seconds` | 0 / 1 |
| `clock_date` | `system`, `ymd`, `mdy`, `dmy`, `off` |
| `wallpaper` | `off`, `focused` (app artwork background) |
| `wallpaper_dim` | 0-90 (%) |

Colours: `#RRGGBB`, `#RRGGBBAA`, `RRGGBB` or `r, g, b[, a]` in decimal.
Yes/no values also accept `on/off`, `yes/no`, `true/false`. Enumerations
also accept their number.

Image paths without a device (`background.png`, `img/app.png`) are relative
to the theme folder. Full paths (`mass:/THEMES/pic.png`) work too.

Preset ids: `osd`, `black`, `towers`, `discred`, `mist`, `midnight`,
`minimal`, `clockring`, `mcgrid`, `bootlines`, `cubes`, `stream`, `mcblue`,
`brushed`, `biosblue`, `mono`, `crt`, `psx`, `crystal`, `lightshow`.

A complete example is in [theme-example/theme.cfg](theme-example/theme.cfg).

## Images

| | Wallpaper | Default app icon |
|--|--|--|
| Format | PNG (grey, palette, RGB; 8 or 16 bits; with or without alpha or tRNS) | same |
| File size | up to 1.5 MB | up to 1.5 MB |
| Source size | up to 2048x2048 | up to 2048x2048 |
| Kept in memory | box-filtered to fit 320x256 (at most 320 KB) | fit 128x128 (at most 64 KB) |
| Recommended | 320x224 or 320x256, 4:3 | 128x128 with alpha |
| Alpha | used | used |

Images are decoded once and kept (8 theme image slots). Interlaced PNGs are
accepted only up to 256x256. Missing, too large or broken files are
remembered and skipped, and the built-in fallback is drawn instead.
Because the screen is at most 640x512 and wallpapers are shown behind
dimmed menus, a larger wallpaper would not look sharper, only cost more
memory.

## Where settings are stored

The active theme is stored in **DASHBOARD.CNF** as `Theme_*` keys:

* `Theme_preset = <id>` and `Theme_custom = 0`: an unmodified preset. It
  follows improvements to that preset in later versions.
* `Theme_custom = 1` plus one `Theme_<key>` per value (the theme.cfg keys
  above): an edited theme, stored completely.

Edits are written about a second after the last button press (and always
before an application starts or a classic screen opens), so adjusting a
colour does not write the memory card on every step. Older dashboard
versions ignore these keys; a DASHBOARD.CNF without them loads OSD Classic.
LAUNCHELF.CNF is never touched.

The optional PSBBN layers in `<dashboard folder>/THEME/` (`background.png`,
`stars1.png`, `stars2.png`) are still used by the *Crystal* background.

## Restoring defaults

* *Settings > Theme > Restore default theme*: back to OSD Classic.
* *Theme editor > Manage > Reset entire theme*: back to the preset the
  current theme started from.
* *Reset ...* at the end of each editor group: only that group.
* Deleting the `Theme_*` lines from DASHBOARD.CNF (or the file) also
  restores the default.

## Performance

Every background is budgeted for full frame rate on a real PS2 while the
dashboard stays responsive; they are all a few hundred flat or gouraud
primitives at most (PS2 Towers is the heaviest, about 500 quads).

* *Motion: Off* stops the background clock: same picture, no animation.
* *Motion: Subtle* runs slower with fewer elements.
* *Solid colour* costs nothing and is the safe fallback.
* The app artwork background reuses the artwork cache; it never loads a
  file itself.
* Theme images are loaded once, on first use, from devices whose driver is
  already running.

Real hardware is the reference for performance and compatibility. Please
report any background that hitches on your console together with your
video mode (NTSC/PAL/480p).

## Developer aid

Defining `DASH_THEME_TOUR` (add it to the `wLaunchELF` target's `defines`
in ps2.yaml) shows every preset for five seconds, which makes checking
them in an emulator easy. Never ship such a build.
