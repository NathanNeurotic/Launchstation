"""Execute the dashboard launch path: Neutrino prompts precede audio teardown."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_main.c").read_text()
start = source.index("void dashLaunchApp(")
end = source.index("\n}\n", start) + 3
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#define MAX_PATH 1024
#define DASH_TITLE_MAX 64
#define DASH_LAUNCH_PENDING_ARGS 2
#define DASH_APP_HAS_ARGS 2
#define DI_EDIT 1
#define DI_WARNING 2
#define TEXTEDITOR_RESULT_LAUNCH_ARGS 1
typedef struct { char path[MAX_PATH], title[DASH_TITLE_MAX]; int flags; } DashApp;
typedef struct { char main_msg[MAX_PATH]; } MainExecuteContext;
static MainExecuteContext exec_ctx;
static void (*mainActionLaunchHook)(const char *);
static void (*drawMsgOverride)(const char *);
static int muted, suspended, restored, bgm, calls, commit;
static jmp_buf launched;
static void dashLaunchHook(const char *p) { (void)p; }
static void dashMsgHook(const char *p) { (void)p; }
static void dashStrCopy(char *d, const char *s, size_t n) { snprintf(d,n,"%s",s); }
static void LaunchArgsClear(void) {}
static int LaunchArgsLoadFromBuffer(const char *p, char *a, size_t n, char *m, int s) {
    (void)p; (void)a; (void)n; (void)m; (void)s; return 0;
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
static int IsNeutrinoIsoPath(const char *p) { return strstr(p,".iso") != NULL; }
static void dashSoundSuspend(void) { muted = 1; suspended++; }
static void dashGfxRestoreLegacyState(void) {}
static void dashClassicUiInstall(int n) { (void)n; }
static void ExecuteMainAction(const char *p, MainExecuteContext *ctx) {
    calls++;
    if (IsNeutrinoIsoPath(p)) {
        /* Both cancellation and preparation failures return from this menu. */
        assert(!muted);
        if (commit > 0) {
            /* Existing committed-launch cleanup ends audsrv before exec. */
            muted = 1;
            longjmp(launched, 1);
        }
        if (commit < 0) snprintf(ctx->main_msg,sizeof(ctx->main_msg),"Neutrino not found");
    } else assert(muted);
}
static void dashGfxCheckScreenChange(void) {}
static void dashSoundMaintain(void) {}
static void dashSoundApplyVolume(void) { muted = 0; restored++; }
static void dashBgmStart(void) { bgm++; }
static void dashLoadingEnd(void) {}
static void friendlyLaunchError(const DashApp *a, const char *m) { (void)a; (void)m; }
''' + source[start:end] + r'''
int main(void) {
    DashApp app = {"usb0:/game.iso", "Game", 0};
    dashLaunchApp(NULL, 0); assert(!calls);
    for (int failure = 0; failure <= 1; failure++) {
        commit = -failure;
        dashLaunchApp(&app, 0);
        assert(!muted && !suspended && restored == failure+1 && bgm == failure+1);
    }
    commit = 1;
    if (!setjmp(launched)) { dashLaunchApp(&app, 0); assert(0); }
    assert(muted && !suspended && restored == 2);
    muted = commit = 0;
    strcpy(app.path,"usb0:/app.elf");
    dashLaunchApp(&app, 0);
    assert(!muted && suspended == 1 && restored == 3 && bgm == 3);
    puts("PASS: ISO menu audio, cancellation/failure recovery, committed launch and ordinary ELF muting");
}
'''
# The executable harness stubs exec; pin its teardown assumption to production.
actions = (root / "src/main_actions.c").read_text()
neutrino = (root / "src/neutrino_iso.c").read_text()
assert 'dashSoundShutdown(); /* the launch is now committed */' in actions
assert neutrino.index('CleanUpForKeepIopExec("Preparing Neutrino")') < neutrino.index('RunLoaderElfKeepIOP(')
with tempfile.TemporaryDirectory(prefix="neutrino-menu-audio-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    exe = path / "test.exe"
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
