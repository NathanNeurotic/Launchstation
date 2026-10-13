"""Regression: scrolling text must restore the enclosing GS scissor rectangle.

The test host-compiles the actual production clipping helpers with mocked GS
registers. This prevents text marquees from widening a clipped popup or HDD list.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
gfx = (root / "src/dash_gfx.c").read_text(encoding="utf-8")
font = (root / "src/dash_font.c").read_text(encoding="utf-8")
header = (root / "include/dash_internal.h").read_text(encoding="utf-8")

start = gfx.index("/* Clip state is tracked because scrolling text")
stop = gfx.index("// Primitives", start)
clip = gfx[start:stop]

assert "dashGfxScissorPush(" in header
assert "dashGfxScissorPop(" in header
assert "dashGfxScissorPush(x, y - 2.0f, maxw" in font
assert "dashGfxScissorPop();" in font
scroll = font[font.index("void dashTextScrollTimed("):font.index("void dashTextScroll(", font.index("void dashTextScrollTimed("))]
assert "dashGfxScissorReset();" not in scroll
assert "dashGfxScissorReset();" in gfx[gfx.index("void dashGfxBeginFrame("):gfx.index("void dashGfxPresent(")]

source = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 448
#define SX(v) (v)
#define SY(v) (v)
#define GS_SETREG_SCISSOR(x0,x1,y0,y1) \
    ((u64)(x0) | ((u64)(x1)<<10) | ((u64)(y0)<<20) | ((u64)(y1)<<30))
static void *gsGlobal = (void *)1;
static u64 scissor_reg;
static int flushes;
static void gsKit_set_scissor(void *global, u64 reg) {
    assert(global == gsGlobal);
    scissor_reg = reg;
}
static void dashSpriteBatchEnd(void) { flushes++; }
"""
source += clip
source += r"""
static void expect(int x0,int x1,int y0,int y1) {
    assert((int)(scissor_reg & 1023) == x0);
    assert((int)((scissor_reg>>10) & 1023) == x1);
    assert((int)((scissor_reg>>20) & 1023) == y0);
    assert((int)((scissor_reg>>30) & 1023) == y1);
}
int main(void) {
    dashGfxScissorReset();
    expect(0,639,0,447);
    dashGfxScissor(10,20,200,100);
    expect(10,209,20,119);

    /* A marquee spanning outside the panel must intersect the parent. */
    dashGfxScissorPush(5,30,60,200);
    expect(10,64,30,119);
    dashGfxScissorPush(15,10,50,50);
    expect(15,64,30,59);
    dashGfxScissorPop();
    expect(10,64,30,119);
    dashGfxScissorPop();
    expect(10,209,20,119);

    /* A completely off-panel marquee can never widen the parent. */
    dashGfxScissorPush(500,500,30,30);
    expect(209,209,119,119);
    dashGfxScissorPop();
    expect(10,209,20,119);

    /* Stack overflow must not consume parent state or unbalance pops. */
    for(int i=0;i<10;i++) dashGfxScissorPush(0,0,640,448);
    assert(clip_depth==8 && clip_overflow==2);
    for(int i=0;i<10;i++) dashGfxScissorPop();
    assert(clip_depth==0 && clip_overflow==0);
    expect(10,209,20,119);

    /* Reset at the next frame discards any interrupted nested clipping. */
    dashGfxScissorPush(0,0,50,50);
    dashGfxScissorReset();
    assert(clip_depth==0 && clip_overflow==0);
    expect(0,639,0,447);
    assert(flushes>=10);
    puts("PASS: scrolling marquee intersections, balanced restores, frame reset");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="launchstation-scissor-") as tmp:
    src = Path(tmp) / "scissor.c"
    exe = Path(tmp) / "scissor"
    src.write_text(source, encoding="utf-8")
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
