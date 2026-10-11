"""Execute production libmc drain with success, pending and failure responses.

A failed mcSync must never update root-card presence/type/free from an
uninitialized result. No console USB timeout is emulated.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
src=(root/"src/filer.c").read_text()

def function(signature):
    start=src.index(signature)
    pos=src.index("{",start)+1
    depth=1
    while depth:
        depth+=(src[pos]=="{")-(src[pos]=="}")
        pos+=1
    return src[start:pos]+"\n"

program=r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#define DPRINTF(...) ((void)0)
#define ROOT_MC_POLL_INTERVAL_MS 500
static unsigned long long now_ms=10000;
static unsigned long long Timer(void) { return now_ms; }
static int root_mc_poll_active,root_mc_poll_port,root_mc_poll_type,root_mc_poll_free;
static int root_mc_present[2]={1,0},root_mc_polled[2]={1,1};
static int root_mc_known_type[2]={2,0},root_mc_known_free[2]={800,0};
static unsigned long long root_mc_next_poll_time;
static int sync_return=1, sync_result, sync_calls, status_calls;
static int mcSync(int mode,void *user,int *result) {
    (void)user; assert(mode==0); sync_calls++;
    if(sync_return>0)*result=sync_result;
    return sync_return;
}
static void DashStorageProbeStatus(const char *text) {
    (void)text;status_calls++;
}
'''
program+=function("static int memoryCardResultIndicatesPresent(int ret)\n{")
program+=function("void filerRootMcPollIdle(void)\n{")
program+=r'''
int main(void) {
    root_mc_poll_active=1;root_mc_poll_port=0;
    sync_return=-1;
    filerRootMcPollIdle();
    assert(!root_mc_poll_active && sync_calls==1 && status_calls==1);
    assert(root_mc_present[0]==1 && root_mc_known_type[0]==2);
    assert(root_mc_known_free[0]==800 && root_mc_polled[0]==1);
    assert(root_mc_poll_port==0 && root_mc_next_poll_time==now_ms+500);
    filerRootMcPollIdle();assert(sync_calls==1);
    root_mc_poll_active=1;sync_return=0;
    filerRootMcPollIdle();
    assert(sync_calls==2 && root_mc_poll_port==0);
    assert(root_mc_present[0]==1 && root_mc_known_type[0]==2);
    root_mc_poll_active=1;sync_return=1;sync_result=42;
    filerRootMcPollIdle();
    assert(root_mc_present[0]==0 && root_mc_known_type[0]==0);
    assert(root_mc_known_free[0]==-1 && root_mc_poll_port==1);
    root_mc_poll_active=1;sync_result=-1;
    root_mc_poll_type=2;root_mc_poll_free=500;
    filerRootMcPollIdle();
    assert(root_mc_present[1]==1 && root_mc_known_type[1]==2);
    assert(root_mc_known_free[1]==500 && root_mc_poll_port==0);
    puts("PASS: libmc completion preserves cached card state on failures, retries after settling");
}
'''
compiler=shutil.which("gcc") or shutil.which("clang")
assert compiler,"Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-mc-drain-") as temp:
    path=Path(temp)/"check.c"
    binary=Path(temp)/"check"
    path.write_text(program)
    subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                    str(path),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=5)
