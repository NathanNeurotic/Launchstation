"""Footer hints keep one button order on every screen: Cross, Square, Triangle, Circle,
R1, R2, L1, L2, SELECT, START, sticks (C2 of the 2026-11 feedback round, Berion), whatever
order and subset a screen passes to dashDrawHints."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")
hdr = (root / "include/dash_internal.h").read_text(encoding="utf-8")
assert "#define DASH_HINTS_MAX 8" in hdr
a = ui.index("static int hintRank(")
b = ui.index("void dashDrawHints(")
func = ui[a:ui.index("\n}\n", b) + 3]
assert "hintOrder(in_hints, n, order);" in func

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DASH_SAFE_L 40
#define DASH_SAFE_R 600
#define DASH_FS_SMALL 0.66f
#define DASH_C_TEXT 1
#define DASH_HINTS_MAX 8
enum { DI_BTN_CROSS = 1, DI_BTN_CIRCLE, DI_BTN_SQUARE, DI_BTN_TRIANGLE, DI_BTN_UP, DI_BTN_DOWN, DI_BTN_LEFT, DI_BTN_RIGHT,
       DI_BTN_START, DI_BTN_SELECT };
typedef struct { int r, g, b, a; } DashColor;
typedef struct { int button; const char *key; const char *label; } DashHint;
static struct { DashColor bg2; } dashTheme;
#define DC(r, g, b, a) ((DashColor){r, g, b, a})
static float dashTextWidth(const char *s, float sc) { return strlen(s) * 10.0f * sc; }
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
static const char *drawn[8];
static int ndrawn;
static float dashTextS(float x, float y, const char *s, float sc, int c) { (void)y; (void)c; drawn[ndrawn++] = s; return x + dashTextWidth(s, sc); }
''' + func + r'''
static void check(const DashHint *h, int n, const char *want) {
    char got[256] = "";
    int i;
    ndrawn = 0;
    dashDrawHints(h, n);
    assert(ndrawn == n);
    for (i = 0; i < n; i++) { strcat(got, drawn[i]); strcat(got, i + 1 < n ? "," : ""); }
    if (strcmp(got, want)) { printf("got  %s\nwant %s\n", got, want); assert(0); }
}
int main(void) {
    /* Settings (US region): the screen lists Back before Triangle and START; drawn in the fixed order */
    DashHint settings[] = {{DI_BTN_CROSS, 0, "Change"}, {DI_BTN_SQUARE, 0, "Reset"}, {DI_BTN_CIRCLE, 0, "Back"},
                           {DI_BTN_TRIANGLE, 0, "Changes"}, {-1, "START", "Menu"}};
    check(settings, 5, "Change,Reset,Changes,Back,Menu");
    /* Japan region: Circle confirms and Cross goes back; the physical order still holds */
    DashHint settings_jp[] = {{DI_BTN_CIRCLE, 0, "Change"}, {DI_BTN_SQUARE, 0, "Reset"}, {DI_BTN_CROSS, 0, "Back"},
                              {DI_BTN_TRIANGLE, 0, "Changes"}, {-1, "START", "Menu"}};
    check(settings_jp, 5, "Back,Reset,Changes,Change,Menu");
    /* HDD manager: shoulder keys R before L, 1 before 2; Exit (Circle) before them */
    DashHint hdd[] = {{DI_BTN_CROSS, 0, "Actions"}, {-1, "L2/R2", "Page"}, {-1, "L1", "Status"}, {-1, "R1", "Game info"},
                      {DI_BTN_CIRCLE, 0, "Exit"}};
    check(hdd, 5, "Actions,Exit,Game info,Page,Status");
    /* Editor keyboard: the screen's own order is Type, Delete, Caps, Move, Hide keys, Options */
    DashHint kb[] = {{DI_BTN_CROSS, 0, "Type"}, {DI_BTN_CIRCLE, 0, "Delete"}, {DI_BTN_SQUARE, 0, "Caps"},
                     {-1, "L1/R1", "Move"}, {-1, "SELECT", "Hide keys"}, {DI_BTN_TRIANGLE, 0, "Options"}};
    check(kb, 6, "Type,Caps,Options,Delete,Move,Hide keys");
    /* Home: START then R3; d-pad hints (Change) keep their given place after the buttons */
    DashHint home[] = {{DI_BTN_CROSS, 0, "Start"}, {DI_BTN_SQUARE, 0, "Favorite"}, {DI_BTN_TRIANGLE, 0, "Options"},
                       {-1, "START", "Menu"}, {-1, "R3", "Show background"}};
    check(home, 5, "Start,Favorite,Options,Menu,Show background");
    DashHint num[] = {{DI_BTN_LEFT, 0, "Change"}, {DI_BTN_CIRCLE, 0, "Home"}, {DI_BTN_TRIANGLE, 0, "Changes"}, {-1, "START", "Menu"}};
    check(num, 4, "Changes,Home,Menu,Change");
    /* Equal ranks stay in the order given (stable) */
    DashHint same[] = {{-1, "UNKNOWN", "A"}, {-1, "OTHER", "B"}, {DI_BTN_UP, 0, "C"}};
    check(same, 3, "A,B,C");
    puts("ok");
    return 0;
}
'''
d = Path(tempfile.mkdtemp())
(d / "t.c").write_text(code)
subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-o", str(d / "t"), str(d / "t.c")], check=True)
out = subprocess.run([str(d / "t")], capture_output=True, text=True, check=True)
assert out.stdout.strip() == "ok", out.stdout
print("PASS: footer hints draw in one fixed button order on every screen")
