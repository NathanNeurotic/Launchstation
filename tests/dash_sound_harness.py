"""Compile the complete production sound module with a controlled worker and RPC service."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def run(body):
    source = (ROOT / 'src/dash_sound.c').read_text()
    blobs = re.findall(r'extern u8 (\w+)\[\];', source)
    sizes = re.findall(r'extern int (size_\w+);', source)
    source = re.sub(r'^#include .*$', '', source, flags=re.M)
    code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dash_sound.h"
#define MAX_PATH 256
#define DASH_TOAST_WARN 1
typedef uint8_t u8; typedef uint32_t u32;
typedef struct { void *buffer; } audsrv_adpcm_t;
static struct { int sound,bgm,sound_vol,bgm_vol,sound_theme; } dashCfg={1,1,50,40,0};
static unsigned generation;
static int worker_thread, foreign_busy, parked, rejected, active, submits, awaits;
static int module_calls, init_calls, resets, quits, stops, uploads, bgm_uploads, warnings, plays, gain[24];
static int module_failure, init_failure, adpcm_failure, sample_failures, forbidden_rpc;
static int sidecar_present, stack_loaded=1, opens, handles, short_read, read_error, close_error, allocations;
static int position, sidecar_size=80;
static unsigned char sidecar[256];
static char LaunchElfBootDir[MAX_PATH]="mass0:/BOOT/", LaunchElfDir[MAX_PATH]="mc0:/BOOT/", last_path[MAX_PATH];
typedef void (*JobFn)(void *);
static JobFn job_run, job_done; static void *job_arg;
static void rpc(void);
static void finish_job(void);
static unsigned getIopResetGeneration(void) { return generation; }
static int dashWorkerIoBusy(void) { return foreign_busy || worker_thread; }
static int dashIopParked(void) { return parked; }
static int dashWorkerSubmit(const char *title,int flags,JobFn run,JobFn done,void *arg) {
    assert(!worker_thread && !strcmp(title,"Preparing audio") && !flags);
    if(rejected || active) return -1;
    job_run=run;job_done=done;job_arg=arg;active=1;return ++submits;
}
static int dashWorkerAwait(int id,const char *title,int cancelable) {
    assert(id>0 && title && !cancelable);awaits++;finish_job();return 1;
}
static int isPathDeviceStackLoaded(const char *path) { assert(path[0]);return stack_loaded; }
static void filerRootMcPollIdle(void) { assert(worker_thread); }
static int SifExecModuleBuffer(void *p,int size,int n,void *args,int *ret) {
    (void)p;(void)size;(void)n;(void)args;assert(worker_thread);rpc();module_calls++;
    *ret=0;return module_failure ? -1 : 1;
}
static int audsrv_init(void) { rpc();assert(worker_thread);init_calls++;return init_failure; }
static int audsrv_adpcm_init(void) { rpc();resets++;return adpcm_failure; }
static int audsrv_stop_audio(void) { rpc();stops++;return 0; }
static void audsrv_quit(void) { rpc();quits++; }
static int audsrv_load_adpcm(audsrv_adpcm_t *sample,void *data,int size) {
    (void)data;rpc();assert(worker_thread);
    if(size>=48) bgm_uploads++;
    else { uploads++; if(sample_failures) { sample->buffer=NULL;return -1; } }
    sample->buffer=sample;return 0;
}
static int audsrv_ch_play_adpcm(int ch,audsrv_adpcm_t *sample) {
    rpc();assert(!worker_thread && ch>=0 && ch<24 && sample->buffer);plays++;return ch;
}
static void audsrv_adpcm_set_volume_and_pan(int ch,int volume,int pan) {
    rpc();assert(ch>=0 && ch<24 && !pan);gain[ch]=volume;
}
static void dashToast(int kind,const char *message) { assert(!worker_thread && kind==DASH_TOAST_WARN && message[0]);warnings++; }
static int genFixPath(const char *p,char *out) { snprintf(out,MAX_PATH,"%s",p);return 0; }
static int genOpen(const char *path,int flags) {
    (void)flags;assert(worker_thread);opens++;snprintf(last_path,MAX_PATH,"%s",path);
    if(!sidecar_present) return -1;
    assert(!handles++);position=0;return 7;
}
static int genLseek(int fd,int offset,int whence) {
    assert(fd==7);if(whence==SEEK_END) position=sidecar_size+offset;else position=offset;return position;
}
static int genRead(int fd,void *data,int len) {
    assert(fd==7 && worker_thread);
    if(read_error) return -1;
    if(short_read && len>short_read) len=short_read;
    if(len>sidecar_size-position) len=sidecar_size-position;
    memcpy(data,sidecar+position,len);position+=len;return len;
}
static int genClose(int fd) { assert(fd==7 && handles==1);handles--;return close_error?-1:0; }
#define FIO_O_RDONLY 0
static void *test_memalign(size_t align,size_t size) { (void)align;allocations++;return malloc(size); }
static void test_free(void *p) { if(p) allocations--;free(p); }
#define memalign test_memalign
#define free test_free
'''
    code += source
    code += '\n#undef free\n'
    code += '\n'.join(f'u8 {name}[16];' for name in blobs)
    code += '\n' + '\n'.join(f'int {name}=16;' for name in sizes)
    code += r'''
static void rpc(void) {
    assert(!forbidden_rpc && !parked);
    assert(!sound_job_id || worker_thread || sound_closing);
}
static void finish_job(void) {
    assert(active);JobFn done=job_done;void *arg=job_arg;
    worker_thread=1;job_run(arg);worker_thread=0;active=0;done(arg);
}
static void finish_all(void) { for(int i=0;active;i++) { assert(i<5);finish_job(); } }
static void reset_sound(void) {
    foreign_busy=parked=forbidden_rpc=0;dashSoundShutdown();assert(!handles && !allocations);
    generation++;module_calls=init_calls=resets=quits=stops=uploads=bgm_uploads=warnings=plays=0;
    submits=awaits=rejected=active=module_failure=init_failure=adpcm_failure=sample_failures=0;
    sidecar_present=opens=read_error=close_error=short_read=0;stack_loaded=1;
    dashCfg.sound=dashCfg.bgm=1;dashCfg.sound_vol=50;dashCfg.bgm_vol=40;dashCfg.sound_theme=0;
    memset(sidecar,0,sizeof(sidecar));memcpy(sidecar,"APCM",4);sidecar[4]=sidecar[5]=1;
    sidecar[8]=0xb3;sidecar[9]=0x0e;sidecar_size=80;
}
int main(void) {
'''
    code += body
    code += '\nforeign_busy=parked=forbidden_rpc=0;dashSoundShutdown();assert(!handles && !allocations);return 0;\n}\n'
    with tempfile.TemporaryDirectory(prefix='audio-worker-') as folder:
        folder = Path(folder)
        (folder/'test.c').write_text(code)
        exe = folder/'test.exe'
        compiler = shutil.which('gcc') or shutil.which('clang')
        assert compiler
        subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function',
                        '-I',str(ROOT/'include'),str(folder/'test.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=15)
