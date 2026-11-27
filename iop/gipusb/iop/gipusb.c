#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"
#include "gipusb.h"

IRX_ID("gipusb", 1, 0);

//#define DPRINTF(x...) printf(x)
#define DPRINTF(x...)

// RPC contract shared with iop/gipusb/ee/libgipusb.c
#define GIPUSB_GET_STATUS 2
#define GIPUSB_GET_DATA   3
#define GIPUSB_SET_RUMBLE 4
#define GIPUSB_BIND_RPC_ID 0x2F6B9D41

static const u8 power_on_packet[] = {GIP_CMD_POWER, GIP_OPT_INTERNAL, GIP_SEQ0, 0x01, GIP_PWR_ON};

static u8 in_buf[GIP_BUF_SIZE] __attribute__((aligned(64)));
static u8 out_buf[GIP_BUF_SIZE] __attribute__((aligned(64)));

// One interrupt-in read stays armed at all times. Each report is parsed in the
// callback and the next read is queued at once, so the RPC never waits on USB.
static volatile int io_armed = 0;

static gipusb_device gip;

static int usb_probe(int devId);
static int usb_connect(int devId);
static int usb_disconnect(int devId);
static void usb_config_set(int result, int count, void *arg);

UsbDriver usb_driver = {NULL, NULL, "gipusb", usb_probe, usb_connect, usb_disconnect};

static int gip_is_interface(const UsbInterfaceDescriptor *interface)
{
    return interface->bInterfaceClass == GIP_INTF_CLASS &&
           interface->bInterfaceSubClass == GIP_INTF_SUBCLASS &&
           interface->bInterfaceProtocol == GIP_INTF_PROTOCOL;
}

static int usb_probe(int devId)
{
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    UsbInterfaceDescriptor *interface;

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL)
        return 0;

    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (config == NULL)
        return 0;

    // Interface 0 carries the game input protocol. Interface 1 is the audio interface.
    interface = (UsbInterfaceDescriptor *)((char *)config + config->bLength);
    return gip_is_interface(interface);
}

static void usb_release(void)
{
    if (gip.inEndp >= 0)
        UsbCloseEndpoint(gip.inEndp);

    if (gip.outEndp >= 0)
        UsbCloseEndpoint(gip.outEndp);

    io_armed = 0;
    gip.controlEndp = -1;
    gip.inEndp = -1;
    gip.outEndp = -1;
    gip.devId = -1;
    gip.status = GIPUSB_STATE_DISCONNECTED;
}

static int usb_connect(int devId)
{
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    UsbInterfaceDescriptor *interface;
    UsbEndpointDescriptor *endpoint;
    int i;

    WaitSema(gip.lock);

    // Only one pad is supported.
    if (gip.devId != -1) {
        SignalSema(gip.lock);
        return 1;
    }

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (device == NULL || config == NULL) {
        SignalSema(gip.lock);
        return 1;
    }

    interface = (UsbInterfaceDescriptor *)((char *)config + config->bLength);
    if (!gip_is_interface(interface)) {
        SignalSema(gip.lock);
        return 1;
    }

    gip.devId = devId;
    gip.status = GIPUSB_STATE_CONNECTED;
    gip.controlEndp = UsbOpenEndpoint(devId, NULL);

    endpoint = (UsbEndpointDescriptor *)((char *)interface + interface->bLength);
    for (i = 0; i < interface->bNumEndpoints; i++) {
        if ((endpoint->bmAttributes & 0x03) == USB_ENDPOINT_XFER_INT) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && gip.inEndp < 0)
                gip.inEndp = UsbOpenEndpointAligned(devId, endpoint);
            else if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && gip.outEndp < 0)
                gip.outEndp = UsbOpenEndpointAligned(devId, endpoint);
        }
        endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength);
    }

    if (gip.inEndp < 0 || gip.outEndp < 0) {
        DPRINTF("GIPUSB: missing interrupt endpoints\n");
        usb_release();
        SignalSema(gip.lock);
        return 1;
    }

    UsbSetDeviceConfiguration(gip.controlEndp, config->bConfigurationValue, usb_config_set, NULL);

    SignalSema(gip.lock);
    return 0;
}

static int usb_disconnect(int devId)
{
    WaitSema(gip.lock);

    if (gip.devId == devId)
        usb_release();

    SignalSema(gip.lock);
    return 0;
}

static void out_cb(int resultCode, int bytes, void *arg)
{
}

// Rumble: GIP_CMD_RUMBLE 0x09 with 9 payload bytes, as xpad.c sends it:
//   payload[0] unused, [1] motors (0x0F = all), [2] left trigger, [3] right trigger,
//   [4] strong motor, [5] weak motor, [6] duration (0xFF = until changed), [7] delay, [8] repeat.
static u8 gip_rumble_buf[16] __attribute__((aligned(64)));
static u8 gip_rumble_last[2];

static void gip_set_rumble(u8 strong, u8 weak)
{
    if (!(gip.status & GIPUSB_STATE_RUNNING) || gip.outEndp < 0)
        return;

    // Send only on change: the EE polls every frame and the motors do not need repeats.
    if (gip_rumble_last[0] == strong && gip_rumble_last[1] == weak)
        return;
    gip_rumble_last[0] = strong;
    gip_rumble_last[1] = weak;

    for (int i = 0; i < 16; i++)
        gip_rumble_buf[i] = 0;
    gip_rumble_buf[0] = GIP_CMD_RUMBLE;
    gip_rumble_buf[1] = 0x00;
    gip_rumble_buf[2] = GIP_SEQ0;
    gip_rumble_buf[3] = 0x09;
    gip_rumble_buf[5] = GIP_MOTOR_ALL;
    gip_rumble_buf[8] = strong;
    gip_rumble_buf[9] = weak;
    gip_rumble_buf[10] = 0xFF;

    UsbInterruptTransfer(gip.outEndp, gip_rumble_buf, 13, out_cb, NULL);
}

// Converts an input report (cmd 0x20) into the PS2 padButtonStatus layout.
static void gip_parse(const u8 *d)
{
    const u8 b4 = d[4];
    const u8 b5 = d[5];
    const u16 lt = (u16)(d[6] | (d[7] << 8));
    const u16 rt = (u16)(d[8] | (d[9] << 8));
    const s16 lx = (s16)(d[10] | (d[11] << 8));
    const s16 ly = (s16)(d[12] | (d[13] << 8));
    const s16 rx = (s16)(d[14] | (d[15] << 8));
    const s16 ry = (s16)(d[16] | (d[17] << 8));

    // Xbox buttons: Menu=Start, View=Select, A=Cross, B=Circle, X=Square, Y=Triangle.
    const int start = (b4 >> 2) & 1;
    const int select = (b4 >> 3) & 1;
    const int cross = (b4 >> 4) & 1;
    const int circle = (b4 >> 5) & 1;
    const int square = (b4 >> 6) & 1;
    const int triangle = (b4 >> 7) & 1;

    const int up = b5 & 1;
    const int down = (b5 >> 1) & 1;
    const int left = (b5 >> 2) & 1;
    const int right = (b5 >> 3) & 1;
    const int l1 = (b5 >> 4) & 1;
    const int r1 = (b5 >> 5) & 1;
    const int l3 = (b5 >> 6) & 1;
    const int r3 = (b5 >> 7) & 1;

    const int l2 = (lt >> 2) >= GIP_TRIGGER_DIGITAL;
    const int r2 = (rt >> 2) >= GIP_TRIGGER_DIGITAL;

    // Same bit layout as the DS4 mapping in ds34usb: inverted, active-low bytes.
    gip.data[0] = (u8)~(select | (l3 << 1) | (r3 << 2) | (start << 3) | (up << 4) | (right << 5) | (down << 6) | (left << 7));
    gip.data[1] = (u8)~(l2 | (r2 << 1) | (l1 << 2) | (r1 << 3) | (triangle << 4) | (circle << 5) | (cross << 6) | (square << 7));

    // PS2 sticks: 0x00 = left/up, 0xFF = right/down. Y is inverted, as in xpad.c.
    gip.data[2] = (u8)(128 + (rx >> 8));
    gip.data[3] = (u8)(127 - (ry >> 8));
    gip.data[4] = (u8)(128 + (lx >> 8));
    gip.data[5] = (u8)(127 - (ly >> 8));

    // Analog pressures, in the order the EE expects.
    gip.data[6] = right ? 0xFF : 0x00;
    gip.data[7] = left ? 0xFF : 0x00;
    gip.data[8] = up ? 0xFF : 0x00;
    gip.data[9] = down ? 0xFF : 0x00;
    gip.data[10] = triangle ? 0xFF : 0x00;
    gip.data[11] = circle ? 0xFF : 0x00;
    gip.data[12] = cross ? 0xFF : 0x00;
    gip.data[13] = square ? 0xFF : 0x00;
    gip.data[14] = l1 ? 0xFF : 0x00;
    gip.data[15] = r1 ? 0xFF : 0x00;
    gip.data[16] = (u8)(lt >> 2);
    gip.data[17] = (u8)(rt >> 2);
}

static void arm_in(void);

static void io_cb(int resultCode, int bytes, void *arg)
{
    io_armed = 0;

    // Other packets (announce, virtual key, firmware) carry no pad state and are skipped.
    if (resultCode == USB_RC_OK && bytes >= 4 + GIP_INPUT_MIN_LEN && in_buf[0] == GIP_CMD_INPUT)
        gip_parse(in_buf);

    arm_in();
}

static void arm_in(void)
{
    if (io_armed || gip.inEndp < 0 || !(gip.status & GIPUSB_STATE_RUNNING))
        return;

    io_armed = 1;
    if (UsbInterruptTransfer(gip.inEndp, in_buf, GIP_BUF_SIZE, io_cb, NULL) != USB_RC_OK)
        io_armed = 0;
}

// Xbox One pads stay silent until they receive the power-on packet.
static void usb_config_set(int result, int count, void *arg)
{
    int i;

    gip.status |= GIPUSB_STATE_CONFIGURED;

    for (i = 0; i < sizeof(power_on_packet); i++)
        out_buf[i] = power_on_packet[i];

    UsbInterruptTransfer(gip.outEndp, out_buf, sizeof(power_on_packet), out_cb, NULL);

    gip.status |= GIPUSB_STATE_RUNNING;
    arm_in();
}

static void gipusb_read(u8 *dst)
{
    int i;

    WaitSema(gip.lock);

    // Re-arms the read if a previous transfer failed.
    arm_in();

    for (i = 0; i < GIPUSB_PAD_SIZE; i++)
        dst[i] = gip.data[i];

    SignalSema(gip.lock);
}

// Copies the current pad state (18 bytes, padButtonStatus order) for freepad.
// Returns 1 when a pad is connected and running. Called from interrupt context,
// so it does not take gip.lock.
int gip_get_pad(u8 *dst)
{
    int i;

    if (!(gip.status & GIPUSB_STATE_RUNNING))
        return 0;

    for (i = 0; i < GIPUSB_PAD_SIZE; i++)
        dst[i] = gip.data[i];

    return 1;
}

int gip_status(void)
{
    return gip.status;
}

static void rpc_thread(void *data);
static void *rpc_sf(int cmd, void *data, int size);

static SifRpcDataQueue_t rpc_que __attribute__((aligned(16)));
static SifRpcServerData_t rpc_svr __attribute__((aligned(16)));

static int rpc_buf[64] __attribute((aligned(16)));

void rpc_thread(void *data)
{
    SifInitRpc(0);
    SifSetRpcQueue(&rpc_que, GetThreadId());
    SifRegisterRpc(&rpc_svr, GIPUSB_BIND_RPC_ID, rpc_sf, rpc_buf, NULL, NULL, &rpc_que);
    SifRpcLoop(&rpc_que);
}

void *rpc_sf(int cmd, void *data, int size)
{
    switch (cmd) {
        case GIPUSB_GET_STATUS:
            *(u8 *)data = gip.status;
            break;
        case GIPUSB_GET_DATA:
            gipusb_read((u8 *)data);
            break;
        case GIPUSB_SET_RUMBLE:
            gip_set_rumble(*((u8 *)data + 1), *((u8 *)data + 2));
            break;
        default:
            break;
    }

    return data;
}

int _start(int argc, char *argv[])
{
    int i;

    gip.devId = -1;
    gip.controlEndp = -1;
    gip.inEndp = -1;
    gip.outEndp = -1;
    gip.status = GIPUSB_STATE_DISCONNECTED;

    // Neutral state until the first report arrives.
    gip.data[0] = 0xFF;
    gip.data[1] = 0xFF;
    for (i = 2; i < 6; i++)
        gip.data[i] = 0x80;
    for (i = 6; i < GIPUSB_PAD_SIZE; i++)
        gip.data[i] = 0x00;

    gip.lock = CreateMutex(IOP_MUTEX_UNLOCKED);
    if (gip.lock < 0)
        return MODULE_NO_RESIDENT_END;

    if (UsbRegisterDriver(&usb_driver) != USB_RC_OK) {
        DPRINTF("GIPUSB: error registering USB driver\n");
        return MODULE_NO_RESIDENT_END;
    }

    iop_thread_t rpc_th;

    rpc_th.attr = TH_C;
    rpc_th.thread = rpc_thread;
    rpc_th.priority = 40;
    rpc_th.stacksize = 0x800;
    rpc_th.option = 0;

    int thid = CreateThread(&rpc_th);

    if (thid > 0) {
        StartThread(thid, NULL);
        return MODULE_RESIDENT_END;
    }

    return MODULE_NO_RESIDENT_END;
}
