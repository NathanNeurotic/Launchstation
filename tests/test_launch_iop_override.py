# Adapted from saildot4k/Launchstation at 1057feb4a3870d2bc10d771c72ff0cfa3d20be73.
"""Run the actual one-second Confirm chooser, per-launch policy and app dispatcher."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
dash = (root / 'src/dash_main.c').read_text()
actions = (root / 'src/main_actions.c').read_text()

def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "main_actions.h"
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { int unused; } DashInput;
#define PAD_CIRCLE 0x2000
#define PAD_CROSS 0x4000
#define MAX_PATH 256
#define DASH_TITLE_MAX 64
#define DASH_LAUNCH_PENDING_ARGS 2
#define DASH_APP_HAS_ARGS 2
#define TEXTEDITOR_RESULT_LAUNCH_ARGS 1
#define DI_EDIT 1
#define DI_WARNING 2
typedef struct { char path[MAX_PATH],title[DASH_TITLE_MAX];int flags; } DashApp;
static struct { int reboot_iop_elf_load; } config,*setting=&config;
static MainExecuteContext exec_ctx;
static char message[MAX_PATH];
static int dash_active,swapKeys,worker,loading,frames,begins,ends,polls,parks;
static int invalid_sample;
static u32 paddata;
static u64 tick,release_at,busy_until,parked_until,park_during_poll;
static int settle_calls,fail_settle,launches,last_mode,editor_result,args_result,args_clears;
static int recent,errors;
static int music_gain,effects_gain,suspends,shutdowns,run_game,handoff_result;
static u64 dispatched_at,editor_hold;
static u64 Timer(void) { return tick; }
static int dashWorkerOnThread(void) { return worker; }
static int dashWorkerIoBusy(void) { return tick<busy_until; }
static int dashIopParked(void) { return tick<parked_until; }
static void dashWorkerParkPoint(void) { parks++; }
static int dashInputPollPhysical(DashInput *in) {
    (void)in;assert(!dashWorkerIoBusy() && !dashIopParked());polls++;
    if(park_during_poll && tick>=park_during_poll) {
        parked_until=tick+50;park_during_poll=0;return 0; /* no physical sample */
    }
    if(invalid_sample)return 0; /* DS34 can leave stale held bits on failure. */
    paddata=tick<release_at?(swapKeys?PAD_CROSS:PAD_CIRCLE):0;
    return 1;
}
static void dashLoadingBegin(const char *title) { (void)title;loading=1;begins++; }
static float dashVisualUpdate(void) { return 0.01f; }
static void dashRenderScene(void) { frames++; }
static void dashGfxPresent(void) { tick+=10; }
static void dashLoadingEnd(void) { loading=0;ends++; }
'''
program += function(dash, 'int DashChooseElfIopMode(')
program += function(actions, 'static int elfLaunchIopMode(')
program += r'''
static int genCmpFileExt(const char *p,const char *extension) {
    const char *dot=strrchr(p,'.');return dot && !strcasecmp(dot+1,extension);
}
static int IsPopstarterVcdPath(const char *p) { return genCmpFileExt(p,"VCD"); }
static int IsNeutrinoIsoPath(const char *p) { return genCmpFileExt(p,"ISO"); }
static int IsEmberGamePath(const char *p) { return genCmpFileExt(p,"BIN") || genCmpFileExt(p,"EXE"); }
static void dashStrCopy(char *out,const char *in,int cap) { snprintf(out,cap,"%s",in); }
static void LaunchArgsClear(void) { args_clears++; }
static void dashModalMessage(int icon,const char *title,const char *text,const char *detail) {
    (void)icon;(void)title;(void)text;(void)detail;
}
static int dashTextEditorEx(const char *path,int mode) {
    (void)path;(void)mode;
    if(editor_hold) { paddata=swapKeys?PAD_CROSS:PAD_CIRCLE;release_at=tick+editor_hold; }
    return editor_result;
}
static int settleBackgroundWork(const char *title) {
    (void)title;settle_calls++;tick+=1500;
    return settle_calls!=fail_settle;
}
static int argSidecarExists(const char *path) { (void)path;return 0; }
static int dashLibLoadArgs(const DashApp *app,char *args,int cap) {
    (void)app;snprintf(args,cap,"arg");return 3;
}
static int LaunchArgsLoadFromBuffer(const char *path,const char *args,int len,char *msg,int size) {
    (void)path;(void)args;(void)len;(void)msg;(void)size;return args_result;
}
static int dashArtTakeDirty(void) { return 0; }
static void dashLibSaveCache(void) {}
static void dashStorePushRecent(const char *path) { (void)path;recent++; }
static void filerRootMcPollIdle(void) {}
static void dashBgFlash(float intensity) { (void)intensity; }
static void dashLoadingStatus(const char *path,int done,int total) { (void)path;(void)done;(void)total; }
static void dashSoundSuspend(void) { suspends++;music_gain=effects_gain=0; }
static void dashSoundShutdown(void) { shutdowns++;music_gain=effects_gain=0; }
static void dashGfxRestoreLegacyState(void) {}
static void dashClassicUiInstall(int on) { (void)on; }
static void dashGfxCheckScreenChange(void) {}
static void dashMsgHook(const char *msg) { (void)msg; }
static void (*drawMsgOverride)(const char *);
static void dashLaunchHook(const char *path) { (void)path; }
void (*mainActionLaunchHook)(const char *path);
static void dashSoundMaintain(void) {}
static void dashSoundApplyVolume(void) { effects_gain=60; }
static void dashBgmStart(void) { music_gain=10; }
static void friendlyLaunchError(const DashApp *app,const char *msg) { (void)app;(void)msg;errors++; }
'''
program += function(dash, 'int DashPrepareKeepIopHandoff(')
program += r'''
void ExecuteMainAction(char *path,const MainExecuteContext *ctx) {
    assert(ctx!=&exec_ctx);
    if(IsNeutrinoIsoPath(path)) {
        /* Options (and nested choices) must inherit live music and effects. */
        assert(music_gain==10 && effects_gain==60 && !suspends && !shutdowns);
        if(run_game) {
            handoff_result=DashPrepareKeepIopHandoff("Preparing Neutrino");
            if(handoff_result)assert(!music_gain && !effects_gain && shutdowns==1);
            else assert(music_gain==10 && effects_gain==60 && !shutdowns);
        }
    } else assert(!music_gain && !effects_gain);
    /* Real file validation/loading is covered by the other launch tests.
     * Capture the actual production policy forwarded across this boundary. */
    launches++;last_mode=ctx->elf_iop_mode;dispatched_at=tick;
    if(last_mode!=MAIN_ELF_IOP_DEFAULT)
        assert(elfLaunchIopMode(ctx)==last_mode-1);
}
'''
program += function(dash, 'void dashLaunchApp(')
program += r'''
static void reset(int saved,int mapping,u64 hold) {
    memset(&exec_ctx,0,sizeof(exec_ctx));exec_ctx.main_msg=message;
    config.reboot_iop_elf_load=saved;swapKeys=mapping;
    dash_active=1;worker=loading=frames=begins=ends=polls=parks=invalid_sample=0;
    settle_calls=fail_settle=launches=last_mode=recent=errors=args_clears=0;
    editor_result=args_result=1;editor_hold=0;
    music_gain=10;effects_gain=60;suspends=shutdowns=run_game=handoff_result=0;
    tick=5000;release_at=tick+hold;busy_until=parked_until=park_during_poll=0;
    paddata=hold?(swapKeys?PAD_CROSS:PAD_CIRCLE):0;
}
static void app(DashApp *app,const char *path) {
    memset(app,0,sizeof(*app));snprintf(app->path,sizeof(app->path),"%s",path);
    snprintf(app->title,sizeof(app->title),"Application");
}
int main(void) {
    DashApp selected;
    for(int saved=0;saved<2;saved++)for(int mapping=0;mapping<2;mapping++) {
        reset(saved,mapping,0);
        assert(DashChooseElfIopMode(saved)==saved && tick==5000 && !polls && !frames);
        reset(saved,mapping,100);
        assert(DashChooseElfIopMode(saved)==saved && tick==5100 && !begins && !ends && !loading);
        reset(saved,mapping,999);
        assert(DashChooseElfIopMode(saved)==saved && tick==6000);
        reset(saved,mapping,1001);
        assert(DashChooseElfIopMode(saved)==!saved && tick==6000 && !begins && !ends && !loading);
        assert(config.reboot_iop_elf_load==saved && exec_ctx.elf_iop_mode==MAIN_ELF_IOP_DEFAULT);
        reset(saved,mapping,10000);loading=1;
        assert(DashChooseElfIopMode(saved)==!saved && loading && !begins && !ends);
        reset(saved,mapping,10000);worker=1;
        assert(DashChooseElfIopMode(saved)==saved && !polls && !frames);
        reset(saved,mapping,10000);dash_active=0;
        assert(DashChooseElfIopMode(saved)==saved && !polls);
        reset(saved,mapping,100);busy_until=tick+1500;
        assert(DashChooseElfIopMode(saved)==saved && polls==1 && tick==6500);
        reset(saved,mapping,10000);busy_until=tick+1500;
        assert(DashChooseElfIopMode(saved)==!saved && polls==1 && tick==6500);
        reset(saved,mapping,100);parked_until=tick+1500;
        assert(DashChooseElfIopMode(saved)==saved && polls==1 && tick==6500 && parks>0);
        reset(saved,mapping,1001);park_during_poll=tick+1000;
        assert(DashChooseElfIopMode(saved)==saved && tick==6050);
        /* Every forced policy bypasses polling, even while Confirm stays down. */
        reset(saved,mapping,10000);exec_ctx.elf_iop_mode=MAIN_ELF_IOP_KEEP;
        assert(elfLaunchIopMode(&exec_ctx)==0 && !polls);
        exec_ctx.elf_iop_mode=MAIN_ELF_IOP_REBOOT;
        assert(elfLaunchIopMode(&exec_ctx)==1 && !polls);
        exec_ctx.elf_iop_mode=MAIN_ELF_IOP_DEFAULT;
        assert(elfLaunchIopMode(&exec_ctx)==!saved && tick==6000);
        /* Real app dispatch freezes the choice BEFORE two long preparation
         * waits. The saved value and reusable parent context remain intact. */
        reset(saved,mapping,1001);app(&selected,"mc0:/APP/BOOT.ELF");
        dashLaunchApp(&selected,0);
        assert(launches==1 && last_mode==(!saved?MAIN_ELF_IOP_REBOOT:MAIN_ELF_IOP_KEEP));
        assert(dispatched_at==9000 && config.reboot_iop_elf_load==saved && !exec_ctx.elf_iop_mode);
        assert(polls==101 && errors==1); /* simulated loader return */
        paddata=0;dashLaunchApp(&selected,0);
        assert(launches==2 && last_mode==(saved?MAIN_ELF_IOP_REBOOT:MAIN_ELF_IOP_KEEP));
        reset(saved,mapping,100);dashLaunchApp(&selected,DASH_LAUNCH_PENDING_ARGS);
        assert(launches==1 && last_mode==(saved?MAIN_ELF_IOP_REBOOT:MAIN_ELF_IOP_KEEP));
        reset(saved,mapping,1001);fail_settle=1;
        dashLaunchApp(&selected,0);assert(!launches && !exec_ctx.elf_iop_mode);
        paddata=0;fail_settle=0;dashLaunchApp(&selected,0);
        assert(launches==1 && last_mode==(saved?MAIN_ELF_IOP_REBOOT:MAIN_ELF_IOP_KEEP));
        reset(saved,mapping,0);editor_hold=1001;
        dashLaunchApp(&selected,1);
        assert(launches==1 && last_mode==(!saved?MAIN_ELF_IOP_REBOOT:MAIN_ELF_IOP_KEEP));
        reset(saved,mapping,1001);editor_result=0;
        dashLaunchApp(&selected,1);assert(!launches && !polls);
        const char *game_paths[]={"usb0:/games/game.ISO","usb0:/POPS/game.VCD",
            "usb0:/EMBER/games/Game/game.CUE","usb0:/EMBER/games/Game/game.BIN"};
        for(unsigned i=0;i<sizeof(game_paths)/sizeof(*game_paths);i++) {
            reset(saved,mapping,10000);app(&selected,game_paths[i]);
            dashLaunchApp(&selected,0);
            assert(launches==1 && last_mode==MAIN_ELF_IOP_DEFAULT && !polls && !frames);
            assert(music_gain==10 && effects_gain==60); /* cancel/failure recovery */
        }
    }
    reset(1,0,10000);busy_until=100000;
    assert(DashChooseElfIopMode(1)==1 && tick==10000 && polls==0);
    reset(0,0,10000);parked_until=100000;
    assert(DashChooseElfIopMode(0)==0 && tick==10000 && polls==0);
    for(int saved=0;saved<2;saved++) {
        reset(saved,0,10000);invalid_sample=1;
        assert(DashChooseElfIopMode(saved)==saved && tick==10000 && polls>0);
    }
    /* Neutrino cancels before handoff, commits after Run, or backs out while
     * finishing background work. Only a committed handoff shuts audio down. */
    for(int commit=0;commit<3;commit++) {
        reset(0,0,0);app(&selected,"usb0:/games/game.ISO");
        run_game=commit!=0;if(commit==2)fail_settle=3;
        dashLaunchApp(&selected,0);
        assert(launches==1 && !suspends && shutdowns==(commit==1));
        assert(handoff_result==(commit==1));
        assert(music_gain==10 && effects_gain==60 && !loading);
    }
    puts("PASS: Neutrino options retain audio, Run stops it at handoff, cancellation and failed preparation restore playback");
    puts("PASS: one-second hold inverts both saved modes/mappings, tap/release, keyboard/automatic, worker/IOP safety and fresh samples");
    puts("PASS: real app dispatch carries a one-launch mode across preparation, failures and arguments without saving it; game-image policies retained");
}
'''
# The two actual handoffs must use the same per-launch mode for cleanup and
# loader flags; the memory-boot path is easy to accidentally leave behind.
assert actions.count('x = elfLaunchIopMode(ctx);') == 2
assert 'x = setting->reboot_iop_elf_load;' not in actions
assert 'RunLoaderElf(fullpath, party, path, t, x);' in actions
assert 'RunLoaderMemory("rom0:HDDBOOT", mbr_mem_arg, x);' in actions
iso = (root/'src/neutrino_iso.c').read_text()
launch = function(iso, 'int LaunchNeutrinoIso(')
assert launch.index('showNeutrinoLaunchOptions(&options)') < launch.index('CleanUpForKeepIopExec(') < launch.index('RunLoaderElfKeepIOP(')
cleanup = function(actions, 'int CleanUpForKeepIopExec(')
assert cleanup.index('DashPrepareKeepIopHandoff(') < cleanup.index('cleanUpForExecInternal(1)')
main = (root/'src/main.c').read_text()
assert 'MainExecuteContext exec_ctx = {0};' in main
assert 'MainExecuteContext dash_ctx = {0};' in main
compiler = shutil.which('gcc') or shutil.which('clang')
assert compiler, 'Host C compiler required'
with tempfile.TemporaryDirectory(prefix='launch-iop-override-') as tmp:
    path, exe = Path(tmp)/'test.c', Path(tmp)/'test'
    path.write_text(program)
    for flags in ([], ['-DDS34']):
        subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror',*flags,
                        '-I',str(root/'include'),str(path),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=5)
