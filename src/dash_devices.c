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
#include <delaythread.h>

extern int cdmode;
extern int vmcMounted[2];

/* Driver residency does not establish that a card, disk or server exists.
 * Cache explicit root checks; never do blocking device IO from drawing. */
enum { PROBE_HDD, PROBE_ATA, PROBE_MX4SIO, PROBE_ILINK, PROBE_MMCE0, PROBE_MMCE1, PROBE_UDPFS };
static u32 probe_checked, probe_present;
static unsigned int probe_reset_generation = (unsigned int)-1;

/* An absent drive is not proof that the USB IOP is poisoned. Remember
 * consecutive explicit failures and only escalate to a reset after the
 * stack itself fails, or after a second no-media/no-enumeration result. */
static unsigned int usb_probe_generation = (unsigned int)-1;
static int usb_probe_failures;
#define DASH_USB_PROBE_ATTEMPTS 5
#define DASH_USB_PROBE_DELAY_US 200000

static int probeIndex(const char *mount)
{
	if (!strncmp(mount, "hdd0:", 5)) return PROBE_HDD;
	if (!strncmp(mount, "ata0:", 5)) return PROBE_ATA;
	if (!strncmp(mount, "mx4sio", 6)) return PROBE_MX4SIO;
	if (!strncmp(mount, "ilink", 5)) return PROBE_ILINK;
	if (!strncmp(mount, "mmce", 4)) return mount[4] == '1' ? PROBE_MMCE1 : PROBE_MMCE0;
	if (!strncmp(mount, "udpfs:", 6)) return PROBE_UDPFS;
	return -1;
}

static void syncProbeSession(void)
{
	if (probe_reset_generation != getIopResetGeneration()) {
		probe_checked = probe_present = 0;
		probe_reset_generation = getIopResetGeneration();
	}
}

static void recordProbe(const char *mount, int ok)
{
	int i = probeIndex(mount);
	syncProbeSession();
	if (i < 0) return;
	probe_checked |= 1u << i;
	if (ok) probe_present |= 1u << i;
	else probe_present &= ~(1u << i);
}

static int storageState(const char *mount)
{
	int i = probeIndex(mount);
	syncProbeSession();
	if (i >= 0 && (probe_checked & (1u << i))) {
		if (!(probe_present & (1u << i))) return DASH_DEV_ABSENT;
		if (isPathDeviceStackLoaded(mount)) return DASH_DEV_READY;
	}
	return DASH_DEV_PROBE;
}

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
	syncProbeSession();
	/* The executing ELF is evidence for its own boot storage, even before a
	 * manual check. Use the boot directory, not a SYS-CONF fallback directory. */
	if (LaunchElfBootDir[0] && isPathDeviceStackLoaded(LaunchElfBootDir))
		recordProbe(LaunchElfBootDir, 1);
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
	int i, view_changed = 0;

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
			/* The first poll also refreshes the cache-backed UI: USB/MC mount
			 * state may finish populating just after DASHLIB.CNF was loaded. */
			view_changed = 1;
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
		view_changed = 1;
	}
	if (view_changed)
		dashAppsRebuildView();
}

int dashMcPresent(int port)
{
	return filerRootMcPresent(port, NULL, NULL) > 0;
}

/* True only when the storage backing an application path is currently
 * usable. The library cache deliberately survives device disconnects, so UI
 * code must use this before exposing a cached application. This check never
 * loads a driver: an inactive device stays inactive and its apps stay hidden. */
int dashDevicePathReady(const char *path)
{
	int mask;

	if (path == NULL || path[0] == '\0')
		return 0;

	/* Memory-card services are always loaded, so test actual card presence. */
	if (!strncmp(path, "mc0:", 4))
		return filerRootMcPresent(0, NULL, NULL) > 0;
	if (!strncmp(path, "mc1:", 4))
		return filerRootMcPresent(1, NULL, NULL) > 0;
	if (!strncmp(path, "mc:", 3))
		return filerRootMcPresent(0, NULL, NULL) > 0 ||
		       filerRootMcPresent(1, NULL, NULL) > 0;

	/* USB's driver can remain resident with no drive inserted, so require a
	 * mounted unit as well. "mass:" is the legacy alias for USB storage. */
	if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3)) {
		if (!isUsbMassStackLoaded())
			return 0;
		mask = filerUsbDriveMask();
		if (!strncmp(path, "usb", 3) && path[3] >= '0' && path[3] <= '9' && path[4] == ':')
			return (mask & (1 << (path[3] - '0'))) != 0;
		return mask != 0;
	}

	/* A VMC path is valid only while that image is actually mounted. */
	if (!strncmp(path, "vmc0:", 5))
		return vmcMounted[0] != 0;
	if (!strncmp(path, "vmc1:", 5))
		return vmcMounted[1] != 0;

	/* Do not show disc-backed cached entries when the tray is empty. */
	if (!strncmp(path, "cdfs:", 5) || !strncmp(path, "cdrom", 5))
		return cdmode > 0 && uLE_cdmode != SCECdNODISC;

	/* ROM is intrinsic storage. All other supported dashboard storage families
	 * (HDD, ATA/exFAT, MMCE, MX4SIO, UDPFS) are ready only when their existing
	 * IOP stack is active. isPathDeviceStackLoaded() has no side effects. */
	if (!strncmp(path, "rom", 3))
		return 1;
	/* A failed explicit check must also hide cached apps from that storage. */
	if (storageState(path) == DASH_DEV_ABSENT)
		return 0;
	return isPathDeviceStackLoaded(path);
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

static void addStorage(DashDevice *out, int *n, int max, int icon,
                       const char *name, const char *mount, const char *browse,
                       const char *ready, const char *absent)
{
	int state = storageState(mount);
	add(out, n, max, icon, state, name, mount, browse,
	    state == DASH_DEV_READY ? ready : state == DASH_DEV_ABSENT ? absent : "Not connected");
}

int dashDevicesList(DashDevice *out, int max)
{
	int n = 0, port, i;
	DashDevice *d;

	for (port = 0; port < 2; port++) {
		int type = 0, free_kb = -1;
		int present = filerRootMcPresent(port, &type, &free_kb);
		char name[32], mount[8], browse[8];

		snprintf(name, sizeof(name), "Memory Card %d", port);
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
					snprintf(d->detail, sizeof(d->detail), "%.2f MB free of 8 MB", free_kb / 1024.0f);
					d->fill = 1.0f - free_kb / 8192.0f;
				} else
					snprintf(d->detail, sizeof(d->detail), "%.2f MB free", free_kb / 1024.0f);
			}
		}
	}

	if (isUsbMassStackLoaded()) {
		int mask = filerUsbDriveMask();
		if (mask == 0)
			add(out, &n, max, DI_USB, DASH_DEV_ABSENT, "MASS (USB)", "usb:", "usb:/", "Connect a USB storage device");
		for (i = 0; i < 10; i++) {
			char name[32], mount[8], browse[8];
			if (!(mask & (1 << i)))
				continue;
			snprintf(name, sizeof(name), "MASS (USB) %d", i);
			snprintf(mount, sizeof(mount), "usb%d:", i);
			snprintf(browse, sizeof(browse), "usb%d:/", i);
			add(out, &n, max, DI_USB, DASH_DEV_READY, name, mount, browse, "Ready");
		}
	} else
		add(out, &n, max, DI_USB, DASH_DEV_PROBE, "MASS (USB)", "usb:", "usb:/", "Not connected");

#ifdef MMCE
	addStorage(out, &n, max, DI_SDCARD, "MMCE Slot 1", "mmce0:", "mmce0:/", "SD2PSX / MemCard PRO2", "No MMCE storage found");
	addStorage(out, &n, max, DI_SDCARD, "MMCE Slot 2", "mmce1:", "mmce1:/", "SD2PSX / MemCard PRO2", "No MMCE storage found");
#endif
#ifdef MX4SIO
	addStorage(out, &n, max, DI_SDCARD, "MX4SIO SD Card", "mx4sio:", "mx4sio:/", "Ready (slot 2)", "No MX4SIO storage found");
#endif
#ifdef ILINK
	addStorage(out, &n, max, DI_HDD, "iLink / FireWire", "ilink:", "ilink:/", "IEEE1394 storage ready", "No iLink storage found");
#endif

	if (filerShowHddUnit(0) && !console_is_PSX) {
		addStorage(out, &n, max, DI_HDD, "Internal HDD (APA)", "hdd0:", "hdd0:/", "APA partitions", "No browsable APA storage found");
	}
#ifdef EXFAT
	if (filerShowAtaUnit(0) && !console_is_PSX) {
		addStorage(out, &n, max, DI_HDD, "exFAT HDD", "ata0:", "ata0:/", "Ready", "No exFAT storage found");
	}
#endif

	{
		int has_disc = (cdmode > 0 && uLE_cdmode != SCECdNODISC);
		add(out, &n, max, DI_DISC, has_disc ? DASH_DEV_READY : DASH_DEV_ABSENT, "Disc Drive", "cdfs:", "cdfs:/", dashDiscStatus());
	}

#ifdef UDPFS
	addStorage(out, &n, max, DI_NETWORK, "Network (UDPFS)", "udpfs:", "udpfs:/", netConfig[0] ? netConfig : "Connected", "UDPFS server not found");
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
	if (!dashCfg.show_unavailable_devices) {
		int write = 0;
		for (i = 0; i < n; i++)
			if (out[i].state == DASH_DEV_READY) {
				if (write != i)
					out[write] = out[i];
				write++;
			}
		n = write;
	}
	return n;
}

static int probeRoot(const char *path, int wait_ms)
{
	u64 deadline = Timer() + wait_ms;
	while (1) {
		int dd = fileXioDopen(path);
		if (dd >= 0) {
			fileXioDclose(dd);
			return 1;
		}
		if (Timer() >= deadline)
			return 0;
		/* BDM mounts can arrive after the driver returns. Yield during the
		 * existing first-open settling window instead of reporting absence. */
		DelayThread(100000);
	}
}

static void syncUsbProbeFailures(void)
{
	unsigned int gen = getIopResetGeneration();

	if (usb_probe_generation != gen) {
		usb_probe_generation = gen;
		usb_probe_failures = 0;
	}
}

static int probeUsbMediaAnimated(void)
{
	int attempt, mask;

	for (attempt = 0; attempt < DASH_USB_PROBE_ATTEMPTS; attempt++) {
		mask = filerUsbRefreshDriveMask();
		if (mask)
			return 1;
		if (attempt == DASH_USB_PROBE_ATTEMPTS - 1)
			break;

		/* Keep the dashboard/background/spinner alive while USB enumeration
		 * settles instead of burning a second in a tight discovery loop. */
		if (dashLoadingActive())
			dashLoadingFrame("Waiting for USB storage...", attempt + 1, DASH_USB_PROBE_ATTEMPTS);
		DelayThread(DASH_USB_PROBE_DELAY_US);
	}
	return 0;
}

static int probeMount(const char *m)
{
	int ok = 1;

	filerRootMcPollIdle();
	if (!strncmp(m, "usb", 3)) {
		int stack_ok;

		syncUsbProbeFailures();
		loadUsbModules();
		stack_ok = isUsbMassStackLoaded();

		/* A module-start failure is an actual broken USB stack, not merely an
		 * unplugged drive. Recover that immediately when it is safe to do so. */
		if (!stack_ok && dashBootDeviceBit() != DASH_DEVA_USB) {
			DPRINTF(" [USB]: storage stack failed to start; rebuilding IOP once\n");
			rebootIopAndReloadCoreStackSilent();
			syncUsbProbeFailures();
			loadUsbModules();
			stack_ok = isUsbMassStackLoaded();
		}

		ok = stack_ok && probeUsbMediaAnimated();
		if (ok) {
			usb_probe_failures = 0;
		} else {
			usb_probe_failures++;
			/*
			 * Do not reboot the whole IOP merely because an empty USB port was
			 * checked. Hardware has also shown a rarer state where the modules are
			 * resident but enumeration stays wedged until reset; a second failed
			 * activation is strong enough evidence to use the recovery path.
			 */
			if (stack_ok && usb_probe_failures >= 2 && dashBootDeviceBit() != DASH_DEVA_USB) {
				DPRINTF(" [USB]: repeated enumeration failure; rebuilding IOP and retrying once\n");
				rebootIopAndReloadCoreStackSilent();
				syncUsbProbeFailures();
				loadUsbModules();
				ok = isUsbMassStackLoaded() && probeUsbMediaAnimated();
				usb_probe_failures = ok ? 0 : 1;
				DPRINTF(" [USB]: recovery probe %s\n", ok ? "succeeded" : "failed");
			}
		}
	} else if (!strncmp(m, "hdd", 3)) {
		ok = loadHddModules();
		if (ok) ok = probeRoot("hdd0:", 0);
	}
#ifdef EXFAT
	else if (!strncmp(m, "ata", 3)) {
		ok = loadAtaModules();
		if (ok) ok = probeRoot("ata0:/", 2000);
	}
#endif
#ifdef MMCE
	else if (!strncmp(m, "mmce", 4)) {
		ok = loadMmceModules();
		if (ok) ok = probeRoot(m[4] == '1' ? "mmce1:/" : "mmce0:/", 0);
	}
#endif
#ifdef MX4SIO
	else if (!strncmp(m, "mx4sio", 6)) {
		ok = loadMx4sioModules();
		if (ok) ok = probeRoot("mx4sio:/", 2000);
	}
#endif
#ifdef ILINK
	else if (!strncmp(m, "ilink", 5)) {
		ok = loadIlinkModules();
		if (ok) ok = probeRoot("ilink:/", 3000);
	}
#endif
#ifdef UDPFS
	else if (!strncmp(m, "udpfs", 5)) {
		ok = load_udpfs();
		if (ok) ok = probeRoot("udpfs:/", 0);
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
	recordProbe(m, ok);
	return ok;
}

//--------------------------------------------------------------
// Starting drivers. This runs on the UI thread behind a loading overlay:
// loading IOP modules from the background worker hung on real hardware
// (the worker only does file IO, like OPL's IO thread). Whatever the
// worker is doing finishes first, so the two never overlap.
//--------------------------------------------------------------
static int safeAutoStartMask(void)
{
	int mask = dashCfg.dev_auto;

	/* Defense in depth: these two are mutually exclusive even if some
	 * caller bypassed settings validation or an old/corrupt config survived.
	 * Never let the automatic startup walker switch between them mid-pass. */
	if ((mask & (DASH_DEVA_MMCE | DASH_DEVA_MX4SIO)) ==
	    (DASH_DEVA_MMCE | DASH_DEVA_MX4SIO)) {
		if (dashBootDeviceBit() == DASH_DEVA_MX4SIO)
			mask &= ~DASH_DEVA_MMCE;
		else
			mask &= ~DASH_DEVA_MX4SIO;
	}
	return mask;
}

static int autoStartNeeded(void)
{
	const int mask = safeAutoStartMask();

	return ((mask & DASH_DEVA_USB) && !isUsbMassStackLoaded()) ||
	       ((mask & DASH_DEVA_HDD) && !isPathDeviceStackLoaded("hdd0:"))
#ifdef EXFAT
	       || ((mask & DASH_DEVA_ATA) && !isPathDeviceStackLoaded("ata0:"))
#endif
#ifdef MMCE
	       || ((mask & DASH_DEVA_MMCE) && !isPathDeviceStackLoaded("mmce0:"))
#endif
#ifdef MX4SIO
	       || ((mask & DASH_DEVA_MX4SIO) && !isPathDeviceStackLoaded("mx4sio:"))
#endif
#ifdef ILINK
	       || ((mask & DASH_DEVA_ILINK) && !isPathDeviceStackLoaded("ilink:"))
#endif
#ifdef UDPFS
	       || ((mask & DASH_DEVA_UDPFS) && !isPathDeviceStackLoaded("udpfs:"))
#endif
	    ;
}

/* -1: the user backed out while background work finished */
static int startSync(const char *mount, const char *title)
{
	int ok;

	if (!dashWorkerAwaitAll("Finishing up"))
		return -1;
	dashLoadingBegin(title);
	dashLoadingStatus("Starting device drivers...", 0, 0);
	ok = probeMount(mount);
	dashLoadingEnd();
	return ok;
}

int dashDevicesStart(const DashDevice *dev, DashProbeDone cb, void *user)
{
	char title[64];
	int ok;

	snprintf(title, sizeof(title), "Starting %s", dev->name);
	ok = startSync(dev->mount, title);
	if (ok < 0)
		return -1;
	/* A newly activated device may already have applications in DASHLIB.CNF.
	 * Reveal them immediately without forcing a rescan. */
	dashAppsRebuildView(); /* failed checks must also hide stale cached apps */
	if (cb != NULL)
		cb(dev->mount, ok, user);
	return 0;
}

void dashDevicesAutoStart(void)
{
	if (!autoStartNeeded() || !dashWorkerAwaitAll("Finishing up"))
		return;
	dashLoadingBegin("Starting devices");
	dashDevicesAutoProbe();
	dashLoadingEnd();
	dashAppsRebuildView();
}

/* Starts the driver for the device a path lives on, if it is not running
 * (used before a full library refresh, which only reads running devices). */
void dashDevicesStartForPath(const char *path)
{
	const char *mount = NULL;

	if (path == NULL || !strncmp(path, "mc", 2) || isPathDeviceStackLoaded(path))
		return;
	if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3))
		mount = "usb:";
	else if (!strncmp(path, "hdd", 3))
		mount = "hdd0:";
#ifdef EXFAT
	else if (!strncmp(path, "ata", 3))
		mount = "ata0:";
#endif
#ifdef MMCE
	else if (!strncmp(path, "mmce", 4))
		mount = "mmce0:";
#endif
#ifdef MX4SIO
	else if (!strncmp(path, "mx4sio", 6))
		mount = "mx4sio:";
#endif
#ifdef ILINK
	else if (!strncmp(path, "ilink", 5))
		mount = "ilink:";
#endif
#ifdef UDPFS
	else if (!strncmp(path, "udpfs", 5))
		mount = "udpfs:";
#endif
	if (mount != NULL)
		startSync(mount, "Starting devices");
}

/* Load the drivers for every device family marked auto-start that is not
 * already running. Never unloads anything: "off" only means "not at
 * startup" — the DEVICES screen can still start a device by hand. */
void dashDevicesAutoProbe(void)
{
	const int mask = safeAutoStartMask();

	if ((mask & DASH_DEVA_USB) && !isUsbMassStackLoaded())
		probeMount("usb:");
	if ((mask & DASH_DEVA_HDD) && !isPathDeviceStackLoaded("hdd0:"))
		probeMount("hdd0:");
#ifdef EXFAT
	if ((mask & DASH_DEVA_ATA) && !isPathDeviceStackLoaded("ata0:"))
		probeMount("ata0:");
#endif
#ifdef MMCE
	if ((mask & DASH_DEVA_MMCE) && !isPathDeviceStackLoaded("mmce0:"))
		probeMount("mmce0:");
#endif
#ifdef MX4SIO
	if ((mask & DASH_DEVA_MX4SIO) && !isPathDeviceStackLoaded("mx4sio:"))
		probeMount("mx4sio:");
#endif
#ifdef ILINK
	if ((mask & DASH_DEVA_ILINK) && !isPathDeviceStackLoaded("ilink:"))
		probeMount("ilink:");
#endif
#ifdef UDPFS
	if ((mask & DASH_DEVA_UDPFS) && !isPathDeviceStackLoaded("udpfs:"))
		probeMount("udpfs:");
#endif
}

/* The DASH_DEVA_* bit of the device the ELF was launched from, 0 when it
 * has no toggle (memory cards, host) or cannot be determined. The boot
 * device is always available: its driver is already running. */
int dashBootDeviceBit(void)
{
	const char *p = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;

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
	if (!strncmp(p, "ilink", 5))
		return DASH_DEVA_ILINK;
	return 0;
}
//--------------------------------------------------------------
//End of file: dash_devices.c
//--------------------------------------------------------------
