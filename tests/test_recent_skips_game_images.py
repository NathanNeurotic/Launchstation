"""Game images must not enter Recent applications; ELFs still do."""
from pathlib import Path
import shutil, subprocess, tempfile

src = (Path(__file__).resolve().parents[1] / "src/dash_store.c").read_text()
a = src.index("void dashStorePushRecent(")
end = src.index("{", a) + 1
depth = 1
while depth:
    depth += (src[end] == "{") - (src[end] == "}")
    end += 1
func = src[a:end]
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define DASH_PATH_MAX 64
#define DASH_MAX_RECENT 4
static struct { char recent[DASH_MAX_RECENT][DASH_PATH_MAX]; int nrecent; } dashLists;
static int saves;
static int genCmpFileExt(const char *f, const char *e) {
    const char *d = strrchr(f, '.'); return d && !strcasecmp(d + 1, e);
}
static int listFind(char l[][DASH_PATH_MAX], int n, const char *p) {
    for (int i = 0; i < n; i++) if (!strcmp(l[i], p)) return i; return -1;
}
static void dashStrCopy(char *o, const char *i, int n) { snprintf(o, n, "%s", i); }
static void dashStoreSaveLists(void) { saves++; }
''' + func + r'''
int main(void) {
    const char *skip[] = {"mass:/GAME.ISO", "mass:/g.iso", "mc0:/X.BIN", "hdd0:/A.cue", "mass:/VCD/S.VCD"};
    for (unsigned i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) dashStorePushRecent(skip[i]);
    assert(dashLists.nrecent == 0 && saves == 0);
    dashStorePushRecent("mass:/APPS/OPL.ELF");
    dashStorePushRecent("mass:/APPS/OPL.ELF");
    assert(dashLists.nrecent == 1 && saves == 2 && !strcmp(dashLists.recent[0], "mass:/APPS/OPL.ELF"));
    dashStorePushRecent("mass:/ISO.ELF"); /* extension, not substring */
    assert(dashLists.nrecent == 2);
    puts("ok");
}
'''
with tempfile.TemporaryDirectory() as d:
    d = Path(d); (d / "t.c").write_text(code)
    subprocess.run([shutil.which("cc") or shutil.which("gcc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(d / "t.c"), "-o", str(d / "t")], check=True)
    subprocess.run([str(d / "t")], check=True, timeout=10)
print("PASS: game images stay out of Recent applications; ELFs are still recorded")
