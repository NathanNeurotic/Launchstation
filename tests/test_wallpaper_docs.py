"""Check that the published wallpaper example is truly loadable 4:3 PNG.

The guide must match the production cover crop, texture size and theme keys.
PNG metadata is parsed from IHDR without installing imaging dependencies.
"""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
example = (root / "docs/theme-example/background.png").read_bytes()
website = (root / "docs-site/static/assets/wallpaper-example.png").read_bytes()
cfg = (root / "docs/theme-example/theme.cfg").read_text(encoding="utf-8")
theme = (root / "docs/themes.md").read_text(encoding="utf-8")
site = (root / "docs-site/content/themes.html").read_text(encoding="utf-8")
wiki = (root / "docs/wiki/Theme-and-Wallpaper-Guide.md").read_text(encoding="utf-8")
home = (root / "docs/wiki/Home.md").read_text(encoding="utf-8")
bg = (root / "src/dash_bg.c").read_text(encoding="utf-8")

assert example == website, "published preview must equal example theme PNG"
assert example.startswith(b"\x89PNG\r\n\x1a\n")
assert example[12:16] == b"IHDR"
w, h, bits, color, compression, filt, interlace = struct.unpack(
    ">IIBBBBB", example[16:29]
)
assert (w, h) == (320, 240) and 3 * w == 4 * h
assert bits == 8 and color in (2, 3) and interlace == 0
assert len(example) <= int(1.5 * 1024 * 1024)

assert "background = image" in cfg
assert "image = background.png" in cfg
assert "wallpaper = off" in cfg
assert 'float screen_aspect = 4.0f / 3.0f' in bg
assert "dashThemeTextureFile(dashTheme.image, 320, 256)" in bg
assert "320x240" in theme and "640 × 480" in theme
assert "wallpaper-example.png" in site
assert "320 &times; 240" in site
assert "Theme-and-Wallpaper-Guide.md" in home
assert "320 × 240" in wiki
assert "theme-example/background.png" in wiki

print("PASS: 320x240 non-interlaced PNG example, theme.cfg, source crop and site/wiki links")
