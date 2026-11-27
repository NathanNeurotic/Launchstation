#include <kernel.h>
#include <sifrpc.h>
#include <string.h>
#include "libgipusb.h"

// RPC contract shared with iop/gipusb/iop/gipusb.c
#define GIPUSB_GET_STATUS 2
#define GIPUSB_GET_DATA   3
#define GIPUSB_SET_RUMBLE 4
#define GIPUSB_BIND_RPC_ID 0x2F6B9D41

#define GIPUSB_BIND_TRIES 1000

static SifRpcClientData_t gipusb;

static u8 rpcbuf[64] __attribute__((aligned(64)));

static u8 gipusb_inited = 0;

int gipusb_init(void)
{
    int i;

    gipusb.server = NULL;

    // Bounded retry: if the IRX is missing, the EE must not hang here.
    for (i = 0; i < GIPUSB_BIND_TRIES; i++) {
        if (SifBindRpc(&gipusb, GIPUSB_BIND_RPC_ID, 0) >= 0 && gipusb.server != NULL) {
            gipusb_inited = 1;
            return 1;
        }
        nopdelay();
    }

    return 0;
}

int gipusb_get_status(int port)
{
    if (!gipusb_inited)
        return 0;

    rpcbuf[0] = port;

    if (SifCallRpc(&gipusb, GIPUSB_GET_STATUS, 0, rpcbuf, 1, rpcbuf, 1, NULL, NULL) == 0)
        return rpcbuf[0];

    return 0;
}

int gipusb_get_data(int port, u8 *data)
{
    int ret;

    if (!gipusb_inited)
        return 0;

    rpcbuf[0] = port;

    ret = (SifCallRpc(&gipusb, GIPUSB_GET_DATA, 0, rpcbuf, 1, rpcbuf, 18, NULL, NULL) == 0);

    memcpy(data, rpcbuf, 18);

    return ret;
}

int gipusb_set_rumble(int port, u8 strong, u8 weak)
{
    if (!gipusb_inited)
        return 0;

    rpcbuf[0] = port;
    rpcbuf[1] = strong;
    rpcbuf[2] = weak;

    return (SifCallRpc(&gipusb, GIPUSB_SET_RUMBLE, 0, rpcbuf, 3, NULL, 0, NULL, NULL) == 0);
}
