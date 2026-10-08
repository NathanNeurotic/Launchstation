//--------------------------------------------------------------
//File name:   dash_files.c
//FILES and DEVICES sections.
//
//FILES is the doorway into the File Browser (dash_browser.c: copy,
//paste, PSU, rename, timestamps, VMC mounting, launch with args...): it
//offers the device root, the last folder, every detected device and the
//user's PathPad bookmarks.
//DEVICES shows what wLaunchELF actually knows about each device. Drivers
//that are not running are only loaded when the user asks for it.
//--------------------------------------------------------------
#include "dash_internal.h"

extern char PathPad[30][MAX_PATH];
extern char LastDir[MAX_NAME];

//--------------------------------------------------------------
// Shared: open a device, probing its driver first when needed
//--------------------------------------------------------------
static const char *deviceDisplayMount(const DashDevice *d, char *buf, int size)
{
	if (d != NULL && d->icon == DI_USB) {
		int unit = 0;

		if (!strncmp(d->mount, "usb", 3) &&
		    d->mount[3] >= '0' && d->mount[3] <= '9' && d->mount[4] == ':')
			unit = d->mount[3] - '0';
		snprintf(buf, size, "mass%d:/", unit);
		return buf;
	}
	return (d != NULL) ? d->mount : "";
}

static void deviceNotFound(const DashDevice *d)
{
	char body[160];

	if (d->icon == DI_USB)
		snprintf(body, sizeof(body), "No USB storage device was found. Connect a USB drive and try again.");
	else if (d->icon == DI_NETWORK)
		snprintf(body, sizeof(body), "The network share could not be reached. Check the cable, IP settings and that the UDPFS server is running.");
	else
		snprintf(body, sizeof(body), "%s is not available on this console, or it is not connected.", d->name);
	{
		char id[16];
		dashModalMessage(DI_WARNING, "Device not found", body, deviceDisplayMount(d, id, sizeof(id)));
	}
}

/* A device the user asked to open whose driver is starting in the
 * background. The UI stays usable; when the driver is up the device opens,
 * provided the user is still on the section they opened it from. */
static struct
{
	int active;
	int done;
	int ok;
	int section;
	int browse; /* 1: open it when ready, 0: just report */
	DashDevice dev;
} pend;

static void pendProbeDone(const char *mount, int ok, void *user)
{
	(void)mount;
	(void)user;
	pend.done = 1;
	pend.ok = ok;
}

void dashFilesPoll(void)
{
	char msg[80];

	if (!pend.active || !pend.done)
		return;
	pend.active = 0;
	if (dashCurrentSection() != pend.section) {
		snprintf(msg, sizeof(msg), pend.ok ? "%s is ready" : "%s was not found", pend.dev.name);
		dashToast(pend.ok ? DASH_TOAST_OK : DASH_TOAST_WARN, msg);
		return;
	}
	if (!pend.ok) {
		deviceNotFound(&pend.dev);
		return;
	}
	if (pend.browse) {
		const char *path = pend.dev.browse;
		char usb_path[8];

		/* The probe already resolved USB units; avoid another forced scan. */
		if (!strcmp(path, "usb:/")) {
			int mask = filerUsbDriveMask();
			int unit;
			for (unit = 0; unit < 10; unit++)
				if (mask & (1 << unit))
					break;
			if (unit < 10) {
				snprintf(usb_path, sizeof(usb_path), "usb%d:/", unit);
				path = usb_path;
			}
		}
		dashOpenBrowser(path);
	} else {
		snprintf(msg, sizeof(msg), "%s is ready", pend.dev.name);
		dashToast(DASH_TOAST_OK, msg);
	}
}

static void startDevice(const DashDevice *d, int browse)
{
	if (pend.active) {
		dashToast(DASH_TOAST_INFO, "Another device is still starting");
		return;
	}
	memset(&pend, 0, sizeof(pend));
	pend.active = 1;
	pend.section = dashCurrentSection();
	pend.browse = browse;
	pend.dev = *d;
	if (dashDevicesStart(d, pendProbeDone, NULL) < 0) {
		pend.active = 0;
		dashToast(DASH_TOAST_ERROR, "Could not start the device (out of memory)");
	}
}

static void openDevice(const DashDevice *d)
{
	if (d->state == DASH_DEV_ABSENT && d->icon == DI_MC) {
		dashModalMessage(DI_MC, d->name, "Insert a memory card into this slot to browse it.", NULL);
		return;
	}
	if (d->icon == DI_DISC && d->state != DASH_DEV_READY) {
		dashModalMessage(DI_DISC, "No disc", "Insert a disc to browse it.", NULL);
		return;
	}
	if (d->state == DASH_DEV_PROBE || d->state == DASH_DEV_ABSENT) {
		startDevice(d, 1);
		return;
	}
	dashOpenBrowser(d->browse);
}

//--------------------------------------------------------------
// FILES
//--------------------------------------------------------------
enum FileEntryKind {
	FE_HEADER = 0,
	FE_ROOT,
	FE_LAST,
	FE_DEVICE,
	FE_BOOKMARK,
	FE_MISC
};

typedef struct
{
	int kind;
	int icon;
	int index;
	char label[64];
	char detail[DASH_PATH_MAX];
} FileEntry;

#define FILES_MAX 64
static FileEntry fentries[FILES_MAX];
static int nfentries;
static DashDevice fdevs[DASH_MAX_DEVICES];
static int nfdevs;
static int fsel;
static float fsel_anim;
static int ftop;

static void fAdd(int kind, int icon, int index, const char *label, const char *detail)
{
	FileEntry *e;

	if (nfentries >= FILES_MAX)
		return;
	e = &fentries[nfentries++];
	e->kind = kind;
	e->icon = icon;
	e->index = index;
	dashStrCopy(e->label, label, sizeof(e->label));
	dashStrCopy(e->detail, detail ? detail : "", sizeof(e->detail));
}

static void filesRebuild(void)
{
	int i;

	nfentries = 0;
	fAdd(FE_HEADER, -1, 0, "BROWSE", NULL);
	fAdd(FE_ROOT, DI_FOLDER, 0, "File Browser", "Open the Launchstation File Browser");
	if (LastDir[0] && strchr(LastDir, ':') != NULL)
		fAdd(FE_LAST, DI_CLOCK, 0, "Last folder", LastDir);
	fAdd(FE_MISC, DI_TOOLS, 0, "MISC commands", "Launchstation built-in commands folder");

	nfdevs = 0; /* Devices are the alternate mode of this same section. */

	{
		int have = 0;
		for (i = 0; i < MAX_PATH_PAD; i++) {
			if (!PathPad[i][0])
				continue;
			if (!have) {
				fAdd(FE_HEADER, -1, 0, "BOOKMARKS (PATHPAD)", NULL);
				have = 1;
			}
			{
				char label[64];
				const char *p = PathPad[i];
				int len = strlen(p);
				const char *name = p;
				int k;
				/* label: last path component */
				for (k = len - 2; k >= 0; k--)
					if (p[k] == '/' || p[k] == ':') {
						name = p + k + 1;
						break;
					}
				dashStrCopy(label, name[0] ? name : p, sizeof(label));
				if (label[0] && label[strlen(label) - 1] == '/')
					label[strlen(label) - 1] = '\0';
				fAdd(FE_BOOKMARK, DI_PIN, i, label[0] ? label : p, p);
			}
		}
	}
	if (fsel >= nfentries || fsel < 0 || fentries[fsel].kind == FE_HEADER)
		fsel = 1;
}

static void filesEnter(void)
{
	filesRebuild();
	fsel_anim = (float)fsel;
}

static void fMove(int d)
{
	int s = fsel;

	do {
		s += d;
		if (s < 0 || s >= nfentries)
			return;
	} while (fentries[s].kind == FE_HEADER);
	fsel = s;
	dashUiFocusPoke();
}

static void filesActivate(void)
{
	FileEntry *e = &fentries[fsel];

	switch (e->kind) {
		case FE_ROOT:
			dashOpenBrowser("");
			break;
		case FE_LAST:
			dashOpenBrowser(LastDir);
			break;
		case FE_MISC:
			dashOpenBrowser(setting->Misc);
			break;
		case FE_DEVICE:
			if (e->index < nfdevs)
				openDevice(&fdevs[e->index]);
			break;
		case FE_BOOKMARK:
			dashOpenBrowser(PathPad[e->index]);
			break;
	}
	filesRebuild();
}

static void filesInput(const DashInput *in)
{
	int i;

	if (in->pressed & DASH_IN_UP)
		fMove(-1);
	if (in->pressed & DASH_IN_DOWN)
		fMove(1);
	if (in->pressed & DASH_IN_L2)
		for (i = 0; i < 6; i++)
			fMove(-1);
	if (in->pressed & DASH_IN_R2)
		for (i = 0; i < 6; i++)
			fMove(1);
	if (in->pressed & DASH_IN_OK)
		filesActivate();
	if (in->pressed & DASH_IN_MENU) {
		DashMenuItem items[] = {
		    {DI_FOLDER, "Open File Browser", NULL, 0},
		    {DI_TEXT, "Text Editor", NULL, 0},
		    {DI_REFRESH, "Refresh devices", NULL, 0}};
		int r = dashModalMenu("Files", NULL, items, 3, 0);
		if (r == 0)
			filesActivate();
		else if (r == 1)
			dashOpenTextEditor(NULL);
		else if (r == 2)
			filesRebuild();
	}
}

static void filesUpdate(float dt)
{
	static float t;

	fsel_anim = dashApproach(fsel_anim, (float)fsel, 16.0f * dashAnimSpeed(), dt);
	t += dt;
	if (t > 1.0f) {
		/* device states change (memory card inserted, disc...) */
		int keep = fsel;
		t = 0.0f;
		filesRebuild();
		fsel = keep < nfentries ? keep : fsel;
	}
}

static void filesDraw(void)
{
	const float lx = 48, ly = 102, row_h = 29, lw = 300;
	const int rows = 10;
	int i;
	FileEntry *e = &fentries[fsel];

	dashDrawSectionTitle(48, 68, "Files", "the complete Launchstation file manager");

	if (fsel < ftop + 1)
		ftop = fsel - 1;
	if (fsel >= ftop + rows)
		ftop = fsel - rows + 1;
	if (ftop < 0)
		ftop = 0;

	dashDrawPanelBox(lx - 6, ly - 6, lw + 12, rows * row_h + 12, 14);
	dashGfxScissor(lx - 6, ly - 4, lw + 12, rows * row_h + 8);
	{
		float fy = ly + (fsel_anim - ftop) * row_h;
		dashDrawSelection(lx, fy, lw, row_h - 3, 9, 1.0f);
	}
	for (i = ftop; i < nfentries && i < ftop + rows; i++) {
		FileEntry *f = &fentries[i];
		float y = ly + (i - ftop) * row_h;

		if (f->kind == FE_HEADER) {
			dashText(lx + 8, y + 7, f->label, DASH_FS_CAPTION, DASH_C_ACCENT);
			continue;
		}
		dashIcon(f->icon, lx + 10, y + 5, 16, dashItemColor(i == fsel));
		dashTextFit(lx + 34, y + 3, lw - 50, f->label, DASH_FS_SMALL, dashItemColor(i == fsel));
		if (f->kind == FE_DEVICE && f->index < nfdevs) {
			int st = fdevs[f->index].state;
			DashColor dot = (st == DASH_DEV_READY) ? DASH_C_OK : (st == DASH_DEV_ABSENT) ? DASH_C_TEXT3 : DASH_C_WARM;
			dashGlow(lx + lw - 14, y + 13, 5, 5, dot);
		}
	}
	dashGfxScissorReset();
	dashDrawScrollbar(lx + lw + 10, ly, rows * row_h, ftop, rows, nfentries);

	/* detail panel */
	{
		float dx = 372, dy = 98, dw = 228, dh = 292;
		dashDrawCard(dx, dy, dw, dh, 16);
		dashGfxBlend(DASH_BLEND_ADD);
		dashAccentGlow(dx + dw * 0.5f, dy + 70, 80, 60, 1.0f);
		dashGfxBlend(DASH_BLEND_NORMAL);
		dashIcon(e->icon >= 0 ? e->icon : DI_FOLDER, dx + dw * 0.5f - 32, dy + 38, 64, DASH_C_TEXT);
		dashTextScrollCenter(dx + dw * 0.5f, dy + 118, dw - 32, e->label, DASH_FS_LABEL, DASH_C_TEXT);
		if (e->kind == FE_DEVICE && e->index < nfdevs) {
			DashDevice *d = &fdevs[e->index];
			dashTextScrollCenter(dx + dw * 0.5f, dy + 150, dw - 32, d->status, DASH_FS_SMALL, d->state == DASH_DEV_READY ? DASH_C_OK : DASH_C_TEXT2);
			if (d->detail[0])
				dashTextScrollCenter(dx + dw * 0.5f, dy + 174, dw - 32, d->detail, DASH_FS_CAPTION, DASH_C_TEXT3);
			{
				char id[16];
				dashTextScrollCenter(dx + dw * 0.5f, dy + 200, dw - 32, deviceDisplayMount(d, id, sizeof(id)), DASH_FS_CAPTION, DASH_C_TEXT3);
			}
			if (d->state == DASH_DEV_PROBE)
				dashTextWrap(dx + 16, dy + 228, dw - 32, 3, "Its driver is loaded only when you open it, to keep startup fast.", DASH_FS_CAPTION, DASH_C_TEXT3, 18);
		} else
			dashTextWrap(dx + 16, dy + 150, dw - 32, 5, e->detail, DASH_FS_CAPTION, DASH_C_TEXT2, 19);
	}
}

static void filesHints(void)
{
	DashHint h[4];

	h[0].button = dashOkIcon(), h[0].key = NULL, h[0].label = "Open";
	h[1].button = DI_BTN_TRIANGLE, h[1].key = NULL, h[1].label = "Options";
	h[2].button = -1, h[2].key = "SELECT", h[2].label = "Devices";
	h[3].button = -1, h[3].key = "START", h[3].label = "Menu";
	dashDrawHints(h, 4);
}

static const DashScreen dashFilesListScreen = {filesEnter, filesInput, filesUpdate, filesDraw, filesHints};

//--------------------------------------------------------------
// DEVICES
//--------------------------------------------------------------
static DashDevice devs[DASH_MAX_DEVICES];
static int ndevs;
static int dsel;
static float dsel_anim;
static float dcam;

#define DEV_COLS 3
#define DEV_W 170.0f
#define DEV_H 80.0f
#define DEV_PX 184.0f
#define DEV_PY 88.0f

static int devSelectable(int idx)
{
	/* PROBE means "not checked yet", not unavailable. Let the user select it
	 * so OK can invoke the existing lazy loader. Explicitly absent devices
	 * remain display-only and are skipped by navigation. */
	return idx >= 0 && idx < ndevs &&
	       (devs[idx].state == DASH_DEV_READY || devs[idx].state == DASH_DEV_PROBE);
}

static void devSelectFirst(void)
{
	int i;
	for (i = 0; i < ndevs; i++)
		if (devSelectable(i)) {
			dsel = i;
			return;
		}
	dsel = -1;
}

static void devRefresh(void)
{
	ndevs = dashDevicesList(devs, DASH_MAX_DEVICES);
	if (!devSelectable(dsel))
		devSelectFirst();
}

static void devMove(int delta)
{
	int s;
	if (dsel < 0)
		return;
	for (s = dsel + delta; s >= 0 && s < ndevs; s += delta)
		if (devSelectable(s)) {
			dsel = s;
			dashUiFocusPoke();
			return;
		}
}

static void devEnter(void)
{
	devRefresh();
	dsel_anim = (float)(dsel >= 0 ? dsel : 0);
}

static void devInput(const DashInput *in)
{
	if (ndevs <= 0 || dsel < 0)
		return;
	if (in->pressed & DASH_IN_LEFT)
		devMove(-1);
	if (in->pressed & DASH_IN_RIGHT)
		devMove(1);
	if (in->pressed & DASH_IN_UP)
		devMove(-DEV_COLS);
	if (in->pressed & DASH_IN_DOWN)
		devMove(DEV_COLS);
	if (in->pressed & DASH_IN_OK) {
		openDevice(&devs[dsel]);
		devRefresh();
	}
	if (in->pressed & DASH_IN_MENU) {
		DashDevice *d = &devs[dsel];
		DashMenuItem items[4];
		char display_mount[16];
		int n = 0, r, ids[4];

		items[n].icon = DI_FOLDER, items[n].label = "Browse",
		items[n].detail = deviceDisplayMount(d, display_mount, sizeof(display_mount)),
		items[n].disabled = 0, ids[n++] = 0;
		items[n].icon = DI_REFRESH, items[n].label = (d->state == DASH_DEV_READY) ? "Check again" : "Detect now", items[n].detail = NULL,
		items[n].disabled = (d->icon == DI_MC || d->icon == DI_DISC), ids[n++] = 1;
		if (d->icon == DI_HDD && !strncmp(d->mount, "hdd", 3))
			items[n].icon = DI_HDD, items[n].label = "HDD Manager", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 2;
		if (d->icon == DI_DISC)
			items[n].icon = DI_PLAY, items[n].label = "Start disc", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 3;
		r = dashModalMenu(d->name, d->status, items, n, 0);
		if (r >= 0) {
			switch (ids[r]) {
				case 0:
					openDevice(d);
					break;
				case 1:
					startDevice(d, 0);
					break;
				case 2:
					dashRunMisc(setting->Misc_HddManager);
					break;
				case 3:
					dashRunMisc(setting->Misc_PS2Disc);
					break;
			}
		}
		devRefresh();
	}
}

static void devUpdate(float dt)
{
	static float t;
	float target;

	if (dsel >= 0)
		dsel_anim = dashApproach(dsel_anim, (float)dsel, 14.0f * dashAnimSpeed(), dt);
	target = (((dsel >= 0 ? dsel : 0) / DEV_COLS) - 1) * DEV_PY;
	if (target < 0)
		target = 0;
	if (target > ((ndevs - 1) / DEV_COLS - 2) * DEV_PY)
		target = ((ndevs - 1) / DEV_COLS - 2) * DEV_PY;
	if (target < 0)
		target = 0;
	dcam = dashApproach(dcam, target, 10.0f * dashAnimSpeed(), dt);
	t += dt;
	if (t > 0.5f) {
		t = 0.0f;
		devRefresh();
	}
}

static void devDraw(void)
{
	int i;
	char sub[48];
	int ready = 0;

	for (i = 0; i < ndevs; i++)
		if (devs[i].state == DASH_DEV_READY)
			ready++;
	snprintf(sub, sizeof(sub), "%d ready", ready);
	dashDrawSectionTitle(48, 68, "Devices", sub);

	dashGfxScissor(DASH_SAFE_L - 10, 98, DASH_SAFE_R - DASH_SAFE_L + 20, 296);
	for (i = 0; i < ndevs; i++) {
		DashDevice *d = &devs[i];
		float x = 48 + (i % DEV_COLS) * DEV_PX;
		float y = 108 + (i / DEV_COLS) * DEV_PY - dcam;
		float f = dashClampf(1.0f - fabsf(i - dsel_anim), 0.0f, 1.0f);
		int dim = (d->state != DASH_DEV_READY);
		DashColor pc = DASH_C_PANEL_HI;
		DashColor top = dim ? dashColorScale(pc, 0.8f) : dashColorMix(pc, DASH_C_SELECT, 0.7f * f);
		DashColor bot = dim ? dashColorScale(pc, 0.55f) : dashColorScale(pc, 0.7f);
		top.a = dim ? 200 : 225;
		bot.a = top.a;
		DashColor stc = (d->state == DASH_DEV_READY) ? DASH_C_OK : (d->state == DASH_DEV_ABSENT) ? DASH_C_TEXT3 : DASH_C_WARM;

		if (y < 60 || y > 400)
			continue;
		dashShadow(x, y, DEV_W, DEV_H, 10, 0.5f);
		dashPanelGradV(x, y, DEV_W, DEV_H, 14, top, bot);
		if (i == dsel) {
			dashDrawSelection(x - 2, y - 2, DEV_W + 4, DEV_H + 4, 16, 0.82f);
			/* Always close the four card edges; this prevents broken corners on CRT/interlaced output. */
			dashPanelBorder(x - 2, y - 2, DEV_W + 4, DEV_H + 4, 16, dashColorA(DASH_C_SELEDGE, 0.92f));
		}
		dashGfxBlend(DASH_BLEND_ADD);
		dashGlow(x + 33, y + 29, 28, 28, dashColorA(stc, dim ? 0.1f : 0.25f));
		dashGfxBlend(DASH_BLEND_NORMAL);
		dashIcon(d->icon, x + 14, y + 10, 38, dim ? DASH_C_TEXT3 : (i == dsel ? DASH_C_SELTEXT : DASH_C_TEXT));
		if (i == dsel)
			dashTextScroll(x + 64, y + 12, DEV_W - 72, d->name, DASH_FS_SMALL, dim ? DASH_C_TEXT3 : DASH_C_SELTEXT);
		else
			dashTextFit(x + 64, y + 12, DEV_W - 72, d->name, DASH_FS_SMALL, dim ? DASH_C_TEXT3 : DASH_C_TEXT);
		{
			char id[16];
			dashTextFit(x + 64, y + 34, DEV_W - 72, deviceDisplayMount(d, id, sizeof(id)), DASH_FS_CAPTION, DASH_C_TEXT3);
		}
		dashGlow(x + 18, y + 67, 4, 4, stc);
		dashTextFit(x + 28, y + 58, DEV_W - 36, d->detail[0] ? d->detail : d->status, DASH_FS_CAPTION,
		            d->state == DASH_DEV_READY ? DASH_C_TEXT2 : DASH_C_TEXT3);
		if (d->fill >= 0.0f) {
			dashPanel(x + 64, y + 50, DEV_W - 78, 6, 3, DC(255, 255, 255, 30));
			dashPanel(x + 64, y + 50, (DEV_W - 78) * dashClampf(d->fill, 0.02f, 1.0f), 6, 3,
			          d->fill > 0.9f ? DASH_C_DANGER : DASH_C_ACCENT);
		}
	}
	dashGfxScissorReset();
	if (ndevs > 0 && dsel >= 0) {
		DashDevice *d = &devs[dsel];
		const char *tip = (d->state == DASH_DEV_PROBE) ? "Not connected: opening it will load its driver."
		                                               : (d->state == DASH_DEV_ABSENT) ? d->status : "Ready to browse.";
		dashDrawCard(48, 368, 400, 26, 13);
		dashTextFit(60, 374.5f, 380, tip, DASH_FS_SMALL, DASH_C_TEXT2);
	}
}

static void devHints(void)
{
	DashHint h[4];

	h[0].button = dashOkIcon(), h[0].key = NULL, h[0].label = "Browse";
	h[1].button = DI_BTN_TRIANGLE, h[1].key = NULL, h[1].label = "Options";
	h[2].button = -1, h[2].key = "SELECT", h[2].label = "Files";
	h[3].button = -1, h[3].key = "START", h[3].label = "Menu";
	dashDrawHints(h, 4);
}

static const DashScreen dashDevicesGridScreen = {devEnter, devInput, devUpdate, devDraw, devHints};

static void storageEnter(void)
{
	if (dashCfg.files_mode)
		dashDevicesGridScreen.enter();
	else
		dashFilesListScreen.enter();
}

static void storageInput(const DashInput *in)
{
	if (in->pressed & DASH_IN_SELECT) {
		dashCfg.files_mode = !dashCfg.files_mode;
		dashStoreSaveSettings();
		dashUiFocusPoke();
		storageEnter();
		return;
	}
	if (dashCfg.files_mode)
		dashDevicesGridScreen.input(in);
	else
		dashFilesListScreen.input(in);
}

static void storageUpdate(float dt)
{
	if (dashCfg.files_mode)
		dashDevicesGridScreen.update(dt);
	else
		dashFilesListScreen.update(dt);
}

static void storageDraw(void)
{
	if (dashCfg.files_mode)
		dashDevicesGridScreen.draw();
	else
		dashFilesListScreen.draw();
}

static void storageHints(void)
{
	if (dashCfg.files_mode)
		dashDevicesGridScreen.hints();
	else
		dashFilesListScreen.hints();
}

const DashScreen dashFilesScreen = {storageEnter, storageInput, storageUpdate, storageDraw, storageHints};
const DashScreen dashDevicesScreen = {storageEnter, storageInput, storageUpdate, storageDraw, storageHints};
//--------------------------------------------------------------
//End of file: dash_files.c
//--------------------------------------------------------------
