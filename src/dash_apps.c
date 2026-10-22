//--------------------------------------------------------------
//File name:   dash_apps.c
//APPS: the application library with three presentations sharing one
//filtered index and one artwork cache:
//  GRID      5 x 3 artwork cards
//  LIST      compact rows + large detail panel
//  CAROUSEL  horizontal collection with depth (PSBBN/LUNA inspired)
//SELECT cycles the view (saved), Triangle opens options.
//--------------------------------------------------------------
#include "dash_internal.h"

void dashHomeRebuild(void);

enum AppFilter {
	FILTER_ALL = 0,
	FILTER_FAV,
	FILTER_MC,
	FILTER_USB,
	FILTER_HDD,
	FILTER_OTHER,
	FILTER_COUNT
};

static const char *const filter_names[FILTER_COUNT] = {"All", "Favorites", "Memory Cards", "MASS", "HDD", "Other devices"};

static int order[DASH_MAX_APPS];
static int norder;
static int sel;
static float sel_anim;
static float grid_cam;
static int filter;
static float view_anim = 1.0f;

#define GRID_COLS 5
#define GRID_PITCH_X 110.0f
#define GRID_PITCH_Y 100.0f
#define GRID_CARD 84.0f
#define GRID_TOP 104.0f
#define LIST_ROWS 9
#define LIST_ROW_H 31.0f

static int matchFilter(const DashApp *a)
{
	int icon;

	/* Cached applications remain in DASHLIB.CNF, but are only visible while
	 * the storage that owns their path is currently mounted/ready. */
	if (!dashDevicePathReady(a->path))
		return 0;

	switch (filter) {
		case FILTER_FAV:
			return dashStoreIsFavorite(a->path) || dashStoreIsPinned(a->path);
		case FILTER_MC:
			return !strncmp(a->path, "mc", 2);
		case FILTER_USB:
			return !strncmp(a->path, "mass", 4) || !strncmp(a->path, "usb", 3);
		case FILTER_HDD:
			icon = dashLibDeviceIcon(a->path);
			return icon == DI_HDD;
		case FILTER_OTHER:
			icon = dashLibDeviceIcon(a->path);
			return icon != DI_HDD && icon != DI_MC && icon != DI_USB;
		default:
			return 1;
	}
}

void dashAppsRebuildView(void)
{
	int i;
	char keep[DASH_PATH_MAX];

	keep[0] = '\0';
	if (sel >= 0 && sel < norder && order[sel] < dashAppCount)
		dashStrCopy(keep, dashApps[order[sel]].path, sizeof(keep));
	norder = 0;
	for (i = 0; i < dashAppCount; i++)
		if (matchFilter(&dashApps[i]))
			order[norder++] = i;
	sel = 0;
	for (i = 0; i < norder && keep[0]; i++)
		if (!stricmp(dashApps[order[i]].path, keep))
			sel = i;
	if (sel >= norder)
		sel = norder > 0 ? norder - 1 : 0;
	dashHomeRebuild();
}

void dashAppsFocusPath(const char *path)
{
	int i;

	for (i = 0; i < norder; i++)
		if (!stricmp(dashApps[order[i]].path, path))
			sel = i;
}

int dashAppsVisibleCount(void)
{
	return norder;
}

static DashApp *selApp(void)
{
	if (norder <= 0 || sel < 0 || sel >= norder)
		return NULL;
	return &dashApps[order[sel]];
}

//--------------------------------------------------------------
// Per-application options (shared with HOME)
//--------------------------------------------------------------
static void showDetails(DashApp *a)
{
	char body[640], dev[48], meta[MAX_PATH];
	int len;

	dashLibDeviceLabel(a->path, dev, sizeof(dev));
	len = snprintf(body, sizeof(body), "Location: %s\nFile: %s\n", dev, a->path);
	if (a->author[0])
		len += snprintf(body + len, sizeof(body) - len, "Author: %s\n", a->author);
	if (a->version[0])
		len += snprintf(body + len, sizeof(body) - len, "Version: %s\n", a->version);
	if (a->category[0])
		len += snprintf(body + len, sizeof(body) - len, "Category: %s\n", a->category);
	len += snprintf(body + len, sizeof(body) - len, "Artwork: %s\n",
	                a->art_state == DASH_ART_FOUND ? a->art : (a->art_state == DASH_ART_NONE ? "none (generated tile)" : "not checked yet"));
	dashLibMetadataPath(a, meta, sizeof(meta));
	snprintf(body + len, sizeof(body) - len, "Metadata: %s%s", (a->flags & DASH_APP_HAS_META) ? "" : "none - create ", meta);
	dashModalMessage(DI_INFO, a->title, body, a->desc[0] ? a->desc : NULL);
}

static void openFolder(const DashApp *a)
{
	char dir[DASH_PATH_MAX];
	const char *slash = strrchr(a->path, '/');
	int n = slash ? (int)(slash - a->path) + 1 : 0;

	if (n <= 0 || n >= (int)sizeof(dir))
		return;
	memcpy(dir, a->path, n);
	dir[n] = '\0';
	dashNavigateFilesTab(dir);
}

static void editMetadata(DashApp *a)
{
	char meta[MAX_PATH];

	if (dashLibMetadataPath(a, meta, sizeof(meta)) < 0)
		return;
	if (!(a->flags & DASH_APP_HAS_META)) {
		/* create a starter sidecar the user can fill in */
		char text[512];
		int len = snprintf(text, sizeof(text),
		                   "# Launchstation dashboard metadata for %s\r\n"
		                   "# Every key is optional. Lines starting with # are ignored.\r\n"
		                   "title = %s\r\n"
		                   "description = \r\n"
		                   "author = \r\n"
		                   "version = \r\n"
		                   "category = \r\n"
		                   "# cover = cover.png\r\n"
		                   "# arg = first argument (one per line)\r\n",
		                   a->path, a->title);
		if (!dashModalConfirm("Create metadata file?", "A small text file describing this application will be created next to it. You can then edit it in the Text Editor.", "Create", 0))
			return;
		/* the UI thread writes directly: background work must finish first */
		if (!dashWorkerAwaitAll("Finishing up"))
			return;
		filerRootMcPollIdle();
		if (dashWriteFile(meta, text, len) != 0) {
			dashModalMessage(DI_WARNING, "Could not create file", "The device may be read-only or full.", meta);
			return;
		}
	}
	if (!dashOpenTextEditor(meta))
		return;
	/* re-read this application's metadata after editing (the UI thread
	 * reads it, so a save still running in the background finishes first) */
	if (dashLibIndexOf(a) >= 0 && dashWorkerAwaitAll("Finishing up"))
		dashLibRefreshApp(a);
}

void dashAppActions(DashApp *a)
{
	int fav = dashStoreIsFavorite(a->path);
	int pin = dashStoreIsPinned(a->path);
	int in_lib = dashLibIndexOf(a) >= 0;
	DashMenuItem items[9];
	int ids[9];
	int n = 0, r;

	items[n].icon = DI_PLAY, items[n].label = "Start", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 0;
	items[n].icon = DI_EDIT, items[n].label = "Start with arguments...", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 1;
	items[n].icon = DI_STAR, items[n].label = fav ? "Remove from Favorites" : "Add to Favorites", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 2;
	items[n].icon = DI_PIN, items[n].label = pin ? "Unpin from Home" : "Pin to Home", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 3;
	items[n].icon = DI_INFO, items[n].label = "Details", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 4;
	items[n].icon = DI_FOLDER, items[n].label = "Show in File Browser", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 5;
	items[n].icon = DI_TEXT, items[n].label = "Edit metadata", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 6;
	if (in_lib)
		items[n].icon = DI_CLOSE, items[n].label = "Hide from library", items[n].detail = "file kept", items[n].disabled = 0, ids[n++] = 7;
	else
		items[n].icon = DI_CLOSE, items[n].label = "Remove shortcut", items[n].detail = NULL, items[n].disabled = 0, ids[n++] = 8;

	r = dashModalMenu(a->title, a->path, items, n, 0);
	if (r < 0)
		return;
	switch (ids[r]) {
		case 0:
			dashLaunchApp(a, 0);
			break;
		case 1:
			dashLaunchApp(a, 1);
			break;
		case 2:
			r = dashStoreToggleFavorite(a->path);
			dashToast(r > 0 ? DASH_TOAST_OK : DASH_TOAST_INFO, r > 0 ? "Added to Favorites" : "Removed from Favorites");
			break;
		case 3:
			r = dashStoreTogglePinned(a->path);
			dashToast(r > 0 ? DASH_TOAST_OK : DASH_TOAST_INFO, r > 0 ? "Pinned to Home" : "Unpinned from Home");
			break;
		case 4:
			showDetails(a);
			break;
		case 5:
			openFolder(a);
			break;
		case 6:
			editMetadata(a);
			break;
		case 7:
			if (dashModalConfirm("Hide from library?", "The application stays on your device and will reappear after the next library refresh.", "Hide", 0)) {
				dashLibRemove(dashLibIndexOf(a));
				dashAppsRebuildView();
			}
			break;
		case 8:
			dashStoreRemoveEverywhere(a->path);
			dashToast(DASH_TOAST_INFO, "Shortcut removed");
			break;
	}
	dashAppsRebuildView();
}

static void appsMenu(void)
{
	DashApp *a = selApp();
	DashMenuItem items[4];
	int n = 0, r;
	char filt[48];

	snprintf(filt, sizeof(filt), "Show: %s", filter_names[filter]);
	if (a != NULL) {
		items[n].icon = DI_APP, items[n].label = "Application options...", items[n].detail = NULL, items[n].disabled = 0, n++;
	}
	items[n].icon = DI_VIEW_GRID + dashCfg.view, items[n].label = "Change view", items[n].detail = NULL, items[n].disabled = 0, n++;
	items[n].icon = DI_STAR, items[n].label = filt, items[n].detail = NULL, items[n].disabled = 0, n++;
	items[n].icon = DI_REFRESH, items[n].label = "Refresh library", items[n].detail = "scan now", items[n].disabled = 0, n++;

	r = dashModalMenu("Applications", NULL, items, n, 0);
	if (r < 0)
		return;
	if (a == NULL)
		r++;
	switch (r) {
		case 0:
			dashAppActions(a);
			break;
		case 1: {
			static const char *const views[DASH_VIEW_COUNT] = {"Grid", "List", "Carousel"};
			int v = dashModalChoice("View", views, DASH_VIEW_COUNT, dashCfg.view);
			if (v >= 0 && v != dashCfg.view) {
				dashCfg.view = v;
				view_anim = 0.0f;
				dashStoreSaveSettings();
			}
			break;
		}
		case 2: {
			int f = dashModalChoice("Show applications", filter_names, FILTER_COUNT, filter);
			if (f >= 0) {
				filter = f;
				dashCfg.app_filter = f;
				dashStoreSaveSettings();
				dashAppsRebuildView();
			}
			break;
		}
		case 3:
			dashRescanLibrary(1);
			break;
	}
}

//--------------------------------------------------------------
static void appsEnter(void)
{
	filter = dashCfg.app_filter;
	if (filter < 0 || filter >= FILTER_COUNT)
		filter = FILTER_ALL;
	dashAppsRebuildView();
	sel_anim = (float)sel;
}

static void move(int delta)
{
	int next;
	int old = sel;

	if (norder <= 0)
		return;
	next = (sel + delta) % norder;
	if (next < 0)
		next += norder;
	sel = next;
	if (sel != old)
		dashUiFocusPoke();
}

static void appsInput(const DashInput *in)
{
	int v = dashCfg.view;

	if (norder == 0) {
		if (in->pressed & DASH_IN_OK)
			dashRescanLibrary(1);
		else if (in->pressed & DASH_IN_MENU)
			appsMenu();
		else if ((in->pressed & DASH_IN_SELECT) && filter != FILTER_ALL) {
			filter = FILTER_ALL;
			dashCfg.app_filter = FILTER_ALL;
			dashStoreSaveSettings();
			dashAppsRebuildView();
		}
		return;
	}
	if (v == DASH_VIEW_GRID) {
		if (in->pressed & DASH_IN_LEFT)
			move(-1);
		if (in->pressed & DASH_IN_RIGHT)
			move(1);
		if (in->pressed & DASH_IN_UP)
			move(-GRID_COLS);
		if (in->pressed & DASH_IN_DOWN)
			move(GRID_COLS);
		if (in->pressed & DASH_IN_L2)
			move(-GRID_COLS * 3);
		if (in->pressed & DASH_IN_R2)
			move(GRID_COLS * 3);
	} else if (v == DASH_VIEW_LIST) {
		if (in->pressed & DASH_IN_UP)
			move(-1);
		if (in->pressed & DASH_IN_DOWN)
			move(1);
		if (in->pressed & (DASH_IN_L2 | DASH_IN_LEFT))
			move(-LIST_ROWS);
		if (in->pressed & (DASH_IN_R2 | DASH_IN_RIGHT))
			move(LIST_ROWS);
	} else {
		if (in->pressed & (DASH_IN_LEFT | DASH_IN_UP))
			move(-1);
		if (in->pressed & (DASH_IN_RIGHT | DASH_IN_DOWN))
			move(1);
		if (in->pressed & DASH_IN_L2)
			move(-5);
		if (in->pressed & DASH_IN_R2)
			move(5);
	}
	if (in->pressed & DASH_IN_OK)
		dashLaunchApp(selApp(), 0);
	else if (in->pressed & DASH_IN_FAV) {
		DashApp *a = selApp();
		int r = dashStoreToggleFavorite(a->path);
		dashToast(r > 0 ? DASH_TOAST_OK : DASH_TOAST_INFO, r > 0 ? "Added to Favorites" : "Removed from Favorites");
		if (filter == FILTER_FAV)
			dashAppsRebuildView();
		else
			dashHomeRebuild();
	} else if (in->pressed & DASH_IN_MENU)
		appsMenu();
	else if (in->pressed & DASH_IN_SELECT) {
		dashCfg.view = (dashCfg.view + 1) % DASH_VIEW_COUNT;
		view_anim = 0.0f;
		dashStoreSaveSettings();
	}
}

static void appsUpdate(float dt)
{
	float sp = dashAnimSpeed();
	float row = (float)(sel / GRID_COLS);
	float target = (row - 1.0f) * GRID_PITCH_Y;
	float maxcam = ((norder - 1) / GRID_COLS - 2) * GRID_PITCH_Y;

	sel_anim = dashApproach(sel_anim, (float)sel, 14.0f * sp, dt);
	if (target > maxcam)
		target = maxcam;
	if (target < 0.0f)
		target = 0.0f;
	grid_cam = dashApproach(grid_cam, target, 11.0f * sp, dt);
	view_anim = dashApproach(view_anim, 1.0f, 8.0f * sp, dt);
}

//--------------------------------------------------------------
// Views
//--------------------------------------------------------------
static void drawTitleBar(void)
{
	char sub[64];
	DashApp *a = selApp();
	int v;

	dashThemeSetFocusApp(a);

	if (filter != FILTER_ALL)
		snprintf(sub, sizeof(sub), "%d shown - %s", norder, filter_names[filter]);
	else
		snprintf(sub, sizeof(sub), "%d %s", norder, norder == 1 ? "application" : "applications");
	dashDrawSectionTitle(48, 68, "Applications", sub);
	for (v = 0; v < DASH_VIEW_COUNT; v++) {
		float x = 526 + v * 26;
		int on = (v == dashCfg.view);
		if (on)
			dashDrawSelection(x - 4, 70, 26, 26, 8, 1.0f);
		dashIcon(DI_VIEW_GRID + v, x, 74, 18, on ? DASH_C_SELTEXT : DASH_C_TEXT3);
	}
	(void)a;
}

static void drawGrid(void)
{
	int i, first, last;
	float fx = 0, fy = 0;
	DashApp *a = selApp();

	first = (int)(grid_cam / GRID_PITCH_Y) * GRID_COLS - GRID_COLS;
	if (first < 0)
		first = 0;
	last = first + GRID_COLS * 5;
	if (last > norder)
		last = norder;

	dashGfxScissor(DASH_SAFE_L - 10, 98, DASH_SAFE_R - DASH_SAFE_L + 20, 296);
	for (i = first; i < last; i++) {
		int r = i / GRID_COLS, c = i % GRID_COLS;
		float x = 64 + c * GRID_PITCH_X;
		float y = GRID_TOP + 8 + r * GRID_PITCH_Y - grid_cam;
		float d = fabsf(i - sel_anim);
		float f = dashClampf(1.0f - d, 0.0f, 1.0f);
		float s = 1.0f + 0.13f * f;
		float w = GRID_CARD * s;
		float cx = x + GRID_CARD * 0.5f, cy = y + GRID_CARD * 0.5f;
		int prio = abs(i - sel);

		if (i == sel) {
			fx = cx;
			fy = cy;
		}
		dashDrawAppArt(&dashApps[order[i]], cx - w * 0.5f, cy - w * 0.5f, w, w, f, prio);
		if (dashStoreIsFavorite(dashApps[order[i]].path))
			dashIcon(DI_STAR, cx + w * 0.5f - 20, cy - w * 0.5f + 4, 16, DASH_C_WARM);
	}
	/* focus frame follows the animated selection */
	{
		int r = (int)(sel_anim + 0.5f);
		float fr = sel_anim - r;
		float x = 64 + (sel % GRID_COLS) * GRID_PITCH_X;
		float y = GRID_TOP + 8 + (sel / GRID_COLS) * GRID_PITCH_Y - grid_cam;
		float w = GRID_CARD * 1.13f;
		(void)fr;
		(void)fx;
		(void)fy;
		dashDrawTileFocus(x + GRID_CARD * 0.5f - w * 0.5f, y + GRID_CARD * 0.5f - w * 0.5f, w, w, w * 0.1f, 1.0f);
	}
	dashGfxScissorReset();
	dashDrawScrollbar(DASH_SAFE_R + 4, 104, 284, (int)(grid_cam / GRID_PITCH_Y), 3, (norder + GRID_COLS - 1) / GRID_COLS);

	if (a != NULL) {
		/* focused title, right-aligned next to the view switcher */
		float tw = dashTextWidth(a->title, DASH_FS_SMALL);
		float maxw = 200.0f;
		float x = 514.0f - (tw < maxw ? tw : maxw);
		dashIcon(dashLibDeviceIcon(a->path), x - 22, 76, 16, DASH_C_ACCENT);
		dashTextScroll(x, 73, maxw, a->title, DASH_FS_SMALL, DASH_C_TEXT);
	}
}

static void drawList(void)
{
	int top, i;
	DashApp *a = selApp();
	float lx = 48, ly = 104, lw = 290;

	top = sel - LIST_ROWS / 2;
	if (top > norder - LIST_ROWS)
		top = norder - LIST_ROWS;
	if (top < 0)
		top = 0;

	dashDrawPanelBox(lx - 6, ly - 6, lw + 12, LIST_ROWS * LIST_ROW_H + 12, 14);
	{
		float fy = ly + (sel_anim - top) * LIST_ROW_H;
		if (fy >= ly - LIST_ROW_H && fy < ly + LIST_ROWS * LIST_ROW_H) {
			dashGfxScissor(lx - 6, ly - 4, lw + 12, LIST_ROWS * LIST_ROW_H + 8);
			dashDrawSelection(lx, fy, lw, LIST_ROW_H - 3, 9, 1.0f);
			dashGfxScissorReset();
		}
	}
	for (i = top; i < norder && i < top + LIST_ROWS; i++) {
		DashApp *e = &dashApps[order[i]];
		float y = ly + (i - top) * LIST_ROW_H;
		int on = (i == sel);
		GSTEXTURE *t = dashArtGet(e, abs(i - sel) + 1);

		if (t != NULL)
			dashTexDrawFit(t, lx + 6, y + 2, 24, 24, DC(255, 255, 255, 255));
		else
			dashIcon(dashTheme.app_icon_style == DASH_APPICON_SYSTEM ? DI_EXEC : dashLibDeviceIcon(e->path), lx + 9, y + 5, 18,
			         dashTheme.app_icon_style == DASH_APPICON_MONOGRAM ? dashColorHue(e->hue, 120, 230, 255) : dashItemColor(on));
		if (on)
			dashTextScroll(lx + 38, y + 3, lw - 64, e->title, DASH_FS_SMALL, dashItemColor(1));
		else
			dashTextFit(lx + 38, y + 3, lw - 64, e->title, DASH_FS_SMALL, dashItemColor(0));
		if (dashStoreIsFavorite(e->path))
			dashIcon(DI_STAR, lx + lw - 22, y + 6, 14, DASH_C_WARM);
	}
	dashDrawScrollbar(lx + lw + 10, ly, LIST_ROWS * LIST_ROW_H, top, LIST_ROWS, norder);

	/* detail panel */
	if (a != NULL) {
		float dx = 362, dy = 98, dw = 238;
		char dev[48];

		dashDrawCard(dx, dy, dw, 290, 16);
		dashDrawAppArt(a, dx + (dw - 132) * 0.5f, dy + 14, 132, 132, 1.0f, 0);
		dashTextScroll(dx + 14, dy + 156, dw - 28, a->title, DASH_FS_LABEL, DASH_C_TEXT);
		dashLibDeviceLabel(a->path, dev, sizeof(dev));
		dashIcon(dashLibDeviceIcon(a->path), dx + 14, dy + 186, 14, DASH_C_ACCENT);
		dashTextFit(dx + 34, dy + 184, dw - 48, dev, DASH_FS_CAPTION, DASH_C_ACCENT);
		if (a->desc[0])
			dashTextWrap(dx + 14, dy + 206, dw - 28, 3, a->desc, DASH_FS_CAPTION, DASH_C_TEXT2, 19);
		else {
			char line[80];
			snprintf(line, sizeof(line), "%s%s%s", a->author, (a->author[0] && a->version[0]) ? " - " : "", a->version);
			if (line[0])
				dashTextFit(dx + 14, dy + 206, dw - 28, line, DASH_FS_CAPTION, DASH_C_TEXT2);
		}
		dashTextScroll(dx + 14, dy + 264, dw - 28, a->path, DASH_FS_CAPTION * 0.9f, DASH_C_TEXT3);
	}
}

static void drawCarousel(void)
{
	int i, k;
	DashApp *a = selApp();
	const float cx = 320.0f, cy = 206.0f;

	/* floor glow under the focused item */
	dashGfxBlend(DASH_BLEND_ADD);
	if (dashTheme.glow)
		dashGlow(cx, cy + 100, 170, 26, dashColorA(DASH_C_ACCENT, 0.24f));
	dashGfxBlend(DASH_BLEND_NORMAL);

	/* draw back to front: farthest items first */
	for (k = 4; k >= 0; k--) {
		for (i = -1; i <= 1; i += 2) {
			int idx = (k == 0) ? (int)(sel_anim + 0.5f) : 0;
			float off;
			if (k == 0 && i == 1)
				continue;
			if (k > 0)
				idx = (int)(sel_anim + 0.5f) + i * k;
			if (idx < 0 || idx >= norder)
				continue;
			off = idx - sel_anim;
			{
				float ad = fabsf(off);
				float s = (ad < 1.0f) ? dashLerpf(172.0f, 116.0f, ad) : dashLerpf(116.0f, 70.0f, dashClampf((ad - 1.0f) / 3.0f, 0.0f, 1.0f));
				float xo = (ad < 1.0f) ? off * 150.0f : (off > 0 ? 1 : -1) * (150.0f + (ad - 1.0f) * 88.0f);
				float alpha = dashClampf(1.25f - ad * 0.28f, 0.0f, 1.0f);
				float f = dashClampf(1.0f - ad, 0.0f, 1.0f);

				dashGfxPushAlpha(alpha);
				dashDrawAppArt(&dashApps[order[idx]], cx + xo - s * 0.5f, cy - s * 0.5f + ad * 6.0f, s, s, f, (int)(ad + 0.5f));
				/* Distance is already represented by alpha. A second solid
				 * rectangle tinted to bg2 created a black square around
				 * nonfocused artwork in Coverflow, especially with transparent
				 * or floating icon themes (#159). */
				dashGfxPopAlpha();
			}
		}
	}
	if (a != NULL) {
		dashDrawTileFocus(cx - 86, cy - 86, 172, 172, 17, 1.0f);
		dashTextScrollCenter(cx, 306, 500, a->title, DASH_FS_TITLE, DASH_C_TEXT);
		if (a->desc[0])
			dashTextScrollCenter(cx, 342, 500, a->desc, DASH_FS_SMALL, DASH_C_TEXT2);
		else {
			char dev[48];
			dashLibDeviceLabel(a->path, dev, sizeof(dev));
			dashTextCenter(cx, 342, dev, DASH_FS_SMALL, DASH_C_TEXT2);
		}
		{
			char pos[24];
			snprintf(pos, sizeof(pos), "%d / %d", sel + 1, norder);
			dashTextCenter(cx, 366, pos, DASH_FS_CAPTION, DASH_C_TEXT3);
		}
	}
}

static void appsDraw(void)
{
	drawTitleBar();
	if (norder == 0) {
		if (dashAppCount == 0)
			dashDrawEmptyState(320, 238, DI_APPS, "No applications found",
				                   "Add a folder in Settings > Home & applications > Application library, or browse to an ELF in File Browser. "
			                   "Press the confirm button to scan now.");
		else
			dashDrawEmptyState(320, 238, DI_STAR, filter == FILTER_FAV ? "No favorites yet" : "Nothing to show",
			                   filter == FILTER_FAV ? "Press Square on an application to add it to Favorites. Press SELECT to show all."
			                                        : "No applications match this filter. Press SELECT to show all.");
		return;
	}
	dashGfxPushAlpha(dashEaseOutCubic(view_anim));
	dashGfxPushOffset(0, 12.0f * (1.0f - dashEaseOutCubic(view_anim)));
	if (dashCfg.view == DASH_VIEW_LIST)
		drawList();
	else if (dashCfg.view == DASH_VIEW_CAROUSEL)
		drawCarousel();
	else
		drawGrid();
	dashGfxPopOffset();
	dashGfxPopAlpha();
}

static void appsHints(void)
{
	DashHint h[5];
	int n = 0;

	if (norder == 0) {
		h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = "Scan now", n++;
		h[n].button = DI_BTN_TRIANGLE, h[n].key = NULL, h[n].label = "Options", n++;
	} else {
		h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = "Start", n++;
		h[n].button = DI_BTN_SQUARE, h[n].key = NULL, h[n].label = "Favorite", n++;
		h[n].button = DI_BTN_TRIANGLE, h[n].key = NULL, h[n].label = "Options", n++;
		h[n].button = -1, h[n].key = "SELECT", h[n].label = "View", n++;
	}
	h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = "Home", n++;
	dashDrawHints(h, n);
}

const DashScreen dashAppsScreen = {appsEnter, appsInput, appsUpdate, appsDraw, appsHints};
//--------------------------------------------------------------
//End of file: dash_apps.c
//--------------------------------------------------------------
