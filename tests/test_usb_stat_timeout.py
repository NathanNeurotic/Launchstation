"""Execute the production bounded USB stat RPC helper with controlled completion."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/init.c").read_text()
start = source.index("#define FXIO_STAT_BIND_MS")
end = source.index("void waitForUsbMassPath(const char *path)", start)

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned long long u64;
typedef struct { unsigned int mode, rest[15]; } iox_stat_t __attribute__((aligned(64)));
struct fxio_getstat_packet { char pathname[512]; iox_stat_t *stat; };
typedef struct { void *server; } SifRpcClientData_t;
#define FILEXIO_IRX 1
#define FILEXIO_GETSTAT 2
#define SIF_RPC_M_NOWAIT 1
#define UNCACHED_SEG(x) (x)

static int SifBindRpc(SifRpcClientData_t *client, unsigned int id, int mode);
static int SifCallRpc(SifRpcClientData_t *client, unsigned int number, int mode,
                      void *send, int ssize, void *recv, int rsize, void *cb, void *ud);
static int SifCheckStatRpc(SifRpcClientData_t *client);
static u64 Timer(void);
static void DelayThread(unsigned int us);
static int WaitSema(int id);
static int SignalSema(int id);
static unsigned int getIopResetGeneration(void);
static int sceSifWriteBackDCache(void *p, int n);
'''
code += source[start:end]
code += r'''
static unsigned int generation, now;
static int bind_delay, call_delay, bind_answer, call_answer;
static int rpc_active, rpc_kind, checks_left;
static int bind_calls, rpc_calls, wait_count, signal_count;
static char last_path[512];

static int SifBindRpc(SifRpcClientData_t *client, unsigned int id, int mode)
{
    assert(id == FILEXIO_IRX && mode == SIF_RPC_M_NOWAIT);
    bind_calls++;
    if (bind_answer < 0) {
        rpc_active = 0;
        return -1;
    }
    client->server = bind_answer ? (void *)1 : NULL;
    rpc_active = 1;
    rpc_kind = 1;
    checks_left = bind_delay;
    return 0;
}

static int SifCallRpc(SifRpcClientData_t *client, unsigned int number, int mode,
                      void *send, int ssize, void *recv, int rsize, void *cb, void *ud)
{
    assert(client->server != NULL && number == FILEXIO_GETSTAT && mode == SIF_RPC_M_NOWAIT);
    assert(send == &fxstat_pkt && ssize == (int)sizeof(fxstat_pkt));
    assert(recv == fxstat_ret && rsize == 4 && cb == NULL && ud == NULL);
    assert(fxstat_pkt.stat == &fxstat_stat);
    rpc_calls++;
    snprintf(last_path, sizeof(last_path), "%s", fxstat_pkt.pathname);
    rpc_active = 1;
    rpc_kind = 2;
    checks_left = call_delay;
    return 0;
}

static int SifCheckStatRpc(SifRpcClientData_t *client)
{
    (void)client;
    if (!rpc_active)
        return 0;
    if (checks_left > 1) {
        checks_left--;
        return 1;
    }
    rpc_active = 0;
    if (rpc_kind == 2) {
        fxstat_ret[0] = call_answer;
        fxstat_stat.mode = 0x12345678;
    }
    return 0;
}

static u64 Timer(void) { return now; }
static void DelayThread(unsigned int us) { now += us / 1000; }
static int WaitSema(int id) { (void)id; wait_count++; return 0; }
static int SignalSema(int id) { (void)id; signal_count++; return 0; }
static unsigned int getIopResetGeneration(void) { return generation; }
static int sceSifWriteBackDCache(void *p, int n) { (void)p; (void)n; return 0; }

static void reset_case(void)
{
    memset(&fxstat_cd, 0, sizeof(fxstat_cd));
    fxstat_generation = 0;
    fxstat_bound = fxstat_pending = 0;
    fxstat_lock = 1;
    generation = 1;
    now = 0;
    bind_delay = call_delay = 1;
    bind_answer = 1;
    call_answer = 0;
    rpc_active = rpc_kind = checks_left = 0;
    bind_calls = rpc_calls = wait_count = signal_count = 0;
    last_path[0] = '\0';
}

int main(void)
{
    iox_stat_t out;

    reset_case();
    call_delay = 3;
    out.mode = 0;
    assert(fileXioGetStatBounded("usb0:/", &out, 750) == 0);
    assert(out.mode == 0x12345678 && now == 20);
    assert(bind_calls == 1 && rpc_calls == 1 && !fxstat_pending);
    assert(!strcmp(last_path, "usb0:/") && wait_count == signal_count);

    reset_case();
    call_delay = 1000000;
    assert(fileXioGetStatBounded("usb0:/", &out, 750) == -1);
    assert(now == 750 && fxstat_pending && rpc_calls == 1);
    assert(fileXioGetStatBounded("usb1:/", &out, 750) == -2);
    assert(now == 750 && rpc_calls == 1 && wait_count == signal_count);

    reset_case();
    call_delay = 1000000;
    assert(fileXioGetStatBounded("usb0:/", &out, 750) == -1);
    checks_left = 1;
    rpc_active = 1;
    call_delay = 1;
    assert(fileXioGetStatBounded("usb1:/", &out, 750) == 0);
    assert(rpc_calls == 2 && !fxstat_pending && out.mode == 0x12345678);

    reset_case();
    call_delay = 1000000;
    assert(fileXioGetStatBounded("usb0:/", &out, 750) == -1);
    generation = 2;
    call_delay = 1;
    assert(fileXioGetStatBounded("usb1:/", &out, 750) == 0);
    assert(bind_calls == 2 && rpc_calls == 2 && !fxstat_pending);

    reset_case();
    bind_delay = 1000000;
    assert(fileXioGetStatBounded("usb0:/", &out, 750) == -1);
    assert(now == 1000 && rpc_calls == 0 && fxstat_pending);
    assert(fileXioGetStatBounded("usb1:/", &out, 750) == -2);
    assert(now == 1000 && rpc_calls == 0);
    generation = 2;
    bind_delay = call_delay = 1;
    assert(fileXioGetStatBounded("usb1:/", &out, 750) == 0);
    assert(bind_calls == 2 && rpc_calls == 1 && wait_count == signal_count);

    reset_case();
    fxstat_lock = -1;
    assert(fileXioGetStatBounded("usb0:/", &out, 750) == -1);
    assert(bind_calls == 0 && rpc_calls == 0 && wait_count == 0 && signal_count == 0);

    puts("PASS: bounded USB stat success, timeout, fail-fast, late healing, and IOP reset recovery");
    return 0;
}
'''

compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="usb-stat-timeout-") as tmp:
    source_path = Path(tmp) / "stat_timeout.c"
    binary_path = Path(tmp) / "stat_timeout"
    source_path.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(source_path), "-o", str(binary_path)], check=True)
    subprocess.run([str(binary_path)], check=True, timeout=5)
