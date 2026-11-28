//--------------------------------------------------------------
//File name:   dash_hdd.c
//The HDD Manager. Every way in (Tools, Devices, launch keys, MISC/HddManager)
//opens this screen; wLaunchELF's classic HddManager screen is gone.
//
//* The partition list is read by the background worker; PFS usage is
//  probed while browsing. HDL titles load only on explicit R1/details
//  or when the user explicitly requests a game rename.
//* Create / remove / rename / rename game / format / header injection
//  run on the worker behind an overlay that cannot be backed out of
//  (they must not be interrupted halfway).
//* IOP modules (HDD stack, HDL info, header source devices) are only
//  loaded on the UI thread.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "filer_size_cache.h"
#include "filer_shared.h"
#include "dash_sound.h"
#include "hdd_header_injector.h"
#include "hdd_filesystem.h"
#include "hdd_app.h"
#include "hdd_format.h"
#include "hdd_partition_size.h"
#include "hdd_install.h"
#include "hdd_install_recovery.h"
#include "hdd_install_resume.h"
#include <time.h>
#include "gui_sort.h"
#include "hdl_rpc.h"
#include "init.h"

#define SECTORS_PER_MB 2048
#define HDD_MB 1048576
#define MAX_PART_MB (128 * 1024) /* 128 GB, as wLaunchELF's HddManager */

/* HDIOC_STATUS codes and what they mean */
typedef struct
{
	int Code;
	const char *ShortText;
	const char *InfoText;
} HDDSTATUSINFO;

static const HDDSTATUSINFO HddStatusInfo[] = {
	{3, "NO HDD", "No suitable HDD was detected by the HDD driver.\nCheck the drive, adapter, power, DEV9, or ATA stack."},
	{2, "LOCKED", "The HDD was detected, but the driver could not\ncomplete the unlock/usability check."},
	{1, "NO APA", "The HDD is usable, but no APA format was found.\nThe drive may use another filesystem."},
	{0, "READY", "The HDD is detected, usable, and APA formatted."},
	{-1, "EPERM", "Operation not permitted.\nThe HDD driver refused the status operation."},
	{-2, "ENOENT", "The HDD device/path was not found.\nhdd0: may not be registered by the driver."},
	{-3, "ESRCH", "A required service or RPC target was not found.\nHDD modules may not be running correctly."},
	{-5, "EIO", "I/O error while querying the HDD.\nCheck the drive, adapter, cabling, and power."},
	{-6, "ENXIO", "No such device or address.\nThe HDD hardware may be missing or unreachable."},
	{-11, "EAGAIN", "The HDD resource is temporarily unavailable.\nRetry after the HDD stack settles."},
	{-12, "ENOMEM", "Out of memory while querying HDD status.\nSystem or module memory may be exhausted."},
	{-13, "EACCES", "Access denied while querying HDD status.\nThe device or driver state refused access."},
	{-16, "EBUSY", "The HDD/device resource is busy.\nAnother operation or mount may be holding it."},
	{-19, "ENODEV", "No such device.\nHDD driver/device registration may be missing."},
};

static const HDDSTATUSINFO *GetHddStatusInfo(int status)
{
	int i;

	for (i = 0; i < (int)(sizeof(HddStatusInfo) / sizeof(HddStatusInfo[0])); i++) {
		if (HddStatusInfo[i].Code == status)
			return &HddStatusInfo[i];
	}

	return NULL;
}

static const char *hddStatusShortText(int status)
{
	const HDDSTATUSINFO *info = GetHddStatusInfo(status);

	return (info != NULL) ? info->ShortText : "UNKNOWN";
}

static const char *hddStatusInfoText(int status)
{
	const HDDSTATUSINFO *info = GetHddStatusInfo(status);

	return (info != NULL) ? info->InfoText : "No specific HDD status description is available for this code.";
}


enum {
	HP_PFS = 0, /* PFS partition: normal file access */
	HP_HDL,     /* HDL game partition */
	HP_SYSTEM,  /* __mbr: never touched */
	HP_OTHER    /* anything else: not accessible */
};

typedef struct
{
	char name[MAX_PART_NAME + 1];
	u32 raw_mb;
	int nsub;      /* extra segments (stat.private_0) */
	int type;
	int pfs_state; /* 0 unknown/retry, 1 loaded, 2 loading, -1 failed */
	int pfs_error;
	u64 pfs_retry_after;
	u32 pfs_total, pfs_free;
	int hdl_state; /* 0 unknown, 1 loaded, -1 failed */
	GameInfo game;
} HddPart;

static struct
{
	int status;
	int present, formatted;
	unsigned filesystem;
	int filesystem_error;
	u32 size_mb, apa_mb, used_mb, free_mb;
	int scanned, scanning, overflow;
	int resume_checked, resume_available;
	HddPart *parts;
	int n;
	int changed; /* partitions were modified: file browser caches refresh */
} hm;

//--------------------------------------------------------------
// Background jobs
//--------------------------------------------------------------
enum {
	HOP_SCAN = 0,
	HOP_PFS,
	HOP_CREATE,
	HOP_REMOVE,
	HOP_RENAME,
	HOP_RENAME_GAME,
	HOP_FORMAT,
	HOP_INJECT,
	HOP_LIST_HEADERS,
	HOP_UNMOUNT,
	HOP_DOWNLOAD_INSTALLER,
	HOP_INSTALL_SOFTWARE,
	HOP_RESUME_LOOKUP,
	HOP_RESUME_AVAILABLE,
	HOP_SYSTEM_APP
};

typedef struct
{
	int op;
	char a[MAX_PART_NAME + 8];
	char b[80];
	int size;
	int nsub;
	int ret;
	/* scan */
	HddPart *parts;
	int n, overflow, status, present, formatted;
	unsigned filesystem;
	int filesystem_error;
	u32 size_mb, apa_mb, used_mb;
	HddFormatPlan format_plan;
	HddInstall *installer;
	int format_started;
	unsigned resume_sequence;
	/* pfs */
	u32 pfs_total, pfs_free;
	/* inject */
	char source_dir[MAX_PATH];
	int pfs_copied, pfs_error;
	/* header list */
	HddHeaderSourcePartition *sources;
	int nsources, invalid;
	int abandoned;
} HddJob;

/* Match File Browser: retain up to MOUNT_LIMIT partitions, reuse the first
 * eligible slot, protect active/VMC mounts and reserve scan headroom as needed.
 * Cached usage survives eviction, so revisiting needs no remount. */

/* A resident FTP service consumes APA handle capacity. Keep four mounts
 * without FTP, and reserve a handle while the service is running. */
static int pfsRetainedLimit(void)
{
#ifdef ETH
	return isFtpServerRunning() ? MOUNT_LIMIT - 1 : MOUNT_LIMIT;
#else
	return MOUNT_LIMIT;
#endif
}

static int pfsMakeRoom(int limit)
{
	unsigned tried = 0;
	int i, count, first;

	for (;;) {
		count = 0;
		first = -1;
		for (i = 0; i < MOUNT_LIMIT; i++) {
			if (mountedParty[i][0] == 0)
				continue;
			count++;
			if (i == latestMount || Party_vmcIndex[i] >= 0 || (tried & (1u << i)))
				continue;
			if (first < 0)
				first = i;
		}
		if (count <= limit)
			return 0;
		if (first < 0)
			return -EBUSY;
		tried |= 1u << first;
		unmountParty(first);
		/* A failed unmount retains its bookkeeping; try another eligible mount. */
	}
}

static int pfsMountForInfo(const char *dev)
{
	int i, ret;
	char pfs[16];

	for (i = 0; i < MOUNT_LIMIT; i++) {
		if (!strcmp(mountedParty[i], dev)) {
			latestMount = i;
			return i;
		}
	}
	ret = pfsMakeRoom(pfsRetainedLimit() - 1);
	if (ret < 0)
		return ret;
	for (i = 0; i < MOUNT_LIMIT; i++) {
		if (mountedParty[i][0] == 0)
			break;
	}
	if (i == MOUNT_LIMIT)
		return -EBUSY;
	snprintf(pfs, sizeof(pfs), "pfs%d:", i);
	/* Keep File Browser bookkeeping and access mode, but do not clear other
	 * mounts when the target itself is invalid or cannot be mounted. */
	ret = fileXioMount(pfs, dev, FIO_MT_RDWR);
	if (ret < 0)
		return ret;
	dashStrCopy(mountedParty[i], dev, sizeof(mountedParty[i]));
	latestMount = i;
	return i;
}

static int hddReadFormatSector(void *context, uint32_t lba, void *sector)
{
	hddAtaTransfer_t transfer __attribute__((aligned(64)));

	memset(&transfer, 0, sizeof(transfer));
	transfer.lba = lba;
	transfer.size = 1;
	return fileXioDevctl((const char *)context, HDIOC_READSECTOR, &transfer,
	                     sizeof(transfer), sector, 512);
}

static void scanRun(HddJob *j)
{
	iox_dirent_t de;
	int fd, i;

	j->status = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
	j->present = (hddCheckPresent() >= 0);
	if (!j->present)
		return;
	j->formatted = (hddCheckFormatted() >= 0);
	if (j->formatted)
		j->filesystem = HDD_FS_APA;
	if (j->status == 0 || j->status == 1) {
		int sectors = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
		/* The capacity is an unsigned sector count carried in a signed RPC
		 * result. Preserve capacities above 1 TiB without treating errno as size. */
		if (sectors > 0 || sectors < -4095) {
			unsigned additional = HDD_FS_UNKNOWN;

			j->size_mb = (u32)sectors / SECTORS_PER_MB;
			if (j->formatted) {
				uint32_t limit = 0;
				if (hddApaLimit(hddReadFormatSector, "hdd0:", (u32)sectors, &limit) == 0)
					j->apa_mb = limit / SECTORS_PER_MB;
			}
			/* APA Jail can contain both APA and FAT/exFAT metadata. Check
			 * independently and retain the driver-confirmed APA result. */
			j->filesystem_error = hddProbeFilesystem(hddReadFormatSector, "hdd0:", (u32)sectors, &additional);
			if (j->filesystem_error == 0)
				j->filesystem |= additional;
		} else
			j->filesystem_error = sectors < 0 ? sectors : -EIO;
	}
	if (!j->formatted)
		return;
	j->parts = (HddPart *)malloc(sizeof(HddPart) * MAX_PARTITIONS);
	if (j->parts == NULL) {
		j->ret = -ENOMEM;
		return;
	}
	j->ret = pfsMakeRoom(pfsRetainedLimit() - 1);
	if (j->ret < 0)
		return;
	fd = fileXioDopen("hdd0:");
	if (fd < 0) {
		j->ret = fd;
		return;
	}
	while (fileXioDread(fd, &de) > 0) {
		u32 mb = de.stat.size / SECTORS_PER_MB;

		if (de.stat.mode == APA_TYPE_FREE)
			continue;
		j->used_mb += mb;
		for (i = 0; i < j->n; i++)
			if (!strcmp(de.name, j->parts[i].name))
				break;
		if (i < j->n) {
			/* another segment of a partition seen before */
			j->parts[i].raw_mb += mb;
			continue;
		}
		if (j->n >= MAX_PARTITIONS) {
			j->overflow = 1;
			break;
		}
		memset(&j->parts[j->n], 0, sizeof(HddPart));
		snprintf(j->parts[j->n].name, sizeof(j->parts[j->n].name), "%.32s", de.name);
		j->parts[j->n].raw_mb = mb;
		j->parts[j->n].nsub = de.stat.private_0;
		if (de.stat.mode == APA_TYPE_MBR)
			j->parts[j->n].type = HP_SYSTEM;
		else if (de.stat.mode == 0x1337)
			j->parts[j->n].type = HP_HDL;
		else if (de.stat.mode == APA_TYPE_PFS)
			j->parts[j->n].type = HP_PFS;
		else
			j->parts[j->n].type = HP_OTHER;
		j->n++;
		if ((j->n & 31) == 0) {
			char s[48];
			snprintf(s, sizeof(s), "%d partitions", j->n);
			dashWorkerStatus(s, 0, 0);
		}
	}
	fileXioDclose(fd);
}

static void pfsRun(HddJob *j)
{
	char dev[48], pfs[16];
	int idx, zfree, zsize;

	snprintf(dev, sizeof(dev), "hdd0:%s", j->a);
	idx = pfsMountForInfo(dev);
	if (idx < 0) {
		j->ret = idx;
		return;
	}
	snprintf(pfs, sizeof(pfs), "pfs%d:", idx);
	zfree = fileXioDevctl(pfs, PDIOC_ZONEFREE, NULL, 0, NULL, 0);
	zsize = fileXioDevctl(pfs, PDIOC_ZONESZ, NULL, 0, NULL, 0);
	if (zfree < 0 || zsize <= 0) {
		j->ret = zfree < 0 ? zfree : (zsize < 0 ? zsize : -EIO);
		return;
	}
	j->pfs_free = (u32)((u64)(u32)zfree * (u32)zsize / HDD_MB);
	/* Total APA size already includes every segment from the scan. Opening
	 * the raw partition again would fail if PFS already has it mounted. */
	j->ret = 0;
}

/* Menu probes reuse a retained mount or borrow a free slot without evicting
 * an unrelated partition. Keep a failed unmount visible to the mount manager. */
static int pfsProbeMount(const char *partition, char device[16], int *temporary)
{
    int free_slot = -1;
    *temporary = -1;
    for (int i = 0; i < MOUNT_LIMIT; i++) {
        if (!strcmp(mountedParty[i], partition)) {
            snprintf(device, 16, "pfs%d:", i);
            return 0;
        }
        if (free_slot < 0 && !mountedParty[i][0]) free_slot = i;
    }
    if (free_slot < 0) return -EBUSY;
    snprintf(device, 16, "pfs%d:", free_slot);
    int ret = fileXioMount(device, partition, FIO_MT_RDONLY);
    if (ret < 0) return ret;
    dashStrCopy(mountedParty[free_slot], partition, sizeof(mountedParty[free_slot]));
    *temporary = free_slot;
    return 0;
}

static int pfsProbeClose(int temporary, int result)
{
    if (temporary >= 0 && !hddInstallRecoveryFault()) {
        unmountParty(temporary);
        if (mountedParty[temporary][0] && result >= 0) return -EBUSY;
    }
    return result;
}

static void systemAppRun(HddJob *j)
{
    iox_stat_t stat;
    char device[16], path[64];
    int temporary;
    int ret = pfsProbeMount("hdd0:__system", device, &temporary);

    if (ret < 0) { j->ret = ret; return; }
    snprintf(path, sizeof(path), "%s/osdmenu/hosdmenu.elf", device);
    ret = fileXioGetStat(path, &stat);
    j->ret = pfsProbeClose(temporary, ret >= 0 ? !!FIO_S_ISREG(stat.mode) : ret == -ENOENT ? 0 : ret);
}

static void releaseMounts(void)
{
	unmountAll();
	unmountParty(0);
	unmountParty(1);
}

/* Renaming needs only the target volume closed. Retain other mounts and
 * refuse a partition pinned by a VMC rather than disconnecting the card. */
static int unmountForRename(const char *partition)
{
	for (int i = 0; i < MOUNT_LIMIT; i++) {
		if (strcmp(mountedParty[i], partition)) continue;
		if (Party_vmcIndex[i] >= 0) return -EBUSY;
		unmountParty(i);
		if (mountedParty[i][0]) return -EBUSY;
	}
	return 0;
}

static void hddJobRun(void *arg)
{
	HddJob *j = (HddJob *)arg;
	char in[48], out[48];

	if (j->op == HOP_CREATE || j->op == HOP_REMOVE || j->op == HOP_RENAME ||
	    j->op == HOP_FORMAT || j->op == HOP_INJECT)
		filerSizeCacheInvalidate("hdd0:/");
	switch (j->op) {
		case HOP_SCAN:
			scanRun(j);
			break;
		case HOP_PFS:
			pfsRun(j);
			break;
		case HOP_CREATE:
			releaseMounts();
			j->ret = hddMakeFilesystem(j->size, j->a, FS_GROUP_APPLICATION);
			break;
		case HOP_REMOVE:
			releaseMounts();
			snprintf(in, sizeof(in), "hdd0:%s", j->a);
			j->ret = fileXioRemove(in);
			break;
		case HOP_RENAME:
			snprintf(in, sizeof(in), "hdd0:%.32s", j->a);
			snprintf(out, sizeof(out), "hdd0:%.32s", j->b);
			j->ret = unmountForRename(in);
			if (j->ret >= 0) j->ret = fileXioRename(in, out);
			break;
		case HOP_RENAME_GAME:
			j->ret = HdlRenameGame(j->a, j->b);
			break;
		case HOP_FORMAT:
			{
				int i;
				for (i = 0; i < 2; i++) {
					if (vmcMounted[i] && (j->ret = filerUnmountVmc(i)) < 0)
						return;
				}
				/* Refuse a failed PFS unmount before any raw disk writes. */
				for (i = 0; i < MOUNT_LIMIT; i++) {
					unmountParty(i);
					if (mountedParty[i][0]) { j->ret = -EBUSY; return; }
				}
				j->ret = hddFormatDisk(&j->format_plan, &j->format_started);
			}
			break;
		case HOP_INJECT:
			releaseMounts();
			j->ret = InjectHddPartitionHeaderFromSource(j->a, j->b, j->source_dir, sizeof(j->source_dir),
			                                            &j->pfs_copied, &j->pfs_error);
			break;
		case HOP_LIST_HEADERS:
			j->sources = (HddHeaderSourcePartition *)malloc(sizeof(HddHeaderSourcePartition) * MAX_PARTITIONS);
			j->nsources = j->sources ? HddHeaderListSourcePartitions(j->b, j->sources, MAX_PARTITIONS, &j->invalid) : -ENOMEM;
			break;
		case HOP_SYSTEM_APP:
			systemAppRun(j);
			break;
		case HOP_RESUME_AVAILABLE: {
			char device[16] = "ata0:";
			int temporary = -1;
			int ret = j->nsub ? 0 : pfsProbeMount("hdd0:__common", device, &temporary);
			if (ret >= 0) ret = hddInstallResumeAvailable(j->installer, device, j->nsub);
			j->ret = pfsProbeClose(temporary, ret);
			break;
		}
		case HOP_RESUME_LOOKUP:
			j->ret = hddInstallResumeLoad(j->installer, &j->format_plan, &j->resume_sequence);
			break;
		case HOP_DOWNLOAD_INSTALLER:
			hddInstallLog("JOB download: mounting temporary volume");
			j->ret = hddInstallMountStage(j->installer);
			if (j->ret >= 0) {
				j->ret = j->installer->stage_ready ? hddInstallFindStage(j->installer) : hddInstallCreateStage(j->installer);
				if (j->ret >= 0) j->ret = hddInstallDownload(j->installer);
				hddInstallLog("JOB download finished rc=%d", j->ret);
				hddInstallLogFlush(j->installer);
				{ int closed = hddInstallUnmountStage(j->installer); if (j->ret >= 0) j->ret = closed; }
			}
			break;
		case HOP_INSTALL_SOFTWARE:
			j->installer->apa_sectors = j->installer->jail ? j->format_plan.apa_sectors : j->format_plan.sectors;
			hddInstallLog("JOB install: verifying staged files and installing software");
			filerSizeCacheInvalidate("hdd0:/");
			filerSizeCacheInvalidate("ata0:/");
			j->ret = j->installer->applied ? 0 : hddInstallApply(j->installer);
			if (j->ret < 0) break;
			j->installer->applied = 1;
			j->ret = hddInstallMountStage(j->installer);
			if (j->ret < 0) break;
			if (j->ret >= 0 && !j->installer->cleanup_pending) j->ret = hddInstallFindStage(j->installer);
			if (j->ret >= 0 && !j->installer->committed) j->ret = hddInstallResumeSave(j->installer);
			if (j->ret >= 0) {
				char mbr[sizeof(j->installer->stage) + sizeof("/OSDMBR.XLF")];
				const char *payload = NULL;
				if (j->installer->flags & HDD_INSTALL_HOSD) {
					snprintf(mbr, sizeof(mbr), "%s/OSDMBR.XLF", j->installer->stage);
					payload = mbr;
				}
				j->ret = j->installer->committed ? 0 : hddInstallFinish(&j->format_plan, payload,
				                     j->installer->hashes[HI_MBR], &j->format_started,
				                     (j->installer->flags & HDD_INSTALL_PSBBN) && !j->installer->populated ? hddInstallPsbbnPayload : NULL, j->installer);
				if (j->ret >= 0 && !j->installer->committed) {
					j->ret = hddInstallEnableBoot(j->installer);
					if (j->ret >= 0) j->installer->committed = 1;
				}
				if (j->ret >= 0 && !j->installer->cleanup_pending) j->ret = hddInstallResumeSave(j->installer);
				hddInstallLog("JOB install finished rc=%d raw-started=%d", j->ret, j->format_started);
				hddInstallLogFlush(j->installer);
				if (j->ret >= 0) j->ret = hddInstallCleanup(j->installer);
			}
			{ int closed = hddInstallUnmountStage(j->installer); if (j->ret >= 0) j->ret = closed; }
			break;
		case HOP_UNMOUNT:
			releaseMounts();
			break;
	}
}

static void hddJobFree(HddJob *j)
{
	free(j->parts);
	free(j->sources);
	free(j);
}

static void hddJobDone(void *arg)
{
	HddJob *j = (HddJob *)arg;

	if (j->abandoned)
		hddJobFree(j);
}

static HddJob *jobNew(int op)
{
	HddJob *j = (HddJob *)malloc(sizeof(HddJob));

	if (j != NULL) {
		memset(j, 0, sizeof(*j));
		j->op = op;
	}
	return j;
}

/* Runs `j` and waits. 1 = finished (caller owns j); 0 = the user backed
 * out (cancelable only) or it could not be queued (j freed or abandoned). */
static int runJob(HddJob *j, const char *title, int cancelable)
{
	int probing = j->op == HOP_RESUME_LOOKUP || j->op == HOP_RESUME_AVAILABLE;
	int recoverable = j->installer && (probing || j->op == HOP_DOWNLOAD_INSTALLER || j->op == HOP_INSTALL_SOFTWARE);
	int owns_loading = recoverable && !dashLoadingActive();
	if (owns_loading) dashLoadingBegin(title);
	for (;;) {
		int fault, was_reset;
		if (recoverable) hddInstallRecoveryBegin();
		int id = dashWorkerSubmit(title, DASH_JOB_VISIBLE, hddJobRun, hddJobDone, j);
		if (id < 0) {
			if (recoverable) hddInstallRecoveryEnd();
			dashToast(DASH_TOAST_WARN, "Busy, try again in a moment");
			hddJobFree(j);
			if (owns_loading) dashLoadingEnd();
			return 0;
		}
		if (!(recoverable || (j->installer && (j->installer->flags & HDD_INSTALL_PSBBN)) ?
		      dashHddInstallAwait(id, title) : dashWorkerAwait(id, title, cancelable))) {
			j->abandoned = 1; return 0;
		}
		if (!recoverable) return 1;
		hddInstallRecoveryObserve(j->ret);
		fault = hddInstallRecoveryFault(); was_reset = hddInstallRecoveryWasReset();
		hddInstallRecoveryEnd();
		if (!fault) { if (owns_loading) dashLoadingEnd(); return 1; }
		hddInstallLog("ATA timeout: recovering %s (completed %u retries)", title, j->installer->recovery_attempts);
		hddInstallProgress("Recovering storage and controller", 0, 0);
		/* The worker has unwound. No old file/RPC buffers may be in use now. */
		j->format_started = 0;
		if ((!was_reset && !resetInstallerIopTransport()) || !reloadInstallerCoreAfterReset())
			dashHddInstallRecoveryFailed();
		if (probing) { j->ret = HDD_INSTALL_ATA_TIMEOUT; if (owns_loading) dashLoadingEnd(); return 1; }
		j->ret = dashHddInstallPrepareStorage(j->installer);
		if (j->ret < 0) {
			j->installer->stage_ready = 0;
			snprintf(j->installer->error, sizeof(j->installer->error), "Could not reload storage after the ATA timeout (error %d). Cached downloads are retained.", j->ret);
			if (owns_loading) dashLoadingEnd();
			return 1;
		}
		if (j->installer->recovery_attempts >= HDD_INSTALL_RECOVERY_LIMIT) {
			j->ret = HDD_INSTALL_ATA_TIMEOUT;
			snprintf(j->installer->error, sizeof(j->installer->error), "ATA timeout persisted after %u retries. Installation is incomplete; cached downloads are retained.", HDD_INSTALL_RECOVERY_LIMIT);
			if (owns_loading) dashLoadingEnd();
			return 1;
		}
		j->installer->recovery_attempts++;
		if (j->op == HOP_INSTALL_SOFTWARE) j->ret = hddFormatLoad();
		if (j->ret < 0) { if (owns_loading) dashLoadingEnd(); return 1; }
		j->installer->error[0] = 0;
		hddInstallLog("RECOVERY retry %u/%u: %s; downloaded files and completed stages retained",
		              j->installer->recovery_attempts, HDD_INSTALL_RECOVERY_LIMIT, title);
	}
}

//--------------------------------------------------------------
// Model
//--------------------------------------------------------------
static void recomputeFree(void)
{
	hm.free_mb = (hm.apa_mb > hm.used_mb) ? ((hm.apa_mb - hm.used_mb) & 0x7FFFFFF8) : 0;
}

static int partCmp(const void *a, const void *b)
{
	const HddPart *x = (const HddPart *)a, *y = (const HddPart *)b;

	static const int order[] = {0, 1, 2, 3}; /* PFS, HDL, system, other */
	int xr = hddPartitionPriority(x->name);
	int yr = hddPartitionPriority(y->name);

	if (xr != yr)
		return xr - yr;
	if (order[x->type] != order[y->type])
		return order[x->type] - order[y->type];
	return stricmp(x->name, y->name);
}

/* 1 = scanned (or the user can still look at what is known) */
static int scan(void)
{
	HddJob *j = jobNew(HOP_SCAN);

	if (j == NULL)
		return 0;
	if (!runJob(j, "Reading the HDD", 1))
		return 0;
	free(hm.parts);
	hm.parts = j->parts;
	j->parts = NULL;
	hm.n = hm.parts ? j->n : 0;
	hm.status = j->status;
	hm.present = j->present;
	hm.formatted = j->formatted;
	hm.filesystem = j->filesystem;
	hm.filesystem_error = j->filesystem_error;
	hm.size_mb = j->size_mb;
	hm.apa_mb = j->apa_mb;
	hm.used_mb = j->used_mb;
	hm.overflow = j->overflow;
	hm.scanned = 1;
	hm.resume_checked = hm.resume_available = 0;
	recomputeFree();
	if (hm.n > 1)
		qsort(hm.parts, hm.n, sizeof(HddPart), partCmp);
	if (j->ret < 0 && hm.formatted)
		dashToast(DASH_TOAST_ERROR, "The partition list could not be read");
	hddJobFree(j);
	return 1;
}

static int findPart(const char *name)
{
	int i;

	for (i = 0; i < hm.n; i++)
		if (!strcmp(hm.parts[i].name, name))
			return i;
	return -1;
}

/* name, or name1, name2... when it is taken (like the classic manager) */
static void uniqueName(const char *want, char *out, int size, int skip_index)
{
	int num = 1, i, clash;

	dashStrCopy(out, want, size);
	do {
		clash = 0;
		for (i = 0; i < hm.n; i++)
			if (i != skip_index && !strcmp(hm.parts[i].name, out))
				clash = 1;
		if (clash)
			snprintf(out, size, "%.28s%d", want, num++);
	} while (clash && num < 100);
}

extern int Hdl_Inited;

static int hdlReady(void)
{
	static int tried;

	if (Hdl_Inited)
		return 1;
	if (tried)
		return 0;
	/* module load: UI thread, nothing else running */
	if (!dashWorkerAwaitAll("Finishing up"))
		return 0;
	tried = 1;
	dashLoadingBegin("Loading game information");
	loadHdlInfoModule();
	dashLoadingEnd();
	return Hdl_Inited;
}

static void loadGameInfo(HddPart *p)
{
	if (p->type != HP_HDL || p->hdl_state != 0 || dashWorkerIoBusy())
		return;
	if (!hdlReady()) {
		p->hdl_state = -1;
		return;
	}
	/* HdlGetGameInfo shares one RPC buffer: UI thread only */
	p->hdl_state = (HdlGetGameInfo(p->name, &p->game) == 0) ? 1 : -1;
}

static HddJob *pfs_job;

static void pfsDone(void *arg)
{
	HddJob *j = (HddJob *)arg;
	int i;

	if (!j->abandoned && hm.parts != NULL) {
		i = findPart(j->a);
		if (i >= 0) {
			if (j->ret == 0) {
				hm.parts[i].pfs_state = 1;
				hm.parts[i].pfs_error = 0;
				hm.parts[i].pfs_retry_after = 0;
				hm.parts[i].pfs_total = j->pfs_total;
				hm.parts[i].pfs_free = j->pfs_free;
			} else {
				hm.parts[i].pfs_error = j->ret;
				if (j->ret == -EBUSY || j->ret == -EMFILE || j->ret == -EAGAIN) {
					hm.parts[i].pfs_state = 0;
					hm.parts[i].pfs_retry_after = Timer() + 1000;
				} else
					hm.parts[i].pfs_state = -1;
			}
		}
	}
	if (pfs_job == j)
		pfs_job = NULL;
	hddJobFree(j);
}

static void loadPfsInfo(int i)
{
	HddPart *p = &hm.parts[i];
	HddJob *j;

	if (p->type != HP_PFS || pfs_job != NULL || dashWorkerBusy())
		return;
	if (p->pfs_state == 1) {
		char dev[48];
		int slot;

		snprintf(dev, sizeof(dev), "hdd0:%s", p->name);
		for (slot = 0; slot < MOUNT_LIMIT; slot++) {
			if (!strcmp(mountedParty[slot], dev) && latestMount != slot) {
				latestMount = slot;
				break;
			}
		}
		return;
	}
	if (p->pfs_state != 0 || Timer() < p->pfs_retry_after)
		return;
	j = jobNew(HOP_PFS);
	if (j == NULL)
		return;
	dashStrCopy(j->a, p->name, sizeof(j->a));
	j->pfs_total = p->raw_mb;
	if (dashWorkerSubmit("Reading partition", 0, hddJobRun, pfsDone, j) < 0) {
		hddJobFree(j);
		return;
	}
	pfs_job = j;
	p->pfs_state = 2;
}

//--------------------------------------------------------------
// Size picker
//--------------------------------------------------------------
static int pickSizeMenu(const char *title, int min_mb, int max_mb, int start_mb, int partition)
{
	int size = start_mb;
	const float w = 600.0f, h = 244.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	float open = 0.0f;
	DashInput in;

	if (max_mb < min_mb)
		return -1;
	if (partition) {
		max_mb = hddPartitionSizeStep(max_mb, 0, min_mb, max_mb);
		size = hddPartitionSizeStep(start_mb, 0, min_mb, max_mb);
		if (size < 0) return -1;
	}
	dashInputFlush();
	while (1) {
		int step = 0;

		dashInputPoll(&in);
		if (dashDashboardPending())
			return -1;
		if (in.pressed & DASH_IN_RIGHT)
			step = 128;
		if (in.pressed & DASH_IN_LEFT)
			step = -128;
		if (in.pressed & DASH_IN_R1)
			step = 1024;
		if (in.pressed & DASH_IN_L1)
			step = -1024;
		if (in.pressed & DASH_IN_R2)
			step = 10240;
		if (in.pressed & DASH_IN_L2)
			step = -10240;
		if (step) {
			if (partition) size = hddPartitionSizeStep(size, step, min_mb, max_mb);
			else {
				size += step;
				if (size < min_mb) size = min_mb;
				if (size > max_mb) size = max_mb;
				size -= size % 128;
				if (size < min_mb) size = min_mb;
			}
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_OK) {
			dashSoundPlay(DASH_SND_CONFIRM);
			dashInputFlush();
			return size;
		}
		if (in.pressed & (DASH_IN_BACK | DASH_IN_MENU)) {
			dashSoundPlay(DASH_SND_CANCEL);
			dashInputFlush();
			return -1;
		}
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		{
			char num[32], lo[24], hi[24];
			float bx = x + 30, bw = w - 60, by = y + 116;
			float k = (max_mb > min_mb) ? (float)(size - min_mb) / (float)(max_mb - min_mb) : 1.0f;

			dashRenderScene();
			dashModalBackdrop(dashEaseOutCubic(open));
			dashModalPanel(x, y, w, h, 1.0f);
			dashIcon(DI_HDD, x + 24, y + 18, 22, DASH_C_ACCENT);
			dashTextFit(x + 56, y + 18, w - 80, title, DASH_FS_LABEL, DASH_C_TEXT);
			if (size >= 1024)
				snprintf(num, sizeof(num), "%.1f GB  (%d MB)", size / 1024.0f, size);
			else
				snprintf(num, sizeof(num), "%d MB", size);
			dashTextCenter(x + w * 0.5f, y + 64, num, DASH_FS_TITLE, DASH_C_ACCENT);
			dashPanel(bx, by, bw, 8, 4, dashColorA(DASH_C_TEXT3, 0.3f));
			dashPanel(bx, by, bw * k + 8, 8, 4, DASH_C_ACCENT);
			snprintf(lo, sizeof(lo), "%d MB", min_mb);
			if (max_mb >= 1024)
				snprintf(hi, sizeof(hi), "%.1f GB", max_mb / 1024.0f);
			else
				snprintf(hi, sizeof(hi), "%d MB", max_mb);
			dashText(bx, by + 14, lo, DASH_FS_CAPTION, DASH_C_TEXT3);
			dashTextRight(bx + bw, by + 14, hi, DASH_FS_CAPTION, DASH_C_TEXT3);
			{
				float hx = x + 24, hy = y + h - 36;
				/* Tighten the chevrons without overlapping button backings. */
				hx += dashDrawButton(hx, hy, DI_BTN_LEFT, NULL) + (dashCfg.icon_backing ? 0 : -6);
				hx += dashDrawButton(hx, hy, DI_BTN_RIGHT, NULL) + 4;
				hx = dashTextS(hx, dashButtonTextY(hy, DASH_FS_SMALL), "128 MB", DASH_FS_SMALL, DASH_C_TEXT2) + 10;
				hx += dashDrawButton(hx, hy, -1, "L1") + 2;
				hx += dashDrawButton(hx, hy, -1, "R1") + 4;
				hx = dashTextS(hx, dashButtonTextY(hy, DASH_FS_SMALL), "1 GB", DASH_FS_SMALL, DASH_C_TEXT2) + 10;
				hx += dashDrawButton(hx, hy, -1, "L2") + 2;
				hx += dashDrawButton(hx, hy, -1, "R2") + 4;
				hx = dashTextS(hx, dashButtonTextY(hy, DASH_FS_SMALL), "10 GB", DASH_FS_SMALL, DASH_C_TEXT2) + 16;
				dashModalHintRow(hx, hy, dashOkIcon(), localizeDashboardText("OK"), dashBackIcon(), localizeDashboardText("Cancel"));
			}
			dashGfxPresent();
		}
	}
}

//--------------------------------------------------------------
// Actions
//--------------------------------------------------------------
static int sel, top;
static float sel_anim;
static float top_anim = -1.0f;

static void afterChange(void)
{
	hm.changed = 1;
	invalidatePartitionCaches();
	scan();
}

/* names starting with "__" belong to the system and PSBBN */
static int confirmReservedName(const char *name)
{
	return strncmp(name, "__", 2) ||
	       dashModalConfirm("System partition name", "Names starting with \"__\" are reserved for the system and for PSBBN. Create it anyway?", localizeDashboardText("Create"), 1);
}

static int pickSize(const char *title, int min_mb, int max_mb, int start_mb)
{
	return pickSizeMenu(title, min_mb, max_mb, start_mb, 0);
}

static void actCreate(void)
{
	char name[MAX_PART_NAME + 1], unique[MAX_PART_NAME + 1], body[160];
	int size, max_mb;
	HddJob *j;

	name[0] = '\0';
	if (!dashModalKeyboard("New partition name", name, MAX_PART_NAME - 3) || name[0] == '\0')
		return;
	if (!confirmReservedName(name))
		return;
	max_mb = hm.free_mb < MAX_PART_MB ? (int)hm.free_mb : MAX_PART_MB;
	if (max_mb < 8) {
		dashModalMessage(DI_WARNING, "HDD full", "There is not enough free space for a new partition (8 MB minimum).", NULL);
		return;
	}
	size = pickSizeMenu("Partition size", 8, max_mb, 8, 1);
	if (size <= 0)
		return;
	uniqueName(name, unique, sizeof(unique), -1);
	snprintf(body, sizeof(body), "Create hdd0:%s with %d MB?", unique, size);
	if (!dashModalConfirm("Create partition", body, localizeDashboardText("Create"), 0))
		return;
	j = jobNew(HOP_CREATE);
	if (j == NULL)
		return;
	dashStrCopy(j->a, unique, sizeof(j->a));
	j->size = size;
	if (!runJob(j, "Creating partition", 0))
		return;
	if (j->ret > 0) {
		dashToast(DASH_TOAST_OK, "Partition created");
		afterChange();
		sel = findPart(unique) >= 0 ? findPart(unique) : sel;
	} else
		dashModalMessage(DI_WARNING, "Partition not created", "The HDD refused to create the partition. Check the free space.", unique);
	hddJobFree(j);
}

static void actRemove(HddPart *p)
{
	char body[200], name[MAX_PART_NAME + 1];
	HddJob *j;

	snprintf(body, sizeof(body), "Delete hdd0:%s and everything on it? This cannot be undone.", p->name);
	if (!dashModalConfirm("Remove partition", body, localizeDashboardText("Remove"), 1))
		return;
	dashStrCopy(name, p->name, sizeof(name));
	j = jobNew(HOP_REMOVE);
	if (j == NULL)
		return;
	dashStrCopy(j->a, name, sizeof(j->a));
	if (!runJob(j, "Removing partition", 0))
		return;
	if (j->ret == 0) {
		dashToast(DASH_TOAST_OK, "Partition removed");
		afterChange();
	} else
		dashModalMessage(DI_WARNING, "Partition not removed", "The HDD refused to remove the partition.", name);
	hddJobFree(j);
}

static void renamePartition(HddPart *p, const char *name)
{
	char target[MAX_PART_NAME + 1], detail[160];
	HddJob *j;
	if (!p || p->type == HP_SYSTEM || !strncmp(p->name, "__", 2) || !strcmp(p->name, name)) return;
	if (!name[0] || strlen(name) > MAX_PART_NAME || strpbrk(name, "/\\:,\r\n") ||
	    !strncmp(name, "__", 2) || !strcmp(name, ".") || !strcmp(name, "..")) {
		dashModalMessage(DI_WARNING, "Invalid partition name", "Use 1-32 characters without path separators, commas or the reserved __ prefix.", NULL);
		return;
	}
	dashStrCopy(target, name, sizeof(target));
	j = jobNew(HOP_RENAME);
	if (j == NULL) return;
	dashStrCopy(j->a, p->name, sizeof(j->a));
	dashStrCopy(j->b, target, sizeof(j->b));
	if (!runJob(j, "Renaming partition", 0)) return;
	if (j->ret == 0) {
		dashToast(DASH_TOAST_OK, "Partition renamed");
		afterChange();
		int index = findPart(target);
		if (index >= 0) sel = index;
	} else {
		snprintf(detail, sizeof(detail), "%s (error %d)", target, j->ret);
		dashModalMessage(DI_WARNING, "Partition not renamed",
		    j->ret == -EBUSY ? "The partition is in use. Unmount its VMC or close other applications/FTP using it." :
		    j->ret == -EEXIST ? "A partition with that name already exists. Choose a different name." : "The HDD refused the new name.", detail);
	}
	hddJobFree(j);
}

static void actRename(HddPart *p)
{
	char name[MAX_PART_NAME + 1];
	dashStrCopy(name, p->name, sizeof(name));
	if (dashModalKeyboard("Partition name", name, MAX_PART_NAME)) renamePartition(p, name);
}

static int partitionHidden(const char *name)
{
	size_t length = strlen(name);
	return length > 7 && !strcmp(name + length - 7, ".HIDDEN");
}

static void actVisibility(HddPart *p)
{
	char name[MAX_PART_NAME + 1], body[256];
	int hidden = partitionHidden(p->name);
	if (hidden) snprintf(name, sizeof(name), "%.*s", (int)strlen(p->name) - 7, p->name);
	else {
		if (strlen(p->name) > MAX_PART_NAME - 7) {
			dashModalMessage(DI_WARNING, "Partition name too long", "Adding .HIDDEN would exceed 32 characters. Rename the partition to 25 characters or fewer first.", p->name);
			return;
		}
		snprintf(name, sizeof(name), "%.25s.HIDDEN", p->name);
	}
	snprintf(body, sizeof(body), "Rename %s to %s? This controls HOSDMenu visibility. PSBBN DP entries and other saved partition paths are not updated.", p->name, name);
	if (dashModalConfirm(hidden ? "Show in HOSDMenu HDD Browser" : "Hide in HOSDMenu HDD Browser", body, hidden ? "Show" : "Hide", 0))
		renamePartition(p, name);
}

static void actRenameGame(HddPart *p)
{
	char name[64 + 1];
	HddJob *j;

	loadGameInfo(p);
	if (p->hdl_state != 1) {
		dashModalMessage(DI_WARNING, "No game information", "The game title of this partition could not be read.", p->name);
		return;
	}
	dashStrCopy(name, p->game.Name, sizeof(name));
	if (!dashModalKeyboard("Game title", name, MAX_PART_NAME - 1) || name[0] == '\0' || !strcmp(name, p->game.Name))
		return;
	j = jobNew(HOP_RENAME_GAME);
	if (j == NULL)
		return;
	dashStrCopy(j->a, p->game.Name, sizeof(j->a));
	dashStrCopy(j->b, name, sizeof(j->b));
	if (!runJob(j, "Renaming game", 0))
		return;
	/* HdlRenameGame returns 0 on success (the classic screen had this
	 * check backwards and reported success as a failure) */
	if (j->ret == 0) {
		dashStrCopy(p->game.Name, name, sizeof(p->game.Name));
		dashToast(DASH_TOAST_OK, "Game renamed");
	} else
		dashModalMessage(DI_WARNING, "Game not renamed", "The game title could not be changed.", name);
	hddJobFree(j);
}

static void actFormat(void)
{
#ifndef NO_IOP_RESET
	static const char *const formats[] = {"APA", "exFAT", "APA Jail (APA + exFAT)"};
	HddFormatPlan plan;
	HddInstall install;
	HddJob *j;
	char body[640], choice_title[128];
	int kind, reserve = 0, total, ret, began, format_ok;
#endif

	if (console_is_PSX) {
		dashModalMessage(DI_WARNING, "Not available", "The PSX internal HDD cannot be formatted from Launchstation.", NULL);
		return;
	}
#ifdef NO_IOP_RESET
	dashModalMessage(DI_WARNING, "Not available", "Formatting requires a build with IOP reset support.", NULL);
#else
#ifdef ETH
	if (isFtpServerRunning()) {
		dashModalMessage(DI_WARNING, "HDD busy", "Stop the FTP server before formatting the HDD.", NULL);
		return;
	}
	if (isSmbServerRunning()) {
		dashModalMessage(DI_WARNING, "HDD busy", "Stop the SMB server before formatting the HDD.", NULL);
		return;
	}
#endif
	total = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
	if ((total >= -4095 && total <= 0) || (u32)total == UINT32_MAX) {
		dashModalMessage(DI_WARNING, "Not available", "The drive capacity could not be determined within the supported 2 TiB range.", NULL);
		return;
	}
	kind = dashModalSelect("Format HDD", formats, 3, 0);
	if (kind < 0) return;
	memset(&install, 0, sizeof(install));
	if (kind != HDD_FORMAT_EXFAT && !dashHddInstallChoose(&install, kind == HDD_FORMAT_APA_JAIL)) return;
	if (kind == HDD_FORMAT_APA_JAIL) {
		int max = (int)hddFormatMaxReserve((u32)total);
		int minimum = install.flags & HDD_INSTALL_PSBBN ? 6400 : install.flags & HDD_INSTALL_APPS ? 256 : 0;
		if (install.flags & HDD_INSTALL_PSBBN) {
			HddFormatPlan base;
			int needed, free_reserve;
			int extent = fileXioDevctl("hdd0:", HDIOC_MAXSECTOR, NULL, 0, NULL, 0);
			if (extent < 2048 * 2048 || hddFormatPlan(&base, kind, (u32)total, 0, 0) < 0) {
				dashModalMessage(DI_WARNING, "Drive too small for PSBBN", "This installer needs the existing HDD driver to support a single 2 GiB APA extent for PSBBN's data partition.", NULL);
				return;
			}
			if (max > 131072 - (int)(base.apa_sectors / 2048)) max = 131072 - (int)(base.apa_sectors / 2048);
			for (;;) {
				if (!dashHddInstallConfigure(&install, (unsigned)extent / 2048)) return;
				install.reserved_mb = 0;
				needed = hddInstallRequiredMb(&install, base.apa_sectors / 2048);
				if (needed >= 0 && needed <= max) break;
				dashModalMessage(DI_WARNING, "Not enough APA space", "The selected Music, Contents and Linux sizes do not fit. Choose smaller sizes or cancel.", NULL);
			}
			free_reserve = pickSize(dashHddInstallChoiceTitle(&install, HDD_CHOICE_RESERVE, "Reserved APA space for future use", choice_title, sizeof(choice_title)), 0, max - needed, 0);
			if (free_reserve < 0) return;
			install.reserved_mb = free_reserve;
			reserve = hddInstallRequiredMb(&install, base.apa_sectors / 2048);
		} else {
			if (max < minimum) {
				dashModalMessage(DI_WARNING, "Not enough APA space", "This drive cannot reserve enough APA space for the selected software.", NULL);
				return;
			}
			reserve = pickSize("Additional APA space", minimum, max, max < 8192 ? max : 8192);
			if (reserve < 0) return;
		}
	}
	ret = hddFormatPlan(&plan, kind, (u32)total, reserve, (u32)time(NULL));
	if (ret < 0) {
		dashModalMessage(DI_WARNING, "Not available", "The drive is too small for this layout or its capacity is unsupported.", NULL);
		return;
	}
	if (install.flags) {
		dashHddInstallDebugBegin(&install, &plan);
		ret = dashHddInstallPrepare(&install);
		if (ret <= 0) { dashHddInstallDebugEnd(&install, ret == -ECANCELED ? ret : -EIO); hddInstallRelease(&install); return; }
		/* Network preparation may have reloaded the IOP. */
		loadHddModules();
	}
	{
		DashTableRow rows[18];
		char values[18][96];
		int count = 0;
#define REVIEW(label_, ...) do { snprintf(values[count], sizeof(values[count]), __VA_ARGS__); rows[count].label = label_; rows[count].value = values[count]; count++; } while (0)
		REVIEW("Format", "%s", formats[kind]);
		REVIEW("Drive Capacity", "%u MiB", (unsigned)((u32)total / 2048));
		if (kind != HDD_FORMAT_EXFAT) REVIEW("APA total space", "%u MiB", (unsigned)(plan.apa_sectors / 2048));
		if (kind != HDD_FORMAT_APA) {
			REVIEW("exFAT space", "%u MiB", (unsigned)(plan.exfat_sectors / 2048));
			REVIEW("exFAT label / cluster", "PS2 / 32 KiB");
		}
		REVIEW("Software", "%s", install.flags & HDD_INSTALL_PSBBN ? "PSBBN DP, HOSDMenu (EXPERIMENTAL)" :
		       install.flags == (HDD_INSTALL_HOSD | HDD_INSTALL_APPS) ? "MBR, HOSDMenu and apps" :
		       install.flags & HDD_INSTALL_HOSD ? "MBR and HOSDMenu" : install.flags & HDD_INSTALL_APPS ? "WLE-R3Z and R3CONFIGURATOR" : "No software, format only");
		if (install.flags & HDD_INSTALL_PSBBN) {
			REVIEW("PSBBN language", "%s", install.language.name);
			REVIEW("Default game launcher", "%s", hddInstallGameName(install.game_launcher));
			REVIEW("Music", "%u MiB", install.music_mb);
			REVIEW("Contents", "%u MiB", install.contents_mb);
			REVIEW("Standalone PS2 Linux", "%s", install.flags & HDD_INSTALL_PS2LINUX ? "On (2 GiB system)" : "Off");
			if (install.flags & HDD_INSTALL_PS2LINUX) REVIEW("PS2 Linux home", "%u MiB", install.linux_home_mb);
			REVIEW("APA future reserve", "%u MiB", install.reserved_mb);
		} else if (kind == HDD_FORMAT_APA_JAIL) REVIEW("Additional APA space", "%u MiB", (unsigned)plan.reserve_mb);
		if (kind == HDD_FORMAT_APA_JAIL) REVIEW("Recovery", "32 MiB");
		if (install.flags) REVIEW("Downloads", "To the internal HDD after erase");
		if (install.flags & HDD_INSTALL_HOSD) REVIEW("Console HDD boot", "Enable");
#undef REVIEW
		if (!dashModalConfirmTable(dashHddInstallChoiceTitle(&install, HDD_CHOICE_REVIEW, "Review HDD format", choice_title, sizeof(choice_title)), rows, count, localizeDashboardText("Continue"), 1)) { hddInstallCleanup(&install); dashHddInstallDebugEnd(&install, -ECANCELED); hddInstallRelease(&install); return; }
	}
	if (!dashModalConfirm(dashHddInstallChoiceTitle(&install, HDD_CHOICE_CONFIRM, "Are you sure?", choice_title, sizeof(choice_title)), "Every partition, game and file on this HDD will be erased. This cannot be undone.", localizeDashboardText("Format"), 1)) { hddInstallCleanup(&install); dashHddInstallDebugEnd(&install, -ECANCELED); hddInstallRelease(&install); return; }
	j = jobNew(HOP_FORMAT);
	if (!j) { hddInstallCleanup(&install); dashHddInstallDebugEnd(&install, -ENOMEM); hddInstallRelease(&install); return; }
	j->format_plan = plan;
	j->installer = install.flags ? &install : NULL;
	hddInstallPhase(HDD_INSTALL_PHASE_FORMAT);
	hddInstallLog("Starting disk-format RPC service");
	if (hddFormatLoad() < 0) {
		hddJobFree(j);
		hddInstallCleanup(&install);
		dashModalMessage(DI_WARNING, "Format unavailable", "The disk formatting service could not be started. The HDD has not been changed.", NULL);
		dashHddInstallDebugEnd(&install, -EIO); hddInstallRelease(&install); return;
	}
	if (install.flags) hddInstallResumeBegin(&install, &plan, 0);
	/* Release audio file handles before the formatter detaches ATA volumes. */
	dashSoundShutdown();
	if (!runJob(j, "Formatting the HDD", 0)) {
		hddInstallCleanup(&install);
		dashSoundMaintain(); dashBgmStart();
		hddInstallResumeEnd();
		dashHddInstallDebugEnd(&install, -EBUSY); hddInstallRelease(&install); return;
	}
	ret = j->ret;
	began = j->format_started;
	if (began) {
		hddInstallLog("Format completed rc=%d; resetting IOP and reloading HDD", ret);
		/* Discard all APA and BDM caches, including after partial failures.
		 * This is the existing runtime reset path, with PAD/audio recovery. */
		rebootIopAndReloadCoreStackSilent();
		loadHddModules();
	}
	hddJobFree(j);
	format_ok = ret == 0;
	if (ret == 0 && install.flags) {
		hddInstallPhase(HDD_INSTALL_PHASE_DOWNLOAD);
		hddInstallLog("Preparing network and ATA after format");
		ret = dashHddInstallPrepareStorage(&install);
		hddInstallLog("Storage preparation rc=%d stage=%s", ret, install.stage);
		if (ret >= 0) {
			j = jobNew(HOP_DOWNLOAD_INSTALLER);
			if (!j) ret = -ENOMEM;
			else {
				j->installer = &install;
				if (!runJob(j, "Downloading PSBBN DP installer", 0)) ret = -EBUSY;
				else { ret = j->ret; hddJobFree(j); }
			}
		}
		if (ret >= 0) ret = hddFormatLoad();
		if (ret >= 0) {
			j = jobNew(HOP_INSTALL_SOFTWARE);
			if (!j) ret = -ENOMEM;
			else {
				j->installer = &install;
				j->format_plan = plan;
				if (!runJob(j, "Installing system software", 0)) ret = -EBUSY;
				else {
					ret = j->ret;
					if (j->format_started) {
						hddInstallLog("Install completed rc=%d; resetting IOP", ret);
						rebootIopAndReloadCoreStackSilent();
						loadHddModules();
					}
					hddJobFree(j);
				}
			}
		}
		if (ret < 0) {
			if (!install.error[0]) snprintf(install.error, sizeof(install.error), "Installer error %d.", ret);
			snprintf(body, sizeof(body), "The HDD is formatted, but software installation is incomplete. %s", install.error);
			dashModalMessage(DI_WARNING, "Software installation incomplete", body, install.stage[0] ? install.stage : NULL);
		} else {
			dashToast(DASH_TOAST_OK, "HDD formatted and software installed");
		}
	} else if (ret == 0) {
		hddInstallCleanup(&install);
		dashToast(DASH_TOAST_OK, "HDD formatted");
	}
	hddInstallResumeEnd();
	dashHddInstallDebugEnd(&install, ret);
	hddInstallRelease(&install);
	dashSoundMaintain(); dashBgmStart();
	if (ret < 0 && !format_ok) {
		if (began)
			snprintf(body, sizeof(body), "Formatting failed (error %d). The drive may be partially formatted. Refresh its status before using it.", ret);
		else
			snprintf(body, sizeof(body), "Formatting could not start (error %d). The HDD has not been changed.", ret);
		dashModalMessage(DI_WARNING, "Format failed", body, NULL);
	}
	sel = top = 0;
	top_anim = -1.0f;
	afterChange();
#endif
}

/* Resume is deliberately separate from formatting: no format command is
 * reachable from this action, and reading the cache precedes confirmation. */
static void actResume(void)
{
#ifndef NO_IOP_RESET
	HddInstall install = {0}; HddFormatPlan plan; unsigned sequence;
	HddJob *j; int ret;
	if (console_is_PSX) return;
#ifdef ETH
	if (isFtpServerRunning()) { dashModalMessage(DI_WARNING, "HDD busy", "Stop the FTP server before resuming installation.", NULL); return; }
#endif
	if (!dashWorkerAwaitAll("Preparing installer resume")) return;
	dashSoundShutdown(); releaseMounts();
	if (!loadHddModules()) { dashSoundMaintain(); dashBgmStart(); return; }
#ifdef EXFAT
	loadAtaModules();
#endif
	j = jobNew(HOP_RESUME_LOOKUP); if (!j) { dashSoundMaintain(); dashBgmStart(); return; }
	j->installer = &install;
	if (!runJob(j, "Reading installer checkpoint", 0)) { hddInstallRelease(&install); dashSoundMaintain(); dashBgmStart(); return; }
	ret = j->ret; plan = j->format_plan; sequence = j->resume_sequence; hddJobFree(j);
	if (ret < 0) {
		dashModalMessage(DI_WARNING, "Cannot resume installation", "No valid checkpoint matching this drive and cache owner could be read. Installations started with older builds have no resume checkpoint. The HDD has not been formatted.", install.error[0] ? install.error : NULL);
		hddInstallRelease(&install); dashSoundMaintain(); dashBgmStart(); return;
	}
	DashTableRow rows[14]; char values[14][96]; int count = 0;
#define RESUME_ROW(label_, ...) do { snprintf(values[count], sizeof(values[count]), __VA_ARGS__); rows[count].label = label_; rows[count].value = values[count]; count++; } while (0)
	RESUME_ROW("Layout", "%s", install.jail ? "APA Jail (APA + exFAT)" : "APA");
	RESUME_ROW("Drive capacity", "%u MiB", (unsigned)(plan.sectors / 2048));
	RESUME_ROW("Cache", "%.95s", install.stage);
	RESUME_ROW("Repository", "%.7s", install.commit);
	RESUME_ROW("Software", "%s", install.flags & HDD_INSTALL_PSBBN ? "PSBBN DP (Experimental), MBR, HOSDMenu, apps" : install.flags == 3 ? "MBR, HOSDMenu and apps" : install.flags & HDD_INSTALL_HOSD ? "MBR and HOSDMenu" : "WLE-R3Z and R3CONFIGURATOR");
	if (install.flags & HDD_INSTALL_PSBBN) {
		RESUME_ROW("Language", "%s", install.language.name);
		RESUME_ROW("Default game launcher", "%s", hddInstallGameName(install.game_launcher));
		RESUME_ROW("Music / Contents", "%u / %u MiB", install.music_mb, install.contents_mb);
		RESUME_ROW("Standalone PS2 Linux", "%s", install.flags & HDD_INSTALL_PS2LINUX ? "On" : "Off");
		if (install.flags & HDD_INSTALL_PS2LINUX) RESUME_ROW("Linux home", "%u MiB", install.linux_home_mb);
		RESUME_ROW("APA future reserve", "%u MiB", install.reserved_mb);
	}
	RESUME_ROW("Continue from", "%s", install.committed ? "Finishing cleanup" : install.populated ? "MBR and HDD boot setup" : install.applied ? "Filesystem population" : "Downloads and software setup");
	RESUME_ROW("Formatting", "Keep existing drive layout");
#undef RESUME_ROW
	if (!dashModalConfirmTable("Resume HDD installation", rows, count, "Resume", 0)) {
		hddInstallRelease(&install); dashSoundMaintain(); dashBgmStart(); return;
	}
	dashHddInstallDebugBegin(&install, &plan);
	hddInstallLog("RESUME checkpoint %u; applied=%u populated=%u committed=%u", sequence, install.applied, install.populated, install.committed);
	hddInstallResumeBegin(&install, &plan, sequence);
	if (!install.committed) {
		ret = dashHttpsCaPath(install.ca_path, sizeof(install.ca_path)) ? hddInstallLoadCa(&install) : -ENOENT;
		if (ret >= 0) ret = dashHddInstallPrepareStorage(&install);
		if (ret >= 0) {
			j = jobNew(HOP_DOWNLOAD_INSTALLER);
			if (!j) ret = -ENOMEM;
			else { j->installer = &install; if (!runJob(j, "Checking cached installer downloads", 0)) ret = -EBUSY; else { ret = j->ret; hddJobFree(j); } }
		}
	}
	if (ret >= 0 && !install.committed) ret = hddFormatLoad();
	if (ret >= 0) {
		j = jobNew(HOP_INSTALL_SOFTWARE);
		if (!j) ret = -ENOMEM;
		else {
			j->installer = &install; j->format_plan = plan;
			if (!runJob(j, "Resuming system software installation", 0)) ret = -EBUSY;
			else { ret = j->ret; if (j->format_started) { rebootIopAndReloadCoreStackSilent(); loadHddModules(); } hddJobFree(j); }
		}
	}
	hddInstallResumeEnd();
	if (ret < 0) dashModalMessage(DI_WARNING, "Installation incomplete", install.error[0] ? install.error : "The installer could not finish. Cached files and the last checkpoint are retained.", install.stage);
	else dashToast(DASH_TOAST_OK, "HDD software installation completed");
	dashHddInstallDebugEnd(&install, ret); hddInstallRelease(&install);
	dashSoundMaintain(); dashBgmStart(); sel = top = 0; top_anim = -1; afterChange();
#else
	dashModalMessage(DI_WARNING, "Not available", "Installer resume requires IOP reset support.", NULL);
#endif
}

static void actStatus(void)
{
	char body[320], detail[48];

	if ((hm.filesystem & HDD_FS_APA) && (hm.filesystem & (HDD_FS_EXFAT | HDD_FS_FAT32)))
		snprintf(body, sizeof(body), "This drive contains APA partitions and %s volumes, a hybrid layout used by APA Jail. HDD Manager lists APA partitions. Browse FAT/exFAT files in File Browser.",
		         hddFilesystemName(hm.filesystem & ~HDD_FS_APA));
	else if (hm.filesystem == HDD_FS_APA && hm.filesystem_error < 0)
		snprintf(body, sizeof(body), "APA partitions were detected. Checking for additional exFAT/FAT32 volumes failed (error %d). Refresh to try again.", hm.filesystem_error);
	else if (hm.filesystem && hm.filesystem != HDD_FS_APA)
		snprintf(body, sizeof(body), "The internal HDD contains %s volumes. Browse their files in File Browser. HDD Manager manages APA partitions.",
		         hddFilesystemName(hm.filesystem));
	else if (hm.status == 1 && hm.filesystem_error < 0)
		snprintf(body, sizeof(body), "No APA format was found. The filesystem check failed (error %d). Refresh to try again.", hm.filesystem_error);
	else if (hm.status == 1)
		snprintf(body, sizeof(body), "No supported filesystem was identified. The drive may be unformatted, use another format, or contain damaged filesystem metadata.");
	else
		snprintf(body, sizeof(body), "%s", hddStatusInfoText(hm.status));
	snprintf(detail, sizeof(detail), "HDIOC_STATUS %d: %s", hm.status, hddStatusShortText(hm.status));
	dashModalMessage(DI_HDD, "HDD status", body, detail);
}

/* the device holding __Headers/: its driver starts on the UI thread */
static const char *pickHeaderSource(void)
{
	static const char *const devs[] = {
	    "usb:",
#ifdef MMCE
	    "mmce0:",
	    "mmce1:",
#endif
#ifdef UDPFS
	    "udpfs:",
#endif
	};
	DashMenuItem items[4];
	int n = (int)(sizeof(devs) / sizeof(devs[0])), i, r;

	for (i = 0; i < n; i++) {
		items[i].icon = !strncmp(devs[i], "usb", 3) ? DI_USB : !strncmp(devs[i], "udpfs", 5) ? DI_SHARE : DI_SDCARD;
		items[i].label = devs[i];
		items[i].detail = "__Headers/";
		items[i].disabled = 0;
	}
	r = dashModalMenu("Header source", "Device with a __Headers/<partition>/ folder", items, n, 0);
	if (r < 0)
		return NULL;
	if (dashDevicesStartForPath(devs[r]) < 0)
		return NULL;
	return devs[r];
}

static void injectOne(HddPart *p, const char *src)
{
	char body[240];
	HddJob *j;

	snprintf(body, sizeof(body),
	         "Write the APA header of hdd0:%s from %s__Headers/%s/? Required: system.cnf; PS2 titles also icon.sys and list.ico.",
	         p->name, src, p->name);
	if (!dashModalConfirm("Inject header", body, localizeDashboardText("Inject"), 1))
		return;
	j = jobNew(HOP_INJECT);
	if (j == NULL)
		return;
	dashStrCopy(j->a, p->name, sizeof(j->a));
	dashStrCopy(j->b, src, sizeof(j->b));
	if (!runJob(j, "Injecting header", 0))
		return;
	if (j->ret < 0) {
		char d[48];
		snprintf(d, sizeof(d), "error %d", j->ret);
		dashModalMessage(DI_WARNING, "Header not injected", "Check that the header folder has a system.cnf.", d);
	} else if (j->pfs_error < 0)
		dashToast(DASH_TOAST_WARN, "Header injected; copying the PFS files failed");
	else
		dashToast(DASH_TOAST_OK, j->pfs_copied > 0 ? "Header and PFS files injected" : "Header injected");
	hddJobFree(j);
}

static void injectMatching(const char *src, int create_missing)
{
	HddJob *list = jobNew(HOP_LIST_HEADERS);
	int injected = 0, created = 0, skipped = 0, failed = 0, pfs_failed = 0, stopped = 0, i;
	char body[240];

	if (list == NULL)
		return;
	dashStrCopy(list->b, src, sizeof(list->b));
	if (!runJob(list, "Reading header folders", 1))
		return;
	if (list->nsources < 0) {
		snprintf(body, sizeof(body), "%s__Headers/ could not be read (error %d).", src, list->nsources);
		dashModalMessage(DI_WARNING, "No header folders", body, NULL);
		hddJobFree(list);
		return;
	}
	if (list->nsources == 0) {
		snprintf(body, sizeof(body), "%s__Headers/ has no usable partition folders (%d invalid).", src, list->invalid);
		dashModalMessage(DI_INFO, "Nothing to inject", body, NULL);
		hddJobFree(list);
		return;
	}
	snprintf(body, sizeof(body), "%d header folder%s in %s__Headers/. Inject them into the matching partitions%s?",
	         list->nsources, list->nsources == 1 ? "" : "s", src, create_missing ? ", creating missing ones" : "");
	if (!dashModalConfirm("Inject headers", body, localizeDashboardText("Inject"), 1)) {
		hddJobFree(list);
		return;
	}
	for (i = 0; i < list->nsources && !stopped; i++) {
		const char *name = list->sources[i].name;
		int idx = findPart(name);
		HddJob *j;

		if (idx >= 0 && hm.parts[idx].type != HP_PFS) {
			skipped++;
			continue;
		}
		if (idx < 0) {
			DashMenuItem items[] = {{DI_PLUS, "Create it", NULL, 0}, {DI_BACK, "Skip it", NULL, 0}, {DI_CLOSE, "Stop", NULL, 0}};
			int r, size;
			if (!create_missing) {
				skipped++;
				continue;
			}
			snprintf(body, sizeof(body), "hdd0:%s does not exist", name);
			r = dashModalMenu("Missing partition", body, items, 3, 0);
			if (r == 1) {
				skipped++;
				continue;
			}
			if (r != 0) {
				stopped = 1;
				break;
			}
			if (!confirmReservedName(name)) {
				skipped++;
				continue;
			}
			size = pickSizeMenu("Partition size", 8, hm.free_mb < MAX_PART_MB ? (int)hm.free_mb : MAX_PART_MB, 8, 1);
			if (size <= 0) {
				stopped = 1;
				break;
			}
			j = jobNew(HOP_CREATE);
			if (j == NULL)
				break;
			dashStrCopy(j->a, name, sizeof(j->a));
			j->size = size;
			if (!runJob(j, "Creating partition", 0)) {
				stopped = 1;
				break;
			}
			if (j->ret <= 0) {
				failed++;
				hddJobFree(j);
				continue;
			}
			hddJobFree(j);
			created++;
			hm.changed = 1;
		}
		j = jobNew(HOP_INJECT);
		if (j == NULL)
			break;
		dashStrCopy(j->a, name, sizeof(j->a));
		dashStrCopy(j->b, src, sizeof(j->b));
		if (!runJob(j, "Injecting headers", 0)) {
			stopped = 1;
			break;
		}
		if (j->ret >= 0) {
			injected++;
			if (j->pfs_error < 0)
				pfs_failed++;
		} else
			failed++;
		hddJobFree(j);
	}
	snprintf(body, sizeof(body), "Injected: %d   Created: %d   Skipped: %d   Failed: %d   PFS copy failed: %d%s",
	         injected, created, skipped, failed, pfs_failed, stopped ? "   (stopped)" : "");
	dashModalMessage((failed || pfs_failed) ? DI_WARNING : DI_CHECK, "Header injection finished", body,
	                 list->invalid ? "Some header folders were incomplete and were ignored." : NULL);
	hddJobFree(list);
	if (created)
		afterChange();
}

static void actInject(HddPart *p)
{
	DashMenuItem items[] = {
	    {DI_HDD, "This partition", NULL, 0},
	    {DI_VIEW_LIST, "All matching partitions", NULL, 0},
	    {DI_PLUS, "Matching, and create missing ones", NULL, 0}};
	const char *src;
	int r;

	r = dashModalMenu("Inject APA header", p->name, items, 3, 0);
	if (r < 0)
		return;
	src = pickHeaderSource();
	if (src == NULL)
		return;
	if (r == 0)
		injectOne(p, src);
	else
		injectMatching(src, r == 2);
}

static int editorForHeader(char *path)
{
	return dashTextEditor(path);
}

static void actSystemCnf(HddPart *p)
{
	int ret;

	/* raw sector access plus a temporary file: on the UI thread, with
	 * the worker idle; the text is edited in the dashboard Text Editor */
	if (!dashWorkerAwaitAll("Finishing up"))
		return;
	releaseMounts();
	hddHeaderTextEditor = editorForHeader;
	ret = EditHddPartitionHeaderSystemCnf(p->name);
	hddHeaderTextEditor = TextEditor;
	if (ret > 0)
		dashToast(DASH_TOAST_OK, "SYSTEM.CNF updated");
	else if (ret == 0)
		dashToast(DASH_TOAST_INFO, "SYSTEM.CNF unchanged");
	else {
		char d[32];
		snprintf(d, sizeof(d), "error %d", ret);
		dashModalMessage(DI_WARNING, "SYSTEM.CNF not saved", "The partition header could not be updated.", d);
	}
}

/* Cache the read-only probe until refresh or an installer/disk action. The
 * selected partition has no bearing on whether the drive can resume. */
static int resumeStatus(void)
{
#ifndef NO_IOP_RESET
	HddInstall probe = {0};
	HddJob *j;
	int jail = !!(hm.filesystem & (HDD_FS_EXFAT | HDD_FS_FAT32));
	if (console_is_PSX || !hm.present || !hm.formatted) return 0;
#ifdef ETH
	if (isFtpServerRunning()) return 0;
#endif
	if (hm.resume_checked) return hm.resume_available;
	if (!dashWorkerAwaitAll("Checking installer status")) return -EBUSY;
#ifdef EXFAT
	if (jail && !loadAtaModules()) return -EIO;
#else
	if (jail) return 0;
#endif
	j = jobNew(HOP_RESUME_AVAILABLE);
	if (!j) return -ENOMEM;
	j->installer = &probe; j->nsub = jail;
	if (!runJob(j, "Checking installer status", 0)) return -EAGAIN;
	int ret = j->ret;
	hddJobFree(j); hddInstallRelease(&probe);
	if (ret == HDD_INSTALL_ATA_TIMEOUT) loadHddModules();
	hm.resume_checked = ret >= 0;
	if (hm.resume_checked) hm.resume_available = ret;
	return ret;
#else
	return 0;
#endif
}

/* Check on opening Options so removing/replacing the ELF takes effect. */
static int systemAppStatus(void)
{
    if (!dashWorkerAwaitAll("Checking HDD software")) return -EBUSY;
    HddJob *j = jobNew(HOP_SYSTEM_APP);
    if (!j) return -ENOMEM;
    if (!runJob(j, "Checking HDD software", 0)) return -EAGAIN;
    int ret = j->ret;
    hddJobFree(j);
    return ret;
}

static void partitionMenu(void)
{
	HddPart *p = (hm.n > 0 && sel < hm.n) ? &hm.parts[sel] : NULL;
	DashMenuItem items[14];
	int ids[14], n = 0, r, has_cnf = 0;
	int system = (p == NULL || p->type == HP_SYSTEM);
	int reserved = (p != NULL && !strncmp(p->name, "__", 2));
	enum {
		A_CREATE,
		A_REMOVE,
		A_RENAME,
		A_RENAME_GAME,
		A_INJECT,
		A_SYSCNF,
		A_STATUS,
		A_REFRESH,
		A_FORMAT,
		A_RESUME,
		A_INSTALL_APP,
		A_VISIBILITY
	};

	if (p != NULL && !system && dashWorkerAwaitAll("Finishing up"))
		has_cnf = (HddPartitionHeaderSystemCnfExists(p->name) > 0);
	int can_hide = p && p->type == HP_PFS && !reserved;
	int can_install = 0;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD) && defined(EXFAT)
	can_install = !console_is_PSX && hm.formatted && (hm.filesystem & (HDD_FS_EXFAT | HDD_FS_FAT32));
#endif
	int hosdmenu = (can_hide || can_install) ? systemAppStatus() : 0;
	const char *retry_detail = "Check failed. Reopen Options.";
	const char *hosd_detail = hosdmenu < 0 ? retry_detail : NULL;
#define ADD(ic, lab, det, dis, id) \
	(items[n].icon = (ic), items[n].label = (lab), items[n].detail = (det), items[n].disabled = (dis), ids[n++] = (id))
	if (hm.formatted)
		ADD(DI_PLUS, "Create partition...", NULL, 0, A_CREATE);
	if (p != NULL) {
		ADD(DI_CLOSE, "Remove partition...", reserved ? "system" : NULL, system || reserved, A_REMOVE);
		ADD(DI_EDIT, "Rename partition...", NULL, system || reserved, A_RENAME);
		if (can_hide && hosdmenu)
			ADD(partitionHidden(p->name) ? DI_SHOW : DI_HIDE, partitionHidden(p->name) ? "Show in HOSDMenu HDD Browser" : "Hide in HOSDMenu HDD Browser",
			    hosd_detail, hosdmenu < 0, A_VISIBILITY);
	}
#if defined(ETH) && defined(WLE_NATIVE_CLOUD) && defined(EXFAT)
	if (can_install && hosdmenu)
		ADD(DI_ARCHIVE_EXTRACT, "Install apps from PSU...", hosd_detail ? hosd_detail : "8 MiB launcher; Internet required",
		    hosdmenu < 0, A_INSTALL_APP);
#endif
	if (p != NULL) {
		if (p->type == HP_HDL)
			ADD(DI_DISC, "Rename game...", NULL, 0, A_RENAME_GAME);
		ADD(DI_TEXT, "Inject APA header...", NULL, p->type != HP_PFS, A_INJECT);
		if (has_cnf)
			ADD(DI_SETTINGS, "Edit SYSTEM.CNF...", NULL, 0, A_SYSCNF);
	}
	ADD(DI_INFO, "HDD status", hm.filesystem ? hddFilesystemName(hm.filesystem) : hddStatusShortText(hm.status), 0, A_STATUS);
	ADD(DI_REFRESH, "Refresh", NULL, 0, A_REFRESH);
	if (!console_is_PSX && hm.present)
		ADD(DI_WARNING, "Format HDD...", NULL, 0, A_FORMAT);
	int resume = resumeStatus();
	if (resume)
		ADD(DI_REFRESH, "Resume installation...", resume < 0 ? retry_detail : NULL,
		    resume < 0, A_RESUME);
#undef ADD
	r = dashModalMenu(localizeDashboardText("HDD Manager"), p ? p->name : "hdd0:", items, n, 0);
	if (r < 0)
		return;
	switch (ids[r]) {
		case A_INSTALL_APP:
			dashHddInstallApp(NULL);
			afterChange();
			break;
		case A_CREATE:
			actCreate();
			break;
		case A_REMOVE:
			actRemove(p);
			break;
		case A_VISIBILITY:
			actVisibility(p);
			break;
		case A_RENAME:
			actRename(p);
			break;
		case A_RENAME_GAME:
			actRenameGame(p);
			break;
		case A_INJECT:
			actInject(p);
			break;
		case A_SYSCNF:
			actSystemCnf(p);
			break;
		case A_STATUS:
			actStatus();
			break;
		case A_REFRESH:
			scan();
			break;
		case A_RESUME:
			actResume();
			hm.resume_checked = 0;
			break;
		case A_FORMAT:
			actFormat();
			break;
	}
}

//--------------------------------------------------------------
// Drawing
//--------------------------------------------------------------
#define LIST_X 40.0f
#define LIST_Y 108.0f
#define LIST_W 300.0f
#define ROW_H 26.0f
#define ROWS 10
#define DET_X 356.0f
#define DET_W 244.0f

static const char *typeName(const HddPart *p)
{
	switch (p->type) {
		case HP_PFS:
			return "PFS partition";
		case HP_HDL:
			return "HDL game";
		case HP_SYSTEM:
			return "System";
	}
	return "Not accessible";
}

static int typeIcon(const HddPart *p)
{
	switch (p->type) {
		case HP_PFS:
			return DI_PARTITION;
		case HP_HDL:
			return DI_DISC;
		case HP_SYSTEM:
			return DI_CHIP;
	}
	return DI_WARNING;
}

/* HDL partitions are named "PP.<startup ID>..<TITLE>" (older hdl_dump:
 * "PP.HDL.<TITLE>"). Returns the offset of the title part, or 0. */
static int hdlTitleOffset(const char *name)
{
	const char *t;

	if (strncmp(name, "PP.", 3))
		return 0;
	t = strstr(name + 3, "..");
	if (t != NULL && t[2])
		return (int)(t + 2 - name);
	if (!strncmp(name + 3, "HDL.", 4) && name[7])
		return 7;
	return 0;
}

/* Match the Applications list: scroll the selected row and details together,
 * hold each end for three seconds, then restart at the beginning. */
static float partNameScrollProgress(const HddPart *p, float age)
{
	const float dwell = 3.0f, speed = 34.0f;
	float travel, extra, move_time, phase, progress;

	if (p == NULL)
		return 0.0f;
	travel = dashTextWidth(p->name, DASH_FS_SMALL) - (LIST_W - 110);
	extra = dashTextWidth(p->name, DASH_FS_LABEL) - (DET_W - 32);
	if (extra > travel)
		travel = extra;
	if (travel <= 0.0f)
		return 0.0f;
	move_time = travel / speed;
	if (move_time < 0.35f)
		move_time = 0.35f;
	phase = fmodf(age < 0.0f ? 0.0f : age, dwell * 2.0f + move_time);
	progress = dashClampf((phase - dwell) / move_time, 0.0f, 1.0f);
	return progress * progress * (3.0f - 2.0f * progress);
}

/* The raw APA name (classic browsing, no HDL lookups) with its game-title
 * part highlighted: the ID prefix is dimmed and gives way to the title.
 * Selected names scroll in full; idle rows keep the title visible. */
static void drawPartName(float x, float y, float maxw, const char *name, DashColor col, int selected, float scroll)
{
	char prefix[40];
	int off = hdlTitleOffset(name);
	float tw, pw;

	if (selected) {
		tw = dashTextWidth(name, DASH_FS_SMALL);
		pw = tw > maxw ? (tw - maxw) * dashClampf(scroll, 0.0f, 1.0f) : 0.0f;
		dashGfxScissorPush(x, y - 2.0f, maxw, dashTextLineH(DASH_FS_SMALL) + 4.0f);
		x -= pw;
		if (off > 0 && off < (int)sizeof(prefix)) {
			memcpy(prefix, name, off);
			prefix[off] = '\0';
			x = dashTextS(x, y, prefix, DASH_FS_SMALL, dashColorA(col, 0.5f));
			name += off;
		}
		dashTextS(x, y, name, DASH_FS_SMALL, col);
		dashGfxScissorPop();
		return;
	}
	if (off <= 0 || off >= (int)sizeof(prefix)) {
		dashTextFit(x, y, maxw, name, DASH_FS_SMALL, col);
		return;
	}
	memcpy(prefix, name, off);
	prefix[off] = '\0';
	tw = dashTextWidth(name + off, DASH_FS_SMALL);
	if (tw > maxw * 0.65f)
		tw = maxw * 0.65f;
	pw = maxw - tw;
	if (pw > dashTextWidth(prefix, DASH_FS_SMALL))
		pw = dashTextWidth(prefix, DASH_FS_SMALL);
	x = dashTextFit(x, y, pw, prefix, DASH_FS_SMALL, dashColorA(col, 0.5f));
	dashTextFit(x, y, maxw - pw, name + off, DASH_FS_SMALL, col);
}

static void fmtMB(char *out, int size, u32 mb)
{
	if (mb >= 1024)
		snprintf(out, size, "%.1f GB", mb / 1024.0f);
	else
		snprintf(out, size, "%u MB", mb);
}

static void usageBar(float x, float y, float w, u32 used, u32 total, DashColor c)
{
	float k = (total > 0) ? (float)used / (float)total : 0.0f;

	if (k > 1.0f)
		k = 1.0f;
	dashPanel(x, y, w, 10, 5, dashColorA(DASH_C_TEXT3, 0.3f));
	if (k > 0.0f)
		dashPanel(x, y, w * k < 10 ? 10 : w * k, 10, 5, c);
}

static void drawDetails(float scroll)
{
	float x = DET_X, y = LIST_Y - 6, w = DET_W, h = ROWS * ROW_H + 12;
	char a[48], b[48], line[96];
	HddPart *p;

	dashDrawCard(x, y, w, h, 16);
	if (hm.n == 0 || sel >= hm.n) {
		if (hm.filesystem && !hm.formatted)
			snprintf(line, sizeof(line), "%s volumes are accessed in File Browser. HDD Manager manages APA partitions.", hddFilesystemName(hm.filesystem));
		else
			snprintf(line, sizeof(line), "%s", hm.formatted ? "No APA partitions." : "No APA partition list is available.");
		dashTextWrap(x + 16, y + 20, w - 32, 4, line, DASH_FS_SMALL, DASH_C_TEXT2, 19);
		return;
	}
	p = &hm.parts[sel];
	dashIcon(typeIcon(p), x + w * 0.5f - 24, y + 16, 48, DASH_C_TEXT);
	/* APA names can be 32 characters (e.g. PP.SLUS-...); unrestricted
	 * centered text overflowed the 244px details card on 4:3 TVs. */
	dashTextScrollProgress(x + 16, y + 72, w - 32, p->name, DASH_FS_LABEL, DASH_C_TEXT, scroll);
	dashTextFit(x + 16, y + 98, w - 32, typeName(p), DASH_FS_SMALL, DASH_C_ACCENT);
	fmtMB(a, sizeof(a), p->raw_mb);
	snprintf(line, sizeof(line), "Size on disk: %s", a);
	dashTextFit(x + 16, y + 122, w - 32, line, DASH_FS_SMALL, DASH_C_TEXT2);
	y += 150;
	if (p->type == HP_PFS) {
		if (p->pfs_state == 1) {
			u32 used = p->pfs_total > p->pfs_free ? p->pfs_total - p->pfs_free : 0;
			fmtMB(a, sizeof(a), used);
			fmtMB(b, sizeof(b), p->pfs_free);
			usageBar(x + 20, y, w - 40, used, p->pfs_total, DASH_C_ACCENT);
			snprintf(line, sizeof(line), "%s used, %s free", a, b);
			dashTextFit(x + 16, y + 18, w - 32, line, DASH_FS_CAPTION, DASH_C_TEXT2);
		} else if (p->pfs_state == -1) {
			snprintf(line, sizeof(line), "Usage unavailable (error %d)", p->pfs_error);
			dashTextFit(x + 16, y, w - 32, line, DASH_FS_CAPTION, DASH_C_WARM);
		} else {
			dashGfxBlend(DASH_BLEND_ADD);
			dashDrawSpinner(x + w * 0.5f - 60, y + 8, 6, DASH_C_ACCENT);
			dashGfxBlend(DASH_BLEND_NORMAL);
			dashText(x + w * 0.5f - 46, y, "Reading usage...", DASH_FS_CAPTION, DASH_C_TEXT3);
		}
	} else if (p->type == HP_HDL) {
		if (p->hdl_state == 1) {
			dashTextWrap(x + 16, y, w - 32, 2, p->game.Name, DASH_FS_SMALL, DASH_C_TEXT, 18);
			snprintf(line, sizeof(line), "%s  |  %s", p->game.Startup, p->game.Is_Dvd ? "DVD" : "CD");
			dashTextFit(x + 16, y + 44, w - 32, line, DASH_FS_CAPTION, DASH_C_TEXT2);
		} else if (p->hdl_state == -1)
			dashTextFit(x + 16, y, w - 32, "Game information unavailable", DASH_FS_CAPTION, DASH_C_TEXT3);
		else
			dashTextFit(x + 16, y, w - 32, "Press R1 for game information", DASH_FS_CAPTION, DASH_C_TEXT3);
	} else if (p->type == HP_SYSTEM)
		dashTextWrap(x + 16, y, w - 32, 3, "Reserved for the system. It cannot be changed.", DASH_FS_CAPTION, DASH_C_TEXT3, 17);
	else
		dashTextWrap(x + 16, y, w - 32, 3, "Not a PFS or HDL partition; Launchstation cannot open it.", DASH_FS_CAPTION, DASH_C_TEXT3, 17);
}

static void hddScene(void)
{
	char a[32], b[32], c[32], line[160];
	int i;
	const HddPart *selected = (hm.n > 0 && sel < hm.n) ? &hm.parts[sel] : NULL;
	float scroll = partNameScrollProgress(selected, dashUiFocusAge());
	float title_right;

	dashGfxBeginFrame();
	dashBgDraw();
	dashIcon(DI_HDD_MANAGER, 40, 20, 24, DASH_C_ACCENT);
	title_right = dashTextS(72, 20, localizeDashboardText("HDD Manager"), DASH_FS_TITLE, DASH_C_TEXT);
	if (hm.filesystem)
		snprintf(line, sizeof(line), "Format: %s%s", hddFilesystemName(hm.filesystem), hm.filesystem_error < 0 ? " + ?" : "");
	else
		snprintf(line, sizeof(line), "Status: %s", hddStatusShortText(hm.status));
	{
		float width = dashTextWidth(line, DASH_FS_SMALL);
		float available = DASH_SAFE_R - title_right - 16;

		if (width > available)
			width = available;
		if (width > 0)
			dashTextFit(DASH_SAFE_R - width, 26, width, line, DASH_FS_SMALL,
			            hm.filesystem && hm.filesystem_error == 0 ? DASH_C_OK : DASH_C_WARM);
	}

	if (hm.formatted && hm.size_mb > 0) {
		fmtMB(a, sizeof(a), hm.used_mb);
		fmtMB(b, sizeof(b), hm.free_mb);
		fmtMB(c, sizeof(c), hm.size_mb);
		usageBar(40, 62, 560, hm.used_mb, hm.size_mb, DASH_C_ACCENT);
		snprintf(line, sizeof(line), "%s used  |  %s %s  |  %s  |  %d partition%s%s", a, b, hm.apa_mb < hm.size_mb ? "APA free" : "free", c, hm.n, hm.n == 1 ? "" : "s",
		         hm.overflow ? " (list full)" : "");
		dashTextFit(40, 78, 560, line, DASH_FS_CAPTION, DASH_C_TEXT2);
	} else if (hm.scanned) {
		if (hm.filesystem)
			snprintf(line, sizeof(line), "%s volumes detected. Browse files in File Browser.", hddFilesystemName(hm.filesystem));
		else if (hm.status == 1)
			snprintf(line, sizeof(line), "%s", hm.filesystem_error < 0 ? "Filesystem check failed. Refresh to try again." : "Filesystem not recognized; no APA format found.");
		else
			snprintf(line, sizeof(line), "%s", hm.present ? "The HDD is not ready for filesystem detection." : "No HDD was found.");
		dashTextFit(40, 64, 560, line, DASH_FS_SMALL, hm.filesystem ? DASH_C_TEXT2 : DASH_C_WARM);
	}

	top_anim = listSlideTop(top_anim, top, sel, hm.n, ROWS,
	                        dashApproach(0.0f, 1.0f, 14.0f * dashAnimSpeed(), dashFrameDelta()));
	dashDrawPanelBox(LIST_X - 6, LIST_Y - 6, LIST_W + 12, ROWS * ROW_H + 12, 14);
	if (hm.n > 0) {
		dashGfxScissor(LIST_X - 4, LIST_Y - 4, LIST_W + 8, ROWS * ROW_H + 8);
		dashDrawSelection(LIST_X, LIST_Y + (sel_anim - top_anim) * ROW_H, LIST_W, ROW_H - 3, 8, 1.0f);
		for (i = (int)top_anim; i < hm.n && i < top_anim + ROWS; i++) {
			HddPart *p = &hm.parts[i];
			float y = LIST_Y + (i - top_anim) * ROW_H;
			DashColor col = dashItemColor(i == sel);
			/* Classic wLaunchELF-style APA browsing: never replace partition
			 * identifiers with asynchronously fetched game titles. */
			const char *label = p->name;

			dashIcon(typeIcon(p), LIST_X + 8, y + 4, 15, p->type == HP_OTHER ? DASH_C_TEXT3 : col);
			drawPartName(LIST_X + 30, y + 1.5f, LIST_W - 110, label, col, i == sel, scroll);
			fmtMB(a, sizeof(a), p->raw_mb);
			dashTextRight(LIST_X + LIST_W - 10, y + 2, a, DASH_FS_CAPTION, DASH_C_TEXT3);
		}
		dashGfxScissorReset();
		dashDrawScrollbar(LIST_X + LIST_W + 8, LIST_Y, ROWS * ROW_H, top_anim, ROWS, hm.n);
	}
	drawDetails(scroll);
	{
		DashHint h[] = {{dashOkIcon(), NULL, "Actions"}, {-1, "L2/R2", "Page"}, {-1, "L1", "Status"}, {-1, "R1", "Game info"}, {dashBackIcon(), NULL, "Exit"}};
		dashDrawHints(h, (int)(sizeof(h) / sizeof(h[0])));
	}
	dashDrawToasts();
	dashDrawActivity();
}

//--------------------------------------------------------------
// Entry
//--------------------------------------------------------------
void dashHddManager(void)
{
	DashInput in;
	u64 last_move = Timer();

	memset(&hm, 0, sizeof(hm));
	sel = top = 0;
	top_anim = -1.0f;
	sel_anim = 0.0f;
	/* the HDD stack loads on the UI thread */
	if (dashDevicesStartForPath("hdd0:") < 0)
		return;
	if (!isPathDeviceStackLoaded("hdd0:")) {
		dashModalMessage(DI_WARNING, "No HDD", "The internal HDD drivers could not be started. Is an HDD connected?", NULL);
		return;
	}
	dashPushSceneOverride(hddScene);
	if (!scan()) {
		dashPopSceneOverride();
		return;
	}
	dashInputFlush();
	dashUiFocusPoke();
	while (1) {
		int prev = sel;

		dashSoundMaintain(); /* Recover audio after cancelled IOP/network setup. */
		dashInputPoll(&in);
		if (dashDashboardPending())
			break;
		if (hm.n > 0) {
			if (in.pressed & DASH_IN_UP)
				sel = sel > 0 ? sel - 1 : hm.n - 1;
			if (in.pressed & DASH_IN_DOWN)
				sel = (sel + 1) % hm.n;
			if (in.pressed & DASH_IN_L2) {
				sel = (sel - ROWS) % hm.n;
				if (sel < 0)
					sel += hm.n;
			}
			if (in.pressed & DASH_IN_R2)
				sel = (sel + ROWS) % hm.n;
			/* Left/Right page like the File Browser: stop at the first or
			 * last partition, then wrap on the next press. */
			if (in.pressed & DASH_IN_LEFT)
				sel = sel == 0 ? hm.n - 1 : (sel < ROWS ? 0 : sel - ROWS);
			if (in.pressed & DASH_IN_RIGHT)
				sel = sel == hm.n - 1 ? 0 : (sel + ROWS >= hm.n ? hm.n - 1 : sel + ROWS);
		}
		if (sel != prev) {
			dashSoundPlay(DASH_SND_CURSOR);
			dashUiFocusPoke();
			last_move = Timer();
		}
		if (in.pressed & (DASH_IN_OK | DASH_IN_MENU)) {
			partitionMenu();
			dashUiFocusPoke();
		}
		if (in.pressed & DASH_IN_L1)
			actStatus();
		/* Game metadata is deliberately opt-in. Some old HDDs stall when
		 * every highlighted HDL partition triggers synchronous title RPC. */
		if ((in.pressed & DASH_IN_R1) && hm.n > 0 && sel < hm.n && hm.parts[sel].type == HP_HDL) {
			loadGameInfo(&hm.parts[sel]);
			if (hm.parts[sel].hdl_state < 0)
				dashToast(DASH_TOAST_WARN, "Could not read this HDL game's information");
		}
		if (in.pressed & DASH_IN_BACK)
			break;

		if (sel >= hm.n)
			sel = hm.n > 0 ? hm.n - 1 : 0;
		top = listCenteredTop(sel, hm.n, ROWS);
		if (sel_anim < top || sel_anim >= top + ROWS)
			sel_anim = (float)sel;
		/* Keep PFS usage polling as before; HDL title RPCs are explicit
		 * (R1 or Rename game) and never run on cursor movement. */
		if (hm.n > 0 && Timer() - last_move > 180)
			loadPfsInfo(sel);
		sel_anim = dashApproach(sel_anim, (float)sel, 20.0f * dashAnimSpeed(), dashVisualUpdate());
		dashRenderScene();
		dashGfxPresent();
	}
	/* leave the mount points and caches the way the File Browser expects */
	{
		HddJob *j = jobNew(HOP_UNMOUNT);
		if (j != NULL && runJob(j, "Closing the HDD Manager", 0))
			hddJobFree(j);
	}
	if (hm.changed)
		invalidatePartitionCaches();
	dashPopSceneOverride();
	free(hm.parts);
	memset(&hm, 0, sizeof(hm));
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_hdd.c
//--------------------------------------------------------------
