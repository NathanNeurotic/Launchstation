//--------------------------------------------------------------
//File name:   dash_devices.c
//Device model for the dashboard DEVICES / FILES screens.
//
//Only information wLaunchELF already has is shown; nothing here loads an
//IOP driver on its own. Devices whose driver is not running yet are listed
//as "not checked" and are only probed (through the regular lazy loaders in
//init.c) when the user explicitly selects them. Memory card presence/free
//space comes from the FileBrowser's asynchronous root poller, disc state
//from the classic uLE_cdStop() tracking.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "init.h"

extern int cdmode;
extern int vmcMounted[2];

/* remembered results of explicit probes that failed */
static u8 probe_failed_hdd, probe_failed_ata, probe_failed_mx4sio, probe_failed_mmce, probe_failed_udpfs;

/* Device-presence snapshot for connect/disconnect sounds.
 *
 * Mount discovery is asynchronous: MC/USB state can continue changing for a
 * short time after the dashboard starts. A single silent first poll is not
 * enough because a boot-mounted device that appears on the second/third poll
 * would look exactly like a real hotplug. Keep hotplug sounds disarmed until
 * the snapshot has stayed unchanged for a settling period; all boot-time
 * mount transitions are folded into the baseline silently. */
#define DEV_HOTPLUG_SETTLE_MS 2000
static u32 dev_snapshot = 0xFFFFFFFFu;
static u64 dev_snapshot_changed_ms;
static int dev_hotplug_armed;

void dashDevicesInit(void)
{
	dev_snapshot = 0xFFFFFFFFu;
	dev_snapshot_changed_ms = Timer();
	dev_hotplug_armed = 0;
}

/* Re-arm the silent baseline (used when the dash (re)starts, so devices that
 * are already attached - the boot device, memory cards - never play
 * connect sounds; only later hot-plug/unplug events do). */
void dashDevicesResetBaseline(void)
{
	dev_snapshot = 0xFFFFFFFFu;
	dev_snapshot_changed_ms = Timer();
	dev_hotplug_armed = 0;
}

void dashDevicesPoll(void)
{
	u32 now = 0;
	int i;

	pollRootMemoryCardDevices();
	for (i = 0; i < 2; i++)
		if (filerRootMcPresent(i, NULL, NULL) > 0)
			now |= 1u << i;
	if (filerUsbDriversLoaded()) {
		int m = filerUsbDriveMask();
		for (i = 0; i < 10; i++)
			if (m & (1 << i))
				now |= 1u << (2 + i);
	}
	if (!dev_hotplug_armed) {
		u64 ms = Timer();

		/* Boot mounts and post-IOP-reset rediscovery are baseline changes, not
		 * user hotplugs. Any change restarts the quiet settling timer. */
		if (dev_snapshot == 0xFFFFFFFFu || now != dev_snapshot) {
			dev_snapshot = now;
			dev_snapshot_changed_ms = ms;
		} else if (ms - dev_snapshot_changed_ms >= DEV_HOTPLUG_SETTLE_MS) {
			/* From this point onward, changes are genuine runtime hotplug events. */
			dev_hotplug_armed = 1;
		}
	} else if (now != dev_snapshot) {
		if (now & ~dev_snapshot)
			dashSoundPlay(DASH_SND_CONNECT);
		if (dev_snapshot & ~now)
			dashSoundPlay(DASH_SND_DISCONNECT);
		dev_snapshot = now;
	}
}

int dashMcPresent(int port)
{
	return filerRootMcPresent(port, NULL, NULL) != 0;
}

const char *dashDiscStatus(void)
{
	switch (uLE_cdmode) {
		case SCECdNODISC:
			return "No disc";
		case SCECdDETCT:
		case SCECdDETCTCD:
		case SCECdDETCTDVDS:
		case SCECdDETCTDVDD:
			return "Reading disc...";
		case SCECdPSCD:
		case SCECdPSCDDA:
			return "PlayStation disc";
		case SCECdPS2CD:
		case SCECdPS2CDDA:
			return "PlayStation 2 CD";
		case SCECdPS2DVD:
			return "PlayStation 2 DVD";
		case SCECdESRDVD_0:
		case SCECdESRDVD_1:
			return "ESR disc";
		case SCECdCDDA:
			return "Audio CD";
		case SCECdDVDV:
			return "DVD Video";
		case SCECdIllegalMedia:
			return "Unsupported disc";
		default:
			return (cdmode > 0) ? "Disc inserted" : "No disc";
	}
}

static DashDevice *add(DashDevice *out, int *n, int max, int icon, int state,
                       const char *name, const char *mount, const char *browse,
                       const char *status)
{
	DashDevice *d;

	if (*n >= max)
		return NULL;
	d = &out[(*n)++];
	memset(d, 0, sizeof(*d));
	d->icon = icon;
	d->state = state;
	d->fill = -1.0f;
	dashStrCopy(d->name, name, sizeof(d->name));
	dashStrCopy(d->mount, mount, sizeof(d->mount));
	dashStrCopy(d->browse, browse, sizeof(d->browse));
	dashStrCopy(d->status, status, sizeof(d->status));
	return d;
}

int dashDevicesList(DashDevice *out, int max)
{
	int n = 0, port, i;
	DashDevice *d;

	for (port = 0; port < 2; port++) {
		int type = 0, free_kb = -1;
		int present = filerRootMcPresent(port, &type, &free_kb);
		char name[32], mount[8], browse[8];

		snprintf(name, sizeof(name), "Memory Card %d", port + 1);
		snprintf(mount, sizeof(mount), "mc%d:", port);
		snprintf(browse, sizeof(browse), "mc%d:/", port);
		if (present < 0)
			d = add(out, &n, max, DI_MC, DASH_DEV_BUSY, name, mount, browse, "Checking...");
		else if (present == 0)
			d = add(out, &n, max, DI_MC, DASH_DEV_ABSENT, name, mount, browse, "Slot empty");
		else {
			d = add(out, &n, max, DI_MC, DASH_DEV_READY, name, mount, browse,
			        type == 1 ? "PS1 memory card" : "PS2 memory card");
			if (d != NULL && free_kb >= 0) {
				if (type == 2 && free_kb <= 8 * 1024) {
					snprintf(d->detail, sizeof(d->detail), "%d KB free of 8 MB", free_kb);
					d->fill = 1.0f - free_kb / 8192.0f;
				} else if (free_kb >= 1024)
					snprintf(d->detail, sizeof(d->detail), "%.1f MB free", free_kb / 1024.0f);
				else
					snprintf(d->detail, sizeof(d->detail), "%d KB free", free_kb);
			}
		}
	}

	if (isUsbMassStackLoaded()) {
		int mask = filerUsbDriveMask();
		if (mask == 0)
			add(out, &n, max, DI_USB, DASH_DEV_ABSENT, "USB Storage", "usb:", "usb:/", "Connect a USB storage device");
		for (i = 0; i < 10; i++) {
			char name[32], mount[8], browse[8];
			if (!(mask & (1 << i)))
				continue;
			snprintf(name, sizeof(name), "USB Storage %d", i + 1);
			snprintf(mount, sizeof(mount), "usb%d:", i);
			snprintf(browse, sizeof(browse), "usb%d:/", i);
			add(out, &n, max, DI_USB, DASH_DEV_READY, name, mount, browse, "Ready");
		}
	} else
		add(out, &n, max, DI_USB, DASH_DEV_PROBE, "USB Storage", "usb:", "usb:/", "Not checked yet");

#ifdef MMCE
	if (isPathDeviceStackLoaded("mmce0:")) {
		add(out, &n, max, DI_SDCARD, DASH_DEV_READY, "MMCE Slot 1", "mmce0:", "mmce0:/", "SD2PSX / MemCard PRO2");
		add(out, &n, max, DI_SDCARD, DASH_DEV_READY, "MMCE Slot 2", "mmce1:", "mmce1:/", "SD2PSX / MemCard PRO2");
	} else
		add(out, &n, max, DI_SDCARD, probe_failed_mmce ? DASH_DEV_ABSENT : DASH_DEV_PROBE, "MMCE SD Card", "mmce:", "mmce0:/",
		    probe_failed_mmce ? "Driver could not start" : "Not checked yet");
#endif
#ifdef MX4SIO
	if (isPathDeviceStackLoaded("mx4sio:"))
		add(out, &n, max, DI_SDCARD, DASH_DEV_READY, "MX4SIO SD Card", "mx4sio:", "mx4sio:/", "Ready (slot 2)");
	else
		add(out, &n, max, DI_SDCARD, probe_failed_mx4sio ? DASH_DEV_ABSENT : DASH_DEV_PROBE, "MX4SIO SD Card", "mx4sio:", "mx4sio:/",
		    probe_failed_mx4sio ? "No MX4SIO adapter found" : "Not checked yet");
#endif

	if (filerShowHddUnit(0) && !console_is_PSX) {
		if (isPathDeviceStackLoaded("hdd0:"))
			add(out, &n, max, DI_HDD, DASH_DEV_READY, "Internal HDD", "hdd0:", "hdd0:/", "APA partitions");
		else
			add(out, &n, max, DI_HDD, probe_failed_hdd ? DASH_DEV_ABSENT : DASH_DEV_PROBE, "Internal HDD", "hdd0:", "hdd0:/",
			    probe_failed_hdd ? "No HDD detected" : "Not checked yet");
	}
#ifdef EXFAT
	if (filerShowAtaUnit(0) && !console_is_PSX) {
		if (isPathDeviceStackLoaded("ata0:"))
			add(out, &n, max, DI_HDD, DASH_DEV_READY, "exFAT HDD", "ata0:", "ata0:/", "Ready");
		else
			add(out, &n, max, DI_HDD, probe_failed_ata ? DASH_DEV_ABSENT : DASH_DEV_PROBE, "exFAT HDD", "ata0:", "ata0:/",
			    probe_failed_ata ? "No exFAT drive found" : "Not checked yet");
	}
#endif

	{
		int has_disc = (cdmode > 0 && uLE_cdmode != SCECdNODISC);
		add(out, &n, max, DI_DISC, has_disc ? DASH_DEV_READY : DASH_DEV_ABSENT, "Disc Drive", "cdfs:", "cdfs:/", dashDiscStatus());
	}

#ifdef UDPFS
	if (isPathDeviceStackLoaded("udpfs:"))
		add(out, &n, max, DI_NETWORK, DASH_DEV_READY, "Network (UDPFS)", "udpfs:", "udpfs:/", netConfig[0] ? netConfig : "Connected");
	else
		add(out, &n, max, DI_NETWORK, probe_failed_udpfs ? DASH_DEV_ABSENT : DASH_DEV_PROBE, "Network (UDPFS)", "udpfs:", "udpfs:/",
		    probe_failed_udpfs ? "UDPFS server not found" : "Not connected");
#endif
#ifdef ETH
	add(out, &n, max, DI_NETWORK, DASH_DEV_PROBE, "Network Host", "host:", "host:/", "PS2Link host");
#endif
#ifdef XFROM
	if (console_is_PSX || boot_show_all_devices)
		add(out, &n, max, DI_CHIP, DASH_DEV_PROBE, "PSX Flash", "xfrom0:", "xfrom0:/", "DESR internal flash");
#endif
#ifdef DVRP
	if (console_is_PSX || boot_show_all_devices)
		add(out, &n, max, DI_HDD, DASH_DEV_PROBE, "PSX DVR HDD", "dvr_hdd0:", "dvr_hdd0:/", "DESR recorder HDD");
#endif
	for (i = 0; i < 2; i++) {
		if (vmcMounted[i]) {
			char name[24], mount[8], browse[8];
			snprintf(name, sizeof(name), "Virtual MC %d", i);
			snprintf(mount, sizeof(mount), "vmc%d:", i);
			snprintf(browse, sizeof(browse), "vmc%d:/", i);
			add(out, &n, max, DI_MC, DASH_DEV_READY, name, mount, browse, "Mounted image");
		}
	}
	return n;
}

static int probeMount(const char *m)
{
	int ok = 1;

	filerRootMcPollIdle();
	if (!strncmp(m, "usb", 3)) {
		loadUsbModules();
		ok = (prepareUsbRootBrowse() >= 0);
	} else if (!strncmp(m, "hdd", 3)) {
		ok = loadHddModules();
		probe_failed_hdd = !ok;
	}
#ifdef EXFAT
	else if (!strncmp(m, "ata", 3)) {
		ok = loadAtaModules();
		if (ok) {
			int dd = fileXioDopen("ata0:/");
			if (dd >= 0)
				fileXioDclose(dd);
			else
				ok = 0;
		}
		probe_failed_ata = !ok;
	}
#endif
#ifdef MMCE
	else if (!strncmp(m, "mmce", 4)) {
		ok = loadMmceModules();
		probe_failed_mmce = !ok;
	}
#endif
#ifdef MX4SIO
	else if (!strncmp(m, "mx4sio", 6)) {
		ok = loadMx4sioModules();
		if (ok) {
			int dd = fileXioDopen("mx4sio:/");
			if (dd >= 0)
				fileXioDclose(dd);
			else
				ok = 0;
		}
		probe_failed_mx4sio = !ok;
	}
#endif
#ifdef UDPFS
	else if (!strncmp(m, "udpfs", 5)) {
		ok = load_udpfs();
		probe_failed_udpfs = !ok;
	}
#endif
#ifdef XFROM
	else if (!strncmp(m, "xfrom", 5))
		ok = loadFlashModules();
#endif
#ifdef DVRP
	else if (!strncmp(m, "dvr_hdd", 7))
		ok = loadDVRPHddModules();
#endif
	return ok;
}

int dashDevicesProbe(const DashDevice *dev)
{
	return probeMount(dev->mount);
}

/* Load the drivers for every device family marked auto-start that is not
 * already running. Never unloads anything: "off" only means "not at
 * startup" — the DEVICES screen can still start a device by hand. */
void dashDevicesAutoProbe(void)
{
	if ((dashCfg.dev_auto & DASH_DEVA_USB) && !isUsbMassStackLoaded())
		probeMount("usb:");
	if ((dashCfg.dev_auto & DASH_DEVA_HDD) && !isPathDeviceStackLoaded("hdd0:"))
		probeMount("hdd0:");
#ifdef EXFAT
	if ((dashCfg.dev_auto & DASH_DEVA_ATA) && !isPathDeviceStackLoaded("ata0:"))
		probeMount("ata0:");
#endif
#ifdef MMCE
	if ((dashCfg.dev_auto & DASH_DEVA_MMCE) && !isPathDeviceStackLoaded("mmce0:"))
		probeMount("mmce0:");
#endif
#ifdef MX4SIO
	if ((dashCfg.dev_auto & DASH_DEVA_MX4SIO) && !isPathDeviceStackLoaded("mx4sio:"))
		probeMount("mx4sio:");
#endif
#ifdef UDPFS
	if ((dashCfg.dev_auto & DASH_DEVA_UDPFS) && !isPathDeviceStackLoaded("udpfs:"))
		probeMount("udpfs:");
#endif
}

/* The DASH_DEVA_* bit of the device the ELF was launched from, 0 when it
 * has no toggle (memory cards, host) or cannot be determined. The boot
 * device is always available: its driver is already running. */
int dashBootDeviceBit(void)
{
	const char *p = LaunchElfDir;

	if (!strncmp(p, "mass", 4) || !strncmp(p, "usb", 3))
		return DASH_DEVA_USB;
	if (!strncmp(p, "hdd", 3))
		return DASH_DEVA_HDD;
	if (!strncmp(p, "ata", 3))
		return DASH_DEVA_ATA;
	if (!strncmp(p, "mmce", 4))
		return DASH_DEVA_MMCE;
	if (!strncmp(p, "mx4sio", 6))
		return DASH_DEVA_MX4SIO;
	return 0;
}
//--------------------------------------------------------------
//End of file: dash_devices.c
//--------------------------------------------------------------
