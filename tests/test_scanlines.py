"""CRT scanlines cover the whole screen in every video mode.

gsKit clamps each UV to the texture size, so the overlay must never ask a
strip for more rows than the texture has. Compiles the real drawScanlines()
from src/dash_bg.c with stubs and checks the strips tile every framebuffer
line from top to bottom with one texel row per line.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
assert "python3 tests/test_scanlines.py" in (root / ".github/workflows/compile.yml").read_text()
bg = (root / "src/dash_bg.c").read_text()
start = bg.index("#define SCANLINE_ROWS")
end = bg.index("//--------------------------------------------------------------\n// Entry points")
code = r'''
#include <assert.h>
#include <math.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned char u8;
typedef struct { int Width, Height, PSM, Filter, Delayed; void *Mem; } GSTEXTURE;
typedef struct { u8 r, g, b, a; } DashColor;
#define DC(r, g, b, a) ((DashColor){r, g, b, a})
#define GS_PSM_CT32 0
#define GS_FILTER_NEAREST 0
#define DASH_VW 640
#define DASH_VH 448
static void gsKit_setup_tbw(GSTEXTURE *t) { (void)t; }
static void SyncDCache(void *a, void *b) { (void)a; (void)b; }
static float scale;
static float dashGfxScaleY(void) { return scale; }
static float covered; static int strips;
static void dashTexDrawUV(GSTEXTURE *t, float x, float y, float w, float h,
                          float u1, float v1, float u2, float v2, DashColor c) {
    (void)x; (void)w; (void)u1; (void)u2; (void)c;
    assert(v1 == 0 && v2 > 0 && v2 <= t->Height);     /* inside the texture: no gsKit clamp */
    assert(fabsf(y * scale - covered) < 0.01f);       /* strips abut */
    assert(fabsf(h * scale - v2) < 0.01f);            /* one texel row per line */
    covered += v2; strips++;
}
''' + bg[start:end] + r'''
int main(void) {
    const int heights[] = {448, 480, 512, 576, 720, 1080, 224, 256};
    for (unsigned i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
        scale = heights[i] / (float)DASH_VH; covered = 0; strips = 0;
        drawScanlines();
        assert(fabsf(covered - heights[i]) < 0.01f);
        assert(strips <= heights[i] / SCANLINE_ROWS + 1);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="scanlines-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: scanlines tile every line of the screen in 8 video heights")
