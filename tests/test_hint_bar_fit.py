"""Footer hint labels are drawn whole: gaps tighten, then the type shrinks, never "..." (#221)."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")
a = ui.index("void dashDrawHints(")
func = ui[a:ui.index("\n}\n", a) + 3]

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DASH_SAFE_L 40
#define DASH_SAFE_R 600
#define DASH_FS_SMALL 0.66f
#define DASH_C_TEXT 1
typedef struct { int r, g, b, a; } DashColor;
typedef struct { int button; const char *key; const char *label; } DashHint;
static struct { DashColor bg2; } dashTheme;
#define DC(r, g, b, a) ((DashColor){r, g, b, a})
static float font_factor = 1.0f;
static float dashTextWidth(const char *s, float sc) { return strlen(s) * 10.0f * sc * font_factor; }
static const char *localizeDashboardText(const char *s) { return s; }
static float buttonWidth(int icon, const char *key) { return (icon >= 0 || !key) ? 22.0f : 50.0f; }
static float drawButton(float x, float y, int icon, const char *key) { (void)x; (void)y; return buttonWidth(icon, key); }
static float dashClampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float dashEaseOutCubic(float t) { return t; }
static float dashSectionAnim(void) { return 1.0f; }
static float footerClockWidth(char *a, int an, char *b, int bn) { (void)a; (void)an; (void)b; (void)bn; return 0; }
static void drawFooterClock(float y, const char *a, const char *b) { (void)y; (void)a; (void)b; }
static DashColor dashColorScale(DashColor c, float f) { (void)f; return c; }
static void dashRectGradH(float x, float y, float w, float h, DashColor a, DashColor b) { (void)x; (void)y; (void)w; (void)h; (void)a; (void)b; }
static struct { float x, w, sc; } fit[8];
static int nfit;
static float dashTextS(float x, float y, const char *s, float sc, int c) {
    (void)y; (void)c; fit[nfit].x = x; fit[nfit].w = dashTextWidth(s, sc); fit[nfit].sc = sc; nfit++;
    return x + dashTextWidth(s, sc);
}
''' + func + r'''
static int run(float factor, int n) {
    DashHint h[5] = {{1, 0, "Start"}, {2, 0, "Favorite"}, {3, 0, "Options"}, {-1, "START", "Menu"}, {-1, "R3", "Show background"}};
    int i;
    font_factor = factor; nfit = 0;
    dashDrawHints(h, n);
    assert(nfit == n);                       /* every label is drawn, whole, through dashTextS */
    for (i = 1; i < n; i++) assert(fit[i].x > fit[i - 1].x + fit[i - 1].w); /* no overlap */
    return fit[n - 1].x + fit[n - 1].w <= DASH_SAFE_R + 12.0f + 0.5f;       /* ends inside the safe edge */
}
int main(void) {
    assert(run(1.0f, 5) && fit[0].sc == DASH_FS_SMALL);   /* full size, nothing shrinks needlessly */
    assert(run(1.12f, 5) && fit[0].sc == DASH_FS_SMALL);  /* wider font: tighter gaps are enough */
    assert(run(1.2f, 5) && fit[0].sc == DASH_FS_SMALL);
    assert(run(1.0f, 3));
    assert(run(1.5f, 5) && fit[0].sc < DASH_FS_SMALL);    /* gaps are not enough: the type shrinks */
    assert(fit[0].sc >= DASH_FS_SMALL * 0.5f - 0.001f);
    assert(run(1.9f, 5));                                 /* still whole and inside the edge */
    puts("ok");
    return 0;
}
'''
d = Path(tempfile.mkdtemp())
(d / "t.c").write_text(code)
subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-o", str(d / "t"), str(d / "t.c")], check=True)
out = subprocess.run([str(d / "t")], capture_output=True, text=True, check=True)
assert out.stdout.strip() == "ok"
print("PASS: hint bar draws labels whole: gaps tighten, then type shrinks, never an ellipsis")
