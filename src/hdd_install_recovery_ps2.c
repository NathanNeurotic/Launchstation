#include "hdd_install_recovery.h"
#include "dash_internal.h"
#include <errno.h>
#include <sifrpc.h>

extern int __real_sceSifCallRpc(SifRpcClientData_t *, int, int, void *, int,
                               void *, int, SifRpcEndFunc_t, void *);
extern int __real_sceSifBindRpc(SifRpcClientData_t *, int, int);

static int pad_rpc_thread = -1, pad_rpc_failed;
static uint64_t pad_rpc_deadline;
/* libpad's first bind clients live on its stack. Keep a SIF completion
 * destination alive even if a bind times out and padInit returns. */
static SifRpcClientData_t pad_bind __attribute__((aligned(64)));

void hddInstallPadRpcBegin(uint64_t deadline)
{
    pad_rpc_deadline = deadline;
    pad_rpc_failed = 0;
    pad_rpc_thread = GetThreadId();
}

int hddInstallPadRpcReady(void)
{
    return !pad_rpc_failed && Timer() < pad_rpc_deadline;
}

void hddInstallPadRpcEnd(void)
{
    pad_rpc_thread = -1;
}

static void installerRpcComplete(void *arg)
{
    (void)arg;
}

static int waitForPadRpc(SifRpcClientData_t *client, int result)
{
    if (result >= 0) {
        while (hddInstallPadRpcReady() && SifCheckStatRpc(client))
            DelayThread(1000);
        if (hddInstallPadRpcReady()) return result;
        result = -ETIMEDOUT;
    }
    /* Recovery ends at the restart screen. No further libpad call may reuse
     * its static RPC buffer while a timed-out reply can still arrive. */
    pad_rpc_failed = 1;
    return result;
}

static int waitForInstallerRpc(SifRpcClientData_t *client, u64 started)
{
    for (;;) {
        if (Timer() - started >= HDD_INSTALL_RPC_TIMEOUT_MS) {
            hddInstallRecoveryRequestReset();
            /* The worker owns the DMA buffers until the UI sees a new IOP boot. */
            while (!hddInstallRecoveryWasReset()) DelayThread(1000);
            return HDD_INSTALL_ATA_TIMEOUT;
        }
        if (!SifCheckStatRpc(client)) return 0;
        DelayThread(1000);
    }
}

int __wrap_sceSifCallRpc(SifRpcClientData_t *client, int number, int mode,
                        void *send, int send_size, void *receive, int receive_size,
                        SifRpcEndFunc_t complete, void *arg)
{
    if (pad_rpc_thread >= 0 && GetThreadId() == pad_rpc_thread && !(mode & SIF_RPC_M_NOWAIT)) {
        if (!hddInstallPadRpcReady()) return -ETIMEDOUT;
        int ret = __real_sceSifCallRpc(client, number, mode | SIF_RPC_M_NOWAIT,
                                      send, send_size, receive, receive_size,
                                      complete ? complete : installerRpcComplete, arg);
        return waitForPadRpc(client, ret);
    }
    if (!hddInstallRecoveryActive() || !dashWorkerOnThread())
        return __real_sceSifCallRpc(client, number, mode, send, send_size,
                                   receive, receive_size, complete, arg);
    if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    if (mode & SIF_RPC_M_NOWAIT)
        return __real_sceSifCallRpc(client, number, mode, send, send_size,
                                   receive, receive_size, complete, arg);
    u64 started = Timer();
    int ret = __real_sceSifCallRpc(client, number, mode | SIF_RPC_M_NOWAIT,
                                  send, send_size, receive, receive_size,
                                  complete ? complete : installerRpcComplete, arg);
    return ret < 0 ? ret : waitForInstallerRpc(client, started);
}

int __wrap_sceSifBindRpc(SifRpcClientData_t *client, int server, int mode)
{
    if (pad_rpc_thread >= 0 && GetThreadId() == pad_rpc_thread && !(mode & SIF_RPC_M_NOWAIT)) {
        if (!hddInstallPadRpcReady()) return -ETIMEDOUT;
        int ret = __real_sceSifBindRpc(&pad_bind, server, mode | SIF_RPC_M_NOWAIT);
        ret = waitForPadRpc(&pad_bind, ret);
        /* A completed bind with no server lets libpad try the alternate PADMAN ID. */
        if (ret >= 0) *client = pad_bind;
        return ret;
    }
    if (!hddInstallRecoveryActive() || !dashWorkerOnThread())
        return __real_sceSifBindRpc(client, server, mode);
    if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    if (mode & SIF_RPC_M_NOWAIT)
        return __real_sceSifBindRpc(client, server, mode);
    u64 started = Timer();
    for (;;) {
        int ret = __real_sceSifBindRpc(client, server, mode | SIF_RPC_M_NOWAIT);
        if (ret < 0) return ret;
        ret = waitForInstallerRpc(client, started);
        if (ret < 0 || client->server) return ret;
        DelayThread(1000);
    }
}
