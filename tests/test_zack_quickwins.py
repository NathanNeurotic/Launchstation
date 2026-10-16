"""Regression coverage for Zack's small GUI fixes (#137, #158, #159, #130).

Source contracts accompany a host-compiled orb overlap-weight test. Console
acceptance is still necessary for CRT visuals and theme-specific rendering.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def src(name):
    return (root / "src" / name).read_text(encoding="utf-8")

home = src("dash_home.c")
main = src("dash_main.c")
ui = src("dash_ui.c")
apps = src("dash_apps.c")
orbs = src("dash_bg_ps2.c")

# R3 is advertised before HOME is hidden and remains visible to restore it.
hint_block = home[home.index("static void homeHints(void)"):home.index("const DashScreen dashHomeScreen")]
assert '"R3", h[n].label = "Show background"' in hint_block
assert "dashDrawHints(h, n);" in hint_block
assert '{-1, "R3", "Show interface"}' in main
assert "!(paddata & PAD_SELECT)" in main

# The same glyph drawing path must serve modal actions and normal hints.
modal = ui[ui.index("static int modalButtons("):ui.index("int dashModalConfirm(")]
assert "drawButton(bx0 + 12, by + 6, dashOkIcon(), NULL);" in modal
assert "drawButton(bx1 + 12, by + 6, dashBackIcon(), NULL);" in modal
assert "dashIcon(dashOkIcon(), bx0" not in modal
assert "dashIcon(dashBackIcon(), bx1" not in modal

# Coverflow depth fades through existing alpha; no square tinted overlay.
carousel = apps[apps.index("static void drawCarousel("):apps.index("static void appsDraw(")]
assert "dashGfxPushAlpha(alpha);" in carousel
assert "dashRect(" not in carousel

# Compile the actual production smooth overlap helper (not a reimplementation).
def extract(signature):
    start = orbs.index(signature)
    brace = orbs.index("{", start) + 1
    depth, i = 1, brace
    while depth:
        depth += (orbs[i] == "{") - (orbs[i] == "}")
        i += 1
    return orbs[start:i] + "\n"

helper = extract("static float orbHeadWeight(")
body = orbs[orbs.index("static void osdOrbs(const OrbStyle *st, float t, int fx)\n{"):]
body = body[:body.index("dashGfxBlend(DASH_BLEND_NORMAL);")]
assert "orbHeadWeight(head_phi, k)" in body
assert "already_drawn" not in body
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
assert cc, "A host C compiler is required"
test = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define ORB_N 7
#define PI_F 3.14159265358979323846f
#define TAU_F 6.28318530717958647692f
#define ORB_MERGE_SPAN 0.16f
""" + helper + r"""
int main(void) {
    float p[ORB_N] = {0,0,1,2,3,4,5};
    float prev0 = 0, prev1 = 0;
    for (int i=0; i<=2000; i++) {
        p[1] = 0.25f * i / 2000.0f;
        float a = orbHeadWeight(p, 0), b = orbHeadWeight(p, 1);
        assert(isfinite(a) && isfinite(b));
        assert(a >= 0.49f && a <= 1.01f && b >= 0.49f && b <= 1.01f);
        if (i) {
            assert(fabsf(a - prev0) < 0.01f);
            assert(fabsf(b - prev1) < 0.01f);
        }
        prev0 = a; prev1 = b;
    }
    p[1] = 0;
    assert(fabsf(orbHeadWeight(p,0) - 0.5f) < 0.0001f);
    assert(fabsf(orbHeadWeight(p,1) - 0.5f) < 0.0001f);
    p[1] = 0.25f;
    assert(fabsf(orbHeadWeight(p,0) - 1) < 0.0001f);
    assert(fabsf(orbHeadWeight(p,1) - 1) < 0.0001f);
    p[1] = 0; p[2] = 0;
    for (int k=0;k<3;k++)
        assert(fabsf(orbHeadWeight(p,k) - 1.0f/3.0f) < 0.0001f);
    p[1] = 2*PI_F - 0.02f; p[2] = 1;
    assert(orbHeadWeight(p,0) < 1);
    puts("PASS: orb collisions fade continuously, including phase wrap");
}
"""
with tempfile.TemporaryDirectory(prefix="launchstation-zack-") as folder:
    source = Path(folder) / "orb-weights.c"
    binary = Path(folder) / "orb-weights"
    source.write_text(test, encoding="utf-8")
    try:
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(source), "-lm", "-o", str(binary)],
                       check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"Timed out running {exc.cmd}") from exc
print("PASS: R3 visibility, shared modal glyphs, Coverflow, collision continuity")
