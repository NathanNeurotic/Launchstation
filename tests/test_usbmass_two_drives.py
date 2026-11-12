"""Run the embedded USB mass driver (iop/usbmass_bd) against two fake USB drives.

1. Two threads send commands to two drives at once (a folder read on one drive while
   the other is still starting up). Each drive must receive only its own commands and
   every command must finish.
2. A drive that fails every command during start-up must not stall start-up forever,
   must yield between tries, and must not be handed to BDM without a size.

Host pthreads stand in for IOP threads. This does not emulate USB timing, real
drives, or the IOP scheduler. Optional argument: another driver source folder
(for example sdk-core's core/usbmass_bd/src) to run the same checks against it.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
src = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'iop/usbmass_bd/src'

headers = {
    'tamtypes.h': r'''
#pragma once
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef uintptr_t uiptr;
''',
    'sysclib.h': '#pragma once\n#include <string.h>\n#include "tamtypes.h"\n',
    'thbase.h': r'''
#pragma once
#define TH_C 0x02000000
typedef struct { unsigned int attr, option; void (*thread)(void *); int stacksize, priority; } iop_thread_t;
int CreateThread(iop_thread_t *t); int StartThread(int id, void *arg); int DeleteThread(int id);
int DelayThread(int usec);
''',
    'thsemap.h': r'''
#pragma once
typedef struct { unsigned int attr, option; int initial, max; } iop_sema_t;
int CreateSema(iop_sema_t *s); int WaitSema(int id); int SignalSema(int id); int DeleteSema(int id);
''',
    'usbd.h': r'''
#pragma once
#include "tamtypes.h"
#define USB_RC_OK 0
#define USB_RC_STALL 4
#define USB_RC_DATAUNDER 9
#define USB_DT_DEVICE 1
#define USB_DT_CONFIG 2
#define USB_DT_ENDPOINT 5
#define USB_CLASS_MASS_STORAGE 8
#define USB_ENDPOINT_XFER_BULK 2
#define USB_ENDPOINT_DIR_MASK 0x80
#define USB_DIR_OUT 0
#define USB_DIR_IN 0x80
typedef struct _UsbDriver { struct _UsbDriver *next, *prev; char *name;
  int (*probe)(int); int (*connect)(int); int (*disconnect)(int); } sceUsbdLddOps;
typedef struct { u8 bLength, bDescriptorType; u8 pad[15]; u8 bNumConfigurations; } UsbDeviceDescriptor;
typedef struct { u8 bLength, bDescriptorType; u16 wTotalLength; u8 bNumInterfaces, bConfigurationValue; } UsbConfigDescriptor;
typedef struct { u8 bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints,
  bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol; } UsbInterfaceDescriptor;
typedef struct { u8 bLength, bDescriptorType, bEndpointAddress, bmAttributes, wMaxPacketSizeLB, wMaxPacketSizeHB; } UsbEndpointDescriptor;
typedef void (*sceUsbdDoneCallback)(int result, int count, void *arg);
void *sceUsbdScanStaticDescriptor(int devId, void *data, u8 type);
int sceUsbdOpenPipe(int devId, UsbEndpointDescriptor *e); int sceUsbdOpenPipeAligned(int devId, UsbEndpointDescriptor *e);
int sceUsbdClosePipe(int id); int sceUsbdRegisterLdd(sceUsbdLddOps *d); int sceUsbdGetDeviceLocation(int devId, u8 *path);
int fakeBulk(int pipe, void *buf, int len, sceUsbdDoneCallback cb, void *arg);
int fakeControl(int pipe, sceUsbdDoneCallback cb, void *arg);
''',
    'usbd_macro.h': r'''
#pragma once
#define sceUsbdBulkTransfer(ep, p, l, cb, a) fakeBulk((ep), (p), (l), (cb), (a))
#define sceUsbdControlTransfer(ep, t, r, v, i, l, p, cb, a) fakeControl((ep), (cb), (a))
#define sceUsbdSetConfiguration(ep, c, cb, a) fakeControl((ep), (cb), (a))
#define sceUsbdSetInterface(ep, i, s, cb, a) fakeControl((ep), (cb), (a))
#define sceUsbdClearEndpointFeature(ep, f, e, cb, a) fakeControl((ep), (cb), (a))
''',
    'usbhdfsd-common.h': '#pragma once\n#define USBMASS_DEV_STAT_CONN 0x01\n#define USBMASS_DEV_STAT_CONF 0x02\n#define USBMASS_DEV_STAT_ERR 0x80\n',
    'bdm.h': r'''
#pragma once
#include "tamtypes.h"
struct block_device { void *priv; char *name; unsigned int devNr, parNr; unsigned char parId;
  unsigned int sectorSize; u64 sectorOffset, sectorCount;
  int (*read)(struct block_device *, u64, void *, u16); int (*write)(struct block_device *, u64, const void *, u16);
  void (*flush)(struct block_device *); int (*stop)(struct block_device *); char *path; };
void bdm_connect_bd(struct block_device *bd); void bdm_disconnect_bd(struct block_device *bd);
''',
}

harness = r'''
#define MINI_DRIVER 1
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "usb_mass.c"
#include "scsi.c"

/* --- IOP thread and semaphore stand-ins --- */
static sem_t semas[64]; static int nsemas;
int CreateSema(iop_sema_t *s) { sem_init(&semas[nsemas], 0, s->initial); return nsemas++; }
int WaitSema(int id) { sem_wait(&semas[id]); return 0; }
int SignalSema(int id) { sem_post(&semas[id]); return 0; }
int DeleteSema(int id) { (void)id; return 0; }
int CreateThread(iop_thread_t *t) { (void)t; return 1; }
int StartThread(int id, void *arg) { (void)id; (void)arg; return 0; }
int DeleteThread(int id) { (void)id; return 0; }
static int delays, delay_other;
int DelayThread(int usec) { if (usec != 10000) delay_other++; delays++; sched_yield(); return 0; }
void *sceUsbdScanStaticDescriptor(int d, void *p, u8 t) { (void)d; (void)p; (void)t; return NULL; }
int sceUsbdOpenPipe(int d, UsbEndpointDescriptor *e) { (void)d; (void)e; return 0; }
int sceUsbdOpenPipeAligned(int d, UsbEndpointDescriptor *e) { (void)d; (void)e; return 0; }
int sceUsbdClosePipe(int id) { (void)id; return 0; }
int sceUsbdRegisterLdd(sceUsbdLddOps *d) { (void)d; return 0; }
int sceUsbdGetDeviceLocation(int d, u8 *p) { (void)d; p[0] = 2; return 0; }
static int bdm_connects;
void bdm_connect_bd(struct block_device *bd) { (void)bd; bdm_connects++; }
void bdm_disconnect_bd(struct block_device *bd) { (void)bd; }

/* --- Two fake bulk-only drives. Pipes: drive d has OUT = 10+2d, IN = 11+2d. ---
 * Transfers complete on a separate "USB" thread after a short delay; the CBW bytes
 * are read when the transfer is carried out, as the USB host controller does. */
enum { WANT_CBW, SEND_DATA, SEND_CSW };
struct drive { int state; u32 tag, left; u8 op; int wrong; };
static struct drive drives[2];
static volatile int short_csw_once;  /* drive 0's next status read completes with 0 bytes */
static volatile int zlp_once;  /* drive 0 ends its next data phase with a zero-length packet */
struct xfer { int pipe; u8 *buf; int len; sceUsbdDoneCallback cb; void *arg; int control; };
static struct xfer queue[64]; static int qhead, qtail;
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;
static int push(struct xfer x) {
    pthread_mutex_lock(&qlock); queue[qtail++ % 64] = x; pthread_cond_signal(&qcond); pthread_mutex_unlock(&qlock);
    return USB_RC_OK;
}
int fakeBulk(int pipe, void *buf, int len, sceUsbdDoneCallback cb, void *arg) {
    struct xfer x = {pipe, buf, len, cb, arg, 0}; return push(x);
}
int fakeControl(int pipe, sceUsbdDoneCallback cb, void *arg) {
    struct xfer x = {pipe, NULL, 0, cb, arg, 1}; return push(x);
}
static void *usb_thread(void *unused) {
    (void)unused;
    for (;;) {
        struct xfer x; struct drive *d;
        pthread_mutex_lock(&qlock);
        while (qhead == qtail) pthread_cond_wait(&qcond, &qlock);
        x = queue[qhead++ % 64];
        pthread_mutex_unlock(&qlock);
        usleep(200);
        if (x.control) { x.cb(USB_RC_OK, 0, x.arg); continue; }
        d = &drives[(x.pipe - 10) / 2];
        if ((x.pipe & 1) == 0) {                 /* OUT: a command block */
            u8 *c = x.buf; u32 sig, len;
            memcpy(&sig, c, 4); memcpy(&d->tag, c + 4, 4); memcpy(&len, c + 8, 4);
            d->op = c[15];
            if (sig != CBW_TAG || d->state != WANT_CBW) d->wrong++;
            if (d->op != ((x.pipe - 10) / 2 ? INQUIRY : READ_10)) d->wrong++;
            d->left = len; d->state = len ? SEND_DATA : SEND_CSW;
            x.cb(USB_RC_OK, x.len, x.arg);
        } else if (d->state == SEND_DATA && zlp_once && d == &drives[0]) {
            zlp_once = 0; d->state = SEND_CSW;   /* no data, then a CSW that claims success */
            x.cb(USB_RC_OK, 0, x.arg);
        } else if (d->state == SEND_DATA) {      /* IN: data, marked with the drive */
            int n = x.len < (int)d->left ? x.len : (int)d->left;
            memset(x.buf, 0xA0 + (x.pipe - 10) / 2, n);
            d->left -= n; if (!d->left) d->state = SEND_CSW;
            x.cb(USB_RC_OK, n, x.arg);
        } else if (d->state == SEND_CSW && x.len == 13 && short_csw_once && d == &drives[0]) {
            short_csw_once = 0;                 /* an empty status read; the real CSW is still to come */
            x.cb(USB_RC_OK, 0, x.arg);
        } else if (d->state == SEND_CSW && x.len == 13) {
            u32 sig = CSW_TAG, zero = 0;
            memcpy(x.buf, &sig, 4); memcpy(x.buf + 4, &d->tag, 4); memcpy(x.buf + 8, &zero, 4); x.buf[12] = 0;
            d->state = WANT_CBW;
            x.cb(USB_RC_OK, 13, x.arg);
        } else {
            d->wrong++;   /* the drive has nothing to send: a real one NAKs forever */
        }
    }
    return NULL;
}

#define ROUNDS 200
static int failures[2];
static void *issue(void *p) {
    int d = (int)(intptr_t)p, i;
    static u8 bufs[2][512];
    unsigned char cdb[12] = {0};
    for (i = 0; i < ROUNDS; i++) {
        int j, len = d ? 36 : 512;
        cdb[0] = d ? INQUIRY : READ_10;
        if (usb_queue_cmd(&g_mass_device[d].scsi, cdb, sizeof(cdb), bufs[d], len, 0) != 0) failures[d]++;
        for (j = 0; j < len; j++) if (bufs[d][j] != 0xA0 + d) { failures[d]++; break; }
    }
    return NULL;
}

static int offline_cmd(struct scsi_interface *s, const unsigned char *c, unsigned int cl,
                       unsigned char *data, unsigned int dl, unsigned int wr) {
    (void)s; (void)cl; (void)data; (void)dl; (void)wr;
    return c[0] == INQUIRY ? 0 : -1;   /* answers who it is, then fails every command */
}
static int offline_lun(struct scsi_interface *s) { (void)s; return 0; }
/* A drive over 2 TiB that is unplugged when asked for its full size (READ CAPACITY 16). */
static int unplug_cmd(struct scsi_interface *s, const unsigned char *c, unsigned int cl,
                      unsigned char *data, unsigned int dl, unsigned int wr) {
    static const u8 rc10[8] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x02, 0x00};
    (void)cl; (void)wr;
    if (c[0] == READ_CAPACITY_10) { memcpy(data, rc10, dl < 8 ? dl : 8); return 0; }
    if (c[0] == SERVICE_ACTION_IN) { scsi_disconnect(s); return -1; }
    return 0;
}
static void *offline_startup(void *p) { scsi_connect((struct scsi_interface *)p); return NULL; }

int main(void) {
    pthread_t usb, t0, t1, t2; int d, bad = 0; struct timespec until;
    struct scsi_interface offline = {0};
    if (usb_mass_init() != 0 || scsi_init() != 0) return 2;
    for (d = 0; d < 2; d++) {
        g_mass_device[d].devId = d + 1; g_mass_device[d].bulkEpO = 10 + 2 * d; g_mass_device[d].bulkEpI = 11 + 2 * d;
        g_mass_device[d].controlEp = 20 + d; g_mass_device[d].status = USBMASS_DEV_STAT_CONN | USBMASS_DEV_STAT_CONF;
        { iop_sema_t s = {0, 0, 0, 1}; g_mass_device[d].ioSema = CreateSema(&s); }
    }
    pthread_create(&usb, NULL, usb_thread, NULL);
    pthread_create(&t0, NULL, issue, (void *)0);
    pthread_create(&t1, NULL, issue, (void *)1);
    clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 20;
    if (pthread_timedjoin_np(t0, NULL, &until) || pthread_timedjoin_np(t1, NULL, &until)) {
        printf("FAIL two drives: a command never finished (wrong commands: drive0 %d, drive1 %d)\n", drives[0].wrong, drives[1].wrong);
        bad = 1;
    } else if (failures[0] || failures[1] || drives[0].wrong || drives[1].wrong) {
        printf("FAIL two drives: failed commands %d/%d, commands sent to the wrong drive %d/%d\n", failures[0], failures[1], drives[0].wrong, drives[1].wrong);
        bad = 1;
    } else
        printf("ok two drives: %d commands each, no crossed commands\n", ROUNDS);

    {
        static u8 buf[512]; unsigned char cdb[12] = {READ_10}; int r1, r2;
        zlp_once = 1;
        r1 = usb_queue_cmd(&g_mass_device[0].scsi, cdb, sizeof(cdb), buf, sizeof(buf), 0);
        r2 = usb_queue_cmd(&g_mass_device[0].scsi, cdb, sizeof(cdb), buf, sizeof(buf), 0);
        if (r1 == 0 || r2 != 0 || drives[0].wrong) {
            printf("FAIL short read: cut-short read returned %d, next read %d, wrong %d\n", r1, r2, drives[0].wrong);
            bad = 1;
        } else
            printf("ok short read: a read cut short by a zero-length packet fails, the next one works\n");
    }

    {
        static u8 buf[512]; unsigned char cdb[12] = {READ_10}; int r1, r2;
        short_csw_once = 1;
        r1 = usb_queue_cmd(&g_mass_device[0].scsi, cdb, sizeof(cdb), buf, sizeof(buf), 0);
        r2 = usb_queue_cmd(&g_mass_device[0].scsi, cdb, sizeof(cdb), buf, sizeof(buf), 0);
        if (r1 != 0 || r2 != 0 || drives[0].wrong) {
            printf("FAIL short status: read with an empty status returned %d, next read %d, out-of-step commands %d\n", r1, r2, drives[0].wrong);
            bad = 1;
        } else
            printf("ok short status: an empty status read is read again, the drive stays in step\n");
    }

    offline.name = "usb"; offline.get_max_lun = offline_lun; offline.queue_cmd = offline_cmd;
    pthread_create(&t2, NULL, offline_startup, &offline);
    clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 20;
    if (pthread_timedjoin_np(t2, NULL, &until)) {
        printf("FAIL offline drive: start-up never gave up (%d delays)\n", delays);
        return 1;
    }
    if (bdm_connects != 0) { printf("FAIL offline drive: handed to BDM without a size\n"); return 1; }
    if (delays < 100 || delays > 1000 || delay_other) {
        printf("FAIL offline drive: expected 100..1000 waits of 10 ms, got %d (%d of another length)\n", delays, delay_other);
        return 1;
    }
    printf("ok offline drive: start-up gave up after %d waits, not handed to BDM\n", delays);

    {
        struct scsi_interface gone = {0}; int i;
        gone.name = "usb"; gone.get_max_lun = offline_lun; gone.queue_cmd = unplug_cmd;
        scsi_connect(&gone);
        for (i = 0; i < NUM_DEVICES; i++) if (g_scsi_bd[i].priv == &gone) break;
        if (bdm_connects != 0 || i < NUM_DEVICES) {
            printf("FAIL unplugged drive: handed to BDM after it was unplugged during start-up\n");
            return 1;
        }
        printf("ok unplugged drive: unplugged during start-up, not handed to BDM\n");
    }
    return bad;
}
'''

compiler = shutil.which('gcc') or shutil.which('clang')
if compiler is None:
    raise SystemExit('Host gcc or clang required')
with tempfile.TemporaryDirectory(prefix='usbmass-two-drives-') as tmp:
    inc = Path(tmp) / 'inc'
    inc.mkdir()
    for name, text in headers.items():
        (inc / name).write_text(text)
    c = Path(tmp) / 'harness.c'
    exe = Path(tmp) / 'harness'
    c.write_text(harness)
    subprocess.run([compiler, '-std=gnu2x', '-D_GNU_SOURCE', '-w', '-I', str(inc), '-I', str(src), '-I', str(src / 'include'),
                    str(c), '-o', str(exe), '-pthread'], check=True)
    result = subprocess.run([str(exe)], timeout=60, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        raise SystemExit(f'usbmass two-drive harness failed ({src})')
print('usbmass two-drive harness passed')
