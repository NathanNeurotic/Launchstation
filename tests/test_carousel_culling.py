"""Execute carousel drawing and compare its visible cards with the old loop."""
from pathlib import Path
import shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/dash_apps.c').read_text()
a=source.index('static void drawCarousel('); b=source.index('static void appsDraw(',a)
production=source[a:b]
reference=production.replace('drawCarousel(void)','drawCarouselReference(void)').replace('if (left + s < -8.0f || left > DASH_VW + 8.0f)','if (0)')
code=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define DASH_VW 640
#define DASH_BLEND_ADD 1
#define DASH_BLEND_NORMAL 0
#define DASH_C_ACCENT 0
#define DASH_C_TEXT 0
#define DASH_C_TEXT2 0
#define DASH_C_TEXT3 0
#define DASH_FS_TITLE 1
#define DASH_FS_SMALL 1
#define DASH_FS_CAPTION 1
typedef struct {char title[16],desc[16],path[16];} DashApp;
static DashApp dashApps[100];static int order[100],norder,sel;
static float sel_anim;
static struct {int glow;} dashTheme;
static DashApp *selApp(void){return norder?&dashApps[sel]:NULL;}
static void dashGfxBlend(int x){(void)x;}
static int dashColorA(int c,float a){(void)a;return c;}
static void dashGlow(float x,float y,float w,float h,int c){(void)x;(void)y;(void)w;(void)h;(void)c;}
static float dashClampf(float x,float a,float b){return fminf(b,fmaxf(a,x));}
static float dashLerpf(float a,float b,float t){return a+(b-a)*t;}
static void dashGfxPushAlpha(float a){(void)a;}static void dashGfxPopAlpha(void){}
static void dashDrawTileFocus(float x,float y,float w,float h,float r,float f){(void)x;(void)y;(void)w;(void)h;(void)r;(void)f;}
static void dashTextScrollCenter(float x,float y,float w,const char*s,float f,int c){(void)x;(void)y;(void)w;(void)s;(void)f;(void)c;}
static void dashTextCenter(float x,float y,const char*s,float f,int c){(void)x;(void)y;(void)s;(void)f;(void)c;}
static void dashLibDeviceLabel(const char*p,char*b,int n){(void)p;snprintf(b,n,"device");}
typedef struct {int idx;float x,y,w,h,f;int prio;} Draw;
static Draw calls[16];static int count;
static void dashDrawAppArt(DashApp*a,float x,float y,float w,float h,float f,int p){assert(count<16);calls[count++]=(Draw){(int)(a-dashApps),x,y,w,h,f,p};}
'''
code+=production+reference
code+=r'''
int main(void){
 for(int i=0;i<100;i++)order[i]=i;
 int saved=0;
 for(norder=1;norder<=100;norder+=3)for(sel=0;sel<norder;sel++)for(int phase=-4;phase<=4;phase++){
  sel_anim=sel+phase*0.125f;count=0;drawCarouselReference();
  Draw expected[16];int n=0,old=count;
  for(int i=0;i<count;i++)if(!(calls[i].x+calls[i].w < -8 || calls[i].x > DASH_VW+8))expected[n++]=calls[i];
  count=0;drawCarousel();assert(count==n);saved+=old-count;
  for(int i=0;i<n;i++)assert(calls[i].idx==expected[i].idx&&calls[i].x==expected[i].x&&calls[i].y==expected[i].y&&calls[i].w==expected[i].w&&calls[i].f==expected[i].f&&calls[i].prio==expected[i].prio);
 }
 assert(saved>0);puts("PASS: identical visible carousel geometry/order across fractional poses; offscreen artwork/tiles omitted");
}
'''
with tempfile.TemporaryDirectory(prefix='carousel-culling-') as folder:
 p=Path(folder);(p/'test.c').write_text(code)
 subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
