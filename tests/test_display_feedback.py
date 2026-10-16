"""Exercise physical text alignment, 576p geometry and texture cache identity."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
gfx = (root / "src/dash_gfx.c").read_text()
gs = (root / "src/draw_gs.c").read_text()
art = (root / "src/dash_art.c").read_text()


def function(source, signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


harness = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GS_INTERLACED 1
#define GS_NONINTERLACED 0
#define GS_FIELD 0
#define GS_FRAME 1
#define GS_MODE_NTSC 2
#define GS_MODE_PAL 3
#define GS_MODE_VGA_640_60 4
#define GS_MODE_DTV_480P 5
#define GS_MODE_DTV_576P 0x53
#define FONT_HEIGHT 16
#define LINE_THICKNESS 2
static struct { int Mode, Interlace, Field, Width, Height; } g, *gsGlobal = &g;
static int SCREEN_WIDTH, SCREEN_HEIGHT, Menu_end_y, Menu_start_y = 20;
static int Frame_end_y, Menu_tooltip_y;
static float g_sy = 1.0f, g_oy;
typedef unsigned int u32;
typedef struct { int unused; } GSTEXTURE;
#define DASH_PATH_MAX 512
#define THEME_SLOTS 8
#define DASH_JOB_THEME 1
#define DASH_JOB_VISIBLE 2
enum { TS_EMPTY, TS_LOADED, TS_MISSING, TS_LOADING };
typedef struct {
    char path[DASH_PATH_MAX]; GSTEXTURE tex;
    int max_w, max_h, state; unsigned short gen;
} ThemeSlot;
typedef struct {
    int slot; unsigned short gen; char path[DASH_PATH_MAX];
    int max_w, max_h; u32 *pixels; int w, h, ok;
} ThemeTexJob;
static ThemeSlot theme_slots[THEME_SLOTS];
static int jobs, inactive, loading;
static void dashStrCopy(char *out, const char *in, int size) { snprintf(out, size, "%s", in); }
static int dashLoadingActive(void) { return loading; }
static int isPathDeviceStackLoaded(const char *path) { (void)path; return !inactive; }
static void themeTexRun(void *arg) { (void)arg; }
static void themeTexDone(void *arg) { (void)arg; }
static int dashWorkerSubmit(const char *name, int flags, void (*run)(void *), void (*done)(void *), void *arg) {
    (void)name; (void)flags; (void)run; (void)done;
    ++jobs; free(arg); return 0;
}
'''
harness += function(gfx, "float dashGfxTextY(")
harness += function(gs, "static void applyGSParams(")
harness += function(art, "GSTEXTURE *dashThemeTextureFile(")
harness += r'''
int main(void) {
    int scale, offset, step;
    g.Interlace = GS_INTERLACED; g.Field = GS_FIELD;
    for (scale = 0; scale < 2; ++scale) {
        g_sy = scale ? 512.0f / 448.0f : 1.0f;
        for (offset = -16; offset <= 16; ++offset) {
            g_oy = offset * 0.17f;
            for (step = -30; step <= 300; ++step) {
                float y = step * 0.13f;
                float physical = (dashGfxTextY(y) + g_oy) * g_sy;
                assert(fabsf(physical / 2 - roundf(physical / 2)) < 0.00001f);
                assert(fabsf(physical - (y + g_oy) * g_sy) <= 1.00001f);
            }
        }
    }
    g.Interlace = GS_NONINTERLACED;
    assert(dashGfxTextY(13.37f) == 13.37f);
    g.Interlace = GS_INTERLACED; g.Field = GS_FRAME;
    assert(dashGfxTextY(13.37f) == 13.37f);
    g.Mode = GS_MODE_DTV_576P; applyGSParams();
    assert(g.Width == 640 && g.Height == 512);
    assert(g.Interlace == GS_NONINTERLACED && g.Field == GS_FRAME);
    g.Mode = GS_MODE_PAL; applyGSParams();
    assert(g.Width == 640 && g.Height == 512 && g.Interlace == GS_INTERLACED);
    g.Mode = GS_MODE_NTSC; applyGSParams();
    assert(g.Width == 640 && g.Height == 448 && g.Field == GS_FIELD);
    g.Mode = GS_MODE_DTV_480P; applyGSParams();
    assert(g.Width == 640 && g.Height == 448 && g.Interlace == GS_NONINTERLACED);
    /* One image used as both app icon and wallpaper must not reuse its thumbnail. */
    assert(!dashThemeTextureFile("usb0:/same.png", 128, 128) && jobs == 1);
    theme_slots[0].state = TS_LOADED;
    assert(dashThemeTextureFile("usb0:/same.png", 128, 128) == &theme_slots[0].tex);
    assert(!dashThemeTextureFile("usb0:/same.png", 640, 512) && jobs == 2);
    theme_slots[1].state = TS_LOADED;
    assert(dashThemeTextureFile("usb0:/same.png", 640, 512) == &theme_slots[1].tex);
    inactive = 1;
    assert(!dashThemeTextureFile("usb1:/missing.png", 640, 512) && jobs == 2);
    inactive = 0; loading = 1;
    assert(!dashThemeTextureFile("usb1:/missing.png", 640, 512) && jobs == 2);
    puts("PASS: NTSC/PAL physical field alignment, progressive animation, 576p geometry, resolution-aware wallpaper cache");
    return 0;
}
'''
cc = shutil.which("gcc") or shutil.which("clang")
if not cc:
    raise SystemExit("Host C compiler required")
build = root / "build"
build.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="display-", dir=build) as tmp:
    source, exe = Path(tmp) / "h.c", Path(tmp) / "h.exe"
    source.write_text(harness)
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(source), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

font = (root / "src/dash_font.c").read_text()
assert "top = dashGfxTextY(y + sel.yshift);" in function(font, "static float drawRun(")
assert "TV_mode_480P,\n\tTV_mode_576P," in (root / "include/launchelf.h").read_text()
assert gs.count("case TV_mode_576P:") == 2  # initial setup and live changes
assert '"VGA", "480p", "576p"' in (root / "src/dash_settings.c").read_text()
