# Example 4:3 Launchstation wallpaper theme

This folder contains a real, loadable wallpaper example:

- **background.png** — 320 × 240 pixels (4:3), indexed non-interlaced PNG,
  with simple center-composition and edge/safe-area guide marks.
- **theme.cfg** — ready-to-load theme that sets `background = image` and
  `image = background.png`, with a readable blue colour palette.

## Load on PS2

1. Copy **both files** into the same folder under `THEMES/` in the
   Launchstation ELF folder (for example
   `mass:/APPS/Launchstation/THEMES/WallpaperExample/`), or under the
   dashboard configuration folder's `THEMES/`.
2. Launchstation: **Settings > Appearance > Theme files & resets >
   Load theme from folder...**. Choose `WallpaperExample`.
3. Preview, then save the Settings changes. No other files are required.

To use only your own wallpaper without a theme folder, choose
**Settings > Appearance > Customize theme > Background > Wallpaper image...**.

## Create your own

- **4:3** is the actual wallpaper display ratio.
- Use **640 × 480 PNG** for detailed artwork, or **320 × 240 PNG**
  for the lower-memory option demonstrated by this example.
- Keep essential artwork and lettering away from the outside edges: CRT
  overscan and the theme's foreground panels/hints vary.
- PNG files up to **1.5 MB**, source size up to **2048 × 2048**, are
  supported. The PS2 decodes theme wallpaper to fit **640 × 512**
  (a 4:3 source can retain **640 × 480**).
- Wider pictures like 16:9 are center-cropped left and right to 4:3.
  320 × 256 and 320 × 224 are supported inputs but aren't 4:3.
- Use a dim background for readable menus and appropriate text contrast.
- `background = image` with `image = background.png` activates the static
  theme wallpaper. The separate `wallpaper = focused` key displays app
  cover art over it; the bundled example leaves that feature off.

## If it does not load

- The folder must be named under a `THEMES/` folder that is either beside the
  Launchstation ELF or next to `DASHBOARD.CNF`, and its device must already
  be running (loading a theme never starts a device driver; open the device
  once from the Files tab first).
- `theme.cfg` and `background.png` must sit directly in the same folder;
  `image = background.png` is resolved relative to that folder. A missing or
  unreadable image falls back to the gradient colours in `theme.cfg`.
- `theme.cfg` is read only when you choose **Load theme from folder...**
  (never at startup) and must be under 16 KB. Unknown keys are ignored.
- The loaded theme is copied into `DASHBOARD.CNF`, so it keeps working if the
  folder is later removed, except that its image is read from the folder.
  Remember to save the Settings changes (Triangle > Save changes).
- This example uses `preset = towers` as a starting point. Start your own theme
  with **Export theme to folder...** to get a complete `theme.cfg` for the
  theme you are currently using.

See [Dashboard themes](../themes.md) and the
[website's theme guide](https://nathanneurotic.github.io/Launchstation/themes.html).
