"""Drive the production picker with controller input, including cross-folder selection."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
_header = (root / "include/dash_internal.h").read_text()
icon_enum = _header[_header.index("enum DashIcon {"):_header.index("};", _header.index("enum DashIcon {")) + 2]
source = (root / "src/dash_widgets.c").read_text()


def function(signature):
    start = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[start:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}':
            return source[start:start + token.end()] + '\n'
    raise AssertionError(signature)


code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
@@ICONS@@
#define MAX_PATH 1025
#define MAX_NAME 256
#define PICK_MAX 1024
#define DASH_VW 640
#define DASH_VH 448
#define DASH_LIST_ROW_H 24
typedef unsigned char u8;
typedef int DashColor;
typedef struct {unsigned pressed;} DashInput;
typedef struct {int state,icon;char name[32],mount[32],browse[32],status[32];} DashDevice;
enum {DASH_PICK_FILE,DASH_PICK_FOLDER,DASH_PICK_LAUNCH};
enum {DASH_IN_UP=1,DASH_IN_DOWN=2,DASH_IN_LEFT=4,DASH_IN_RIGHT=8,DASH_IN_OK=16,DASH_IN_BACK=32,
      DASH_IN_MENU=64,DASH_IN_FAV=128,DASH_IN_L2=256,DASH_IN_R2=512,DASH_IN_R3=1024,DASH_IN_SELECT=2048,DASH_IN_START=4096};
enum {DASH_SND_CURSOR,DASH_SND_CONFIRM,DASH_SND_CANCEL,DASH_TOAST_ERROR,DASH_TOAST_WARN,DASH_TOAST_INFO,
      DASH_FS_LABEL,DASH_FS_CAPTION,DASH_FS_SMALL,DASH_C_TEXT,DASH_C_TEXT2,DASH_C_TEXT3,DASH_C_ACCENT,DASH_C_OK,
      DASH_BLEND_ADD,DASH_BLEND_NORMAL,DASH_DEV_READY,DASH_DEV_PROBE,DASH_DEV_ABSENT};
typedef struct {char name[MAX_NAME];u8 dir;} PickEntry;
static struct {int active,gen,loading,mode,n,ndevs,starting,start_done,start_ok,selected_count;
    char dir[MAX_PATH],start_dir[24];const char *ext;PickEntry *entries;DashDevice devs[2];char **selected;} pk;
static const unsigned *buttons;static unsigned input_index,draws;
static void dashStrCopy(char*out,const char*in,int n){snprintf(out,n,"%s",in);}
static void dashToast(int kind,const char*s){(void)kind;assert(s);}
static void dashInputFlush(void){}
static int mcPathUnit(const char*p){(void)p;return -1;}
static int dashWorkerIoBusy(void){return 0;}
static int dashIopParked(void){return 0;}
static void pollRootMemoryCardDevices(void){}
static int dashDevicesMultitapChanged(DashDevice*d,int n){(void)d;(void)n;return 0;}
static int dashDashboardPending(void){return 0;}
static const char *localizeDashboardText(const char*s){return s;}
static void dashInputPoll(DashInput*in){assert(input_index<100);in->pressed=buttons[input_index++];}
static int dashDevicePathReady(const char*p){assert(p);return 1;}
static void dashDevicesRefreshMemoryCards(DashDevice*d,int n){(void)d;(void)n;}
static void dashSoundPlay(int id){(void)id;}
static int listCenteredTop(int s,int n,int rows){(void)s;(void)n;(void)rows;return 0;}
static float listSlideTop(float old,int top,int sel,int n,int rows,float dt){(void)old;(void)sel;(void)n;(void)rows;(void)dt;return top;}
static float dashVisualUpdate(void){return .016f;}
static float dashApproach(float a,float b,float c,float dt){(void)a;(void)c;(void)dt;return b;}
static float dashAnimSpeed(void){return 1;}
static float dashEaseOutCubic(float x){return x;}
static int pickIsMisc(const char*p){(void)p;return 0;}
static void filerLaunchElfFolder(char*p,int n){snprintf(p,n,"mass:/A/");}
static void pickStartDone(const char*p,int ok,void*arg){(void)p;(void)ok;(void)arg;}
static int dashDevicesStart(DashDevice*d,void(*fn)(const char*,int,void*),void*arg){(void)d;(void)fn;(void)arg;return -1;}
static void pickOpen(const char*p){
    snprintf(pk.dir,sizeof(pk.dir),"%s",p);pk.n=0;pk.loading=0;
    if(!*p){pk.ndevs=1;pk.devs[0].state=DASH_DEV_READY;strcpy(pk.devs[0].browse,"mass:/A/");return;}
    if(!strcmp(p,"mass:/A/")){
        strcpy(pk.entries[0].name,"sub");pk.entries[0].dir=1;
        strcpy(pk.entries[1].name,"A.psu");pk.entries[1].dir=0;
        strcpy(pk.entries[2].name,"B.psu");pk.entries[2].dir=0;pk.n=3;
    }else{assert(!strcmp(p,"mass:/A/sub/"));strcpy(pk.entries[0].name,"C.psu");pk.entries[0].dir=0;pk.n=1;}
}
#define dashRenderScene() ((void)0)
#define dashModalBackdrop(a) ((void)(a))
#define dashGfxPushAlpha(a) ((void)(a))
#define dashGfxPushOffset(a,b) ((void)(a),(void)(b))
#define dashModalPanel(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
#define dashIcon(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
static float dashTextFit(float a,float b,float c,const char*d,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
#define dashTextRight(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
#define dashText(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
#define dashTextCenter(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
#define dashGfxScissor(a,b,c,d) ((void)(a),(void)(b),(void)(c),(void)(d))
#define dashDrawSelection(a,b,c,d,e,f) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e),(void)(f))
#define dashDrawDeviceRow(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
#define dashGfxScissorReset() ((void)0)
#define dashDrawSpinner(a,b,c,d) ((void)(a),(void)(b),(void)(c),(void)(d))
#define dashGfxBlend(a) ((void)(a))
#define dashDrawScrollbar(a,b,c,d,e,f) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e),(void)(f))
#define dashGfxPopOffset() ((void)0)
#define dashGfxPopAlpha() ((void)0)
#define dashGfxPresent() (draws++)
static int dashItemColor(int sel){return sel;}
static int genCmpFileExt(const char*n,const char*ext){(void)n;(void)ext;return 0;}
static float dashDrawButton(float x,float y,int icon,const char*label){(void)x;(void)y;(void)icon;(void)label;return 18;}
static float dashTextS(float x,float y,const char*s,int size,int c){(void)y;(void)size;(void)c;return x+strlen(s)*7;}
static float dashButtonTextY(float y,int size){(void)size;return y;}
static int dashOkIcon(void){return DI_BTN_CROSS;}
static int dashBackIcon(void){return DI_BTN_CIRCLE;}
'''
code = code.replace("@@ICONS@@", icon_enum)
for signature in ('static void pickParent(', 'static int pickSelectionIndex(', 'static int pickToggle(',
                  'static int pickPath(', 'int dashPickPath(', 'int dashPickFiles('):
    code += function(signature)
code += r'''
static int pick(const unsigned*keys,char***paths){buttons=keys;input_index=0;int n=dashPickFiles("PSUs","mass:/A/","PSU",paths);assert(!pk.active&&!pk.entries&&!pk.selected&&!pk.selected_count);return n;}
static void release(char**paths,int n){for(int i=0;i<n;i++)free(paths[i]);free(paths);}
int main(void){
    char **paths=NULL;
    unsigned two[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_FAV,DASH_IN_DOWN,DASH_IN_FAV,DASH_IN_START};
    assert(pick(two,&paths)==2&&!strcmp(paths[0],"mass:/A/A.psu")&&!strcmp(paths[1],"mass:/A/B.psu"));release(paths,2);
    unsigned across[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_OK,DASH_IN_UP,DASH_IN_OK,DASH_IN_DOWN,DASH_IN_FAV,DASH_IN_BACK,DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_FAV,DASH_IN_START};
    assert(pick(across,&paths)==1&&!strcmp(paths[0],"mass:/A/sub/C.psu"));release(paths,1);
    unsigned cancel[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_FAV,DASH_IN_MENU};paths=(void*)1;
    assert(!pick(cancel,&paths)&&!paths);
    unsigned direct[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_START};assert(pick(direct,&paths)==1&&!strcmp(paths[0],"mass:/A/A.psu"));release(paths,1);
    unsigned toggle[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_OK,DASH_IN_OK,DASH_IN_START};assert(pick(toggle,&paths)==1);release(paths,1);
    unsigned single[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_OK};buttons=single;input_index=0;char out[MAX_PATH];
    assert(dashPickPath("One","mass:/A/",DASH_PICK_FILE,"PSU",out,sizeof(out))==1&&!strcmp(out,"mass:/A/A.psu"));
    unsigned relative[]={DASH_IN_DOWN,DASH_IN_DOWN,DASH_IN_R3};buttons=relative;input_index=0;
    assert(dashPickPath("One","mass:/A/",DASH_PICK_FILE,"PSU",out,sizeof(out))==1&&!strcmp(out,"uLE:/A.psu"));
    assert(draws>10);
    puts("PASS: production picker selects/toggles multiple PSUs across folders, preserves order, starts a focused single file, cancels/frees selections and retains single-file/R3 behavior");
}
'''
with tempfile.TemporaryDirectory(prefix='picker-multi-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                    '-I', str(root / 'include'), str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True, timeout=10)
