#include "hdd_install_recovery.h"
#include "dash_internal.h"
#include <sifrpc.h>

extern int __real_sceSifCallRpc(SifRpcClientData_t *, int, int, void *, int,
                               void *, int, SifRpcEndFunc_t, void *);
extern int __real_sceSifBindRpc(SifRpcClientData_t *, int, int);

static void installerRpcComplete(void *arg)
{
    (void)arg;
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
