"""Execute the production asynchronous audio lifecycle, failures and theme changes."""
from dash_sound_harness import run
from pathlib import Path

run(r'''
reset_sound();foreign_busy=1;assert(!dashSoundInit());dashSoundPlay(DASH_SND_BOOT);
assert(sound_boot_pending && !active);
foreign_busy=0;dashSoundMaintain();finish_all();assert(plays==1 && !sound_boot_pending);
reset_sound();foreign_busy=1;assert(!dashSoundInit() && !active && !module_calls);
foreign_busy=0;parked=1;dashSoundMaintain();assert(!active);
parked=0;dashSoundMaintain();assert(active && !module_calls && !sound_ready);
dashSoundPlay(DASH_SND_BOOT);dashSoundPlay(DASH_SND_CURSOR);dashSoundApplyVolume();
assert(!plays && sound_boot_pending);
finish_all();assert(soundAlive() && module_calls==2 && uploads==8 && plays==1 && !sound_boot_pending);
for(int i=0;i<10;i++) { dashSoundMaintain(); }
assert(submits==1 && uploads==8);
reset_sound();dashSoundInit();dashCfg.sound_theme=1;dashCfg.sound_vol=73;
finish_job();assert(soundAlive() && uploads==8 && theme_reload_pending && gain[0]==73);
dashSoundMaintain();assert(active && !theme_reload_pending);finish_all();assert(uploads==16 && gain[0]==73);
foreign_busy=1;dashCfg.sound_theme=0;dashSoundReloadTheme();assert(theme_reload_pending && !active);
foreign_busy=0;dashSoundMaintain();finish_all();assert(uploads==24);
reset_sound();sample_failures=1;dashSoundMaintain();finish_all();
assert(soundAlive() && warnings==1 && uploads==8);
for(int i=0;i<8;i++) assert(!samples[i].buffer);
for(int fail=0;fail<3;fail++) {
    reset_sound();module_failure=fail==0;init_failure=fail==1;adpcm_failure=fail==2;
    dashSoundMaintain();assert(active && !warnings);finish_all();assert(!soundAlive() && warnings==1);
    int attempts=module_calls;for(int i=0;i<10;i++) { dashSoundMaintain(); }
assert(module_calls==attempts && !active);
    module_failure=init_failure=adpcm_failure=0;generation++;dashSoundMaintain();finish_all();assert(soundAlive());
}
reset_sound();dashSoundInit();dashSoundShutdown();assert(awaits==1 && !active && !sound_ready && quits==1 && stops==1);
reset_sound();dashSoundInit();generation++;forbidden_rpc=1;finish_all();assert(!sound_ready && !module_calls);
forbidden_rpc=0;dashSoundMaintain();finish_all();assert(soundAlive());
generation++;forbidden_rpc=1;dashSoundShutdown();assert(!sound_ready && !quits);
forbidden_rpc=0;
reset_sound();rejected=1;dashSoundMaintain();assert(!active && !sound_job_id && !module_calls);
rejected=0;dashSoundMaintain();finish_all();assert(soundAlive());
reset_sound();dashCfg.sound=dashCfg.bgm=0;dashSoundMaintain();assert(!submits);
puts("PASS: queued audio ownership, boot deferral, theme snapshots, latest gain, stale generations, rejection, failure retries and draining shutdown");
''')

# Use the production signer entry point with the sound module. The queue's
# captured target completes without submitting more work from its callback.
init = (Path(__file__).resolve().parents[1] / 'src/init.c').read_text()
start = init.index('int prepareExploitSignerIop(void)')
end = init.index('//endfunc prepareExploitSignerIop', start)
signer = r'''
#define DPRINTF(...) ((void)0)
#define iop_reset_generation generation
static int have_exploit_signer_iop, pccf_bus_owned, pccf_loaded;
static u8 exploit_ioprp_img[16];static int size_exploit_ioprp_img=16;
static int shutdownPcCardCf(void) { return 0; }
static int smbStoppedForReset(void) { return 1; }
static void ensureCoreIoStackReady(void) {}
static int setupPad(void) { return 1; }
static int loadSecrSifModule(void) { return 1; }
static void closeKeyboardIfOpened(void) {}
static void unmountAll(void) {}
static void showRebootingIopMsg(void) {}
static void SifInitRpc(int n) { (void)n; }
static void resetUsbHostControllerBeforeIopReset(void) {}
static int SifIopRebootBuffer(void *p,int size) {
    (void)p;(void)size;
    assert(!active && !sound_job_id && !sound_ready && !handles);
    return 1;
}
static int SifIopSync(void) { return 1; }
static void SifLoadFileInit(void) {}
static void initsbv_patches(void) {}
static void clearIopModuleState(void) {}
''' + init[start:end]
run(r'''
reset_sound();dashCfg.bgm=0;dashSoundInit();
dashCfg.bgm=1;sidecar_present=1;
finish_job();assert(!active && !sound_job_id); /* an AwaitAll of ID 1 ends here */
dashSoundMaintain();assert(active && sound_job_id==2); /* next dashboard frame */
assert(prepareExploitSignerIop());
assert(!active && !sound_job_id && !sound_ready && awaits==1 && quits==1);
puts("PASS: completion does not extend a queue drain; signer reset drains pending audio");
''', extra=signer)
