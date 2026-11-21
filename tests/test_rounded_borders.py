"""Sample production selection primitives for continuous, uniformly tinted borders.

This host raster model checks geometry and alpha composition, not GS scanout.
Optional source paths test an older dash_gfx.c and dash_ui.c respectively.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
gfx = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else (root / "src/dash_gfx.c").read_text()
ui = Path(sys.argv[2]).read_text() if len(sys.argv) > 2 else (root / "src/dash_ui.c").read_text()
theme = (root / "src/dash_themes.c").read_text()
header = (root / "include/dash_internal.h").read_text()


def function(source, signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u8 r,g,b,a; } DashColor;
typedef struct { int Width,Height; u32 *Mem; } GSTEXTURE;
static GSTEXTURE tex_corner,tex_ring;
static float g_alpha=1,g_sy=1,g_ox,g_oy;
static struct { int corner,highlight; } dashTheme;
enum { DASH_CORNER_SQUARE,DASH_CORNER_SOFT,DASH_CORNER_ROUND };
enum { DASH_HL_FILL,DASH_HL_BAR,DASH_HL_OUTLINE,DASH_HL_TEXT };
static DashColor DASH_C_SELECT={23,61,117,111},DASH_C_SELEDGE={149,227,93,181};
static DashColor DASH_C_ACCENT={219,83,167,213};
static DashColor DASH_C_TEXT3={112,122,150,255};
static float dashClampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
static u32 *allocTexMem(int w,int h) { return calloc(w*h,sizeof(u32)); }
static void setupTex(GSTEXTURE *t,int w,int h,u32 *mem) { t->Width=w;t->Height=h;t->Mem=mem; }
static u64 gsColor(DashColor c) { return (u64)c.r|((u64)c.g<<8)|((u64)c.b<<16)|((u64)c.a<<24); }
static int dashTexBind(GSTEXTURE *t) { return t->Mem!=NULL; }
static void dashSpriteBatchEnd(void) {}
typedef struct {
    double x[4],y[4];
    float u1,v1,u2,v2;
    DashColor c;
    GSTEXTURE *tex;
} Primitive;
static inline float SX(float x);
static inline float SY(float y);
static Primitive primitives[256];
static int count;
static void dashQuadG(float x1,float y1,float x2,float y2,float x3,float y3,float x4,float y4,
                     DashColor c1,DashColor c2,DashColor c3,DashColor c4) {
    assert(count<256 && !memcmp(&c1,&c2,sizeof(c1)) && !memcmp(&c1,&c3,sizeof(c1)) && !memcmp(&c1,&c4,sizeof(c1)));
    Primitive *p=&primitives[count++];memset(p,0,sizeof(*p));p->c=c1;
    /* Convert the renderer's triangle-strip order to perimeter order. */
    p->x[0]=x1;p->y[0]=y1;p->x[1]=x2;p->y[1]=y2;
    p->x[2]=x4;p->y[2]=y4;p->x[3]=x3;p->y[3]=y3;
    /* Model actual GS coordinates, including PAL scaling and 12.4 vertices. */
    for(int i=0;i<4;i++) {
        p->x[i]=floorf(SX(p->x[i])*16)/16;
        p->y[i]=floorf(SY(p->y[i])*16)/16;
    }
}
static void dashRect(float x,float y,float w,float h,DashColor c) {
    if(w<=0 || h<=0 || !c.a || g_alpha<=0)return;
    dashQuadG(x,y,x+w,y,x,y+h,x+w,y+h,c,c,c,c);
}
static void dashRectGradV(float x,float y,float w,float h,DashColor top,DashColor bottom) {
    assert(!memcmp(&top,&bottom,sizeof(top)));dashRect(x,y,w,h,top);
}
static void texQuad(GSTEXTURE *t,float x1,float y1,float x2,float y2,
                    float u1,float v1,float u2,float v2,u64 tint) {
    DashColor c={tint&255,(tint>>8)&255,(tint>>16)&255,(tint>>24)&255};
    dashRect(x1,y1,x2-x1,y2-y1,c);
    Primitive *p=&primitives[count-1];p->tex=t;p->u1=u1;p->v1=v1;p->u2=u2;p->v2=v2;
}
static int contains(const Primitive *p,float x,float y) {
    int inside=0;
    for(int i=0,j=3;i<4;j=i++)
        if((p->y[i]>y)!=(p->y[j]>y) && x<(p->x[j]-p->x[i])*(y-p->y[i])/(p->y[j]-p->y[i])+p->x[i])
            inside=!inside;
    return inside;
}
static float texAlpha(const GSTEXTURE *t,int x,int y) {
    if(x<0)x=0;
    if(y<0)y=0;
    if(x>=t->Width)x=t->Width-1;
    if(y>=t->Height)y=t->Height-1;
    return (t->Mem[y*t->Width+x]>>24)/128.0f;
}
static void sample(float x,float y,float rgb[3]) {
    rgb[0]=11;rgb[1]=17;rgb[2]=23;
    for(int i=0;i<count;i++) {
        Primitive *p=&primitives[i];
        if(!contains(p,x,y))continue;
        float alpha=p->c.a/255.0f*g_alpha;
        if(p->tex) {
            float u=p->u1+(p->u2-p->u1)*(x-p->x[0])/(p->x[1]-p->x[0])-0.5f;
            float v=p->v1+(p->v2-p->v1)*(y-p->y[0])/(p->y[3]-p->y[0])-0.5f;
            int tx=(int)floorf(u),ty=(int)floorf(v);float fx=u-tx,fy=v-ty;
            alpha*=texAlpha(p->tex,tx,ty)*(1-fx)*(1-fy)+texAlpha(p->tex,tx+1,ty)*fx*(1-fy)
                  +texAlpha(p->tex,tx,ty+1)*(1-fx)*fy+texAlpha(p->tex,tx+1,ty+1)*fx*fy;
        }
        rgb[0]=p->c.r*alpha+rgb[0]*(1-alpha);
        rgb[1]=p->c.g*alpha+rgb[1]*(1-alpha);
        rgb[2]=p->c.b*alpha+rgb[2]*(1-alpha);
    }
}
'''
code += function(header, "static inline DashColor dashColorA(")
code += function(gfx, "DashColor dashColorLerp(")
code += function(gfx, "static inline u32 whiteA(")
for line in gfx.splitlines():
    if line.startswith("#define CORNER_"):
        code += line + "\n"
code += function(gfx, "static void buildCornerTextures(")
code += function(theme, "float dashThemeRadius(")
code += function(gfx, "static inline float SX(")
code += function(gfx, "static inline float SY(")
if "static int roundedGeometry(" in gfx:
    code += function(gfx, "static int roundedGeometry(")
for signature in ("static float roundedX(", "static float roundedY(", "static void roundedRectGradV("):
    if signature in gfx:
        code += function(gfx, signature)
code += function(gfx, "static void panelCorners(")
for signature in ("static void borderRect(", "static void borderPixels(", "static void borderSpan(", "static float cornerInset(", "static void borderRow("):
    if signature in gfx:
        code += function(gfx, signature)
if "static void borderCorner(" in gfx:
    code += function(gfx, "static void borderCorner(")
code += "void dashRoundedGradV(float,float,float,float,float,DashColor,DashColor);\n"
code += "void dashRoundedBorder(float,float,float,float,float,DashColor);\n"
for signature in ("void dashPanelGradV(", "void dashRoundedGradV(", "void dashPanel(",
                  "void dashPanelBorder(", "void dashRoundedBorder("):
    code += function(gfx, signature)
code += function(ui, "void dashDrawSelection(")
if "static void scrollbarShape(" in ui:
    code += function(ui, "static void scrollbarShape(")
code += function(ui, "void dashDrawScrollbar(")
if "static void loadingBarShape(" in ui:
    code += function(ui, "static void loadingBarShape(")
# Execute the actual track/fill draw calls, with timing handled separately by
# the transfer and modal tests. This also runs against the old panel renderer.
loading = function(ui, "static void loadingDraw(")
start = loading.index("loading_fraction =", loading.index("if (loading_total > 0)"))
start = loading.index("\n", start) + 1
end = loading.index("\n\t}", start)
code += "static void drawLoadingBar(float x,float bar_y,float w,float loading_fraction) {\n"
code += loading[start:end] + "\n}\n"
code += r'''
static void samePixel(float x,float y,const float expected[3]) {
    float actual[3];sample(x,y,actual);
    for(int channel=0;channel<3;channel++) {
        if(fabsf(actual[channel]-expected[channel])>0.5f) {
            fprintf(stderr,"border mismatch at %.3f,%.3f: %.2f versus %.2f (corner=%d style=%d)\n",
                    x,y,actual[channel],expected[channel],dashTheme.corner,dashTheme.highlight);
            fprintf(stderr,"scale=%.4f offsets=%.4f,%.4f\n",g_sy,g_ox,g_oy);
            abort();
        }
    }
}
static void mirroredShape(float x,float y,float w,float h,float radius) {
    int left=(int)floorf(SX(x)+0.5f),top=(int)floorf(SY(y)+0.5f);
    int width=(int)floorf(SX(x+w)+0.5f)-left,height=(int)floorf(SY(y+h)+0.5f)-top;
    int cols=(width+1)/2,rows=(height+1)/2;
    float fill[32][32];
    if(cols>24)cols=24;
    if(rows>24)rows=24;
    for(int style=0;style<3;style++) {
        DashColor white={255,255,255,255};count=0;
        if(style==0)dashPanel(x,y,w,h,radius,white);
        else if(style==1)dashPanelBorder(x,y,w,h,radius,white);
        else {dashTheme.highlight=DASH_HL_FILL;dashDrawSelection(x,y,w,h,radius,0.7f);}
        for(int row=0;row<rows;row++)for(int col=0;col<cols;col++) {
            float expected[3];sample(left+col,top+row,expected);
            if(style==0)fill[row][col]=(expected[0]-11)/244;
            if(style==1 && expected[0]>254 && fill[row][col]<0.1f) {
                fprintf(stderr,"outline outside fill at %d,%d r=%.2f mode=%.3f\n",col,row,radius,g_sy);abort();
            }
            samePixel(left+width-1-col,top+row,expected);
            samePixel(left+col,top+height-1-row,expected);
            samePixel(left+width-1-col,top+height-1-row,expected);
        }
    }
}
int main(void) {
    buildCornerTextures();
    const float scales[]={1,512.0f/448};
    const float sizes[][3]={{120,40,12},{120,40,14},{121,21,9},{73,23,1.5f},{13,3,9}};
    for(int corner=1;corner<=2;corner++)for(int mode=0;mode<2;mode++)
    for(int phase=0;phase<4;phase++)for(unsigned shape=0;shape<sizeof(sizes)/sizeof(*sizes);shape++) {
        dashTheme.corner=corner;g_sy=scales[mode];g_ox=phase*0.1875f;g_oy=phase*0.28125f;
        mirroredShape(20.0f+phase*0.125f,10.0f+phase*0.125f,sizes[shape][0],sizes[shape][1],sizes[shape][2]);
    }
    g_sy=1;g_ox=g_oy=0;
    /* GS pixels are sampled at integer coordinates. Reflect every pixel,
     * rather than checking only ideal arc points inside the stroke. */
    dashTheme.corner=DASH_CORNER_ROUND;
    count=0;dashPanel(20,10,120,40,9,(DashColor){80,120,180,170});
    for(int row=0;row<40;row++)for(int col=0;col<120;col++) {
        float expected[3];sample(20+119-col,10+row,expected);
        samePixel(20+col,10+row,expected);
        sample(20+col,10+39-row,expected);
        samePixel(20+col,10+row,expected);
    }
    const float radii[]={4,6,9,12,14,16};
    const float strengths[]={0.35f,0.7f,1.0f};
    for(int corner=0;corner<3;corner++) for(int mode=0;mode<2;mode++)
    for(unsigned radius=0;radius<sizeof(radii)/sizeof(radii[0]);radius++)
    for(unsigned strength=0;strength<sizeof(strengths)/sizeof(strengths[0]);strength++) {
        dashTheme.corner=corner;dashTheme.highlight=mode?DASH_HL_OUTLINE:DASH_HL_FILL;
        float r=dashThemeRadius(radii[radius]),w=120,h=40;
        float x=13.25f,y=7.125f,reference[3],background[3]={11,17,23};
        count=0;dashDrawSelection(x,y,w,h,radii[radius],strengths[strength]);
        if(r>=1) { x=floorf(x+0.5f)-0.5f;y=floorf(y+0.5f)-0.5f; }
        sample(x+w/2,y+1,reference);
        samePixel(x+w/2,y+h-1,reference);
        samePixel(x+1,y+h/2,reference);samePixel(x+w-1,y+h/2,reference);
        if(r>=2) {
            for(int angle=1;angle<18;angle++) {
                float a=angle*1.57079632679f/18,depth=r-1,curve[3];
                float px=floorf(x+r-depth*cosf(a)+0.5f);
                float py=floorf(y+r-depth*sinf(a)+0.5f);
                /* Antialiased edges legitimately have partial coverage;
                 * their tint and coverage must match in every quadrant. */
                sample(px,py,curve);
                samePixel(2*x+w-px,py,curve);
                samePixel(px,2*y+h-py,curve);
                samePixel(2*x+w-px,2*y+h-py,curve);
            }
            samePixel(x+0.05f,y+0.05f,background); /* keep the rounded cutout */
            samePixel(x+w-0.05f,y+h-0.05f,background);
        } else {
            samePixel(x+1,y+1,reference);samePixel(x+w-1,y+h-1,reference);
        }
        if(mode)samePixel(x+w/2,y+h/2,background); /* outline stays hollow */
    }
    count=0;dashRoundedBorder(0,0,120,40,9,(DashColor){1,2,3,0});assert(!count);
    g_alpha=0;dashRoundedBorder(0,0,120,40,9,DASH_C_ACCENT);assert(!count);g_alpha=1;
    /* Integer framebuffer sample positions expose asymmetry hidden by
     * sampling the ideal continuous shape at half-pixel centres. Check the
     * track and thumb at their top/middle/bottom positions, including modal
     * offsets, all corner styles, and a thumb limited to the 18-pixel minimum. */
    const float origins[]={20.0f,20.25f,20.5f,20.75f};
    const float heights[]={60,240,284,286};
    const int starts[]={0,7,15};
    for(int corner=0;corner<3;corner++)
    for(unsigned origin=0;origin<sizeof(origins)/sizeof(origins[0]);origin++)
    for(unsigned height=0;height<sizeof(heights)/sizeof(heights[0]);height++)
    for(unsigned first=0;first<sizeof(starts)/sizeof(starts[0]);first++) {
        float x=origins[origin],y=10.25f,h=heights[height];
        float left=ceilf(x),background[3]={11,17,23};
        dashTheme.corner=corner;count=0;
        dashDrawScrollbar(x,y,h,starts[first],5,20);
        assert(count>0);
        for(int row=(int)ceilf(y);row<y+h;row++)
        for(int col=0;col<2;col++) {
            float reference[3];sample(left+col,row,reference);
            samePixel(left+4-col,row,reference);
        }
        if(corner!=DASH_CORNER_SQUARE) {
            samePixel(left,ceilf(y),background);
            samePixel(left+4,ceilf(y),background);
        }
    }
    count=0;dashDrawScrollbar(20,10,240,0,5,5);assert(!count);
    count=0;dashDrawScrollbar(20,10,240,0,5,0);assert(!count);
    /* Progress caps must mirror at both ends, including fractional modal
     * origins. Very short fills must stay inside the track's rounded shape
     * and never display more progress than their actual width. */
    const float fractions[]={0,0.00001f,0.001f,1.0f/360,0.005f,0.012f,
                             0.019f,0.125f,0.3333f,0.5f,0.999f,1};
    const float fades[]={0.4f,1};
    for(int corner=0;corner<3;corner++)
    for(unsigned origin=0;origin<sizeof(origins)/sizeof(*origins);origin++)
    for(unsigned fade=0;fade<sizeof(fades)/sizeof(*fades);fade++) {
        float x=origins[origin],y=10+(origins[origin]-20);
        float left=ceilf(x+30),top=ceilf(y),background[3]={11,17,23};
        float track[8][360][3];
        dashTheme.corner=corner;g_alpha=fades[fade];count=0;
        drawLoadingBar(x,y,420,0);
        for(int row=0;row<8;row++)for(int col=0;col<360;col++)
            sample(left+col,top+row,track[row][col]);
        if(corner!=DASH_CORNER_SQUARE) {
            samePixel(left,top,background);
            samePixel(left+359,top,background);
            samePixel(left,top+7,background);
            samePixel(left+359,top+7,background);
        }
        for(unsigned f=0;f<sizeof(fractions)/sizeof(*fractions);f++) {
            int filled=(int)ceilf(360*fractions[f]);
            count=0;drawLoadingBar(x,y,420,fractions[f]);
            for(int row=-1;row<=8;row++)for(int col=-1;col<=360;col++) {
                const float *base=(row<0||row>=8||col<0||col>=360)
                                  ? background:track[row][col];
                int outside=1;
                for(int channel=0;channel<3;channel++)
                    if(fabsf(base[channel]-background[channel])>0.01f)outside=0;
                if(outside||col>=filled)samePixel(left+col,top+row,base);
                if((fractions[f]==0||fractions[f]==1)&&row>=0&&row<8&&col>=0&&col<360) {
                    float mirrored[3];
                    sample(left+359-col,top+row,mirrored);
                    samePixel(left+col,top+row,mirrored);
                    sample(left+col,top+7-row,mirrored);
                    samePixel(left+col,top+row,mirrored);
                }
            }
        }
    }
    free(tex_corner.Mem);free(tex_ring.Mem);
    puts("PASS: rounded fill/outline pixel reflections agree in NTSC/PAL, fractional modal offsets, odd/even dimensions and small-radius controls");
    puts("PASS: equal border tint/opacity on all four corners and straight edges, square/soft/round, solid/outline, translucent strength and rounded cutouts");
    puts("PASS: scrollbar track and thumb match left-to-right on the pixel grid at every scroll position and corner style");
    puts("PASS: progress track/fill caps mirror horizontally and vertically; tiny fills remain inside the track across corner styles and modal fades");
}
'''
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-rounded-borders-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    subprocess.run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-lm", "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
