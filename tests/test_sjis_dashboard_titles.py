"""Execute title encoding, ELISA atlas pixels, run boundaries and actual drawing."""
from pathlib import Path
import shutil
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
font=(root/'src/dash_font.c').read_text()
classic=(root/'src/draw_text.c').read_text(encoding='utf-8', errors='replace')
def fn(src,sig):
    a=src.index(sig);p=src.index('{',a)+1;depth=1
    while depth:
        depth+=(src[p]=='{')-(src[p]=='}');p+=1
    return src[a:p]+'\n'

prefix=r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define ELISA_GLYPHS (55016/8)
#define ELISA_CODE 0x10000u
#define GS_PSM_T8 1
#define GS_PSM_CT32 2
#define GS_CLUT_STORAGE_CSM1 0
#define GS_FILTER_NEAREST 0
#define FONT_FIRST 32
#define FONT_COLS 28
#define FONT_SLOT_W 18
#define FONT_SLOT_H 34
#define CLASSIC_CELL_EM 1.1f
#define CLASSIC_XMID_ROW 9.5f
#define DASH_FONT_FIRST 32
#define DASH_FONT_LAST 255
typedef struct {int Width,Height,PSM,ClutPSM,ClutStorageMode,Filter,Delayed;u32 *Mem,*Clut;} GSTEXTURE;
typedef struct {int r,g,b,a;} DashColor;
typedef struct {float xoff,yoff,w,h,x,y,adv16;} DashGlyphData;
typedef struct {DashGlyphData glyphs[224];} DashFaceData;
typedef struct {const DashFaceData *f;GSTEXTURE *tex;float s,px,yshift;int classic;} FaceSel;
typedef struct {int width,xoff,adv;} GlyphMetric;
static GlyphMetric metrics[260];
static GSTEXTURE elisa_tex,font_tex,face_tex;
static const unsigned char *elisa_source;
static u32 font_clut[256];
static unsigned char data[55016],*elisaFnt;
static int alloc_fail,font_ready,adds,jp_adds,classic_adds;
static GSTEXTURE *active_batch;
enum { DASH_FONT_ROBOTO, DASH_FONT_SYSTEM, DASH_FONT_EMOTION_ENGINE };
static struct {int bold,font;} dashTheme;
static void *memalign(int a,size_t n){(void)a;return alloc_fail?NULL:malloc(n);}
static void dashTexInvalidate(GSTEXTURE *t){(void)t;}
static void gsKit_setup_tbw(GSTEXTURE *t){(void)t;}
static void SyncDCache(void *a,void *b){(void)a;(void)b;}
static char *transcpy_sjis(char *out,const unsigned char *s){(void)s;strcpy(out,"_");return out;}
static void facesInit(void){}
static void pickFace(float scale,FaceSel *s){static DashFaceData f;s->f=&f;s->tex=&face_tex;s->s=1;s->px=20*scale;s->yshift=0;s->classic=0;}
static float dashGfxTextY(float y){return y;}
static void dashSpriteBatchBegin(GSTEXTURE *t,DashColor c){(void)c;assert(!active_batch);active_batch=t;if(t==&elisa_tex)jp_adds++;}
static void dashSpriteBatchEnd(void){active_batch=NULL;}
static void dashSpriteBatchAdd(float a,float b,float c,float d,float e,float f,float g,float h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;adds++;}
static float dashTextWidthN(const char *s,int n,float f){(void)s;(void)n;(void)f;return 0;}
'''
table=classic[classic.index('const u16 font404[]'):classic.index('// ASCII')]
program=prefix+table
for sig in ['static int elisaGlyphIndex(', 'static int elisaAtlasReady(', 'void dashSjisTitle(',
            'static int nextGlyphs(', 'static int glyphPrefixLength(', 'static float glyphAdv(', 'static float drawRun(']:
    program+=fn(font,sig)
# Python's independent Shift-JIS/EUC-JP codecs supply the row/column oracle.
holes=list(map(lambda x:int(x,0),re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b',table[table.index('{')+1:table.index('}')])) )
checks=[]
for lead in list(range(0x81,0xA0))+list(range(0xE0,0xF0)):
    for trail in range(0x40,0xFD):
        try:
            euc=bytes([lead,trail]).decode('shift_jis').encode('euc_jp')
        except UnicodeError:
            continue
        if len(euc)!=2: continue
        code=int.from_bytes(euc,'big');index=(euc[0]-0xA1)*94+euc[1]-0xA1
        for start,count in zip(holes[::2],holes[1::2]):
            if not start: break
            if start<=code<start+count: index=-1;break
            if code>=start: index-=count
        if index<0 or index>=55016//8: index=-1
        checks.append(f'assert(elisaGlyphIndex(0x{lead:02X}{trail:02X})=={index});')
program+='static void checkCodecMapping(void){'+''.join(checks)+'}\n'
program+=r'''
int main(void){
    checkCodecMapping();
    char out[256];unsigned int glyph[2];const unsigned char *p;
    /* No font: fallback preserves ASCII and uses the existing transliterator. */
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"A\x82\xA0" "B");assert(!strcmp(out,"A_B"));
    assert(elisaGlyphIndex(0x817F)<0 && elisaGlyphIndex(0xFFFF)<0 && elisaGlyphIndex(0x8140)==0);
    data[0]=0x80;data[7]=1;elisaFnt=data;
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x81\x40");
    assert(!memcmp(out,"\xFF" "J0000",6) && out[6]==0);
    u8 *pixels=(u8 *)elisa_tex.Mem;
    assert(pixels[0]==255 && pixels[1]==0 && pixels[7*512+7]==255);
    assert(elisa_tex.Width==512 && elisa_tex.Height==1024);
    p=(unsigned char *)out;assert(nextGlyphs(&p,glyph)==1 && glyph[0]==ELISA_CODE && !*p);
    for(int n=0;n<6;n++) assert(glyphPrefixLength(out,n)==0);
    assert(glyphPrefixLength(out,6)==6);
    for(int size=1;size<12;size++) {
        char small[16];memset(small,0x55,sizeof(small));
        dashSjisTitle(small,size,(const unsigned char *)"\x81\x40" "A");
        assert(strlen(small)<(size_t)size && small[size]==0x55);
        if(size<=6) assert(!small[0]);else assert(!memcmp(small,"\xFF" "J0000",6));
    }
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x81\x9B");
    assert((unsigned char)out[0]==255 && out[1]=='0' && !out[2]);
    p=(unsigned char *)out;assert(nextGlyphs(&p,glyph)==2 && glyph[0]==0x100 && glyph[1]==0x101);
    const unsigned char *bad[]={ (unsigned char *)"\xFF" "J",(unsigned char *)"\xFF" "J00",(unsigned char *)"\xFF" "JFFFF"};
    for(int i=0;i<3;i++){p=bad[i];assert(nextGlyphs(&p,glyph)==1 && glyph[0]=='?');}
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x81\x40");
    FaceSel face;pickFace(1,&face);assert(glyphAdv(&face,ELISA_CODE)>0);
    DashColor color={255,255,255,255};
    drawRun(0,0,(unsigned char *)out,NULL,1,color);assert(adds==1 && jp_adds==1);
    adds=jp_adds=0;drawRun(0,0,(unsigned char *)out,(unsigned char *)out+3,1,color);assert(!adds);
    /* Consecutive Japanese glyphs share one batch, including bold sprites. */
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x81\x40\x81\x40\x81\x40");
    adds=jp_adds=0;drawRun(0,0,(unsigned char *)out,NULL,1,color);
    assert(adds==3 && jp_adds==1 && !active_batch);
    dashTheme.bold=1;adds=jp_adds=0;drawRun(0,0,(unsigned char *)out,NULL,1,color);
    assert(adds==6 && jp_adds==1 && !active_batch);dashTheme.bold=0;
    dashTheme.font=DASH_FONT_EMOTION_ENGINE;dashTheme.bold=1;adds=jp_adds=0;
    drawRun(0,0,(unsigned char *)out,NULL,1,color);
    assert(adds==6 && jp_adds==1 && !active_batch);
    dashTheme.font=DASH_FONT_ROBOTO;dashTheme.bold=0;
    /* A Latin or pad run ends the Japanese batch; later Japanese resumes it. */
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x81\x40" "A" "\x81\x40");
    adds=jp_adds=0;drawRun(0,0,(unsigned char *)out,NULL,1,color);
    assert(adds==2 && jp_adds==2 && !active_batch);
    font_ready=1;
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x81\x40\x81\x9B\x81\x40");
    adds=jp_adds=0;drawRun(0,0,(unsigned char *)out,NULL,1,color);
    assert(adds==4 && jp_adds==2 && !active_batch);

    free(elisa_tex.Mem);elisa_tex.Mem=NULL;elisa_source=NULL;alloc_fail=1;
    dashSjisTitle(out,sizeof(out),(const unsigned char *)"\x82\xA0");assert(!strcmp(out,"_"));
    (void)classic_adds;
}
'''
with tempfile.TemporaryDirectory(prefix='sjis-titles-') as td:
    path=Path(td);(path/'test.c').write_text(program)
    subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test.exe')],check=True)
    subprocess.run([str(path/'test.exe')],check=True)
print('PASS: Shift-JIS fallback, atlas rasterization/drawing, symbols, allocation failure and token-safe clipping')
