"""Exercise the production section routing after Home+Apps+Files consolidation."""
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
assert 'dashOpenBrowser(setting->Misc)' in tools
assert 'TA_MISC_FOLDER' in tools
assert 'CHIP_LAST_FOLDER' in home and 'CHIP_LIBRARY' in home
assert 'dashOpenBrowser(dir);' in home
assert 'dashAppsScreen.input(in)' in home and 'dashAppsScreen.draw()' in home
assert 'dashAppsScreen.update(dt)' in home and 'dashAppsScreen.hints()' in home
assert 'library_open = 0;' in home and 'if (in->pressed & DASH_IN_BACK)' in home
assert 'DASH_VIEW_COUNT' in apps and 'FILTER_COUNT' in apps
assert '"Home", "Tools", "Settings"' in settings
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
static int section = DASH_SEC_HOME,prev_section=DASH_SEC_HOME,nav_section=DASH_SEC_HOME;
static float section_anim=1.0f;
static int section_dir=1, leave_ok=1, opened, enter_count, sound_count, focus_count;
static int dashConfirmLeave(void) { return leave_ok; }
static void dashOpenBrowser(const char *path) { assert(path[0]==0); opened++; }
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
    assert(opened==1 && section==DASH_SEC_HOME && nav_section==DASH_SEC_FILES);
    assert(enter_count==0 && sound_count==0);
    assert(dashAdjacentSection(section,+1)==DASH_SEC_TOOLS);
    assert(dashAdjacentSection(section,-1)==DASH_SEC_HOME);
    dashSwitchSection(DASH_SEC_TOOLS);
    assert(section==DASH_SEC_TOOLS && enter_count==1 && nav_section==DASH_SEC_TOOLS);
    assert(dashAdjacentSection(section,-1)==DASH_SEC_FILES);
    /* Cancel any pending edits before opening browser. */
    leave_ok=0;
    dashSwitchSection(DASH_SEC_FILES);
    assert(opened==1 && section==DASH_SEC_TOOLS && nav_section==DASH_SEC_TOOLS);
    leave_ok=1;
    dashSwitchSection(DASH_SEC_FILES);
    assert(opened==2 && section==DASH_SEC_TOOLS && nav_section==DASH_SEC_FILES);
    assert(dashAdjacentSection(section,+1)==DASH_SEC_TOOLS);
    dashSwitchSection(DASH_SEC_APPS);
    assert(section==DASH_SEC_HOME && nav_section==DASH_SEC_HOME);
    assert(enter_count==2);
    assert(dashAdjacentSection(section,-1)==DASH_SEC_SETTINGS);
    dashSwitchSection(DASH_SEC_SETTINGS);
    assert(section==DASH_SEC_SETTINGS);
    dashSwitchSection(DASH_SEC_SETTINGS);
    assert(enter_count==3);
    puts("PASS: unified Home, Files direct browser, transient shoulder navigation and leave cancel");
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
