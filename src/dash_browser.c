//--------------------------------------------------------------
//File name:   dash_browser.c
//Native dashboard File Browser. Replaces the classic getFilePath() browser
//whenever the dashboard opens a folder (FILES, DEVICES, Tools, "Show in
//File Browser").
//
//* Folders are read by the background worker; the list stays usable and
//  a spinner shows while a slow device answers.
//* File operations reuse the proven classic code (copy/paste, PSU,
//  delete, rename, new folder, size, title.cfg). While the dashboard owns
//  the screen their questions and progress use dashboard dialogs
//  (dashClassicUiInstall), so nothing drops to the classic pop-ups.
//* Tools without a native version yet (VMC mounting, timestamps, icon
//  creation) are one entry away: "More tools..." opens the classic File
//  Browser in the same folder.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "filer_shared.h"
#include "filer_actions.h"
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
	int gen;
	int sel, top;
	float sel_anim;
	char want_sel[MAX_NAME]; /* select this name once the listing arrives */
	DashDevice devs[DASH_MAX_DEVICES];
	int ndevs;
	char msg[MAX_PATH]; /* result text from classic operations */
} fb;

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
} FbListJob;

static int isDir(const FILEINFO *f)
{
	return (f->stats.AttrFile & sceMcFileAttrSubdir) != 0;
}

static int fbCmp(const void *a, const void *b)
{
	const FILEINFO *x = (const FILEINFO *)a, *y = (const FILEINFO *)b;

	if (isDir(x) != isDir(y))
		return isDir(y) - isDir(x);
	return stricmp(x->name, y->name);
}

static void listRun(void *arg)
{
	FbListJob *j = (FbListJob *)arg;

	dashWorkerStatus(j->path, 0, 0);
	j->n = getDir(j->path, j->files);
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
		qsort(fb.files, fb.n, sizeof(FILEINFO), fbCmp);
		memset(fb.mark, 0, MAX_ENTRY);
		fb.nmarked = 0;
		fb.loading = 0;
		fb.sel = 0;
		if (fb.want_sel[0])
			selectName(fb.want_sel);
		fb.want_sel[0] = '\0';
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

	fb.gen++;
	fb.n = 0;
	fb.nmarked = 0;
	fb.sel = fb.top = 0;
	fb.sel_anim = 0.0f;
	fb.want_sel[0] = '\0';
	if (select != NULL)
		dashStrCopy(fb.want_sel, select, sizeof(fb.want_sel));
	dashStrCopy(fb.dir, dir, sizeof(fb.dir));
	if (dir[0] == '\0') {
		fb.ndevs = dashDevicesList(fb.devs, DASH_MAX_DEVICES - 1);
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
	dashStrCopy(j->path, dir, sizeof(j->path));
	fb.loading = 1;
	if (dashWorkerSubmit("Reading folder", DASH_JOB_VISIBLE, listRun, listDone, j) < 0) {
		free(j->files);
		free(j);
		fb.loading = 0;
		dashToast(DASH_TOAST_WARN, "Busy, try again in a moment");
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

	dashLaunchApp(dashLibResolve(path, &tmp), with_args);
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
			if (dashDevicesStart(d, NULL, NULL) < 0)
				return;
			if (!dashDevicePathReady(path)) {
				openDir("", path); /* the list shows the new state */
				dashToast(DASH_TOAST_WARN, "Device not found");
				return;
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

static void opPaste(int psu)
{
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
		filerPasteInto(fb.msg, fb.dir);
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

static void opClassic(void)
{
	char dir[MAX_PATH];

	dashStrCopy(dir, fb.dir, sizeof(dir));
	dashOpenBrowserClassic(dir);
	refresh();
}

static void actionsMenu(void)
{
	FILEINFO *f = (fb.dir[0] && !isMisc(fb.dir) && fb.sel < fb.n) ? &fb.files[fb.sel] : NULL;
	char path[MAX_PATH];
	int in_folder = fb.dir[0] && !isMisc(fb.dir);
	int is_elf = 0, has_target = (fb.nmarked > 0 || f != NULL);
	DashMenuItem items[16];
	int ids[16], n = 0, r;
	char marked[24];
	enum {
		F_OPEN,
		F_COPY,
		F_CUT,
		F_PASTE,
		F_PSU,
		F_DELETE,
		F_RENAME,
		F_NEWDIR,
		F_SIZE,
		F_EDIT,
		F_ARGS,
		F_TITLECFG,
		F_SELALL,
		F_CLEARSEL,
		F_REFRESH,
		F_CLASSIC
	};

	if (f != NULL && !isDir(f) && fullPath(f, path, sizeof(path)))
		is_elf = genCmpFileExt(path, "ELF");
	snprintf(marked, sizeof(marked), "%d selected", fb.nmarked);
#define ADD(ic, lab, det, dis, id) \
	(items[n].icon = (ic), items[n].label = (lab), items[n].detail = (det), items[n].disabled = (dis), ids[n++] = (id))
	if (f != NULL)
		ADD(isDir(f) ? DI_FOLDER : DI_PLAY, isDir(f) ? "Open" : "Open / start", NULL, 0, F_OPEN);
	if (in_folder) {
		ADD(DI_FILE, "Copy", fb.nmarked ? marked : NULL, !has_target, F_COPY);
		ADD(DI_CLOSE, "Cut", fb.nmarked ? marked : NULL, !has_target, F_CUT);
		ADD(DI_PLUS, "Paste here", nclipFiles > 0 ? (browser_cut ? "move" : "copy") : NULL, nclipFiles <= 0, F_PASTE);
		ADD(DI_MC, "Paste as PSU (save / restore)", NULL, nclipFiles <= 0, F_PSU);
		ADD(DI_WARNING, "Delete...", fb.nmarked ? marked : NULL, !has_target, F_DELETE);
		ADD(DI_EDIT, "Rename...", NULL, f == NULL, F_RENAME);
		ADD(DI_FOLDER, "New folder...", NULL, 0, F_NEWDIR);
		ADD(DI_INFO, "Size", NULL, !has_target, F_SIZE);
		if (f != NULL && !isDir(f))
			ADD(DI_TEXT, "Open in Text Editor", NULL, 0, F_EDIT);
		if (is_elf) {
			ADD(DI_EDIT, "Start with arguments...", NULL, 0, F_ARGS);
			ADD(DI_SETTINGS, "Create title.cfg", NULL, 0, F_TITLECFG);
		}
		ADD(DI_CHECK, "Select all", NULL, fb.n == 0, F_SELALL);
		if (fb.nmarked)
			ADD(DI_BACK, "Clear selection", NULL, 0, F_CLEARSEL);
	}
	ADD(DI_REFRESH, "Refresh", NULL, 0, F_REFRESH);
	if (in_folder)
		ADD(DI_TOOLS, "More tools...", "classic", 0, F_CLASSIC);
#undef ADD
	r = dashModalMenu("File Browser", fb.dir[0] ? fb.dir : "Devices", items, n, 0);
	if (r < 0)
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
			opPaste(0);
			break;
		case F_PSU:
			opPaste(1);
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
		case F_SELALL:
			memset(fb.mark, 1, fb.n);
			fb.nmarked = fb.n;
			break;
		case F_CLEARSEL:
			memset(fb.mark, 0, MAX_ENTRY);
			fb.nmarked = 0;
			break;
		case F_REFRESH:
			refresh();
			break;
		case F_CLASSIC:
			opClassic();
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
	dashTextFit(FB_X, 58, FB_W - 150, fb.dir[0] ? fb.dir : "Choose a device", DASH_FS_SMALL, DASH_C_TEXT2);
	if (fb.dir[0]) {
		if (fb.nmarked)
			snprintf(info, sizeof(info), "%d of %d selected", fb.nmarked, fb.n);
		else
			snprintf(info, sizeof(info), "%d item%s", fb.n, fb.n == 1 ? "" : "s");
		dashTextRight(FB_X + FB_W, 60, info, DASH_FS_CAPTION, DASH_C_TEXT3);
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
			dashTextFit(FB_X + 36, y + 3, FB_W - 220, d->name, DASH_FS_SMALL, col);
			dashTextRight(FB_X + FB_W - 12, y + 5, d->status, DASH_FS_CAPTION, DASH_C_TEXT3);
		} else {
			FILEINFO *f = &fb.files[i];
			char sz[24];
			if (fb.mark[i])
				dashIcon(DI_CHECK, FB_X + 8, y + 4, 16, DASH_C_OK);
			else
				dashIcon(isMisc(fb.dir) ? DI_TOOLS : fileIcon(f), FB_X + 8, y + 4, 16, col);
			dashTextFit(FB_X + 34, y + 3, FB_W - 150, f->name, DASH_FS_SMALL, fb.mark[i] ? DASH_C_OK : col);
			fmtSize(sz, sizeof(sz), f);
			if (sz[0])
				dashTextRight(FB_X + FB_W - 12, y + 5, sz, DASH_FS_CAPTION, DASH_C_TEXT3);
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
		fb.sel_anim = dashApproach(fb.sel_anim, (float)fb.sel, 22.0f * dashAnimSpeed(), 1.0f / 60.0f);
		dashRenderScene();
		dashGfxPresent();
	}
	fb.gen++; /* a listing still in flight is dropped */
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
