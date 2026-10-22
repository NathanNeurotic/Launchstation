"""Navigation wrap-around contracts for dashboard list controllers (#189)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]


def src(name):
    return (root / "src" / name).read_text(encoding="utf-8")


browser = src("dash_browser.c")
apps = src("dash_apps.c")
files = src("dash_files.c")
tools = src("dash_tools.c")
settings = src("dash_settings.c")
widgets = src("dash_widgets.c")
hdd = src("dash_hdd.c")
home = src("dash_home.c")

# File Browser: directional and page motion wraps instead of clamping.
assert "static int wrapIndex(int index, int total)" in browser
for expr in (
    "fb.sel = wrapIndex(fb.sel - 1, total);",
    "fb.sel = wrapIndex(fb.sel + 1, total);",
    "fb.sel = wrapIndex(fb.sel - FB_ROWS, total);",
    "fb.sel = wrapIndex(fb.sel + FB_ROWS, total);",
):
    assert expr in browser

# Applications: list/grid/carousel moves now cycle through all entries.
assert "next = (sel + delta) % norder;" in apps
assert "if (next < 0)" in apps and "next += norder;" in apps
assert "if (in->pressed & DASH_IN_UP)\n\t\t\tmove(-GRID_COLS);" in apps
assert "if (in->pressed & DASH_IN_DOWN)\n\t\t\tmove(GRID_COLS);" in apps

# Files list and Devices grid: movement wraps and keeps skipping non-selectable rows.
assert "if (s < 0)\n\t\t\ts = nfentries - 1;" in files
assert "else if (s >= nfentries)\n\t\t\ts = 0;" in files
assert "while (s < 0)\n\t\t\ts += ndevs;" in files
assert "while (s >= ndevs)\n\t\t\ts -= ndevs;" in files

# Tools and Settings lists wrap at top/bottom.
assert "if (s < 0)\n\t\t\ts = NTOOLS - 1;" in tools
assert "else if (s >= NTOOLS)\n\t\t\ts = 0;" in tools
assert "int next = item > 0 ? item - 1 : total - 1;" in settings
assert "int next = item < total - 1 ? item + 1 : 0;" in settings

# Picker and HDD manager now cycle through rows.
assert "sel = sel > 0 ? sel - 1 : n_rows - 1;" in widgets
assert "sel = sel < n_rows - 1 ? sel + 1 : 0;" in widgets
assert "sel = sel > 0 ? sel - 1 : hm.n - 1;" in hdd
assert "sel = (sel + 1) % hm.n;" in hdd

# Home row/chip strips now use wrapped movement.
assert "static int wrapCol(int value, int n)" in home
assert "int next = wrapCol(col[row] - 1, n);" in home
assert "int next = wrapCol(col[row] + 1, n);" in home

print("PASS: dashboard list navigation wraps across file, menu and browser views")
