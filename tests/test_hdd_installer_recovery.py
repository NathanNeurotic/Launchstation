"""Production installer RPC watchdog: retain DMA buffers until UI reset ack."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = r'''
#include <stdint.h>
typedef uint64_t u64;
typedef int32_t s32;
typedef struct {int busy; void *server;} SifRpcClientData_t;
typedef void (*SifRpcEndFunc_t)(void *);
#define SIF_RPC_M_NOWAIT 1
int SifCheckStatRpc(SifRpcClientData_t *);
s32 DelayThread(s32);
'''
dash_header = r'''
#include "sifrpc.h"
u64 Timer(void);
int dashWorkerOnThread(void);
int GetThreadId(void);
'''
code = r'''
#include "dash_internal.h"
#include "hdd_install_recovery.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
extern int __wrap_sceSifCallRpc(SifRpcClientData_t*,int,int,void*,int,void*,int,SifRpcEndFunc_t,void*);
extern int __wrap_sceSifBindRpc(SifRpcClientData_t*,int,int);
static u64 now;
static int polls,calls,binds,resets,scenario,worker=1,thread=2;
static int *live_buffer;
static SifRpcClientData_t *waiting;
u64 Timer(void){return now;}
int GetThreadId(void){return thread;}
int dashWorkerOnThread(void){return worker;}
int DashIsActive(void){return 0;}
int dashGfxCanYield(void){return 1;}
void dashLoadingRedraw(void){assert(0);}
int __real_sceSifCallRpc(SifRpcClientData_t *cd,int cmd,int mode,void *send,int size,void *recv,int rsize,SifRpcEndFunc_t cb,void *arg){
 (void)cmd;(void)send;(void)size;(void)cb;(void)arg;calls++;assert(rsize==4&&recv==live_buffer);
 if(hddInstallRecoveryActive())assert(mode&SIF_RPC_M_NOWAIT);
 cd->busy=scenario==1?1000:0;waiting=cd;
 *(int*)recv=scenario==2?HDD_INSTALL_ATA_TIMEOUT:0;return 0;
}
int __real_sceSifBindRpc(SifRpcClientData_t *cd,int server,int mode){
 assert(server==123&&(mode&SIF_RPC_M_NOWAIT));binds++;waiting=cd;
 cd->busy=scenario==3?1000:0;cd->server=scenario==4?NULL:(void*)1;return 0;
}
int SifCheckStatRpc(SifRpcClientData_t *cd){polls++;if(cd->busy)cd->busy--;return cd->busy;}
s32 DelayThread(s32 usec){
 assert(usec==1000);now+=10000;
 if(hddInstallRecoveryResetRequested()){
  assert(live_buffer&&waiting&&hddInstallRecoveryFault()==HDD_INSTALL_ATA_TIMEOUT);
  /* Real UI stops the old IOP/DMA while this caller's stack is still live. */
  assert(!hddInstallRecoveryWasReset());resets++;hddInstallRecoveryResetComplete();
 }
 return 0;
}
static void reset(int n){
 hddInstallRecoveryEnd();scenario=n;now=0;polls=calls=binds=resets=0;waiting=NULL;
 hddInstallRecoveryBegin();assert(!hddInstallRecoveryFault()&&!hddInstallRecoveryResetRequested());
}
int main(void){
 int output=1234;live_buffer=&output;SifRpcClientData_t cd={0};
 reset(0);assert(!__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)&&calls==1&&!resets&&!hddInstallRecoveryFault());
 reset(2);assert(!__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)&&output==-502&&!hddInstallRecoveryFault());
 hddInstallRecoveryObserve(output);
 assert(__wrap_sceSifCallRpc(&cd,2,0,NULL,0,&output,4,NULL,NULL)==-502&&calls==1);
 assert(__wrap_sceSifBindRpc(&cd,123,0)==-502&&!binds); /* no cleanup/rebind RPC after fault */
 reset(1);assert(__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)==-502);
 assert(resets==1&&hddInstallRecoveryWasReset()&&hddInstallRecoveryFault()==-502);
 for(int mode=3;mode<=4;mode++){
  reset(mode);assert(__wrap_sceSifBindRpc(&cd,123,0)==-502&&resets==1&&binds>=1);
 }
 reset(0);assert(!__wrap_sceSifBindRpc(&cd,123,0)&&binds==1&&!resets);
 hddInstallRecoveryEnd();assert(!hddInstallRecoveryActive()&&!hddInstallRecoveryFault());
 hddInstallRecoveryObserve(-502);assert(!hddInstallRecoveryFault());
 assert(!__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL));
 puts("PASS: returned ATA faults suppress cleanup RPCs; stalled calls/bindings retain buffers until UI reset; non-installer paths stay unchanged");
 worker=0;now=calls=binds=resets=0;scenario=4;
 hddInstallPadRpcBegin(50000);
 assert(!__wrap_sceSifBindRpc(&cd,123,0)&&!cd.server&&waiting!=&cd);
 scenario=0;assert(!__wrap_sceSifBindRpc(&cd,123,0)&&cd.server&&waiting!=&cd&&binds==2);
 assert(!__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)&&hddInstallPadRpcReady());
 thread=3;scenario=1;
 assert(!__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)&&now==0);
 thread=2;
 assert(__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)==-ETIMEDOUT&&now==50000);
 assert(!hddInstallPadRpcReady()&&!resets&&calls==3);
 assert(__wrap_sceSifCallRpc(&cd,1,0,NULL,0,&output,4,NULL,NULL)==-ETIMEDOUT&&calls==3);
 assert(__wrap_sceSifBindRpc(&cd,123,0)==-ETIMEDOUT&&binds==2);
 hddInstallPadRpcEnd();
 now=0;scenario=3;
 hddInstallPadRpcBegin(20000);
 assert(__wrap_sceSifBindRpc(&cd,123,0)==-ETIMEDOUT&&now==20000&&waiting!=&cd);
 assert(!hddInstallPadRpcReady()&&!resets);
 hddInstallPadRpcEnd();
 puts("PASS: recovery PAD bind and calls are bounded on the UI thread, with no late DMA to the caller's stack");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-recovery-') as directory:
    d = Path(directory)
    (d / 'sifrpc.h').write_text('#pragma once\n' + header)
    (d / 'dash_internal.h').write_text(dash_header)
    (d / 'test.c').write_text(code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
                    '-fdata-sections', '-I', str(d), '-I', str(root / 'include'),
                    str(root / 'src/hdd_install_recovery_ps2.c'), str(root / 'src/hdd_install_recovery.c'),
                    str(d / 'test.c'), '-Wl,--gc-sections', '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True, timeout=10)


def function(source, signature):
    a = source.index(signature)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[a:], re.S):
        depth += (token.group() == '{') - (token.group() == '}')
        if not depth and token.group() == '}': return source[a:a + token.end()] + '\n'
    raise AssertionError(signature)


# Exercise the physical reset/UI handshake too: no old-server cleanup before
# reset acknowledgment, and no client reconstruction until the worker unwinds.
init = (root / 'src/init.c').read_text()
ui = (root / 'src/dash_hdd_install.c').read_text()
transport = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install_recovery.h"
typedef uint64_t u64;
typedef struct { int pressed; } DashInput;
static int debug_open,worker_alive,done,suspended,stopped,forgot,reset_calls,sync_calls;
static int rebuilding,core_calls,pads,inputs,frames,pumps,fallback,loading,ds34;
static int fail_reset,fail_sync,fail_filexio,fail_pad,expect_terminal;
static int pccf_bus_owned,pccf_loaded;
static unsigned iop_reset_generation;
static u64 now;
static jmp_buf terminal;
static u64 Timer(void){return now+=1000;}
static void padSuspendForRecovery(void){assert(!rebuilding);suspended++;}
static void resetUsbHostControllerBeforeIopReset(void){assert(suspended&&!rebuilding);}
static void SifInitRpc(int n){assert(!n&&!rebuilding);}
#define SIF_REG_SMFLAG 1
#define SIF_REG_MSFLAG 2
#define SIF_SYSREG_SUBADDR 3
static int exited_rpc;
static void SifExitRpc(void){assert(!rebuilding);exited_rpc++;}
static unsigned SifGetReg(int reg){assert(reg>=1&&reg<=3);return 0x12345678;}
static int SifIopReset(const char *s,int n){assert(s&&!s[0]&&!n&&suspended&&!rebuilding&&exited_rpc);reset_calls++;return !fail_reset&&reset_calls>=2;}
static int SifIopSync(void){assert(reset_calls>=2&&!rebuilding);sync_calls++;if(!fail_sync&&sync_calls>=2){stopped=1;return 1;}return 0;}
static void padForgetAfterRecovery(void){assert(stopped&&!rebuilding);forgot++;}
static void clearPadPressState(void){assert(stopped);}
static void fileXioExit(void){assert(stopped&&!worker_alive&&forgot);rebuilding=1;core_calls++;}
static void SifLoadFileExit(void){assert(rebuilding);}
static void clearIopModuleState(void){assert(rebuilding&&!worker_alive);}
static int SifLoadFileInit(void){assert(rebuilding);return 0;}
static int SifInitIopHeap(void){assert(rebuilding);return 0;}
static void initsbv_patches(void){assert(rebuilding);}
static int ensureCoreIoStackReady(void){assert(rebuilding);return !fail_filexio;}
static void dashSoundShutdown(void){assert(rebuilding);}
#ifdef DS34
static void loadDs34InputModules(void){assert(rebuilding);ds34++;}
#endif
static int setupPadForRecovery(void){assert(rebuilding&&!worker_alive);pads++;return !fail_pad;}
static void startKbd(void){assert(pads);}
void hddInstallLog(const char *s,...){assert(s);}
void hddInstallProgress(const char *s,int a,int b){assert(s&&!a&&!b);}
static void dashLoadingFrame(const char *s,int a,int b){
    assert(!a&&!b);frames++;
    if(expect_terminal){assert(s&&worker_alive&&!hddInstallRecoveryWasReset()&&!forgot&&!core_calls);longjmp(terminal,1);}
}
static int dashLoadingActive(void){return loading;}
static void dashLoadingBegin(const char *s){assert(s);loading=1;}
static void dashLoadingEnd(void){loading=0;}
static int dashWorkerDone(int id){assert(id==1);return done;}
static void dashWorkerPump(void){
    pumps++;
    if(hddInstallRecoveryFault())assert(stopped&&forgot&&hddInstallRecoveryWasReset()&&!core_calls);
    if(hddInstallRecoveryFault()||pumps==2){worker_alive=0;done=1;}
}
static void dashInputPoll(DashInput *in){assert(in&&!hddInstallRecoveryFault());inputs++;}
static void debugInput(DashInput *in){assert(in);}
static int dashWorkerAwait(int id,const char *s,int cancelable){assert(id==1&&s&&!cancelable);fallback++;return 1;}
'''
transport += init[init.index('static const char *installer_recovery_error'):init.index('int resetInstallerIopTransport(')]
transport += function(init, 'int resetInstallerIopTransport(')
transport += function(init, 'int reloadInstallerCoreAfterReset(')
transport += function(ui, 'void dashHddInstallRecoveryFailed(')
transport += function(ui, 'int dashHddInstallAwait(')
transport += r'''
static void reset(void){
    hddInstallRecoveryEnd();exited_rpc=0;worker_alive=1;done=suspended=stopped=forgot=reset_calls=sync_calls=0;
    rebuilding=core_calls=pads=inputs=frames=pumps=fallback=loading=ds34=0;
    fail_reset=fail_sync=fail_filexio=fail_pad=expect_terminal=0;pccf_bus_owned=pccf_loaded=1;now=0;
}
int main(void){
    reset();assert(dashHddInstallAwait(1,"Fallback")&&fallback==1&&!frames);
    reset();hddInstallRecoveryBegin();assert(dashHddInstallAwait(1,"Normal"));
    assert(!worker_alive&&!core_calls&&inputs==1&&frames==1&&!loading);
    reset();hddInstallRecoveryBegin();hddInstallRecoveryRequestReset();
    unsigned generation=iop_reset_generation;
    assert(dashHddInstallAwait(1,"Recovery"));
    assert(!worker_alive&&!inputs&&!core_calls&&hddInstallRecoveryWasReset()&&forgot==1);
    assert(iop_reset_generation==generation+1&&!pccf_bus_owned&&!pccf_loaded);
    hddInstallRecoveryEnd();assert(reloadInstallerCoreAfterReset()&&core_calls==1&&pads==1);
#ifdef DS34
    assert(ds34==1);
#else
    assert(!ds34);
#endif
    for(int failure=0;failure<2;failure++){
        reset();fail_reset=!failure;fail_sync=failure;expect_terminal=1;
        hddInstallRecoveryBegin();hddInstallRecoveryRequestReset();
        if(!setjmp(terminal)){dashHddInstallAwait(1,"Failed reset");assert(0);}
        assert(strstr(installerIopRecoveryError(),failure?"boot completion":"could not be queued"));
        assert(now>=15000&&worker_alive&&!forgot&&!core_calls&&!hddInstallRecoveryWasReset());
    }
    for(int failure=0;failure<2;failure++){
        reset();worker_alive=0;
        assert(resetInstallerIopTransport());
        fail_filexio=!failure;fail_pad=failure;
        assert(!reloadInstallerCoreAfterReset()&&core_calls==1&&pads==failure);
        assert(strstr(installerIopRecoveryError(),failure?"Controller initialization":"IOP core services"));
    }
    puts("PASS: reset skips old-server cleanup, UI releases RPC only after IOP acknowledgment, rebuild follows worker unwind, reset failures retain buffers");
}
'''
with tempfile.TemporaryDirectory(prefix='hdd-installer-reset-') as directory:
    d = Path(directory)
    (d / 'test.c').write_text(transport)
    for ds34 in (False, True):
        subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                        *(['-DDS34'] if ds34 else []), '-I', str(root / 'include'), str(d / 'test.c'),
                        str(root / 'src/hdd_install_recovery.c'), '-o', str(d / 'test')], check=True)
        subprocess.run([str(d / 'test')], check=True, timeout=10)

    pad_code = r'''
#include <assert.h>
#include <stdint.h>
typedef uint64_t u64;
enum {PAD_STATE_DISCONN, PAD_STATE_STABLE, PAD_STATE_FINDCTP1, PAD_STATE_ERROR};
static u64 now;
static int stable_after;
static u64 Timer(void){return now;}
static void DelayThread(int us){assert(us==1000);now++;}
static int padGetState(int port,int slot){
    assert(port==1&&!slot);
    return stable_after>=0&&now>=(u64)stable_after?PAD_STATE_STABLE:PAD_STATE_ERROR;
}
'''
    for filename in ('pad.c', 'pad_ds34.c'):
        source = (root / 'src' / filename).read_text()
        body = pad_code
        if filename == 'pad.c':
            body += 'static int pad_reopen_active;\n'
        body += function(source, 'static int waitPadReadyUntil(')
        body += r'''
int main(void){
    stable_after=-1;assert(!waitPadReadyUntil(1,0,5)&&now==5);
    now=0;stable_after=3;assert(waitPadReadyUntil(1,0,5)&&now==3);
}
'''
        (d / 'pad.c').write_text(body)
        subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(d / 'pad.c'),
                        '-o', str(d / 'pad')], check=True)
        subprocess.run([str(d / 'pad')], check=True, timeout=10)

    bind_code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef uint64_t u64;
typedef struct {void *server;u64 ready_at;} SifRpcClientData_t;
enum { SIF_RPC_M_NOWAIT=1 };
static u64 now;
static unsigned calls,scenario;
u64 Timer(void){return now;}
static void DelayThread(int us){assert(us==1000);now++;}
static int SifBindRpc(SifRpcClientData_t *client,int server,int mode){
    assert(mode==SIF_RPC_M_NOWAIT&&server>=1);
    calls++;client->ready_at=now+2;return 0;
}
static int SifCheckStatRpc(SifRpcClientData_t *client){
    if(scenario==2||now<client->ready_at)return 1;
    if(scenario==1)client->server=client;
    return 0;
}
'''
    bind_source = (root / 'iop/ds34_bind.h').read_text()
    bind_code += function(bind_source, 'static inline int ds34BindUntil(')
    bind_code += r'''
int main(void){
    SifRpcClientData_t usb={0},bt={0};
    scenario=0;assert(!ds34BindUntil(&usb,1,5)&&now==5&&calls==2);
    now=calls=0;scenario=2;assert(!ds34BindUntil(&usb,1,5)&&now==5&&calls==1);
    now=calls=0;scenario=1;assert(ds34BindUntil(&usb,1,5)&&now==2&&calls==1);
    scenario=2;assert(!ds34BindUntil(&bt,2,5)&&now==5&&calls==2);
    assert(!ds34BindUntil(&usb,1,5)&&calls==2);
    puts("PASS: missing and stalled DS34 servers share the recovery deadline");
}
'''
    (d / 'bind.c').write_text('#include <stdio.h>\n' + bind_code)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(d / 'bind.c'),
                    '-o', str(d / 'bind')], check=True)
    subprocess.run([str(d / 'bind')], check=True, timeout=10)
