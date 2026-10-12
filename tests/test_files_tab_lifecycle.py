"""Exercise the actual embedded File Browser lifecycle and USB/pad guard.

The Files destination must participate in dashboard rendering/input, not run
the modal browser while the dashboard is blocked. Re-entry must preserve the
folder/selection, closing a tab must invalidate pending async listings, and
USB folder reads must not interleave controller polling.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
browser=(root/"src/dash_browser.c").read_text()
main=(root/"src/dash_main.c").read_text()
files=(root/"src/dash_files.c").read_text()
hdr=(root/"include/dash_internal.h").read_text()
assert "const DashScreen dashFilesScreen" in browser
assert "const DashScreen dashFilesScreen" not in files
assert "if (section == DASH_SEC_FILES)" in main
assert "dashFileBrowserTabClose();" in main
assert "dashBrowserPadInputReady()" in main
assert "dashFileBrowserTabOpen(start_dir)" in main
assert "dashFileBrowser(start_dir)" in main
assert "void dashFileBrowserTabClose(void)" in hdr
assert "browserTabInput, browserTabUpdate, browserTabDraw" in browser
assert "browserProcessInput(in, NULL)" in browser
assert "browserProcessInput(&in, &running)" in browser
assert "dashPushSceneOverride(browserScene)" in browser
assert "dashPopSceneOverride();" in browser
draw=browser[browser.index("static void browserDrawBody(void)"):browser.index("static void browserScene(void)")]
assert "dashGfxBeginFrame" not in draw and "dashBgDraw()" not in draw
assert "fb_tab_active ? 74 : 58" in draw
assert "dashDrawHeader(section, section_anim)" in main
assert "if (setting->Misc[0] && !fb_tab_active)" in browser
assert "dashHomeRebuild();" not in browser[browser.index("void dashFileBrowserTabClose(void)"):browser.index("int dashBrowserPadInputReady(void)")]

def function(signature):
    start=browser.index(signature)
    pos=browser.index("{",start)+1
    depth=1
    while depth:
        depth+=(browser[pos]=="{")-(browser[pos]=="}")
        pos+=1
    return browser[start:pos]+"\n"

code=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
#define MAX_NAME 64
#define MAX_ENTRY 5
#define DASH_TOAST_ERROR 1
typedef struct { char name[MAX_NAME]; } FILEINFO;
typedef struct { char browse[MAX_PATH]; } DashDevice;
typedef struct {
    char dir[MAX_PATH];
    FILEINFO *files;
    unsigned char *mark;
    DashDevice devs[4];
    int gen, args_loaded, loading;
    int sel,n,ndevs;
} FbState;
static FbState fb;
static int fb_tab_active;
static int browser_usb_jobs,browser_usb_input_rearm;
static char fb_tab_resume[MAX_PATH];
static char fb_tab_selection[MAX_NAME];
static int busy,flush_count,clear_args_count,open_count;
static char opened[MAX_PATH], selected[MAX_NAME];
static int LaunchArgsPending(void) {return fb.args_loaded;}
static void LaunchArgsClear(void) {clear_args_count++;fb.args_loaded=0;}
static int dashWorkerIoBusy(void) {return busy;}
static void dashInputFlush(void) {assert(!busy);flush_count++;}
static void dashToast(int type,const char *msg) {(void)type;(void)msg;}
static int isMisc(const char *p) {(void)p;return 0;}
static int dashDevicePathReady(const char *p) {(void)p;return 1;}
static void dashStrCopy(char *dst,const char *src,int n) {
    snprintf(dst,(size_t)n,"%s",src);
}
static void openDir(const char *dir,const char *select) {
    open_count++;
    snprintf(opened,sizeof(opened),"%s",dir);
    snprintf(selected,sizeof(selected),"%s",select?select:"");
    snprintf(fb.dir,sizeof(fb.dir),"%s",dir);
}
'''
for signature in (
    "static int browserOpenState(const char *start_dir, const char *select)",
    "static void browserCloseState(void)",
    "static void browserTabEnter(void)",
    "void dashFileBrowserTabClose(void)",
    "int dashBrowserPadInputReady(void)",
    "void dashFileBrowserTabOpen(const char *start_dir)",
):
    code+=function(signature)
code+=r'''
int main(void) {
    assert(dashBrowserPadInputReady()==1);
    browserTabEnter();
    assert(fb_tab_active && fb.files && fb.mark);
    assert(open_count==1 && !strcmp(opened,"") && fb.gen==0);
    browser_usb_jobs=1;busy=1;
    assert(dashBrowserPadInputReady()==0 && flush_count==0);
    busy=0;browser_usb_jobs=0;browser_usb_input_rearm=1;
    assert(dashBrowserPadInputReady()==0 && flush_count==1);
    assert(dashBrowserPadInputReady()==1);
    dashFileBrowserTabOpen("usb0:/APPS/");
    assert(!strcmp(opened,"usb0:/APPS/"));
    fb.n=2;fb.sel=1;
    strcpy(fb.files[1].name,"LOADER.ELF");
    fb.args_loaded=1;fb.gen=40;
    /* A queued USB read belongs to the worker, NOT this browser
     * session. Closing/reopening the tab must never reset its PAD guard. */
    browser_usb_jobs=1;busy=1;
    dashFileBrowserTabClose();
    assert(!fb_tab_active && !fb.files && !fb.mark);
    assert(browser_usb_jobs==1 && dashBrowserPadInputReady()==0);
    assert(fb.gen==41 && clear_args_count==1);
    assert(!strcmp(fb_tab_resume,"usb0:/APPS/"));
    assert(!strcmp(fb_tab_selection,"LOADER.ELF"));
    browserTabEnter();
    assert(fb_tab_active && fb.files && fb.mark);
    assert(browser_usb_jobs==1 && dashBrowserPadInputReady()==0);
    /* Old completion arrives after the new tab has allocated its buffer. */
    busy=0;browser_usb_jobs=0;browser_usb_input_rearm=1;
    assert(dashBrowserPadInputReady()==0 && flush_count==2);
    assert(dashBrowserPadInputReady()==1);
    assert(!strcmp(opened,"usb0:/APPS/"));
    assert(!strcmp(selected,"LOADER.ELF"));
    assert(fb.gen==41);
    /* Choosing root then leaving must replace the saved path. */
    dashFileBrowserTabOpen("");
    fb.sel=0;fb.ndevs=1;
    strcpy(fb.devs[0].browse,"mc0:/");
    dashFileBrowserTabClose();
    assert(!strcmp(fb_tab_resume,""));
    assert(!strcmp(fb_tab_selection,"mc0:/"));
    browserTabEnter();
    assert(!strcmp(opened,""));
    assert(!strcmp(selected,"mc0:/"));
    dashFileBrowserTabClose();
    puts("PASS: embedded Files tab state/selection resume, async-generation cleanup, USB input serialization");
}
'''
cc=shutil.which("gcc") or shutil.which("clang")
assert cc,"Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-files-tab-") as temp:
    source=Path(temp)/"browser.c"
    binary=Path(temp)/"browser"
    source.write_text(code)
    subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(source),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],timeout=5,check=True)
