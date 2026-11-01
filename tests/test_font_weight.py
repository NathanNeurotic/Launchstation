"""Exercise the bundled font palettes and real Bold/shadow drawing paths."""
from pathlib import Path
import ast
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
font = (root / "src/dash_font.c").read_text()
assert re.search(r'static u32 face_clut\[DASH_FONT_FACES\]\[256\]', font)
data_header = (root / "include/dash_font_data.h").read_text()
internal_header = (root / "include/dash_internal.h").read_text()
data = "\n".join((root / "src" / name).read_text() for name in
                 ("dash_font_data.c", "dash_font_emotion_engine.c", "dash_font_emotion_engine_bold.c"))
generator = ast.parse((root / "scripts/gen_dash_font.py").read_text())
sizes = next(ast.literal_eval(node.value) for node in generator.body
             if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "SIZES"
                                                    for target in node.targets))


def function(signature):
    start = font.index(signature)
    end = font.index("{", start) + 1
    depth = 1
    while depth:
        depth += (font[end] == "{") - (font[end] == "}")
        end += 1
    return font[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef struct { int r,g,b,a; } DashColor;
#define DC(r,g,b,a) ((DashColor){r,g,b,a})
#define DASH_FS_SMALL 0.66f
typedef struct {
    int Width,Height,PSM,ClutPSM,ClutStorageMode,Filter,Delayed;
    u32 *Clut,*Mem;
} GSTEXTURE;
enum { GS_PSM_T8, GS_PSM_CT32, GS_CLUT_STORAGE_CSM1, GS_FILTER_LINEAR };
static struct { int font,font_scale,bold,text_shadow; } dashTheme;
static int cache_flushes,texture_setups,texture_frees,foreground,shadow;
static void dashTexRelease(GSTEXTURE *t) { assert(t->Mem);texture_frees++; }
static DashColor batch_color;
static GSTEXTURE *last_texture;
static float last_left,last_right,last_u,last_v;
static int literal_calls;
static unsigned char literal_byte;
static void SyncDCache(void *start,void *end) {
    assert((u8 *)end-(u8 *)start==1024);cache_flushes++;
}
static void gsKit_setup_tbw(GSTEXTURE *t) { assert(t->Clut && t->Mem);texture_setups++; }
static float dashGfxTextY(float y) { return y; }
static void dashSpriteBatchBegin(GSTEXTURE *tex,DashColor color) { assert(tex);batch_color=color;last_texture=tex; }
static void dashSpriteBatchEnd(void) {}
static void dashSpriteBatchAdd(float x1,float y1,float u1,float v1,float x2,float y2,float u2,float v2) {
    assert(x2>x1 && y2>y1 && u2>u1 && v2>v1);
    last_left=x1;last_right=x2;last_u=u1;last_v=v1;
    if(batch_color.a==255) foreground++;
    else { assert(batch_color.a==89);shadow++; }
}
'''
start = internal_header.index("enum DashFontFace {")
code += internal_header[start:internal_header.index("};", start) + 2] + "\n"
code += data_header.replace("#include <tamtypes.h>", "")
code += data.replace('#include "dash_font_data.h"', "")
for name in ("FONT_CELL_W", "FONT_CELL_H", "FONT_SLOT_W", "FONT_SLOT_H", "FONT_COLS", "FONT_FIRST", "FONT_LAST",
             "FONT_GLYPHS", "DASH_FONT_UNIT", "CLASSIC_CELL_EM", "CLASSIC_XMID_ROW"):
    code += re.search(rf"^#define {name} .+$", font, re.M).group() + "\n"
for name in ("GlyphMetric", "FaceSel"):
    end = font.index("} " + name + ";") + len("} " + name + ";")
    code += font[font.rfind("typedef struct", 0, end):end] + "\n"
code += r'''
static GlyphMetric metrics[FONT_GLYPHS];
static GSTEXTURE font_tex,elisa_tex,face_tex[DASH_FONT_FACES];
#define ELISA_GLYPHS (55016 / 8)
#define ELISA_CODE 0x10000u
static u32 face_clut[DASH_FONT_FACES][256];
static int font_ready=1,faces_ready;
static const DashFaceData *active_faces;
static void dashFontByte(unsigned char c,float x,float y,DashColor color) {
    assert(x>=10 && x<=10.5f && y==20 && color.a==255);literal_calls++;literal_byte=c;
}
'''
for signature in ("static inline int clutSwizzle(", "static int nextGlyphs(",
                  "static const DashFaceData *fontFaces(", "static void facesShutdown(void)\n{",
                  "static void facesInit(void)\n{",
                  "static float fontScale(", "static void pickFace(", "static float glyphAdv(",
                  "float dashTextWidthN(", "static float drawRun(", "float dashTextS(", "void dashFontMonoByte("):
    code += function(signature)
code += "static const int expected_gamma[] = {" + ",".join(str(round(gamma * 100)) for _, gamma in sizes) + "};\n"
code += r'''
int main(void) {
    facesInit();assert(cache_flushes==DASH_FONT_FACES && texture_setups==DASH_FONT_FACES);
    /* Atlas alpha is decoded to regular coverage independently of text size. */
    for(int face=0;face<DASH_FONT_FACES;face++) {
        const DashFaceData *f=&dash_font_faces[face];
        assert(f->alpha_gamma100==expected_gamma[face] && face_tex[face].Clut==face_clut[face]);
        assert(face_tex[face].Width==f->tex_w && face_tex[face].Height==f->tex_h);
        for(int coverage=0;coverage<=255;coverage++) {
            int encoded=(int)(255*powf(coverage/255.0f,f->alpha_gamma100/100.0f)+0.5f);
            int actual=face_clut[face][clutSwizzle(encoded)]>>24;
            int expected=(coverage*128+127)/255;
            assert(abs(actual-expected)<=1);
            assert((face_clut[face][clutSwizzle(encoded)]&0xffffff)==0x808080);
        }
        for(int alpha=0;alpha<256;alpha++) {
            int actual=face_clut[face][clutSwizzle(alpha)]>>24;
            int linear=(alpha*128+127)/255;
            assert(actual<=linear);
            if(f->alpha_gamma100==100) assert(actual==linear);
        }
        if(f->alpha_gamma100<100) assert((face_clut[face][clutSwizzle(128)]>>24)<64);
    }
    for(int i=0;i<FONT_GLYPHS;i++) metrics[i]=(GlyphMetric){0,6,9};
    const int scales[]={90,100,115};
    const float sizes[]={0.60f,0.66f,0.82f,1.0f,1.1f,1.4f};
    const DashColor colors[]={DC(236,238,244,255),DC(118,214,255,255)};
    int used_faces[DASH_FONT_COUNT][2][DASH_FONT_FACES]={{{0}}};
    for(int face=0;face<DASH_FONT_COUNT;face++) for(int fs=0;fs<3;fs++) for(int size=0;size<6;size++) {
        dashTheme.font=face;dashTheme.font_scale=scales[fs];
        for(int col=0;col<2;col++) for(int sh=0;sh<2;sh++) for(int bold=0;bold<2;bold++) {
            dashTheme.bold=bold;dashTheme.text_shadow=sh;foreground=shadow=0;
            const DashFaceData *expected=face==DASH_FONT_EMOTION_ENGINE
                ? (bold?dash_emotion_engine_bold_faces:dash_emotion_engine_faces) : dash_font_faces;
            assert(fontFaces()==expected);
            FaceSel chosen;pickFace(sizes[size],&chosen);
            if(!chosen.classic) used_faces[face][bold][chosen.f-expected]=1;
            /* Real Bold has its own metrics, even before texture switching. */
            float width=dashTextWidthN("Ag",-1,sizes[size]);
            float expected_width=chosen.classic ? glyphAdv(&chosen,'A')+glyphAdv(&chosen,'g')
                : (chosen.f->glyphs['A'-DASH_FONT_FIRST].adv16+chosen.f->glyphs['g'-DASH_FONT_FIRST].adv16)/16.0f*chosen.s;
            assert(fabsf(width-expected_width)<0.001f);
            float end=dashTextS(10,20,"Ag",sizes[size],colors[col]);
            int passes=bold && face!=DASH_FONT_EMOTION_ENGINE ? 2 : 1;
            assert(fabsf(end-(10+width))<0.001f);
            assert(foreground==2*passes);
            assert(shadow==(sh?2*passes:0));
        }
    }
    for(int family=0;family<DASH_FONT_COUNT;family++) if(family!=DASH_FONT_SYSTEM)
        for(int bold=0;bold<2;bold++) for(int size=0;size<DASH_FONT_FACES;size++) assert(used_faces[family][bold][size]);
    /* Symbols and document text share the selected regular/Bold face,
     * including Latin-1 bytes. System/custom control glyphs remain literal. */
    for(int face=0;face<DASH_FONT_COUNT;face++) for(int fs=0;fs<3;fs++) for(int bold=0;bold<2;bold++) for(int col=0;col<2;col++) {
        dashTheme.font=face;
        dashTheme.font_scale=scales[fs];dashTheme.bold=bold;
        for(int c=0;c<256;c++) {
            foreground=shadow=literal_calls=0;
            dashFontMonoByte(c,10,20,colors[col]);
            if(c<0x20 || c==' ') assert(!foreground && !literal_calls);
            else if(face==DASH_FONT_SYSTEM || (c>=0x7f && c<0xa0)) {
                assert(!foreground && literal_calls==(bold?2:1) && literal_byte==c);
            } else if(c==0xa0) {
                assert(!foreground && !literal_calls); /* Nonbreaking space has no ink. */
            } else {
                const DashGlyphData *g=&fontFaces()[0].glyphs[c-DASH_FONT_FIRST];
                int passes=bold && face!=DASH_FONT_EMOTION_ENGINE ? 2 : 1;
                assert(!literal_calls && foreground==passes && !shadow);
                assert(last_texture==&face_tex[0] && last_left>=10 && last_right<=18.0001f);
                assert(last_u==g->x && last_v==g->y); /* 0xFF is a literal glyph, never a pad icon. */
                if(passes==1) assert(fabsf((last_left+last_right)*0.5f-14)<0.0001f);
            }
        }
    }
    puts("PASS: symbol/editor regular/Bold glyph paths, selected/unselected text, cell bounds, font sizes and literal Latin-1 including 0xFF");
    assert(cache_flushes==texture_setups && texture_frees==texture_setups-DASH_FONT_FACES);
    const int cycle[][2]={{DASH_FONT_ROBOTO,0},{DASH_FONT_ROBOTO,1},
                          {DASH_FONT_EMOTION_ENGINE,0},{DASH_FONT_EMOTION_ENGINE,1},
                          {DASH_FONT_EMOTION_ENGINE,0},{DASH_FONT_EMOTION_ENGINE,1},
                          {DASH_FONT_SYSTEM,1},{DASH_FONT_SYSTEM,0},
                          {DASH_FONT_EMOTION_ENGINE,0},{DASH_FONT_ROBOTO,0}};
    for(unsigned family=0;family<sizeof(cycle)/sizeof(cycle[0]);family++) {
        dashTheme.font=cycle[family][0];dashTheme.bold=cycle[family][1];
        const DashFaceData *faces=fontFaces();
        int changed=faces!=active_faces,old_setups=texture_setups,old_frees=texture_frees;
        /* Measurement must pick the new face even before its textures are initialized. */
        FaceSel chosen;pickFace(0.82f,&chosen);assert(chosen.f>=faces && chosen.f<faces+DASH_FONT_FACES);
        facesInit();assert(active_faces==faces);
        assert(texture_setups==old_setups+(changed?DASH_FONT_FACES:0));
        assert(texture_frees==old_frees+(changed?DASH_FONT_FACES:0));
        for(int size=0;size<DASH_FONT_FACES;size++) {
            const DashFaceData *f=&faces[size];
            assert(face_tex[size].Mem==(u32 *)f->pixels && face_tex[size].Width==f->tex_w && face_tex[size].Height==f->tex_h);
            assert(f->alpha_gamma100==expected_gamma[size]);
            for(int c=0x20;c<=0xff;c++) {
                if(c==0x20 || c==0xa0 || (c>=0x7f && c<0xa0)) continue;
                const DashGlyphData *g=&f->glyphs[c-DASH_FONT_FIRST];
                assert(g->w && g->h && g->adv16 && g->x+g->w<=f->tex_w && g->y+g->h<=f->tex_h);
            }
        }
        facesInit();assert(texture_setups==old_setups+(changed?DASH_FONT_FACES:0));
    }
    int old_frees=texture_frees,old_setups=texture_setups;
    facesShutdown();assert(!faces_ready && active_faces==NULL && texture_frees==old_frees+DASH_FONT_FACES);
    facesShutdown();assert(texture_frees==old_frees+DASH_FONT_FACES);
    facesInit();assert(faces_ready && texture_setups==old_setups+DASH_FONT_FACES);
    puts("PASS: all font families/sizes and Latin-1 glyphs, family/style switches reuse three texture slots, idempotent shutdown/reinit");
    puts("PASS: regular alpha, real Emotion Engine Regular/Bold faces without extra strokes, Roboto/System synthesized Bold, matching measurements and independent shadow");
}
'''
with tempfile.TemporaryDirectory(prefix="font-weight-") as folder:
    path = Path(folder)
    (path / "font.c").write_text(code)
    exe = path / "font"
    subprocess.run([shutil.which("gcc") or shutil.which("cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "font.c"), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
