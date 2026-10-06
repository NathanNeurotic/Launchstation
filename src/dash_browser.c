//--------------------------------------------------------------
//File name:   dash_browser.c
//Native dashboard File Browser. Replaces the classic getFilePath() browser
//whenever the dashboard opens a folder (FILES, DEVICES, Tools, "Show in
//File Browser").
//
//* Folders are read by the background worker; the list stays usable and
//  a spinner shows while a slow device answers.
//* File operations reuse the proven classic code: copy/paste (also with
//  new names), PSU, delete, rename, new folder, size, title.cfg, VMC
//  mounting, folder icons and folder dates. Their questions and progress
//  use dashboard dialogs (dashClassicUiInstall, filerTimestampUi), so
//  nothing drops to a classic screen. Which operations are offered
//  follows the classic rules (filerMenuEnables).
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "filer_shared.h"
#include "filer_actions.h"
#include "filer_copy.h"
#include "mc_card_export.h"
#include "mmce_backup.h"
#include "main_title.h"
#include "gui_sort.h"
#include "init.h"

int IsTextEditorFileType(const char *path);

#define FB_ROWS 11
#define FB_ROW_H 25.0f
#define FB_X 40.0f
#define FB_Y 92.0f
#define FB_W 560.0f

static struct
{
	char dir[MAX_PATH]; /* "" = devices, setting->Misc = built-in commands */
	FILEINFO *files;    /* listing (sorted), MAX_ENTRY */
	u8 *mark;
	int n, nmarked;
	int loading;
	int usb_input_jobs; /* outstanding USB/mass list jobs that must not overlap PAD */
	int input_rearm;    /* swallow presses accumulated while USB fileXio was active */
	int gen;
	int sel, top;
	float sel_anim;
	char want_sel[MAX_NAME]; /* select this name once the listing arrives */
	DashDevice devs[DASH_MAX_DEVICES];
	int ndevs;
	char msg[MAX_PATH]; /* result text from classic operations */
	u64 free_bytes;     /* free space of this folder's device */
	int free_valid;
	int args_loaded;    /* launch arguments loaded here, not started yet */
} fb;

/* View and sort choices last the session, like the classic browser's
 * L1 pop-up (they are the classic file_show / file_sort values). */
static int fb_show = 1; /* 0 names, 1 names + details, 2 game titles + details */
static int fb_sort = 1; /* 0 as stored, 1 name, 2 game title, 3 date */

//--------------------------------------------------------------
// Listing
//--------------------------------------------------------------
typedef struct
{
	int gen;
	char path[MAX_PATH];
	FILEINFO *files;
	int n;
	int abandoned;
	int titles; /* read the game titles (saves, PSU files) */
	int quiet_input;
	u64 free_bytes;
	int free_valid;
} FbListJob;

static int isDir(const FILEINFO *f)
{
	return (f->stats.AttrFile & sceMcFileAttrSubdir) != 0;
}

/* partition lists keep the drive's order, as in the classic browser */
static int isPartitionList(const char *dir)
{
	return (!strncmp(dir, "hdd", 3) && dir[3] >= '0' && dir[3] <= '9' && !strcmp(dir + 4, ":/")) ||
	       !strcmp(dir, "dvr_hdd0:/");
}

static int isUsbBrowsePath(const char *dir)
{
	return dir != NULL && (!strncmp(dir, "usb", 3) || !strncmp(dir, "mass", 4));
}

static void listRun(void *arg)
{
	FbListJob *j = (FbListJob *)arg;
	int i;

	dashWorkerStatus(j->path, 0, 0);
	j->n = getDir(j->path, j->files);
	/* right after the read: for the HDD this asks the partition just mounted */
	j->free_valid = j->n >= 0 ? filerFreeSpace(j->path, &j->free_bytes) : 0;
	for (i = 0; i < j->n; i++) {
		j->files[i].title[0] = '\0';
		if (j->titles && !isPartitionList(j->path)) {
			dashWorkerStatus(j->files[i].name, i, j->n);
			if (filerGetGameTitle(j->path, &j->files[i], j->files[i].title) < 0)
				j->files[i].title[0] = '\0';
		}
	}
}

static void selectName(const char *name)
{
	int i;

	for (i = 0; i < fb.n; i++)
		if (!strcmp(fb.files[i].name, name)) {
			fb.sel = i;
			return;
		}
}

static void listDone(void *arg)
{
	FbListJob *j = (FbListJob *)arg;
	int i, o = 0;

	if (!j->abandoned && j->gen == fb.gen && fb.files != NULL) {
		for (i = 0; i < j->n && o < MAX_ENTRY; i++) {
			FILEINFO *f = &j->files[i];
			if (f->name[0] == '\0' || !strcmp(f->name, ".") || !strcmp(f->name, ".."))
				continue;
			fb.files[o++] = *f;
		}
		fb.n = o;
		/* the classic order: folders, then ELF files, then the rest */
		file_show = fb_show;
		file_sort = fb_sort;
		if (fb_sort && fb.n > 1 && !isPartitionList(j->path))
			sort(fb.files, 0, fb.n - 1);
		fb.free_valid = j->free_valid;
		fb.free_bytes = j->free_bytes;
		memset(fb.mark, 0, MAX_ENTRY);
		fb.nmarked = 0;
		fb.loading = 0;
		fb.sel = 0;
		if (fb.want_sel[0])
			selectName(fb.want_sel);
		fb.want_sel[0] = '\0';
	}
	if (j->quiet_input && fb.usb_input_jobs > 0) {
		fb.usb_input_jobs--;
		if (fb.usb_input_jobs == 0)
			fb.input_rearm = 1;
	}
	free(j->files);
	free(j);
}

static int isMisc(const char *dir)
{
	return setting->Misc[0] && !strcmp(dir, setting->Misc);
}

static void listMisc(void)
{
	const char *cmds[] = {setting->Misc_PS2Disc, setting->Misc_FileBrowser, setting->Misc_PS2Browser,
	                      setting->Misc_PS2Net, setting->Misc_PS2PowerOff, setting->Misc_HddManager,
	                      setting->Misc_TextEditor, setting->Misc_Configure, setting->Misc_ShowFont,
	                      setting->Misc_Debug_Info, setting->Misc_About_uLE, setting->Misc_Show_Build_Info,
	                      setting->Misc_OSDSYS, setting->Misc_Exploit_Installer, setting->Misc_Reboot_IOP};
	int misc_len = strlen(setting->Misc);
	unsigned int i;

	fb.n = 0;
	for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		if (strncmp(cmds[i], setting->Misc, misc_len) || cmds[i][misc_len] == '\0')
			continue;
		memset(&fb.files[fb.n], 0, sizeof(FILEINFO));
		dashStrCopy(fb.files[fb.n].name, cmds[i] + misc_len, MAX_NAME);
		fb.n++;
	}
}

static void openDir(const char *dir, const char *select)
{
	FbListJob *j;
	char usb_dir[8] = "usb0:/";

	/* Match the classic browser: the unnumbered USB root is a request to
	 * discover a drive, not an alias for unit 0. Do this before submitting
	 * the folder read so driver loads and discovery stay on the UI thread. */
	if (!strcmp(dir, "usb:") || !strcmp(dir, "usb:/") ||
	    !strcmp(dir, "mass:") || !strcmp(dir, "mass:/")) {
		int unit;

		if (!dashWorkerAwaitAll("Finishing up"))
			return;
		dashLoadingBegin("Finding USB storage");
		unit = prepareUsbRootBrowse();
		dashLoadingEnd();
		if (unit < 0) {
			dashToast(DASH_TOAST_WARN, "No USB storage device was found");
			if (fb.dir[0] == '\0')
				openDir("", NULL);
			return;
		}
		usb_dir[3] = '0' + unit; /* prepareUsbRootBrowse returns units 0 or 1 */
		dir = usb_dir;
	}

	fb.gen++;
	fb.n = 0;
	fb.nmarked = 0;
	fb.free_valid = 0;
	fb.input_rearm = 0;
	fb.sel = fb.top = 0;
	fb.sel_anim = 0.0f;
	fb.want_sel[0] = '\0';
	if (select != NULL)
		dashStrCopy(fb.want_sel, select, sizeof(fb.want_sel));
	dashStrCopy(fb.dir, dir, sizeof(fb.dir));
	if (dir[0] == '\0') {
		fb.ndevs = dashDevicesList(fb.devs, DASH_MAX_DEVICES - 3);
		/* memory card images mounted from the menu */
		{
			int v;
			for (v = 0; v < 2; v++) {
				DashDevice *d;
				if (!vmcMounted[v])
					continue;
				d = &fb.devs[fb.ndevs++];
				memset(d, 0, sizeof(*d));
				d->icon = DI_MC;
				d->state = DASH_DEV_READY;
				snprintf(d->name, sizeof(d->name), "Memory card image %d", v);
				snprintf(d->browse, sizeof(d->browse), "vmc%d:/", v);
				dashStrCopy(d->status, "VMC", sizeof(d->status));
			}
		}
		/* the built-in commands, like the classic device list */
		if (setting->Misc[0]) {
			DashDevice *d = &fb.devs[fb.ndevs++];
			memset(d, 0, sizeof(*d));
			d->icon = DI_TOOLS;
			d->state = DASH_DEV_READY;
			dashStrCopy(d->name, "Built-in commands", sizeof(d->name));
			dashStrCopy(d->browse, setting->Misc, sizeof(d->browse));
			dashStrCopy(d->status, "MISC", sizeof(d->status));
		}
		fb.loading = 0;
		if (select != NULL) {
			int i;
			for (i = 0; i < fb.ndevs; i++)
				if (!strcmp(fb.devs[i].browse, select))
					fb.sel = i;
		}
		return;
	}
	if (isMisc(dir)) {
		listMisc();
		fb.loading = 0;
		if (select != NULL)
			selectName(select);
		return;
	}
	/* drivers and the HDD partition list are set up here on the UI thread;
	 * the worker only reads (it must never load IOP modules) */
	if (filerDirReadNeedsPrep(dir)) {
		if (!dashWorkerAwaitAll("Finishing up"))
			return;
		dashLoadingBegin("Starting device");
		filerPrepareDirRead(dir);
		dashLoadingEnd();
	}
	/* the classic browser remembers where it was: "Last folder" in FILES */
	if (strlen(dir) < MAX_NAME)
		dashStrCopy(LastDir, dir, MAX_NAME);
	j = (FbListJob *)malloc(sizeof(FbListJob));
	if (j != NULL)
		j->files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	if (j == NULL || j->files == NULL) {
		free(j);
		fb.loading = 0;
		dashToast(DASH_TOAST_ERROR, "Not enough memory to read this folder");
		return;
	}
	j->gen = fb.gen;
	j->n = 0;
	j->abandoned = 0;
	j->titles = (fb_show == 2 || fb_sort == 2);
	j->quiet_input = isUsbBrowsePath(dir);
	j->free_valid = 0;
	dashStrCopy(j->path, dir, sizeof(j->path));
	fb.loading = 1;
	if (dashWorkerSubmit("Reading folder", DASH_JOB_VISIBLE, listRun, listDone, j) < 0) {
		free(j->files);
		free(j);
		fb.loading = 0;
		dashToast(DASH_TOAST_WARN, "Busy, try again in a moment");
	} else if (j->quiet_input) {
		fb.usb_input_jobs++;
	}
}

static void refresh(void)
{
	char keep[MAX_NAME];

	keep[0] = '\0';
	if (fb.dir[0] && fb.n > 0 && fb.sel < fb.n)
		dashStrCopy(keep, fb.files[fb.sel].name, sizeof(keep));
	openDir(fb.dir, keep[0] ? keep : NULL);
}

/* "mc0:/APPS/FOO/" -> "mc0:/APPS/", "mc0:/" -> "" (devices); the folder
 * left is returned in `child` so it can be selected again */
static void parentOf(const char *dir, char *out, int size, char *child, int child_size)
{
	int len;
	char *colon;

	dashStrCopy(out, dir, size);
	child[0] = '\0';
	if (isMisc(out)) {
		out[0] = '\0';
		dashStrCopy(child, setting->Misc, child_size);
		return;
	}
	colon = strchr(out, ':');
	if (colon == NULL || colon[1] == '\0' || (colon[1] == '/' && colon[2] == '\0')) {
		dashStrCopy(child, dir, child_size);
		out[0] = '\0';
		return;
	}
	len = strlen(out);
	if (len > 0 && out[len - 1] == '/')
		out[--len] = '\0';
	while (len > 0 && out[len - 1] != '/') {
		len--;
	}
	dashStrCopy(child, out + len, child_size);
	out[len] = '\0';
}

//--------------------------------------------------------------
// Opening things
//--------------------------------------------------------------
static int fullPath(const FILEINFO *f, char *out, int size)
{
	return snprintf(out, size, "%s%s%s", fb.dir, f->name, isDir(f) ? "/" : "") < size;
}

static void launchPath(const char *path, int with_args)
{
	DashApp tmp;

	/* arguments loaded with "Use as launch arguments" go with this start */
	if (!with_args && LaunchArgsPending())
		with_args = DASH_LAUNCH_PENDING_ARGS;
	dashLaunchApp(dashLibResolve(path, &tmp), with_args);
	fb.args_loaded = LaunchArgsPending(); /* only still set when the start failed */
}

static void activate(void)
{
	char path[MAX_PATH];
	FILEINFO *f;

	if (fb.dir[0] == '\0') {
		DashDevice *d;
		if (fb.sel >= fb.ndevs)
			return;
		d = &fb.devs[fb.sel];
		if (d->icon == DI_MC && d->state == DASH_DEV_ABSENT) {
			dashToast(DASH_TOAST_INFO, "Insert a memory card into this slot");
			return;
		}
		if (d->icon == DI_DISC && d->state != DASH_DEV_READY) {
			dashToast(DASH_TOAST_INFO, "Insert a disc");
			return;
		}
		/* openDir("") rebuilds fb.devs, so keep a copy of the target */
		dashStrCopy(path, d->browse, sizeof(path));
		if (d->state == DASH_DEV_PROBE || d->state == DASH_DEV_ABSENT) {
			int mask, unit;

			if (dashDevicesStart(d, NULL, NULL) < 0)
				return;
			if (!dashDevicePathReady(path)) {
				openDir("", path); /* the list shows the new state */
				dashToast(DASH_TOAST_WARN, "Device not found");
				return;
			}
			/* The USB probe has already discovered the units. Open its first
			 * available unit directly instead of rescanning the generic root. */
			if (!strcmp(path, "usb:/")) {
				mask = filerUsbDriveMask();
				for (unit = 0; unit < 10; unit++)
					if (mask & (1 << unit))
						break;
				if (unit < 10)
					snprintf(path, sizeof(path), "usb%d:/", unit);
			}
		}
		openDir(path, NULL);
		return;
	}
	if (fb.loading || fb.sel >= fb.n)
		return;
	f = &fb.files[fb.sel];
	if (isMisc(fb.dir)) {
		snprintf(path, sizeof(path), "%s%s", setting->Misc, f->name);
		dashRunMisc(path);
		return;
	}
	if (!fullPath(f, path, sizeof(path))) {
		dashToast(DASH_TOAST_WARN, "Path too long");
		return;
	}
	if (isDir(f)) {
		openDir(path, NULL);
		return;
	}
	if (IsTextEditorFileType(path)) {
		dashOpenTextEditor(path);
		refresh();
		return;
	}
	/* ELF/KELF, POPStarter VCD...: the proven launch path */
	if (genCmpFileExt(path, "ELF") || genCmpFileExt(path, "XLF") || genCmpFileExt(path, "KELF") ||
	    genCmpFileExt(path, "VCD")) {
		launchPath(path, 0);
		return;
	}
	dashModalMessage(DI_FILE, f->name, "wLaunchELF cannot open this type of file. Use the menu to copy, move or rename it.", path);
}

//--------------------------------------------------------------
// Operations (classic code, dashboard dialogs)
//--------------------------------------------------------------
static int prepareOp(void)
{
	/* classic file operations run on the UI thread with the worker idle */
	if (!dashWorkerAwaitAll("Finishing up"))
		return 0;
	filerRootMcPollIdle();
	return 1;
}

static void opDone(void)
{
	dashClassicUiDone();
	dashLoadingEnd();
}

/* the entries an operation applies to: the marked ones, or the focused */
static int targets(int *idx, int max)
{
	int i, n = 0;

	if (fb.nmarked > 0) {
		for (i = 0; i < fb.n && n < max; i++)
			if (fb.mark[i])
				idx[n++] = i;
	} else if (fb.sel < fb.n)
		idx[n++] = fb.sel;
	return n;
}

static void opCopy(int cut)
{
	static int idx[MAX_ENTRY];
	int n = targets(idx, MAX_ENTRY), i;

	if (n == 0)
		return;
	if (cut) {
		for (i = 0; i < n; i++)
			if (filerIsExploitProtectedPath(fb.dir, &fb.files[idx[i]]) &&
			    filerConfirmExploitModify(fb.dir, &fb.files[idx[i]]) < 0)
				return;
	}
	dashStrCopy(clipPath, fb.dir, MAX_PATH);
	for (i = 0; i < n; i++)
		clipFiles[i] = fb.files[idx[i]];
	nclipFiles = n;
	browser_cut = cut;
	clipIopResetGeneration = getIopResetGeneration();
	memset(fb.mark, 0, MAX_ENTRY);
	fb.nmarked = 0;
	{
		char msg[64];
		snprintf(msg, sizeof(msg), "%d item%s ready to %s", n, n == 1 ? "" : "s", cut ? "move" : "paste");
		dashToast(DASH_TOAST_INFO, msg);
	}
}

enum {
	PASTE_NORMAL = 0,
	PASTE_RENAME, /* asks for a new name for each item */
	PASTE_PSU
};

static void opPaste(int mode)
{
	int psu = (mode == PASTE_PSU);

	if (nclipFiles <= 0) {
		dashToast(DASH_TOAST_INFO, "The clipboard is empty");
		return;
	}
	if (!prepareOp())
		return;
	if (filerConfirmExploitModify(fb.dir, NULL) <= 0)
		return;
	fb.msg[0] = '\0';
	dashLoadingBegin(psu ? "PSU" : (browser_cut ? "Moving" : "Pasting"));
	if (psu)
		filerPsuPasteInto(fb.msg, fb.dir);
	else
		filerPasteModeInto(fb.msg, fb.dir, mode == PASTE_RENAME);
	opDone();
	if (fb.msg[0]) /* the classic paste only leaves a message when it failed */
		dashModalMessage(DI_WARNING, psu ? "PSU failed" : "Paste failed", "Not everything could be pasted. The destination may be full or read-only.", fb.msg);
	else
		dashToast(DASH_TOAST_OK, browser_cut ? "Moved" : "Pasted");
	refresh();
}

static void opDelete(void)
{
	static int idx[MAX_ENTRY];
	int n = targets(idx, MAX_ENTRY), i, ret = 0, done = 0;
	char body[MAX_NAME + 64];

	if (n == 0)
		return;
	if (n == 1)
		snprintf(body, sizeof(body), "Delete %s%s? This cannot be undone.", fb.files[idx[0]].name, isDir(&fb.files[idx[0]]) ? "/ and everything in it" : "");
	else
		snprintf(body, sizeof(body), "Delete the %d selected items? This cannot be undone.", n);
	if (!dashModalConfirm("Delete", body, "Delete", 1))
		return;
	if (!prepareOp())
		return;
	dashLoadingBegin("Deleting");
	for (i = 0; i < n; i++) {
		FILEINFO *f = &fb.files[idx[i]];
		if (filerIsExploitProtectedPath(fb.dir, f) && filerConfirmExploitDelete(fb.dir, f) < 0)
			continue;
		dashLoadingStatus(f->name, i, n);
		ret = delete (fb.dir, f);
		if (ret < 0)
			break;
		done++;
	}
	opDone();
	if (ret < 0) {
		char d[32];
		snprintf(d, sizeof(d), "error %d", ret);
		dashModalMessage(DI_WARNING, "Delete failed", "Not everything could be deleted.", d);
	} else {
		char msg[48];
		snprintf(msg, sizeof(msg), "%d item%s deleted", done, done == 1 ? "" : "s");
		dashToast(DASH_TOAST_OK, msg);
	}
	refresh();
}

static void opRename(void)
{
	FILEINFO *f;
	char name[MAX_NAME];

	if (fb.sel >= fb.n)
		return;
	f = &fb.files[fb.sel];
	if (filerIsExploitProtectedPath(fb.dir, f)) {
		dashModalMessage(DI_WARNING, "Protected folder", "This system folder cannot be renamed from here.", f->name);
		return;
	}
	dashStrCopy(name, f->name, sizeof(name));
	if (!dashModalKeyboard("New name", name, 36) || name[0] == '\0' || !strcmp(name, f->name))
		return;
	if (!prepareOp())
		return;
	if (Rename(fb.dir, f, name) < 0)
		dashModalMessage(DI_WARNING, "Rename failed", "The name may be in use or not allowed on this device.", name);
	else {
		dashStrCopy(fb.want_sel, name, sizeof(fb.want_sel));
		openDir(fb.dir, name);
	}
	opDone();
}

static void opNewFolder(void)
{
	char name[MAX_NAME];
	int ret;

	name[0] = '\0';
	if (!dashModalKeyboard("Folder name", name, 36) || name[0] == '\0')
		return;
	if (!prepareOp())
		return;
	if (filerConfirmExploitModify(fb.dir, NULL) <= 0)
		return;
	ret = newdir(fb.dir, name);
	opDone();
	if (ret == -17)
		dashModalMessage(DI_WARNING, "Folder exists", "A folder with that name already exists.", name);
	else if (ret < 0)
		dashModalMessage(DI_WARNING, "Folder not created", "The device may be read-only or full.", name);
	else
		openDir(fb.dir, name);
}

/* getFileSize() recurses with a MAX_ENTRY listing on the stack and may
 * start drivers, so it runs here on the UI thread (big stack), not on the
 * worker */
static void opSize(void)
{
	static int idx[MAX_ENTRY];
	int n = targets(idx, MAX_ENTRY), i;
	u64 total = 0;
	char body[96];

	if (n == 0 || !prepareOp())
		return;
	dashLoadingBegin("Measuring size");
	for (i = 0; i < n; i++) {
		u64 s;
		dashLoadingStatus(fb.files[idx[i]].name, i, n);
		s = getFileSize(fb.dir, &fb.files[idx[i]]);
		if ((s64)s > 0)
			total += s;
	}
	opDone();
	if (total >= (u64)1024 * 1024 * 1024)
		snprintf(body, sizeof(body), "%.2f GB  (%llu bytes)", (double)total / (1024.0 * 1024.0 * 1024.0), (unsigned long long)total);
	else if (total >= 1024 * 1024)
		snprintf(body, sizeof(body), "%.2f MB  (%llu bytes)", (double)total / (1024.0 * 1024.0), (unsigned long long)total);
	else
		snprintf(body, sizeof(body), "%llu bytes", (unsigned long long)total);
	dashModalMessage(DI_INFO, n == 1 ? fb.files[idx[0]].name : "Selected items", body, NULL);
}

static void opTitleCfg(void)
{
	if (fb.sel >= fb.n || !prepareOp())
		return;
	fb.msg[0] = '\0';
	make_title_cfg(fb.dir, &fb.files[fb.sel], fb.msg);
	opDone();
	if (fb.msg[0])
		dashToast(DASH_TOAST_INFO, fb.msg);
	refresh();
}

/* the image extensions offered for "Mount as vmc0:/vmc1:" */
static int isVmcImage(const char *name)
{
	return genCmpFileExt(name, "BIN") || genCmpFileExt(name, "VMC") || genCmpFileExt(name, "MC2") ||
	       genCmpFileExt(name, "PS2");
}

static void opMountVmc(int slot)
{
	char dev[8];

	if (fb.sel >= fb.n || !prepareOp())
		return;
	fb.msg[0] = '\0';
	dashLoadingBegin("Mounting");
	if (filerMountVmc(fb.dir, &fb.files[fb.sel], slot, fb.msg, sizeof(fb.msg)) < 0) {
		opDone();
		dashModalMessage(DI_WARNING, "Not mounted", fb.msg, fb.files[fb.sel].name);
		return;
	}
	opDone();
	snprintf(dev, sizeof(dev), "vmc%d:/", slot);
	dashToast(DASH_TOAST_OK, "Mounted: the image is open as a memory card");
	openDir(dev, NULL);
}

static void cardExportProgress(int phase, u64 done, u64 total)
{
	int scaled = total ? (int)((done * 1000) / total) : 0;
	dashLoadingStatus(phase == MC_CARD_EXPORT_VERIFY ? "Verifying memory card image" : "Writing memory card image", scaled, 1000);
}


static int dashBcdToInt(u8 value)
{
	return ((value >> 4) * 10) + (value & 0x0F);
}

static int activeBackupStamp(char *out, size_t out_size)
{
	sceCdCLOCK clock;
	int year, month, day, hour, minute, second;

	if (out == NULL || out_size == 0)
		return 0;

	memset(&clock, 0, sizeof(clock));
	if (sceCdReadClock(&clock) == 0 || clock.stat != 0) {
		/* A missing/invalid RTC should not prevent a backup. Timer() is only
		 * a per-boot fallback; uniqueness is still enforced against the folder. */
		snprintf(out, out_size, "00000000-000000-S%010llu", (unsigned long long)(Timer() / 1000));
		return 1;
	}

	year = 2000 + dashBcdToInt(clock.year);
	month = dashBcdToInt(clock.month & 0x7F);
	day = dashBcdToInt(clock.day);
	hour = dashBcdToInt(clock.hour);
	minute = dashBcdToInt(clock.minute);
	second = dashBcdToInt(clock.second);
	menuTitleConvertTimestampToLocalTime(&year, &month, &day, &hour, &minute, &second);
	snprintf(out, out_size, "%04d%02d%02d-%02d%02d%02d",
	         year, month, day, hour, minute, second);
	return 1;
}

static int activeHistoryMatch(int unit, const char *name)
{
	char prefix[32];
	const char *dot;

	if (name == NULL)
		return 0;
	snprintf(prefix, sizeof(prefix), "MMCE%d-ActiveCard-", unit);
	if (strncmp(name, prefix, strlen(prefix)) != 0)
		return 0;
	dot = strrchr(name, '.');
	return dot != NULL && !stricmp(dot, ".mcd");
}

static int joinBackupPath(char *out, size_t out_size, const char *folder, const char *name)
{
	int len;

	if (out == NULL || out_size == 0 || folder == NULL || name == NULL)
		return 0;
	len = strlen(folder);
	if (len > 0 && folder[len - 1] != '/' && folder[len - 1] != ':')
		return snprintf(out, out_size, "%s/%s", folder, name) < (int)out_size;
	return snprintf(out, out_size, "%s%s", folder, name) < (int)out_size;
}

static int activeStampedName(int unit, const char *folder, const char *ext,
                             char *name, size_t name_size)
{
	char stamp[32], candidate[MAX_PATH];
	int suffix;

	if (!activeBackupStamp(stamp, sizeof(stamp)) || ext == NULL || ext[0] == '\0')
		return 0;

	for (suffix = 0; suffix < 100; suffix++) {
		if (suffix == 0)
			snprintf(name, name_size, "MMCE%d-ActiveCard-%s.%s", unit, stamp, ext);
		else
			snprintf(name, name_size, "MMCE%d-ActiveCard-%s-%02d.%s", unit, stamp, suffix, ext);
		if (!joinBackupPath(candidate, sizeof(candidate), folder, name))
			return 0;
		if (!wleExists(candidate))
			return 1;
	}
	return 0;
}

static int activeHistoryName(int unit, const char *folder, char *name, size_t name_size)
{
	return activeStampedName(unit, folder, "mcd", name, name_size);
}

static int activeHistoryScan(const char *folder, int unit, char *oldest, size_t oldest_size)
{
	iox_dirent_t ent;
	char path[MAX_PATH];
	int fd, count = 0;

	if (oldest != NULL && oldest_size > 0)
		oldest[0] = '\0';
	dashStrCopy(path, folder, sizeof(path));
	fd = genDopen(path);
	if (fd < 0)
		return -1;

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (!activeHistoryMatch(unit, ent.name))
			continue;
		count++;
		if (oldest != NULL && oldest_size > 0 &&
		    (oldest[0] == '\0' || strcmp(ent.name, oldest) < 0))
			dashStrCopy(oldest, ent.name, oldest_size);
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return count;
}

static int activeHistoryNewestOther(const char *folder, int unit, const char *exclude,
                                    char *newest, size_t newest_size)
{
	iox_dirent_t ent;
	char path[MAX_PATH];
	int fd;

	if (newest == NULL || newest_size == 0)
		return -1;
	newest[0] = '\0';
	dashStrCopy(path, folder, sizeof(path));
	fd = genDopen(path);
	if (fd < 0)
		return -1;

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (!activeHistoryMatch(unit, ent.name) ||
		    (exclude != NULL && !strcmp(ent.name, exclude))) {
			memset(&ent, 0, sizeof(ent));
			continue;
		}
		if (newest[0] == '\0' || strcmp(ent.name, newest) > 0)
			dashStrCopy(newest, ent.name, newest_size);
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return newest[0] != '\0' ? 1 : 0;
}

static int activeHistoryFilesEqual(const char *a, const char *b)
{
	static u8 buf_a[16384] __attribute__((aligned(64)));
	static u8 buf_b[16384] __attribute__((aligned(64)));
	int fa = -1, fbk = -1, result = -1;
	s64 size_a, size_b;

	fa = genOpen(a, FIO_O_RDONLY);
	if (fa < 0)
		goto done;
	fbk = genOpen(b, FIO_O_RDONLY);
	if (fbk < 0)
		goto done;

	size_a = genLseek(fa, 0, SEEK_END);
	size_b = genLseek(fbk, 0, SEEK_END);
	if (size_a < 0 || size_b < 0)
		goto done;
	if (size_a != size_b) {
		result = 0;
		goto done;
	}
	if (genLseek(fa, 0, SEEK_SET) != 0 || genLseek(fbk, 0, SEEK_SET) != 0)
		goto done;

	result = 1;
	while (size_a > 0) {
		int want = size_a > (s64)sizeof(buf_a) ? (int)sizeof(buf_a) : (int)size_a;
		int ra = genRead(fa, buf_a, want);
		int rb = genRead(fbk, buf_b, want);
		if (ra != want || rb != want) {
			result = -1;
			break;
		}
		if (memcmp(buf_a, buf_b, want) != 0) {
			result = 0;
			break;
		}
		size_a -= want;
	}

done:
	if (fbk >= 0)
		genClose(fbk);
	if (fa >= 0)
		genClose(fa);
	return result;
}

static int activeHistoryPrune(const char *folder, int unit, int keep)
{
	char oldest[MAX_NAME], path[MAX_PATH];
	int count, guard = 0;

	while (guard++ < 256) {
		count = activeHistoryScan(folder, unit, oldest, sizeof(oldest));
		if (count < 0)
			return -1;
		if (keep == 0 || count <= keep)
			return 0;
		if (oldest[0] == '\0' || !joinBackupPath(path, sizeof(path), folder, oldest))
			return -1;
		if (genRemove(path) < 0)
			return -1;
	}
	return -1;
}

static int mmceUnitFromPath(const char *path)
{
	if (path == NULL || strncmp(path, "mmce", 4) != 0 ||
	    path[4] < '0' || path[4] > '1' || path[5] != ':')
		return -1;
	return path[4] - '0';
}

static void mmceTreeBackupProgress(const char *source, int done, int total)
{
	char line[112];
	const char *name = source != NULL ? strrchr(source, '/') : NULL;

	if (name != NULL)
		name++;
	else if (source != NULL)
		name = source;
	else
		name = "card";

	snprintf(line, sizeof(line), "MMCE cards %d/%d: %s", done, total, name);
	dashLoadingStatus(line, done, total > 0 ? total : 1);
}

static void opBackupAllMmceCards(void)
{
	MmceBackupStats stats;
	char source[MAX_PATH], folder[MAX_PATH], msg[192];
	int unit = mmceUnitFromPath(fb.dir);
	int r, prep;

	if (unit < 0)
		return;

	snprintf(source, sizeof(source), "mmce%d:/MemoryCards", unit);
	dashStrCopy(folder, fb.dir, sizeof(folder));
	if (!dashPickPath("Back up all MMCE cards to", folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;
	if (!prepareOp())
		return;

	prep = prepareTransferDeviceStacks(source, folder);
	if (prep != TRANSFER_STACK_READY) {
		dashModalMessage(DI_WARNING, "Cannot back up there",
		                 prep == TRANSFER_STACK_INCOMPATIBLE ?
		                     "The MMCE source and destination require device stacks that cannot stay active together." :
		                     "The MMCE source or destination device could not be started.",
		                 folder);
		return;
	}

	dashLoadingBegin("Backing up all MMCE cards");
	{
		MmceBackupOptions options = {dashCfg.mmce_backup_keep, dashCfg.mmce_backup_format};
		r = mmceBackupAllCards(unit, folder, &options, &stats, mmceTreeBackupProgress);
	}
	dashLoadingEnd();

	if (r < 0) {
		dashModalMessage(DI_WARNING, "MMCE card backup failed",
		                 "The MemoryCards tree could not be read or the selected destination is not safe for this backup.",
		                 folder);
		return;
	}

	if (stats.candidates == 0) {
		dashToast(DASH_TOAST_INFO, "No .mcd card images found under MemoryCards");
		return;
	}

	snprintf(msg, sizeof(msg), "%d changed backed up, %d unchanged, %d failed (%d card images found).",
	         stats.backed_up, stats.unchanged, stats.failed, stats.candidates);
	if (stats.failed)
		dashModalMessage(DI_WARNING, "MMCE backup completed with errors", msg,
		                 "Verified successful backups were kept; failed card images were not.");
	else
		dashToast(DASH_TOAST_OK, msg);
	refresh();
}

static void opBackupActiveMmce(void)
{
	char source[MAX_PATH], folder[MAX_PATH], dest[MAX_PATH], name[40];
	int unit = mmceUnitFromPath(fb.dir);
	int r, prep;

	if (unit < 0)
		return;

	snprintf(source, sizeof(source), "mmce%d:/__active_card__.mcd", unit);
	dashStrCopy(folder, fb.dir, sizeof(folder));
	if (!dashPickPath("Back up active MMCE card to", folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;

	if (!activeHistoryName(unit, folder, name, sizeof(name)) ||
	    !joinBackupPath(dest, sizeof(dest), folder, name)) {
		dashToast(DASH_TOAST_WARN, "Could not create a unique backup history name");
		return;
	}
	if (!prepareOp())
		return;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep != TRANSFER_STACK_READY) {
		dashModalMessage(DI_WARNING, "Cannot back up there",
		                 prep == TRANSFER_STACK_INCOMPATIBLE ?
		                     "The source and destination require device stacks that cannot stay active together." :
		                     "The source or destination device could not be started.",
		                 dest);
		return;
	}

	dashLoadingBegin("Backing up active MMCE card");
	r = mcCardBackupActiveMmce(source, dest, cardExportProgress);
	dashLoadingEnd();

	if (r == MC_CARD_EXPORT_OK) {
		char previous_name[MAX_NAME], previous_path[MAX_PATH];
		int newest = activeHistoryNewestOther(folder, unit, name, previous_name, sizeof(previous_name));
		int same = -1;

		if (newest > 0 && joinBackupPath(previous_path, sizeof(previous_path), folder, previous_name))
			same = activeHistoryFilesEqual(dest, previous_path);

		if (same == 1) {
			if (genRemove(dest) < 0)
				dashToast(DASH_TOAST_WARN, "Card is unchanged, but the duplicate backup could not be removed");
			else if (activeHistoryPrune(folder, unit, dashCfg.mmce_backup_keep) < 0)
				dashToast(DASH_TOAST_WARN, "Card unchanged; duplicate removed, but old history could not be pruned");
			else
				dashToast(DASH_TOAST_INFO, "Active MMCE card unchanged; no duplicate history saved");
		} else if (activeHistoryPrune(folder, unit, dashCfg.mmce_backup_keep) < 0) {
			dashToast(DASH_TOAST_WARN, "Backup verified, but old history could not be pruned");
		} else {
			dashToast(DASH_TOAST_OK, "Active MMCE card changed; verified backup added to history");
		}
		refresh();
	} else if (r == MC_CARD_EXPORT_FORMAT) {
		dashModalMessage(DI_WARNING, "Not backed up",
		                 "The active MMCE backing file is not a valid PS2 memory-card image.", source);
	} else if (r == MC_CARD_EXPORT_VERIFY_FAILED) {
		dashModalMessage(DI_WARNING, "Verification failed",
		                 "The backup did not match the active card and was removed.", dest);
	} else {
		dashModalMessage(DI_WARNING, "Active-card backup failed",
		                 "The exact active backing image could not be read, or the destination could not be written. Any incomplete backup was removed.",
		                 dest);
	}
}

static void opExportActiveMmcePcsx2(void)
{
	char source[MAX_PATH], folder[MAX_PATH], dest[MAX_PATH], name[48];
	int unit = mmceUnitFromPath(fb.dir);
	int r, prep;

	if (unit < 0)
		return;

	snprintf(source, sizeof(source), "mmce%d:/__active_card__.mcd", unit);
	dashStrCopy(folder, fb.dir, sizeof(folder));
	if (!dashPickPath("Export active MMCE card as PCSX2 .ps2 to",
	                  folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;

	if (!activeStampedName(unit, folder, "ps2", name, sizeof(name)) ||
	    !joinBackupPath(dest, sizeof(dest), folder, name)) {
		dashToast(DASH_TOAST_WARN, "Could not create a unique PCSX2 export name");
		return;
	}
	if (!prepareOp())
		return;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep != TRANSFER_STACK_READY) {
		dashModalMessage(DI_WARNING, "Cannot export there",
		                 prep == TRANSFER_STACK_INCOMPATIBLE ?
		                     "The source and destination require device stacks that cannot stay active together." :
		                     "The source or destination device could not be started.",
		                 dest);
		return;
	}

	dashLoadingBegin("Exporting active MMCE card to PCSX2");
	r = mcCardExportActiveMmcePcsx2(source, dest, cardExportProgress);
	dashLoadingEnd();

	if (r == MC_CARD_EXPORT_OK) {
		dashToast(DASH_TOAST_OK, "Active MMCE card exported to verified PCSX2 .ps2");
		refresh();
	} else if (r == MC_CARD_EXPORT_FORMAT) {
		dashModalMessage(DI_WARNING, "Not exported",
		                 "The active MMCE backing file is not a valid PS2 memory-card image.", source);
	} else if (r == MC_CARD_EXPORT_VERIFY_FAILED) {
		dashModalMessage(DI_WARNING, "Verification failed",
		                 "The PCSX2 export did not match the active card conversion and was removed.", dest);
	} else {
		dashModalMessage(DI_WARNING, "Active-card export failed",
		                 "The active card could not be read or the PCSX2 image could not be written. Any incomplete export was removed.",
		                 dest);
	}
}

static void opBackupCard(void)
{
	FILEINFO *f;
	char source[MAX_PATH], folder[MAX_PATH], dest[MAX_PATH];
	int r, prep;

	if (fb.sel >= fb.n)
		return;
	f = &fb.files[fb.sel];
	if (isDir(f) || !genCmpFileExt(f->name, "MCD") || !fullPath(f, source, sizeof(source)))
		return;

	dashStrCopy(folder, fb.dir, sizeof(folder));
	if (!dashPickPath("Back up memory card to", folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;
	if (snprintf(dest, sizeof(dest), "%s%s", folder, f->name) >= (int)sizeof(dest)) {
		dashToast(DASH_TOAST_WARN, "Destination path is too long");
		return;
	}
	if (!strcmp(source, dest)) {
		dashModalMessage(DI_WARNING, "Choose another folder",
		                 "The backup destination is the source card image itself.", source);
		return;
	}
	if (wleExists(dest) && !dashModalConfirm("Replace existing card backup?", dest, "Replace", 1))
		return;
	if (!prepareOp())
		return;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep != TRANSFER_STACK_READY) {
		dashModalMessage(DI_WARNING, "Cannot back up there",
		                 prep == TRANSFER_STACK_INCOMPATIBLE ?
		                     "The source and destination require device stacks that cannot stay active together." :
		                     "The source or destination device could not be started.",
		                 dest);
		return;
	}

	dashLoadingBegin("Backing up memory card");
	r = mcCardBackupNoEcc(source, dest, cardExportProgress);
	dashLoadingEnd();

	if (r == MC_CARD_EXPORT_OK) {
		dashToast(DASH_TOAST_OK, "Native .mcd backup written and verified");
		refresh();
	} else if (r == MC_CARD_EXPORT_FORMAT) {
		dashModalMessage(DI_WARNING, "Not backed up",
		                 "This is not a valid no-ECC PS2 memory-card image.", source);
	} else if (r == MC_CARD_EXPORT_SAME_PATH) {
		dashModalMessage(DI_WARNING, "Choose another folder",
		                 "The backup destination is the source card image itself.", source);
	} else if (r == MC_CARD_EXPORT_VERIFY_FAILED) {
		dashModalMessage(DI_WARNING, "Verification failed",
		                 "The backup did not match the source and was removed.", dest);
	} else {
		dashModalMessage(DI_WARNING, "Backup failed",
		                 "The source could not be read or the destination could not be written. Any incomplete backup was removed.",
		                 dest);
	}
}

static void opConvertCard(int to_pcsx2)
{
	FILEINFO *f;
	char source[MAX_PATH], folder[MAX_PATH], dest[MAX_PATH], name[MAX_NAME], *dot;
	const char *want_ext = to_pcsx2 ? "MCD" : "PS2";
	const char *new_ext = to_pcsx2 ? ".ps2" : ".mcd";
	int r, prep;

	if (fb.sel >= fb.n)
		return;
	f = &fb.files[fb.sel];
	if (isDir(f) || !genCmpFileExt(f->name, want_ext) || !fullPath(f, source, sizeof(source)))
		return;

	dashStrCopy(folder, fb.dir, sizeof(folder));
	if (!dashPickPath(to_pcsx2 ? "Export PCSX2 memory card to" : "Convert PCSX2 card to .mcd in",
	                  folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;

	dashStrCopy(name, f->name, sizeof(name));
	dot = strrchr(name, '.');
	if (dot == NULL)
		return;
	strcpy(dot, new_ext);
	if (snprintf(dest, sizeof(dest), "%s%s", folder, name) >= (int)sizeof(dest)) {
		dashToast(DASH_TOAST_WARN, "Destination path is too long");
		return;
	}
	if (wleExists(dest) && !dashModalConfirm("Replace existing card image?", dest, "Replace", 1))
		return;
	if (!prepareOp())
		return;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep != TRANSFER_STACK_READY) {
		dashModalMessage(DI_WARNING, "Cannot convert there",
		                 prep == TRANSFER_STACK_INCOMPATIBLE ?
		                     "The source and destination require device stacks that cannot stay active together." :
		                     "The source or destination device could not be started.",
		                 dest);
		return;
	}

	dashLoadingBegin(to_pcsx2 ? "Exporting PCSX2 memory card" : "Creating no-ECC memory card");
	r = to_pcsx2 ? mcCardExportPcsx2(source, dest, cardExportProgress) :
	               mcCardImportPcsx2(source, dest, cardExportProgress);
	dashLoadingEnd();

	if (r == MC_CARD_EXPORT_OK) {
		dashToast(DASH_TOAST_OK, to_pcsx2 ?
		          "PCSX2 .ps2 export written and verified" :
		          "No-ECC .mcd image written and verified");
		refresh();
	} else if (r == MC_CARD_EXPORT_FORMAT) {
		dashModalMessage(DI_WARNING, "Not converted",
		                 to_pcsx2 ?
		                     "This is not a valid no-ECC PS2 memory-card image." :
		                     "This is not a valid PCSX2 528-byte/page memory-card image.",
		                 source);
	} else if (r == MC_CARD_EXPORT_VERIFY_FAILED) {
		dashModalMessage(DI_WARNING, "Verification failed",
		                 "The converted file did not match the source data and was removed.", dest);
	} else {
		dashModalMessage(DI_WARNING, "Conversion failed",
		                 "The source could not be read or the destination could not be written. Any incomplete output was removed.",
		                 dest);
	}
}

static void opNewIcon(void)
{
	char title[40], text[40];
	int r;

	if (filerConfirmExploitModify(fb.dir, NULL) <= 0)
		return;
	dashStrCopy(title, LNG(Icon_Title), sizeof(title));
	if (!dashModalKeyboard("Icon title (shown in the browser)", title, 32) || title[0] == '\0')
		return;
	dashStrCopy(text, LNG(IconText), sizeof(text));
	if (!dashModalKeyboard("Icon text (drawn on the icon)", text, 32))
		return;
	if (!prepareOp())
		return;
	fb.msg[0] = '\0';
	dashLoadingBegin("Creating the icon");
	r = filerCreateFolderIcon(fb.dir, title, text, fb.msg, sizeof(fb.msg));
	opDone();
	if (r < 0)
		dashModalMessage(DI_WARNING, "Icon not created", fb.msg, fb.dir);
	else if (r > 0)
		dashToast(DASH_TOAST_OK, "icon.sys and icon.icn created");
	refresh();
}

//--------------------------------------------------------------
// Folder dates (memory card roots and hdd0:/__common)
//--------------------------------------------------------------
static int tsChoose(const char *title, const char **labels, int count)
{
	static const char *const details[] = {"edit dates yourself", "oldest first by name", "newest first by name", "SAS layout"};
	DashMenuItem items[8];
	int i;

	if (count > 8)
		count = 8;
	for (i = 0; i < count; i++) {
		items[i].icon = DI_CLOCK;
		items[i].label = labels[i];
		items[i].detail = i < 4 ? details[i] : NULL;
		items[i].disabled = 0;
	}
	return dashModalMenu(title, fb.dir, items, count, 0);
}

static void tsProgress(int done, int total, const char *folder)
{
	if (!dashLoadingActive())
		dashLoadingBegin("Updating folder dates");
	dashLoadingStatus(folder ? folder : "", done, total);
}

#define TS_ROWS 9
#define TS_ROW_H 22.0f

/* native date editor: the folders newest first, the edited one moving as
 * its date changes */
static int tsEdit(FilerDateEditor *e)
{
	const float w = 580.0f, h = 372.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	const float list_y = y + 70.0f;
	int nfields = filerDateEditorFieldCount(), top = 0, idx, i;
	float open = 0.0f;
	DashInput in;

	dashInputFlush();
	while (1) {
		dashInputPoll(&in);
		if (in.pressed & DASH_IN_BACK) {
			dashSoundPlay(DASH_SND_CANCEL);
			dashInputFlush();
			return 0;
		}
		if (in.pressed & (DASH_IN_OK | DASH_IN_START)) {
			dashSoundPlay(DASH_SND_CONFIRM);
			dashInputFlush();
			return 1;
		}
		if (in.pressed & DASH_IN_LEFT)
			e->position = (e->position + nfields - 1) % nfields;
		if (in.pressed & DASH_IN_RIGHT)
			e->position = (e->position + 1) % nfields;
		if (in.pressed & DASH_IN_UP)
			filerDateEditorAdjust(e, 1);
		if (in.pressed & DASH_IN_DOWN)
			filerDateEditorAdjust(e, -1);
		if (in.pressed & DASH_IN_L1)
			filerDateEditorSelect(e, -1);
		if (in.pressed & DASH_IN_R1)
			filerDateEditorSelect(e, 1);
		if (in.pressed & DASH_IN_L2)
			filerDateEditorMoveNextTo(e, 1);
		if (in.pressed & DASH_IN_R2)
			filerDateEditorMoveNextTo(e, 0);
		if (in.pressed & (DASH_IN_LEFT | DASH_IN_RIGHT | DASH_IN_UP | DASH_IN_DOWN | DASH_IN_L1 | DASH_IN_R1 | DASH_IN_L2 | DASH_IN_R2))
			dashSoundPlay(DASH_SND_CURSOR);

		idx = filerDateEditorSync(e);
		if (idx < 0)
			idx = 0;
		if (idx < top)
			top = idx;
		if (idx >= top + TS_ROWS)
			top = idx - TS_ROWS + 1;
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		{
			float oe = dashEaseOutCubic(open), fx, fy;
			char text[32], date[40];
			const char *name = "", *sep = "";

			dashRenderScene();
			dashModalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashModalPanel(x, y, w, h, oe);
			dashIcon(DI_CLOCK, x + 24, y + 18, 24, DASH_C_ACCENT);
			dashTextS(x + 58, y + 20, "Folder dates", DASH_FS_LABEL, DASH_C_TEXT);
			dashTextFit(x + 58, y + 46, w - 82, fb.dir, DASH_FS_CAPTION, DASH_C_TEXT3);
			for (i = top; i < e->count && i < top + TS_ROWS; i++) {
				float ry = list_y + (i - top) * TS_ROW_H;
				int changed = memcmp(&e->folders[i].stats._Modify, &e->original[i], sizeof(sceMcStDateTime)) != 0;
				DashColor col = (i == idx) ? DASH_C_TEXT : changed ? DASH_C_ACCENT : DASH_C_TEXT2;

				if (i == idx)
					dashDrawSelection(x + 16, ry - 1, w - 32, TS_ROW_H - 2, 6, 1.0f);
				dashIcon(DI_FOLDER, x + 24, ry + 3, 14, col);
				dashTextFit(x + 46, ry + 2, w - 290, e->folders[i].name, DASH_FS_SMALL, col);
				filerDateEditorFormat(&e->folders[i].stats._Modify, date, sizeof(date));
				dashTextRight(x + w - 28, ry + 3, date, DASH_FS_SMALL, col);
			}
			dashDrawScrollbar(x + w - 16, list_y, TS_ROWS * TS_ROW_H, top, TS_ROWS, e->count);

			/* the edited date, one box per field */
			fy = list_y + TS_ROWS * TS_ROW_H + 12.0f;
			fx = x + 28.0f;
			for (i = 0; i < nfields; i++) {
				float bw;
				filerDateEditorField(e->timestamp, i, text, sizeof(text), &name, &sep);
				bw = dashTextWidth(text, DASH_FS_LABEL) + 14.0f;
				if (i == e->position)
					dashDrawSelection(fx - 2, fy - 3, bw, 28, 6, 1.0f);
				dashText(fx + 5, fy, text, DASH_FS_LABEL, i == e->position ? DASH_C_TEXT : DASH_C_TEXT2);
				fx += bw;
				if (sep[0]) {
					dashText(fx, fy, sep, DASH_FS_LABEL, DASH_C_TEXT3);
					fx += dashTextWidth(sep, DASH_FS_LABEL) + 2.0f;
				}
			}
			filerDateEditorField(e->timestamp, e->position, text, sizeof(text), &name, &sep);
			dashText(fx + 16, fy + 5, name, DASH_FS_SMALL, DASH_C_ACCENT);
			dashGfxPopAlpha();
		}
		{
			DashHint hn[6];
			hn[0].button = DI_BTN_LEFT, hn[0].key = NULL, hn[0].label = "Field";
			hn[1].button = DI_BTN_UP, hn[1].key = NULL, hn[1].label = "Change";
			hn[2].button = DI_BTN_R1, hn[2].key = NULL, hn[2].label = "Folder";
			hn[3].button = DI_BTN_R2, hn[3].key = NULL, hn[3].label = "Next to";
			hn[4].button = dashOkIcon(), hn[4].key = NULL, hn[4].label = "Apply";
			hn[5].button = dashBackIcon(), hn[5].key = NULL, hn[5].label = "Cancel";
			dashDrawHints(hn, 6);
		}
		dashGfxPresent();
	}
}

static const FilerTimestampUi dash_timestamp_ui = {tsChoose, tsProgress, tsEdit};

/* custom = 0: the *Tuna date (2099) on this memory card folder;
 * custom = 1: set or organise the dates of every folder here */
static void opTimestamps(int custom)
{
	FILEINFO *f = fb.sel < fb.n ? &fb.files[fb.sel] : NULL;
	int r = 0;

	if (!prepareOp())
		return;
	fb.msg[0] = '\0';
	filerTimestampUi = &dash_timestamp_ui;
	if (custom)
		r = time_manip_organize(fb.dir, f, fb.msg);
	else if (f != NULL)
		time_manip(fb.dir, f, fb.msg);
	filerTimestampUi = NULL;
	opDone();
	if (fb.msg[0])
		dashToast(r < 0 || strstr(fb.msg, "error") || strstr(fb.msg, "failed") ? DASH_TOAST_WARN : DASH_TOAST_OK, fb.msg);
	refresh();
}

/* The classic "launch with arguments": the text file's lines become the
 * arguments of the next application started from here. */
static void opUseArgs(const FILEINFO *f)
{
	char path[MAX_PATH];

	if (f == NULL || !fullPath(f, path, sizeof(path)))
		return;
	fb.msg[0] = '\0';
	if (LaunchArgsLoadFromFile(path, fb.msg, sizeof(fb.msg)) > 0) {
		fb.args_loaded = 1;
		dashToast(DASH_TOAST_OK, "Arguments loaded: now choose the application to start");
	} else
		dashModalMessage(DI_WARNING, "No arguments loaded", fb.msg[0] ? fb.msg : "The file has no usable arguments.", f->name);
}

#define PATHPAD_SLOTS 30

/* the bookmarked folders; returns how many, with their slots in idx */
static int bookmarkItems(DashMenuItem *items, int *idx, int first)
{
	int n = first, i;

	for (i = 0; i < PATHPAD_SLOTS; i++) {
		if (!PathPad[i][0])
			continue;
		items[n].icon = DI_STAR;
		items[n].label = PathPad[i];
		items[n].detail = NULL;
		items[n].disabled = 0;
		idx[n++] = i;
	}
	return n - first;
}

static void addBookmark(void)
{
	DashMenuItem items[PATHPAD_SLOTS];
	int idx[PATHPAD_SLOTS], i, slot = -1, n, r;

	for (i = 0; i < PATHPAD_SLOTS; i++) {
		if (!strcmp(PathPad[i], fb.dir)) {
			dashToast(DASH_TOAST_INFO, "This folder is already bookmarked");
			return;
		}
		if (slot < 0 && !PathPad[i][0])
			slot = i;
	}
	if (slot < 0) {
		/* all 30 slots used: replace one */
		n = bookmarkItems(items, idx, 0);
		r = dashModalMenu("Replace which bookmark?", fb.dir, items, n, 0);
		if (r < 0)
			return;
		slot = idx[r];
	}
	dashStrCopy(PathPad[slot], fb.dir, MAX_PATH);
	if (dashSettingsSaveCnf())
		dashToast(DASH_TOAST_OK, "Folder bookmarked");
}

static void removeBookmark(void)
{
	DashMenuItem items[PATHPAD_SLOTS];
	int idx[PATHPAD_SLOTS], n, r;

	n = bookmarkItems(items, idx, 0);
	r = dashModalMenu("Remove which bookmark?", NULL, items, n, 0);
	if (r < 0)
		return;
	PathPad[idx[r]][0] = '\0';
	if (dashSettingsSaveCnf())
		dashToast(DASH_TOAST_OK, "Bookmark removed");
}

/* the PathPad bookmarks: jump to one, bookmark this folder, remove one */
static void opBookmark(void)
{
	DashMenuItem items[PATHPAD_SLOTS + 2];
	int idx[PATHPAD_SLOTS + 2], n = 0, count, r;
	int can_edit = !setting->PathPad_Lock;
	int in_folder = fb.dir[0] && !isMisc(fb.dir);

	if (can_edit && in_folder) {
		items[n].icon = DI_PLUS, items[n].label = "Bookmark this folder", items[n].detail = NULL, items[n].disabled = 0;
		idx[n++] = -1;
	}
	count = bookmarkItems(items, idx, n + (can_edit ? 1 : 0));
	if (can_edit && count > 0) {
		items[n].icon = DI_CLOSE, items[n].label = "Remove a bookmark...", items[n].detail = NULL, items[n].disabled = 0;
		idx[n++] = -2;
	}
	n += count;
	if (n == 0) {
		dashModalMessage(DI_STAR, "No bookmarks", setting->PathPad_Lock ? "Bookmarks are locked (Settings > Launching)." : "Open a folder, then choose Bookmark this folder.", NULL);
		return;
	}
	r = dashModalMenu("Bookmarks", setting->PathPad_Lock ? "locked" : NULL, items, n, 0);
	if (r < 0)
		return;
	if (idx[r] == -1) {
		addBookmark();
		return;
	}
	if (idx[r] == -2) {
		removeBookmark();
		return;
	}
	r = idx[r];
	if (strchr(PathPad[r], ':') == NULL) {
		dashToast(DASH_TOAST_WARN, "This bookmark is not a folder path");
		return;
	}
	{
		char dir[MAX_PATH];
		int len;

		dashStrCopy(dir, PathPad[r], sizeof(dir) - 1);
		len = strlen(dir);
		if (len > 0 && dir[len - 1] != '/' && dir[len - 1] != ':')
			strcat(dir, "/");
		else if (len > 0 && dir[len - 1] == ':')
			strcat(dir, "/");
		openDir(dir, NULL);
	}
}

static void actionsMenu(void)
{
	FILEINFO *f = (fb.dir[0] && !isMisc(fb.dir) && fb.sel < fb.n) ? &fb.files[fb.sel] : NULL;
	FILEINFO none;
	char path[MAX_PATH], en[NUM_MENU];
	const char *psu_label;
	int in_folder = fb.dir[0] && !isMisc(fb.dir);
	int is_elf = 0, has_target = (fb.nmarked > 0 || f != NULL);
	DashMenuItem items[44];
	int ids[44], n = 0, r;
	char marked[24];
	enum {
		F_OPEN,
		F_COPY,
		F_CUT,
		F_PASTE,
		F_PASTE_RENAME,
		F_PSU,
		F_DELETE,
		F_RENAME,
		F_NEWDIR,
		F_NEWICON,
		F_SIZE,
		F_EDIT,
		F_ARGS,
		F_TITLECFG,
		F_VMC0,
		F_VMC1,
		F_BACKUP_ALL_MMCE,
		F_BACKUP_ACTIVE_MMCE,
		F_EXPORT_ACTIVE_MMCE_PS2,
		F_BACKUP_MCD,
		F_EXPORT_PS2,
		F_IMPORT_MCD,
		F_TUNA,
		F_DATES,
		F_SELALL,
		F_INVERT,
		F_CLEARSEL,
		F_USE_ARGS,
		F_CLEAR_ARGS,
		F_VIEW,
		F_SORT,
		F_BOOKMARK,
		F_HOME,
		F_REFRESH
	};
	static const char *const show_names[] = {"File names", "Names and details", "Game titles and details"};
	static const char *const sort_names[] = {"As stored", "Name", "Game title", "Date"};

	/* the same rules as the classic menu: read-only devices, partition
	 * lists, the clipboard, protected system folders */
	memset(&none, 0, sizeof(none));
	strcpy(none.name, "..");
	memset(en, 0, sizeof(en));
	if (in_folder)
		filerMenuEnables(fb.dir, f != NULL ? f : &none, fb.nmarked, en);
	psu_label = in_folder ? filerPsuPasteLabel(fb.dir) : NULL;
	if (f != NULL && !isDir(f) && fullPath(f, path, sizeof(path)))
		is_elf = genCmpFileExt(path, "ELF");
	snprintf(marked, sizeof(marked), "%d selected", fb.nmarked);
#define ADD(ic, lab, det, dis, id) \
	(items[n].icon = (ic), items[n].label = (lab), items[n].detail = (det), items[n].disabled = (dis), ids[n++] = (id))
	if (f != NULL)
		ADD(isDir(f) ? DI_FOLDER : DI_PLAY, isDir(f) ? "Open" : "Open / start", NULL, 0, F_OPEN);
	if (in_folder) {
		ADD(DI_FILE, "Copy", fb.nmarked ? marked : NULL, !has_target || !en[COPY], F_COPY);
		ADD(DI_CLOSE, "Cut", fb.nmarked ? marked : NULL, !has_target || !en[CUT], F_CUT);
		ADD(DI_PLUS, "Paste here", nclipFiles > 0 ? (browser_cut ? "move" : "copy") : NULL, !en[PASTE], F_PASTE);
		if (en[PASTE])
			ADD(DI_EDIT, "Paste with new names...", NULL, 0, F_PASTE_RENAME);
		if (psu_label != NULL && en[PSUPASTE])
			ADD(DI_MC, psu_label, NULL, 0, F_PSU);
		ADD(DI_WARNING, "Delete...", fb.nmarked ? marked : NULL, !has_target || !en[DELETE], F_DELETE);
		ADD(DI_EDIT, "Rename...", NULL, f == NULL || !en[RENAME], F_RENAME);
		ADD(DI_FOLDER, "New folder...", NULL, !en[NEWDIR], F_NEWDIR);
		if (en[NEWICON])
			ADD(DI_IMAGE, "Create folder icon...", "icon.sys", 0, F_NEWICON);
		ADD(DI_INFO, "Size", NULL, !has_target || !en[GETSIZE], F_SIZE);
		if (f != NULL && !isDir(f) && en[OPEN_TEXTEDITOR])
			ADD(DI_TEXT, "Open in Text Editor", NULL, 0, F_EDIT);
		if (is_elf) {
			ADD(DI_EDIT, "Start with arguments...", NULL, 0, F_ARGS);
			if (en[TITLE_CFG])
				ADD(DI_SETTINGS, "Create title.cfg", NULL, 0, F_TITLECFG);
		}
		if (f != NULL && !isDir(f) && isVmcImage(f->name) && en[MOUNTVMC0]) {
			ADD(DI_MC, "Mount as vmc0:", vmcMounted[0] ? "replaces the mounted one" : NULL, 0, F_VMC0);
			ADD(DI_MC, "Mount as vmc1:", vmcMounted[1] ? "replaces the mounted one" : NULL, !en[MOUNTVMC1], F_VMC1);
		}
		if (mmceUnitFromPath(fb.dir) >= 0) {
			ADD(DI_MC, "Back up all MMCE cards...", "normal, named, GameID and BootCard images", 0, F_BACKUP_ALL_MMCE);
			ADD(DI_MC, "Back up active MMCE card...", "reads the exact firmware-selected backing image", 0, F_BACKUP_ACTIVE_MMCE);
			ADD(DI_MC, "Export active MMCE card as PCSX2 .ps2...", "adds ECC/spare bytes and verifies", 0, F_EXPORT_ACTIVE_MMCE_PS2);
		}
		if (f != NULL && !isDir(f) && fb.nmarked == 0 && genCmpFileExt(f->name, "MCD")) {
			ADD(DI_MC, "Back up .mcd...", "verified byte-for-byte copy", 0, F_BACKUP_MCD);
			ADD(DI_MC, "Export as PCSX2 .ps2...", "adds ECC and verifies the copy", 0, F_EXPORT_PS2);
		}
		if (f != NULL && !isDir(f) && fb.nmarked == 0 && genCmpFileExt(f->name, "PS2"))
			ADD(DI_MC, "Convert to no-ECC .mcd...", "strips PCSX2 spare bytes and verifies", 0, F_IMPORT_MCD);
		if (f != NULL && en[TIMEMANIP])
			ADD(DI_CLOCK, LNG(time_manip), "2099-12-31", 0, F_TUNA);
		if (en[TIMEMANIP_CUSTOM])
			ADD(DI_CLOCK, "Folder dates...", "order the saves", 0, F_DATES);
		if (f != NULL && !isDir(f) && en[LAUNCH_ELF_ARGS] && !is_elf)
			ADD(DI_EDIT, "Use as launch arguments", "then choose the application", 0, F_USE_ARGS);
		ADD(DI_CHECK, "Select all", NULL, fb.n == 0, F_SELALL);
		ADD(DI_CHECK, "Invert selection", NULL, fb.n == 0, F_INVERT);
		if (fb.nmarked)
			ADD(DI_BACK, "Clear selection", NULL, 0, F_CLEARSEL);
		ADD(DI_VIEW_LIST, "Show", show_names[fb_show], 0, F_VIEW);
		ADD(DI_VIEW_GRID, "Sort by", sort_names[fb_sort], 0, F_SORT);
	}
	if (LaunchArgsPending())
		ADD(DI_CLOSE, "Clear launch arguments", NULL, 0, F_CLEAR_ARGS);
	ADD(DI_STAR, "Bookmarks...", "PathPad", 0, F_BOOKMARK);
	ADD(DI_HOME, "Go to wLaunchELF's folder", NULL, 0, F_HOME);
	ADD(DI_REFRESH, "Refresh", NULL, 0, F_REFRESH);
#undef ADD
	r = dashModalMenu("File Browser", fb.dir[0] ? fb.dir : "Devices", items, n, 0);
	if (r < 0 || items[r].disabled)
		return;
	switch (ids[r]) {
		case F_OPEN:
			activate();
			break;
		case F_COPY:
			opCopy(0);
			break;
		case F_CUT:
			opCopy(1);
			break;
		case F_PASTE:
			opPaste(PASTE_NORMAL);
			break;
		case F_PASTE_RENAME:
			opPaste(PASTE_RENAME);
			break;
		case F_PSU:
			opPaste(PASTE_PSU);
			break;
		case F_DELETE:
			opDelete();
			break;
		case F_RENAME:
			opRename();
			break;
		case F_NEWDIR:
			opNewFolder();
			break;
		case F_NEWICON:
			opNewIcon();
			break;
		case F_SIZE:
			opSize();
			break;
		case F_EDIT:
			if (fullPath(f, path, sizeof(path))) {
				dashOpenTextEditor(path);
				refresh();
			}
			break;
		case F_ARGS:
			if (fullPath(f, path, sizeof(path)))
				launchPath(path, 1);
			break;
		case F_TITLECFG:
			opTitleCfg();
			break;
		case F_VMC0:
			opMountVmc(0);
			break;
		case F_VMC1:
			opMountVmc(1);
			break;
		case F_BACKUP_ALL_MMCE:
			opBackupAllMmceCards();
			break;
		case F_BACKUP_ACTIVE_MMCE:
			opBackupActiveMmce();
			break;
		case F_EXPORT_ACTIVE_MMCE_PS2:
			opExportActiveMmcePcsx2();
			break;
		case F_BACKUP_MCD:
			opBackupCard();
			break;
		case F_EXPORT_PS2:
			opConvertCard(1);
			break;
		case F_IMPORT_MCD:
			opConvertCard(0);
			break;
		case F_TUNA:
			opTimestamps(0);
			break;
		case F_DATES:
			opTimestamps(1);
			break;
		case F_SELALL:
			memset(fb.mark, 1, fb.n);
			fb.nmarked = fb.n;
			break;
		case F_INVERT:
			for (r = 0; r < fb.n; r++)
				fb.mark[r] = !fb.mark[r];
			fb.nmarked = fb.n - fb.nmarked;
			break;
		case F_CLEARSEL:
			memset(fb.mark, 0, MAX_ENTRY);
			fb.nmarked = 0;
			break;
		case F_USE_ARGS:
			opUseArgs(f);
			break;
		case F_CLEAR_ARGS:
			LaunchArgsClear();
			fb.args_loaded = 0;
			dashToast(DASH_TOAST_INFO, "Launch arguments cleared");
			break;
		case F_VIEW:
			r = dashModalChoice("Show", show_names, 3, fb_show);
			if (r >= 0 && r != fb_show) {
				fb_show = r;
				refresh();
			}
			break;
		case F_SORT:
			r = dashModalChoice("Sort by", sort_names, 4, fb_sort);
			if (r >= 0 && r != fb_sort) {
				fb_sort = r;
				refresh();
			}
			break;
		case F_BOOKMARK:
			opBookmark();
			break;
		case F_HOME: {
			char dir[MAX_PATH];
			filerLaunchElfFolder(dir, sizeof(dir));
			if (dir[0] && strchr(dir, ':') != NULL)
				openDir(dir, NULL);
			break;
		}
		case F_REFRESH:
			refresh();
			break;
	}
}

//--------------------------------------------------------------
// Drawing
//--------------------------------------------------------------
static int fileIcon(const FILEINFO *f)
{
	if (isDir(f))
		return DI_FOLDER;
	if (genCmpFileExt(f->name, "ELF") || genCmpFileExt(f->name, "KELF") || genCmpFileExt(f->name, "XLF"))
		return DI_APP;
	if (genCmpFileExt(f->name, "PNG") || genCmpFileExt(f->name, "JPG"))
		return DI_IMAGE;
	if (genCmpFileExt(f->name, "VCD") || genCmpFileExt(f->name, "ISO"))
		return DI_DISC;
	if (IsTextEditorFileType(f->name))
		return DI_TEXT;
	return DI_FILE;
}

static void fmtBytes(char *out, int size, u64 b)
{
	if (b >= (u64)1024 * 1024 * 1024)
		snprintf(out, size, "%.1f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
	else
		snprintf(out, size, "%.2f MB", (double)b / (1024.0 * 1024.0));
}

static void fmtSize(char *out, int size, const FILEINFO *f)
{
	u64 b = ((u64)f->stats.Reserve2 << 32) | f->stats.FileSizeByte;

	if (isDir(f) || !size_valid)
		out[0] = '\0';
	else if (b >= (u64)1024 * 1024 * 1024)
		snprintf(out, size, "%.1f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
	else if (b >= 1024 * 1024)
		snprintf(out, size, "%.1f MB", (double)b / (1024.0 * 1024.0));
	else if (b >= 1024)
		snprintf(out, size, "%u KB", (unsigned)(b / 1024));
	else
		snprintf(out, size, "%u B", (unsigned)b);
}

static void browserScene(void)
{
	int i, rows_total = fb.dir[0] ? fb.n : fb.ndevs;
	char info[64];

	dashGfxBeginFrame();
	dashBgDraw();
	dashIcon(DI_FOLDER, FB_X, 20, 24, DASH_C_ACCENT);
	dashTextS(FB_X + 32, 20, "File Browser", DASH_FS_TITLE, DASH_C_TEXT);
	dashTextScroll(FB_X, 58, FB_W - 150, fb.dir[0] ? fb.dir : "Choose a device", DASH_FS_SMALL, DASH_C_TEXT2);
	if (fb.dir[0]) {
		char space[24];

		space[0] = '\0';
		if (fb.free_valid)
			fmtBytes(space, sizeof(space), fb.free_bytes);
		if (fb.nmarked)
			snprintf(info, sizeof(info), "%d of %d selected", fb.nmarked, fb.n);
		else if (space[0])
			snprintf(info, sizeof(info), "%d item%s   %s free", fb.n, fb.n == 1 ? "" : "s", space);
		else
			snprintf(info, sizeof(info), "%d item%s", fb.n, fb.n == 1 ? "" : "s");
		dashTextRight(FB_X + FB_W, 60, info, DASH_FS_CAPTION, DASH_C_TEXT3);
	}
	if (LaunchArgsPending()) {
		snprintf(info, sizeof(info), "%d launch argument%s loaded: choose the application", LaunchArgsGetCount(),
		         LaunchArgsGetCount() == 1 ? "" : "s");
		dashTextRight(FB_X + FB_W, 42, info, DASH_FS_CAPTION, DASH_C_ACCENT);
	}
	if (nclipFiles > 0) {
		snprintf(info, sizeof(info), "Clipboard: %d to %s", nclipFiles, browser_cut ? "move" : "copy");
		dashTextRight(FB_X + FB_W, 26, info, DASH_FS_CAPTION, DASH_C_ACCENT);
	}

	dashDrawPanelBox(FB_X - 6, FB_Y - 6, FB_W + 12, FB_ROWS * FB_ROW_H + 12, 14);
	dashGfxScissor(FB_X - 4, FB_Y - 4, FB_W + 8, FB_ROWS * FB_ROW_H + 8);
	if (rows_total > 0 && !fb.loading)
		dashDrawSelection(FB_X, FB_Y + (fb.sel_anim - fb.top) * FB_ROW_H, FB_W, FB_ROW_H - 3, 8, 1.0f);
	for (i = fb.top; i < rows_total && i < fb.top + FB_ROWS && !fb.loading; i++) {
		float y = FB_Y + (i - fb.top) * FB_ROW_H;
		DashColor col = dashItemColor(i == fb.sel);

		if (fb.dir[0] == '\0') {
			DashDevice *d = &fb.devs[i];
			if (d->state != DASH_DEV_READY && i != fb.sel)
				col = DASH_C_TEXT3;
			dashIcon(d->icon, FB_X + 10, y + 4, 16, col);
			if (i == fb.sel)
				dashTextScroll(FB_X + 36, y + 3, FB_W - 220, d->name, DASH_FS_SMALL, col);
			else
				dashTextFit(FB_X + 36, y + 3, FB_W - 220, d->name, DASH_FS_SMALL, col);
			dashTextRight(FB_X + FB_W - 12, y + 5, d->status, DASH_FS_CAPTION, DASH_C_TEXT3);
		} else {
			FILEINFO *f = &fb.files[i];
			char sz[24], label[MAX_NAME + 8];
			int details = fb_show > 0 && !isMisc(fb.dir);
			if (fb.mark[i])
				dashIcon(DI_CHECK, FB_X + 8, y + 4, 16, DASH_C_OK);
			else
				dashIcon(isMisc(fb.dir) ? DI_TOOLS : fileIcon(f), FB_X + 8, y + 4, 16, col);
			/* save titles are Shift-JIS; the dashboard font shows their ASCII */
			if (fb_show == 2 && f->title[0])
				transcpy_sjis(label, f->title);
			else
				dashStrCopy(label, f->name, sizeof(label));
			if (i == fb.sel)
				dashTextScroll(FB_X + 34, y + 3, details ? FB_W - 250 : FB_W - 60, label, DASH_FS_SMALL, fb.mark[i] ? DASH_C_OK : col);
			else
				dashTextFit(FB_X + 34, y + 3, details ? FB_W - 250 : FB_W - 60, label, DASH_FS_SMALL, fb.mark[i] ? DASH_C_OK : col);
			if (details) {
				fmtSize(sz, sizeof(sz), f);
				if (sz[0])
					dashTextRight(FB_X + FB_W - 142, y + 5, sz, DASH_FS_CAPTION, DASH_C_TEXT3);
				if (time_valid && f->stats._Modify.Year) {
					char date[40];
					filerDateEditorFormat(&f->stats._Modify, date, sizeof(date));
					dashTextRight(FB_X + FB_W - 12, y + 5, date, DASH_FS_CAPTION, DASH_C_TEXT3);
				}
			}
		}
	}
	dashGfxScissorReset();
	if (fb.loading) {
		dashGfxBlend(DASH_BLEND_ADD);
		dashDrawSpinner(FB_X + FB_W * 0.5f - 60, FB_Y + 58, 8, DASH_C_ACCENT);
		dashGfxBlend(DASH_BLEND_NORMAL);
		dashText(FB_X + FB_W * 0.5f - 44, FB_Y + 49, "Reading folder...", DASH_FS_SMALL, DASH_C_TEXT2);
	} else if (rows_total == 0)
		dashTextCenter(FB_X + FB_W * 0.5f, FB_Y + 50, "This folder is empty", DASH_FS_SMALL, DASH_C_TEXT3);
	dashDrawScrollbar(FB_X + FB_W + 10, FB_Y, FB_ROWS * FB_ROW_H, fb.top, FB_ROWS, rows_total);
	{
		DashHint h[5];
		int n = 0;
		h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = "Open", n++;
		if (fb.dir[0] && !isMisc(fb.dir))
			h[n].button = DI_BTN_SQUARE, h[n].key = NULL, h[n].label = "Select", n++;
		h[n].button = DI_BTN_TRIANGLE, h[n].key = NULL, h[n].label = "Menu", n++;
		h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = fb.dir[0] ? "Up" : "Close", n++;
		dashDrawHints(h, n);
	}
	dashDrawToasts();
	dashDrawActivity();
}

//--------------------------------------------------------------
// Entry
//--------------------------------------------------------------
void dashFileBrowser(const char *start_dir)
{
	DashInput in;
	int running = 1;

	int gen = fb.gen; /* keeps counting: a listing from a previous visit is never taken */

	memset(&fb, 0, sizeof(fb));
	fb.gen = gen;
	fb.files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	fb.mark = (u8 *)malloc(MAX_ENTRY);
	if (fb.files == NULL || fb.mark == NULL) {
		free(fb.files);
		free(fb.mark);
		dashToast(DASH_TOAST_ERROR, "Not enough memory for the File Browser");
		return;
	}
	memset(fb.mark, 0, MAX_ENTRY);
	dashSetSceneOverride(browserScene);
	/* a start folder on a device that is not running starts at the list */
	if (start_dir != NULL && start_dir[0] && (isMisc(start_dir) || (strchr(start_dir, ':') && dashDevicePathReady(start_dir))))
		openDir(start_dir, NULL);
	else
		openDir("", NULL);
	dashInputFlush();

	while (running) {
		int total, prev;

		/* The classic wLaunchELF browser serializes USB directory I/O and PAD
		 * polling. Keep the dashboard animated, but preserve that ordering for
		 * USB: fileXio runs on the worker while input stays untouched. Once the
		 * listing completion is applied, re-anchor the pad state before accepting
		 * a new action so the confirm press that opened USB cannot leak through. */
		if (fb.input_rearm && !dashWorkerIoBusy()) {
			dashInputFlush();
			fb.input_rearm = 0;
		}
		if (fb.usb_input_jobs > 0 && dashWorkerIoBusy())
			memset(&in, 0, sizeof(in));
		else
			dashInputPoll(&in);
		total = fb.dir[0] ? fb.n : fb.ndevs;
		prev = fb.sel;
		if (!fb.loading) {
			if (in.pressed & DASH_IN_UP)
				fb.sel--;
			if (in.pressed & DASH_IN_DOWN)
				fb.sel++;
			if (in.pressed & (DASH_IN_L2 | DASH_IN_LEFT))
				fb.sel -= FB_ROWS;
			if (in.pressed & (DASH_IN_R2 | DASH_IN_RIGHT))
				fb.sel += FB_ROWS;
			if (fb.sel >= total)
				fb.sel = total - 1;
			if (fb.sel < 0)
				fb.sel = 0;
			if (fb.sel != prev)
				dashSoundPlay(DASH_SND_CURSOR);
			if (in.pressed & DASH_IN_OK) {
				dashSoundPlay(DASH_SND_CONFIRM);
				activate();
			}
			if ((in.pressed & DASH_IN_FAV) && fb.dir[0] && !isMisc(fb.dir) && fb.sel < fb.n) {
				fb.mark[fb.sel] = !fb.mark[fb.sel];
				fb.nmarked += fb.mark[fb.sel] ? 1 : -1;
				if (fb.sel < fb.n - 1)
					fb.sel++;
			}
			if (in.pressed & DASH_IN_MENU)
				actionsMenu();
		}
		if (in.pressed & DASH_IN_BACK) {
			dashSoundPlay(DASH_SND_CANCEL);
			if (fb.dir[0] == '\0')
				running = 0;
			else {
				char parent[MAX_PATH], child[MAX_NAME];
				parentOf(fb.dir, parent, sizeof(parent), child, sizeof(child));
				openDir(parent, child[0] ? child : NULL);
			}
		}

		total = fb.dir[0] ? fb.n : fb.ndevs;
		if (fb.sel >= total)
			fb.sel = total > 0 ? total - 1 : 0;
		if (fb.sel < fb.top)
			fb.top = fb.sel;
		if (fb.sel >= fb.top + FB_ROWS)
			fb.top = fb.sel - FB_ROWS + 1;
		fb.sel_anim = dashApproach(fb.sel_anim, (float)fb.sel, 22.0f * dashAnimSpeed(), dashVisualUpdate());
		dashRenderScene();
		dashGfxPresent();
	}
	fb.gen++; /* a listing still in flight is dropped */
	if (fb.args_loaded && LaunchArgsPending())
		LaunchArgsClear(); /* loaded here but no application was started */
	dashSetSceneOverride(NULL);
	free(fb.files);
	free(fb.mark);
	fb.files = NULL;
	fb.mark = NULL;
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_browser.c
//--------------------------------------------------------------
