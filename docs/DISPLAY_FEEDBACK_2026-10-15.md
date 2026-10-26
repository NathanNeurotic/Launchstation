# Display feedback implementation, 2026-10-15

> **Status as of 2026-10-26: implemented (PR #161); console acceptance pending.**
> In tree: 576p is the last video-mode entry (`src/dash_settings.c` `tv_names`, `src/config_screen.c`),
> wallpaper decode box is 640x512 (`src/dash_bg.c`), interlace-aware text alignment and neutral
> controller-glyph backings are covered by `tests/test_display_feedback.py`. 720p/1080i menu modes were
> **not** added, as planned. Known stale text outside this file: the Settings wallpaper help string in
> `src/dash_settings.c` still says images are scaled to at most 320x256 although the decoder allows 640x512.


The tester requested the credits scrolling clarity fix across fonts, consistent
and more visible controller hints, and less pixelated custom wallpaper. The
tester separately reported that the console-region issue was fixed.

## Implemented changes

- Text runs align their vertical translation to physical line pairs in
  interlaced FIELD modes, after PAL scaling and modal offsets. All font faces
  use this path. Glyph sizes/advances remain intact, and progressive output
  retains subpixel animation. This addresses the same phase instability as the
  credits sheet fix; console testing must establish the visual improvement.
- Controller glyph backings use a shared neutral dark fill and visible rim,
  including D-pad hints and text-key fallbacks. Face-button colors and the
  user's icon-backing toggle are preserved. Footer labels use the main text
  color for contrast.
- Custom wallpaper decoding increases from 320x256 to 640x512. A 4:3 source
  can retain 640x480 pixels instead of 320x240. The decoder never enlarges small
  sources. Cache identity includes decode bounds so a file previously decoded
  as a 128x128 icon cannot supply the wallpaper thumbnail by mistake.
- 576p is appended to the saved video-mode enumeration, preserving IDs 0-4.
  Both initial setup and live mode changes use progressive 640x512 geometry,
  with labels in dashboard and classic settings. Auto/NTSC/PAL/VGA/480p retain
  their existing geometry and behavior.

## Resource limits and higher video modes

A 640x480 CT32 wallpaper needs 1,228,800 decoded bytes (about 1.17 MiB), four
times a 320x240 image. The full decode box is bounded to 1.25 MiB. The existing
texture manager retains responsibility for VRAM uploads/eviction. Larger
wallpaper inputs do not change the output mode, and a small original will
still look soft when enlarged on screen.

[SMS](https://github.com/NathanNeurotic/Simple-Media-System) advertises 480p,
576p, 720p and 1080i. The inspected RiptOPL renderer in
`NathanNeurotic/Open-PS2-Loader`, `src/renderman.c`, uses `gsKit_hires_*`,
multiple passes, and CT16S for HD modes. Its high-resolution clear ordering
also matters: an alpha-blended zero-alpha pass clear can preserve stale pixels.
Launchstation currently uses an ordinary double-buffered ONESHOT renderer.
Its 720p CT32 double framebuffers would alone require 7,372,800 bytes, exceeding
the GS's 4 MiB VRAM before textures. Thus 720p/1080i remain separate renderer
work, including X/Y layout scaling, aspect handling, pass clearing, texture
budgeting and frame presentation. No HD menu entries are exposed prematurely.

576p uses an existing [gsKit mode](https://github.com/ps2dev/gsKit/blob/master/ee/gs/include/gsInit.h)
and the same framebuffer dimensions as the current PAL path. No SMS or RiptOPL
renderer source was copied by this change.

## Validation boundaries

`tests/test_display_feedback.py` compiles the real alignment, geometry and
texture-cache functions against host mocks. It covers NTSC/PAL translations,
fractional modal offsets, progressive behavior, 576p geometry, existing mode
geometry, and distinct icon/wallpaper cache resolutions. Existing wallpaper,
controller-hint, modal-animation and frame-presentation tests are also run.

Visual quality, CRT flicker, 576p display compatibility, and texture upload cost
with complex themes require console testing. No physical-console result is
claimed by host tests, compilation, or CI.

Local validation completed: the eight focused host test scripts passed. Standard
and DS34 full EE builds both linked and packed successfully with ps2build
v2026.10.13 / GCC. The first build attempt exposed missing local cloud TLS
headers; validation then reused the existing project Mbed TLS 3.6.7 dependency
cache from the media-cloud checkout. CI builds that dependency from the pinned
source archive independently. No console/display acceptance has been run.

The complete CI host-test list was also exercised locally. Its older
loading-overlay fixture was updated for the cache decode-size fields. The
release-workflow syntax check requires native Git Bash on Windows; the WSL
launcher cannot run in the sandbox. Both documentation sites build locally.
