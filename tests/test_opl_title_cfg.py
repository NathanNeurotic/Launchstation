"""Guard OPL title.cfg parsing semantics used by the dashboard app library."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/dash_lib.c").read_text()

# OPL distinguishes lowercase metadata keys from similarly named descriptive keys.
assert 'm->opl_style && !strcmp(key, "boot")' in src
assert 'm->opl_style && !strcmp(key, "title")' in src
assert '!m->opl_style && (!stricmp(key, "title") || !stricmp(key, "name"))' in src

opl = src[src.index("static int metaKv("):src.index("static void splitPath(", src.index("static int metaKv("))]
assert 'm->opl_style && !stricmp(key, "boot")' not in opl
assert 'm->opl_style && !stricmp(key, "title")' not in opl

print("PASS: OPL title.cfg uses exact lowercase title/boot while wLE sidecars remain case-insensitive")

# Compile and run the real path/edit functions: Edit metadata must target title.cfg.
import shutil
import subprocess
import tempfile

apps = (root / "src/dash_apps.c").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 1024
#define DASH_PATH_MAX 256
#define DASH_TITLE_MAX 64
#define FIO_O_RDONLY 1
#define DI_WARNING 1
typedef struct { char path[DASH_PATH_MAX], title[DASH_TITLE_MAX]; int flags; } DashApp;
static int exists, confirm, ready, in_lib, write_fail;
static int opens, closes, writes, edits, refreshes, prompts, warnings;
static char contents[1024], expected[MAX_PATH];
static void dashStrCopy(char *o, const char *i, int n) { snprintf(o, n, "%s", i); }
static const char *localizeDashboardText(const char *t) { return t; }
static int dashWorkerAwaitAll(const char *t) { (void)t; return ready; }
static void filerRootMcPollIdle(void) {}
static int genOpen(const char *p, int m) { assert(m == FIO_O_RDONLY && !strcmp(p, expected)); opens++; return exists ? 0 : -2; }
static int genClose(int fd) { assert(fd == 0); closes++; return 0; }
static int dashModalConfirm(const char *t, const char *b, const char *ok, int back) {
    (void)b; (void)ok; (void)back; assert(!strcmp(t, "Create title.cfg?")); prompts++; return confirm;
}
static int dashWriteFile(const char *p, const void *t, int n) {
    assert(!exists && !strcmp(p, expected) && n == (int)strlen(t) && n < (int)sizeof(contents));
    writes++; if (write_fail) return -1; memcpy(contents, t, n + 1); exists = 1; return 0;
}
static void dashModalMessage(int i, const char *t, const char *b, const char *d) {
    (void)i; (void)t; (void)b; assert(!strcmp(d, expected)); warnings++;
}
static int dashOpenTextEditor(const char *p) { assert(exists && !strcmp(p, expected)); edits++; return 1; }
static int dashLibIndexOf(const DashApp *a) { (void)a; return in_lib ? 0 : -1; }
static void dashLibRefreshApp(DashApp *a) { (void)a; refreshes++; }
'''
code += function(src, "static void splitPath(")
code += function(src, "int dashLibMetadataPath(")
code += function(apps, "static void editMetadata(")
code += r'''
static void reset(void) {
    exists = write_fail = opens = closes = writes = edits = refreshes = prompts = warnings = 0;
    confirm = ready = in_lib = 1; contents[0] = 0;
    strcpy(expected, "usb0:/APPS/TOOL/title.cfg");
}
int main(void) {
    const struct { const char *app, *meta; } paths[] = {
        {"usb0:/APPS/TOOL/TOOL.ELF", "usb0:/APPS/TOOL/title.cfg"},
        {"mc1:/BOOT/BOOT.ELF", "mc1:/BOOT/title.cfg"},
        {"hdd1:/+APPS/Tool/BOOT.ELF", "hdd1:/+APPS/Tool/title.cfg"},
        {"host:APP.ELF", "host:title.cfg"},
        {"APP.ELF", "title.cfg"}
    };
    DashApp app = {0}; char out[MAX_PATH];
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        snprintf(app.path, sizeof(app.path), "%s", paths[i].app);
        assert(!dashLibMetadataPath(&app, out, sizeof(out)) && !strcmp(out, paths[i].meta));
        assert(dashLibMetadataPath(&app, out, 4) < 0);
    }
    strcpy(app.path, paths[0].app); strcpy(app.title, "My tool");
    /* an existing title.cfg is opened untouched, whatever the cached flag says */
    reset(); exists = 1; strcpy(contents, "title=Existing\nboot=TOOL.ELF\n");
    editMetadata(&app);
    assert(opens == 1 && closes == 1 && !writes && !prompts && edits == 1 && refreshes == 1);
    assert(!strcmp(contents, "title=Existing\nboot=TOOL.ELF\n"));
    /* a missing one is created with this ELF's boot entry, never a second file */
    reset(); app.flags = 1; editMetadata(&app);
    assert(writes == 1 && prompts == 1 && edits == 1 && refreshes == 1);
    assert(strstr(contents, "title = My tool\r\n") && strstr(contents, "boot = TOOL.ELF\r\n"));
    reset(); confirm = 0; editMetadata(&app); assert(prompts == 1 && !writes && !edits);
    reset(); ready = 0; editMetadata(&app); assert(!opens && !writes && !edits);
    reset(); write_fail = 1; editMetadata(&app); assert(warnings == 1 && !edits && !refreshes);
    reset(); exists = 1; in_lib = 0; editMetadata(&app); assert(edits == 1 && !refreshes);
    puts("ok");
}
'''
with tempfile.TemporaryDirectory(prefix="title-cfg-") as folder:
    path = Path(folder)
    (path / "t.c").write_text(code)
    subprocess.run([shutil.which("cc") or shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "t.c"), "-o", str(path / "t")], check=True)
    subprocess.run([str(path / "t")], check=True, timeout=10)
print("PASS: Edit metadata targets title.cfg, keeps an existing one, and creates it with boot=")
