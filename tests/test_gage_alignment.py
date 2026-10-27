"""Guard Gage's alignment feedback on Home and the hint bar.

* The hint bar is right-aligned, so its measuring pass must use the same
  widths/gaps as its drawing pass; otherwise screens with more START/SELECT
  key hints (Files) end further left than the other tabs.
* The focused Home tile must not touch the Continue underline or the Quick
  launch label, and the underlines span their whole label.
* Labels beside icons place their x-height centre (y + 16 * scale) near the
  icon centre instead of ~4 px below it.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")
home = (root / "src/dash_home.c").read_text(encoding="utf-8")

hints = ui[ui.index("void dashDrawHints("):]
hints = hints[: hints.index("\n}\n") if "\n}\n" in hints else hints.index("\r\n}\r\n")]
assert "fixed += buttonWidth(hints[i].button, hints[i].key) + 8.0f" in hints
assert "x += drawButton(x, y, hints[i].button, hints[i].key) + 8.0f;" in hints
assert "total = fixed + n * gap + labels;" in hints
assert "x += gap;" in hints
# at full size the label sits at y + 1.5; shrunk type keeps the same x-height centre
assert "dashTextS(x, y + 1.5f + (DASH_FS_SMALL - fs) * 16.0f, label, fs" in hints
assert "dashTextFit" not in hints, "hint labels are drawn whole, never ellipsized"
assert "+ 16.0f;" not in hints

FS_SMALL = 0.66


def num(pattern: str) -> float:
    m = re.search(pattern, home)
    assert m, pattern
    return float(m.group(1))


tile = num(r"#define HOME_TILE ([\d.]+)f")
zoom = num(r"#define HOME_TILE_ZOOM ([\d.]+)f")
y0 = num(r"float y0 = ([\d.]+), cw = HOME_TILE;")
underline = y0 - num(r"dashRect\(DASH_SAFE_L, y0 - ([\d.]+), dashTextWidth\(localizeDashboardText\(\"Continue\"")
cy = num(r"float cy = ([\d.]+);")
quick_label = cy - num(r"dashText\(DASH_SAFE_L, cy - ([\d.]+), \"Browse & shortcuts\"")

grow = tile * zoom / 2
assert y0 - grow - (underline + 2) >= 6, "focused tile touches the Continue underline"
assert quick_label - (y0 + tile + grow) >= 6, "focused tile touches the Browse & shortcuts label"
assert cy + 32 <= 394, "Browse & shortcuts chips run into the hint bar"

# Chip label: icon 16 px at cy + 8 -> centre cy + 16.
chip_text = num(r"dashTextFit\(x \+ 34, cy \+ ([\d.]+)f")
assert abs((chip_text + 16 * FS_SMALL) - 16) <= 1.5, chip_text

print("PASS: hint bar width and Home row spacing/alignment are consistent")
