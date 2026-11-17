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
assert "profileLoad(active_profile_gameid, &saved" in source
assert 'items[NEUTRINO_CANCEL].label = "Save for this game"' in source
assert "active_profile_gameid[0] = '\\0';" in source
assert "buildNeutrinoOptionArgs(options, &encoded)" in source

def extract(name):
    m = re.search(r"(?:static )?(?:int|unsigned int) " + re.escape(name) + r"\(", source)
    assert m, name
    start = source.index("{", m.start())
    depth = 1
    cur = start + 1
    while depth:
        depth += (source[cur] == "{") - (source[cur] == "}")
        cur += 1
    return source[m.start():cur] + "\n"

names = ("neutrinoVmcNameValid", "profileGameIdValid", "profileChecksum", "profilePath", "profileStorageSafe",
         "profileOptionsValid", "profileReadSlot", "profileReadLatest", "profileCompatBit", "profileIntValue",
         "profileParseText", "profileReadFile", "profileLoad", "profileSave")
code = r'''
#define _POSIX_C_SOURCE 200809L
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
#define NEUTRINO_VMC_NAME_MAX 64
typedef struct { unsigned int game_compatibility; int video_mode, video_compatibility, debug_colors, logo; char vmc[2][NEUTRINO_VMC_NAME_MAX]; } NEUTRINO_LAUNCH_OPTIONS;
static const struct { int mode; const char *label; } compatibility_options[] = {{0,""},{2,""},{3,""},{5,""},{7,""}};
enum { NEUTRINO_COMPAT_COUNT = 5, NEUTRINO_VIDEO_COUNT = 6, NEUTRINO_VIDEO_COMPAT_COUNT = 4 };
typedef struct { int unused; } NEUTRINO_OPTION_ARGS;
static char content[3][512];
static int fail_write, truncate_write, allow_wait=1;
static int lookup(const char *path) { return strstr(path, ".cfg") ? 2 : strstr(path, "-B.CNF") ? 1 : 0; }
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
    assert(size > 0 && size < 512);
    if (truncate_write) size /= 2;
    memcpy(content[k],data,(size_t)size);
    content[k][size]=0;
    return truncate_write ? -1 : 0;
}
static int dashWorkerAwaitAll(const char *s) { (void)s; return allow_wait; }
static int buildNeutrinoOptionArgs(const NEUTRINO_LAUNCH_OPTIONS *p, NEUTRINO_OPTION_ARGS *out) {
    (void)out;
    return !((p->game_compatibility & ~0xADu) || p->video_mode<0 || p->video_mode>=6 ||
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
    NEUTRINO_LAUNCH_OPTIONS a, b, got;
    unsigned int rev=0;
    int slot=-1;
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
    a.game_compatibility=0x0Cu; a.video_mode=2; a.video_compatibility=1; a.logo=1; strcpy(a.vmc[0],"My Game");
    b.game_compatibility=0x8Du; b.video_mode=5; b.video_compatibility=3; b.debug_colors=1; strcpy(b.vmc[1],"card1");
    assert(!profileGameIdValid("SLUS/123.45"));
    assert(!profileGameIdValid("SLUS_123.4"));
    assert(profileGameIdValid(id));
    assert(neutrinoVmcNameValid("a b") && !neutrinoVmcNameValid("a/b") && !neutrinoVmcNameValid("a:b") && !neutrinoVmcNameValid("a\nb"));
    clear_slots();
    assert(!profileLoad(id,&got));
    /* Round trip through the OPL-style file. */
    assert(profileSave(id,&a));
    assert(strstr(content[2],"$Compatibility=") && strstr(content[2],"$VMC_0=My Game"));
    assert(profileLoad(id,&got) && !memcmp(&got,&a,sizeof(got)));
    assert(profileSave(id,&b));
    assert(profileLoad(id,&got) && !memcmp(&got,&b,sizeof(got)));
    /* A hand-edited file is read. */
    strcpy(content[2],"# note\n$Compatibility=2\n$NeutrinoVideo=1\n$NeutrinoArgs=-logo -dbc -qb\n$VMC_0 = hand\n$Other=9\n$End=1\n");
    memset(&a,0,sizeof(a)); a.game_compatibility=1u<<2; a.video_mode=1; a.debug_colors=1; a.logo=1; strcpy(a.vmc[0],"hand");
    assert(profileLoad(id,&got) && !memcmp(&got,&a,sizeof(got)));
    /* A file cut short (no $End=1) is not accepted, even if what is there parses. */
    strcpy(content[2],"$Compatibility=2\n$NeutrinoVideo=1\n");
    assert(!profileLoad(id,&got));
    strcpy(content[2],"$Compatibility=2\n$NeutrinoVideo=1\n$End=1\n");
    assert(profileLoad(id,&got));
    /* A bad value invalidates the file; a legacy NTP1 slot is then used. */
    strcpy(content[2],"$NeutrinoVideo=9\n");
    assert(!profileLoad(id,&got));
    {   /* legacy A slot: built by the same checksum as the old writer */
        char payload[112];
        int n=snprintf(payload,sizeof(payload),"NTP1|%s|%08X|%08X|%d|%d|%d|%d",id,1u,4u,2,1,0,1);
        snprintf(content[0],sizeof(content[0]),"%s|%08X\n",payload,profileChecksum(payload,n));
    }
    assert(profileLoad(id,&got) && got.game_compatibility==4u && got.video_mode==2 && got.logo==1 && !got.vmc[0][0]);
    /* Saving never touches the legacy files. */
    content[2][0]=0;
    { char before[512]; strcpy(before,content[0]); assert(profileSave(id,&b)); assert(!strcmp(before,content[0])); }
    /* Failed or truncated writes are not reported as saved. */
    fail_write=1; assert(!profileSave(id,&a)); fail_write=0;
    truncate_write=1; assert(!profileSave(id,&a)); assert(!profileReadFile(id,&got)); truncate_write=0;
    allow_wait=0; assert(!profileSave(id,&a)); allow_wait=1;
    a.video_mode=9; assert(!profileSave(id,&a));
    assert(!profileSave("INVALID",&b));
    /* Reading/writing MC profiles must not switch SIO2 ownership. */
    mx4sio_driver_running=1;
    assert(!profileSave(id,&b) && !profileReadFile(id,&got));
    mx4sio_driver_running=0; mmce_stack=1;
    assert(!profileSave(id,&b) && !profileReadFile(id,&got));
    (void)rev; (void)slot;
    puts("PASS: Neutrino game settings: OPL-style KEY=value round trip, hand edits, legacy NTP1 fallback, failed writes, validation");
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
