/* Launchstation-specific lifecycle control for mihhru / PS2CF's driver.
 * Deliberately separate from cf: (BDMFS) and from global BDM STOP_ALL. */
#define IOMANX_OLD_NAME_COMPATIBILITY 0
#define IOMANX_OLD_NAME_ADDDELDRV 0
#include <iomanX.h>
#include "../../include/pccf_control.h"

static int control_init(iomanX_iop_device_t *dev) { (void)dev; return 0; }
/* The resident BDM registration holds pointers into this module. */
static int control_deinit(iomanX_iop_device_t *dev) { (void)dev; return -EBUSY; }
static int control_devctl(iomanX_iop_file_t *file, const char *name, int command,
                          void *args, unsigned int arglen, void *buffer, unsigned int buflen)
{
    pccf_control_status_t *status = buffer;
    (void)name;
    if (!file || file->unit != 0) return -ENODEV;
    if (args || arglen) return -EINVAL;
    if (command == PCCF_CONTROL_OFF) {
        if (buffer || buflen) return -EINVAL;
        return cf_stop(&device);
    }
    if (command != PCCF_CONTROL_STATUS || !buffer || buflen != sizeof(*status))
        return -EINVAL;
    if (WaitSema(io_lock) < 0) return -EIO;
    if (ata.ready && !pccf_hw_present()) ata.ready = 0;
    status->version = 1;
    status->online = ata.ready != 0;
    status->sectors = ata.sectors;
    status->reserved = 0;
    SignalSema(io_lock);
    return 0;
}

/* Same SDK fallback convention as the existing HDD driver. No NULL ops. */
IOMANX_RETURN_VALUE_IMPL(EPERM);
static iomanX_iop_device_ops_t control_ops = {
    control_init, control_deinit,
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE_S64(EPERM), control_devctl,
    IOMANX_RETURN_VALUE(EPERM), IOMANX_RETURN_VALUE(EPERM),
    IOMANX_RETURN_VALUE(EPERM)
};
static iomanX_iop_device_t control_device = {
    "pccfctl", IOP_DT_CHAR | IOP_DT_FSEXT, 1, "PC CARD CF owner control", &control_ops
};
