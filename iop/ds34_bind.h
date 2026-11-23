#ifndef DS34_BIND_H
#define DS34_BIND_H

#include <delaythread.h>
#include <kernel.h>
#include <sifrpc.h>

extern u64 Timer(void);

static inline int ds34BindUntil(SifRpcClientData_t *client, int server, u64 deadline)
{
    client->server = NULL;
    while (!deadline || Timer() < deadline) {
        if (SifBindRpc(client, server, SIF_RPC_M_NOWAIT) < 0)
            return 0;
        while (SifCheckStatRpc(client)) {
            if (deadline && Timer() >= deadline)
                return 0;
            DelayThread(1000);
        }
        if (deadline && Timer() >= deadline)
            return 0;
        if (client->server)
            return 1;
        DelayThread(1000);
    }
    return 0;
}

#endif
