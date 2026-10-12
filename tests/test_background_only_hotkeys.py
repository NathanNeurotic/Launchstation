"""Execute the production HOME background-only / SELECT quick-launch input.

Uses the actual quickLaunchInput() and main-loop dispatch snippet, with mock
pad state and launch targets. No hardware, filesystem or output is accessed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
main = (root / "src/dash_main.c").read_text()


def function(src, signature):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 1
    pos = brace + 1
    while depth:
        if src[pos] == "{":
            depth += 1
        elif src[pos] == "}":
            depth -= 1
        pos += 1
    return src[start:pos]


start = main.index("/* R3 is unused by HOME navigation;")
end = main.index("/* Main dashboard screens do not run through", start)
dispatch = main[start:end]

code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u32 raw_new, pressed; int any; } DashInput;
#define PAD_SELECT 0x001u
#define PAD_R3 0x002u
#define DASH_IN_SELECT 0x001u
#define DASH_IN_R3 0x002u
#define DASH_IN_UP 0x004u
#define DASH_SEC_HOME 0
#define DASH_SND_CONFIRM 1
#define DASH_TOAST_INFO 1
#define SETTING_LK_R3 9
#define SETTING_LK_SELECT 10

static int boot_phase, section = DASH_SEC_HOME, background_only;
static u32 paddata;
static u64 now_ms = 1000;
static int launches, flushes, sounds;
static u64 Timer(void) { return now_ms; }
static void dashInputFlush(void) { flushes++; }
static void dashSoundPlay(int s) { (void)s; sounds++; }
static void dashToast(int t, const char *s) { (void)t; (void)s; }
static void runLaunchKey(int key) { assert(key == SETTING_LK_R3); launches++; }
static int lkUsable(int key) { return key == SETTING_LK_R3; }
static const struct { u32 mask; int lk; } lk_map[] = {
    {PAD_R3, SETTING_LK_R3}, {PAD_SELECT, SETTING_LK_SELECT}
};
#define LK_MAP_N 2
static int ql_down, ql_used;
static u64 ql_since;
"""
code += function(main, "static void quickLaunchInput(")
code += "\nstatic void dispatch(DashInput *event)\n{\n\tDashInput in = *event;\n"
code += dispatch
code += "\t*event = in;\n}\n"
code += r"""
static void press(DashInput *in, u32 raw, u32 pressed, u32 held) {
    in->raw_new = raw;
    in->pressed = pressed;
    in->any = !!(raw || pressed);
    paddata = held;
    dispatch(in);
}
int main(void) {
    DashInput in = {0};
    /* R3 toggles the background without inadvertently launching anything. */
    press(&in, PAD_R3, DASH_IN_R3, PAD_R3);
    assert(background_only && launches == 0);
    assert(!in.any && !in.pressed);
    /* Normal HOME navigation is ignored while background-only is active. */
    press(&in, 0, DASH_IN_UP, 0);
    assert(background_only && in.pressed == 0);
    /* SELECT starts the quick-launch chord even with the UI hidden. */
    press(&in, PAD_SELECT, DASH_IN_SELECT, PAD_SELECT);
    assert(ql_down == 1 && background_only);
    now_ms += 40;
    press(&in, PAD_R3, DASH_IN_R3, PAD_SELECT | PAD_R3);
    assert(launches == 1 && !ql_down && background_only);
    assert(!in.any && !in.pressed);
    /* Release SELECT and restore the dashboard with R3. */
    press(&in, 0, 0, 0);
    press(&in, PAD_R3, DASH_IN_R3, PAD_R3);
    assert(!background_only && launches == 1);
    /* A normal SELECT + R3 shortcut remains functional in normal mode. */
    press(&in, PAD_SELECT, DASH_IN_SELECT, PAD_SELECT);
    now_ms += 40;
    press(&in, PAD_R3, DASH_IN_R3, PAD_SELECT | PAD_R3);
    assert(launches == 2 && !background_only);
    puts("PASS: HOME R3 background toggle and SELECT+R3 launch chord");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if compiler is None:
    raise SystemExit("host compiler required")
with tempfile.TemporaryDirectory(prefix="launchstation-bg-hotkeys-") as t:
    src = Path(t) / "hotkeys.c"
    binary = Path(t) / "hotkeys"
    src.write_text(code)
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", str(src),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
