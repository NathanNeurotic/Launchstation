//--------------------------------------------------------------
//File name:   dash_home.c
//HOME: the landing page.
//  * hero panel describing the focused item
//  * "Continue" carousel: pinned apps, then recent, then favorites
//  * unified full application library subview (all filters/views retained)
//  * direct chips: full library, Last folder, configured launch keys
//  * compact live device status
//--------------------------------------------------------------
#include "dash_internal.h"

#define HOME_MAX_APPS 24

extern char LastDir[MAX_NAME];
#define HOME_MAX_CHIPS 24

enum ChipKind {
	CHIP_LIBRARY = 0,
	CHIP_LAST_FOLDER,
	CHIP_LAUNCHKEY
};

typedef struct
{
	int kind;
	int lk;
	int icon;
	char label[48];
	char desc[96];
} Chip;

static DashApp *home_apps[HOME_MAX_APPS];
static u8 home_badge[HOME_MAX_APPS]; /* 1 pinned, 2 recent, 3 favorite */
static DashApp home_tmp[HOME_MAX_APPS];
static int nhome;
static Chip chips[HOME_MAX_CHIPS];
static int nchips;

static int row;
static int library_open; /* full Apps screen nested inside Home */
static int row_forced; /* row 1 chosen only because Continue was empty */
static int col[2];
static float col_anim[2];
static float cam[2];
static float row_anim;

static const char *const lk_names[SETTING_LK_BTN_COUNT] = {
    "Auto", "Circle", "Cross", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT", "Left", "Right"};

static int homeHas(const char *path)
{
	int i;

	for (i = 0; i < nhome; i++)
		if (!stricmp(home_apps[i]->path, path))
			return 1;
	return 0;
}

static void homeAdd(const char *path, int badge)
{
	if (nhome >= HOME_MAX_APPS || path[0] == '\0' || homeHas(path))
		return;
	/* Pins, recents and favorites can outlive the device they came from.
	 * Keep the shortcut stored, but do not expose it until that device is
	 * actually mounted again. */
	if (!dashDevicePathReady(path))
		return;
	home_apps[nhome] = dashLibResolve(path, &home_tmp[nhome]);
	home_badge[nhome] = (u8)badge;
	nhome++;
}

static void addChip(int kind, int lk, int icon, const char *label, const char *desc)
{
	Chip *c;

	if (nchips >= HOME_MAX_CHIPS)
		return;
	c = &chips[nchips++];
	c->kind = kind;
	c->lk = lk;
	c->icon = icon;
	dashStrCopy(c->label, label, sizeof(c->label));
	dashStrCopy(c->desc, desc, sizeof(c->desc));
}

static void homeRebuild(void)
{
	int i;
	char keep[DASH_PATH_MAX];
	int old_col = col[0];

	keep[0] = '\0';
	if (nhome > 0 && col[0] >= 0 && col[0] < nhome)
		dashStrCopy(keep, home_apps[col[0]]->path, sizeof(keep));

	nhome = 0;
	for (i = 0; i < dashLists.npin; i++)
		homeAdd(dashLists.pin[i], 1);
	for (i = 0; i < dashAppCount; i++)
		if (dashApps[i].flags & DASH_APP_META_PIN)
			homeAdd(dashApps[i].path, 1);
	for (i = 0; i < dashLists.nrecent && i < dashCfg.recent_count; i++)
		homeAdd(dashLists.recent[i], 2);
	for (i = 0; i < dashLists.nfav; i++)
		homeAdd(dashLists.fav[i], 3);
	/* a small library sample keeps the row useful on first boot */
	for (i = 0; i < dashAppCount && nhome < 6; i++)
		homeAdd(dashApps[i].path, 0);

	nchips = 0;
	/* These two navigation shortcuts remain available even when the user
	 * disables the optional launch-key shortcuts. */
	addChip(CHIP_LIBRARY, -1, DI_APPS, "Applications", "Browse all applications in Grid, List or Carousel view.");
	if (LastDir[0] && strchr(LastDir, ':') != NULL)
		addChip(CHIP_LAST_FOLDER, -1, DI_CLOCK, "Last folder", LastDir);
	for (i = 1; dashCfg.home_shortcuts && i < SETTING_LK_BTN_COUNT; i++) {
		const char *p = setting->LK_Path[i];
		char name[DASH_TITLE_MAX], label[48], desc[96];

		if (!p[0] || ((i == SETTING_LK_LEFT || i == SETTING_LK_RIGHT) && !setting->LK_Flag[i]))
			continue;
		if (setting->LK_Title[i][0])
			dashStrCopy(name, setting->LK_Title[i], sizeof(name));
		else if (strchr(p, ':') != NULL)
			dashLibPrettyName(p, name, sizeof(name));
		else {
			const char *slash = strrchr(p, '/');
			dashStrCopy(name, slash ? slash + 1 : p, sizeof(name));
		}
		dashStrCopy(label, name, sizeof(label));
		snprintf(desc, sizeof(desc), "Launch key %s: %s", lk_names[i], p);
		addChip(CHIP_LAUNCHKEY, i, DI_KEY, label, desc);
	}

	for (i = 0; i < nhome && keep[0]; i++)
		if (!stricmp(home_apps[i]->path, keep)) {
			col[0] = i;
			break;
		}
	if (col[0] >= nhome)
		col[0] = nhome > 0 ? nhome - 1 : 0;
	if (col[0] != old_col) {
		float target = (col[0] - 2) * 104.0f;
		float maxcam = (nhome - 5) * 104.0f;
		col_anim[0] = (float)col[0];
		cam[0] = target > maxcam ? maxcam : target;
		if (cam[0] < 0)
			cam[0] = 0;
	}
	if (col[1] >= nchips)
		col[1] = nchips - 1;
	if (nhome == 0)
		row = nchips > 0 ? 1 : 0;
	else if (row_forced)
		row = 0;
	row_forced = (nhome == 0 && nchips > 0);
}

static void homeEnter(void)
{
	library_open = 0;
	homeRebuild();
	if (nhome > 0 || nchips == 0)
		row = 0;
}

static DashApp *focusedApp(void)
{
	if (row == 0 && nhome > 0)
		return home_apps[col[0]];
	return NULL;
}

static void activateChip(const Chip *c)
{
	switch (c->kind) {
		case CHIP_LIBRARY:
			library_open = 1;
			if (dashAppsScreen.enter)
				dashAppsScreen.enter();
			dashUiFocusPoke();
			break;
		case CHIP_LAST_FOLDER: {
			/* LastDir can change after the previous file operation. Copy it
			 * because the File Browser updates this same global as it enters. */
			char dir[MAX_NAME];
			dashStrCopy(dir, LastDir, sizeof(dir));
			if (dir[0] && strchr(dir, ':') != NULL)
				dashNavigateFilesTab(dir);
			/* A successful switch already leaves Home. Rebuilding now would
			 * access device status while the new Files listing is in flight. */
			if (dashCurrentSection() == DASH_SEC_HOME)
				homeRebuild();
			break;
		}
		case CHIP_LAUNCHKEY: {
			const char *p = setting->LK_Path[c->lk];
			if (strchr(p, ':') != NULL && genCmpFileExt(p, "ELF")) {
				DashApp tmp, *a = dashLibResolve(p, &tmp);
				if (setting->LK_Title[c->lk][0])
					dashStrCopy(a->title, setting->LK_Title[c->lk], sizeof(a->title));
				dashLaunchApp(a, 0);
			} else
				dashRunMisc(p);
			homeRebuild();
			break;
		}
	}
}

static void homeInput(const DashInput *in)
{
	int n = (row == 0) ? nhome : nchips;

	if (library_open) {
		if (in->pressed & DASH_IN_BACK) {
			library_open = 0;
			homeRebuild();
			dashUiFocusPoke();
		} else if (dashAppsScreen.input)
			dashAppsScreen.input(in);
		return;
	}

	if (in->pressed & DASH_IN_UP) {
		if (row == 1 && nhome > 0) {
			row = 0;
			dashUiFocusPoke();
		}
	} else if (in->pressed & DASH_IN_DOWN) {
		if (row == 0) {
			row = 1;
			dashUiFocusPoke();
		}
	} else if (in->pressed & DASH_IN_LEFT) {
		if (col[row] > 0) {
			col[row]--;
			dashUiFocusPoke();
		}
	} else if (in->pressed & DASH_IN_RIGHT) {
		if (col[row] < n - 1) {
			col[row]++;
			dashUiFocusPoke();
		}
	} else if (in->pressed & DASH_IN_L2) {
		if (col[row] != 0)
			dashUiFocusPoke();
		col[row] = 0;
	} else if (in->pressed & DASH_IN_R2) {
		if (col[row] != (n > 0 ? n - 1 : 0))
			dashUiFocusPoke();
		col[row] = n > 0 ? n - 1 : 0;
	} else if (in->pressed & DASH_IN_OK) {
		if (row == 0 && nhome > 0) {
			dashLaunchApp(home_apps[col[0]], 0);
			homeRebuild();
		} else if (row == 1 && nchips > 0)
			activateChip(&chips[col[1]]);
	} else if (in->pressed & DASH_IN_FAV) {
		DashApp *a = focusedApp();
		if (a != NULL) {
			int r = dashStoreToggleFavorite(a->path);
			if (r < 0)
				dashToast(DASH_TOAST_INFO, "Favorites full");
			else {
				dashToast(r > 0 ? DASH_TOAST_OK : DASH_TOAST_INFO, r > 0 ? "Added to Favorites" : "Removed from Favorites");
				homeRebuild();
			}
		}
	} else if (in->pressed & DASH_IN_MENU) {
		DashApp *a = focusedApp();
		if (a != NULL) {
			dashAppActions(a);
			homeRebuild();
		}
	}
}

static void homeUpdate(float dt)
{
	int r;
	float sp = dashAnimSpeed();

	if (library_open) {
		if (dashAppsScreen.update)
			dashAppsScreen.update(dt);
		return;
	}

	row_anim = dashApproach(row_anim, (float)row, 12.0f * sp, dt);
	for (r = 0; r < 2; r++) {
		float slot = (r == 0) ? 104.0f : 0.0f;
		col_anim[r] = dashApproach(col_anim[r], (float)col[r], 14.0f * sp, dt);
		if (r == 0) {
			float target = (col[0] - 2) * slot;
			float maxcam = (nhome - 5) * slot;
			if (target > maxcam)
				target = maxcam;
			if (target < 0)
				target = 0;
			cam[0] = dashApproach(cam[0], target, 10.0f * sp, dt);
		}
	}
}

static void drawBadge(float x, float y, int badge)
{
	/* Only pins and favorites are marked; recents need no badge. */
	if (badge != 1 && badge != 3)
		return;
	dashIcon(badge == 1 ? DI_PIN : DI_STAR, x + 3, y + 3, 13, badge == 3 ? DASH_C_WARM : DASH_C_ACCENT);
}

static void drawHero(void)
{
	DashApp *a = focusedApp();

	dashThemeSetFocusApp(a);
	const float x = 48, y = 78, art = 120;
	const float tx = 190, tw = 400;

	if (a != NULL) {
		char meta[128], dev[48];
		float title_fs = dashTextWidth(a->title, DASH_FS_HERO) > tw ? DASH_FS_TITLE : DASH_FS_HERO;

		dashDrawAppArt(a, x, y, art, art, 1.0f, 0);
		dashTextScroll(tx, y + 6, tw, a->title, title_fs, DASH_C_TEXT);
		dashLibDeviceLabel(a->path, dev, sizeof(dev));
		snprintf(meta, sizeof(meta), "%s%s%s%s%s", dev, a->version[0] ? "   " : "", a->version,
		         a->author[0] ? "   " : "", a->author);
		dashIcon(dashLibDeviceIcon(a->path), tx, y + 48, 13, DASH_C_ACCENT);
		{
			int favorite = dashStoreIsFavorite(a->path);
			float meta_w = tw - 40 - (favorite ? 24.0f : 0.0f);
			dashTextScroll(tx + 19, y + 45, meta_w, meta, DASH_FS_SMALL, DASH_C_TEXT2);
			if (favorite)
				dashIcon(DI_STAR, tx + tw - 16, y + 48, 13, DASH_C_WARM);
		}
		if (a->desc[0])
			dashTextWrap(tx, y + 70, tw, 2, a->desc, DASH_FS_SMALL, DASH_C_TEXT3, 19);
	} else if (row == 1 && nchips > 0) {
		const Chip *c = &chips[col[1]];
		DashColor top = dashColorMix(DASH_C_PANEL_HI, DASH_C_SELECT, 0.5f), bot = dashColorScale(DASH_C_PANEL_HI, 0.7f);
		top.a = bot.a = 210;
		dashPanelGradV(x, y, art, art, 16, top, bot);
		if (DASH_C_BORDER.a)
			dashPanelBorder(x, y, art, art, 16, DASH_C_BORDER);
		dashAccentGlow(x + art * 0.5f, y + art * 0.5f, 64, 64, 1.0f);
		dashIcon(c->icon, x + 34, y + 34, art - 68, DASH_C_TEXT);
		dashTextScroll(tx, y + 6, tw, c->label, dashTextWidth(c->label, DASH_FS_HERO) > tw ? DASH_FS_TITLE : DASH_FS_HERO, DASH_C_TEXT);
		dashTextWrap(tx, y + 48, tw, 2, c->desc, DASH_FS_SMALL, DASH_C_TEXT3, 19);
	}
}

#define HOME_TILE 86.0f
#define HOME_TILE_ZOOM 0.12f

static void homeDraw(void)
{
	int i;

	if (library_open) {
		if (dashAppsScreen.draw)
			dashAppsScreen.draw();
		return;
	}
	/* Row geometry: the focused tile grows by HOME_TILE_ZOOM, so keep about
	 * 8 px between its edges and the row labels/underlines above and below
	 * (the 1.14x zoom of a 90 px tile used to touch both). */
	float y0 = 236, cw = HOME_TILE;

	drawHero();

	/* Continue row */
	dashText(48, y0 - 33, "Continue", DASH_FS_SMALL, row == 0 ? DASH_C_TEXT : DASH_C_TEXT3);
	if (row == 0)
		dashRect(48, y0 - 15, dashTextWidth("Continue", DASH_FS_SMALL), 2, DASH_C_ACCENT);
	if (nhome == 0) {
		dashDrawPanelBox(48, y0, 552, cw, 14);
		dashIcon(DI_CLOCK, 70, y0 + 25, 40, DASH_C_TEXT3);
		dashText(126, y0 + 20, "Nothing here yet", DASH_FS_BODY, DASH_C_TEXT2);
		dashText(126, y0 + 48, "Started, pinned and favorite apps will appear here.", DASH_FS_SMALL, DASH_C_TEXT3);
	} else {
		dashGfxScissor(DASH_SAFE_L - 8, y0 - 14, DASH_SAFE_R - DASH_SAFE_L + 16, 130);
		for (i = 0; i < nhome; i++) {
			float x = 48 + i * 104 - cam[0];
			float f = (row == 0) ? dashClampf(1.0f - fabsf(i - col_anim[0]), 0.0f, 1.0f) : 0.0f;
			float s = 1.0f + HOME_TILE_ZOOM * f;
			float w = cw * s;
			float cx = x + cw * 0.5f, cy = y0 + cw * 0.5f;

			if (x < -120 || x > DASH_VW + 20)
				continue;
			dashGfxPushAlpha(row == 0 ? 1.0f : 0.72f);
			dashDrawAppArt(home_apps[i], cx - w * 0.5f, cy - w * 0.5f, w, w, f, abs(i - col[0]) + 1);
			if (f > 0.02f)
				dashDrawTileFocus(cx - w * 0.5f, cy - w * 0.5f, w, w, w * 0.1f, f);
			drawBadge(cx + w * 0.5f - 16, cy - w * 0.5f - 4, home_badge[i]);
			dashGfxPopAlpha();
		}
		dashGfxScissorReset();
	}

	/* Direct launch actions are optional for browser-centric users. */
	if (nchips > 0) {
		float cy = 361;
		float x = 48;
		float focus_x = 0, focus_w = 0;
		float label_x = 0, label_w = 0;
		static float chip_cam;
		float target;

		dashText(48, cy - 24, "Browse & shortcuts", DASH_FS_SMALL, row == 1 ? DASH_C_TEXT : DASH_C_TEXT3);
		if (row == 1)
			dashRect(48, cy - 6, dashTextWidth("Browse & shortcuts", DASH_FS_SMALL), 2, DASH_C_ACCENT);
		/* measure focus position for horizontal scrolling */
		for (i = 0; i < nchips; i++) {
			float w = dashTextWidth(chips[i].label, DASH_FS_SMALL) + 46;
			if (w > 170)
				w = 170;
			if (i == col[1]) {
				focus_x = x;
				focus_w = w;
			}
			x += w + 8;
		}
		target = focus_x + focus_w - (DASH_SAFE_R - 48);
		if (target < 0)
			target = 0;
		chip_cam = dashApproach(chip_cam, target, 10.0f * dashAnimSpeed(), dashFrameDelta());
		dashGfxScissor(DASH_SAFE_L - 6, cy - 6, DASH_SAFE_R - DASH_SAFE_L + 12, 44);
		x = 48 - chip_cam;
		for (i = 0; i < nchips; i++) {
			float w = dashTextWidth(chips[i].label, DASH_FS_SMALL) + 46;
			int foc = (row == 1 && i == col[1]);
			if (w > 170)
				w = 170;
			if (dashCfg.icon_backing && (DASH_C_PANEL.a || dashTheme.highlight != DASH_HL_TEXT))
				dashPanel(x, cy, w, 32, 16, dashColorA(DASH_C_TEXT, 0.055f));
			if (foc)
				dashDrawSelection(x, cy, w, 32, 16, 1.0f);
			dashIcon(chips[i].icon, x + 12, cy + 8, 16, dashItemColor(foc));
			if (foc) {
				label_x = x + 34;
				label_w = w - 44;
			} else
				dashTextFit(x + 34, cy + 6.5f, w - 44, chips[i].label, DASH_FS_SMALL, dashItemColor(0));
			x += w + 8;
		}
		dashGfxScissorReset();
		if (row == 1 && label_w > 0) {
			if (label_x < DASH_SAFE_L - 6) {
				label_w -= DASH_SAFE_L - 6 - label_x;
				label_x = DASH_SAFE_L - 6;
			}
			if (label_x + label_w > DASH_SAFE_R + 6)
				label_w = DASH_SAFE_R + 6 - label_x;
			dashTextScroll(label_x, cy + 6.5f, label_w, chips[col[1]].label, DASH_FS_SMALL, dashItemColor(1));
		}
	}
}

static void homeHints(void)
{
	DashHint h[5];

	if (library_open) {
		if (dashAppsScreen.hints)
			dashAppsScreen.hints();
		return;
	}
	int n = 0;

	h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = (row == 0) ? "Start" : "Open", n++;
	if (row == 0 && nhome > 0) {
		h[n].button = DI_BTN_SQUARE, h[n].key = NULL, h[n].label = "Favorite", n++;
		h[n].button = DI_BTN_TRIANGLE, h[n].key = NULL, h[n].label = "Options", n++;
	}
	h[n].button = -1, h[n].key = "START", h[n].label = "Menu", n++;
	/* Advertise the R3 display toggle on HOME, not only the way back
	 * after the interface has already been hidden (#137). */
	h[n].button = -1, h[n].key = "R3", h[n].label = "Show background", n++;
	dashDrawHints(h, n);
}

const DashScreen dashHomeScreen = {homeEnter, homeInput, homeUpdate, homeDraw, homeHints};
//--------------------------------------------------------------
//End of file: dash_home.c
//--------------------------------------------------------------

void dashHomeRebuild(void)
{
	homeRebuild();
}
