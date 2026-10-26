"""Hidden applications persist by path, independent of library scans (R3Z 0bedb64)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
store = (root / "src/dash_store.c").read_text()
apps = (root / "src/dash_apps.c").read_text()
home = (root / "src/dash_home.c").read_text()
lib = (root / "src/dash_lib.c").read_text()
filer = (root / "src/filer.c").read_text()


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


code = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define stricmp strcasecmp
#define DASH_PATH_MAX 16
#define DASH_MAX_APPS 3
static char *hidden_apps[DASH_MAX_APPS];
static int nhidden, saves;
static void dashStoreSaveLists(void) { saves++; }
"""
code += function(store, "int dashStoreIsHidden(") + function(store, "static int setHidden(const char *path, int hidden)\n{")
code += function(store, "int dashStoreSetHidden(")
code += r"""
int main(void) {
    assert(!dashStoreIsHidden(NULL) && !dashStoreIsHidden("mc0:/A.ELF"));
    assert(dashStoreSetHidden("mc0:/A.ELF", 1) == 1 && saves == 1);
    assert(dashStoreIsHidden("MC0:/a.elf")); /* paths compare case-insensitively */
    assert(dashStoreSetHidden("mc0:/a.elf", 1) == 0 && saves == 1); /* no duplicate, no save */
    assert(dashStoreSetHidden("", 1) == -1 && dashStoreSetHidden(NULL, 1) == -1);
    assert(dashStoreSetHidden("0123456789abcdef", 1) == -1); /* >= DASH_PATH_MAX */
    assert(dashStoreSetHidden("b", 1) == 1 && dashStoreSetHidden("c", 1) == 1 && nhidden == 3);
    assert(dashStoreSetHidden("d", 1) == -1 && saves == 3); /* full */
    assert(dashStoreSetHidden("b", 0) == 1 && !dashStoreIsHidden("b") && nhidden == 2);
    assert(dashStoreIsHidden("c") && dashStoreIsHidden("mc0:/A.ELF") && hidden_apps[2] == NULL);
    assert(dashStoreSetHidden("b", 0) == 0 && saves == 4);
    return 0;
}
"""
cc = shutil.which("cc") or shutil.which("gcc")
with tempfile.TemporaryDirectory(prefix="hidden-apps-") as td:
    path = Path(td)
    (path / "test.c").write_text(code)
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(path / "test.exe")], check=True)
    subprocess.run([str(path / "test.exe")], check=True)

# Persistence and view wiring.
assert 'else if (!stricmp(key, "Hidden"))\n\t\tsetHidden(value, 1);' in store.replace("\r\n", "\n")
assert '"Hidden = %s\\r\\n", hidden_apps[i]' in store
assert "DASH_MAX_RECENT + nhidden" in store
assert "dashCfg.app_filter = clampInt(value, 0, 6, 0);" in store
# Saved filter IDs 0-5 stay stable; the new filter is appended.
assert "FILTER_OTHER,\n\tFILTER_ALL_HIDDEN," in apps.replace("\r\n", "\n")
assert "filter != FILTER_ALL_HIDDEN && dashStoreIsHidden(a->path)" in function(apps, "static int matchFilter(")
assert "dashStoreIsHidden(path)" in function(home, "static void homeAdd(")
# Library scans probe a cold memory card instead of trusting an early snapshot.
assert "filerPrepareMemoryCardRead(j->dirs[i][2] - '0') != 0" in lib
assert "filerPrepareMemoryCardRead(port)" in function(filer, "int readMC(")
print("PASS: hidden app paths add/remove/dedupe/limits, persistence, filter IDs, Home/Apps wiring, cold-card scan")
