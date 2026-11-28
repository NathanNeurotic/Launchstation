"""Render the production review table across video heights and page scrolling."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dash_ui.c').read_text()


def function(signature):
    a = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}':
            return source[a:a + token.end()] + '\n'
    raise AssertionError(signature)


code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
typedef struct { unsigned r, g, b, a; } DashColor;
typedef struct { unsigned pressed; } DashInput;
typedef struct { const char *label,*value; } DashTableRow;
#define DASH_VW 640
static float DASH_VH;
#define DASH_LIST_ROW_H 24.0f
#define DASH_FS_SMALL 1
#define DASH_FS_LABEL 2
#define DASH_FS_CAPTION 3
#define DASH_C_TEXT DC(1,0,0,0)
#define DASH_C_TEXT2 DC(2,0,0,0)
#define DASH_C_TEXT3 DC(3,0,0,0)
#define DASH_C_ACCENT DC(4,0,0,0)
#define DASH_C_WARM DC(5,0,0,0)
#define DASH_IN_OK 1
#define DASH_IN_BACK 2
#define DASH_IN_UP 4
#define DASH_IN_DOWN 8
#define DASH_IN_LEFT 16
#define DASH_IN_RIGHT 32
#define DASH_SND_CONFIRM 1
#define DASH_SND_CANCEL 2
#define DASH_BLEND_ADD 1
#define DASH_BLEND_NORMAL 2
#define DI_WARNING 1
#define DI_INFO 2
#define DI_DPAD_UD 3
#define DC(a,b,c,d) ((DashColor){a,b,c,d})
#define DASH_C_DANGER DC(6,0,0,0)
#define DASH_C_SELEDGE DASH_C_DANGER
#define DASH_HL_TEXT 1
#define DASH_FS_BODY DASH_FS_SMALL
#define DASH_SND_CURSOR 3
static struct {int glow, highlight;} dashTheme;
static int frame,scissor,bars,clipped,mode,small,last_seen;
static float clip_y,clip_h;
static u64 Timer(void){return 0;}
static void dashInputFlush(void){}
static void dashInputPoll(DashInput *in){
 static const unsigned keys[]={DASH_IN_DOWN,DASH_IN_RIGHT,DASH_IN_RIGHT,DASH_IN_RIGHT,DASH_IN_LEFT,DASH_IN_UP,0};
 assert(frame<7);in->pressed=keys[frame++];if(frame==7)in->pressed=mode?DASH_IN_OK:DASH_IN_BACK;
}
static void dashSoundPlay(int n){assert(n==1||n==2||n==3);}
static float dashVisualUpdate(void){return 1;}
static float dashAnimSpeed(void){return 1;}
static float dashApproach(float v,float t,float speed,float dt){(void)v;(void)speed;(void)dt;return t;}
static float dashEaseOutCubic(float n){return n;}
static float dashClampf(float n,float a,float b){assert(b>=a);return n<a?a:n>b?b:n;}
static int dashTextWrapLines(float w,int n,const char *s,int fs){(void)w;(void)n;(void)s;(void)fs;assert(0);return 0;}
static void dashRenderScene(void){}
static int dash_start_menu_blocked;
static int dashDashboardPending(void){return 0;}
static float dashLerpf(float a,float b,float t){return a+(b-a)*t;}
static DashColor dashColorScale(DashColor c,float t){(void)t;return c;}
static void modalBackdrop(float n){assert(n==1);}
static void dashGfxPushAlpha(float n){assert(n==1||n==0.55f);}
static void dashGfxPushOffset(float x,float y){assert(!x&&!y);}
static void dashGfxPopOffset(void){}
static void dashGfxPopAlpha(void){}
static void dashGfxPresent(void){assert(!scissor);}
static void modalPanel(float x,float y,float w,float h,float alpha){assert(x>=0&&y>=0&&x+w<=640&&y+h<=DASH_VH&&alpha==1);}
static void dashGfxBlend(int n){(void)n;}
static DashColor dashColorA(DashColor c,float a){(void)a;return c;}
static void dashGlow(float x,float y,float w,float h,DashColor c){(void)x;(void)y;(void)w;(void)h;(void)c;}
static void dashIcon(int i,float x,float y,float size,DashColor c){(void)i;(void)x;(void)y;(void)size;(void)c;}
static void dashTextScrollTimed(float x,float y,float w,const char *s,int fs,DashColor c,float age){(void)x;(void)y;(void)w;(void)s;(void)fs;(void)c;(void)age;}
static void dashTextWrap(float x,float y,float w,int n,const char *s,int fs,DashColor c,float h){(void)x;(void)y;(void)w;(void)n;(void)s;(void)fs;(void)c;(void)h;assert(0);}
static void dashText(float x,float y,const char *s,int fs,DashColor c){(void)x;(void)y;(void)s;(void)fs;assert(!scissor);(void)c;}
static void dashTextFit(float x,float y,float w,const char *s,int fs,DashColor c){
 assert(scissor&&fs==DASH_FS_SMALL&&w>0&&y+24>clip_y&&y<clip_y+clip_h);
 if(c.r==DASH_C_TEXT2.r)assert(x==90);else assert(c.r==DASH_C_TEXT.r&&x==264);
 if(!strcmp(s,"last"))last_seen=1;clipped++;
}
static void dashGfxScissor(float x,float y,float w,float h){assert(!scissor&&x>=0&&w>0&&y+h<DASH_VH-44);scissor=1;clip_y=y;clip_h=h;}
static void dashGfxScissorReset(void){assert(scissor);scissor=0;}
static void dashDrawScrollbar(float x,float y,float h,float offset,int visible,int total){assert(x<640&&y+h<DASH_VH-44&&offset>=0&&visible<=total);bars++;}
static void dashPanel(float x,float y,float w,float h,int r,DashColor c){(void)r;(void)c;assert(x>=0&&y>=0&&x+w<=640&&y+h<=DASH_VH);}
static void dashPanelBorder(float x,float y,float w,float h,int r,DashColor c){dashPanel(x,y,w,h,r,c);}
static void dashDrawSelection(float x,float y,float w,float h,int r,float strength){assert(strength==1.0f);dashPanel(x,y,w,h,r,DASH_C_ACCENT);}
static DashColor dashItemColor(int selected){return selected?DASH_C_TEXT:DASH_C_TEXT2;}
static void drawButton(float x,float y,int icon,const char *s){assert(x>=0&&y>=0&&x+22<=640&&y+22<=DASH_VH);(void)icon;(void)s;}
static int dashOkIcon(void){return 1;}
static int dashBackIcon(void){return 2;}
static float dashButtonTextY(float y,int fs){(void)fs;return y;}
static void dashTextCenter(float x,float y,const char *s,int fs,DashColor c){(void)x;(void)y;(void)s;(void)fs;(void)c;}
'''
code += function('static int modalButtons(') + function('int dashModalConfirmTable(')
code += r'''
int main(void){
 DashTableRow rows[18];for(int i=0;i<18;i++)rows[i]=(DashTableRow){"label",i==17?"last":"value"};
 for(int height=384;height<=576;height+=32)for(mode=0;mode<2;mode++)for(small=0;small<2;small++){
  DASH_VH=height;frame=bars=clipped=last_seen=0;
  assert(dashModalConfirmTable("Review HDD format",rows,small?3:18,"Continue",1)==mode);
  assert(frame==7&&clipped>0&&!scissor);
  if(small)assert(!bars);else assert(bars==6&&last_seen);
 }
 assert(!dashModalConfirmTable("Review",NULL,0,"Continue",1));
 puts("PASS: aligned 24px review rows, video-height bounds, scrollbars, visible-page navigation, confirm and cancel");
}
'''
with tempfile.TemporaryDirectory(prefix='modal-confirm-table-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                    '-fsanitize=undefined', str(d / 'test.c'), '-lm', '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True)
