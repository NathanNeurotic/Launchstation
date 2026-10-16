/* Adapted from mihhru / PS2CF: https://github.com/mihhru/PS2CF
   Pinned provenance and preserved component terms: VENDORED.md. */
/* PCMCIA CF block device for OPL menu and game core.
   No SPEED, DEV9 interrupt handler or DMA. PS2SDK BDM ABI 1.1. */
#include <irx.h>
#include <loadcore.h>
#include <bdm.h>
#include <thbase.h>
#include <thsemap.h>
#include <sysclib.h>
#include <errno.h>
#include "pccf_ata.h"
#include "pcclog.h"

IRX_ID("pccf_bd", 1, 1);
static pccf_ata_t ata;
static int io_lock=-1;
static struct block_device device;
/* Preserve the ATA trace callback ABI without collecting or formatting logs. */
void pccardTrace(const char *format, ...) { (void)format; }

static int cf_read(struct block_device *bd, u64 sector, void *buffer, u16 count)
{
    unsigned int remaining=count, take; unsigned char *out=buffer; int rc=count;
    (void)bd;
    if (!buffer || !count || sector>=ata.sectors || count>ata.sectors-sector) return -EINVAL;
    if (WaitSema(io_lock) < 0) return -EIO;
    if (!ata.ready || !pccf_hw_present()) { ata.ready=0; rc=-EIO; goto done; }
    while (remaining) {
        take=remaining>64 ? 64 : remaining;
        if (pccf_ata_read(&ata,(unsigned int)sector,take,out)) { rc=-EIO; break; }
        sector+=take; out+=take*512; remaining-=take;
    }
done:
    SignalSema(io_lock); return rc;
}
static int cf_write(struct block_device *bd, u64 sector, const void *buffer, u16 count)
{
#ifdef GAME_DRIVER
    (void)bd; (void)sector; (void)buffer; (void)count; return -EROFS;
#else
    unsigned int remaining=count, take; const unsigned char *in=buffer; int rc=count;
    (void)bd;
    if (!buffer || !count || sector>=ata.sectors || count>ata.sectors-sector) return -EINVAL;
    if (WaitSema(io_lock) < 0) return -EIO;
    if (!ata.ready || !pccf_hw_present()) { ata.ready=0; rc=-EIO; goto done; }
    while (remaining) {
        take=remaining>64 ? 64 : remaining;
        if (pccf_ata_write(&ata,(unsigned int)sector,take,in)) { rc=-EIO; break; }
        sector+=take; in+=take*512; remaining-=take;
    }
done:
    SignalSema(io_lock); return rc;
#endif
}
/* Each successful GUI write already flushes and verifies before returning. */
static void cf_flush(struct block_device *bd) { (void)bd; }
static int cf_stop(struct block_device *bd)
{
    (void)bd;
    if (WaitSema(io_lock) < 0) return -EIO;
    ata.ready=0; pccf_hw_stop(); SignalSema(io_lock); return 0;
}

int _start(int argc, char **argv)
{
    iop_sema_t sema; int rc;
    (void)argc; (void)argv;
    /* A resident DEV9 library owns the controller even if it is currently
       powered down. Module entry runs under modload serialization. Walk the
       loadcore library chain without loading DEV9 or touching registers. */
    {
        lc_internals_t *core = GetLoadcoreInternalData();
        iop_library_t *library;
        unsigned int scanned = 0;
        if (!core) return MODULE_NO_RESIDENT_END;
        for (library = core->let_next; library; library = library->prev) {
            if (++scanned > 256 || !memcmp(library->name, "dev9\0\0\0\0", 8))
                return MODULE_NO_RESIDENT_END;
        }
    }
    memset(&sema,0,sizeof(sema)); sema.initial=1; sema.max=1;
    io_lock=CreateSema(&sema);
    if (io_lock<0) return MODULE_NO_RESIDENT_END;
    /* Launchstation shares the PC CARD bus with DEV9. Never reclaim a
       powered controller here: pccf_hw_start rejects a busy host without
       writing its registers. EE ownership checks are an additional gate. */
    rc=pccf_hw_start(&ata);
    if (rc) {
        DeleteSema(io_lock); return MODULE_NO_RESIDENT_END;
    }
    memset(&device,0,sizeof(device));
    device.name="cf"; device.path="cf"; device.devNr=0; device.parNr=0;
    device.sectorSize=512; device.sectorCount=ata.sectors;
    device.read=cf_read; device.write=cf_write; device.flush=cf_flush; device.stop=cf_stop;
    bdm_connect_bd(&device);
    return MODULE_RESIDENT_END;
}
