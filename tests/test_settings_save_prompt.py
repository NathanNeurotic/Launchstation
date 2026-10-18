"""Host regression tests of production Settings session/save/leave decisions.

Serializers and I/O are mocked; this is not console or filesystem validation.
Pass an optional git ref to test the same assertions against the old source.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (subprocess.check_output(["git", "show", f"{sys.argv[1]}:src/dash_settings.c"],
                                  cwd=root, text=True) if len(sys.argv) > 1 else
          (root / "src/dash_settings.c").read_text())


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef struct { int sound, bgm, recent_count, artwork; char path[64], software_clock[24]; } DashSettings;
typedef struct { int color; } DashTheme;
typedef struct {
    int TV_mode, screen_x, screen_y, language, swapKeys;
    char lang_file[64], font_file[64], path[64];
} SETTING;
static DashSettings dashCfg;
static DashTheme dashTheme;
static SETTING config, *setting = &config;
static char ip[16]="192.168.0.10", netmask[16]="255.255.255.0", gw[16]="192.168.0.1";
static int preset, custom, swapKeys;
static int cfg_pending[2], cfg_failed[2], store_failed, store_pending;
static int modal_calls, modal_result=2, queue_ok=1, queued[3];
enum { CFG_SAVE_CNF, CFG_SAVE_NET, DI_CHECK, DI_CLOSE, DI_BACK, DASH_TOAST_INFO };
typedef struct { int icon; const char *label, *detail; int disabled; } DashMenuItem;
static u64 Timer(void) { static u64 t; return t+=300; }
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static int dashThemeBasePreset(void) { return preset; }
static int dashThemeIsCustom(void) { return custom; }
static int dashStoreSettingsFailed(void) { return store_failed && !store_pending; }
static char *dashStoreSettingsText(const DashSettings *c,int *len) {
    char s[256]; *len=snprintf(s,sizeof(s),"%d/%d/%d/%d/%s/%d/%d/%d",
        c->sound,c->bgm,c->recent_count,c->artwork,c->path,dashTheme.color,preset,custom);
    return strdup(s);
}
static char *configBuildSaveText(int *len) {
    char s[512]; *len=snprintf(s,sizeof(s),"%d/%d/%d/%d/%d/%s/%s/%s",
        setting->TV_mode,setting->screen_x,setting->screen_y,setting->language,
        setting->swapKeys,setting->lang_file,setting->font_file,setting->path);
    return strdup(s);
}
static int dashStoreSaveSettings(void) { queued[2]++; store_pending=1; return 0; }
static int cfgSave(int kind,int target,const char *path) {
    (void)target; (void)path; queued[kind]++;
    cfg_failed[kind]=!queue_ok; cfg_pending[kind]=queue_ok; return queue_ok;
}
static int dashModalMenu(const char *t,const char *d,DashMenuItem *m,int n,int s) {
    (void)t; (void)m; (void)s; assert(n==3 && d[0]!=' ');
    modal_calls++; return modal_result;
}
static void dashToast(int kind,const char *s) { (void)kind; (void)s; }
static int clock_snapshots, clock_restores;
static int software_clock_active;
static int menuTitleSoftwareClockActive(void) { return software_clock_active; }
static void menuTitleClockSnapshot(void) { clock_snapshots++; }
static void menuTitleClockRestore(void) { clock_restores++; }
static void updateScreenMode(void) {}
static int dashWorkerAwaitAll(const char *s) { (void)s; return 1; }
static void Set_Language(int l) { (void)l; }
static void Load_External_Language(void) {}
static void loadFont(const char *s) { (void)s; }
static void dashThemeRestore(const DashTheme *t,int p,int c) { dashTheme=*t; preset=p; custom=c; }
static void DashThemeSetLegacyEnabled(int e) { (void)e; }
static void dashSoundInit(void) {}
static void dashSoundApplyVolume(void) {}
static void dashBgmStart(void) {}
static void dashBgmStop(void) {}
static void dashBgmSetVolume(void) {}
static void dashArtFlushAll(void) {}
static void dashHomeRebuild(void) {}
static void dashAppsRebuildView(void) {}
static void updateIpConfigRuntimeState(void) {}
"""
code += source[source.index("static int session;"):source.index("static const char *const onoff")]
if "enum { SESSION_SAVE_CNF" in source:
    code += source[source.index("enum { SESSION_SAVE_CNF"):source.index("static void cfgSaveRun(")]
for signature in ("static char *dashSaveText(", "static char *cnfSaveText(",
                  "static int textChanged(", "static void refreshDirty("):
    code += function(signature)
# Keep the old-ref harness working while testing the new scoped failure helper.
if "static int failedSaves(" in source:
    code += function("static int failedSaves(")
code += function("static int dirtyOrFailed(")
if "static int softwareClockCheckpointAvailable(" in source:
    code += function("static int softwareClockCheckpointAvailable(")
for signature in ("static int anyDirty(", "static int anyDirtyCached(",
                  "static void snapshotTexts(", "static void sessionSnapshot(",
                  "static void sessionBegin(", "static void settingsCommit(",
                  "static void settingsRevert(", "static int settingsLeave("):
    code += function(signature)
if "static void settingsSaveAction(" in source:
    code += function("static void settingsSaveAction(")
action = source[source.index("case S_SAVE_CLASSIC:") + len("case S_SAVE_CLASSIC:"):]
code += "static void clickSaveSettings(void) {\n" + action[:action.index("break;")] + "\n}\n"
code += r"""
static void begin(void) { session=0; modal_calls=0; sessionBegin(); }
static void clean(void) {
    cfg_failed[0]=cfg_failed[1]=store_failed=0;
    cfg_pending[0]=cfg_pending[1]=store_pending=0;
    queue_ok=1; modal_result=2; begin();
}
int main(void) {
    clean(); assert(settingsLeave()==1 && !modal_calls && clock_snapshots>0);
    int restores = clock_restores;
    clean(); dashCfg.sound++; modal_result=1;
    assert(settingsLeave()==1 && clock_restores==restores+1);
    clean();
    /* An unrelated failed write must not turn an untouched visit into edits. */
    for(int k=0;k<3;k++) {
        clean(); if(k<2) cfg_failed[k]=1; else store_failed=1;
        begin(); assert(!anyDirtyCached());
        assert(settingsLeave()==1 && !modal_calls);
        /* Also catch an older background save finishing during this visit. */
        clean(); if(k<2) cfg_failed[k]=1; else store_failed=1;
        assert(settingsLeave()==1 && !modal_calls);
    }
    clean(); dashCfg.sound++; dashCfg.sound--;
    assert(settingsLeave()==1 && !modal_calls);
    /* Shorten a string, restore it, leave different bytes after the NUL. */
    strcpy(dashCfg.path,"mc0:/BOOT/BOOT.ELF");
    strcpy(setting->path,"mc0:/BOOT/BOOT.ELF"); clean();
    strcpy(dashCfg.path,"mass:/a/longer/path/BOOT.ELF");
    strcpy(setting->path,"mass:/a/longer/path/BOOT.ELF");
    strcpy(dashCfg.path,"mc0:/BOOT/BOOT.ELF");
    strcpy(setting->path,"mc0:/BOOT/BOOT.ELF");
    assert(!anyDirty() && settingsLeave()==1 && !modal_calls);
    clean(); dashTheme.color++; dashTheme.color--;
    assert(settingsLeave()==1 && !modal_calls);
    /* Each persisted family really does prompt; Keep editing retains session. */
    for(int k=0;k<3;k++) {
        clean(); if(k==0) dashCfg.sound++; else if(k==1) setting->swapKeys++;
        else strcpy(ip,"192.168.0.11");
        assert(anyDirtyCached() && settingsLeave()==0 && modal_calls==1 && session);
        modal_result=1; assert(settingsLeave()==1);
        begin(); assert(!anyDirty());
    }
    /* A save requested here can still fail: no false success, retry remains. */
    clean(); dashCfg.sound++; settingsCommit();
    store_pending=0; store_failed=1;
    assert(anyDirty() && settingsLeave()==0 && modal_calls==1);
    settingsCommit(); store_pending=0; store_failed=0;
    assert(!anyDirty() && settingsLeave()==1);
    for(int k=0;k<2;k++) {
        clean(); if(k==0) setting->swapKeys++; else strcpy(ip,"192.168.0.12");
        settingsCommit(); cfg_pending[k]=0; cfg_failed[k]=1;
        assert(anyDirty() && settingsLeave()==0);
        settingsCommit(); cfg_pending[k]=0; cfg_failed[k]=0;
        assert(!anyDirty() && settingsLeave()==1);
    }
    clean(); setting->swapKeys++; queue_ok=0; modal_result=0;
    assert(settingsLeave()==0 && anyDirty());
    /* Discard acknowledges session edits, not successful persistence. */
    modal_result=1; assert(settingsLeave()==1);
    assert(cfg_failed[0]); begin(); assert(!anyDirty() && settingsLeave()==1);
    /* Explicit Save remains usable for elapsed software time without edits. */
    clean(); strcpy(dashCfg.software_clock,"2028-06-01 10:00:20"); software_clock_active=1;
    sessionSnapshot(); assert(!anyDirty());
    int clock_saves=queued[2]; clickSaveSettings(); assert(queued[2]==clock_saves+1);
    begin(); assert(!anyDirty() && settingsLeave()==1 && !modal_calls);
    software_clock_active=0; clean(); clock_saves=queued[2];
    clickSaveSettings(); assert(queued[2]==clock_saves);
    free(snap_dash_text); free(snap_cnf_text);
    puts("PASS: pristine/reverted/canceled sessions, stale failures, actual edits, failed saves/retry/discard");
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-save-prompt-") as tmp:
    harness = Path(tmp) / "save.c"
    exe = Path(tmp) / "save.exe"
    harness.write_text(code)
    subprocess.run([compiler, "-std=gnu11", str(harness), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
