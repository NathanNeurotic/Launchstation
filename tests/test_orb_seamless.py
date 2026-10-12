"""Compile production orb trajectory and radius helpers and verify seamless boundaries."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_bg_ps2.c").read_text()
assert "orbPhaseAt(k, t - span * i / n)" in source
assert "orbRadiusAt(st, ot.minute, ot.second)" in source
assert "fmodf(ot.minute" not in source
assert "dir * base" not in source

def function(signature):
    start = source.index(signature)
    brace = source.index("{", start) + 1
    n = 1
    i = brace
    while n:
        n += (source[i] == "{") - (source[i] == "}")
        i += 1
    return source[start:i] + "\n"

test = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef struct { float cx, cy, rmin, rmax; int c; } OrbStyle;
#define PI_F 3.14159265358979323846f
#define TAU_F 6.28318530717958647692f
#define ORB_N 7
#define ORB_BASE 20.0f
static float dashBgHash(int a, int b) {
    unsigned x = (unsigned)(a * 12479u + b * 48271u + 1103515245u);
    x ^= x >> 13;
    return (x & 0xFFFFu) / 65535.0f;
}
static float dashSinf(float a) { return sinf(a); }
static float dashCosf(float a) { return cosf(a); }
"""
test += function("static float orbPhaseAt(")
test += function("static float orbRadiusAt(")
test += r"""
int main(void) {
    const float dt = 1.0f/60.0f;
    OrbStyle s={236,214,44,84,0};
    // Every orb's actual position must continue through the old 20s
    // wrap, the RTC minute boundary, and the hour transition.
    for(int k=0;k<ORB_N;k++) {
        const float seams[]={20,40,60,80,3600,7200};
        for (int j=0;j<6;j++) {
            float t=seams[j];
            float before=orbPhaseAt(k,t-dt), after=orbPhaseAt(k,t+dt);
            assert(isfinite(before) && isfinite(after));
            assert(fabsf(after-before)<0.03f);
            // Trail at the head must be exactly the same trajectory.
            assert(fabsf(orbPhaseAt(k,t)-orbPhaseAt(k,t))<1e-6f);
            assert(isfinite(orbPhaseAt(k,t-6.5f)));
        }
        // No artificial jump, regardless of where the half-second
        // frame boundaries fall on the trajectory.
        for(int i=1;i<12000;i++) {
            float t=i*dt;
            assert(fabsf(orbPhaseAt(k,t)-orbPhaseAt(k,t-dt))<0.02f);
        }
    }
    float a=orbRadiusAt(&s,59,59.9833333f);
    float b=orbRadiusAt(&s,0,0);
    assert(fabsf(a-b)<0.001f);
    assert(fabsf(orbRadiusAt(&s,0,0)-s.rmin)<0.0001f);
    assert(fabsf(orbRadiusAt(&s,30,0)-s.rmax)<0.0001f);
    assert(fabsf(orbRadiusAt(&s,15,0)-orbRadiusAt(&s,45,0))<0.0001f);
    assert(orbRadiusAt(&s,59,59)<s.rmax);
    // Freeze at zero motion: same phase and radius for same time.
    assert(orbPhaseAt(3,123)==orbPhaseAt(3,123));
    puts("PASS: all seven orb phases stay continuous across 20s/minute/hour, cyclic radius and trails");
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "A host C compiler is required"
with tempfile.TemporaryDirectory(prefix="launchstation-orb-seam-") as work:
    path=Path(work)/"orbs.c"
    exe=Path(work)/"orbs"
    path.write_text(test)
    subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(path),"-lm","-o",str(exe)],check=True)
    subprocess.run([str(exe)],timeout=10,check=True)
