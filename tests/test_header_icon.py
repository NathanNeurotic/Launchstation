"""Exercise rotating header and ELF/application icon phase locking."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
icons = (root / "src/dash_icons.c").read_text()
header = (root / "include/dash_internal.h").read_text()


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


assert "Timer(" not in function(icons, "void dashHeaderIcon(")

code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u8 r,g,b,a; } DashColor;
typedef struct { int unused; } GSTEXTURE;
static GSTEXTURE icon_tex;
static int icons_ready=1, draws, tints;
static u64 last_frame_ms;
static DashColor dashIconTint(DashColor c) { tints++; c.g=17; return c; }
static struct { int icon_backing; } dashCfg;
static DashColor DC(int r,int g,int b,int a) { return (DashColor){r,g,b,a}; }
static void dashRoundedBorder(float x,float y,float w,float h,float r,DashColor c) {
    (void)x;(void)y;(void)w;(void)h;(void)r;(void)c;
}
static void dashSpriteBatchBegin(GSTEXTURE *tex,DashColor c) { (void)tex;(void)c;assert(!"Unexpected static icon"); }
static void dashSpriteBatchEnd(void) {}
static void dashSpriteBatchAdd(float x,float y,float u,float v,float x2,float y2,float u2,float v2) {
    (void)x;(void)y;(void)u;(void)v;(void)x2;(void)y2;(void)u2;(void)v2;
}
static void dashSpriteBatchAddAligned(float x,float y,float u,float v,float x2,float y2,float u2,float v2) {
    dashSpriteBatchAdd(x,y,u,v,x2,y2,u2,v2);
}
static struct { float x[4],y[4],u[4],v[4]; DashColor c; } quads[2];
static void dashTexDrawQuadUV(GSTEXTURE *tex,
    float x1,float y1,float u1,float v1, float x2,float y2,float u2,float v2,
    float x3,float y3,float u3,float v3, float x4,float y4,float u4,float v4, DashColor c) {
    assert(tex==&icon_tex && draws<2);
    quads[draws].x[0]=x1; quads[draws].y[0]=y1; quads[draws].u[0]=u1; quads[draws].v[0]=v1;
    quads[draws].x[1]=x2; quads[draws].y[1]=y2; quads[draws].u[1]=u2; quads[draws].v[1]=v2;
    quads[draws].x[2]=x3; quads[draws].y[2]=y3; quads[draws].u[2]=u3; quads[draws].v[2]=v3;
    quads[draws].x[3]=x4; quads[draws].y[3]=y4; quads[draws].u[3]=u4; quads[draws].v[3]=v4;
    quads[draws].c=c;
    draws++;
}
'''
for macro in ("ICON_SIZE", "ICON_SLOT", "ICON_COLS", "ICON_TEX_W", "ICON_TEX_H",
              "APP_PART_SIZE", "APP_PART_SLOT", "APP_PART_X", "CLIP_ICON_Y",
              "GRID_BOTTOM", "MARGIN_COL_N", "MARGIN_ROW_N"):
    code += re.search(r"^#define " + macro + r" .+$", icons, re.M).group() + "\n"
start = header.index("enum DashIcon {")
code += header[start:header.index("};", start) + 2] + "\n"
code += function(icons, "static void marginSlot(")
code += function((root / "src/dash_main.c").read_text(), "u64 dashFrameTimeMs(")
for signature in ("static void drawHeaderIconPart(", "void dashHeaderIcon(", "void dashIcon("):
    code += function(icons, signature)
code += r'''
static void check_at(u64 ms,float degrees)
{
    const float cx=123.5f, cy=45.25f, size=26.0f;
    const float span=size*APP_PART_SLOT/APP_PART_SIZE;
    const float radians=degrees*3.14159265358979323846f/180;
    const float cs=cosf(radians), sn=sinf(radians);
    const DashColor color={123,234,210,211};
    last_frame_ms=ms; draws=tints=0;
    dashHeaderIcon(cx,cy,size,color);
    assert(draws==2 && tints==1);
    for(int part=0;part<2;part++) {
        float sine=part==0?-sn:sn;
        float vx=quads[part].x[1]-quads[part].x[0];
        float vy=quads[part].y[1]-quads[part].y[0];
        assert(fabsf(vx/span-cs)<0.00001f && fabsf(vy/span-sine)<0.00001f);
        assert(fabsf(quads[part].x[2]-quads[part].x[0]+vy)<0.0001f);
        assert(fabsf(quads[part].y[2]-quads[part].y[0]-vx)<0.0001f);
        assert(quads[part].u[0]==APP_PART_X && quads[part].u[1]==APP_PART_X+APP_PART_SLOT);
        assert(quads[part].v[0]==part*APP_PART_SLOT && quads[part].v[2]==(part+1)*APP_PART_SLOT);
    }
    const int ids[]={DI_APP,DI_EXEC};
    const float sizes[]={14,18,26,48,96};
    for(int id=0;id<2;id++) for(int i=0;i<5;i++) {
        float sz=sizes[i], ispan=sz*APP_PART_SLOT/APP_PART_SIZE;
        draws=tints=0;
        dashIcon(ids[id],cx-sz/2,cy-sz/2,sz,color);
        assert(draws==2 && tints==1);
        for(int part=0;part<2;part++) {
            float vx=quads[part].x[1]-quads[part].x[0];
            float vy=quads[part].y[1]-quads[part].y[0];
            assert(fabsf(vx/ispan-cs)<0.00001f);
            assert(fabsf(vy/ispan-(part==0?-sn:sn))<0.00001f);
            float sx=0,sy=0;
            for(int k=0;k<4;k++) { sx+=quads[part].x[k]; sy+=quads[part].y[k]; }
            assert(fabsf(sx/4-cx)<0.0001f && fabsf(sy/4-cy)<0.0001f);
            assert(quads[part].c.r==color.r && quads[part].c.g==17 && quads[part].c.a==color.a);
        }
    }
}
int main(void)
{
    check_at(0, 0);
    check_at(7500, 90);
    check_at(15000, 180);
    check_at(29999, 360.0f * 29999 / 30000);
    check_at(30000, 0);
    check_at(3600000000ULL + 7500, 90);
    draws=tints=0;
    dashHeaderIcon(0,0,0,(DashColor){255,255,255,255});
    dashHeaderIcon(0,0,26,(DashColor){255,255,255,0});
    assert(!draws && !tints);
    puts("PASS: header and ELF/application icons share opposite 30-second turns and frame-locked rotation across five sizes");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="header-icon-") as folder:
    c = Path(folder) / "check.c"
    executable = Path(folder) / "check"
    c.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(executable), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)
