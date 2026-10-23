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
assert "move(sel == 0 ? norder - 1" in apps
assert "move(sel == norder - 1 ? -sel" in apps

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


# Execute the production directional-input block and viewport helper on the host.
# SDK actions after navigation are excluded; selection arithmetic is unchanged.
import shutil
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


start = browser.index("\t\tif (total > 0) {", browser.index("static void browserProcessInput"))
end = browser.index("\n\t\tif (fb.sel != prev", start)
browser_move = browser[start:end]
start = apps.index("\t\tif (in->pressed & DASH_IN_UP)", apps.index("static void appsInput"))
end = apps.index("\n\t\tif (in->pressed & DASH_IN_L2)", start)
grid_move = apps[start:end]
code = r"""
#include <assert.h>
#include <stddef.h>
#define FB_ROWS 12
#define GRID_COLS 5
#define GRID_PITCH_Y 100
#define DASH_IN_UP 1
#define DASH_IN_DOWN 2
#define DASH_IN_LEFT 4
#define DASH_IN_RIGHT 8
#define DASH_IN_L2 16
#define DASH_IN_R2 32
typedef struct { unsigned pressed; } DashInput;
static struct { int sel, top, n; float sel_anim; } fb;
static int sel, norder;
static float sel_anim, grid_cam;
static void dashUiFocusPoke(void) {}
"""
code += function(browser, "static int wrapIndex(") + "\n"
code += function(browser, "static void revealSelection(") + "\n"
code += function(apps, "static void move(") + "\n"
code += r"""
#define DASH_FS_SMALL 12
#define DASH_SAFE_R 592
static int row, nhome, nchips;
static float col_anim[2], cam[2], chip_cam;
static struct { char label[48]; } chips[24];
static float dashTextWidth(const char *label, int size) {(void)label;(void)size;return 140;}
"""
code += function(home, "static float homeChipScrollTarget(") + "\n"
code += function(home, "static void snapHomeColumn(") + "\n"

code += "static void browserMove(const DashInput *in, int total) {\n" + browser_move + "\nrevealSelection();}\n"
code += "static void gridMove(const DashInput *in) {\n" + grid_move + "\n}\n"
code += r"""
int main(void) {
 DashInput up={DASH_IN_UP}, down={DASH_IN_DOWN};
 const int counts[]={0,1,2,5,7,12,13,127};
 for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);i++) {
  int n=counts[i]; fb.n=n; fb.sel=fb.top=0; fb.sel_anim=0;
  browserMove(&up,n); assert(fb.sel==(n?n-1:0));
  assert(fb.sel>=fb.top && fb.sel<fb.top+FB_ROWS);
  assert(fb.sel_anim>=fb.top && fb.sel_anim<fb.top+FB_ROWS);
  fb.sel_anim=(float)fb.sel;
  browserMove(&down,n); assert(fb.sel==0 && fb.top==0);
  assert(fb.sel_anim>=fb.top && fb.sel_anim<fb.top+FB_ROWS);
 }
 fb.n=127;fb.sel=5;fb.top=0;fb.sel_anim=5;
 browserMove(&down,127);assert(fb.sel==6 && fb.sel_anim==5); /* retain adjacent easing */
 for(norder=1;norder<=17;norder++) {
  sel=0;sel_anim=grid_cam=0;gridMove(&up);assert(sel==norder-1 && sel_anim==sel);
  gridMove(&down);assert(sel==0 && sel_anim==0 && grid_cam==0);
  for(int old=0;old<norder;old++) {
   sel=old;gridMove(&up);assert(sel>=0 && sel<norder);
   if(old>=GRID_COLS)assert(sel==old-GRID_COLS);
   sel=old;gridMove(&down);assert(sel>=0 && sel<norder);
   if(old+GRID_COLS<norder)assert(sel==old+GRID_COLS);
  }
 }
 row=1;nchips=12;chip_cam=0;snapHomeColumn(11);
 assert(chip_cam==homeChipScrollTarget(11) && chip_cam>0 && col_anim[1]==11);
 snapHomeColumn(0);assert(chip_cam==0 && col_anim[1]==0);
 row=0;nhome=12;cam[0]=0;snapHomeColumn(11);assert(cam[0]>0 && col_anim[0]==11);
 snapHomeColumn(0);assert(cam[0]==0 && col_anim[0]==0);
 return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("cc")
assert cc, "A host C compiler is required"
with tempfile.TemporaryDirectory(prefix="navigation-wrap-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    binary = path / "test.exe"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: production browser endpoint/viewport transitions and incomplete application grids")
