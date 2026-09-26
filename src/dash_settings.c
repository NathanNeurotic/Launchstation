//--------------------------------------------------------------
//File name:   dash_settings.c
//SETTINGS: dashboard options plus the existing wLaunchELF settings,
//grouped into Display / Dashboard / Controls / Devices / Network /
//Advanced. Classic settings edit the same SETTING fields as the classic
//configuration screen and are written to LAUNCHELF.CNF only when the user
//saves (with the classic save-location prompt). Every classic sub-screen
//remains one press away, so no option is lost.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "config_private.h"

void dashLegacyBegin(void);
void dashLegacyEnd(void);

enum SetType {
	ST_CHOICE = 0,
	ST_ACTION,
	ST_INFO
};

enum SetId {
	/* display */
	S_TVMODE = 0,
	S_SCREEN_CLASSIC,
	S_EFFECTS,
	S_ANIM,
	S_LEGACY_THEME,
	/* dashboard */
	S_ENABLED,
	S_START,
	S_VIEW,
	S_ARTWORK,
	S_RECENT,
	S_AUTOSCAN,
	S_SCANPATHS,
	S_RESCAN,
	S_CLEAR_RECENT,
	S_INTRO,
	/* controls */
	S_CONFIRM,
	S_VKEY,
	S_USBKBD,
	S_LAUNCHKEYS,
	/* devices */
	S_HIDE_HDD,
	S_HIDE_MC,
	S_REBOOT_ELF,
	/* network */
	S_NET_INFO,
	S_NET_CLASSIC,
#ifdef ETH
	S_HOSTWRITE,
#endif
	/* advanced */
	S_LANGUAGE,
	S_GAMEID,
	S_GAMEID_DISC,
	S_PSU_HUGE,
	S_PSU_DATE,
	S_PSU_NOOVER,
	S_FB_ICONS,
	S_SHOW_TITLES,
	S_HIDE_PATHS,
	S_STARTUP_CLASSIC,
	S_ADVANCED_CLASSIC,
	S_CONFIG_CLASSIC,
	S_SAVE_CLASSIC
};

typedef struct
{
	int type;
	int id;
	const char *label;
	const char *help;
} SetItem;

typedef struct
{
	const char *name;
	int icon;
	const SetItem *items;
	int n;
} SetCat;

static const SetItem display_items[] = {
    {ST_CHOICE, S_TVMODE, "Video mode", "Auto follows the console region. Changing it applies immediately."},
    {ST_ACTION, S_SCREEN_CLASSIC, "Screen position and colors...", "Classic screen settings: overscan offsets, interlace and colour skins."},
    {ST_CHOICE, S_EFFECTS, "Background effects", "Crystal towers, stars and light effects behind the dashboard."},
    {ST_CHOICE, S_ANIM, "Animations", "Reduce or turn off motion."},
    {ST_CHOICE, S_LEGACY_THEME, "Modern style for classic screens", "Draw the File Browser and classic tools with the dashboard look."},
};

static const SetItem dash_items[] = {
    {ST_CHOICE, S_ENABLED, "Start in the dashboard", "Off: boot into the classic launch-key menu instead."},
    {ST_CHOICE, S_START, "Start screen", "Section shown when the dashboard opens."},
    {ST_CHOICE, S_VIEW, "Application view", "Grid, list or carousel (SELECT also switches it in APPS)."},
    {ST_CHOICE, S_ARTWORK, "Artwork", "Load cover art (OPL ART folder, <ELF>_COV.png, cover.png...)."},
    {ST_CHOICE, S_RECENT, "Recent applications", "How many recently started applications HOME shows."},
    {ST_CHOICE, S_AUTOSCAN, "Refresh at startup", "Check memory cards and already running devices for applications at startup."},
    {ST_ACTION, S_SCANPATHS, "Application folders...", "Folders searched for .ELF files (one sub-folder level deep)."},
    {ST_ACTION, S_RESCAN, "Refresh library now", "Search every application folder, loading drivers as needed."},
    {ST_ACTION, S_CLEAR_RECENT, "Clear recent applications", NULL},
    {ST_ACTION, S_INTRO, "Show the welcome message", NULL},
};

static const SetItem control_items[] = {
    {ST_CHOICE, S_CONFIRM, "Confirm button", "Which button confirms. The other one cancels (all screens)."},
    {ST_CHOICE, S_VKEY, "On-screen keyboard layout", NULL},
    {ST_CHOICE, S_USBKBD, "USB keyboard", "Use a USB keyboard as a controller and for typing."},
    {ST_ACTION, S_LAUNCHKEYS, "Launch keys...", "Classic per-button launch paths (also shown on HOME)."},
};

static const SetItem device_items[] = {
    {ST_CHOICE, S_HIDE_HDD, "Internal HDD / ATA units", "Which HDD and exFAT units appear in the File Browser."},
    {ST_CHOICE, S_HIDE_MC, "Hide empty memory card slots", "Also hides unused MMCE entries in the File Browser."},
    {ST_CHOICE, S_REBOOT_ELF, "Reboot IOP when launching ELFs", "Recommended: gives launched programs a clean IOP."},
};

static const SetItem net_items[] = {
    {ST_INFO, S_NET_INFO, "IP configuration", NULL},
    {ST_ACTION, S_NET_CLASSIC, "Network settings...", "IP address, netmask and gateway (IPCONFIG.DAT)."},
#ifdef ETH
    {ST_CHOICE, S_HOSTWRITE, "Allow writing to host:", NULL},
#endif
};

static const SetItem adv_items[] = {
    {ST_CHOICE, S_LANGUAGE, "Language", "Language of the classic screens."},
    {ST_CHOICE, S_GAMEID, "RetroGem game ID", "Show the game ID for OSD/HDMI adapters when launching."},
    {ST_CHOICE, S_GAMEID_DISC, "Game ID for discs", NULL},
    {ST_CHOICE, S_PSU_HUGE, "PSU: add game ID and title to names", NULL},
    {ST_CHOICE, S_PSU_DATE, "PSU: add date to names", NULL},
    {ST_CHOICE, S_PSU_NOOVER, "PSU: never overwrite existing files", NULL},
    {ST_CHOICE, S_FB_ICONS, "File Browser icons", NULL},
    {ST_CHOICE, S_SHOW_TITLES, "Classic menu: show launch titles", NULL},
    {ST_CHOICE, S_HIDE_PATHS, "Classic menu: hide full paths", NULL},
    {ST_ACTION, S_STARTUP_CLASSIC, "Startup settings...", "USB drivers, delays, timeout, CNF path, fonts, POPStarter ELF..."},
    {ST_ACTION, S_ADVANCED_CLASSIC, "Advanced settings...", "Classic advanced settings screen."},
    {ST_ACTION, S_CONFIG_CLASSIC, "Complete classic configuration...", "Every wLaunchELF option, including skins and launch keys."},
    {ST_ACTION, S_SAVE_CLASSIC, "Save wLaunchELF settings", "Write LAUNCHELF.CNF (you choose where)."},
};

#define NITEMS(a) ((int)(sizeof(a) / sizeof(a[0])))
static const SetCat cats[] = {
    {"Display", DI_SETTINGS, display_items, NITEMS(display_items)},
    {"Dashboard", DI_HOME, dash_items, NITEMS(dash_items)},
    {"Controls", DI_GAMEPAD, control_items, NITEMS(control_items)},
    {"Devices", DI_DEVICES, device_items, NITEMS(device_items)},
    {"Network", DI_NETWORK, net_items, NITEMS(net_items)},
    {"Advanced", DI_TOOLS, adv_items, NITEMS(adv_items)},
};
#define NCATS NITEMS(cats)

static int cat, item, focus_col; /* focus_col 0: categories, 1: items */
static float cat_anim, item_anim, col_anim;
static int classic_dirty;

static const char *const onoff[] = {"Off", "On"};
static const char *const tv_names[] = {"Auto", "NTSC", "PAL", "VGA", "480p"};
static const char *const fx_names[] = {"Off", "Subtle", "Full"};
static const char *const anim_names[] = {"Off", "Reduced", "Full"};
static const char *const view_names[] = {"Grid", "List", "Carousel"};
static const char *const recent_names[] = {"0", "4", "8", "12", "16"};
static const char *const hide_names[] = {"Show all", "Hide HDD1", "Hide HDD0+1", "Hide ATA1", "Hide ATA0+1", "Hide HDD1+ATA1", "Hide all"};
static const char *const confirm_names[] = {"Circle", "Cross"};

/* Returns number of options and the current index for a CHOICE item. */
static int choiceInfo(int id, int *cur, const char *const **names, const char **single)
{
	*names = onoff;
	*single = NULL;
	switch (id) {
		case S_TVMODE:
			*names = tv_names, *cur = setting->TV_mode;
			return TV_mode_COUNT;
		case S_EFFECTS:
			*names = fx_names, *cur = dashCfg.effects;
			return DASH_FX_COUNT;
		case S_ANIM:
			*names = anim_names, *cur = dashCfg.anim;
			return DASH_ANIM_COUNT;
		case S_LEGACY_THEME:
			*cur = dashCfg.legacy_theme;
			return 2;
		case S_ENABLED:
			*cur = dashCfg.enabled;
			return 2;
		case S_START: {
			static const char *secs[DASH_SEC_COUNT];
			int i;
			for (i = 0; i < DASH_SEC_COUNT; i++)
				secs[i] = dashSectionName(i);
			*names = secs, *cur = dashCfg.start_section;
			return DASH_SEC_COUNT;
		}
		case S_VIEW:
			*names = view_names, *cur = dashCfg.view;
			return DASH_VIEW_COUNT;
		case S_ARTWORK:
			*cur = dashCfg.artwork;
			return 2;
		case S_RECENT:
			*names = recent_names, *cur = dashCfg.recent_count / 4;
			return 5;
		case S_AUTOSCAN:
			*cur = dashCfg.autoscan;
			return 2;
		case S_CONFIRM:
			*names = confirm_names, *cur = setting->swapKeys ? 1 : 0;
			return 2;
		case S_VKEY: {
			static const char *layouts[VKEY_LAYOUT_COUNT];
			int i;
			for (i = 0; i < VKEY_LAYOUT_COUNT; i++)
				layouts[i] = getVirtualKeyboardLayoutDisplayName(i);
			*names = layouts, *cur = normalizeVirtualKeyboardLayout(setting->virtual_keyboard_layout);
			return VKEY_LAYOUT_COUNT;
		}
		case S_USBKBD:
			*cur = setting->usbkbd_used ? 1 : 0;
			return 2;
		case S_HIDE_HDD:
			*names = hide_names, *cur = setting->Hide_Hdd;
			return HIDE_HDD_COUNT;
		case S_HIDE_MC:
			*cur = setting->Hide_MCMMCE ? 1 : 0;
			return 2;
		case S_REBOOT_ELF:
			*cur = setting->reboot_iop_elf_load ? 1 : 0;
			return 2;
#ifdef ETH
		case S_HOSTWRITE:
			*cur = setting->HOSTwrite ? 1 : 0;
			return 2;
#endif
		case S_LANGUAGE: {
			static const char *langs[BUILTIN_LANGUAGE_COUNT];
			int i;
			for (i = 0; i < BUILTIN_LANGUAGE_COUNT; i++)
				langs[i] = getBuiltinLanguageNativeName(i);
			*names = langs, *cur = normalizeBuiltinLanguage(setting->language);
			return BUILTIN_LANGUAGE_COUNT;
		}
		case S_GAMEID:
			*cur = setting->app_gameid ? 1 : 0;
			return 2;
		case S_GAMEID_DISC:
			*cur = setting->cdrom_disable_gameid ? 0 : 1;
			return 2;
		case S_PSU_HUGE:
			*cur = setting->PSU_HugeNames ? 1 : 0;
			return 2;
		case S_PSU_DATE:
			*cur = setting->PSU_DateNames ? 1 : 0;
			return 2;
		case S_PSU_NOOVER:
			*cur = setting->PSU_NoOverwrite ? 1 : 0;
			return 2;
		case S_FB_ICONS:
			*cur = setting->FB_NoIcons ? 0 : 1;
			return 2;
		case S_SHOW_TITLES:
			*cur = setting->Show_Titles ? 1 : 0;
			return 2;
		case S_HIDE_PATHS:
			*cur = setting->Hide_Paths ? 1 : 0;
			return 2;
	}
	*cur = 0;
	return 0;
}

static void setChoice(int id, int v)
{
	int dash_changed = 1;

	switch (id) {
		case S_TVMODE:
			setting->TV_mode = v;
			updateScreenMode();
			classic_dirty = 1, dash_changed = 0;
			break;
		case S_EFFECTS:
			dashCfg.effects = v;
			break;
		case S_ANIM:
			dashCfg.anim = v;
			break;
		case S_LEGACY_THEME:
			dashCfg.legacy_theme = v;
			DashThemeSetLegacyEnabled(v);
			break;
		case S_ENABLED:
			dashCfg.enabled = v;
			break;
		case S_START:
			dashCfg.start_section = v;
			break;
		case S_VIEW:
			dashCfg.view = v;
			break;
		case S_ARTWORK:
			dashCfg.artwork = v;
			if (!v)
				dashArtFlushAll();
			break;
		case S_RECENT:
			dashCfg.recent_count = v * 4;
			dashHomeRebuild();
			break;
		case S_AUTOSCAN:
			dashCfg.autoscan = v;
			break;
		default:
			dash_changed = 0;
			classic_dirty = 1;
			switch (id) {
				case S_CONFIRM:
					setting->swapKeys = v;
					swapKeys = v;
					break;
				case S_VKEY:
					setting->virtual_keyboard_layout = v;
					break;
				case S_USBKBD:
					setting->usbkbd_used = v;
					break;
				case S_HIDE_HDD:
					setting->Hide_Hdd = v;
					break;
				case S_HIDE_MC:
					setting->Hide_MCMMCE = v;
					break;
				case S_REBOOT_ELF:
					setting->reboot_iop_elf_load = v;
					break;
#ifdef ETH
				case S_HOSTWRITE:
					setting->HOSTwrite = v;
					break;
#endif
				case S_LANGUAGE:
					Set_Language(v);
					Load_External_Language();
					break;
				case S_GAMEID:
					setting->app_gameid = v;
					break;
				case S_GAMEID_DISC:
					setting->cdrom_disable_gameid = !v;
					break;
				case S_PSU_HUGE:
					setting->PSU_HugeNames = v;
					break;
				case S_PSU_DATE:
					setting->PSU_DateNames = v;
					break;
				case S_PSU_NOOVER:
					setting->PSU_NoOverwrite = v;
					break;
				case S_FB_ICONS:
					setting->FB_NoIcons = !v;
					break;
				case S_SHOW_TITLES:
					setting->Show_Titles = v;
					break;
				case S_HIDE_PATHS:
					setting->Hide_Paths = v;
					break;
			}
			break;
	}
	if (dash_changed)
		dashStoreSaveSettings();
}

//--------------------------------------------------------------
// Actions
//--------------------------------------------------------------
static void saveClassic(void)
{
	char o[MAX_PATH], c[MAX_PATH], s[MAX_PATH];
	char *path;
	int has, t;
	const MainExecuteContext *ctx = dashExecContext();

	configBuildSaveTargets(o, sizeof(o), c, sizeof(c), s, sizeof(s), ctx->cnf_path, LoadedConfigPath, &has);
	dashLegacyBegin();
	t = configSaveTargetPrompt(o, c, s, LoadedConfigPath, has);
	if (t != CONFIG_SAVE_TARGET_CANCEL) {
		path = (t == CONFIG_SAVE_TARGET_OVERRIDE) ? o : (t == CONFIG_SAVE_TARGET_CWD) ? c : s;
		configRefreshSaveTargetForWrite(t, path, MAX_PATH, ctx->cnf_path, LoadedConfigPath);
		saveConfigToPath(ctx->main_msg, ctx->cnf_path, path);
		classic_dirty = 0;
	}
	dashLegacyEnd();
	if (ctx->main_msg[0]) {
		dashToast(strstr(ctx->main_msg, LNG(Saved_Config)) ? DASH_TOAST_OK : DASH_TOAST_ERROR, ctx->main_msg);
		ctx->main_msg[0] = '\0';
	}
}

static void runClassicScreen(void (*fn)(void))
{
	dashLegacyBegin();
	fn();
	dashLegacyEnd();
	classic_dirty = 1;
}

static void editScanPaths(void)
{
	while (1) {
		DashMenuItem items[DASH_MAX_SCAN + 2];
		int n = 0, r, i;

		for (i = 0; i < dashCfg.nscan; i++) {
			items[n].icon = dashLibDeviceIcon(dashCfg.scan[i]);
			items[n].label = dashCfg.scan[i];
			items[n].detail = NULL;
			items[n].disabled = 0;
			n++;
		}
		items[n].icon = DI_PLUS, items[n].label = "Add folder...", items[n].detail = NULL, items[n].disabled = (dashCfg.nscan >= DASH_MAX_SCAN), n++;
		items[n].icon = DI_REFRESH, items[n].label = "Restore defaults", items[n].detail = NULL, items[n].disabled = 0, n++;
		r = dashModalMenu("Application folders", "Select a folder to remove it", items, n, 0);
		if (r < 0)
			break;
		if (r < dashCfg.nscan) {
			char body[DASH_PATH_MAX + 48];
			snprintf(body, sizeof(body), "Stop looking for applications in %s?", dashCfg.scan[r]);
			if (dashModalConfirm("Remove folder", body, "Remove", 0)) {
				for (i = r; i < dashCfg.nscan - 1; i++)
					memcpy(dashCfg.scan[i], dashCfg.scan[i + 1], DASH_PATH_MAX);
				dashCfg.nscan--;
				dashStoreSaveSettings();
			}
		} else if (r == dashCfg.nscan) {
			char out[MAX_PATH];
			out[0] = '\0';
			dashModalMessage(DI_FOLDER, "Choose a folder",
			                 "Browse to the folder that contains your applications, then press START to choose it.", NULL);
			dashLegacyBegin();
			getFilePath(out, DIR_CNF);
			dashLegacyEnd();
			if (out[0] && strlen(out) < DASH_PATH_MAX - 2) {
				int len = strlen(out);
				if (out[len - 1] != '/')
					strcat(out, "/");
				dashStrCopy(dashCfg.scan[dashCfg.nscan++], out, DASH_PATH_MAX);
				dashStoreSaveSettings();
				if (dashModalConfirm("Folder added", "Search this folder for applications now?", "Search", 0))
					dashRescanLibrary(1);
			}
		} else {
			int keep_enabled = dashCfg.enabled, keep_view = dashCfg.view;
			DashSettings saved = dashCfg;
			dashStoreDefaults();
			saved.nscan = dashCfg.nscan;
			memcpy(saved.scan, dashCfg.scan, sizeof(saved.scan));
			dashCfg = saved;
			dashCfg.enabled = keep_enabled;
			dashCfg.view = keep_view;
			dashStoreSaveSettings();
		}
	}
}

static void runAction(int id)
{
	switch (id) {
		case S_SCREEN_CLASSIC:
			runClassicScreen(Config_Screen);
			break;
		case S_SCANPATHS:
			editScanPaths();
			break;
		case S_RESCAN:
			dashRescanLibrary(1);
			break;
		case S_CLEAR_RECENT:
			if (dashModalConfirm("Clear recent applications", "Remove every entry from the recent list? Applications are not affected.", "Clear", 0)) {
				dashStoreClearRecent();
				dashHomeRebuild();
				dashToast(DASH_TOAST_OK, "Recent list cleared");
			}
			break;
		case S_INTRO:
			dashCfg.intro_done = 0;
			dashStoreSaveSettings();
			dashToast(DASH_TOAST_INFO, "The welcome message will be shown next time");
			break;
		case S_LAUNCHKEYS:
		case S_CONFIG_CLASSIC:
			dashRunMisc(setting->Misc_Configure);
			classic_dirty = 0; /* the classic configuration saves or cancels itself */
			dashHomeRebuild();
			break;
		case S_NET_CLASSIC:
			runClassicScreen(Config_Network);
			break;
		case S_STARTUP_CLASSIC:
			runClassicScreen(Config_Startup);
			break;
		case S_ADVANCED_CLASSIC:
			runClassicScreen(Config_Advanced);
			break;
		case S_SAVE_CLASSIC:
			saveClassic();
			break;
	}
}

//--------------------------------------------------------------
static const SetItem *curItem(void)
{
	return &cats[cat].items[item];
}

static void settingsEnter(void)
{
	if (item >= cats[cat].n)
		item = 0;
}

static void settingsInput(const DashInput *in)
{
	const SetItem *it;

	if (focus_col == 0) {
		if ((in->pressed & DASH_IN_UP) && cat > 0)
			cat--, item = 0;
		if ((in->pressed & DASH_IN_DOWN) && cat < NCATS - 1)
			cat++, item = 0;
		if (in->pressed & (DASH_IN_RIGHT | DASH_IN_OK))
			focus_col = 1;
		return;
	}
	it = curItem();
	if (in->pressed & DASH_IN_UP) {
		if (item > 0)
			item--;
	} else if (in->pressed & DASH_IN_DOWN) {
		if (item < cats[cat].n - 1)
			item++;
	} else if (in->pressed & DASH_IN_BACK) {
		focus_col = 0;
	} else if (it->type == ST_CHOICE && (in->pressed & (DASH_IN_LEFT | DASH_IN_RIGHT | DASH_IN_OK))) {
		const char *const *names;
		const char *single;
		int cur, n = choiceInfo(it->id, &cur, &names, &single);
		if (n > 0) {
			if (in->pressed & DASH_IN_LEFT)
				cur = (cur + n - 1) % n;
			else if (in->pressed & DASH_IN_RIGHT)
				cur = (cur + 1) % n;
			else if (n > 2) {
				int r = dashModalChoice(it->label, names, n, cur);
				if (r < 0)
					return;
				cur = r;
			} else
				cur = (cur + 1) % n;
			setChoice(it->id, cur);
		}
	} else if (in->pressed & DASH_IN_LEFT) {
		focus_col = 0;
	} else if (it->type == ST_ACTION && (in->pressed & DASH_IN_OK)) {
		runAction(it->id);
	}
}

static void settingsUpdate(float dt)
{
	float sp = dashAnimSpeed();

	cat_anim = dashApproach(cat_anim, (float)cat, 16.0f * sp, dt);
	item_anim = dashApproach(item_anim, (float)item, 16.0f * sp, dt);
	col_anim = dashApproach(col_anim, (float)focus_col, 12.0f * sp, dt);
}

static void settingsDraw(void)
{
	int i;
	const float cx = 48, cy = 104, crow = 36, cw = 150;
	const float ix = 216, iy = 104, irow = 30, iw = 384;
	const SetCat *c = &cats[cat];

	dashDrawSectionTitle(48, 68, "Settings", classic_dirty ? "unsaved wLaunchELF changes" : NULL);
	if (classic_dirty)
		dashGlow(48 + dashTextWidth("Settings", DASH_FS_TITLE) + 8, 82, 4, 4, DASH_C_WARM);

	/* categories */
	dashDrawPanelBox(cx - 6, cy - 6, cw + 12, NCATS * crow + 12, 14);
	dashDrawSelection(cx, cy + cat_anim * crow, cw, crow - 4, 10, focus_col == 0 ? 1.0f : 0.45f);
	for (i = 0; i < NCATS; i++) {
		DashColor col = (i == cat) ? (focus_col == 0 ? DASH_C_SELTEXT : DASH_C_TEXT) : DASH_C_TEXT2;
		dashIcon(cats[i].icon, cx + 10, cy + i * crow + 8, 16, col);
		dashText(cx + 34, cy + i * crow + 9.5f, cats[i].name, DASH_FS_SMALL, col);
	}

	/* items */
	dashDrawPanelBox(ix - 6, iy - 6, iw + 12, 282, 14);
	if (focus_col == 1)
		dashDrawSelection(ix, iy + item_anim * irow, iw, irow - 3, 9, 1.0f);
	for (i = 0; i < c->n; i++) {
		const SetItem *it = &c->items[i];
		float y = iy + i * irow;
		int on = (focus_col == 1 && i == item);
		DashColor col = dashItemColor(on);

		if (it->type == ST_INFO) {
			char buf[64];
			snprintf(buf, sizeof(buf), "%s / %s", ip, netmask);
			dashText(ix + 12, y + 6.5f, it->label, DASH_FS_SMALL, DASH_C_TEXT3);
			dashTextRight(ix + iw - 12, y + 6.5f, buf, DASH_FS_SMALL, DASH_C_TEXT2);
			continue;
		}
		dashTextFit(ix + 12, y + 6.5f, iw * 0.58f, it->label, DASH_FS_SMALL, col);
		if (it->type == ST_CHOICE) {
			const char *const *names;
			const char *single;
			int cur, n = choiceInfo(it->id, &cur, &names, &single);
			if (n > 0 && cur >= 0 && cur < n) {
				float vw = dashTextWidth(names[cur], DASH_FS_SMALL);
				float vx = ix + iw - 20 - vw;
				if (on) {
					dashIcon(DI_BACK, vx - 22, y + 7, 12, DASH_C_ACCENT);
					dashIcon(DI_PLAY, ix + iw - 16, y + 8, 10, DASH_C_ACCENT);
				}
				dashText(vx, y + 6.5f, names[cur], DASH_FS_SMALL, (n == 2 && names == onoff && cur == 1) ? DASH_C_OK : DASH_C_ACCENT);
			}
		} else
			dashIcon(DI_PLAY, ix + iw - 22, y + 8, 12, DASH_C_TEXT3);
	}
	/* help line */
	{
		const SetItem *it = curItem();
		if (focus_col == 1 && it->help)
			dashTextWrap(ix, iy + 284, iw, 2, it->help, DASH_FS_CAPTION, DASH_C_TEXT3, 18);
	}
}

static void settingsHints(void)
{
	DashHint h[4];
	int n = 0;

	if (focus_col == 0) {
		h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = "Select", n++;
	} else {
		const SetItem *it = curItem();
		h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = (it->type == ST_ACTION) ? "Open" : "Change", n++;
		h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = "Categories", n++;
	}
	h[n].button = -1, h[n].key = "START", h[n].label = "Menu", n++;
	dashDrawHints(h, n);
}

const DashScreen dashSettingsScreen = {settingsEnter, settingsInput, settingsUpdate, settingsDraw, settingsHints};
//--------------------------------------------------------------
//End of file: dash_settings.c
//--------------------------------------------------------------
