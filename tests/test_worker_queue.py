"""Host tests for the production queue, with deterministic EE kernel stubs.

This validates admission, ownership and FIFO completion, not EE scheduling.
Run: python tests/test_worker_queue.py (requires a host C compiler).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_worker.c").read_text()
queue = source[source.index("#define WORKER_STACK_SIZE"):source.index("// Waiting for results")]
stub = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef uint64_t u64;
typedef void (*DashJobFn)(void *);
typedef struct {int init_count, max_count;} ee_sema_t;
typedef struct {void *func, *stack, *gp_reg; int stack_size, initial_priority;} ee_thread_t;
typedef struct {int current_priority;} ee_thread_status_t;
void *_gp;
void (*iopResetNotify)(int);
int (*iopResetBlocked)(void);
int dashWorkerOnThread(void);
static int drained, signaled;
static int CreateSema(void *p) {(void)p; return 1;}
static int CreateThread(void *p) {(void)p; return 2;}
static int StartThread(int id, void *p) {(void)id; (void)p; return 0;}
static void DeleteThread(int id) {(void)id;}
static int GetThreadId(void) {return 1;}
static int ReferThreadStatus(int id, ee_thread_status_t *p) {(void)id; p->current_priority=64; return 0;}
static void ChangeThreadPriority(int id,int p) {(void)id; (void)p;}
static void WaitSema(int id) {(void)id;}
static void SignalSema(int id) {(void)id; signaled++;}
static void filerRootMcPollIdle(void) {drained++;}
static void dashStrCopy(char *d,const char *s,int n) {snprintf(d,n,"%s",s);}
void dashWorkerPump(void);
'''
checks = r'''
static int ran, finished, expected = 1;
static void run(void *arg) {assert(*(int *)arg == expected); ran++;}
static void done(void *arg) {assert(*(int *)arg == expected++); finished++;}
int main(void) {
    int args[JOB_QUEUE + 2], ids[JOB_QUEUE + 1];
    for (int i=0; i<JOB_QUEUE+2; i++) args[i]=i+1;
    assert(dashWorkerSubmit("unavailable", 1, run, done, &args[0]) == -1);
    assert(ran == 0 && finished == 0 && next_id == 1);
    dashWorkerInit();
    for (int i=0; i<JOB_QUEUE+1; i++) {
        ids[i]=dashWorkerSubmit("queued", 1, run, done, &args[i]);
        assert(ids[i] == i+1);
    }
    assert(q_count == JOB_QUEUE && cur_state == CUR_RUNNING);
    assert(drained == 0); /* submitting must not wait for libmc */
    assert(dashWorkerSubmit("full", 1, run, done, &args[JOB_QUEUE+1]) == -1);
    assert(ran == 0 && finished == 0 && next_id == JOB_QUEUE+2);
    assert(!dashWorkerDone(-1) && !dashWorkerDone(0));
    for (int i=0; i<JOB_QUEUE+1; i++) {
        assert(!dashWorkerDone(ids[i]));
        cur.run(cur.arg); /* deterministic completion of one kernel job */
        cur_state=CUR_FINISHED;
        dashWorkerPump();
        assert(dashWorkerDone(ids[i]));
    }
    assert(!dashWorkerBusy() && ran == JOB_QUEUE+1 && finished == ran);
    puts("PASS: unavailable/full rejection, caller ownership, no UI libmc wait, FIFO IDs and callbacks");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-worker-") as tmp:
    c = Path(tmp) / "queue.c"
    exe = Path(tmp) / "queue-test.exe"
    c.write_text(stub + queue + checks)
    subprocess.run([compiler, "-std=c11", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
