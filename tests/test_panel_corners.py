"""All four filled corners of a rounded panel have the same shape.

The GS samples a texture at each pixel's top-left. A quarter-disc texture
drawn mirrored (reversed UVs) therefore sampled the right and bottom corners
one pixel further inside the curve than the left and top ones. This compiles
the real buildCornerTextures() and panelCorners() from src/dash_gfx.c, renders
each corner with the GS sampling rule (bilinear, sample point at the pixel's
top-left, gsKit's UV clamp) and checks the four corners are mirror images of
each other and follow the circle the border uses.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
assert "python3 tests/test_panel_corners.py" in (root / ".github/workflows/compile.yml").read_text()
gfx = (root / "src/dash_gfx.c").read_text()
tex = gfx[gfx.index("/* Full disc with a margin"):gfx.index("static void buildGlowTextures(void)")]
corners = gfx[gfx.index("static void panelCorners(GSTEXTURE *tex"):gfx.index("void dashPanelGradV(")]
assert "panelCorners(&tex_corner, x, y, w, h, r," in gfx

code = r'''
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned long long u64;
typedef struct { int Width, Height; u32 *Mem; } GSTEXTURE;
typedef struct { unsigned char r, g, b, a; } DashColor;
static GSTEXTURE tex_corner;
static float dashClampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static u32 whiteA(float a) { int ia = (int)(a * 128.0f + 0.5f); return 0x00808080 | ((u32)ia << 24); }
static u32 *allocTexMem(int w, int h) { return calloc((size_t)w * h, 4); }
static void setupTex(GSTEXTURE *t, int w, int h, u32 *m) { t->Width = w; t->Height = h; t->Mem = m; }
static int dashTexBind(GSTEXTURE *t) { (void)t; return 1; }
static u64 gsColor(DashColor c) { (void)c; return 0; }
#define R 12
static float out[4][R][R]; static int quads;
static float texel(const GSTEXTURE *t, int x, int y) {
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x >= t->Width) x = t->Width - 1; if (y >= t->Height) y = t->Height - 1;
    return (float)(t->Mem[y * t->Width + x] >> 24) / 128.0f;
}
static float bilinear(const GSTEXTURE *t, float u, float v) {
    float fx = u - 0.5f, fy = v - 0.5f; int x = (int)floorf(fx), y = (int)floorf(fy);
    float ax = fx - x, ay = fy - y;
    return (texel(t, x, y) * (1 - ax) + texel(t, x + 1, y) * ax) * (1 - ay) +
           (texel(t, x, y + 1) * (1 - ax) + texel(t, x + 1, y + 1) * ax) * ay;
}
static float clampuv(float uv, int max) { return uv < 0 ? 0 : uv > max ? max : uv; }  /* gsKit */
/* GS sprite: pixel (i, j) covers sample point (x1 + i, y1 + j) and reads
 * the UV interpolated there; no half-pixel bias. */
static void texQuad(GSTEXTURE *t, float x1, float y1, float x2, float y2,
                    float u1, float v1, float u2, float v2, u64 c) {
    int i, j; (void)c;
    u1 = clampuv(u1, t->Width); u2 = clampuv(u2, t->Width);
    v1 = clampuv(v1, t->Height); v2 = clampuv(v2, t->Height);
    assert(x2 - x1 == R && y2 - y1 == R);
    for (j = 0; j < R; j++)
        for (i = 0; i < R; i++)
            out[quads][j][i] = bilinear(t, u1 + i * (u2 - u1) / R, v1 + j * (v2 - v1) / R);
    quads++;
}
''' + tex + corners + r'''
int main(void) {
    int i, j;
    buildCornerTextures();
    panelCorners(&tex_corner, 0, 0, 100, 60, R, (DashColor){0}, (DashColor){0});
    assert(quads == 4);
    for (j = 0; j < R; j++)
        for (i = 0; i < R; i++) {
            float tl = out[0][j][i];
            /* mirrored positions in the other three corners */
            assert(fabsf(tl - out[1][j][R - 1 - i]) < 0.02f);
            assert(fabsf(tl - out[2][R - 1 - j][i]) < 0.02f);
            assert(fabsf(tl - out[3][R - 1 - j][R - 1 - i]) < 0.02f);
            /* follows the circle around the inner corner, by pixel centre */
            float dx = R - (i + 0.5f), dy = R - (j + 0.5f), d = sqrtf(dx * dx + dy * dy);
            if (d < R - 1.0f) assert(tl > 0.95f);
            if (d > R + 1.0f) assert(tl < 0.05f);
        }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="corners-") as tmp:
    src, exe = Path(tmp) / "t.c", Path(tmp) / "t"
    src.write_text(code)
    cc = shutil.which("gcc") or shutil.which("cc")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                    str(src), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: the four filled corners are mirror images and follow the circle")
