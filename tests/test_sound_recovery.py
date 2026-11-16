"""Execute production audsrv initialization and reset recovery under failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_sound.c").read_text()

def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

code = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct { void *buffer; } audsrv_adpcm_t;
#define DASH_SND_COUNT 8
#define SND_SFX_CH_MAX 23
#define DASH_TOAST_WARN 1
static audsrv_adpcm_t samples[DASH_SND_COUNT], bgm_sample;
static int sound_ready, bgm_loaded, bgm_tried, bgm_channel = -1, sound_retry;
static void *bgm_data;
static unsigned int generation, sound_reboot_generation, sound_tried_generation = -1;
static struct { int sound, bgm, sound_vol, custom_sounds; } dashCfg = {1, 1, 50, 0};
static unsigned char libsd_irx[1], audsrv_irx[1];
static int size_libsd_irx = 1, size_audsrv_irx = 1;
static int busy, parked, module_failure, init_failure, adpcm_failure, sample_failures;
static int module_calls, init_calls, uploads, quits, hard_resets, stops, bgm_starts, warnings;
static int forbidden_rpc, reload_pending;
typedef unsigned char u8;
static char LaunchElfBootDir[64] = "mass0:/APPS/", LaunchElfDir[64] = "mc0:/APPS/";
static int stack_loaded = 1, custom_present[DASH_SND_COUNT], custom_loops[DASH_SND_COUNT], custom_reads, custom_uploads, custom_live;
static int isPathDeviceStackLoaded(const char *p) { assert(p == LaunchElfBootDir); return stack_loaded; }
static unsigned int getIopResetGeneration(void) { return generation; }
static int dashWorkerIoBusy(void) { return busy; }
static int dashIopParked(void) { return parked; }
static int SifExecModuleBuffer(void *p, int size, int n, void *args, int *ret) {
    (void)p; (void)size; (void)n; (void)args;
    module_calls++; *ret = 0; return module_failure ? -1 : 1;
}
static int audsrv_init(void) { assert(!forbidden_rpc); init_calls++; return init_failure; }
static int audsrv_adpcm_init(void) { assert(!forbidden_rpc); hard_resets++; return adpcm_failure; }
static int audsrv_stop_audio(void) { assert(!forbidden_rpc); stops++; return 0; }
static void audsrv_quit(void) { assert(!forbidden_rpc); quits++; }
static void *blob_data(int id) { return &samples[id]; }
static int blob_size(int id) { (void)id; return 16; }
static int audsrv_load_adpcm(audsrv_adpcm_t *sample, void *p, int size) {
    int custom = p != (void *)&samples[sample - samples];
    assert(!forbidden_rpc && size == (custom ? 32 : 16));
    if (custom) custom_uploads++;
    int fail = uploads++ < sample_failures;
    sample->buffer = fail ? NULL : sample; return fail ? -1 : 0;
}
static void audsrv_adpcm_set_volume_and_pan(int ch, int volume, int pan) {
    assert(!forbidden_rpc && ch >= 0 && ch < 23 && volume == 50 && pan == 0);
}
static void dashToast(int kind, const char *message) { assert(kind == DASH_TOAST_WARN && message[0]); warnings++; }
static void dashBgmStop(void) { assert(!forbidden_rpc); }
static void dashBgmStart(void) { bgm_starts++; bgm_tried = 1; }
void dashSoundShutdown(void);
'''
code += source[source.index("#define SFX_MAX_BYTES"):source.index("static u8 *readAdpSidecar(const char *name, int max, int *out_size);")]
code += r'''
static u8 *tracked(int loop) { u8 *d = calloc(1, 32); custom_live++; d[6] = loop == 1; d[17] = loop == 2 ? 3 : 1; return d; }
static void untrack(void *p) { if (p) custom_live--; free(p); }
static u8 *readAdpSidecar(const char *name, int max, int *out_size) {
    int i;
    assert(max == SFX_MAX_BYTES);
    for (i = 0; i < DASH_SND_COUNT; i++)
        if (name == custom_names[i]) break;
    assert(i < DASH_SND_COUNT); custom_reads++;
    if (!custom_present[i]) return NULL;
    *out_size = 32; return tracked(custom_loops[i]);
}
#define free untrack
'''
for signature in ("static void freeCustomSounds(", "static int playsOnce(", "static int uploadSamples("):
    code += function(signature)
for signature in ("static int soundAlive(", "int dashSoundInit(",
                  "void dashSoundShutdown(", "void dashSoundApplyVolume(",
                  "void dashSoundReloadEffects(", "void dashSoundMaintain("):
    code += function(signature)
code += r'''
static void reset(void) {
    forbidden_rpc = 0; dashSoundShutdown();
    generation = sound_reboot_generation = 0; sound_tried_generation = -1;
    sound_retry = busy = parked = module_failure = init_failure = adpcm_failure = sample_failures = 0;
    module_calls = init_calls = uploads = quits = hard_resets = stops = bgm_starts = warnings = 0;
    dashCfg.sound = dashCfg.bgm = 1; reload_pending = 0; dashCfg.custom_sounds = 0;
    stack_loaded = 1; custom_reads = custom_uploads = 0;
    for (int i = 0; i < DASH_SND_COUNT; i++) custom_present[i] = custom_loops[i] = 0;
    assert(!custom_live && !custom_pending);
}
int main(void) {
    /* Neither parked IOPs nor active IO may start modules or mark an attempt. */
    reset(); busy = 1; assert(!dashSoundInit());
    assert(!module_calls && sound_retry && sound_tried_generation == (unsigned)-1);
    busy = 0; parked = 1; dashSoundMaintain(); assert(!module_calls);
    parked = 0; dashSoundMaintain();
    assert(soundAlive() && module_calls == 2 && uploads == 8 && bgm_starts == 1);
    for (int i=0; i<20; i++) dashSoundMaintain();
    assert(module_calls == 2 && uploads == 8 && bgm_starts == 1);
    /* A Custom sounds change while background I/O runs is remembered and applied once idle. */
    reset(); dashSoundMaintain(); assert(soundAlive() && uploads == 8);
    dashCfg.custom_sounds = 1; busy = 1; dashSoundReloadEffects();
    assert(reload_pending && uploads == 8 && hard_resets == 1);
    dashSoundMaintain(); assert(reload_pending && uploads == 8);     /* still busy */
    busy = 0; dashSoundMaintain();
    assert(!reload_pending && uploads == 16 && hard_resets == 2);   /* effects re-uploaded */
    dashSoundMaintain(); assert(uploads == 16);                           /* and only once */
    reset(); dashCfg.custom_sounds = 1; dashSoundReloadEffects();             /* not running: nothing to reload */
    assert(!reload_pending && !uploads);
    /* Custom sounds: off reads no file; on uses each file that is present, the
       built-in effect for the rest, and keeps only the used buffers. */
    reset(); dashSoundMaintain(); assert(uploads == 8 && !custom_reads && !custom_uploads);
    reset(); dashCfg.custom_sounds = 1; custom_present[0] = custom_present[5] = 1; dashSoundMaintain();
    assert(uploads == 8 && custom_reads == 8 && custom_uploads == 2 && custom_live == 2);
    /* A rejected upload frees that file and falls back to the built-in effect. */
    reset(); dashCfg.custom_sounds = 1; custom_present[0] = 1; sample_failures = 1; dashSoundMaintain();
    assert(uploads == 9 && custom_uploads == 1 && !custom_live && samples[0].buffer && !warnings);
    /* A looping file (APCM loop byte, or end block flagged repeat) is refused: built-in effect instead. */
    reset(); dashCfg.custom_sounds = 1; custom_present[0] = custom_present[1] = 1;
    custom_loops[0] = 1; custom_loops[1] = 2; dashSoundMaintain();
    assert(uploads == 8 && !custom_uploads && !custom_live && samples[0].buffer && samples[1].buffer);
    /* A failed hard reset while files wait for the boot device is not retried every frame. */
    reset(); dashCfg.custom_sounds = 1; custom_present[1] = 1; stack_loaded = 0; dashSoundMaintain();
    assert(custom_pending && uploads == 8);
    stack_loaded = 1; adpcm_failure = 1; dashSoundMaintain();
    assert(!custom_pending && warnings == 1 && hard_resets == 2);
    for (int i=0; i<20; i++) dashSoundMaintain();
    assert(warnings == 1 && hard_resets == 2);
    adpcm_failure = 0;
    /* Switching off drops the files and uploads only built-in effects. */
    reset(); dashCfg.custom_sounds = 1; custom_present[3] = 1; dashSoundMaintain(); assert(custom_live == 1);
    dashCfg.custom_sounds = 0; dashSoundReloadEffects(); assert(!custom_live && custom_uploads == 1 && uploads == 16);
    /* Boot device not running yet: built-in effects now, the files once it is, once. */
    reset(); dashCfg.custom_sounds = 1; custom_present[1] = 1; stack_loaded = 0; dashSoundMaintain();
    assert(custom_pending && uploads == 8 && !custom_reads);
    dashSoundMaintain(); assert(uploads == 8);
    stack_loaded = 1; dashSoundMaintain();
    assert(!custom_pending && uploads == 16 && custom_uploads == 1 && custom_live == 1);
    dashSoundMaintain(); assert(uploads == 16);
    /* A healthy service remains usable by BGM if every SFX upload fails. */
    reset(); sample_failures = DASH_SND_COUNT; dashSoundMaintain();
    assert(soundAlive() && warnings == 1 && bgm_starts == 1);
    for (int i=0; i<DASH_SND_COUNT; i++) assert(!samples[i].buffer);
    for (int i=0; i<20; i++) dashSoundMaintain();
    assert(warnings == 1 && module_calls == 2 && uploads == 8);
    /* Real service failures are attempted once per generation, then recover. */
    for (int failure=0; failure<3; failure++) {
        reset(); module_failure = failure == 0; init_failure = failure == 1; adpcm_failure = failure == 2;
        dashSoundMaintain(); assert(!soundAlive() && warnings == 1 && !bgm_starts);
        int modules = module_calls, inits = init_calls;
        for (int i=0; i<20; i++) dashSoundMaintain();
        assert(module_calls == modules && init_calls == inits && warnings == 1);
        module_failure = init_failure = adpcm_failure = 0;
        generation++; dashSoundMaintain(); assert(soundAlive() && bgm_starts == 1);
    }
    /* Forget stale RPC state without contacting the destroyed old server. */
    reset(); dashSoundMaintain(); generation++; forbidden_rpc = 1;
    dashSoundShutdown(); assert(!sound_ready && !quits && !stops);
    forbidden_rpc = 0; dashSoundMaintain(); assert(soundAlive() && bgm_starts == 2);
    dashSoundShutdown(); assert(quits == 1 && stops == 1 && !sound_ready);
    /* No automatic work when both buses are off. */
    reset(); dashCfg.sound = dashCfg.bgm = 0; dashSoundMaintain(); assert(!module_calls);
    puts("PASS: production sound initialization failures, BGM independence, IO deferral and reset generations");
}
'''
with tempfile.TemporaryDirectory(prefix="sound-recovery-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    exe = path / "test.exe"
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
