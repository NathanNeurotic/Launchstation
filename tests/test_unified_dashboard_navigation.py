"""Exercise production section routing with Files as a first-class tab."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
main = (root / "src/dash_main.c").read_text()
home = (root / "src/dash_home.c").read_text()
tools = (root / "src/dash_tools.c").read_text()
apps = (root / "src/dash_apps.c").read_text()
settings = (root / "src/dash_settings.c").read_text()

assert "DASH_SEC_APPS" not in main[main.index("static const int visible_sections[]"):main.index("#define DASH_VISIBLE_SEC_COUNT")]
assert 'showMiscCommands();' in tools and 'dashRunMisc(paths[selected])' in tools
assert 'TA_MISC_MENU' in tools
assert 'CHIP_LAST_FOLDER' in home and 'CHIP_LIBRARY' in home
assert 'dashNavigateFilesTab(dir);' in home
assert 'dashNavigateFilesTab(dir);' in (root / "src/dash_apps.c").read_text()
assert 'dashAppsScreen.input(in)' in home and 'dashAppsScreen.draw()' in home
assert 'dashAppsScreen.update(dt)' in home and 'dashAppsScreen.hints()' in home
assert 'library_open = 0;' in home and 'if (in->pressed & DASH_IN_BACK)' in home
assert 'DASH_VIEW_COUNT' in apps and 'FILTER_COUNT' in apps
assert '"Home", "Applications", "Files", "Tools", "Settings"' in settings
assert 'S_FILES_MODE, "Files / Devices view"' not in settings

def function(source,signature):
    start=source.index(signature)
    pos=source.index("{",start)+1
    depth=1
    while depth:
        depth += (source[pos]=="{") - (source[pos]=="}")
        pos+=1
    return source[start:pos]+"\n"

defs=main[main.index("static const int visible_sections[]"):main.index("const char *dashSectionName")]
code=String = r'''
#include <assert.h>
#include <stdio.h>
typedef struct { void (*enter)(void); } DashScreen;
enum { DASH_SEC_HOME=0, DASH_SEC_APPS, DASH_SEC_FILES, DASH_SEC_DEVICES,
       DASH_SEC_TOOLS, DASH_SEC_SETTINGS, DASH_SEC_COUNT };
enum { DASH_SND_TRANSITION=1 };
static int section = DASH_SEC_HOME,prev_section=DASH_SEC_HOME;
static float section_anim=1.0f;
static int section_dir=1, leave_ok=1, enter_count, sound_count, focus_count, tab_closes;
static int dashConfirmLeave(void) { return leave_ok; }
static void dashFileBrowserTabClose(void) { assert(section==DASH_SEC_FILES); tab_closes++; }
static void dashUiFocusPoke(void) { focus_count++; }
static void dashSoundPlay(int sound) { assert(sound==DASH_SND_TRANSITION); sound_count++; }
static void entered(void) { enter_count++; }
static const DashScreen item = {entered};
static const DashScreen *const screens[DASH_SEC_COUNT] = {
    &item,&item,&item,&item,&item,&item
};
'''
code+=defs
for fn in ("int dashVisibleSectionCount(void)","int dashVisibleSectionAt(int index)",
           "int dashVisibleSectionIndex(int s)","int dashAdjacentSection(int s, int direction)",
           "void dashSwitchSection(int s)"):
    code+=function(main,fn)
code+=r'''
int main(void) {
    assert(dashVisibleSectionCount()==4);
    assert(dashVisibleSectionAt(0)==DASH_SEC_HOME);
    assert(dashVisibleSectionAt(1)==DASH_SEC_FILES);
    assert(dashVisibleSectionAt(2)==DASH_SEC_TOOLS);
    assert(dashVisibleSectionAt(3)==DASH_SEC_SETTINGS);
    assert(dashVisibleSectionIndex(DASH_SEC_APPS)==0);
    assert(dashVisibleSectionIndex(DASH_SEC_DEVICES)==1);
    assert(dashAdjacentSection(section,+1)==DASH_SEC_FILES);
    dashSwitchSection(DASH_SEC_FILES);
    assert(section==DASH_SEC_FILES && tab_closes==0);
    assert(enter_count==1 && sound_count==1);
    assert(dashAdjacentSection(section,+1)==DASH_SEC_TOOLS);
    assert(dashAdjacentSection(section,-1)==DASH_SEC_HOME);
    /* Returning to the same tab doesn't open a second browser. */
    dashSwitchSection(DASH_SEC_FILES);
    assert(enter_count==1 && tab_closes==0);
    dashSwitchSection(DASH_SEC_TOOLS);
    assert(section==DASH_SEC_TOOLS && enter_count==2 && tab_closes==1);
    assert(dashAdjacentSection(section,-1)==DASH_SEC_FILES);
    /* Unsaved changes prevent the target tab from being initialized. */
    leave_ok=0;
    dashSwitchSection(DASH_SEC_FILES);
    assert(section==DASH_SEC_TOOLS && tab_closes==1 && enter_count==2);
    leave_ok=1;
    dashSwitchSection(DASH_SEC_FILES);
    assert(section==DASH_SEC_FILES && enter_count==3 && tab_closes==1);
    assert(dashAdjacentSection(section,+1)==DASH_SEC_TOOLS);
    dashSwitchSection(DASH_SEC_APPS);
    assert(section==DASH_SEC_HOME && tab_closes==2);
    assert(enter_count==4);
    assert(dashAdjacentSection(section,-1)==DASH_SEC_SETTINGS);
    dashSwitchSection(DASH_SEC_SETTINGS);
    assert(section==DASH_SEC_SETTINGS && enter_count==5);
    dashSwitchSection(DASH_SEC_SETTINGS);
    assert(enter_count==5);
    puts("PASS: unified Home, Files as real tab, shoulder cycling, cancel and cleanup");
}
'''
compiler=shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-unified-nav-") as tmp:
    source=Path(tmp)/"nav.c";exe=Path(tmp)/"nav"
    source.write_text(code)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    str(source),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],timeout=5,check=True)
