"""Execute restored classic setters/colour serializer and exact colour readback."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
settings=(root/'src/dash_settings.c').read_text()
header=(root/'include/launchelf.h').read_text()
colors=(root/'src/gui_colors.c').read_text()
def fn(src,sig):
 a=src.index(sig);b=src.index('{',a)+1;depth=1
 while depth:depth+=(src[b]=='{')-(src[b]=='}');b+=1
 return src[a:b]+'\n'

prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;typedef uint32_t u32;
typedef struct {unsigned char r,g,b,a;} DashColor;
#define GS_SETREG_RGBA(r,g,b,a) ((u64)(r)|((u64)(g)<<8)|((u64)(b)<<16)|((u64)(a)<<24))
'''
prefix+='enum { '+header[header.index('COLOR_BACKGR'):header.index('};',header.index('COLOR_BACKGR'))+2]+'\n'
prefix+=header[header.index('enum SETTING_LK {'):header.index('};',header.index('enum SETTING_LK {'))+2]+'\n'
prefix+=settings[settings.index('enum SetId {'):settings.index('};',settings.index('enum SetId {'))+2]+'\n'
prefix+=r'''
#define MAX_PATH 1025
#define MAX_ELF_TITLE 72
#define DASH_PICK_LAUNCH 1
#define DASH_TOAST_INFO 1
typedef struct {int id;const char *label;} SetItem;
static int locked,pick_ok=1,home_updates;
static int lkeyLocked(void){return locked;}
static const char *localizeDashboardText(const char *s){return s;}
static void startDirOf(const char *s,char *o,int n){(void)s;snprintf(o,n,"mass0:/");}
static int dashPickPath(const char *t,const char *s,int k,const char *ext,char *out,int n){(void)t;(void)s;(void)k;(void)ext;snprintf(out,n,"mass0:/BOOT.ELF");return pick_ok;}
static void mcAlias(char *p,int n){(void)p;(void)n;}
static void dashStrCopy(char *o,const char *s,int n){snprintf(o,n,"%s",s);}
static void dashToast(int k,const char *m){(void)k;(void)m;}
static int dashModalKeyboard(const char *s,char *o,int n){(void)s;(void)o;(void)n;return 0;}
static void dashHomeRebuild(void){home_updates++;}
static struct {char LK_Path[SETTING_LK_COUNT][MAX_PATH],LK_Title[SETTING_LK_COUNT][MAX_ELF_TITLE];int LK_Flag[SETTING_LK_COUNT];u64 color[8];int TV_mode,screen_x,screen_y,Popup_Opaque,Menu_Frame,Show_Titles,Hide_Paths,FB_NoIcons;} config,*setting=&config;
static DashColor classic_col[8];
static int classic_dirty;
'''
program=prefix+fn(settings,'static void classicColorStore(')+fn(settings,'static void editLaunchKey(')
for sig in ['static u32 hextou32(', 'size_t storeGuiColorsCNF(', 'int scanGuiColorsCNF(']:program+=fn(colors,sig)
setter=fn(settings,'static void setChoice(')
a=setter.index('case S_MENU_FRAME:');b=setter.index('case S_PATHPAD_LOCK:',a)
c=setter.index('case S_FB_ICONS:');d=setter.index('\n\t\t\t}',c)
program+='static void setClassic(int id,int v){switch(id){'+setter[a:b]+setter[c:d]+'}}\n'
program+=r'''
int main(void){
#ifdef _WIN32
 _set_printf_count_output(1);
#endif
 SetItem key={S_LK_0+SETTING_LK_LEFT,"Left"};
 editLaunchKey(&key,0);assert(config.LK_Flag[SETTING_LK_LEFT] && config.LK_Path[SETTING_LK_LEFT][0] && classic_dirty);
 editLaunchKey(&key,2);assert(!config.LK_Flag[SETTING_LK_LEFT] && !config.LK_Path[SETTING_LK_LEFT][0]);
 key.id=S_LK_0+SETTING_LK_RIGHT;locked=1;classic_dirty=0;
 editLaunchKey(&key,0);assert(!config.LK_Flag[SETTING_LK_RIGHT] && !classic_dirty);
 locked=0;pick_ok=0;editLaunchKey(&key,0);assert(!config.LK_Flag[SETTING_LK_RIGHT] && !classic_dirty);
 for(int v=0;v<2;v++){
  setClassic(S_MENU_FRAME,v);setClassic(S_POPUP_OPAQUE,v);
  setClassic(S_SHOW_TITLES,v);setClassic(S_HIDE_PATHS,v);setClassic(S_FB_ICONS,v);
  assert(config.Menu_Frame==v && config.Popup_Opaque==v && config.Show_Titles==v && config.Hide_Paths==v && config.FB_NoIcons==!v);
 }
 for(int i=0;i<8;i++){classic_col[i]=(DashColor){i+1,i+2,i+3,255};classicColorStore(S_C_COL0+i);}
 assert(classic_dirty);
 config.TV_mode=2;config.screen_x=-3;config.screen_y=4;
 u64 saved[8];memcpy(saved,config.color,sizeof(saved));char buf[2048];
 size_t n=storeGuiColorsCNF(buf);assert(n==strlen(buf));
 memset(&config,0,sizeof(config));
 char *line=strtok(buf,"\r\n");while(line){char *eq=strstr(line," = ");assert(eq);*eq=0;assert(scanGuiColorsCNF(line,eq+3));line=strtok(NULL,"\r\n");}
 assert(!memcmp(saved,config.color,sizeof(saved)) && config.TV_mode==2 && config.screen_x==-3 && config.screen_y==4 && config.Menu_Frame==1 && config.Popup_Opaque==1);
}
'''
with tempfile.TemporaryDirectory(prefix='classic-settings-') as td:
 p=Path(td);(p/'test.c').write_text(program)
 subprocess.run([shutil.which('cc') or shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
print('PASS: actual launch-key enable/clear/lock, classic setters, eight independent colours, serialized mode/offset/opacity/frame readback')
