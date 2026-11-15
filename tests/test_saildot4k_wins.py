"""Host checks for ported/adapted UI, input, icon, and shutdown wins from saildot4k."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("A host C compiler is required")


def function(source, signature):
    start = source.index(signature)
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',
                         source[start:], re.S)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:start + token.end()] + "\n"
    raise AssertionError("Unclosed function: " + signature)


def run_c(name, source):
    with tempfile.TemporaryDirectory(prefix=f"{name}-") as folder:
        c = Path(folder) / f"{name}.c"
        exe = Path(folder) / f"{name}.exe"
        c.write_text(source, encoding="utf-8")
        subprocess.run(
            [compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             str(c), "-o", str(exe), "-lm"],
            check=True,
        )
        subprocess.run([str(exe)], check=True, timeout=10)


gfx = (root / "src/dash_gfx.c").read_text(encoding="utf-8")
# Keep the small textured border from master: four texture draws per border,
# instead of emitting up to 96 procedural corner quads every frame.
assert "static GSTEXTURE tex_ring;" in gfx
assert "panelCorners(&tex_ring" in gfx

hdr = (root / "include/dash_internal.h").read_text(encoding="utf-8")
icons = (root / "src/dash_icons.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
apps = (root / "src/dash_apps.c").read_text(encoding="utf-8")
neutrino = (root / "src/gui_neutrino.c").read_text(encoding="utf-8")
enum = hdr[hdr.rfind("enum DashIcon", 0, hdr.index("DI_HOME = 0,")):]
enum = enum[:enum.index("};") + 2]
body = icons[icons.index("typedef struct\n{\n\tfloat x, y;\n} V2;"):icons.index("static inline int clutSwizzle")]
icons_test = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
static float dashClampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
""" + enum + "\n" + body + r"""
static int renderTile(int id, u8 out[32 * 32]) {
    int lit = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            V2 p = {(x + 0.5f) / 16.0f - 1.0f, (y + 0.5f) / 16.0f - 1.0f};
            u8 a = (u8)(dashClampf(0.5f - iconSdf(id, p) * 16.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
            out[y * 32 + x] = a;
            if (a > 127) lit++;
        }
    return lit;
}
int main(void) {
    const int ids[] = {DI_SOUND, DI_KEYBOARD, DI_HIDE, DI_UNDO, DI_PLAY, DI_KEY, DI_CLOSE, DI_REFRESH};
    u8 tiles[8][1024];
    for (int i = 0; i < 8; i++) {
        int lit = renderTile(ids[i], tiles[i]);
        assert(lit > 90 && lit < 900);
        for (int j = 0; j < i; j++)
            assert(memcmp(tiles[i], tiles[j], 1024) != 0);
    }
    puts("PASS: DI_SOUND, DI_KEYBOARD, DI_HIDE, DI_UNDO rasterize cleanly and stay distinct");
    return 0;
}
"""
run_c("icons_new", icons_test)
assert '[P_SOUND] = PAGE("Sound", DI_SOUND,' in settings
assert '[P_KEYBOARD] = PAGE("USB keyboard", DI_KEYBOARD,' in settings
assert "S_REBOOT_ELF ? DI_UNDO" in settings
assert "S_CLEAR_RECENT ? DI_CLOSE" in settings
assert "S_RESCAN ? DI_REFRESH" in settings
assert apps.count("DI_HIDE") >= 2
assert "items[NEUTRINO_CANCEL + 1].icon = DI_UNDO;" in neutrino

widgets = (root / "src/dash_widgets.c").read_text(encoding="utf-8")
input_enum_start = hdr.rfind("enum {", 0, hdr.index("DASH_IN_UP"))
input_enum = hdr[input_enum_start:hdr.index("};", input_enum_start) + 2]
ip_test = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef struct { u32 pressed; } DashInput;
""" + input_enum + r"""
static struct { u32 pressed, shoulders; } events[32];
static int count, idx, flushes, sounds;
static u32 cur_shoulders;
static void push(u32 pressed, u32 shoulders) { events[count].pressed = pressed; events[count++].shoulders = shoulders; }
static void dashInputPoll(DashInput *in) {
    assert(idx < count);
    in->pressed = events[idx].pressed;
    cur_shoulders = events[idx++].shoulders;
}
static u32 dashInputShoulderRepeat(void) { return cur_shoulders; }
static void dashInputFlush(void) { flushes++; }
enum { DASH_SND_CURSOR, DASH_SND_CONFIRM, DASH_SND_CANCEL };
static void dashSoundPlay(int s) { (void)s; sounds++; }
static int dashDashboardPending(void) { return 0; }
static const char *localizeDashboardText(const char *s) { return s; }
#define DASH_VW 640
#define DASH_VH 448
enum { DI_CLOCK, DI_NETWORK, DI_BTN_LEFT, DI_BTN_RIGHT, DI_BTN_UP, DI_BTN_DOWN,
       DASH_FS_LABEL, DASH_FS_CAPTION, DASH_FS_BODY, DASH_FS_SMALL,
       DASH_C_TEXT, DASH_C_TEXT2, DASH_C_TEXT3, DASH_C_ACCENT };
static float dashVisualUpdate(void) { return 0.016f; }
static float dashAnimSpeed(void) { return 1.0f; }
static float dashApproach(float a, float b, float s, float dt) { (void)a; (void)s; (void)dt; return b; }
static float dashEaseOutCubic(float a) { return a; }
static int dashItemColor(int s) { return s; }
static int DC(int r, int g, int b, int a) { return r + g + b + a; }
static void dashRenderScene(void) {}
static void dashModalBackdrop(float a) { (void)a; }
static void dashGfxPushAlpha(float a) { (void)a; }
static void dashGfxPushOffset(float x, float y) { (void)x; (void)y; }
static void dashModalPanel(float x, float y, float w, float h, float a) { (void)x; (void)y; (void)w; (void)h; (void)a; }
static void dashIcon(int i, float x, float y, float s, int c) { (void)i; (void)x; (void)y; (void)s; (void)c; }
static float dashTextS(float x, float y, const char *s, float k, int c) { (void)y; (void)k; (void)c; (void)s; return x + 10; }
static void dashTextFit(float x, float y, float w, const char *s, float k, int c) { (void)x; (void)y; (void)w; (void)s; (void)k; (void)c; }
static void dashDrawSelection(float x, float y, float w, float h, float r, float s) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)s; }
static void dashPanel(float x, float y, float w, float h, float r, int c) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)c; }
static void dashTextCenter(float x, float y, const char *s, float k, int c) { (void)x; (void)y; (void)s; (void)k; (void)c; }
static float dashDrawButton(float x, float y, int i, const char *k) { (void)x; (void)y; (void)i; (void)k; return 22.0f; }
static int dashOkIcon(void) { return 0; }
static int dashBackIcon(void) { return 0; }
static void dashModalHintRow(float x, float y, int a, const char *b, int c, const char *d) { (void)x; (void)y; (void)a; (void)b; (void)c; (void)d; }
static void dashGfxPopOffset(void) {}
static void dashGfxPopAlpha(void) {}
static void dashGfxPresent(void) {}
""" + function(widgets, "static void ipParse(") + function(widgets, "int dashModalIp(") + r"""
int main(void) {
    char ip[16] = "0.255.5.250";
    push(DASH_IN_DOWN, 0);
    push(DASH_IN_RIGHT, 0); push(DASH_IN_UP, 0);
    push(DASH_IN_RIGHT, 0); push(0, DASH_IN_L1);
    push(DASH_IN_RIGHT, 0); push(0, DASH_IN_R1);
    push(DASH_IN_OK, 0);
    assert(dashModalIp("IP address", NULL, ip, sizeof(ip)));
    assert(!strcmp(ip, "255.0.251.4"));
    assert(idx == count && flushes == 2);
    puts("PASS: dashModalIp wraps each octet modulo 256 and honors dashInputShoulderRepeat");
    return 0;
}
"""
run_c("ip_wrap", ip_test)

font = (root / "src/dash_font.c").read_text(encoding="utf-8")
scroll_test = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int DashColor;
#define DASH_FS_SMALL 0.8f
#define DASH_FS_LABEL 1.0f
#define DASH_FS_CAPTION 0.6f
typedef struct { char path[64], title[64]; } DashApp;
static int pushes, pops;
static float last_x, last_offset_x;
static float dashClampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float dashTextWidth(const char *s, float scale) { return (float)strlen(s) * 10.0f * scale; }
static float dashTextLineH(float scale) { return 20.0f * scale; }
static float dashTextS(float x, float y, const char *s, float scale, DashColor c) {
    (void)y; (void)s; (void)scale; (void)c;
    last_x = x;
    return x;
}
static void dashGfxScissorPush(float x, float y, float w, float h) { (void)x; (void)y; (void)w; (void)h; pushes++; }
static void dashGfxScissorPop(void) { pops++; }
""" + function(font, "void dashTextScrollProgress(") + function(apps, "static float listScrollProgress(") + r"""
int main(void) {
    DashApp short_app = {"mc0:/BOOT/APP.ELF", "Short"};
    DashApp long_app = {"mass0:/APPS/VERY_LONG_APPLICATION_FOLDER/LONG_APP.ELF", "Very Long Application Title"};
    dashTextScrollProgress(100.0f, 10.0f, 200.0f, "Short", 1.0f, 0, 0.5f);
    assert(pushes == 0 && pops == 0 && last_x == 100.0f);
    dashTextScrollProgress(100.0f, 10.0f, 100.0f, "01234567890123456789", 1.0f, 0, 0.5f);
    last_offset_x = last_x;
    assert(pushes == 1 && pops == 1 && fabsf(last_offset_x - 50.0f) < 1e-4f);
    assert(listScrollProgress(&short_app, 226.0f, 260.0f, 4.0f) == 0.0f);
    assert(listScrollProgress(&long_app, 180.0f, 180.0f, 2.5f) == 0.0f);
    float mid = listScrollProgress(&long_app, 180.0f, 180.0f, 5.0f);
    assert(mid > 0.0f && mid < 1.0f);
    float end_dwell = listScrollProgress(&long_app, 180.0f, 180.0f, 7.5f);
    assert(end_dwell == 1.0f);
    float restarted = listScrollProgress(&long_app, 180.0f, 180.0f, 10.0f);
    assert(restarted == 0.0f);
    puts("PASS: synchronized listScrollProgress and dashTextScrollProgress clamp and scissor cleanly");
    return 0;
}
"""
run_c("scroll_progress", scroll_test)

browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
clip_test = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 256
#define MAX_NAME 128
typedef struct { char name[MAX_NAME]; } FILEINFO;
static char clipPath[MAX_PATH];
static FILEINFO clipFiles[4];
static int nclipFiles;
static void dashStrCopy(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s ? s : ""); }
""" + function(browser, "static void browserDisplayPath(") + function(browser, "static void clipboardSummary(") + r"""
int main(void) {
    char out[256];
    nclipFiles = 0; clipboardSummary(out, sizeof(out)); assert(!out[0]);
    nclipFiles = 1; strcpy(clipPath, "usb0:/APPS"); strcpy(clipFiles[0].name, "BOOT.ELF");
    clipboardSummary(out, sizeof(out)); assert(!strcmp(out, "mass0:/APPS/BOOT.ELF"));
    strcpy(clipPath, "mc0:/"); strcpy(clipFiles[0].name, "SYS-CONF");
    clipboardSummary(out, sizeof(out)); assert(!strcmp(out, "mc0:/SYS-CONF"));
    strcpy(clipPath, "hdd0:__common"); strcpy(clipFiles[0].name, "OPL");
    clipboardSummary(out, sizeof(out)); assert(!strcmp(out, "hdd0:__common/OPL"));
    nclipFiles = 3; strcpy(clipPath, "usb1:/SAVES/");
    clipboardSummary(out, sizeof(out)); assert(!strcmp(out, "Assortment in mass1:/SAVES/"));
    puts("PASS: clipboardSummary formats source path and assortment cleanly");
    return 0;
}
"""
run_c("clipboard_summary", clip_test)
assert "dashIconMany(browser_cut ? DI_CUT : DI_COPY," in browser
assert "!fb_tab_active || !LaunchArgsPending()" in browser

files_src = (root / "src/dash_files.c").read_text(encoding="utf-8")
open_dev = function(files_src, "static void openDevice(")
dev_sel = function(files_src, "static int devSelectable(")
assert "d->icon == DI_MC" in open_dev
assert "dashOpenBrowser(d->browse);" in open_dev
assert "devs[idx].icon == DI_MC" in dev_sel
assert "d->icon == DI_MC" in function(browser, "static void activate(")
assert '(!strncmp(start_dir, "mc0:", 4) || !strncmp(start_dir, "mc1:", 4) || dashDevicePathReady(start_dir))' in function(browser, "static int browserOpenState(")
widgets_src = (root / "src/dash_widgets.c").read_text(encoding="utf-8")
assert "!filerDirOpened()" in function(browser, "static void listRun(")
assert "!filerDirOpened()" in function(widgets_src, "static void pickListRun(")
assert "Memory card could not be read. Try opening it again." in function(browser, "static void listDone(")
assert "Memory card could not be read. Try opening it again." in function(widgets_src, "static void pickListDone(")
filer_src = (root / "src/filer.c").read_text(encoding="utf-8")
assert "filerRootMcPresent(path[2] - '0', NULL, NULL) <= 0" in function(filer_src, "int filerDirReadNeedsPrep(")
assert "filerPrepareMemoryCardRead(path[2] - '0') == sceMcResSucceed" in function(filer_src, "int filerPrepareDirRead(")

dash_main = (root / "src/dash_main.c").read_text(encoding="utf-8")
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
shutdown_test = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static int dash_active = 1, boot_init_pending = 1, quiet = 0, statuses = 0;
static void *setting, *elisaFnt, *External_Lang_Buffer;
static int freed_order[4], frees;
static int isStartupDriverStatusQuiet(void) { return quiet; }
static void dashLoadingStatus(const char *m, int d, int t) { (void)m; (void)d; (void)t; statuses++; }
static int stopSmbServer(void) { return 0; }
static void fadeScreenToBlack(void) {}
static void dashSoundShutdown(void) {}
static void DashThemeSetLegacyEnabled(int e) { (void)e; }
static void clearScreenToBlack(void) {}
static void padPortClose(int p, int s) { (void)p; (void)s; }
static void padEnd(void) { assert(!dash_active && setting != NULL); }
static void closeKeyboardIfOpened(void) {}
static void dashGfxRestoreLegacyState(void) {}
static void dashClassicUiInstall(int n) { (void)n; }
static void trackFree(void *p) { if (p == setting) freed_order[frees++] = dash_active; free(p); }
#define free trackFree
""" + function(dash_main, "int DashIsActive(") + function(dash_main, "static void dashMsgHook(") + function(dash_main, "void DashStopUiForExec(") + function(actions, "static void cleanUpForExecInternal(") + r"""
#undef free
int main(void) {
    dashMsgHook("shown"); assert(statuses == 1);
    quiet = 1; dashMsgHook("suppressed"); assert(statuses == 1);
    quiet = 0;
    setting = malloc(8); elisaFnt = malloc(8); External_Lang_Buffer = malloc(8);
    cleanUpForExecInternal(1);
    assert(!DashIsActive() && !boot_init_pending && frees == 1 && freed_order[0] == 0);
    dashMsgHook("after_stop"); assert(statuses == 1);
    puts("PASS: DashStopUiForExec stops redraws before free(setting) and dashMsgHook respects quiet/active");
    return 0;
}
"""
run_c("exec_stop", shutdown_test)
print("PASS: all saildot4k ported/adapted wins verified")
