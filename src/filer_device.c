//--------------------------------------------------------------
//File name:   filer_device.c
//--------------------------------------------------------------
#include "filer_internal.h"
#include "init.h"

#ifdef EXFAT
#include <usbhdfsd-common.h>
#endif

/* Display names are never filesystem paths. A USB unit is NOT a massN slot:
 * mixed BDM mounts may place ATA, MX4SIO or other transports before USB.
 * This cache is updated only when a user browses a USB root, never by a
 * per-frame drawing function, and expires across IOP reset/discovery. */
static int usb_mass_display_slot[USB_MASS_MAX_DRIVES];
static unsigned int usb_mass_display_valid_mask;
static unsigned int usb_mass_display_generation = (unsigned int)-1;

void filerInvalidateUsbMassDisplayAliases(void)
{
	usb_mass_display_valid_mask = 0;
	usb_mass_display_generation = getIopResetGeneration();
}

#ifdef EXFAT
static int filerReadUsbMassRootIdentity(const char *root, int *device)
{
	char driver[32] = {0};
	int fd = fileXioDopen(root);
	int rc;

	if (fd < 0)
		return 0;
	/* Unlike GET_DRIVERNAME with a return buffer, GET_DEVICE_NUMBER
	 * safely rejects a filesystem slot with no mounted backing device.
	 * Never issue a buffered driver-name ioctl on an empty BDM slot. */
	rc = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DEVICE_NUMBER,
	                   NULL, 0, device, sizeof(*device));
	if (rc >= 0)
		rc = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME,
		                   NULL, 0, driver, sizeof(driver) - 1);
	fileXioDclose(fd);
	return rc >= 0 && !strcmp(driver, "usb");
}
#endif

void filerRefreshUsbMassDisplayAlias(int unit)
{
	if (unit < 0 || unit >= USB_MASS_MAX_DRIVES)
		return;
	if (usb_mass_display_generation != getIopResetGeneration())
		filerInvalidateUsbMassDisplayAliases();

	usb_mass_display_valid_mask &= ~(1u << unit);
	if (setting == NULL || setting->modern_browser_device_paths)
		return;
#ifdef EXFAT
	{
		char root[16];
		int actual_device = -1, match = -1;
		int slot;

		snprintf(root, sizeof(root), "usb%d:/", unit);
		if (!filerReadUsbMassRootIdentity(root, &actual_device))
			return;
		for (slot = 0; slot < USB_MASS_MAX_DRIVES; slot++) {
			int candidate_device = -1;
			snprintf(root, sizeof(root), "mass%d:/", slot);
			if (!filerReadUsbMassRootIdentity(root, &candidate_device) ||
			    candidate_device != actual_device)
				continue;
			/* Multiple partitions of one USB disk can share an IOP device
			 * number. Do not claim to know which MASS slot matches. */
			if (match >= 0)
				return;
			match = slot;
		}
		if (match < 0)
			return;
		usb_mass_display_slot[unit] = match;
	}
#else
	/* Classic USBMASS registers units as massN, not a shared BDM pool. */
	usb_mass_display_slot[unit] = unit;
#endif
	usb_mass_display_valid_mask |= (1u << unit);
}

/* Pure formatter: safe for dashboard rendering. On unverified identity use
 * the actual driver path, never a fictitious mass<unit> label. */
void filerFormatDevicePathForDisplay(const char *path, char *out, int size)
{
	int unit;
	const char *tail;

	if (out == NULL || size <= 0)
		return;
	if (path == NULL) {
		out[0] = '\0';
		return;
	}
	if (setting == NULL || setting->modern_browser_device_paths ||
	    strncmp(path, "usb", 3) || path[3] < '0' || path[3] > '9' ||
	    path[4] != ':') {
		snprintf(out, size, "%s", path);
		return;
	}
	unit = path[3] - '0';
	tail = path + 5;
	if (usb_mass_display_generation != getIopResetGeneration() ||
	    !(usb_mass_display_valid_mask & (1u << unit))) {
		snprintf(out, size, "%s", path);
		return;
	}
	snprintf(out, size, "mass%d:%s", usb_mass_display_slot[unit], tail);
}

enum {
	TRANSFER_STORAGE_DEFAULT = 0,
	TRANSFER_STORAGE_MMCE,
	TRANSFER_STORAGE_MX4SIO
};

#if defined(ETH) || defined(UDPFS)
extern int host_error;
#endif

static int transferStorageGateForPath(const char *path)
{
	if (path == NULL)
		return TRANSFER_STORAGE_DEFAULT;
#ifdef MMCE
	if (!strncmp(path, "mmce", 4))
		return TRANSFER_STORAGE_MMCE;
#endif
#ifdef MX4SIO
	if (!strncmp(path, "mx4sio", 6))
		return TRANSFER_STORAGE_MX4SIO;
#endif
	return TRANSFER_STORAGE_DEFAULT;
}

static int isMx4sioPath(const char *path)
{
#ifdef MX4SIO
	return (path != NULL && !strncmp(path, "mx4sio", 6));
#else
	(void)path;
	return FALSE;
#endif
}

static int isMc1Path(const char *path)
{
	return (path != NULL && !strncmp(path, "mc1:", 4));
}

static int transferUsesMx4sioAndMc1(const char *src_path, const char *dst_path)
{
	return ((isMx4sioPath(src_path) && isMc1Path(dst_path)) ||
	        (isMc1Path(src_path) && isMx4sioPath(dst_path)));
}

static int isDev9BackedPath(const char *path)
{
	if (path == NULL)
		return FALSE;
	return (!strncmp(path, "hdd", 3) ||
	        !strncmp(path, "pfs", 3) ||
	        !strncmp(path, "dvr_hdd", 7) ||
	        !strncmp(path, "dvr_pfs", 7) ||
	        !strncmp(path, "ata", 3) ||
	        !strncmp(path, "host", 4) ||
	        !strncmp(path, "udpfs", 5) ||
	        !strncmp(path, "xfrom", 5));
}

static int transferUsesPcCardCfAndDev9(const char *src_path, const char *dst_path)
{
	return ((isPcCardCfPath(src_path) && isDev9BackedPath(dst_path)) ||
	        (isDev9BackedPath(src_path) && isPcCardCfPath(dst_path)));
}

int ensurePathDeviceStackReady(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return TRUE;
	if (!strncmp(path, "mc", 2) || !strncmp(path, "vmc", 3))
		return TRUE;
#ifdef XFROM
	if (!strncmp(path, "xfrom", 5))
		return loadFlashModules();
#endif
#ifdef EXFAT
	if (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':')
		return isPathDeviceStackLoaded(path); /* do not guess its transport */
#endif
	if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3)) {
		loadUsbModules();
		return (USB_mass_loaded != 0);
	}
#ifdef MMCE
	if (!strncmp(path, "mmce", 4)) {
		return loadMmceModules();
	}
#endif
#ifdef MX4SIO
	if (!strncmp(path, "mx4sio", 6))
		return loadMx4sioModules();
#endif
#ifdef ILINK
	if (!strncmp(path, "ilink", 5))
		return loadIlinkModules();
#endif
#ifdef EXFAT
	if (!strncmp(path, "ata", 3)) {
		return loadAtaModules();
	}
#endif
	if (isPcCardCfPath(path))
		return loadPcCardCfModules() && isPcCardCfOnline();
	if (!strncmp(path, "hdd", 3) || !strncmp(path, "pfs", 3)) {
		return loadHddModules();
	}
#ifdef DVRP
	if (!strncmp(path, "dvr_hdd", 7) || !strncmp(path, "dvr_pfs", 7)) {
		return loadDVRPHddModules();
	}
#endif
	if (!strncmp(path, "cdfs", 4)) {
		loadCdModules();
		return TRUE;
	}
#ifdef ETH
	if (!strncmp(path, "host", 4)) {
		initHOST();
		return !host_error;
	}
#endif
#ifdef UDPFS
	if (!strncmp(path, "udpfs", 5)) {
		return load_udpfs();
	}
#endif
	return TRUE;
}
int prepareTransferDeviceStacks(const char *src_path, const char *dst_path)
{
	int src_storage = transferStorageGateForPath(src_path);
	int dst_storage = transferStorageGateForPath(dst_path);

	if (transferUsesMx4sioAndMc1(src_path, dst_path))
		return TRANSFER_STACK_INCOMPATIBLE;

	if (transferUsesPcCardCfAndDev9(src_path, dst_path))
		return TRANSFER_STACK_INCOMPATIBLE;

	if (src_storage != TRANSFER_STORAGE_DEFAULT &&
	    dst_storage != TRANSFER_STORAGE_DEFAULT &&
	    src_storage != dst_storage)
		return TRANSFER_STACK_INCOMPATIBLE;

#ifdef MX4SIO
	/* Normal slot-2 access can retire the MX4SIO IOP session. Do this
	 * before starting either transfer device or opening any file handle;
	 * a late mc1: stat/open would otherwise invalidate an open UDPFS/USB
	 * source. The incompatible MX4SIO <-> mc1 pair was rejected above. */
	if (isMc1Path(src_path) || isMc1Path(dst_path)) {
		ensureMemoryCardPortAccessible(1);
		if (mx4sio_driver_running || isPathDeviceStackLoaded("mx4sio:/"))
			return TRANSFER_STACK_LOAD_FAILED;
	}
#endif
	if (!ensurePathDeviceStackReady(src_path))
		return TRANSFER_STACK_LOAD_FAILED;
	if (!ensurePathDeviceStackReady(dst_path))
		return TRANSFER_STACK_LOAD_FAILED;
	return TRANSFER_STACK_READY;
}
//--------------------------------------------------------------
//End of file: filer_device.c
//--------------------------------------------------------------
