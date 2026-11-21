/* Temporary disk-format service, separate from the HDD and ATA drivers.
 * The EE resets the IOP after any successful BEGIN, including failed formats.
 * ATA volumes deliberately stay detached until that reset. */
#include <types.h>
#include <irx.h>
#include <loadcore.h>
#include <thbase.h>
#include <sifrpc.h>
#include <atad.h>
#include <sysclib.h>
#include <errno.h>
#ifdef EXFAT
#include <bdm.h>
#endif
#include "hdd_format_rpc.h"

IRX_ID("LaunchstationFormat", 1, 1);

static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static HddFormatPacket packet __attribute__((aligned(64)));
static unsigned int capacity;
static int active;

static void *command(int cmd, void *data, int size)
{
    HddFormatPacket *p = data;
    unsigned int lba = p->lba, count = p->count, n;
    int ret = -EINVAL;
    if (size < 64) { p->result = -EINVAL; return p; }
    if (cmd == HF_BEGIN) {
        ata_devinfo_t *info;
        if (active) { p->result = -EBUSY; return p; }
        info = ata_get_devinfo(0);
        if (!info || !info->exists || info->has_packet || !info->total_sectors || info->total_sectors == 0xffffffffu) {
            p->result = -ENODEV; return p;
        }
        capacity = info->total_sectors;
        /* Capacity agreed before confirmation must still match at BEGIN. */
        if (p->sectors != capacity) { p->result = -EIO; return p; }
        ret = ata_device_flush_cache(0);
        if (ret) { p->result = -EIO; return p; }
#ifdef EXFAT
        {
            struct block_device *devices[32];
            unsigned int i;
            int detached = 0;
            memset(devices, 0, sizeof(devices));
            bdm_get_bd(devices, 32);
            for (i = 0; i < 32; i++) {
                struct block_device *bd = devices[i];
                if (bd && bd->name && !strcmp(bd->name, "ata") && bd->devNr == 0 && bd->parNr == 0 &&
                    bd->sectorSize == 512 && bd->sectorOffset == 0 && bd->sectorCount == capacity) {
                    bdm_disconnect_bd(bd);
                    detached = 1;
                    break;
                }
            }
            if (!detached) { p->result = -EBUSY; return p; }
        }
#endif
        active = 1;
        p->result = 0;
        return p;
    }
    if (!active) { p->result = -EPERM; return p; }
    if (cmd == HF_FLUSH || cmd == HF_FINISH) {
        ret = ata_device_flush_cache(0) ? -EIO : 0;
        if (cmd == HF_FINISH) active = 0;
    } else if (count && lba < capacity && count <= capacity - lba) {
        if ((cmd == HF_READ || cmd == HF_WRITE) && count <= HDD_FORMAT_CHUNK &&
            (cmd != HF_WRITE || size >= (int)(64 + count * 512))) {
            ret = ata_device_sector_io(0, p->data, lba, count, cmd == HF_READ ? ATA_DIR_READ : ATA_DIR_WRITE) ? -EIO : 0;
        } else if (cmd == HF_ZERO && count <= 2048) {
            memset(p->data, 0, sizeof(p->data));
            ret = 0;
            while (count) {
                n = count > HDD_FORMAT_CHUNK ? HDD_FORMAT_CHUNK : count;
                if (ata_device_sector_io(0, p->data, lba, n, ATA_DIR_WRITE)) { ret = -EIO; break; }
                lba += n; count -= n;
            }
        }
    }
    p->result = ret;
    return p;
}

static void serve(void *unused)
{
    (void)unused;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&queue, GetThreadId());
    sceSifRegisterRpc(&server, HDD_FORMAT_RPC, command, &packet, NULL, NULL, &queue);
    sceSifRpcLoop(&queue);
}

int _start(int argc, char **argv)
{
    iop_thread_t thread;
    int id;
    (void)argc; (void)argv;
    memset(&thread, 0, sizeof(thread));
    thread.attr = TH_C; thread.thread = serve; thread.priority = 40; thread.stacksize = 0x1000;
    id = CreateThread(&thread);
    if (id < 0) return MODULE_NO_RESIDENT_END;
    if (StartThread(id, NULL) < 0) { DeleteThread(id); return MODULE_NO_RESIDENT_END; }
    return MODULE_RESIDENT_END;
}
