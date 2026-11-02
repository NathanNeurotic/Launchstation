"""Row text must sit centred in the selection bar on every list (#123).

Settings and Tools drew their row text ~4 px below the bar centre (tight at
the bottom), File Browser/HDD/picker ~2 px, while Files and Applications were
centred. The font puts the lowercase x-height centre at y + 16 * scale, so
for each list: text_y + 16*scale must be within 1 px of the bar centre.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = lambda f: (root / "src" / f).read_text(encoding="utf-8")
SMALL, CAPTION = 0.66, 0.60


def num(text, pattern):
    return float(re.search(pattern, text).group(1))


lists = []
s = src("dash_settings.c")
irow = num(s, r"irow = (\d+)")
lists.append(("Settings", 2, irow - 5, s, r"dashTextFit\(ix \+ 38, y \+ ([\d.]+)f, iw - 70, localizeDashboardText\(it->label", SMALL))
lists.append(("Settings value", 2, irow - 5, s, r"dashTextFit\(vx, y \+ ([\d.]+)f, maxw, v,", SMALL))
t = src("dash_tools.c")
lists.append(("Tools", 0, num(t, r"ly = 102, row_h = (\d+)") - 3, t, r"dashTextFit\(lx \+ 34, y \+ ([\d.]+)f, lw - 44, localizeDashboardText\(tools\[i\]\.name", SMALL))
f = src("dash_files.c")
lists.append(("Files", 0, num(f, r"ly = 102, row_h = (\d+)") - 3, f, r"dashTextFit\(lx \+ 34, y \+ ([\d.]+), lw - 50, f->label", SMALL))
a = src("dash_apps.c")
lists.append(("Applications", 0, num(a, r"#define LIST_ROW_H ([\d.]+)f") - 3, a, r"dashTextFit\(lx \+ 38, y \+ ([\d.]+), lw - 64, e->title", SMALL))
b = src("dash_browser.c")
fb = num(b, r"#define FB_ROW_H ([\d.]+)f") - 3
lists.append(("File Browser", 0, fb, b, r"dashTextFit\(FB_X \+ 34, y \+ ([\d.]+), label_w, label", SMALL))
lists.append(("File Browser size", 0, fb, b, r"dashTextRight\(size_right, y \+ ([\d.]+), sz", CAPTION))
h = src("dash_hdd.c")
hr = num(h, r"#define ROW_H ([\d.]+)f") - 3
lists.append(("HDD Manager", 0, hr, h, r"drawPartName\(LIST_X \+ 30, y \+ ([\d.]+)f", SMALL))
lists.append(("HDD Manager size", 0, hr, h, r"dashTextRight\(LIST_X \+ LIST_W - 10, y \+ ([\d.]+), a,", CAPTION))
w = src("dash_widgets.c")
lists.append(("Folder picker", 0, num(w, r"w = 480\.0f, row_h = ([\d.]+)f") - 3, w, r"dashTextFit\(lx \+ 36, ry \+ ([\d.]+)f, mount \?", SMALL))

for name, top, height, text, pattern, scale in lists:
    ty = num(text, pattern)
    off = ty + 16 * scale - (top + height / 2)
    assert -1.0 <= off <= 1.0, f"{name}: text centre is {off:+.1f} px from the selection bar centre"
    print(f"  {name:18s} {off:+.1f} px")
print("PASS: row text is centred in the selection bar on every list")
