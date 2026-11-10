# Launchstation interface captures

The [visual gallery](https://nathanneurotic.github.io/Launchstation/ui-captures.html)
shows all 18 built-in theme presets using the same populated sample library. These are
real captures of released build 974 in RIPPCSX2, not drawn UI mockups.

![OSD Classic dashboard with sample applications](../docs-site/static/assets/ui/theme-osd.png)

## Read by task

Screenshots are integrated beside the relevant instructions in the dashboard,
Applications, File Browser, Text Editor, themes, Settings, cloud, MMCE backup and
Neutrino guides. The visual gallery is a supplementary index of all captures.
It also includes real application-view navigation in MP4 and GIF form.

## Identity and scope

Build 974 is revision `97318e5870b59ce32cae19312ed60415752ae9b3`. The capture
manifest records the exact released ELF and emulator SHA-256 hashes, input steps,
media sizes and hashes. Seven non-bootable sample application files supply titles,
descriptions, favorites and pins. RiptOPL and Launchstation use their own project
logos; other entries demonstrate the built-in fallback icons.

The native frames are 640 x 480, Auto/NTSC, 4:3. The theme tour uses three seconds
from each real recording with straight cuts. Videos are delivered at 640 x 480,
20 fps; the highlights GIF is 480 x 360, 6 fps. No gameplay, BIOS, card image,
application binary, personal data or credentials are included. Screenshots show
emulated UI behavior, not acceptance of real storage/network/console operations.

## Refresh workflow

1. Keep an isolated portable RIPPCSX2 lab, including its own card, settings and
   output folders. Use the exact released ELF and verify its package manifest.
2. Stage non-bootable sample applications with clear titles, matching project
   artwork and metadata. Avoid changing an everyday emulator installation.
3. Configure a preset via `Theme_preset` and `Theme_custom=0` in the lab's
   `DASHBOARD.CNF`. Capture each of the 18 IDs listed in `src/dash_themes.c`.
4. Use the [RIPPS2 background driver](https://github.com/akilluminati47/RIPPS2/blob/f4679198071ebc6d57461111f5a70ba3e268c2e0/tools/dev/ps2bg.ps1)
   with its lab path changed to your isolated directory. F8 captures the native
   PNG; F11 toggles recording. Use the exact steps recorded in the manifest.
5. Inspect every full frame and clip. Keep PNGs unchanged; correct video delivery
   to the same 4:3 display ratio. Record cuts and conversion settings honestly.
6. Update every media size/hash after every refresh. Update ELF/build identity
   when the loader changes, and emulator identity/hash independently when it changes.
7. Build the site and generate the wiki; check media URLs, mobile layout and
   optional-video playback before publishing.

This lab boots an unchanged ELF copied to `SYS-CONF.ELF`: the emulator's per-game
folder-card filter then includes the `SYS-CONF` fixture and its nested APPS folder.
This is a local capture setup, not an installation requirement or loader patch.
