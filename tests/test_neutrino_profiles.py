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
assert "showNeutrinoLaunchOptionsForGame(&options, gameID, path)" in iso
assert "showNeutrinoLaunchOptions(&options)" in iso
assert "profileSave(active_profile_gameid, options)" in source
assert "profileLoad(active_profile_gameid, &saved" in source
assert 'items[NEUTRINO_CANCEL].label = "Save for this game"' in source
assert "active_profile_gameid[0] = '\\0';" in source
assert "buildNeutrinoOptionArgs(options, &encoded)" in source
assert "memory card" in source and "profileDeviceRoot(active_profile_path)" in source
assert "editNeutrinoGameProfile(g->category, g->path)" in (root / "src/dash_games.c").read_text(encoding="utf-8")
assert "int editNeutrinoGameProfile(const char *game_id, const char *game_path)" in source

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


def extract_signature(signature):
    start = source.index(signature)
    depth = 1
    cur = source.index("{", start) + 1
    while depth:
        depth += (source[cur] == "{") - (source[cur] == "}")
        cur += 1
    return source[start:cur] + "\n"


names = ("neutrinoVmcNameValid", "profileGameIdValid", "profileChecksum", "profilePath", "profileDeviceRoot", "profileDevicePath", "profileStorageSafe",
         "profileOptionsValid", "profileReadSlot", "profileReadLatest", "profileCompatBit", "profileIntValue",
         "profileParseText", "profileReadAt", "profileReadFile", "profileLoad", "profileSave")
code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 256
#define TRUE 1
#define FALSE 0
#define NEUTRINO_GAME_ID_SIZE 12
#define MX4SIO 1
#define MMCE 1
static int mx4sio_driver_running, mmce_stack, dev_stack=1, fileMode=0777;
static char active_profile_path[MAX_PATH];
static char active_profile_gameid[NEUTRINO_GAME_ID_SIZE];
typedef uint64_t u64;
static struct { int HOSTwrite; u64 color[8]; } setting_s = {1,{0}}, *setting = &setting_s;
static const char *dashStoreDir(void) { return "mc0:/SYS-CONF/"; }
static int isPathDeviceStackLoaded(const char *path) {
    if (!strcmp(path,"mmce0:/")) return mmce_stack;
    return strstr(path,"mc") == path ? 1 : dev_stack;
}
#define NEUTRINO_VMC_NAME_MAX 64
typedef struct { unsigned int game_compatibility; int video_mode, video_compatibility, debug_colors, logo; char vmc[2][NEUTRINO_VMC_NAME_MAX]; } NEUTRINO_LAUNCH_OPTIONS;
static const struct { int mode; const char *label; } compatibility_options[] = {{0,""},{2,""},{3,""},{5,""},{7,""}};
enum { NEUTRINO_COMPAT_COUNT = 5, NEUTRINO_VIDEO_COUNT = 6, NEUTRINO_VIDEO_COMPAT_COUNT = 4 };
enum { NEUTRINO_VIDEO_MODE = NEUTRINO_COMPAT_COUNT, NEUTRINO_VIDEO_COMPATIBILITY,
       NEUTRINO_DEBUG_COLORS, NEUTRINO_LOGO, NEUTRINO_RUN, NEUTRINO_CANCEL };
typedef struct { int unused; } NEUTRINO_OPTION_ARGS;
static char content[4][512];
static int fail_write, truncate_write, allow_wait=1;
static int read_denied, read_attempts, fail_reads_after, stat_result;
typedef struct { int unused; } iox_stat_t;
static int genGetStat(const char *path, iox_stat_t *out) {
    (void)path; (void)out;
    return stat_result ? stat_result : content[3][0] ? 0 : -ENOENT;
}
static int dev_no_dir, dev_has_dir, dev_mkdir_fails, dev_torn, dev_writes, dev_mkdirs;
static char last_dev_path[256];
static int lookup(const char *path) { return strstr(path, "/CFG/") ? 3 : strstr(path, ".cfg") ? 2 : strstr(path, "-B.CNF") ? 1 : 0; }
static int genMkdir(const char *path, int mode) { (void)mode; assert(strstr(path,"CFG") && !strstr(path,".cfg")); dev_mkdirs++; if (dev_mkdir_fails) return -1; dev_has_dir=1; return 0; }
static int dashStoreBuildPath(char *out, int size, const char *file) {
    int n=snprintf(out, size, "mc0:/SYS-CONF/%s", file);
    return n > 0 && n < size ? 0 : -1;
}
static char *dashLoadTextFile(const char *path, int max_size, int *len) {
    int k=lookup(path), n=(int)strlen(content[k]);
    char *buf;
    if (k==3) read_attempts++;
    if ((k==3 && (read_denied || (fail_reads_after && read_attempts>fail_reads_after))) || n<=0 || n>max_size) return NULL;
    buf=(char *)malloc((size_t)n+1);
    assert(buf);
    memcpy(buf,content[k],(size_t)n+1);
    if (len != NULL) *len=n;
    return buf;
}
static int dashWriteFile(const char *path, const void *data, int size) {
    int k=lookup(path);
    if (k==3) {
        dev_writes++; snprintf(last_dev_path,sizeof(last_dev_path),"%s",path);
        if (dev_no_dir && !dev_has_dir) return -1; /* CFG/ missing: nothing opened or touched */
        if (dev_torn) { size /= 2; memcpy(content[3],data,(size_t)size); content[3][size]=0; return -1; }
    }
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
static const struct { const char *argument, *label; } video_modes[] = {
    {"","Original"},{"fp1","240p"},{"fp2","480p"},{"1080ix1","1080i x1"},{"1080ix2","1080i x2"},{"1080ix3","1080i x3"}
};
static const char *video_compatibility_modes[] = {"Off","Field 1","Field 2","Field 3"};
typedef struct { int icon; const char *label, *detail; int disabled; } DashMenuItem;
typedef struct { NEUTRINO_LAUNCH_OPTIONS *options; DashMenuItem *items; } NeutrinoOptionsMenu;
enum { DI_SAVE, DI_PLAY, DI_INFO, DI_SETTINGS, DI_UNDO, DI_BACK, DASH_TOAST_OK, DASH_TOAST_WARN };
static int menu_script;
static int (*modal_edit)(int, int, void *);
static void *modal_edit_arg;
static void dashSoundMaintain(void) {}
static void dashToast(int kind, const char *text) { (void)kind; (void)text; }
static int dashModalChoice(const char *title, const char *const *labels, int count, int selected) {
    (void)title; (void)labels; (void)count; return selected;
}
static void dashModalSetEdit(int (*edit)(int, int, void *), void *arg) { modal_edit = edit; modal_edit_arg = arg; }
static int dashModalMenu(const char *title, const char *detail, DashMenuItem *items, int count, int selected) {
    (void)title; (void)detail; (void)items; (void)count; (void)selected;
    if (menu_script == 1) {
        modal_edit(NEUTRINO_VIDEO_MODE + 1, 1, modal_edit_arg);
        return 0;
    }
    if (menu_script == 2) {
        modal_edit(NEUTRINO_VIDEO_MODE + 1, 1, modal_edit_arg);
        modal_edit(NEUTRINO_CANCEL, 0, modal_edit_arg);
        return 1;
    }
    for (int i=0;i<count;i++)
        assert(items[i].label == NULL || strcmp(items[i].label,"Save for this game"));
    return 0;
}
static int DashIsActive(void) { return 1; }
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 448
#define SCREEN_MARGIN 8
#define FONT_HEIGHT 16
#define FONT_WIDTH 8
#define LEFT_CUR '<'
#define COLOR_BACKGR 0
#define COLOR_FRAME 1
#define COLOR_TEXT 2
#define COLOR_SELECT 3
#define PAD_TRIANGLE 1
#define PAD_CROSS 2
#define PAD_CIRCLE 4
#define PAD_UP 8
#define PAD_DOWN 16
#define PAD_LEFT 32
#define PAD_RIGHT 64
#define LNG(x) ""
static int swapKeys, new_pad;
static void waitPadReady(int port, int slot) { (void)port; (void)slot; }
static int readpad(void) { return 0; }
static void clrScr(u64 color) { (void)color; }
static void setScrTmp(const char *a, const char *b) { (void)a; (void)b; }
static void drawPopSprite(u64 color, int x1, int y1, int x2, int y2) { (void)color; (void)x1; (void)y1; (void)x2; (void)y2; }
static void drawFrame(int x1, int y1, int x2, int y2, u64 color) { (void)x1; (void)y1; (void)x2; (void)y2; (void)color; }
static int printXY(const char *text, int x, int y, u64 color, int draw, int align) {
    (void)text; (void)x; (void)y; (void)color; (void)draw; (void)align; return 0;
}
static void drawChar(int c, int x, int y, u64 color) { (void)c; (void)x; (void)y; (void)color; }
static void drawScr(void) {}
'''
for signature in (
    "static int profileReadActiveFile(",
    "static int moveNeutrinoSelection(",
    "static void neutrinoOptionDetails(",
    "static int editNeutrinoOption(",
    "static int showDashboardNeutrinoOptions(",
    "static void drawNeutrinoHeading(",
    "static void drawNeutrinoOption(",
    "int showNeutrinoLaunchOptions(",
    "int showNeutrinoLaunchOptionsForGame(",
    "int editNeutrinoGameProfile(",
):
    code += extract_signature(signature)
code += r'''
static void clear_slots(void) { memset(content,0,sizeof(content)); fail_write=truncate_write=read_denied=read_attempts=fail_reads_after=stat_result=0; allow_wait=1; mx4sio_driver_running=mmce_stack=0;
    dev_stack=1; dev_no_dir=dev_has_dir=dev_mkdir_fails=dev_torn=dev_writes=dev_mkdirs=0; strcpy(active_profile_path,"mass:/DVD/Game.iso"); last_dev_path[0]=0; }
int main(void)
{
    const char *id="SLUS_123.45";
    NEUTRINO_LAUNCH_OPTIONS a, b, got;
    assert(genGetStat != NULL);
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
    assert(strstr(content[3],"$Compatibility=") && strstr(content[3],"$VMC_0=My Game"));
    assert(profileLoad(id,&got) && !memcmp(&got,&a,sizeof(got)));
    assert(profileSave(id,&b));
    assert(profileLoad(id,&got) && !memcmp(&got,&b,sizeof(got)));
    /* A hand-edited file is read. */
    strcpy(content[3],"# note\n$Compatibility=2\n$NeutrinoVideo=1\n$NeutrinoArgs=-logo -dbc -qb\n$VMC_0 = hand\n$Other=9\n$End=1\n");
    memset(&a,0,sizeof(a)); a.game_compatibility=1u<<2; a.video_mode=1; a.debug_colors=1; a.logo=1; strcpy(a.vmc[0],"hand");
    assert(profileLoad(id,&got) && !memcmp(&got,&a,sizeof(got)));
    /* A file cut short (no $End=1) is not accepted, even if what is there parses. */
    strcpy(content[3],"$Compatibility=2\n$NeutrinoVideo=1\n");
    assert(!profileLoad(id,&got));
    strcpy(content[3],"$Compatibility=2\n$NeutrinoVideo=1\n$End=1\n");
    assert(profileLoad(id,&got));
    /* $End=1 is the last line: settings after it reject the file; blank lines do not. */
    strcpy(content[3],"$Compatibility=2\n$End=1\n$VMC_0=\n");
    assert(!profileLoad(id,&got));
    strcpy(content[3],"$Compatibility=2\n$End=1\n\r\n\n");
    assert(profileLoad(id,&got));
    /* A bad value invalidates the file; a legacy NTP1 slot is then used. */
    strcpy(content[3],"$NeutrinoVideo=9\n");
    assert(!profileLoad(id,&got));
    {   /* legacy A slot: built by the same checksum as the old writer */
        char payload[112];
        int n=snprintf(payload,sizeof(payload),"NTP1|%s|%08X|%08X|%d|%d|%d|%d",id,1u,4u,2,1,0,1);
        snprintf(content[0],sizeof(content[0]),"%s|%08X\n",payload,profileChecksum(payload,n));
    }
    assert(profileLoad(id,&got) && got.game_compatibility==4u && got.video_mode==2 && got.logo==1 && !got.vmc[0][0]);
    /* Saving never touches the legacy files. */
    content[3][0]=0;
    { char before[512]; strcpy(before,content[0]); assert(profileSave(id,&b)); assert(!strcmp(before,content[0])); }
    /* Failed or truncated writes are not reported as saved. */
    fail_write=1; assert(!profileSave(id,&a)); fail_write=0;
    truncate_write=1; assert(!profileSave(id,&a)); assert(!profileReadFile(id,&got)); truncate_write=0;
    allow_wait=0; assert(!profileSave(id,&a)); allow_wait=1;
    a.video_mode=9; assert(!profileSave(id,&a));
    assert(!profileSave("INVALID",&b));
    /* The app config folder (memory card) is only read, and not while another SIO2 stack owns it. */
    clear_slots(); active_profile_path[0]=0;
    strcpy(content[2],"$NeutrinoVideo=2\n$End=1\n");
    assert(profileReadFile(id,&got) && got.video_mode==2 && !profileSave(id,&b));
    mx4sio_driver_running=1;
    assert(!profileReadFile(id,&got));
    mx4sio_driver_running=0; mmce_stack=1;
    assert(!profileReadFile(id,&got));
    /* Games on a device keep their profile on that device (CFG/), and nowhere else. */
    clear_slots();
    a.video_mode=2; assert(profileSave(id,&a)==1);
    assert(strstr(content[3],"$End=1") && !content[2][0] && !content[0][0] && dev_mkdirs==0);
    assert(!strcmp(last_dev_path,"mass:/CFG/NEUT-SLUS_123.45.cfg"));
    assert(profileLoad(id,&got) && !memcmp(&got,&a,sizeof(got)));
    /* The device file wins over an older app-folder file; an invalid one falls back to it (read only). */
    strcpy(content[2],"$NeutrinoVideo=1\n$End=1\n");
    assert(profileLoad(id,&got) && got.video_mode==2);
    strcpy(content[3],"$NeutrinoVideo=9\n$End=1\n");
    assert(profileLoad(id,&got) && got.video_mode==1);
    /* A missing CFG folder is created once, then the write is retried. */
    clear_slots(); dev_no_dir=1;
    assert(profileSave(id,&a)==1 && dev_mkdirs==1 && dev_writes==2 && strstr(content[3],"$End=1") && !content[2][0]);
    /* Folder cannot be created: the save fails. Nothing goes to the memory card. */
    clear_slots(); dev_no_dir=1; dev_mkdir_fails=1;
    assert(profileSave(id,&a)==0 && !content[3][0] && !content[2][0]);
    /* A game on UDPFS: no save while network writing is off, as in the File Browser. */
    clear_slots(); strcpy(active_profile_path,"udpfs:/DVD/Game.iso"); setting->HOSTwrite=0;
    assert(profileSave(id,&a)==0 && dev_writes==0 && !content[3][0]);
    setting->HOSTwrite=1;
    assert(profileSave(id,&a)==1 && !strcmp(last_dev_path,"udpfs:/CFG/NEUT-SLUS_123.45.cfg"));
    /* A device write cut short leaves a file the loader ignores; the save fails, the memory card is untouched. */
    clear_slots(); strcpy(content[2],"$NeutrinoVideo=1\n$End=1\n"); dev_torn=1;
    assert(profileSave(id,&a)==0 && content[3][0] && !strstr(content[3],"$End=1"));
    assert(!strcmp(content[2],"$NeutrinoVideo=1\n$End=1\n"));
    assert(profileLoad(id,&got) && got.video_mode==1);
    /* Device stack not loaded: the device is not touched, and the save fails. */
    clear_slots(); dev_stack=0;
    assert(profileSave(id,&a)==0 && dev_writes==0 && !content[3][0] && !content[2][0]);
    /* Every kind of game device or share gets its own CFG/ file, even while the memory card is off limits. */
    {   static const char *const games[][2]={
            {"mass0:/DVD/Game.iso","mass0:/CFG/NEUT-SLUS_123.45.cfg"},
            {"usb0:/DVD/Game.iso","usb0:/CFG/NEUT-SLUS_123.45.cfg"},
            {"ata0:/DVD/Game.iso","ata0:/CFG/NEUT-SLUS_123.45.cfg"},
            {"mx4sio0:/DVD/Game.iso","mx4sio0:/CFG/NEUT-SLUS_123.45.cfg"},
            {"mmce0:/DVD/Game.iso","mmce0:/CFG/NEUT-SLUS_123.45.cfg"},
            {"ilink0:/DVD/Game.iso","ilink0:/CFG/NEUT-SLUS_123.45.cfg"},
            {"udpfs:/DVD/Game.iso","udpfs:/CFG/NEUT-SLUS_123.45.cfg"},
            {"smb:/Share/DVD/Game.iso","smb:/Share/CFG/NEUT-SLUS_123.45.cfg"},
            {"smb1:/NAS/Game.iso","smb1:/NAS/CFG/NEUT-SLUS_123.45.cfg"},
            {"nfs:/Export/CD/Game.iso","nfs:/Export/CFG/NEUT-SLUS_123.45.cfg"}};
        size_t h;
        for (h=0;h<sizeof(games)/sizeof(games[0]);h++) {
            clear_slots(); strcpy(active_profile_path,games[h][0]); mmce_stack=1; mx4sio_driver_running=1;
            assert(profileSave(id,&a)==1);
            assert(!strcmp(last_dev_path,games[h][1]) && strstr(content[3],"$End=1") && !content[2][0]);
            assert(profileLoad(id,&got) && !memcmp(&got,&a,sizeof(got)));
        }
    }
    /* Paths that are not on a device or share have no place for the file: the save fails, nothing is written. */
    {   static const char *const none[]={"mc0:/Game.iso","","hdd0:/Game.iso","smb:/","smb:/Share","smb:","nfs:/x"};
        size_t h;
        for (h=0;h<sizeof(none)/sizeof(none[0]);h++) {
            clear_slots(); strcpy(active_profile_path,none[h]);
            assert(profileDeviceRoot(none[h])==0);
            assert(profileSave(id,&a)==0 && dev_writes==0 && !content[2][0] && !content[3][0]);
        }
    }
    /* Real Games settings: an unrelated edit preserves both hand-written VMC fields. */
    clear_slots();
    strcpy(content[3],"$Compatibility=0\n$NeutrinoVideo=0\n$VMC_0=First Card\n$VMC_1=Second Card\n$End=1\n");
    menu_script=1;
    assert(editNeutrinoGameProfile(id,"mass:/DVD/Game.iso")==1);
    assert(strstr(content[3],"$VMC_0=First Card") && strstr(content[3],"$VMC_1=Second Card"));
    assert(strstr(content[3],"$NeutrinoVideo=1"));
    /* The launch-options save route preserves the same fields. */
    clear_slots();
    strcpy(content[3],"$Compatibility=0\n$NeutrinoVideo=0\n$VMC_0=First Card\n$VMC_1=Second Card\n$End=1\n");
    menu_script=2;
    memset(&got,0,sizeof(got));
    (void)showNeutrinoLaunchOptionsForGame(&got,id,"mass:/DVD/Game.iso");
    assert(strstr(content[3],"$VMC_0=First Card") && strstr(content[3],"$VMC_1=Second Card"));
    assert(strstr(content[3],"$NeutrinoVideo=1"));
    /* An unreadable hand edit is never replaced with defaults by either save route. */
    clear_slots();
    strcpy(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n$VMC_1=Also Keep\n");
    menu_script=1;
    assert(editNeutrinoGameProfile(id,"mass:/DVD/Game.iso")==0);
    assert(dev_writes==0 && !strcmp(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n$VMC_1=Also Keep\n"));
    clear_slots();
    strcpy(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n$VMC_1=Also Keep\n");
    menu_script=0;
    memset(&got,0,sizeof(got));
    assert(showNeutrinoLaunchOptionsForGame(&got,id,"mass:/DVD/Game.iso"));
    assert(dev_writes==0 && !strcmp(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n$VMC_1=Also Keep\n"));
    /* An existing profile whose read fails is not treated as an empty slot. */
    clear_slots();
    strcpy(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n");
    read_denied=1; menu_script=1;
    assert(editNeutrinoGameProfile(id,"mass:/DVD/Game.iso")==0);
    assert(dev_writes==0 && !strcmp(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n"));
    /* A successful validation read supplies the settings used by the editor; a second-read failure cannot erase VMC names. */
    clear_slots();
    strcpy(content[3],"$Compatibility=0\n$NeutrinoVideo=0\n$VMC_0=First Card\n$VMC_1=Second Card\n$End=1\n");
    fail_reads_after=1; menu_script=1;
    (void)editNeutrinoGameProfile(id,"mass:/DVD/Game.iso");
    assert(strstr(content[3],"$VMC_0=First Card") && strstr(content[3],"$VMC_1=Second Card"));
    /* Existing files with transient stat/read errors stay off-limits to saving. */
    {
        const int errors[]={-EIO,-ENODEV,-ETIMEDOUT,-EBUSY};
        for (size_t i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
            clear_slots();
            strcpy(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n");
            read_denied=1; stat_result=errors[i]; menu_script=1;
            assert(editNeutrinoGameProfile(id,"mass:/DVD/Game.iso")==0);
            assert(dev_writes==0 && !strcmp(content[3],"$NeutrinoVideo=0\n$VMC_0=Keep Me\n$End=1\n"));
        }
    }
    /* Canceled worker wait must never read profiles or offer to save defaults. */
    clear_slots();
    strcpy(content[3],"$Compatibility=0\n$VMC_0=Leave Me\n$End=1\n");
    allow_wait=0; menu_script=0;
    memset(&got,0,sizeof(got));
    assert(showNeutrinoLaunchOptionsForGame(&got,id,"mass:/DVD/Game.iso"));
    assert(read_attempts==0 && dev_writes==0);
    allow_wait=1;
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
