//--------------------------------------------------------------
//File name:   dash_hdd.c
//Native dashboard HDD Manager. Replaces the classic hddManager() whenever
//the dashboard runs MISC/HddManager (Tools, Devices, launch keys).
//
//* The partition list is read by the background worker; PFS usage and
//  HDL game details load for the focused partition while you browse.
//* Create / remove / rename / rename game / format / header injection
//  run on the worker behind an overlay that cannot be backed out of
//  (they must not be interrupted halfway).
//* IOP modules (HDD stack, HDL info, header source devices) are only
//  loaded on the UI thread.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "hdd_header_injector.h"
#include "hdl_rpc.h"
#include "init.h"

#define SECTORS_PER_MB 2048
#define HDD_MB 1048576
#define MAX_PART_MB (128 * 1024) /* 128 GB, as the classic manager */

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
	int pfs_state; /* 0 unknown, 1 loaded, 2 loading, -1 not mountable */
	u32 pfs_total, pfs_free;
	int hdl_state; /* 0 unknown, 1 loaded, -1 failed */
	GameInfo game;
} HddPart;

static struct
{
	int status;
	int present, formatted;
	u32 size_mb, used_mb, free_mb;
	int scanned, scanning, overflow;
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
	HOP_UNMOUNT
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
	u32 size_mb, used_mb;
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

static void scanRun(HddJob *j)
{
	iox_dirent_t de;
	int fd, i;

	j->status = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
	j->present = (hddCheckPresent() >= 0);
	if (!j->present)
		return;
	j->formatted = (hddCheckFormatted() >= 0);
	if (!j->formatted)
		return;
	{
		int sectors = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
		j->size_mb = (sectors > 0) ? (u32)sectors / SECTORS_PER_MB : 0;
	}
	j->parts = (HddPart *)malloc(sizeof(HddPart) * MAX_PARTITIONS);
	if (j->parts == NULL) {
		j->ret = -ENOMEM;
		return;
	}
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
	char dev[48], pfs[8];
	int fd, i, idx, rv;
	u32 total = 0;

	snprintf(dev, sizeof(dev), "hdd0:%s", j->a);
	fd = fileXioOpen(dev, FIO_O_RDONLY, 0);
	if (fd < 0) {
		j->ret = -1;
		return;
	}
	for (i = 0; i <= j->nsub; i++) {
		rv = fileXioIoctl2(fd, HIOCGETSIZE, &i, 4, NULL, 0);
		if (rv > 0)
			total += (u32)rv / SECTORS_PER_MB;
	}
	fileXioClose(fd);
	/* the managed mount slots, so the File Browser's bookkeeping stays true */
	idx = mountParty(dev);
	if (idx < 0) {
		j->ret = -1;
		return;
	}
	snprintf(pfs, sizeof(pfs), "pfs%d:", idx);
	{
		u32 zfree = (u32)fileXioDevctl(pfs, PDIOC_ZONEFREE, NULL, 0, NULL, 0);
		u32 zsize = (u32)fileXioDevctl(pfs, PDIOC_ZONESZ, NULL, 0, NULL, 0);
		j->pfs_free = (u32)((u64)zfree * zsize / HDD_MB);
	}
	j->pfs_total = total;
	j->ret = 0;
}

static void releaseMounts(void)
{
	unmountAll();
	unmountParty(0);
	unmountParty(1);
}

static void hddJobRun(void *arg)
{
	HddJob *j = (HddJob *)arg;
	char in[48], out[48];

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
			releaseMounts();
			snprintf(in, sizeof(in), "hdd0:%s", j->a);
			snprintf(out, sizeof(out), "hdd0:%s", j->b);
			j->ret = fileXioRename(in, out);
			break;
		case HOP_RENAME_GAME:
			j->ret = HdlRenameGame(j->a, j->b);
			break;
		case HOP_FORMAT:
			releaseMounts();
			j->ret = hddFormat();
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
	int id = dashWorkerSubmit(title, DASH_JOB_VISIBLE, hddJobRun, hddJobDone, j);

	if (id < 0) {
		dashToast(DASH_TOAST_WARN, "Busy, try again in a moment");
		hddJobFree(j);
		return 0;
	}
	if (dashWorkerAwait(id, title, cancelable))
		return 1;
	j->abandoned = 1;
	return 0;
}

//--------------------------------------------------------------
// Model
//--------------------------------------------------------------
static void recomputeFree(void)
{
	hm.free_mb = (hm.size_mb > hm.used_mb) ? ((hm.size_mb - hm.used_mb) & 0x7FFFFF80) : 0;
}

static int partCmp(const void *a, const void *b)
{
	const HddPart *x = (const HddPart *)a, *y = (const HddPart *)b;

	/* system first, then PFS, then HDL games, then the rest */
	static const int order[] = {1, 2, 0, 3};
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
	hm.size_mb = j->size_mb;
	hm.used_mb = j->used_mb;
	hm.overflow = j->overflow;
	hm.scanned = 1;
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
				hm.parts[i].pfs_total = j->pfs_total;
				hm.parts[i].pfs_free = j->pfs_free;
			} else
				hm.parts[i].pfs_state = -1;
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

	if (p->type != HP_PFS || p->pfs_state != 0 || pfs_job != NULL || dashWorkerBusy())
		return;
	j = jobNew(HOP_PFS);
	if (j == NULL)
		return;
	dashStrCopy(j->a, p->name, sizeof(j->a));
	j->nsub = p->nsub;
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
static int pickSize(const char *title, int min_mb, int max_mb, int start_mb)
{
	int size = start_mb;
	const float w = 440.0f, h = 220.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	float open = 0.0f;
	DashInput in;

	if (max_mb < min_mb)
		return -1;
	dashInputFlush();
	while (1) {
		int step = 0;

		dashInputPoll(&in);
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
			size += step;
			if (size < min_mb)
				size = min_mb;
			if (size > max_mb)
				size = max_mb;
			size -= size % 128;
			if (size < min_mb)
				size = min_mb;
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
			dashTextS(x + 56, y + 18, title, DASH_FS_LABEL, DASH_C_TEXT);
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
				hx += dashDrawButton(hx, hy, DI_BTN_LEFT, NULL) + 2;
				hx += dashDrawButton(hx, hy, DI_BTN_RIGHT, NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, "128 MB", DASH_FS_SMALL, DASH_C_TEXT2) + 10;
				hx += dashDrawButton(hx, hy, -1, "L1") + 2;
				hx += dashDrawButton(hx, hy, -1, "R1") + 4;
				hx = dashTextS(hx, hy + 4.5f, "1 GB", DASH_FS_SMALL, DASH_C_TEXT2) + 10;
				hx += dashDrawButton(hx, hy, -1, "L2") + 2;
				hx += dashDrawButton(hx, hy, -1, "R2") + 4;
				hx = dashTextS(hx, hy + 4.5f, "10 GB", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				dashModalHintRow(hx, hy, dashOkIcon(), "OK", dashBackIcon(), "Cancel");
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

static void afterChange(void)
{
	hm.changed = 1;
	invalidatePartitionCaches();
	scan();
}

static void actCreate(void)
{
	char name[MAX_PART_NAME + 1], unique[MAX_PART_NAME + 1], body[160];
	int size, max_mb;
	HddJob *j;

	name[0] = '\0';
	if (!dashModalKeyboard("New partition name", name, MAX_PART_NAME - 3) || name[0] == '\0')
		return;
	if (!strncmp(name, "__", 2) &&
	    !dashModalConfirm("System partition name", "Names starting with \"__\" are reserved for the system and for PSBBN. Create it anyway?", "Create", 1))
		return;
	max_mb = hm.free_mb < MAX_PART_MB ? (int)hm.free_mb : MAX_PART_MB;
	if (max_mb < 128) {
		dashModalMessage(DI_WARNING, "HDD full", "There is not enough free space for a new partition (128 MB minimum).", NULL);
		return;
	}
	size = pickSize("Partition size", 128, max_mb, 128);
	if (size <= 0)
		return;
	uniqueName(name, unique, sizeof(unique), -1);
	snprintf(body, sizeof(body), "Create hdd0:%s with %d MB?", unique, size);
	if (!dashModalConfirm("Create partition", body, "Create", 0))
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
	if (!dashModalConfirm("Remove partition", body, "Remove", 1))
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

static void actRename(HddPart *p)
{
	char name[MAX_PART_NAME + 1], unique[MAX_PART_NAME + 1], old[MAX_PART_NAME + 1];
	HddJob *j;
	int idx = (int)(p - hm.parts);

	dashStrCopy(old, p->name, sizeof(old));
	dashStrCopy(name, p->name, sizeof(name));
	if (!dashModalKeyboard("Partition name", name, MAX_PART_NAME - 3) || name[0] == '\0' || !strcmp(name, old))
		return;
	uniqueName(name, unique, sizeof(unique), idx);
	j = jobNew(HOP_RENAME);
	if (j == NULL)
		return;
	dashStrCopy(j->a, old, sizeof(j->a));
	dashStrCopy(j->b, unique, sizeof(j->b));
	if (!runJob(j, "Renaming partition", 0))
		return;
	if (j->ret == 0) {
		dashToast(DASH_TOAST_OK, "Partition renamed");
		afterChange();
		if (findPart(unique) >= 0)
			sel = findPart(unique);
	} else
		dashModalMessage(DI_WARNING, "Partition not renamed", "The HDD refused the new name.", unique);
	hddJobFree(j);
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
	HddJob *j;

	if (console_is_PSX) {
		dashModalMessage(DI_WARNING, "Not available", "The PSX internal HDD cannot be formatted from r3LaunchELF_ATTDASH.", NULL);
		return;
	}
	if (!dashModalConfirm("Format the HDD", "Every partition, game and file on the internal HDD will be erased.", "Continue", 1))
		return;
	if (!dashModalConfirm("Are you sure?", "This erases the whole HDD and cannot be undone.", "Format", 1))
		return;
	j = jobNew(HOP_FORMAT);
	if (j == NULL)
		return;
	if (!runJob(j, "Formatting the HDD", 0))
		return;
	if (j->ret == 0)
		dashToast(DASH_TOAST_OK, "HDD formatted");
	else
		dashModalMessage(DI_WARNING, "Format failed", "The HDD could not be formatted.", NULL);
	hddJobFree(j);
	sel = top = 0;
	afterChange();
}

static void actStatus(void)
{
	char body[320], detail[48];

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
		items[i].icon = !strncmp(devs[i], "usb", 3) ? DI_USB : !strncmp(devs[i], "udpfs", 5) ? DI_NETWORK : DI_SDCARD;
		items[i].label = devs[i];
		items[i].detail = "__Headers/";
		items[i].disabled = 0;
	}
	r = dashModalMenu("Header source", "Device with a __Headers/<partition>/ folder", items, n, 0);
	if (r < 0)
		return NULL;
	dashDevicesStartForPath(devs[r]);
	return devs[r];
}

static void injectOne(HddPart *p, const char *src)
{
	char body[240];
	HddJob *j;

	snprintf(body, sizeof(body),
	         "Write the APA header of hdd0:%s from %s__Headers/%s/? Required: system.cnf; PS2 titles also icon.sys and list.ico.",
	         p->name, src, p->name);
	if (!dashModalConfirm("Inject header", body, "Inject", 1))
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
	int injected = 0, created = 0, skipped = 0, failed = 0, stopped = 0, i;
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
	if (!dashModalConfirm("Inject headers", body, "Inject", 1)) {
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
			size = pickSize("Partition size", 128, hm.free_mb < MAX_PART_MB ? (int)hm.free_mb : MAX_PART_MB, 128);
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
		if (j->ret >= 0)
			injected++;
		else
			failed++;
		hddJobFree(j);
	}
	snprintf(body, sizeof(body), "Injected: %d   Created: %d   Skipped: %d   Failed: %d%s",
	         injected, created, skipped, failed, stopped ? "   (stopped)" : "");
	dashModalMessage(failed ? DI_WARNING : DI_CHECK, "Header injection finished", body,
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

static void partitionMenu(void)
{
	HddPart *p = (hm.n > 0 && sel < hm.n) ? &hm.parts[sel] : NULL;
	DashMenuItem items[10];
	int ids[10], n = 0, r, has_cnf = 0;
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
		A_FORMAT
	};

	if (p != NULL && !system && dashWorkerAwaitAll("Finishing up"))
		has_cnf = (HddPartitionHeaderSystemCnfExists(p->name) > 0);
#define ADD(ic, lab, det, dis, id) \
	(items[n].icon = (ic), items[n].label = (lab), items[n].detail = (det), items[n].disabled = (dis), ids[n++] = (id))
	if (hm.formatted)
		ADD(DI_PLUS, "Create partition...", NULL, 0, A_CREATE);
	if (p != NULL) {
		ADD(DI_CLOSE, "Remove partition...", reserved ? "system" : NULL, system || reserved, A_REMOVE);
		ADD(DI_EDIT, "Rename partition...", NULL, system, A_RENAME);
		if (p->type == HP_HDL)
			ADD(DI_DISC, "Rename game...", NULL, 0, A_RENAME_GAME);
		ADD(DI_TEXT, "Inject APA header...", NULL, p->type != HP_PFS, A_INJECT);
		if (has_cnf)
			ADD(DI_SETTINGS, "Edit SYSTEM.CNF...", NULL, 0, A_SYSCNF);
	}
	ADD(DI_INFO, "HDD status", hddStatusShortText(hm.status), 0, A_STATUS);
	ADD(DI_REFRESH, "Refresh", NULL, 0, A_REFRESH);
	if (!console_is_PSX && hm.present)
		ADD(DI_WARNING, "Format HDD...", NULL, 0, A_FORMAT);
#undef ADD
	r = dashModalMenu("HDD Manager", p ? p->name : "hdd0:", items, n, 0);
	if (r < 0)
		return;
	switch (ids[r]) {
		case A_CREATE:
			actCreate();
			break;
		case A_REMOVE:
			actRemove(p);
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
			return DI_FOLDER;
		case HP_HDL:
			return DI_DISC;
		case HP_SYSTEM:
			return DI_CHIP;
	}
	return DI_WARNING;
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

static void drawDetails(void)
{
	float x = DET_X, y = LIST_Y - 6, w = DET_W, h = ROWS * ROW_H + 12;
	char a[48], b[48], line[96];
	HddPart *p;

	dashDrawCard(x, y, w, h, 16);
	if (hm.n == 0 || sel >= hm.n) {
		dashTextWrap(x + 16, y + 20, w - 32, 4, hm.formatted ? "No partitions." : "Format the HDD to create partitions.",
		             DASH_FS_SMALL, DASH_C_TEXT2, 19);
		return;
	}
	p = &hm.parts[sel];
	dashIcon(typeIcon(p), x + w * 0.5f - 24, y + 16, 48, DASH_C_TEXT);
	dashTextCenter(x + w * 0.5f, y + 72, p->name, DASH_FS_LABEL, DASH_C_TEXT);
	dashTextCenter(x + w * 0.5f, y + 98, typeName(p), DASH_FS_SMALL, DASH_C_ACCENT);
	fmtMB(a, sizeof(a), p->raw_mb);
	snprintf(line, sizeof(line), "Size on disk: %s", a);
	dashTextCenter(x + w * 0.5f, y + 122, line, DASH_FS_SMALL, DASH_C_TEXT2);
	y += 150;
	if (p->type == HP_PFS) {
		if (p->pfs_state == 1) {
			u32 used = p->pfs_total > p->pfs_free ? p->pfs_total - p->pfs_free : 0;
			fmtMB(a, sizeof(a), used);
			fmtMB(b, sizeof(b), p->pfs_free);
			usageBar(x + 20, y, w - 40, used, p->pfs_total, DASH_C_ACCENT);
			snprintf(line, sizeof(line), "%s used, %s free", a, b);
			dashTextCenter(x + w * 0.5f, y + 18, line, DASH_FS_CAPTION, DASH_C_TEXT2);
		} else if (p->pfs_state == -1)
			dashTextCenter(x + w * 0.5f, y, "Could not be mounted", DASH_FS_CAPTION, DASH_C_WARM);
		else {
			dashGfxBlend(DASH_BLEND_ADD);
			dashDrawSpinner(x + w * 0.5f - 60, y + 8, 6, DASH_C_ACCENT);
			dashGfxBlend(DASH_BLEND_NORMAL);
			dashText(x + w * 0.5f - 46, y, "Reading usage...", DASH_FS_CAPTION, DASH_C_TEXT3);
		}
	} else if (p->type == HP_HDL) {
		if (p->hdl_state == 1) {
			dashTextWrap(x + 16, y, w - 32, 2, p->game.Name, DASH_FS_SMALL, DASH_C_TEXT, 18);
			snprintf(line, sizeof(line), "%s  |  %s", p->game.Startup, p->game.Is_Dvd ? "DVD" : "CD");
			dashTextCenter(x + w * 0.5f, y + 44, line, DASH_FS_CAPTION, DASH_C_TEXT2);
		} else if (p->hdl_state == -1)
			dashTextCenter(x + w * 0.5f, y, "Game information unavailable", DASH_FS_CAPTION, DASH_C_TEXT3);
		else
			dashTextCenter(x + w * 0.5f, y, "Reading game information...", DASH_FS_CAPTION, DASH_C_TEXT3);
	} else if (p->type == HP_SYSTEM)
		dashTextWrap(x + 16, y, w - 32, 3, "Reserved for the system. It cannot be changed.", DASH_FS_CAPTION, DASH_C_TEXT3, 17);
	else
		dashTextWrap(x + 16, y, w - 32, 3, "Not a PFS or HDL partition; wLaunchELF cannot open it.", DASH_FS_CAPTION, DASH_C_TEXT3, 17);
}

static void hddScene(void)
{
	char a[32], b[32], c[32], line[128];
	int i;

	dashGfxBeginFrame();
	dashBgDraw();
	dashIcon(DI_HDD, 40, 20, 24, DASH_C_ACCENT);
	dashTextS(72, 20, "HDD Manager", DASH_FS_TITLE, DASH_C_TEXT);
	snprintf(line, sizeof(line), "Status: %s", hddStatusShortText(hm.status));
	dashTextRight(DASH_SAFE_R, 26, line, DASH_FS_SMALL, hm.status == 0 ? DASH_C_OK : DASH_C_WARM);

	if (hm.formatted && hm.size_mb > 0) {
		fmtMB(a, sizeof(a), hm.used_mb);
		fmtMB(b, sizeof(b), hm.free_mb);
		fmtMB(c, sizeof(c), hm.size_mb);
		usageBar(40, 62, 560, hm.used_mb, hm.size_mb, DASH_C_ACCENT);
		snprintf(line, sizeof(line), "%s used  |  %s free  |  %s  |  %d partition%s%s", a, b, c, hm.n, hm.n == 1 ? "" : "s",
		         hm.overflow ? " (list full)" : "");
		dashText(40, 78, line, DASH_FS_CAPTION, DASH_C_TEXT2);
	} else if (hm.scanned)
		dashText(40, 64, hm.present ? "The HDD is not APA formatted." : "No HDD was found.", DASH_FS_SMALL, DASH_C_WARM);

	dashDrawPanelBox(LIST_X - 6, LIST_Y - 6, LIST_W + 12, ROWS * ROW_H + 12, 14);
	if (hm.n > 0) {
		dashGfxScissor(LIST_X - 4, LIST_Y - 4, LIST_W + 8, ROWS * ROW_H + 8);
		dashDrawSelection(LIST_X, LIST_Y + (sel_anim - top) * ROW_H, LIST_W, ROW_H - 3, 8, 1.0f);
		for (i = top; i < hm.n && i < top + ROWS; i++) {
			HddPart *p = &hm.parts[i];
			float y = LIST_Y + (i - top) * ROW_H;
			DashColor col = dashItemColor(i == sel);
			const char *label = (p->type == HP_HDL && p->hdl_state == 1) ? p->game.Name : p->name;

			dashIcon(typeIcon(p), LIST_X + 8, y + 4, 15, p->type == HP_OTHER ? DASH_C_TEXT3 : col);
			dashTextFit(LIST_X + 30, y + 3, LIST_W - 110, label, DASH_FS_SMALL, col);
			fmtMB(a, sizeof(a), p->raw_mb);
			dashTextRight(LIST_X + LIST_W - 10, y + 5, a, DASH_FS_CAPTION, DASH_C_TEXT3);
		}
		dashGfxScissorReset();
		dashDrawScrollbar(LIST_X + LIST_W + 8, LIST_Y, ROWS * ROW_H, top, ROWS, hm.n);
	}
	drawDetails();
	{
		DashHint h[] = {{dashOkIcon(), NULL, "Actions"}, {-1, "L2/R2", "Page"}, {-1, "L1", "Status"}, {dashBackIcon(), NULL, "Exit"}};
		dashDrawHints(h, 4);
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
	sel_anim = 0.0f;
	/* the HDD stack loads on the UI thread */
	dashDevicesStartForPath("hdd0:");
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
	while (1) {
		int prev = sel;

		dashInputPoll(&in);
		if (in.pressed & DASH_IN_UP)
			sel--;
		if (in.pressed & DASH_IN_DOWN)
			sel++;
		if (in.pressed & DASH_IN_L2)
			sel -= ROWS;
		if (in.pressed & DASH_IN_R2)
			sel += ROWS;
		if (sel >= hm.n)
			sel = hm.n - 1;
		if (sel < 0)
			sel = 0;
		if (sel != prev) {
			dashSoundPlay(DASH_SND_CURSOR);
			last_move = Timer();
		}
		if (in.pressed & (DASH_IN_OK | DASH_IN_MENU))
			partitionMenu();
		if (in.pressed & DASH_IN_L1)
			actStatus();
		if (in.pressed & DASH_IN_BACK)
			break;

		if (sel >= hm.n)
			sel = hm.n > 0 ? hm.n - 1 : 0;
		if (sel < top)
			top = sel;
		if (sel >= top + ROWS)
			top = sel - ROWS + 1;
		/* details of the focused partition, once browsing pauses */
		if (hm.n > 0 && Timer() - last_move > 180) {
			loadPfsInfo(sel);
			loadGameInfo(&hm.parts[sel]);
		}
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
