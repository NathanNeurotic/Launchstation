"""Settings help text must end above the footer hint bar.

Regression: with eight rows the two-line help started at y=380 and its second
line was drawn over the button hints (bar background starts at y=394).
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")

rows = int(re.search(r"#define ITEM_ROWS (\d+)", settings).group(1))
geo = re.search(r"const float ix = \d+, iy = (\d+), irow = (\d+), iw = \d+;", settings)
iy, irow = int(geo.group(1)), int(geo.group(2))
wrap = re.search(r"dashTextWrap\(ix, iy \+ ITEM_ROWS \* irow \+ (\d+), iw, (\d+), help, DASH_FS_CAPTION, DASH_C_TEXT3, (\d+)\)", settings)
gap, lines, line_h = (int(g) for g in wrap.groups())

hints = ui[ui.index("void dashDrawHints("):]
hint_y = float(re.search(r"float y = ([\d.]+)f;", hints).group(1))
bar_top = hint_y - float(re.search(r"dashRectGradH\(x - 40, y - (\d+)", hints).group(1))

help_bottom = iy + rows * irow + gap + lines * line_h
assert help_bottom <= bar_top, (help_bottom, bar_top)
print(f"PASS: settings help ends at y={help_bottom}, above the hint bar at y={bar_top:g}")

# Theme borders outline panels: with a transparent panel (Panels opacity 0)
# neither list boxes, cards nor the header strip may keep a floating border.
border_rule = ui[ui.index("static int panelBorderVisible(void)"):ui.index("void dashDrawSelection(")]
assert "return DASH_C_PANEL.a && DASH_C_BORDER.a;" in border_rule
assert border_rule.count("if (panelBorderVisible())") == 2
header = ui[ui.index("void dashDrawHeader("):ui.index("void dashDrawSectionTitle(")]
assert "if (DASH_C_PANEL.a && !DASH_C_BORDER.a && dashTheme.glow)" in header
print("PASS: panel borders disappear with transparent panels")
