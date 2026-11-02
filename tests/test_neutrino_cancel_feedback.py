"""Execute dashboard launch feedback for Neutrino cancellation and real failures.

A launch whose file is not found offers to remove its shortcut only when the
device is there to show the file is gone, and Cancel is preselected."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
src=(root/'src/dash_main.c').read_text()
a=src.index('static void friendlyLaunchError(')
b=src.index('\n}\n',a)+3
c=r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LNG(x) #x
#define DI_WARNING 1
#define DASH_APP_TRANSIENT 1
typedef struct { char path[128],title[64]; int flags; } DashApp;
static int warnings,removals,confirms,confirm_danger=-1,confirm_answer=1,device_ready=1;
static int IsNeutrinoIsoPath(const char *p) { return strstr(p,".iso")!=NULL; }
static void dashLibDeviceLabel(const char *p,char *out,int n) { (void)p;snprintf(out,n,"storage"); }
static void dashModalMessage(int i,const char *title,const char *body,const char *detail) {
 assert(i==DI_WARNING && title && body && detail);warnings++;
}
static int dashStoreIsFavorite(const char *p) { (void)p;return 0; }
static int dashStoreIsPinned(const char *p) { (void)p;return 0; }
/* not static: a revision that does not call it still compiles */
int dashDevicePathReady(const char *p) { (void)p;return device_ready; }
/* answers as the user does; danger preselects Cancel, so confirming the
 * default then keeps the shortcut */
static int dashModalConfirm(const char *t,const char *b,const char *o,int d) {
 (void)t;(void)b;(void)o;confirms++;confirm_danger=d;return confirm_answer;
}
static void dashStoreRemoveEverywhere(const char *p) { (void)p;removals++; }
"""+src[a:b]+r"""
int main(void) {
 DashApp app={"usb0:/game.iso","Game",DASH_APP_TRANSIENT};
 friendlyLaunchError(&app,"");friendlyLaunchError(&app,NULL);
 assert(warnings==0 && removals==0);
 friendlyLaunchError(&app,"Neutrino not found");assert(warnings==1 && removals==0);
 /* No drive, card or running stack: the warning, but no offer to remove. */
 device_ready=0;friendlyLaunchError(&app,"is_Not_Found");
 if(warnings!=2||confirms||removals){
  fprintf(stderr,"FAIL a file on a device that is not there offered removal (%d) and removed the shortcut (%d)\n",confirms,removals);
  return 1;
 }
 /* On a device that is there, removal is offered with Cancel preselected. */
 device_ready=1;friendlyLaunchError(&app,"is_Not_Found");
 if(warnings!=3||confirms!=1||confirm_danger!=1){
  fprintf(stderr,"FAIL removal offered %d time(s), danger %d (Remove preselected when 0)\n",confirms,confirm_danger);
  return 1;
 }
 assert(removals==1);
 confirm_answer=0;friendlyLaunchError(&app,"is_Not_Found");assert(warnings==4 && confirms==2 && removals==1);
 strcpy(app.path,"usb0:/app.elf");
 friendlyLaunchError(&app,"");assert(warnings==5 && removals==1);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='neutrino-cancel-') as folder:
 p=Path(folder);(p/'test.c').write_text("#define localizeDashboardText(s) (s)\n" + c);exe=p/'test.exe'
 subprocess.run([shutil.which('gcc') or shutil.which('cc'),'-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=10)
print('PASS: cancelled Neutrino launch is quiet; actual errors and ELF fallback still warn; '
      'removal is offered only on a device that is there, with Cancel preselected')
