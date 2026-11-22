/* Temporary disk-format service, separate from the HDD and ATA drivers.
 * The EE resets the IOP after any successful BEGIN, including failed formats.
 * Formatting detaches ATA volumes; installation preserves the exFAT source
 * and restricts raw writes to APA/recovery sectors before that source. */
#include <types.h>
#include <irx.h>
#include <loadcore.h>
#include <thbase.h>
#include <sifrpc.h>
#include <atad.h>
#include <sysclib.h>
#include <errno.h>
#include <iomanX.h>
#include <hdd-ioctl.h>
#ifdef EXFAT
#include <bdm.h>
#endif
#include "hdd_format_rpc.h"

IRX_ID("LaunchstationFormat", 1, 2);

static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static HddFormatPacket packet __attribute__((aligned(64)));
static unsigned int capacity, write_limit;
static int active, installing;

/* Installation I/O uses the existing HDD driver's devctl semaphore, sharing
 * its serialization with PFS requests. FAT source reads and PFS/raw transfers
 * are issued sequentially by the same EE worker, with audio/FTP stopped. */
static int transfer(HddFormatPacket *p, unsigned int lba, unsigned int count, int dir)
{
    unsigned char *arg = p->data - sizeof(hddAtaTransfer_t);
    if (!installing)
        return ata_device_sector_io(0, p->data, lba, count, dir) ? -EIO : 0;
    /* Borrow the last two reserved header words; keep the DMA payload aligned
     * and avoid allocating another 32 KiB buffer on the IOP. */
    memcpy(arg, &lba, 4); memcpy(arg + 4, &count, 4);
    return iomanX_devctl("hdd0:", dir == ATA_DIR_READ ? HDIOC_READSECTOR : HDIOC_WRITESECTOR,
                         arg, sizeof(hddAtaTransfer_t) + (dir == ATA_DIR_WRITE ? count * 512 : 0),
                         dir == ATA_DIR_READ ? p->data : NULL, dir == ATA_DIR_READ ? count * 512 : 0);
}
static int flushDrive(void)
{
    return installing ? iomanX_devctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0) :
                        (ata_device_flush_cache(0) ? -EIO : 0);
}

static unsigned int get32(const unsigned char *p)
{ return p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24; }

/* The live FAT driver may read the source volume while raw writes populate
 * APA/ext2. Confirm the A2 layout from disk, never trust an EE-only boundary. */
static int installBoundary(HddFormatPacket *p)
{
    unsigned int sum = 0, apa, recovery, exfat, length;
    unsigned char *h = p->data;
    if (capacity < 262144 + 2048 + 65536 + 1) return -EINVAL;
    if (transfer(p, 0, 2, ATA_DIR_READ) < 0) return -EIO;
    for (unsigned int i = 4; i < 1024; i += 4) sum += get32(h + i);
    if (sum != get32(h) || get32(h + 4) != 0x00415041) return -EINVAL;
    if (!p->write_limit) {
        if (h[510] == 0x55 && h[511] == 0xaa) return -EINVAL;
        write_limit = capacity; return 0; /* pure APA, no FAT source area */
    }
    if (h[510] != 0x55 || h[511] != 0xaa ||
        h[450] != 0x17 || h[466] != 0x17 || h[482] != 0x07 || get32(h + 454) != 2048) return -EINVAL;
    apa = get32(h + 458); recovery = get32(h + 470); exfat = get32(h + 486); length = get32(h + 490);
    if (apa < 262144 || apa > capacity - 2048 || recovery != apa + 2048 ||
        get32(h + 474) != 65536 || recovery > capacity - 65536 || exfat != recovery + 65536 ||
        exfat >= capacity || !length || length > capacity - exfat || p->write_limit != exfat) return -EINVAL;
    write_limit = exfat;
    return 0;
}

static void *command(int cmd, void *data, int size)
{
    HddFormatPacket *p = data;
    unsigned int lba = p->lba, count = p->count, n;
    int ret = -EINVAL;
    if (size < 64) { p->result = -EINVAL; return p; }
    if (cmd == HF_BEGIN || cmd == HF_INSTALL_BEGIN) {
        ata_devinfo_t *info;
        if (active) { p->result = -EBUSY; return p; }
        info = ata_get_devinfo(0);
        if (!info || !info->exists || info->has_packet || !info->total_sectors || info->total_sectors == 0xffffffffu) {
            p->result = -ENODEV; return p;
        }
        capacity = info->total_sectors;
        /* Capacity agreed before confirmation must still match at BEGIN. */
        if (p->sectors != capacity) { p->result = -EIO; return p; }
        installing = cmd == HF_INSTALL_BEGIN;
        ret = flushDrive();
        if (ret) { p->result = -EIO; return p; }
        write_limit = capacity;
        if (cmd == HF_INSTALL_BEGIN) {
            ret = installBoundary(p);
            if (ret < 0) { p->result = ret; return p; }
        }
#ifdef EXFAT
        if (cmd == HF_BEGIN) {
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
        ret = flushDrive();
        if (cmd == HF_FINISH) { active = 0; installing = 0; }
    } else if (count && lba < capacity && count <= capacity - lba &&
               (cmd == HF_READ || (lba < write_limit && count <= write_limit - lba))) {
        if ((cmd == HF_READ || cmd == HF_WRITE) && count <= HDD_FORMAT_CHUNK &&
            (cmd != HF_WRITE || size >= (int)(64 + count * 512))) {
            ret = transfer(p, lba, count, cmd == HF_READ ? ATA_DIR_READ : ATA_DIR_WRITE);
        } else if (cmd == HF_ZERO && count <= 2048) {
            memset(p->data, 0, sizeof(p->data));
            ret = 0;
            while (count) {
                n = count > HDD_FORMAT_CHUNK ? HDD_FORMAT_CHUNK : count;
                ret = transfer(p, lba, n, ATA_DIR_WRITE);
                if (ret < 0) break;
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
