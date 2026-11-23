"""HDD Manager regression: bounds on 4:3 detail panel and on-demand HDL RPCs.

A long APA identifier (PP.SLUS-... etc.) was centered without a width
constraint, drawing through the edge of the 244px details card. Continuous
title lookup on every 180ms selection pause also changed classic browsing
labels and could cause synchronous HDL RPC delays during scrolling.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_hdd.c").read_text(encoding="utf-8")


def function(signature, text=source):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, pos = 1, brace + 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


details = function("static void drawDetails(")
scene = function("static void hddScene(")
manager = function("void dashHddManager(")
rename = function("static void actRenameGame(")
menu = function("static void partitionMenu(")

# Every long, dynamic headline/line in the narrow details card is width
# limited. Avoid unbounded centered names and startup IDs.
assert '#define DET_W 244.0f' in source
assert "dashTextScrollProgress(x + 16, y + 72, w - 32, p->name" in details
assert "dashTextFit(x + 16, y + 98, w - 32, typeName(p)" in details
assert "dashTextFit(x + 16, y + 122, w - 32, line" in details
assert "dashTextFit(x + 16, y + 44, w - 32, line" in details
assert "dashTextWrap(x + 16, y, w - 32, 2, p->game.Name" in details
assert "dashTextCenter(x + w * 0.5f, y + 72, p->name" not in details
assert "dashTextCenter(x + w * 0.5f, y + 44, line" not in details
assert "dashTextFit(40, 78, 560, line" in scene

# Display APA identifiers, not titles, while navigating the partition list.
assert "partNameScrollProgress(selected, dashUiFocusAge())" in scene
assert "drawDetails(scroll);" in scene
assert "dashUiFocusPoke();" in manager
assert "const char *label = p->name;" in scene
assert "const char *label = (p->type == HP_HDL" not in scene
assert "drawPartName(LIST_X + 30, y + 1.5f, LIST_W - 110, label, col, i == sel, scroll);" in scene

# Idle browsing may read PFS usage but must not perform synchronous HDL
# title queries. R1 and the explicit Rename game action may request them.
idle = manager[manager.index("if (hm.n > 0 && Timer() - last_move > 180)"):
               manager.index("sel_anim = dashApproach(", manager.index("if (hm.n > 0 && Timer() - last_move > 180)"))]
assert "loadPfsInfo(sel);" in idle
assert "loadGameInfo(" not in idle
assert "DASH_IN_R1" in manager
assert "loadGameInfo(&hm.parts[sel]);" in manager
assert "loadGameInfo(p);" in rename
assert '"Game info"' in scene
assert '"Press R1 for game information"' in details
# Paging: L2/R2 kept, D-pad Left/Right added; L1 status and Cross/Triangle actions kept.
assert "DASH_IN_L2" in manager and "DASH_IN_R2" in manager
assert "DASH_IN_LEFT" in manager and "DASH_IN_RIGHT" in manager
assert "DASH_IN_L1" in manager and "(DASH_IN_OK | DASH_IN_MENU)" in manager
assert '"HDD status"' in menu

# The HDL title part of the raw APA name is highlighted (ID prefix dimmed),
# so games are recognisable without any per-row HDL lookups.
import shutil, subprocess, tempfile
offset_fn = function("static int hdlTitleOffset(")
harness = "#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n" + offset_fn + r"""
int main(void) {
    const char *gt3 = "PP.SCUS-97102..GRAN_TURISMO_3__";
    assert(!strcmp(gt3 + hdlTitleOffset(gt3), "GRAN_TURISMO_3__"));
    assert(!strcmp("PP.HDL.Dead or Alive 2" + hdlTitleOffset("PP.HDL.Dead or Alive 2"), "Dead or Alive 2"));
    assert(hdlTitleOffset("+OPL") == 0 && hdlTitleOffset("__common") == 0);
    assert(hdlTitleOffset("PP.SLUS-20062..") == 0); /* no title: keep plain */
    return 0;
}
"""

# Exercise the production name renderer and shared progress, including full
# HDL IDs with a dimmed prefix, clipping, fitting names and font size changes.
import re
font = (root / "src/dash_font.c").read_text(encoding="utf-8")
header = (root / "include/dash_internal.h").read_text(encoding="utf-8")
scroll_harness = r"""
#include <math.h>
typedef int DashColor;
typedef struct { char name[40]; } HddPart;
static float font_scale = 1;
static int face, depth = 1, pushes, pops, draws, fits;
static float first_x, last_end, clip_x, clip_w;
static char drawn[2][40];
static DashColor colors[2];
static float dashClampf(float v, float lo, float hi) { return fminf(hi, fmaxf(lo, v)); }
static float dashTextWidth(const char *s, float scale) {
    float w = 0;
    for (; *s; s++) w += (face ? 10 : (*s == 'W' ? 14 : *s == 'i' || *s == '.' ? 5 : 9)) * scale * font_scale;
    return w;
}
static float dashTextLineH(float scale) { return 26 * scale * font_scale; }
static DashColor dashColorA(DashColor c, float alpha) { assert(alpha == 0.5f); return c / 2; }
static void dashGfxScissorPush(float x, float y, float w, float h) {
    (void)y; assert(depth == 1 && w > 0 && h > 0);
    depth++; pushes++; clip_x = x; clip_w = w;
}
static void dashGfxScissorPop(void) { assert(depth == 2); depth--; pops++; }
static float dashTextS(float x, float y, const char *s, float scale, DashColor c) {
    (void)y; assert(draws < 2 && strlen(s) < 40);
    if (!draws) first_x = x;
    strcpy(drawn[draws], s); colors[draws++] = c;
    last_end = x + dashTextWidth(s, scale);
    return last_end;
}
static float dashTextFit(float x, float y, float w, const char *s, float scale, DashColor c) {
    fits++; dashTextS(x, y, s, scale, c);
    return x + fminf(w, dashTextWidth(s, scale));
}
"""
for macro in ("DASH_FS_SMALL", "DASH_FS_LABEL"):
    scroll_harness += re.search(r"^#define " + macro + r" .+$", header, re.M).group() + "\n"
for macro in ("LIST_W", "DET_W"):
    scroll_harness += re.search(r"^#define " + macro + r" .+$", source, re.M).group() + "\n"
scroll_harness += function("static float partNameScrollProgress(")
scroll_harness += function("static void drawPartName(")
scroll_harness += function("void dashTextScrollProgress(", font)
scroll_harness += r"""
static void resetDraw(void) { draws = fits = pushes = pops = 0; assert(depth == 1); }
static void checkAt(const HddPart *p, float age, float expected) {
    float row_travel = fmaxf(0, dashTextWidth(p->name, DASH_FS_SMALL) - (LIST_W - 110));
    float detail_travel = fmaxf(0, dashTextWidth(p->name, DASH_FS_LABEL) - (DET_W - 32));
    if (row_travel == 0 && detail_travel == 0) expected = 0;
    float progress = partNameScrollProgress(p, age);
    assert(fabsf(progress - expected) < 0.0001f);
    resetDraw();
    drawPartName(70, 110, LIST_W - 110, p->name, 100, 1, progress);
    assert(!fits && pushes == 1 && pops == 1 && depth == 1);
    assert(clip_x == 70 && clip_w == LIST_W - 110);
    assert(fabsf(first_x - (70 - row_travel * expected)) < 0.002f);
    assert(fabsf(last_end - (first_x + dashTextWidth(p->name, DASH_FS_SMALL))) < 0.002f);
    int off = hdlTitleOffset(p->name);
    if (off) {
        assert(draws == 2 && colors[0] == 50 && colors[1] == 100);
        assert(strlen(drawn[0]) == (size_t)off && !strncmp(drawn[0], p->name, off));
        assert(!strcmp(drawn[1], p->name + off));
    } else assert(draws == 1 && colors[0] == 100 && !strcmp(drawn[0], p->name));
    resetDraw();
    dashTextScrollProgress(372, 174, DET_W - 32, p->name, DASH_FS_LABEL, 100, progress);
    assert(draws == 1 && !fits && depth == 1 && pushes == pops);
    assert(fabsf(first_x - (372 - detail_travel * expected)) < 0.002f);
    resetDraw();
    drawPartName(70, 110, LIST_W - 110, p->name, 100, 0, progress);
    assert(fits == draws && !pushes && !pops && depth == 1 && first_x == 70);
}
static void checkScroll(void) {
    HddPart parts[] = {{"PP.SCUS-97102..GRAN_TURISMO_3__"}, {"PP.HDL.Dead or Alive 2"},
                       {"+Long_Partition_Name_WWWWWWWWWW"}, {"+OPL"}, {"PP.HDL.A"}};
    assert(partNameScrollProgress(NULL, 100) == 0);
    for (face = 0; face < 2; face++) for (int size = 0; size < 3; size++) {
        font_scale = size == 0 ? 0.75f : size == 1 ? 1.0f : 1.5f;
        for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
            HddPart *p = &parts[i];
            float travel = fmaxf(dashTextWidth(p->name, DASH_FS_SMALL) - (LIST_W - 110),
                                  dashTextWidth(p->name, DASH_FS_LABEL) - (DET_W - 32));
            float move = fmaxf(travel / 34, 0.35f), end = 3 + move, cycle = end + 3;
            checkAt(p, -1, 0); checkAt(p, 0, 0); checkAt(p, 2.99f, 0); checkAt(p, 3, 0);
            checkAt(p, 3 + move / 2, 0.5f); checkAt(p, end, 1);
            checkAt(p, cycle - 0.01f, 1); checkAt(p, cycle + 0.01f, 0);
            float previous = 0;
            for (int frame = 0; frame < 1000; frame++) {
                float age = (cycle - 0.01f) * frame / 999;
                float progress = partNameScrollProgress(p, age);
                assert(progress >= previous && progress >= 0 && progress <= 1);
                previous = progress;
            }
        }
    }
    puts("PASS: partition names scroll together, dwell at each end, restart, retain HDL styling and restore list clipping; fitting and unselected names stay still");
}
"""
harness = harness.replace("int main(void) {", scroll_harness + "\nint main(void) {\n    checkScroll();", 1)

compiler = shutil.which("gcc") or shutil.which("clang")
filer = (root / "src/filer_browser.c").read_text()
path_harness = '#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n'
path_harness += function("static int isHddBrowserPath(", filer)
path_harness += function("void filerFormatBrowserPathForDisplay(", filer)
path_harness += r'''
static void check(const char *path, const char *want)
{
    char out[128];
    filerFormatBrowserPathForDisplay(path, out, sizeof(out));
    assert(!strcmp(out, want));
}
int main(void)
{
    char browse[] = "hdd0:/__common/APPS/";
    check(browse, "hdd0:__common:pfs:/APPS/");
    assert(!strcmp(browse,"hdd0:/__common/APPS/"));
    check("hdd1:/+APPS", "hdd1:+APPS:pfs:/");
    check("hdd0:/", "hdd0:/");
    check("hdd0:__common:pfs:/APPS/", "hdd0:__common:pfs:/APPS/");
    check("dvr_hdd0:/+APPS/", "dvr_hdd0:+APPS:pfs:/");
    check("mass0:/", "usb0:/");
    check("vmc0:/SAVE/", "vmc0:/SAVE/");
}
'''
with tempfile.TemporaryDirectory(prefix="wle-hdd-path-") as tmp:
    c, exe = Path(tmp) / "path.c", Path(tmp) / "path.exe"
    c.write_text(path_harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-DDVRP",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)

with tempfile.TemporaryDirectory(prefix="wle-hdd-") as tmp:
    c, exe = Path(tmp) / "h.c", Path(tmp) / "h.exe"
    c.write_text(harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", str(c), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
draw_name = function("static void drawPartName(")
assert "dashColorA(col, 0.5f)" in draw_name and "maxw * 0.65f" in draw_name

# Keep the hint count in sync so the Exit hint is always drawn.
assert "dashDrawHints(h, (int)(sizeof(h) / sizeof(h[0])));" in scene

print("PASS: APA IDs stay visible with the HDL title highlighted; bounded 4:3 detail text; HDL RPC only on demand; all hints drawn")
