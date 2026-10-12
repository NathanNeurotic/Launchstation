"""The embedded pad-button PNG tiles must follow the DI_BTN_* enum order.

Regression: tiles 8/9 were loaded as select/start while the enum declares
DI_BTN_START before DI_BTN_SELECT, so every START ("Menu") hint drew the
SELECT bar whenever the pad PNG atlas was available.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
header = (root / "include/dash_internal.h").read_text(encoding="utf-8")
icons = (root / "src/dash_icons.c").read_text(encoding="utf-8")

enum = re.findall(r"\bDI_BTN_(\w+),", header[header.index("DI_BTN_CROSS,"):header.index("DI_BTN_R3,") + 11])
blob = icons[icons.index("static const void *pad_blob("):icons.index("static int pad_blob_size(")]
sizes = icons[icons.index("static int pad_blob_size("):icons.index("int dashIconsPadPng(")]
tiles = re.findall(r"return icon_(\w+)_png;", blob)
tile_sizes = re.findall(r"return size_icon_(\w+)_png;", sizes)

assert len(enum) == 16, enum
assert [t.upper() for t in tiles] == enum, list(zip(enum, tiles))
assert tile_sizes == tiles, list(zip(tiles, tile_sizes))
print("PASS: pad PNG tiles match DI_BTN_* order (START draws the START glyph)")

# Hint key badges must name real PS2 controller buttons ("BACK" is not one;
# the back action uses dashBackIcon(), i.e. Circle or Cross).
allowed = {"START", "SELECT", "L1", "L2", "L3", "R1", "R2", "R3", "L1/R1", "L2/R2"}
for path in sorted((root / "src").glob("*.c")):
    for key in re.findall(r'\.key = "([^"]+)"', path.read_text(encoding="utf-8", errors="replace")):
        assert key in allowed, f"{path.name}: hint key {key!r} is not a PS2 button"
print("PASS: hint key badges name real PS2 buttons")
