"""Compile and execute the upstream Neutrino option argument builder on a host.

Unlike a textual-only check, this exercises the actual C builder used by the
PS2 executable, without needing a PS2 graphics/pad stack on the host.
"""
from pathlib import Path
import subprocess
import shutil
import tempfile

root = Path(__file__).resolve().parents[1]
gui = (root / "src/gui_neutrino.c").read_text(encoding="utf-8")
neutrino = (root / "src/neutrino_iso.c").read_text(encoding="utf-8")
makefile = (root / "Makefile").read_text(encoding="utf-8")
ps2build = (root / "ps2.yaml").read_text(encoding="utf-8")

assert '"gui_neutrino.h"' in neutrino
assert "gui_neutrino.o" in makefile
assert "src/gui_neutrino.c" in ps2build
assert "showNeutrinoLaunchOptions(&options)" in neutrino
assert "buildNeutrinoOptionArgs(options, &option_args)" in neutrino
assert "RunLoaderElfKeepIOP" in neutrino
assert neutrino.index("setting->disable_neutrino") < neutrino.index("showNeutrinoLaunchOptions(&options)")
assert neutrino.index("showNeutrinoLaunchOptions(&options)") < neutrino.index("CleanUpForKeepIopExec")
assert "if (message != NULL && message_size > 0)" in neutrino

# Extract the actual data and pure argument builder without pulling in PS2 UI.
start = gui.index("static const struct {")
end = gui.index("/* Versioned two-slot Neutrino profiles.", start)
pure_c = (
    '#include <stdio.h>\n#include <string.h>\n'
    '#include "gui_neutrino.h"\n'
    + gui[start:end]
)
# Execute the dashboard menu with scripted user choices, including cancel.
native_start = gui.index("static int showDashboardNeutrinoOptions")
native_end = gui.index("static void drawNeutrinoHeading", native_start)
pure_c += r"""
#define TRUE 1
#define FALSE 0
#define DI_INFO 0
#define DI_SETTINGS 1
#define DI_PLAY 2
#define DI_BACK 3
#define DI_STAR 4
#define DI_REFRESH 5
#define DASH_TOAST_OK 1
#define DASH_TOAST_WARN 2
static char active_profile_gameid[12];
static int profileSave(const char *id, const NEUTRINO_LAUNCH_OPTIONS *o) { (void)id; (void)o; return 1; }
static void dashToast(int kind, const char *text) { (void)kind; (void)text; }
typedef struct { int icon; const char *label; const char *detail; int disabled; } DashMenuItem;
int dashModalMenu(const char *, const char *, const DashMenuItem *, int, int);
int dashModalChoice(const char *, const char *const *, int, int);
"""
pure_c += gui[native_start:native_end]
pure_c += "int testDashboard(NEUTRINO_LAUNCH_OPTIONS *o) { return showDashboardNeutrinoOptions(o); }\n"

harness = r"""
#include <assert.h>
#include <string.h>
#include "gui_neutrino.h"
static const int *script;
static int step, choice;
int testDashboard(NEUTRINO_LAUNCH_OPTIONS *);
typedef struct { int icon; const char *label; const char *detail; int disabled; } DashMenuItem;
int dashModalMenu(const char *title, const char *subtitle, const DashMenuItem *items, int count, int selected) {
    assert(title && subtitle && selected >= 0 && selected < count && count == 11);
    for (int i = 0; i < count; i++) assert(items[i].label && !items[i].disabled);
    return script[step++];
}
int dashModalChoice(const char *title, const char *const *labels, int count, int selected) {
    assert(title && labels && count > selected);
    return choice;
}
int main(void) {
    NEUTRINO_LAUNCH_OPTIONS o = {0};
    NEUTRINO_OPTION_ARGS a;
    int n;
    n = buildNeutrinoOptionArgs(&o, &a);
    assert(n == 0); /* defaults are unmodified, never saved */

    o.game_compatibility = (1u<<0) | (1u<<2) | (1u<<3) | (1u<<5) | (1u<<7);
    n = buildNeutrinoOptionArgs(&o, &a);
    assert(n == 1 && strcmp(a.argv[0], "-gc=02357") == 0);

    o.video_mode = 5;
    o.video_compatibility = 3;
    o.debug_colors = 1;
    o.logo = 1;
    n = buildNeutrinoOptionArgs(&o, &a);
    assert(n == 4);
    assert(strcmp(a.argv[0], "-gc=02357") == 0);
    assert(strcmp(a.argv[1], "-gsm=1080ix3:3") == 0);
    assert(strcmp(a.argv[2], "-dbc") == 0);
    assert(strcmp(a.argv[3], "-logo") == 0);

    o.game_compatibility = 1u << 1; /* Neutrino dummy mode is not selectable */
    assert(buildNeutrinoOptionArgs(&o, &a) == -1);
    o.game_compatibility = 0;
    o.video_mode = 99;
    assert(buildNeutrinoOptionArgs(&o, &a) == -1);
    o.video_mode = 0;
    o.video_compatibility = -1;
    assert(buildNeutrinoOptionArgs(&o, &a) == -1);
    assert(buildNeutrinoOptionArgs(0, &a) == -1);
    assert(buildNeutrinoOptionArgs(&o, 0) == -1);
    const int run[] = {0, 4, 5, 6, 7, 8, 9};
    memset(&o, 0, sizeof(o)); script = run; step = 0; choice = 3;
    assert(testDashboard(&o) == 1 && step == 7);
    assert(o.game_compatibility == ((1u << 0) | (1u << 7)));
    assert(o.video_mode == 3 && o.video_compatibility == 3 && o.debug_colors && o.logo);
    const int cancel[] = {-1}; script = cancel; step = 0;
    assert(testDashboard(&o) == 0);
    const int submenu_cancel[] = {5, 10}; script = submenu_cancel; step = 0; choice = -1;
    assert(testDashboard(&o) == 0 && o.video_mode == 3);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="r3z-neutrino-") as folder:
    temp = Path(folder)
    (temp / "builder.c").write_text(pure_c, encoding="utf-8")
    (temp / "main.c").write_text(harness, encoding="utf-8")
    binary = temp / "test_neutrino_args"
    subprocess.run(
        [shutil.which("gcc") or shutil.which("cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I", str(root / "include"), str(temp / "builder.c"),
         str(temp / "main.c"), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)

print("R3Z Neutrino option parser: executable host C checks passed")
