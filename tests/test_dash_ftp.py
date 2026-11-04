"""Run the production FTP screen loop (DashFtpServer) under narrow C mocks.

Covers how the screen treats a server that is already STOPPED (a STOP that
timed out and finished later), one that stops mid-session, Stop and a Stop
that times out. It does not emulate the IOP module or the PS2 network stack.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    begin = source.index(signature)
    pos = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[begin:pos] + "\n"


c = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ftp_protocol.h"

#define DI_WARNING 4
#define DASH_IN_OK 1
#define DASH_IN_BACK 2
typedef struct { unsigned int pressed; } DashInput;

static int query_script[4], query_count, queries, query_ok;
static unsigned int presses[4];
static int press_index, resident, stopped, network_ready, shutdown_ok;
static int reboots, net_loads, modals, shutdowns, frames;

static void reset(void) {
    memset(query_script, 0, sizeof(query_script));
    memset(presses, 0, sizeof(presses));
    query_count = queries = press_index = resident = stopped = 0;
    reboots = net_loads = modals = shutdowns = frames = 0;
    query_ok = network_ready = shutdown_ok = 1;
}
static int dashWorkerAwaitAll(const char *status) { (void)status; return 1; }
static void filerRootMcPollIdle(void) {}
static void dashLoadingBegin(const char *status) { (void)status; }
static void dashLoadingEnd(void) {}
static void loadNetModules(void) { net_loads++; resident = 1; }
static int isFtpServerRunning(void) { return resident; }
static int isFtpNetworkReady(void) { return network_ready && resident && !stopped; }
static const char *dashboardIpNetworkError(void) { return "network"; }
static void dashModalMessage(int icon, const char *title, const char *body, const char *detail) {
    (void)icon; (void)title; (void)body; (void)detail;
    modals++;
}
static int ftpSessionQuery(LsFtpStatus *out) {
    if (!query_ok)
        return 0;
    memset(out, 0, sizeof(*out));
    out->version = LS_FTP_PROTOCOL_VERSION;
    out->state = queries < query_count ? query_script[queries] : LS_FTP_RUNNING;
    queries++;
    stopped = out->state == LS_FTP_STOPPED;
    return 1;
}
static int ftpSessionShutdown(void) {
    shutdowns++;
    if (shutdown_ok)
        stopped = 1;
    return shutdown_ok;
}
static void rebootIopAndReloadCoreStackSilent(void) { reboots++; resident = stopped = 0; }
static void ftpFrame(const LsFtpStatus *status, int stopping) { (void)status; (void)stopping; frames++; }
static void dashInputFlush(void) {}
static void dashInputPoll(DashInput *input) { input->pressed = presses[press_index++]; }
static int dashFtpPollRemote(void) { return 0; }
static void DelayThread(int usec) { (void)usec; }
"""
c += function("src/dash_ftp.c", "int DashFtpServer(void)\n{")
c += r"""
int main(void) {
    /* Fresh start: no stale module, Back keeps FTP running. */
    reset();
    presses[0] = DASH_IN_BACK;
    assert(DashFtpServer() == 1);
    assert(net_loads == 1 && queries == 1 && reboots == 0 && modals == 0 && resident);

    /* A STOP that timed out finished later: the stopped module is reset and FTP starts again. */
    reset();
    resident = 1;
    query_script[0] = LS_FTP_STOPPED;
    query_count = 1;
    presses[0] = DASH_IN_BACK;
    assert(DashFtpServer() == 1);
    assert(reboots == 1 && net_loads == 1 && frames == 1 && modals == 0 && resident && !stopped);

    /* The server stops mid-session: the stopped module does not stay resident. */
    reset();
    query_script[0] = LS_FTP_RUNNING;
    query_script[1] = LS_FTP_STOPPED;
    query_count = 2;
    assert(DashFtpServer() == 1);
    assert(reboots == 1 && frames == 1 && modals == 0 && !resident);

    /* Stop closes files first, then resets. */
    reset();
    presses[0] = DASH_IN_OK;
    assert(DashFtpServer() == 1);
    assert(shutdowns == 1 && reboots == 1 && modals == 0);

    /* A Stop that times out never resets the IOP over open files. */
    reset();
    shutdown_ok = 0;
    presses[0] = DASH_IN_OK;
    assert(DashFtpServer() == 0);
    assert(shutdowns == 1 && reboots == 0 && modals == 1 && resident);

    /* An unanswered control RPC is reported, not reset over. */
    reset();
    resident = 1;
    query_ok = 0;
    assert(DashFtpServer() == 0);
    assert(reboots == 0 && modals == 1 && resident);

    puts("PASS: FTP screen resets a stopped module, keeps a running one and never resets over an unanswered stop");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"

with tempfile.TemporaryDirectory(prefix="dash-ftp-") as tmp:
    path = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    path.write_text(c, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
                    str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
