/* Cooperative FTP control. Static DMA storage survives a timed-out RPC. */
#ifdef ETH
#include "launchelf.h"
#include "init.h"
#include "ftp_session.h"
#include <sifrpc.h>
#include <delaythread.h>

static SifRpcClientData_t client __attribute__((aligned(64)));
static LsFtpStatus packet __attribute__((aligned(64)));
static unsigned int generation;
static int bound, pending, stopped;

static void checkGeneration(void)
{
    unsigned int now = getIopResetGeneration();
    if (now != generation) {
        memset(&client, 0, sizeof(client));
        bound = pending = stopped = 0;
        generation = now;
    }
}
int ftpSessionStopped(void)
{
    checkGeneration();
    return stopped;
}
static int exchange(int command, LsFtpStatus *out)
{
    checkGeneration();
    /* Never reuse DMA state after timeout while the old call can still complete. */
    if (pending) {
        if (SifCheckStatRpc(&client)) return 0;
        pending = 0;
    }
    if (!bound) {
        if (SifBindRpc(&client, LS_FTP_RPC_ID, SIF_RPC_M_NOWAIT) < 0) return 0;
        pending = 1;
        for (int i = 0; i < 1000 && SifCheckStatRpc(&client); i++) DelayThread(1000);
        if (SifCheckStatRpc(&client)) return 0;
        pending = 0;
        if (!client.server) return 0;
        bound = 1;
    }
    memset(&packet, 0, sizeof(packet));
    if (SifCallRpc(&client, command, SIF_RPC_M_NOWAIT, NULL, 0, &packet, sizeof(packet), NULL, NULL) < 0)
        return 0;
    pending = 1;
    for (int i = 0; i < 1000 && SifCheckStatRpc(&client); i++) DelayThread(1000);
    if (SifCheckStatRpc(&client)) return 0;
    pending = 0;
    if (packet.version != LS_FTP_PROTOCOL_VERSION) return 0;
    stopped = packet.state == LS_FTP_STOPPED;
    if (out) memcpy(out, &packet, sizeof(packet));
    return 1;
}
int ftpSessionQuery(LsFtpStatus *out)
{
    return exchange(LS_FTP_STATUS, out);
}
int ftpSessionShutdown(void)
{
    LsFtpStatus status;
    if (ftpSessionStopped()) return 1;
    if (!exchange(LS_FTP_STOP, &status)) return 0;
    for (int i = 0; i < 100 && status.state != LS_FTP_STOPPED; i++) {
        DelayThread(10000);
        if (!ftpSessionQuery(&status)) return 0;
    }
    return status.state == LS_FTP_STOPPED;
}
#endif
