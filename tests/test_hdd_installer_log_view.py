"""Exercise the actual diagnostic renderer and log navigation with bounded draws."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'src/dash_hdd_install.c').read_text()
def function(signature):
 a=s.index(signature);b=s.index('{',a)+1;depth=1
 while depth: depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]+'\n'
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
typedef int DashColor;
typedef struct {unsigned pressed;} DashInput;
#define DASH_IN_UP 1
#define DASH_IN_DOWN 2
#define DASH_IN_LEFT 4
#define DASH_IN_RIGHT 8
#define DASH_IN_FAV 16
#define DI_WARNING 1
#define DI_CHECK 2
#define DI_HDD 3
#define DI_BTN_UP 4
#define DI_BTN_DOWN 5
#define DI_BTN_SQUARE 6
#define DASH_C_DANGER 1
#define DASH_C_ACCENT 2
#define DASH_C_TEXT 3
#define DASH_C_TEXT2 4
#define DASH_C_TEXT3 5
#define DASH_FS_LABEL 1.0f
#define DASH_FS_SMALL 0.66f
#define DASH_FS_CAPTION 0.6f
#define DASH_VW 640
#define DASH_VH view_h
static float view_h=448;
static int debug_review,debug_result,debug_rows=8,debug_top=-1;
static HddInstallLogSnapshot snapshot;
static unsigned first_drawn,drawn;
static int clipped,closed_hint;
static float scrollbar_first,scrollbar_height;
static int scrollbar_visible,scrollbar_total,scrollbar_calls;
static float overall_pixels,current_pixels;
static int overall_tracks,current_tracks;
void hddInstallLogSnapshot(HddInstallLogSnapshot *out){*out=snapshot;}
int hddInstallLogRead(unsigned n,char out[HDD_INSTALL_LOG_COLS]){assert(n>=snapshot.first&&n<snapshot.next);if(!drawn)first_drawn=n;drawn++;snprintf(out,HDD_INSTALL_LOG_COLS,"row %u",n);return 1;}
static void dashRenderScene(void){}
static void dashModalBackdrop(float k){assert(k==1);}
static void dashModalPanel(float x,float y,float w,float h,float k){assert(x>=0&&y>=0&&x+w<=640&&y+h<=view_h&&k==1);}
static void dashIcon(int icon,float x,float y,float size,int c){(void)icon;(void)c;assert(x>=20&&y>=20&&x+size<=620&&y+size<=view_h-20);}
static float dashTextFit(float x,float y,float w,const char*s,float scale,int color){(void)s;(void)scale;(void)color;assert(x>=20&&x+w<=620&&y>=20&&y+14<=view_h-20);return x+w;}
static void dashText(float x,float y,const char*s,float scale,int c){dashTextFit(x,y,400,s,scale,c);}
static float dashTextS(float x,float y,const char*s,float scale,int c){float width=strlen(s)*7;dashTextFit(x,y,width,s,scale,c);return x+width;}
static void dashDrawSpinner(float x,float y,float radius,int c){(void)c;assert(x-radius>=20&&x+radius<=620&&y-radius>=20);}
static void dashRect(float x,float y,float w,float h,int c){assert(w>=0&&x>=20&&x+w<=620&&y>=20&&y+h<=view_h-20);
 if(h==5&&y==88){if(c==DASH_C_ACCENT)overall_pixels=w;else overall_tracks++;}
 if(h==5&&y==124){if(c==DASH_C_ACCENT)current_pixels=w;else current_tracks++;}
}
static int dashColorA(int c,float a){(void)a;return c;}
static float dashClampf(float x,float a,float b){return x<a?a:x>b?b:x;}
static void dashGfxScissorPush(float x,float y,float w,float h){assert(!clipped&&h==debug_rows*24&&x>=20&&x+w<=620&&y+h<view_h-70);clipped=1;}
static void dashGfxScissorPop(void){assert(clipped);clipped=0;}
static void dashDrawScrollbar(float x,float y,float h,float first,int visible,int total){
 assert(!clipped&&x>=20&&x+5<=620&&y>=20&&y+h<view_h-70);
 assert(first>=0&&first<=(total>visible?total-visible:0));
 scrollbar_first=first;scrollbar_height=h;scrollbar_visible=visible;scrollbar_total=total;scrollbar_calls++;
}
static float dashDrawButton(float x,float y,int icon,const char*key){float w=key?48:22;(void)icon;assert(x>=20&&x+w<=620&&y+22<=view_h-20);return w;}
static float dashButtonTextY(float y,float scale){return y+11-16*scale;}
static int dashOkIcon(void){return 0;}
static void dashModalHintRow(float x,float y,int icon,const char*a,int back,const char*b){(void)icon;assert(a&&!strcmp(a,"Close")&&back==-1&&!b);assert(x+70<=620&&y+22<=view_h-20);closed_hint++;}
static void dashGfxPresent(void){assert(!clipped);}
'''
code+=function('static void debugInput(')+function('static void debugDraw(')
code+=r'''
int main(void){
 snapshot.first=100;snapshot.next=150;snapshot.done=100;snapshot.total=1000;
 snapshot.overall=250;strcpy(snapshot.phase,"Downloading");
 strcpy(snapshot.status,"TLS handshake: system.tar.gz");strcpy(snapshot.io,"RAW cmd=3 LBA=8192 sectors=64 rc=0");
 DashInput in={.pressed=DASH_IN_UP};debugInput(&in);assert(debug_top==141);
 snapshot.next++;in.pressed=0;debugInput(&in);assert(debug_top==141); /* new lines do not move a paused viewport */
 in.pressed=DASH_IN_RIGHT;debugInput(&in);assert(debug_top==-1);
 in.pressed=DASH_IN_LEFT;debugInput(&in);assert(debug_top==135);
 in.pressed=DASH_IN_FAV;debugInput(&in);assert(debug_top==-1);
 for(int height=384;height<=576;height+=32){
  view_h=height;drawn=closed_hint=0;debug_review=0;debugDraw("PSBBN installer");assert(drawn==(unsigned)debug_rows&&!closed_hint);
  assert(first_drawn==snapshot.next-(unsigned)debug_rows&&scrollbar_first==first_drawn-snapshot.first);
  assert(overall_tracks&&current_tracks&&overall_pixels==140&&current_pixels==56);
  assert(scrollbar_height==debug_rows*24&&scrollbar_visible==debug_rows&&scrollbar_total==50+1);
  debug_review=1;debug_result=-1;drawn=0;debugDraw("Installer failed");assert(closed_hint==1&&drawn==(unsigned)debug_rows);
 }
 snapshot.first=300;snapshot.next=556;debug_top=100;drawn=0;debugDraw("History rollover");assert(first_drawn==300);
 assert(scrollbar_first==0&&scrollbar_total==256&&scrollbar_calls>0);
 snapshot.first=0;snapshot.next=2;debug_top=100;drawn=0;debugDraw("Short log");assert(first_drawn==0&&scrollbar_first==0&&scrollbar_total==2);
 snapshot.total=0;debug_review=0;drawn=0;debugDraw("Preparing connection");assert(current_pixels==112&&overall_pixels==140);
 puts("PASS: live/paused log scrolling, page steps, history rollover and diagnostic layout/hints stay inside the panel");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-log-view-') as directory:
 d=Path(directory);(d/'check.c').write_text(code)
 subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(d/'check.c'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True,timeout=10)
