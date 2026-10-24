"""Execute the dashboard launch path: Neutrino prompts precede audio teardown."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
dash_src = (root / "src/dash_main.c").read_text()
actions_src = (root / "src/main_actions.c").read_text()
neutrino_src = (root / "src/neutrino_iso.c").read_text()


def extract_func(text, sig):
    start = text.index(sig)
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define MAX_PATH 1024
#define DASH_TITLE_MAX 64
#define DASH_LAUNCH_PENDING_ARGS 2
#define DASH_APP_HAS_ARGS 2
#define DI_EDIT 1
#define DI_WARNING 2
#define TEXTEDITOR_RESULT_LAUNCH_ARGS 1
#define NEUTRINO_GAME_ID_SIZE 16
#define NEUTRINO_ISO_PVD_OFFSET (16 * 2048)
#define POPSTARTER_OK 0
#define SEEK_SET 0
typedef struct { char path[MAX_PATH], title[DASH_TITLE_MAX]; int flags; } DashApp;
typedef struct { char main_msg[MAX_PATH]; } MainExecuteContext;
typedef struct { int count; } POPSTARTER_CANDIDATE_LIST;
typedef struct { int dummy; } NEUTRINO_LAUNCH_OPTIONS;
typedef struct { char argv[4][32]; } NEUTRINO_OPTION_ARGS;
typedef struct { int disable_neutrino, cdrom_disable_gameid; char neutrino_file[MAX_PATH]; } Setting;
static Setting *setting;
static void *elisaFnt, *External_Lang_Buffer;
static MainExecuteContext exec_ctx;
static void (*mainActionLaunchHook)(const char *);
static void (*drawMsgOverride)(const char *);
static int dash_active = 1;
static int muted, suspended, restored, bgm, calls, commit;
static int shutdown_calls, pad_ended;
static jmp_buf launched;
static void dashLaunchHook(const char *p) { (void)p; }
static void dashMsgHook(const char *p) { (void)p; }
static void dashStrCopy(char *d, const char *s, size_t n) { snprintf(d,n,"%s",s); }
static void LaunchArgsClear(void) {}
static int LaunchArgsLoadFromBuffer(const char *p, char *a, size_t n, char *m, size_t s) {
    (void)p; (void)a; (void)n; (void)m; (void)s; return 1;
}
static void dashModalMessage(int i, const char *t, const char *b, const char *d) {
    (void)i; (void)t; (void)b; (void)d;
}
static int dashTextEditorEx(const char *p, int n) { (void)p; (void)n; return 0; }
static int settleBackgroundWork(const char *t) { (void)t; return 1; }
static int argSidecarExists(const char *p) { (void)p; return 0; }
static int dashLibLoadArgs(DashApp *a, char *b, int n) { (void)a; (void)b; (void)n; return 0; }
static int dashArtTakeDirty(void) { return 0; }
static void dashLibSaveCache(void) {}
static void dashStorePushRecent(const char *p) { (void)p; }
static void filerRootMcPollIdle(void) {}
static void dashBgFlash(float f) { (void)f; }
static void dashLoadingBegin(const char *t) { (void)t; }
static void dashLoadingStatus(const char *t, int n, int m) { (void)t; (void)n; (void)m; }
static int genCmpFileExt(const char *p, const char *ext) {
    const char *dot = strrchr(p, '.');
    return dot != NULL && !strcasecmp(dot + 1, ext);
}
static void dashSoundSuspend(void) { muted = 1; suspended++; }
static void dashSoundShutdown(void) { muted = 1; shutdown_calls++; }
static void fadeScreenToBlack(void) {}
static void DashThemeSetLegacyEnabled(int e) { (void)e; }
static void clearScreenToBlack(void) {}
static void padPortClose(int p, int s) { (void)p; (void)s; }
static void padEnd(void) { pad_ended++; }
static void closeKeyboardIfOpened(void) {}
static void dashGfxRestoreLegacyState(void) {}
static void dashClassicUiInstall(int n) { (void)n; }
static int openPathForRead(const char *p, char *o, size_t n) {
    if (o && n) snprintf(o, n, "%s", p);
    return (commit < 0 && (strstr(p, "neutrino.elf") || strstr(p, "NEUTRINO.ELF"))) ? -1 : 1;
}
static long genLseek(int fd, long off, int w) { (void)fd; (void)w; return off; }
static int genRead(int fd, void *buf, size_t sz) {
    unsigned char *b = (unsigned char *)buf;
    (void)fd;
    memset(b, 0, sz);
    if (sz == 4) { b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F'; }
    else if (sz >= 6) { b[0] = 1; memcpy(b + 1, "CD001", 5); }
    return (int)sz;
}
static void genClose(int fd) { (void)fd; }
static int addPopstarterCandidate(POPSTARTER_CANDIDATE_LIST *c, const char *p) { (void)p; c->count++; return 1; }
static void initPopstarterCandidates(POPSTARTER_CANDIDATE_LIST *c) { c->count = 0; }
static int buildNeutrinoOptionArgs(const NEUTRINO_LAUNCH_OPTIONS *o, NEUTRINO_OPTION_ARGS *a) { (void)o; (void)a; return 0; }
static int DashIsActive(void) { return 1; }
static int prepareNeutrinoGameID(const char *p, char *id, size_t n) { (void)p; snprintf(id, n, "SLUS_123.45"); return 1; }
static int showNeutrinoLaunchOptionsForGame(NEUTRINO_LAUNCH_OPTIONS *o, const char *id) {
    (void)o; (void)id;
    /* Menu prompts run while audio is still unmuted. */
    assert(!muted);
    return commit != 0;
}
static int showNeutrinoLaunchOptions(NEUTRINO_LAUNCH_OPTIONS *o) { return showNeutrinoLaunchOptionsForGame(o, ""); }
static int preparePopstarterCandidateLaunch(POPSTARTER_CANDIDATE_LIST *c, char *s, size_t sn, char *f, size_t fn, char *p, size_t pn, int *k) {
    (void)c;
    snprintf(s, sn, "usb0:/NEUTRINO/neutrino.elf");
    snprintf(f, fn, "usb0:/NEUTRINO/neutrino.elf");
    if (pn > 0) p[0] = '\0';
    *k = 1;
    return POPSTARTER_OK;
}
static void displayRetroGemGameID(const char *id, int m) { (void)id; (void)m; }
static void RunLoaderElfKeepIOP(char *f, char *p, const char *s, int k) {
    (void)f; (void)p; (void)s; (void)k;
    assert(muted && shutdown_calls >= 2 && pad_ended == 1);
    longjmp(launched, 1);
}
static void cleanUpFree(void *p) { (void)p; }
#define free cleanUpFree
'''
code += extract_func(dash_src, "int DashPrepareKeepIopHandoff(") + "\n"
code += extract_func(actions_src, "static void cleanUpForExecInternal(") + "\n"
code += extract_func(actions_src, "int CleanUpForKeepIopExec(") + "\n"
code += "#undef free\n"
code += neutrino_src[neutrino_src.index("static void setMessage("):] + "\n"
code += r'''
static void ExecuteMainAction(char *p, const MainExecuteContext *ctx) {
    calls++;
    if (IsNeutrinoIsoPath(p)) {
        LaunchArgsClear();
        LaunchNeutrinoIso(p, (char *)ctx->main_msg, MAX_PATH);
        return;
    }
    assert(muted);
}
static void dashGfxCheckScreenChange(void) {}
static void dashSoundMaintain(void) {}
static void dashSoundApplyVolume(void) { muted = 0; restored++; }
static void dashBgmStart(void) { bgm++; }
static void dashLoadingEnd(void) {}
static void friendlyLaunchError(const DashApp *a, const char *m) { (void)a; (void)m; }
''' + extract_func(dash_src, "void dashLaunchApp(") + r'''
int main(void) {
    DashApp app = {"usb0:/game.iso", "Game", 0};
    setting = calloc(1, sizeof(*setting));
    dashLaunchApp(NULL, 0); assert(!calls);
    for (int failure = 0; failure <= 1; failure++) {
        commit = -failure;
        dashLaunchApp(&app, 0);
        assert(!muted && !suspended && !shutdown_calls && restored == failure+1 && bgm == failure+1);
    }
    commit = 1;
    if (!setjmp(launched)) { dashLaunchApp(&app, 0); assert(0); }
    assert(muted && !suspended && shutdown_calls >= 2 && pad_ended == 1 && restored == 2);
    setting = calloc(1, sizeof(*setting));
    muted = commit = shutdown_calls = pad_ended = 0;
    strcpy(app.path,"usb0:/app.elf");
    dashLaunchApp(&app, 0);
    assert(!muted && suspended == 1 && restored == 3 && bgm == 3);
    free(setting);
    puts("PASS: ISO menu audio, cancellation/failure recovery, committed launch and ordinary ELF muting");
}
'''
with tempfile.TemporaryDirectory(prefix="neutrino-menu-audio-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    exe = path / "test.exe"
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)

