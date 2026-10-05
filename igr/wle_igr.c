/*
 * WLE-IGR.ELF -- minimal OPL IGR trampoline for MMCE-hosted wLaunchELF.
 *
 * This helper is independently implemented for wLaunchELF. It exists because
 * OPL's IGR environment is intentionally minimal and cannot directly start an
 * ELF from the sd2psXtd filesystem. wLaunchELF patches the config block below
 * before installing the helper.
 */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <iopheap.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <sbv_patches.h>
#include <tamtypes.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef FIO_MT_RDONLY
#define FIO_MT_RDONLY 0x01
#endif
#include "wle_igr_config.h"

extern u8 iomanx_irx[];
extern int size_iomanx_irx;
extern u8 filexio_irx[];
extern int size_filexio_irx;
extern u8 sio2man_irx[];
extern int size_sio2man_irx;
extern u8 mcman_irx[];
extern int size_mcman_irx;
extern u8 mcserv_irx[];
extern int size_mcserv_irx;
extern u8 mmceman_irx[];
extern int size_mmceman_irx;
extern u8 usbd_irx[];
extern int size_usbd_irx;
extern u8 bdm_irx[];
extern int size_bdm_irx;
extern u8 bdmfs_fatfs_irx[];
extern int size_bdmfs_fatfs_irx;
extern u8 usbmass_bd_irx[];
extern int size_usbmass_bd_irx;
extern u8 mx4sio_bd_irx[];
extern int size_mx4sio_bd_irx;
extern u8 iLinkman_irx[];
extern int size_iLinkman_irx;
extern u8 IEEE1394_bd_irx[];
extern int size_IEEE1394_bd_irx;
extern u8 ps2dev9_irx[];
extern int size_ps2dev9_irx;
extern u8 ata_bd_irx[];
extern int size_ata_bd_irx;
extern u8 ps2hdd_irx[];
extern int size_ps2hdd_irx;
extern u8 ps2fs_irx[];
extern int size_ps2fs_irx;

#define WLE_IGR_TRIES 24

/*
 * Deliberately non-const: the installer finds WLE_IGR_MAGIC inside the ELF
 * image and writes these two fixed-size fields into the copy installed for
 * OPL. Do not reorder this structure without bumping the magic/version.
 */
__attribute__((used, aligned(16)))
volatile WleIgrEmbeddedConfig wleIgrEmbeddedConfig = {
    WLE_IGR_MAGIC,
    "",
    ""
};

static int start_module(const void *image, int size)
{
    int result = 0;
    int id = SifExecModuleBuffer((void *)image, size, 0, NULL, &result);

    if (id < 0 || result < 0)
        return -1;
    return 0;
}

static int path_exists(const char *path)
{
    int fd;

    fd = fileXioOpen(path, O_RDONLY, 0);
    if (fd < 0)
        return 0;
    fileXioClose(fd);
    return 1;
}

static int hdd_target_path(const char *configured, char *out, size_t out_size)
{
    static char hddarg[] = "-o\0" "4\0" "-n\0" "20";
    static char pfsarg[] = "-m\0" "4\0" "-o\0" "10\0" "-n\0" "40";
    char party[96];
    const char *sep;
    size_t prefix_len, partition_len;
    int result = 0;

    if (configured == NULL ||
        (strncmp(configured, "hdd0:", 5) && strncmp(configured, "hdd1:", 5)))
        return 0;
    sep = strstr(configured + 5, ":pfs:");
    if (sep == NULL)
        return 0;

    prefix_len = 5;
    partition_len = sep - (configured + prefix_len);
    if (partition_len == 0 || prefix_len + partition_len >= sizeof(party))
        return 0;

    memcpy(party, configured, prefix_len + partition_len);
    party[prefix_len + partition_len] = '\0';

    if (start_module(ps2dev9_irx, size_ps2dev9_irx) < 0)
        return 0;
    start_module(bdm_irx, size_bdm_irx);
    start_module(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
    start_module(ata_bd_irx, size_ata_bd_irx);

    if (SifExecModuleBuffer(ps2hdd_irx, size_ps2hdd_irx,
                            sizeof(hddarg), hddarg, &result) < 0 || result < 0)
        return 0;
    result = 0;
    if (SifExecModuleBuffer(ps2fs_irx, size_ps2fs_irx,
                            sizeof(pfsarg), pfsarg, &result) < 0 || result < 0)
        return 0;

    fileXioInit();
    fileXioUmount("pfs0:");
    if (fileXioMount("pfs0:", party, FIO_MT_RDONLY) < 0)
        return 0;

    return snprintf(out, out_size, "pfs0:%s", sep + 5) < (int)out_size;
}

static int resolve_app_path(const char *configured, char *out, size_t out_size)
{
    int attempt;

    if (configured == NULL || configured[0] == '\0')
        return 0;

    for (attempt = 0; attempt < WLE_IGR_TRIES; attempt++) {
        if (!strncmp(configured, "hdd0:", 5) || !strncmp(configured, "hdd1:", 5)) {
            if (hdd_target_path(configured, out, out_size) && path_exists(out))
                return 1;
        } else if (!strncmp(configured, "mmce?:", 6)) {
            snprintf(out, out_size, "mmce0:%s", configured + 6);
            if (path_exists(out))
                return 1;
            snprintf(out, out_size, "mmce1:%s", configured + 6);
            if (path_exists(out))
                return 1;
        } else if (strlen(configured) < out_size) {
            snprintf(out, out_size, "%s", configured);
            if (path_exists(out))
                return 1;
        }
        usleep(250000);
    }

    return 0;
}

static void run_wle(const char *path)
{
    t_ExecData elf;
    char backup_arg[WLE_IGR_PATH + 24];
    char *argv[3];
    int argc = 2;

    memset(&elf, 0, sizeof(elf));
    if (SifLoadElf(path, &elf) != 0 || elf.epc == 0)
        return;

    argv[0] = (char *)path;
    argv[1] = "-wle-igr";
    if (wleIgrEmbeddedConfig.backup[0] != '\0') {
        snprintf(backup_arg, sizeof(backup_arg), "-wle-igr-backup=%s",
                 (const char *)wleIgrEmbeddedConfig.backup);
        argv[2] = backup_arg;
        argc = 3;
    }

    FlushCache(0);
    FlushCache(2);
    fileXioExit();
    SifLoadFileExit();
    SifExitRpc();
    ExecPS2((void *)elf.epc, (void *)elf.gp, argc, argv);
}

int main(int argc, char *argv[])
{
    char target[WLE_IGR_PATH];

    (void)argc;
    (void)argv;

    SifInitRpc(0);
    while (!SifIopReset("", 0))
        ;
    while (!SifIopSync())
        ;

    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    if (start_module(iomanx_irx, size_iomanx_irx) < 0 ||
        start_module(filexio_irx, size_filexio_irx) < 0 ||
        start_module(sio2man_irx, size_sio2man_irx) < 0)
        goto osd;

    /* Load only the storage family needed to relaunch the configured
     * wLaunchELF path. OPL launches this helper from MC/USB; the target may
     * live on MC, USB, MMCE, MX4SIO or iLink. */
    if (!strncmp((const char *)wleIgrEmbeddedConfig.app, "mc", 2)) {
        start_module(mcman_irx, size_mcman_irx);
        start_module(mcserv_irx, size_mcserv_irx);
    } else if (!strncmp((const char *)wleIgrEmbeddedConfig.app, "mmce", 4)) {
        start_module(mmceman_irx, size_mmceman_irx);
    } else if (!strncmp((const char *)wleIgrEmbeddedConfig.app, "mass", 4) ||
               !strncmp((const char *)wleIgrEmbeddedConfig.app, "usb", 3)) {
        if (start_module(usbd_irx, size_usbd_irx) >= 0) {
            start_module(bdm_irx, size_bdm_irx);
            start_module(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
            start_module(usbmass_bd_irx, size_usbmass_bd_irx);
        }
    } else if (!strncmp((const char *)wleIgrEmbeddedConfig.app, "mx4sio", 6)) {
        start_module(bdm_irx, size_bdm_irx);
        start_module(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
        start_module(mx4sio_bd_irx, size_mx4sio_bd_irx);
    } else if (!strncmp((const char *)wleIgrEmbeddedConfig.app, "ilink", 5)) {
        start_module(bdm_irx, size_bdm_irx);
        start_module(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
        if (start_module(iLinkman_irx, size_iLinkman_irx) >= 0)
            start_module(IEEE1394_bd_irx, size_IEEE1394_bd_irx);
    } else if (!strncmp((const char *)wleIgrEmbeddedConfig.app, "ata", 3)) {
        if (start_module(ps2dev9_irx, size_ps2dev9_irx) >= 0) {
            start_module(bdm_irx, size_bdm_irx);
            start_module(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
            start_module(ata_bd_irx, size_ata_bd_irx);
        }
    }

    fileXioInit();

    if (!resolve_app_path((const char *)wleIgrEmbeddedConfig.app,
                          target, sizeof(target)))
        goto osd;

    run_wle(target);

osd:
    ExecOSD(0, NULL);
    return 0;
}
