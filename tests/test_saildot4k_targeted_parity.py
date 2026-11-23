"""Check selected saildot4k parity without importing the broader fork."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sort = (root / "src/gui_sort.c").read_text()
hdd = (root / "src/dash_hdd.c").read_text()
filer = (root / "src/filer.c").read_text()
browser = (root / "src/dash_browser.c").read_text()
icons = (root / "src/dash_icons.c").read_text()
header = (root / "include/dash_internal.h").read_text()
store = (root / "src/dash_store.c").read_text()
settings = (root / "src/dash_settings.c").read_text()
bg = (root / "src/dash_bg.c").read_text()
themes = (root / "docs/themes.md").read_text()

def function(source, signature):
    a = source.index(signature)
    brace = source.index("{", a)
    depth, p = 1, brace + 1
    while depth:
        depth += (source[p] == "{") - (source[p] == "}")
        p += 1
    return source[a:p] + "\n"

code = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define stricmp strcasecmp
enum { HP_PFS, HP_HDL, HP_SYSTEM, HP_OTHER };
typedef struct { char name[33]; int type; } HddPart;
""" + function(sort, "int hddPartitionPriority(") + function(hdd, "static int partCmp(") + r"""
int main(void) {
    HddPart entries[] = {
        {"PP.ZZZ", HP_HDL}, {"__common", HP_PFS},
        {"__system", HP_PFS}, {"__mbr", HP_SYSTEM},
        {"__net", HP_PFS}, {"__sysconf", HP_PFS},
        {"+OPL", HP_PFS}, {"__other", HP_OTHER},
    };
    const char *expect[] = {
        "__mbr", "__net", "__system", "__sysconf",
        "__common", "+OPL", "PP.ZZZ", "__other"
    };
    assert(hddPartitionPriority("__NET") == 1);
    assert(hddPartitionPriority("unknown") == 5);
    qsort(entries, 8, sizeof(entries[0]), partCmp);
    for (int i = 0; i < 8; i++) assert(!strcmp(entries[i].name, expect[i]));
    return 0;
}
"""
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler is needed for partition-sort regression."
with tempfile.TemporaryDirectory() as temp:
    p = Path(temp)
    (p / "check.c").write_text(code)
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(p / "check.c"), "-o", str(p / "check")], check=True)
    subprocess.run([str(p / "check")], check=True, timeout=10)

# One priority implementation; File Browser and manager both use it.
assert "hddPartitionPriority(x) - hddPartitionPriority(y)" in filer
assert "qsort(hddParties[unit]" in filer
assert 'if (!strncmp(dirEnt.name, "__mbr", 6))' in filer
assert "isPartitionList(fb.dir) ? DI_PARTITION : DI_FOLDER" in browser
assert "case DI_PARTITION:" in icons and "case DI_HDD_MANAGER:" in icons
assert "#define ICON_TEX_H 580" in icons  # no added full-size atlas row
assert "DI_PARTITION" in header and "DI_HDD_MANAGER" in header
assert "#define MOUNT_LIMIT 4" in (root / "include/launchelf.h").read_text()  # four PFS slots when FTP is off
assert "pfsRetainedLimit() - 1" in hdd  # adjust to FTP handle usage
assert "j->ret = pfsMakeRoom(pfsRetainedLimit() - 1);" in hdd  # scan reserves one handle before enumeration
assert "DASH_IN_R1" in hdd and "DASH_IN_L2" in hdd  # preserve controller routes

mc = function(filer, "int readMC(")
assert "ret = mcGetDir(MC_UNIT_PORT(port), MC_UNIT_SLOT(port)" in mc
assert "if (mcSync(0, NULL, &ret) <= 0)" in mc
assert "memcpy(info[j].name, mcDir[i].EntryName, sizeof(mcDir[i].EntryName))" in mc
assert "info[j].name[sizeof(mcDir[i].EntryName)] = '\\0';" in mc

# Background detail must not redefine speed, existing startup settings or themes.
assert "int bg_detail;" in header
assert "dashCfg.bg_detail = 1;" in store
assert '"Dash_BackgroundDetail = %d' in store
assert '"Dash_BackgroundDetail"' in store
assert "case S_BG_DETAIL:" in settings
assert "dashCfg.bg_detail = 1;" in settings
assert "int requested = dashCfg.bg_detail ? DASH_FX_FULL : DASH_FX_SUBTLE;" in bg
assert "return bg_perf_reduced ? DASH_FX_SUBTLE : DASH_FX_FULL;" in bg  # preserve frame-time fallback
assert "float motion = animated ? dashCfg.effects / 100.0f : 0.0f;" in bg
# Crystal theme layers obey the render mode, not animation speed: with
# 0% motion and High detail the star layers must remain visible.
crystal_layers = bg[bg.index("static void drawThemeLayers("):bg.index("static void drawOrbs(")]
assert "static void drawThemeLayers(int fx)" in bg
assert "drawThemeLayers(fx);" in bg
assert "if (fx == DASH_FX_OFF)" in crystal_layers
assert "dashCfg.effects == DASH_FX_OFF" not in crystal_layers
assert "* *Background detail: Low* reduces scene geometry" in themes
print("PASS: HDD sorting, compact icons, PS1 directory guards, independent background detail")

# Classic HDD root entries render partition glyphs, not ordinary folder glyphs.
classic = (root / "src/filer_browser.c").read_text()
header = (root / "include/launchelf.h").read_text()
ule_font = (root / "src/font_uLE.c").read_text()
bitmap_font = (root / "src/dash_font.c").read_text()
assert "ICON_PARTITION = 0x124" in header and "ICON_M_PARTITION = 0x126" in header
assert "FONT_COUNT = 0x128" in header and "#define FONT_LAST 0x127" in bitmap_font
assert "//Font position 0x124 == Partition icon unmarked" in ule_font
assert "//Font position 0x126 == Partition icon marked" in ule_font
assert "isHddRootPath(path) || !strcmp(path, \"dvr_hdd0:/\")" in classic
assert "strcmp(files[top + i].name, \"..\")" in classic
assert "iconbase += 2;" in classic  # selected partition glyph

# Popup alignment reuses the existing localized, responsive hint layout.
ui = (root / "src/dash_ui.c").read_text()
widgets = (root / "src/dash_widgets.c").read_text()
hdd = (root / "src/dash_hdd.c").read_text()
main = (root / "src/dash_main.c").read_text()
assert "float dashButtonTextY(float button_y, float text_scale)" in ui
assert "button_y + 11.0f - 16.0f * text_scale" in ui
assert "dashButtonTextY(y, fs), label, fs" in ui
assert "hintOrder(in_hints, n, order)" in ui
assert "dashButtonTextY(y, DASH_FS_SMALL)" in widgets
assert 'dashButtonTextY(hy, DASH_FS_SMALL), "128 MB"' in hdd
for label in ('1 GB', '10 GB'):
    assert f'dashButtonTextY(hy, DASH_FS_SMALL), "{label}"' in hdd
assert "dashTextFit(bx, dashButtonTextY(by, DASH_FS_SMALL)" in main
