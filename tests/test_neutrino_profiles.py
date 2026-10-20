"""Compiled production Neutrino profile storage fault-injection tests."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/gui_neutrino.c").read_text(encoding="utf-8")
iso = (root / "src/neutrino_iso.c").read_text(encoding="utf-8")
header = (root / "include/gui_neutrino.h").read_text(encoding="utf-8")

assert "int showNeutrinoLaunchOptionsForGame(" in header
assert "showNeutrinoLaunchOptionsForGame(&options, gameID)" in iso
assert "showNeutrinoLaunchOptions(&options)" in iso
assert "profileSave(active_profile_gameid, options)" in source
assert "profileReadLatest(active_profile_gameid, &saved" in source
assert 'items[NEUTRINO_CANCEL].label = "Save for this game"' in source
assert "active_profile_gameid[0] = '\\0';" in source
assert "buildNeutrinoOptionArgs(options, &encoded)" in source

def extract(name):
    m = re.search(r"static (?:int|unsigned int) " + re.escape(name) + r"\(", source)
    assert m, name
    start = source.index("{", m.start())
    depth = 1
    cur = start + 1
    while depth:
        depth += (source[cur] == "{") - (source[cur] == "}")
        cur += 1
    return source[m.start():cur] + "\n"

names = ("profileGameIdValid", "profileChecksum", "profilePath", "profileStorageSafe",
         "profileOptionsValid", "profileReadSlot", "profileReadLatest", "profileSave")
code = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
#define NEUTRINO_GAME_ID_SIZE 12
#define MX4SIO 1
#define MMCE 1
static int mx4sio_driver_running, mmce_stack;
static const char *dashStoreDir(void) { return "mc0:/SYS-CONF/"; }
static int isPathDeviceStackLoaded(const char *path) {
    if (!strcmp(path,"mmce0:/")) return mmce_stack;
    return 1;
}
typedef struct { unsigned int game_compatibility; int video_mode, video_compatibility, debug_colors, logo; } NEUTRINO_LAUNCH_OPTIONS;
typedef struct { int unused; } NEUTRINO_OPTION_ARGS;
static char content[2][256];
static int fail_write, truncate_write, allow_wait=1;
static int lookup(const char *path) { return strstr(path, "-B.CNF") ? 1 : 0; }
static int dashStoreBuildPath(char *out, int size, const char *file) {
    int n=snprintf(out, size, "mc0:/SYS-CONF/%s", file);
    return n > 0 && n < size ? 0 : -1;
}
static char *dashLoadTextFile(const char *path, int max_size, int *len) {
    int k=lookup(path), n=(int)strlen(content[k]);
    char *buf;
    if (n<=0 || n>max_size) return NULL;
    buf=(char *)malloc((size_t)n+1);
    assert(buf);
    memcpy(buf,content[k],(size_t)n+1);
    *len=n;
    return buf;
}
static int dashWriteFile(const char *path, const void *data, int size) {
    int k=lookup(path);
    if (fail_write) return -1;
    assert(size > 0 && size < 256);
    if (truncate_write) size /= 2;
    memcpy(content[k],data,(size_t)size);
    content[k][size]=0;
    return truncate_write ? -1 : 0;
}
static int dashWorkerAwaitAll(const char *s) { (void)s; return allow_wait; }
static int buildNeutrinoOptionArgs(const NEUTRINO_LAUNCH_OPTIONS *p, NEUTRINO_OPTION_ARGS *out) {
    (void)out;
    return !((p->game_compatibility & ~0xA5u) || p->video_mode<0 || p->video_mode>=6 ||
             p->video_compatibility<0 || p->video_compatibility>=4) ? 0 : -1;
}
'''
for name in names:
    code += extract(name)
code += r'''
static void clear_slots(void) { memset(content,0,sizeof(content)); fail_write=truncate_write=0; allow_wait=1; mx4sio_driver_running=mmce_stack=0; }
int main(void)
{
    const char *id="SLUS_123.45";
    NEUTRINO_LAUNCH_OPTIONS a={1,2,1,0,1}, b={0xA5u,5,3,1,0}, c={4,1,0,0,0}, got;
    unsigned int rev=0;
    int slot=-1;
    assert(!profileGameIdValid("SLUS/123.45"));
    assert(!profileGameIdValid("SLUS_123.4"));
    assert(profileGameIdValid(id));
    clear_slots();
    assert(!profileReadLatest(id,&got,&rev,&slot));
    assert(profileSave(id,&a));
    assert(profileReadLatest(id,&got,&rev,&slot) && rev==1 && slot==0 && !memcmp(&got,&a,sizeof(got)));
    assert(profileSave(id,&b));
    assert(profileReadLatest(id,&got,&rev,&slot) && rev==2 && slot==1 && !memcmp(&got,&b,sizeof(got)));
    fail_write=1;
    assert(!profileSave(id,&c));
    assert(profileReadLatest(id,&got,&rev,&slot) && rev==2 && slot==1 && !memcmp(&got,&b,sizeof(got)));
    fail_write=0; truncate_write=1;
    assert(!profileSave(id,&c));
    assert(profileReadLatest(id,&got,&rev,&slot) && rev==2 && slot==1 && !memcmp(&got,&b,sizeof(got)));
    truncate_write=0;
    assert(profileSave(id,&c));
    assert(profileReadLatest(id,&got,&rev,&slot) && rev==3 && slot==0 && !memcmp(&got,&c,sizeof(got)));
    content[0][4]='X';
    assert(profileReadLatest(id,&got,&rev,&slot) && rev==2 && slot==1);
    allow_wait=0;
    assert(!profileSave(id,&c));
    allow_wait=1;
    a.video_mode=9;
    assert(!profileSave(id,&a));
    assert(!profileSave("INVALID", &b));
    /* Reading/writing MC profiles must not switch SIO2 ownership. */
    mx4sio_driver_running=1;
    assert(!profileSave(id, &b) && !profileReadSlot(id,1,&got,&rev));
    mx4sio_driver_running=0; mmce_stack=1;
    assert(!profileSave(id, &b) && !profileReadSlot(id,1,&got,&rev));
    clear_slots();
    assert(!profileReadLatest(id,&got,&rev,&slot));
    puts("PASS: Neutrino profile round-trip, A/B generations, failed/truncated writes, corrupt recovery, validation");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="neutrino-profiles-") as folder:
    path = Path(folder)
    test = path / "test.c"
    exe = path / "test"
    test.write_text(code)
    cc = shutil.which("gcc") or shutil.which("clang")
    assert cc, "A C host compiler is required"
    subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(test),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
