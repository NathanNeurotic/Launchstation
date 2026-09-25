#!/usr/bin/env python3
"""Build an optional dashboard THEME folder from a PSBBN texture pack.

The dashboard never embeds third-party artwork. If you own the PSBBN
texture PNGs (e.g. PSBBN-Texture-PNGs.zip), this script converts the few
layers the dashboard can use into small PS2-friendly PNGs:

    THEME/background.png  256x128  base space backdrop     (space1024.png)
    THEME/stars1.png      256x128  far star layer, additive (space_layer1.png)
    THEME/stars2.png      256x128  near star layer, additive (space_layer2.png)

Copy the resulting THEME folder next to LAUNCHELF.CNF (or to mc0:/SYS-CONF/
if the configuration lives there). The dashboard loads each file lazily,
caps every image at 256x256 in RAM (128 KB CT32 each) and falls back to its
procedural background when a file is missing.

Usage:
    python scripts/prepare_psbbn_theme.py PSBBN-Texture-PNGs.zip [out_dir]

Requires Pillow (pip install pillow).
"""
import io
import os
import sys
import zipfile

from PIL import Image

LAYERS = [
    ("space1024.png", "background.png", (256, 128), False),
    ("space_layer1.png", "stars1.png", (256, 128), True),
    ("space_layer2.png", "stars2.png", (256, 128), True),
]


def open_source(src):
    """Returns a callable name -> PIL.Image for a zip file or a directory."""
    if os.path.isdir(src):
        def get(name):
            for root, _dirs, files in os.walk(src):
                for f in files:
                    if f.lower() == name.lower():
                        return Image.open(os.path.join(root, f))
            return None
        return get
    zf = zipfile.ZipFile(src)
    index = {os.path.basename(n).lower(): n for n in zf.namelist()}

    def get(name):
        n = index.get(name.lower())
        return Image.open(io.BytesIO(zf.read(n))) if n else None
    return get


def boost_stars(img):
    """Star layers are mostly black with sparse stars; lift faint stars so
    they survive the downscale, and keep them usable for additive blending."""
    img = img.convert("RGBA")
    r, g, b, a = img.split()
    lum = Image.merge("RGB", (r, g, b)).convert("L")
    lum = lum.point(lambda v: min(255, int(v * 1.8)))
    return Image.merge("RGBA", (lum, lum, lum, lum))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    get = open_source(sys.argv[1])
    out_dir = os.path.join(sys.argv[2] if len(sys.argv) > 2 else ".", "THEME")
    os.makedirs(out_dir, exist_ok=True)
    made = 0
    for src_name, dst_name, size, additive in LAYERS:
        img = get(src_name)
        if img is None:
            print("missing %s, skipped" % src_name)
            continue
        img = boost_stars(img) if additive else img.convert("RGBA")
        img = img.resize(size, Image.LANCZOS)
        if not additive:
            img = img.convert("RGB").convert("RGBA")
        img.save(os.path.join(out_dir, dst_name), optimize=True)
        made += 1
        print("wrote %s (%dx%d)" % (os.path.join(out_dir, dst_name), size[0], size[1]))
    return 0 if made else 1


if __name__ == "__main__":
    sys.exit(main())
