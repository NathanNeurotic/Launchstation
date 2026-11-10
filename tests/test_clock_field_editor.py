"""Drive the boxed date/time editor used by both clock-setting actions."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
def function(path, signature):
 source=(root/path).read_text();a=source.index(signature);b=source.index('{',a)+1;depth=1
 while depth:
  depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]+'\n'
settings=(root/'src/dash_settings.c').read_text()
assert 'dashModalKeyboard(id == S_CLOCK_SOFTWARE' not in settings
assert settings.count('editClockDateTime(id == S_CLOCK_SOFTWARE ? "Set software clock" : "Set PS2 clock", f)')==1
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef int DashColor;
typedef struct { unsigned pressed; } DashInput;
enum { DASH_IN_UP=1, DASH_IN_DOWN=2, DASH_IN_LEFT=4, DASH_IN_RIGHT=8, DASH_IN_OK=16, DASH_IN_BACK=32,
       DASH_IN_MENU=64, DASH_IN_L1=128, DASH_IN_R1=256, DASH_IN_L2=512, DASH_IN_R2=1024 };
#define DASH_VW 640
#define DASH_VH 448
#define DC(r,g,b,a) 0
enum { DI_CLOCK, DI_BTN_LEFT, DI_BTN_RIGHT, DI_BTN_UP, DI_BTN_DOWN, DASH_SND_CURSOR, DASH_SND_CONFIRM, DASH_SND_CANCEL,
       DASH_FS_LABEL, DASH_FS_CAPTION, DASH_FS_BODY, DASH_FS_SMALL, DASH_C_TEXT, DASH_C_TEXT2, DASH_C_TEXT3, DASH_C_ACCENT };
static const unsigned *script; static int frames;
static void dashInputPoll(DashInput *in){in->pressed=*script++;frames++;}
static void dashInputFlush(void){}
static void dashSoundPlay(int s){(void)s;}
static float dashVisualUpdate(void){return 0.016f;}
static float dashApproach(float a,float b,float r,float dt){(void)a;(void)r;(void)dt;return b;}
static float dashAnimSpeed(void){return 1.0f;}
static float dashEaseOutCubic(float t){return t;}
static void dashRenderScene(void){}
static void dashModalBackdrop(float a){(void)a;}
static void dashGfxPushAlpha(float a){(void)a;}
static void dashGfxPushOffset(float x,float y){(void)x;(void)y;}
static void dashGfxPopOffset(void){}
static void dashGfxPopAlpha(void){}
static void dashGfxPresent(void){}
static void dashModalPanel(float x,float y,float w,float h,float a){(void)x;(void)y;(void)w;(void)h;(void)a;}
static void dashIcon(int i,float x,float y,float s,DashColor c){(void)i;(void)x;(void)y;(void)s;(void)c;}
static float dashTextS(float x,float y,const char *s,float k,DashColor c){(void)y;(void)k;(void)c;assert(s);return x+10;}
static void dashTextFit(float x,float y,float w,const char *s,float k,DashColor c){(void)x;(void)y;(void)w;(void)k;(void)c;assert(s);}
static void dashTextCenter(float x,float y,const char *s,float k,DashColor c){(void)x;(void)y;(void)k;(void)c;assert(s);}
static void dashDrawSelection(float x,float y,float w,float h,float r,float s){(void)x;(void)y;(void)w;(void)h;(void)r;(void)s;}
static void dashPanel(float x,float y,float w,float h,float r,DashColor c){(void)x;(void)y;(void)w;(void)h;(void)r;(void)c;}
static DashColor dashItemColor(int s){return s;}
static float dashDrawButton(float x,float y,int i,const char *k){(void)x;(void)y;(void)i;(void)k;return 20;}
static int dashOkIcon(void){return 0;}
static int dashBackIcon(void){return 0;}
static void dashModalHintRow(float x,float y,int a,const char *b,int c,const char *d){(void)x;(void)y;(void)a;(void)c;assert(b&&d);}
'''
code+=function('src/dash_clock_sync.c','static int manualLeap(')
code+=function('src/dash_clock_sync.c','int dashClockDaysInMonth(')
code+=function('src/dash_settings.c','static int editClockDateTime(')
code+=r'''
static int run(const unsigned *keys,int f[6]){script=keys;return editClockDateTime("Set PS2 clock",f);}
int main(void){
 int f[6]={2024,3,31,23,59,59},g[6];
 /* Month down to February of a leap year: the 31st becomes the 29th. */
 static const unsigned a[]={DASH_IN_RIGHT,DASH_IN_DOWN,DASH_IN_OK};
 assert(run(a,f)&&f[0]==2024&&f[1]==2&&f[2]==29);
 /* Year +1 makes 2025 non-leap, so the day follows to the 28th. */
 static const unsigned b[]={DASH_IN_UP,DASH_IN_OK};
 assert(run(b,f)&&f[0]==2025&&f[2]==28);
 /* L1 on the year wraps below 2000 to the top of the range; R1 on minutes wraps 59 -> 9. */
 f[0]=2003;f[4]=59;
 static const unsigned c[]={DASH_IN_L1,DASH_IN_LEFT,DASH_IN_LEFT,DASH_IN_R1,DASH_IN_OK};
 assert(run(c,f)&&f[0]==2093&&f[4]==9&&f[5]==59);
 /* Second +1 wraps to 0; Left from Year reaches Second. */
 static const unsigned d[]={DASH_IN_LEFT,DASH_IN_UP,DASH_IN_OK};
 assert(run(d,f)&&f[5]==0);
 /* Back leaves the caller's fields untouched; out-of-range input is clamped, not passed through. */
 memcpy(g,f,sizeof(g));
 static const unsigned e[]={DASH_IN_UP,DASH_IN_UP,DASH_IN_BACK};
 assert(!run(e,f)&&!memcmp(f,g,sizeof(g)));
 int bad[6]={1999,13,40,24,60,60};
 static const unsigned o[]={DASH_IN_OK};
 assert(run(o,bad)&&bad[0]==2000&&bad[1]==12&&bad[2]==31&&bad[3]==23&&bad[4]==59&&bad[5]==59);
 printf("ok\n");return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 c=Path(tmp)/'t.c';exe=Path(tmp)/'t';c.write_text(code)
 subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print("PASS: clock field editor wraps each field, keeps the day valid for month/leap year, clamps input and leaves fields on Back")
