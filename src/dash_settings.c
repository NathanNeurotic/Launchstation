//--------------------------------------------------------------
//File name:   dash_settings.c
//SETTINGS: dashboard options plus the existing wLaunchELF settings,
//grouped into Display / Dashboard / Controls / Devices / Network /
//Advanced. Classic settings edit the same SETTING fields as the classic
//configuration screen and are written to LAUNCHELF.CNF only when the user
//saves (with the classic save-location prompt). Every classic sub-screen
//remains one press away, so no option is lost.
//
//Theme: preset choice with live preview, and a theme editor that swaps
//the category column for the theme groups (Background, Colours, Style,
//Text, Icons, Clock, Wallpaper, Manage). Every change applies at once;
//theme edits are written to DASHBOARD.CNF after a short idle delay.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "config_private.h"
#include "dash_sound.h"

void dashLegacyBegin(void);
void dashLegacyEnd(void);

enum SetType {
	ST_CHOICE = 0,
	ST_ACTION,
	ST_INFO,
	ST_COLOR
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
	S_SOUND,
	S_SVOL,
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
	S_SAVE_CLASSIC,
	/* theme */
	S_T_PRESET,
	S_T_CUSTOMIZE,
	S_T_LOAD,
	S_T_SAVE,
	S_T_DEFAULT,
	S_T_BG,
	S_T_BG1,
	S_T_BG2,
	S_T_BRIGHT,
	S_T_SCAN,
	S_T_IMAGE,
	S_T_TEXT,
	S_T_TEXT2,
	S_T_TEXT3,
	S_T_SELTEXT,
	S_T_ACCENT,
	S_T_SELECT,
	S_T_PANEL,
	S_T_BORDER,
	S_T_WARN,
	S_T_HL,
	S_T_CORNER,
	S_T_GLOW,
	S_T_PANEL_A,
	S_T_FONT,
	S_T_FSCALE,
	S_T_BOLD,
	S_T_SHADOW,
	S_T_ICONTINT,
	S_T_APPICON,
	S_T_APPICON_IMG,
	S_T_CLOCK,
	S_T_CLOCKPOS,
	S_T_HOURS,
	S_T_SECONDS,
	S_T_DATE,
	S_T_WALL,
	S_T_WALLDIM,
	S_T_RENAME,
	S_T_RESTORE_PRESET,
	S_T_RESETCAT
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
    {ST_CHOICE, S_SOUND, "Interface sounds", "Cursor moves, confirmations and other dashboard sound effects."},
    {ST_CHOICE, S_SVOL, "Sound volume", NULL},
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

static const SetItem theme_items[] = {
    {ST_CHOICE, S_T_PRESET, "Theme", NULL},
    {ST_ACTION, S_T_CUSTOMIZE, "Customize theme...", "Background, colours, style, text, icons, clock and wallpaper. Changes apply immediately."},
    {ST_CHOICE, S_EFFECTS, "Background motion", "Off shows a still frame of the same background."},
    {ST_ACTION, S_T_LOAD, "Load theme from folder...", "Themes are folders in THEMES/ next to DASHBOARD.CNF, each with a theme.cfg."},
    {ST_ACTION, S_T_SAVE, "Save theme to folder...", "Writes THEMES/<name>/theme.cfg so it can be edited on a PC or shared."},
    {ST_ACTION, S_T_DEFAULT, "Restore default theme", "OSD Classic with its original settings."},
};

#define NITEMS(a) ((int)(sizeof(a) / sizeof(a[0])))
static const SetCat main_cats[] = {
    {"Display", DI_SETTINGS, display_items, NITEMS(display_items)},
    {"Theme", DI_PALETTE, theme_items, NITEMS(theme_items)},
    {"Dashboard", DI_HOME, dash_items, NITEMS(dash_items)},
    {"Controls", DI_GAMEPAD, control_items, NITEMS(control_items)},
    {"Devices", DI_DEVICES, device_items, NITEMS(device_items)},
    {"Network", DI_NETWORK, net_items, NITEMS(net_items)},
    {"Advanced", DI_TOOLS, adv_items, NITEMS(adv_items)},
};
#define MAIN_THEME_CAT 1

/* Theme editor groups (order = enum DashThemeGroup, then Manage). */
static const SetItem tbg_items[] = {
    {ST_CHOICE, S_T_BG, "Background", "PS2-inspired scenes, plain colours or your own wallpaper image."},
    {ST_COLOR, S_T_BG1, "Colour", "Main colour of the background (the solid colour, gradient top or scene tint)."},
    {ST_COLOR, S_T_BG2, "Second colour", "Base or gradient bottom colour."},
    {ST_CHOICE, S_T_BRIGHT, "Brightness", "Darken the background behind the menus."},
    {ST_CHOICE, S_EFFECTS, "Motion", "Off shows a still frame. Subtle moves slower with fewer elements."},
    {ST_CHOICE, S_ANIM, "Animations", "Reduce or turn off motion in the menus."},
    {ST_CHOICE, S_T_SCAN, "CRT scanlines", "An understated line pattern over the background only."},
    {ST_ACTION, S_T_IMAGE, "Wallpaper image...", "Choose a PNG (up to 1.5 MB). It is scaled to at most 320x256 and cover-fitted."},
    {ST_ACTION, S_T_RESETCAT, "Reset background", "Back to the values of the theme's preset."},
};
static const SetItem tcol_items[] = {
    {ST_COLOR, S_T_TEXT, "Text", "Titles and focused text. Colours too close to the background are corrected."},
    {ST_COLOR, S_T_TEXT2, "Secondary text", "Unselected rows and details."},
    {ST_COLOR, S_T_TEXT3, "Muted text", "Captions and hints."},
    {ST_COLOR, S_T_SELTEXT, "Selected text", "Text of the highlighted row."},
    {ST_COLOR, S_T_ACCENT, "Accent", "Focus marks, values and icons that need attention."},
    {ST_COLOR, S_T_SELECT, "Selection", "Selection bar colour and opacity."},
    {ST_COLOR, S_T_PANEL, "Panels", "Panel colour and opacity (0 = no panels)."},
    {ST_COLOR, S_T_BORDER, "Borders", "Panel outline colour and opacity (0 = no borders)."},
    {ST_COLOR, S_T_WARN, "Warnings", "Favourites, warnings and unsaved-change marks."},
    {ST_ACTION, S_T_RESETCAT, "Reset colours", "Back to the values of the theme's preset."},
};
static const SetItem tstyle_items[] = {
    {ST_CHOICE, S_T_HL, "Selection style", "Text colour is how the PS2 system menus mark the selection."},
    {ST_CHOICE, S_T_CORNER, "Corners", NULL},
    {ST_CHOICE, S_T_GLOW, "Glow", "Soft light behind focused items."},
    {ST_CHOICE, S_T_PANEL_A, "Panel opacity", NULL},
    {ST_ACTION, S_T_RESETCAT, "Reset style", "Back to the values of the theme's preset."},
};
static const SetItem ttype_items[] = {
    {ST_CHOICE, S_T_FONT, "Font", "System uses the wLaunchELF font (or your .fnt) and its accented characters."},
    {ST_CHOICE, S_T_FSCALE, "Text size", "Larger sizes can shorten long names."},
    {ST_CHOICE, S_T_BOLD, "Bold", "Heavier strokes; easier to read on a CRT."},
    {ST_CHOICE, S_T_SHADOW, "Text shadow", NULL},
    {ST_ACTION, S_T_RESETCAT, "Reset text", "Back to the values of the theme's preset."},
};
static const SetItem ticon_items[] = {
    {ST_CHOICE, S_T_ICONTINT, "Icon colour", NULL},
    {ST_CHOICE, S_T_APPICON, "Default app icon", "Shown for applications without artwork."},
    {ST_ACTION, S_T_APPICON_IMG, "Custom app icon image...", "A PNG used as the default application icon (up to 128x128 is kept)."},
    {ST_ACTION, S_T_RESETCAT, "Reset icons", "Back to the values of the theme's preset."},
};
static const SetItem tclock_items[] = {
    {ST_CHOICE, S_T_CLOCK, "Clock", "System shows the date and time the way the PS2 system menus do."},
    {ST_CHOICE, S_T_CLOCKPOS, "Position", "Footer: left of the button hints (hidden when they need the space)."},
    {ST_CHOICE, S_T_HOURS, "Hours", "System follows the console's own clock setting."},
    {ST_CHOICE, S_T_SECONDS, "Seconds", NULL},
    {ST_CHOICE, S_T_DATE, "Date", "System follows the console's own date format."},
    {ST_ACTION, S_T_RESETCAT, "Reset clock", "Back to the values of the theme's preset."},
};
static const SetItem twall_items[] = {
    {ST_CHOICE, S_T_WALL, "App artwork background", "Shows the focused application's artwork behind HOME and APPS."},
    {ST_CHOICE, S_T_WALLDIM, "Artwork dimming", "Higher values keep the menus easier to read."},
    {ST_CHOICE, S_ARTWORK, "Load artwork", "Cover art for applications (needed by the artwork background)."},
    {ST_ACTION, S_T_RESETCAT, "Reset wallpaper", "Back to the values of the theme's preset."},
};
static const SetItem tmanage_items[] = {
    {ST_ACTION, S_T_RENAME, "Theme name...", NULL},
    {ST_ACTION, S_T_SAVE, "Save theme to folder...", "Writes THEMES/<name>/theme.cfg so it can be edited on a PC or shared."},
    {ST_ACTION, S_T_LOAD, "Load theme from folder...", "Themes are folders in THEMES/ next to DASHBOARD.CNF, each with a theme.cfg."},
    {ST_ACTION, S_T_RESTORE_PRESET, "Reset entire theme", "Undo every change and go back to the preset this theme started from."},
    {ST_ACTION, S_T_DEFAULT, "Restore default theme", "OSD Classic with its original settings."},
};

static const SetCat theme_cats[] = {
    {"Background", DI_IMAGE, tbg_items, NITEMS(tbg_items)},
    {"Colours", DI_PALETTE, tcol_items, NITEMS(tcol_items)},
    {"Style", DI_VIEW_GRID, tstyle_items, NITEMS(tstyle_items)},
    {"Text", DI_TEXT, ttype_items, NITEMS(ttype_items)},
    {"Icons", DI_APPS, ticon_items, NITEMS(ticon_items)},
    {"Clock", DI_CLOCK, tclock_items, NITEMS(tclock_items)},
    {"Wallpaper", DI_IMAGE, twall_items, NITEMS(twall_items)},
    {"Manage", DI_SETTINGS, tmanage_items, NITEMS(tmanage_items)},
};

static const SetCat *cats = main_cats;
static int ncats = NITEMS(main_cats);
static int theme_mode;
#define NCATS ncats
#define ITEM_ROWS 8

static int cat, item, focus_col; /* focus_col 0: categories, 1: items */
static int item_top;
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
static const char *const hl_names[] = {"Solid bar", "Accent bar", "Outline", "Text colour"};
static const char *const corner_names[] = {"Square", "Soft", "Round"};
static const char *const font_names[] = {"Roboto", "System"};
static const char *const fscale_names[] = {"90%", "95%", "100%", "105%", "110%", "115%"};
static const char *const tint_names[] = {"Text colours", "Accent", "Monochrome"};
static const char *const appicon_names[] = {"Application", "Device", "Initials"};
static const char *const clock_names[] = {"Off", "Standard", "System"};
static const char *const clockpos_names[] = {"Header", "Footer"};
static const char *const hours_names[] = {"System", "24-hour", "12-hour"};
static const char *const date_names[] = {"System", "YYYY/MM/DD", "MM/DD/YYYY", "DD/MM/YYYY", "Off"};
static const char *const wall_names[] = {"Off", "Focused app"};
static const char *const pct_names[] = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};

static void themeChanged(void)
{
	dashThemeCommit();
	dashThemeMarkDirty();
}

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
		case S_SOUND:
			*cur = dashCfg.sound;
			return 2;
		case S_SVOL:
			*names = pct_names, *cur = dashCfg.sound_vol / 10;
			return 11;
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
		case S_T_PRESET: {
			static const char *pnames[32];
			static char custom_label[DASH_THEME_NAME_MAX + 12];
			int i, n = dashThemePresetCount();
			if (n > 32)
				n = 32;
			for (i = 0; i < n; i++)
				pnames[i] = dashThemePresetInfo(i)->name;
			*cur = dashThemeBasePreset();
			if (dashThemeIsCustom()) {
				snprintf(custom_label, sizeof(custom_label), "%s (edited)", dashTheme.name);
				pnames[*cur] = custom_label;
			}
			*names = pnames;
			return n;
		}
		case S_T_BG: {
			static const char *bnames[DASH_BG_COUNT];
			int i;
			for (i = 0; i < DASH_BG_COUNT; i++)
				bnames[i] = dashThemeBgName(i);
			*names = bnames, *cur = dashTheme.bg;
			return DASH_BG_COUNT;
		}
		case S_T_BRIGHT:
			/* 10%..100% */
			*names = pct_names + 1, *cur = (dashTheme.bg_bright + 5) / 10 - 1;
			return 10;
		case S_T_SCAN:
			*cur = dashTheme.scanlines;
			return 2;
		case S_T_HL:
			*names = hl_names, *cur = dashTheme.highlight;
			return DASH_HL_COUNT;
		case S_T_CORNER:
			*names = corner_names, *cur = dashTheme.corner;
			return DASH_CORNER_COUNT;
		case S_T_GLOW:
			*cur = dashTheme.glow;
			return 2;
		case S_T_PANEL_A:
			*names = pct_names, *cur = (dashTheme.panel.a * 10 + 127) / 255;
			return 11;
		case S_T_FONT:
			*names = font_names, *cur = dashTheme.font;
			return DASH_FONT_COUNT;
		case S_T_FSCALE:
			*names = fscale_names, *cur = (dashTheme.font_scale - 90 + 2) / 5;
			return 6;
		case S_T_BOLD:
			*cur = dashTheme.bold;
			return 2;
		case S_T_SHADOW:
			*cur = dashTheme.text_shadow;
			return 2;
		case S_T_ICONTINT:
			*names = tint_names, *cur = dashTheme.icon_tint;
			return DASH_ICONS_COUNT;
		case S_T_APPICON:
			*names = appicon_names, *cur = dashTheme.app_icon_style;
			return DASH_APPICON_COUNT;
		case S_T_CLOCK:
			*names = clock_names, *cur = dashTheme.clock;
			return DASH_CLOCK_COUNT;
		case S_T_CLOCKPOS:
			*names = clockpos_names, *cur = dashTheme.clock_pos;
			return DASH_CLOCKPOS_COUNT;
		case S_T_HOURS:
			*names = hours_names, *cur = dashTheme.clock_hours;
			return DASH_HOURS_COUNT;
		case S_T_SECONDS:
			*cur = dashTheme.clock_seconds;
			return 2;
		case S_T_DATE:
			*names = date_names, *cur = dashTheme.clock_date;
			return DASH_DATE_COUNT;
		case S_T_WALL:
			*names = wall_names, *cur = dashTheme.wallpaper;
			return DASH_WALL_COUNT;
		case S_T_WALLDIM:
			*names = pct_names, *cur = (dashTheme.wall_dim + 5) / 10;
			return 10;
	}
	*cur = 0;
	return 0;
}

/* Theme choices: applied at once, saved after a short idle delay. */
static int setThemeChoice(int id, int v)
{
	DashTheme *t = &dashTheme;

	switch (id) {
		case S_T_PRESET:
			dashThemeSetPreset(v);
			dashThemeMarkDirty();
			return 1;
		case S_T_BG:
			t->bg = (u8)v;
			break;
		case S_T_BRIGHT:
			t->bg_bright = (u8)((v + 1) * 10);
			break;
		case S_T_SCAN:
			t->scanlines = (u8)v;
			break;
		case S_T_HL:
			t->highlight = (u8)v;
			break;
		case S_T_CORNER:
			t->corner = (u8)v;
			break;
		case S_T_GLOW:
			t->glow = (u8)v;
			break;
		case S_T_PANEL_A:
			t->panel.a = (u8)(v * 255 / 10);
			break;
		case S_T_FONT:
			t->font = (u8)v;
			break;
		case S_T_FSCALE:
			t->font_scale = (u8)(90 + v * 5);
			break;
		case S_T_BOLD:
			t->bold = (u8)v;
			break;
		case S_T_SHADOW:
			t->text_shadow = (u8)v;
			break;
		case S_T_ICONTINT:
			t->icon_tint = (u8)v;
			break;
		case S_T_APPICON:
			t->app_icon_style = (u8)v;
			break;
		case S_T_CLOCK:
			t->clock = (u8)v;
			break;
		case S_T_CLOCKPOS:
			t->clock_pos = (u8)v;
			break;
		case S_T_HOURS:
			t->clock_hours = (u8)v;
			break;
		case S_T_SECONDS:
			t->clock_seconds = (u8)v;
			break;
		case S_T_DATE:
			t->clock_date = (u8)v;
			break;
		case S_T_WALL:
			t->wallpaper = (u8)v;
			break;
		case S_T_WALLDIM:
			t->wall_dim = (u8)(v * 10);
			break;
		default:
			return 0;
	}
	themeChanged();
	return 1;
}

//--------------------------------------------------------------
// Colour editor
//--------------------------------------------------------------
static DashColor *colorField(int id, int *alpha)
{
	*alpha = 0;
	switch (id) {
		case S_T_BG1:
			return &dashTheme.bg1;
		case S_T_BG2:
			return &dashTheme.bg2;
		case S_T_TEXT:
			return &dashTheme.text;
		case S_T_TEXT2:
			return &dashTheme.text2;
		case S_T_TEXT3:
			return &dashTheme.text3;
		case S_T_SELTEXT:
			return &dashTheme.sel_text;
		case S_T_ACCENT:
			return &dashTheme.accent;
		case S_T_SELECT:
			*alpha = 1;
			return &dashTheme.select;
		case S_T_PANEL:
			*alpha = 1;
			return &dashTheme.panel;
		case S_T_BORDER:
			*alpha = 1;
			return &dashTheme.border;
		case S_T_WARN:
			return &dashTheme.warn;
	}
	return NULL;
}

/* Quick picks: PS2 system-menu colours plus neutral greys. */
static const DashColor swatches[] = {
    {236, 238, 244, 255}, {156, 162, 178, 255}, {100, 106, 124, 255}, {0, 0, 0, 255},
    {118, 214, 255, 255}, {30, 110, 156, 255}, {0, 70, 160, 255}, {8, 14, 34, 255},
    {46, 36, 78, 255}, {167, 217, 255, 255}, {168, 34, 44, 255}, {255, 204, 92, 255},
    {236, 226, 80, 255}, {92, 220, 150, 255}, {150, 140, 255, 255}, {186, 192, 204, 255}};
#define NSWATCH ((int)(sizeof(swatches) / sizeof(swatches[0])))

static void colorEditor(const char *title, int id)
{
	int alpha, row = 0, sw = 0, nrows, i;
	DashColor *c = colorField(id, &alpha);
	DashColor orig, before;
	float open = 0.0f;
	const float w = 360.0f, x = DASH_SAFE_R - w;
	float h, y;
	DashInput in;
	static const char *const ch_names[4] = {"Red", "Green", "Blue", "Opacity"};

	if (c == NULL)
		return;
	orig = *c;
	nrows = (alpha ? 4 : 3) + 1; /* channels + swatch row */
	h = 94.0f + nrows * 34.0f + 40.0f;
	y = (DASH_VH - h) * 0.5f + 10.0f;
	for (i = 0; i < NSWATCH; i++)
		if (swatches[i].r == c->r && swatches[i].g == c->g && swatches[i].b == c->b)
			sw = i;
	dashInputFlush();

	while (1) {
		int step = 0;
		u8 *ch = NULL;

		dashInputPoll(&in);
		if (in.pressed & DASH_IN_UP)
			row = (row + nrows - 1) % nrows;
		if (in.pressed & DASH_IN_DOWN)
			row = (row + 1) % nrows;
		if (in.pressed & DASH_IN_LEFT)
			step = -1;
		if (in.pressed & DASH_IN_RIGHT)
			step = 1;
		if (in.pressed & (DASH_IN_L1 | DASH_IN_L2))
			step = -16;
		if (in.pressed & (DASH_IN_R1 | DASH_IN_R2))
			step = 16;
		if (step != 0) {
			if (row == nrows - 1) {
				sw = (sw + (step > 0 ? 1 : NSWATCH - 1)) % NSWATCH;
				c->r = swatches[sw].r, c->g = swatches[sw].g, c->b = swatches[sw].b;
			} else {
				int v;
				ch = (row == 0) ? &c->r : (row == 1) ? &c->g : (row == 2) ? &c->b : &c->a;
				v = *ch + step;
				*ch = (u8)((v < 0) ? 0 : (v > 255) ? 255 : v);
			}
			dashThemeApply(); /* live, validated on confirm */
		}
		if (in.pressed & DASH_IN_OK) {
			before = *c;
			dashThemeCommit();
			dashThemeMarkDirty();
			if (before.r != c->r || before.g != c->g || before.b != c->b || before.a != c->a)
				dashToast(DASH_TOAST_INFO, "Adjusted to stay readable");
			dashInputFlush();
			return;
		}
		if (in.pressed & DASH_IN_BACK) {
			*c = orig;
			dashThemeApply();
			dashInputFlush();
			return;
		}
		open = dashApproach(open, 1.0f, 14.0f * dashAnimSpeed(), 1.0f / 60.0f);

		dashRenderScene();
		dashGfxPushAlpha(open);
		{
			DashColor top = dashColorLighten(DASH_C_PANEL, 0.14f), bot = dashColorLighten(DASH_C_PANEL, 0.06f);
			char hex[16];
			float ry = y + 84.0f;

			top.a = bot.a = 245;
			dashShadow(x, y, w, h, 22, 0.9f);
			dashPanelGradV(x, y, w, h, 16, top, bot);
			dashPanelBorder(x, y, w, h, 16, dashColorA(DASH_C_ACCENT, 0.24f));
			dashTextS(x + 20, y + 16, title, DASH_FS_LABEL, DASH_C_TEXT);
			/* preview swatch over a checker so opacity is visible */
			dashRect(x + w - 86, y + 16, 33, 25, DC(255, 255, 255, 255));
			dashRect(x + w - 53, y + 16, 33, 25, DC(0, 0, 0, 255));
			dashRect(x + w - 86, y + 41, 33, 25, DC(0, 0, 0, 255));
			dashRect(x + w - 53, y + 41, 33, 25, DC(255, 255, 255, 255));
			dashRect(x + w - 86, y + 16, 66, 50, alpha ? *c : DC(c->r, c->g, c->b, 255));
			dashPanelBorder(x + w - 87, y + 15, 68, 52, 2, DASH_C_TEXT3);
			if (alpha)
				snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", c->r, c->g, c->b, c->a);
			else
				snprintf(hex, sizeof(hex), "#%02X%02X%02X", c->r, c->g, c->b);
			dashText(x + 20, y + 46, hex, DASH_FS_SMALL, DASH_C_TEXT2);

			for (i = 0; i < nrows; i++, ry += 34.0f) {
				int on = (i == row);
				if (on)
					dashDrawSelection(x + 12, ry - 4, w - 24, 30, 8, 1.0f);
				if (i < nrows - 1) {
					int v = (i == 0) ? c->r : (i == 1) ? c->g : (i == 2) ? c->b : c->a;
					DashColor lo = DC(c->r, c->g, c->b, 255), hi = lo;
					char num[8];
					float bx = x + 100, bw = w - 170;
					if (i == 0)
						lo.r = 0, hi.r = 255;
					else if (i == 1)
						lo.g = 0, hi.g = 255;
					else if (i == 2)
						lo.b = 0, hi.b = 255;
					else
						lo = DC(40, 40, 40, 255), hi = DC(230, 230, 230, 255);
					dashText(x + 24, ry + 2, ch_names[i], DASH_FS_SMALL, dashItemColor(on));
					dashRectGradH(bx, ry + 6, bw, 10, lo, hi);
					dashRect(bx + bw * v / 255.0f - 1.5f, ry + 1, 3, 20, on ? DASH_C_TEXT : DASH_C_TEXT2);
					snprintf(num, sizeof(num), "%d", v);
					dashTextRight(x + w - 24, ry + 2, num, DASH_FS_SMALL, dashItemColor(on));
				} else {
					int k;
					float sx = x + 24, ssz = (w - 48) / NSWATCH;
					for (k = 0; k < NSWATCH; k++) {
						dashRect(sx + k * ssz + 1, ry + 1, ssz - 2, 20, swatches[k]);
						if (on && k == sw)
							dashPanelBorder(sx + k * ssz - 1, ry - 1, ssz + 2, 24, 3, DASH_C_TEXT);
					}
				}
			}
			{
				float hx = x + 20, hy = y + h - 34;
				hx = dashTextS(hx, hy + 4, "Left/Right adjust   L1/R1 +/-16", DASH_FS_CAPTION, DASH_C_TEXT3) + 14;
				dashIcon(dashOkIcon(), hx, hy + 3, 14, DASH_C_TEXT2);
				hx = dashTextS(hx + 18, hy + 4, "Done", DASH_FS_CAPTION, DASH_C_TEXT2) + 10;
				dashIcon(dashBackIcon(), hx, hy + 3, 14, DASH_C_TEXT2);
				dashTextS(hx + 18, hy + 4, "Cancel", DASH_FS_CAPTION, DASH_C_TEXT2);
			}
		}
		dashGfxPopAlpha();
		dashGfxPresent();
	}
}

static void setChoice(int id, int v)
{
	int dash_changed = 1;

	if (setThemeChoice(id, v))
		return;
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
		case S_SOUND:
			dashCfg.sound = v;
			if (v)
				dashSoundInit();
			break;
		case S_SVOL:
			dashCfg.sound_vol = v * 10;
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

//--------------------------------------------------------------
// Theme actions
//--------------------------------------------------------------
static DashTheme preview_saved;
static int preview_saved_preset, preview_saved_custom;

static void presetPreview(int index)
{
	dashThemeSetPreset(index);
}

static void choosePreset(void)
{
	DashMenuItem items[32];
	int n = dashThemePresetCount(), i, r;

	if (n > 32)
		n = 32;
	for (i = 0; i < n; i++) {
		items[i].icon = (i == dashThemeBasePreset() && !dashThemeIsCustom()) ? DI_CHECK : -1;
		items[i].label = dashThemePresetInfo(i)->name;
		items[i].detail = NULL;
		items[i].disabled = 0;
	}
	preview_saved = dashTheme;
	preview_saved_preset = dashThemeBasePreset();
	preview_saved_custom = dashThemeIsCustom();
	dashModalSetPreview(presetPreview);
	r = dashModalMenu("Theme", "Move to preview, confirm to keep", items, n, dashThemeBasePreset());
	if (r < 0) {
		dashThemeRestore(&preview_saved, preview_saved_preset, preview_saved_custom);
		return;
	}
	dashThemeSetPreset(r);
	dashThemeMarkDirty();
}

static int pickPng(char *out, int size, const char *what)
{
	char path[MAX_PATH];
	char body[160];

	snprintf(body, sizeof(body), "Browse to a PNG file for the %s and select it.", what);
	dashModalMessage(DI_IMAGE, "Choose an image", body, NULL);
	path[0] = '\0';
	dashLegacyBegin();
	getFilePath(path, TEXT_CNF);
	dashLegacyEnd();
	if (path[0] == '\0')
		return 0;
	if (!genCmpFileExt(path, "PNG") || strlen(path) >= (size_t)size || strlen(path) >= DASH_PATH_MAX) {
		dashModalMessage(DI_WARNING, "Not a PNG image", "Only PNG images can be used.", path);
		return 0;
	}
	dashStrCopy(out, path, size);
	return 1;
}

static void themeSave(void)
{
	char name[DASH_THEME_NAME_MAX], body[200];
	int i;

	dashStrCopy(name, dashTheme.name, sizeof(name));
	for (i = 0; name[i]; i++) {
		char ch = name[i];
		if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
			name[i] = '_';
	}
	if (name[0] == '\0')
		strcpy(name, "MyTheme");
	snprintf(body, sizeof(body), "Save the current theme as %sTHEMES/%s/theme.cfg? An existing theme with that name is replaced.",
	         dashStoreDir(), name);
	if (!dashModalConfirm("Save theme", body, "Save", 0))
		return;
	if (dashThemeFolderExport(name) == 0)
		dashToast(DASH_TOAST_OK, "Theme saved");
	else
		dashModalMessage(DI_WARNING, "Theme not saved", "The theme folder could not be written. Check that the device is writable.", NULL);
}

static void themeLoad(void)
{
	static char names[24][DASH_THEME_NAME_MAX];
	DashMenuItem items[24];
	int n, i, r;
	char body[200];

	dashLoadingBegin("Looking for themes");
	n = dashThemeFolderList(names, 24);
	dashLoadingEnd();
	if (n <= 0) {
		snprintf(body, sizeof(body), "Put each theme in its own folder with a theme.cfg inside %sTHEMES/. "
		                             "\"Save theme to folder\" creates one you can copy and edit.", dashStoreDir());
		dashModalMessage(DI_FOLDER, "No themes found", body, NULL);
		return;
	}
	for (i = 0; i < n; i++) {
		items[i].icon = DI_PALETTE;
		items[i].label = names[i];
		items[i].detail = NULL;
		items[i].disabled = 0;
	}
	r = dashModalMenu("Load theme", "THEMES folder", items, n, 0);
	if (r < 0)
		return;
	if (dashThemeFolderLoad(names[r]) == 0) {
		dashThemeMarkDirty();
		dashToast(DASH_TOAST_OK, "Theme loaded");
	} else
		dashModalMessage(DI_WARNING, "Theme not loaded", "theme.cfg is missing or could not be read.", names[r]);
}

static void runThemeAction(int id)
{
	switch (id) {
		case S_T_CUSTOMIZE:
			theme_mode = 1;
			cats = theme_cats;
			ncats = NITEMS(theme_cats);
			cat = 0, item = 0, item_top = 0, focus_col = 0;
			cat_anim = item_anim = 0.0f;
			break;
		case S_T_LOAD:
			themeLoad();
			break;
		case S_T_SAVE:
			themeSave();
			break;
		case S_T_DEFAULT:
			if (dashModalConfirm("Restore default theme", "Replace the current theme with OSD Classic? Saved theme folders are not affected.", "Restore", 0)) {
				dashThemeSetPreset(0);
				dashThemeMarkDirty();
				dashToast(DASH_TOAST_OK, "Default theme restored");
			}
			break;
		case S_T_RESTORE_PRESET: {
			char body[160];
			int p = dashThemeBasePreset();
			snprintf(body, sizeof(body), "Undo every change and return to \"%s\"?", dashThemePresetInfo(p)->name);
			if (dashModalConfirm("Reset entire theme", body, "Reset", 0)) {
				dashThemeSetPreset(p);
				dashThemeMarkDirty();
			}
			break;
		}
		case S_T_RESETCAT:
			if (cat >= 0 && cat <= DASH_TG_WALLPAPER) {
				dashThemeResetGroup(cat);
				dashThemeMarkDirty();
				dashToast(DASH_TOAST_OK, "Reset to the preset's values");
			}
			break;
		case S_T_IMAGE:
			if (pickPng(dashTheme.image, sizeof(dashTheme.image), "background")) {
				dashTheme.bg = DASH_BG_IMAGE;
				themeChanged();
			}
			break;
		case S_T_APPICON_IMG:
			if (pickPng(dashTheme.app_icon, sizeof(dashTheme.app_icon), "default application icon")) {
				dashTheme.app_icon_style = DASH_APPICON_SYSTEM;
				themeChanged();
			}
			break;
		case S_T_RENAME: {
			char name[DASH_THEME_NAME_MAX];
			dashStrCopy(name, dashTheme.name, sizeof(name));
			dashLegacyBegin();
			if (keyboard(name, DASH_THEME_NAME_MAX - 1) > 0) {
				dashStrCopy(dashTheme.name, name, sizeof(dashTheme.name));
				themeChanged();
			}
			dashLegacyEnd();
			break;
		}
	}
}

static void runAction(int id)
{
	if (id >= S_T_PRESET) {
		runThemeAction(id);
		return;
	}
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
			cat--, item = 0, item_top = 0;
		if ((in->pressed & DASH_IN_DOWN) && cat < NCATS - 1)
			cat++, item = 0, item_top = 0;
		if (in->pressed & (DASH_IN_RIGHT | DASH_IN_OK))
			focus_col = 1;
		if ((in->pressed & DASH_IN_BACK) && theme_mode) {
			/* leave the theme editor, back to SETTINGS > Theme */
			theme_mode = 0;
			cats = main_cats;
			ncats = NITEMS(main_cats);
			cat = MAIN_THEME_CAT, item = 1, item_top = 0, focus_col = 1;
			cat_anim = (float)cat;
			item_anim = (float)item;
		}
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
	} else if (it->type == ST_COLOR && (in->pressed & DASH_IN_OK)) {
		colorEditor(it->label, it->id);
	} else if (it->id == S_T_PRESET && (in->pressed & DASH_IN_OK)) {
		choosePreset();
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
	const float cx = 48, cy = 104, crow = theme_mode ? 31.0f : 36.0f, cw = 150;
	const float ix = 216, iy = 104, irow = 30, iw = 384;
	const SetCat *c = &cats[cat];

	if (theme_mode) {
		char sub[DASH_THEME_NAME_MAX + 16];
		snprintf(sub, sizeof(sub), "%s%s", dashTheme.name, dashThemeIsCustom() ? " (edited)" : "");
		dashDrawSectionTitle(48, 68, "Theme", sub);
	} else {
		dashDrawSectionTitle(48, 68, "Settings", classic_dirty ? "unsaved wLaunchELF changes" : NULL);
		if (classic_dirty)
			dashRect(48 + dashTextWidth("Settings", DASH_FS_TITLE) + 6, 80, 5, 5, DASH_C_WARM);
	}

	/* keep the focused row inside the visible window */
	if (item < item_top)
		item_top = item;
	if (item >= item_top + ITEM_ROWS)
		item_top = item - ITEM_ROWS + 1;

	/* categories */
	dashDrawPanelBox(cx - 6, cy - 6, cw + 12, NCATS * crow + 12, 14);
	dashDrawSelection(cx, cy + cat_anim * crow, cw, crow - 4, 10, focus_col == 0 ? 1.0f : 0.45f);
	for (i = 0; i < NCATS; i++) {
		DashColor col = (i == cat) ? (focus_col == 0 ? DASH_C_SELTEXT : DASH_C_TEXT) : DASH_C_TEXT2;
		dashIcon(cats[i].icon, cx + 10, cy + i * crow + 8, 16, col);
		dashText(cx + 34, cy + i * crow + 9.5f, cats[i].name, DASH_FS_SMALL, col);
	}

	/* items */
	dashDrawPanelBox(ix - 6, iy - 6, iw + 12, ITEM_ROWS * irow + 12, 14);
	if (focus_col == 1)
		dashDrawSelection(ix, iy + (item_anim - item_top) * irow, iw, irow - 3, 9, 1.0f);
	for (i = item_top; i < c->n && i < item_top + ITEM_ROWS; i++) {
		const SetItem *it = &c->items[i];
		float y = iy + (i - item_top) * irow;
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
		if (it->type == ST_COLOR) {
			int alpha;
			DashColor *cv = colorField(it->id, &alpha);
			if (cv != NULL) {
				char hex[12];
				float sx = ix + iw - 50;
				snprintf(hex, sizeof(hex), "#%02X%02X%02X", cv->r, cv->g, cv->b);
				dashTextRight(sx - 10, y + 6.5f, hex, DASH_FS_SMALL, on ? DASH_C_TEXT2 : DASH_C_TEXT3);
				dashRect(sx, y + 6, 36, 15, DC(cv->r, cv->g, cv->b, alpha ? (cv->a < 40 ? 40 : cv->a) : 255));
				dashPanelBorder(sx - 1, y + 5, 38, 17, 2, dashColorA(DASH_C_TEXT, 0.35f));
			}
		} else if (it->type == ST_CHOICE) {
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
	dashDrawScrollbar(ix + iw + 10, iy, ITEM_ROWS * irow, item_top, ITEM_ROWS, c->n);
	/* help line */
	{
		const SetItem *it = curItem();
		const char *help = it->help;
		if (it->id == S_T_PRESET)
			help = dashThemePresetInfo(dashThemeBasePreset())->desc;
		if (focus_col == 1 && help)
			dashTextWrap(ix, iy + ITEM_ROWS * irow + 12, iw, 2, help, DASH_FS_CAPTION, DASH_C_TEXT3, 17);
	}
}

static void settingsHints(void)
{
	DashHint h[4];
	int n = 0;

	if (focus_col == 0) {
		h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = "Select", n++;
		if (theme_mode)
			h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = "Done", n++;
	} else {
		const SetItem *it = curItem();
		h[n].button = dashOkIcon(), h[n].key = NULL,
		h[n].label = (it->type == ST_ACTION) ? "Open" : (it->type == ST_COLOR) ? "Edit" : "Change", n++;
		h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = theme_mode ? "Groups" : "Categories", n++;
	}
	h[n].button = -1, h[n].key = "START", h[n].label = "Menu", n++;
	dashDrawHints(h, n);
}

const DashScreen dashSettingsScreen = {settingsEnter, settingsInput, settingsUpdate, settingsDraw, settingsHints};
//--------------------------------------------------------------
//End of file: dash_settings.c
//--------------------------------------------------------------
