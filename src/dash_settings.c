//--------------------------------------------------------------
//File name:   dash_settings.c
//SETTINGS: task-oriented pages over the existing configuration fields.
//Each option has one home; Back returns to the previous page and Triangle
//opens the session's Save / Discard menu from every page.
//
//Changes apply at once but are only written when the user leaves SETTINGS
//and chooses Save; Discard restores what was
//there when SETTINGS was opened. Writes run on the background worker.
//
//Theme previews and edits share the same Save / Discard session.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "config_private.h"
#include "dash_sound.h"
#include "main_title.h"
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
#include "cloud_google.h"
#include "init.h"
#endif
#ifdef SMB
#include "smb_shares.h"
#endif

enum SetType {
	ST_CHOICE = 0,
	ST_ACTION,
	ST_INFO,
	ST_COLOR,
	ST_NUMBER, /* left/right steps a number (screen offsets, delays) */
	ST_PATH,   /* a file/folder dialog with Choose and Clear */
	ST_IP,     /* IPv4 address */
	ST_LKEY,   /* launch key: target/title/clear dialog, Square title */
	ST_PAGE    /* opens a child page; id is a SetPageId, not a SetId */
};

enum SetId {
	/* display */
	S_TVMODE = 0,
	S_SCREEN_X,
	S_SCREEN_Y,
	S_EFFECTS,
	S_ANIM,
	S_LEGACY_THEME,
	S_MENU_FRAME,
	S_POPUP_OPAQUE,
	S_C_COL0, /* classic screen colours, COLOR_BACKGR..COLOR_GRAPH4 */
	S_C_COL7 = S_C_COL0 + COLOR_GRAPH4,
	S_SCREEN_RESET,
	S_DISPLAY_THEME_RESET,
	/* dashboard */
	S_ENABLED,
	S_SOUND,
	S_SVOL,
	S_STHEME,
	S_BGM,
	S_BGMVOL,
	S_START,
	S_FILES_MODE,
	S_VIEW,
	S_HOME_SHORTCUTS,
	S_TAB_HOME,
	S_TAB_FILES,
	S_TAB_TOOLS,
	S_ICON_BACKING,
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
	S_USBKBD_FILE,
	S_KBDMAP_FILE,
	S_LAUNCHKEYS,
	/* SELECT remains reserved for configuration; Left/Right are optional keys. */
	S_LK_0,
	S_LK_LAST = S_LK_0 + SETTING_LK_RIGHT,
	S_PATHPAD_LOCK,
	/* startup */
	S_INIT_DELAY,
	S_TIMEOUT,
	S_CNF_PATH,
	S_LANG_FILE,
	S_FONT_FILE,
	S_ESR_PATH,
	S_OSDSYS_PATH,
	S_POPS_PATH,
	S_EMBER_DIR,
	S_NEUTRINO_PATH,
	S_DISABLE_EMBER,
	S_DISABLE_POPSTARTER,
	S_DISABLE_NEUTRINO,
	/* devices */
	S_HIDE_HDD,
	S_HIDE_MC,
	S_SHOW_UNAVAILABLE,
	S_REBOOT_ELF,
	S_DEVA_USB,
	S_DEVA_HDD,
	S_DEVA_ATA,
	S_DEVA_MMCE,
	S_DEVA_MX4SIO,
	S_DEVA_ILINK,
	S_DEVA_UDPFS,
	S_MMCE_BACKUP_PATH,
	S_MMCE_BACKUP_KEEP,
	S_MMCE_BACKUP_FORMAT,
	S_MMCE_BACKUP_SAVES,
	S_MMCE_SYNC_ALL,
	S_MMCE_RECOVER_LOCAL,
	S_IGR_SYNC_MODE,
	S_IGR_HELPER,
	/* Legacy/internal split flags retained for persisted settings compatibility. */
	S_IGR_AUTO_BACKUP,
	S_IGR_CLOUD_SYNC,
	S_IGR_AFTER,
	S_IGR_AFTER_APP,
	S_MMCE_LANGUAGE,
	S_GOOGLE_ACCOUNT,
	S_CHECK_UPDATES,
	S_ABOUT_PROJECT,
	/* network */
	S_NET_IP,
	S_NET_NM,
	S_NET_GW,
	S_HOSTWRITE,
	S_NET_LEGACY,
	S_SMB_SHARES,
	S_SMBED_PICK, S_SMBED_NAME, S_SMBED_KIND, S_SMBED_IP, S_SMBED_PORT,
	S_SMBED_FOLDER, S_SMBED_USER, S_SMBED_PASS, S_SMBED_UID, S_SMBED_GID,
	S_SMBED_SAVE, S_SMBED_DELETE, S_SMBED_TEXT,
	S_SMBD_USER, S_SMBD_PASS,
	S_CLOCK,
	S_CLOCK_MANUAL,
	S_CLOCK_SOFTWARE,
	S_CLOCK_HARDWARE,
	S_TEMP_UNITS,
	S_CLOCK_SYNC,
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
	S_SAVE_CLASSIC,
	S_SAVE_AS,
	S_REVERT,
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
	S_T_TILE,
	S_T_GLOW,
	S_T_TABS,
	S_T_CONSOLE,
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
	S_T_RESETCAT,
	S_MISC_COMMANDS
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
	const char *help;
	const SetItem *items;
	int n;
} SetPage;

enum SetPageId {
	P_ROOT, P_APPEARANCE, P_SCREEN, P_THEME,
	P_APPS, P_LIBRARY, P_SOUND, P_INPUT, P_KEYBOARD,
	P_LAUNCH, P_BOOT, P_LKEY, P_LOADERS, P_GAMEID,
	P_DEVICES, P_AUTOSTART, P_MMCE_BACKUP, P_NETWORK, P_SYSTEM, P_LANGUAGE, P_PSU,
	P_SMB, P_NFS,
	/* Keep these in DashThemeGroup order for group resets. */
	P_THEME_BG, P_THEME_COLORS, P_THEME_STYLE, P_THEME_TEXT,
	P_THEME_ICONS, P_THEME_CLOCK, P_THEME_WALL, P_THEME_MANAGE,
	P_CLASSIC, P_COUNT
};

static const SetItem root_items[] = {
    {ST_PAGE, P_APPEARANCE, "Appearance", "Theme, text, clock, animation and screen calibration."},
    {ST_PAGE, P_APPS, "Home & applications", "Start screen, application view, artwork and library folders."},
    {ST_PAGE, P_SOUND, "Sound", "Interface effects, background music and their volumes."},
    {ST_PAGE, P_INPUT, "Input", "Confirm button, on-screen keyboard and USB keyboard."},
    {ST_PAGE, P_LAUNCH, "Launching", "Automatic start, button shortcuts, loaders and compatibility."},
    {ST_PAGE, P_DEVICES, "Storage & devices", "Device visibility and which devices start automatically."},
    {ST_PAGE, P_NETWORK, "Network", "Console address and network write access."},
    {ST_PAGE, P_SYSTEM, "System", "Language, save-file options, configuration locations, updates and About."},
    {ST_ACTION, S_MISC_COMMANDS, "Built-in commands", "Open MISC, including configuration and system tools."},
};

static const SetItem appearance_items[] = {
    {ST_CHOICE, S_T_PRESET, "Theme preset", "Confirm opens a live preview. Cancel restores the previous theme."},
    {ST_PAGE, P_THEME, "Customize theme", "Background, colours, tiles, text, icons, clock and artwork background."},
    {ST_PAGE, P_SCREEN, "Screen calibration", "Video mode and picture position for your TV."},
    {ST_CHOICE, S_EFFECTS, "Background motion", "0% freezes backgrounds; 100% is normal; 200% doubles their motion."},
    {ST_CHOICE, S_ANIM, "Menu animation", "0% makes transitions instant; higher values increase movement and transition duration."},
    {ST_CHOICE, S_T_PANEL_A, "Panel transparency", "0% makes panels fully opaque; 100% makes them fully transparent."},
    {ST_CHOICE, S_ICON_BACKING, "Control icon backings", "Show or hide backing shapes behind text-style controls and quick actions."},
    {ST_PAGE, P_THEME_MANAGE, "Theme files & resets", "Import, export, rename or reset your theme."},
    {ST_ACTION, S_DISPLAY_THEME_RESET, "Reset appearance", "Restore screen calibration, motion, animation and the OSD Classic theme."},
};

static const SetItem display_items[] = {
    {ST_CHOICE, S_TVMODE, "Video mode", "Auto follows the console region. 576p needs PS2 ROM 2.20+ or PSX 1.80J/2.10J; 576i uses PAL. Confirm changes within 10 seconds."},
    {ST_NUMBER, S_SCREEN_X, "Screen position X", "Moves the picture left or right to fit your TV (overscan)."},
    {ST_NUMBER, S_SCREEN_Y, "Screen position Y", "Moves the picture up or down to fit your TV."},
    {ST_ACTION, S_SCREEN_RESET, "Reset screen settings", "Video mode Auto and a centred picture."},
};

static const SetItem dash_items[] = {
    {ST_CHOICE, S_START, "Start screen", "Initial screen. Applications opens the library under Home directly; Launch keys opens Settings on its Launch keys page."},
    {ST_CHOICE, S_TAB_HOME, "Home tab", "Off removes HOME from the tabs and from L1/R1. Settings always stays. Saved as Dash_Disabled_Tabs."},
    {ST_CHOICE, S_TAB_FILES, "Files tab", "Off removes FILES from the tabs and from L1/R1. Settings always stays. Saved as Dash_Disabled_Tabs."},
    {ST_CHOICE, S_TAB_TOOLS, "Tools tab", "Off removes TOOLS from the tabs and from L1/R1. Settings always stays. Saved as Dash_Disabled_Tabs."},
    {ST_CHOICE, S_HOME_SHORTCUTS, "Home quick-launch shortcuts", "Show configured launch-key shortcuts alongside Applications and Last folder."},
    {ST_CHOICE, S_VIEW, "Application view", "Grid, list or carousel (SELECT also switches it in Home > Applications)."},
    {ST_CHOICE, S_ARTWORK, "Application artwork", "Load cover art for tiles and artwork backgrounds."},
    {ST_CHOICE, S_RECENT, "Recent applications", "How many recently started applications HOME shows."},
    {ST_PAGE, P_LIBRARY, "Application library", "Search folders, startup refresh and a full library refresh."},
    {ST_ACTION, S_CLEAR_RECENT, "Clear recent applications", "Clears history immediately; applications and favourites are kept. Discard does not restore history."},
};

static const SetItem library_items[] = {
    {ST_ACTION, S_SCANPATHS, "Application folders...", "Only these folders are scanned. Defaults: mc:/APPS, mc:/BOOT, mass:/APPS and mass:/BOOT; each includes one sub-folder level."},
    {ST_CHOICE, S_AUTOSCAN, "Refresh at startup", "Check memory cards and already running devices for applications at startup."},
    {ST_ACTION, S_RESCAN, "Refresh library now", "Search every application folder, loading drivers as needed. Updates the library immediately."},
};

static const SetItem sound_items[] = {
    {ST_CHOICE, S_SOUND, "Interface sounds", "Cursor moves, confirmations and other dashboard sound effects."},
    {ST_CHOICE, S_SVOL, "Interface sound volume", "Volume for dashboard cursor, confirm and notification effects only."},
    {ST_CHOICE, S_STHEME, "Sound theme", "Classic keeps the original effects; PS2 uses soft console-style tones."},
    {ST_CHOICE, S_BGM, "Background music", "Hardware-looped dashboard music, independent from interface sounds."},
    {ST_CHOICE, S_BGMVOL, "Music volume", "Volume for background music only."},
};

static const SetItem control_items[] = {
    {ST_CHOICE, S_CONFIRM, "Confirm button", "Which button confirms. The other one cancels (all screens)."},
    {ST_CHOICE, S_VKEY, "On-screen keyboard layout", NULL},
    {ST_PAGE, P_KEYBOARD, "USB keyboard", "Enable a USB keyboard, choose its driver and set its key map."},
};

static const SetItem keyboard_items[] = {
    {ST_CHOICE, S_USBKBD, "USB keyboard", "Use a USB keyboard as a controller and for typing."},
    {ST_PATH, S_USBKBD_FILE, "USB keyboard driver", "Optional USBKBD.IRX to use instead of the built-in driver."},
    {ST_PATH, S_KBDMAP_FILE, "Keyboard map", "Optional .KBD key map for non-US USB keyboards."},
};

static const SetItem launch_items[] = {
    {ST_PAGE, P_BOOT, "Automatic start & boot", "Choose what starts automatically and how long startup waits."},
    {ST_PAGE, P_LKEY, "Launch keys", "Applications or commands assigned to controller buttons."},
    {ST_CHOICE, S_PATHPAD_LOCK, "Lock shortcuts & bookmarks", "Prevents editing launch keys and File Browser bookmarks."},
    {ST_PAGE, P_LOADERS, "Disc & image loaders", "ESR, OSDSYS, POPStarter, Ember and Neutrino paths."},
    {ST_CHOICE, S_REBOOT_ELF, "Reset IOP before launch", "Recommended: gives launched ELFs a clean IOP. Hold the confirm button while launching to do the opposite once."},
    {ST_PAGE, P_GAMEID, "Game ID output", "Game IDs for RetroGem and compatible OSD/HDMI adapters."},
};

static const SetItem lkey_items[] = {
    {ST_LKEY, S_LK_0 + SETTING_LK_CIRCLE, "Circle", "Hold at startup to start it; also a HOME shortcut."},
    {ST_LKEY, S_LK_0 + SETTING_LK_CROSS, "Cross", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_SQUARE, "Square", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_TRIANGLE, "Triangle", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_L1, "L1", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_R1, "R1", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_L2, "L2", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_R2, "R2", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_L3, "L3", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_R3, "R3", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_START, "START", NULL},
    {ST_LKEY, S_LK_0 + SETTING_LK_LEFT, "Left", "Optional classic launch-menu shortcut."},
    {ST_LKEY, S_LK_0 + SETTING_LK_RIGHT, "Right", "Optional classic launch-menu shortcut."},
};

static const SetItem startup_items[] = {
    {ST_LKEY, S_LK_0 + SETTING_LK_AUTO, "Automatic start target", "Application or command started after the timeout. Clear the target to disable automatic start."},
    {ST_NUMBER, S_INIT_DELAY, "Startup delay", "Seconds to wait at startup, so a held button can pick its launch key."},
    {ST_NUMBER, S_TIMEOUT, "Automatic start timeout", "Seconds before the automatic start runs. Any button cancels it."},
};

static const SetItem loader_items[] = {
    {ST_PATH, S_ESR_PATH, "ESR loader", "ESR ELF used to start ESR-patched discs."},
    {ST_PATH, S_OSDSYS_PATH, "OSDSYS program", "Started by the OSDSYS command. Empty: the console's own."},
    {ST_PATH, S_POPS_PATH, "POPStarter", "POPStarter ELF used to start PS1 VCD images."},
    {ST_PATH, S_EMBER_DIR, "Ember directory", "Folder containing ember.elf, bios.bin and games/. CUE/BIN/EXE routes to Ember automatically; VCD still uses POPStarter."},
    {ST_PATH, S_NEUTRINO_PATH, "Neutrino", "Neutrino ELF used to start PS2 ISO images. Empty: game device, then mc0:/mc1:/neutrino/."},
    {ST_CHOICE, S_DISABLE_EMBER, "Disable Ember support", "Off (default): CUE/BIN/EXE game routes use Ember. On: refuse Ember image launches without loading devices."},
    {ST_CHOICE, S_DISABLE_POPSTARTER, "Disable POPStarter support", "Off (default): VCD launches use POPStarter. On: refuse automatic VCD launches."},
    {ST_CHOICE, S_DISABLE_NEUTRINO, "Disable Neutrino support", "Off (default): ISO launches use Neutrino. On: refuse automatic PS2 ISO launches."},
};

static const SetItem device_items[] = {
    {ST_PAGE, P_MMCE_BACKUP, "MMCE backups", "Versioned backup history, output format and automatic IGR backup preferences."},
    {ST_CHOICE, S_HIDE_HDD, "Internal HDD / ATA units", "Which HDD and exFAT units appear in the File Browser."},
    {ST_CHOICE, S_HIDE_MC, "Hide empty memory card slots", "Also hides unused MMCE entries in the File Browser."},
    {ST_CHOICE, S_SHOW_UNAVAILABLE, "Show unavailable devices", "On keeps disconnected/probe-only device families visible; Off shows only devices that are ready."},
    {ST_PAGE, P_AUTOSTART, "Start devices automatically", "The boot device always stays enabled. Starting other drivers takes effect immediately."},
};

static const SetItem autostart_items[] = {
    {ST_CHOICE, S_DEVA_USB, "Auto-start USB storage", "Load the USB drivers when the dashboard starts. Off: load manually from DEVICES."},
    {ST_CHOICE, S_DEVA_HDD, "Auto-start internal HDD", "Load the HDD drivers when the dashboard starts."},
    {ST_CHOICE, S_DEVA_ATA, "Auto-start exFAT HDD", "Load the exFAT/ATA drivers when the dashboard starts."},
    {ST_CHOICE, S_DEVA_MMCE, "Auto-start MMCE SD card", "Load the MMCE drivers when the dashboard starts."},
    {ST_CHOICE, S_DEVA_MX4SIO, "Auto-start MX4SIO", "Load the MX4SIO drivers when the dashboard starts."},
#ifdef ILINK
    {ST_CHOICE, S_DEVA_ILINK, "Auto-start iLink / FireWire", "Load the IEEE1394 storage drivers when the dashboard starts. Leave off unless this console has a physical i.Link port."},
#endif
    {ST_CHOICE, S_DEVA_UDPFS, "Auto-start network (UDPFS)", "Start the UDPFS network client when the dashboard starts."},
};

static const SetItem mmce_backup_items[] = {
    {ST_PATH, S_MMCE_BACKUP_PATH, "Backup folder", "Choose the parent folder for WLE-MMCE-Backups. Existing history stays where it is. Reinstall the IGR helper after changing this folder."},
    {ST_ACTION, S_MMCE_SYNC_ALL, "Sync all cards", "Back up every changed MMCE card now. If Google Drive is linked, upload pending local history afterward."},
    {ST_ACTION, S_MMCE_RECOVER_LOCAL, "Recover saved card image...", "Choose a local .mcd/.ps2 snapshot and recover it as a NEW verified .mcd on USB/HDD; never overwrites an active MMCE card."},
    {ST_CHOICE, S_IGR_SYNC_MODE, "Automatic sync", "What happens after OPL returns through WLE-IGR: Off, verified local backup, or local backup plus Google Drive."},
    {ST_ACTION, S_IGR_HELPER, "IGR helper", "Install, update or remove the verified WLE-IGR helper used by OPL Exit-to."},
    {ST_CHOICE, S_IGR_AFTER, "After IGR, open", "Choose where to go after the IGR backup/sync step: Launchstation, the PS2 system menu, or a selected application."},
    {ST_PATH, S_IGR_AFTER_APP, "IGR application", "Application started after IGR when 'After IGR, open' is set to Application."},
    {ST_ACTION, S_MMCE_LANGUAGE, "Language", "Open the existing language settings used by file operations and tools."},
    {ST_NUMBER, S_MMCE_BACKUP_KEEP, "Backups per card", "0 keeps every verified version. Otherwise keep the newest 1-99 versions per card."},
    {ST_CHOICE, S_MMCE_BACKUP_FORMAT, "Backup format", "Native .mcd avoids writes when unchanged. PCSX2 .ps2 adds ECC/spare bytes and verifies the converted image."},
    {ST_CHOICE, S_MMCE_BACKUP_SAVES, "Per-save versions", "Also keep each changed save as a .psu in WLE-Saves beside its card's history, uploaded to Google Drive with the cards. Backups per card limits each save."},
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
    {ST_ACTION, S_GOOGLE_ACCOUNT, "Google account", "Link, relink, sync or disconnect the native Google Drive backup account."},
#endif
};

#ifdef SMB
/* ---- SMB share editor ------------------------------------------------------
 * The page edits one draft and splices it into the share blocks at the end of
 * LAUNCHSTATION.CNF (an old SMB2.CNF's on the first save) with smbCnfUpsertText,
 * which keeps the rest of the file (comments, other blocks) byte for byte. An
 * NFS share is a draft of kind SMBED_NFS, written by nfsCnfBuildBlock. */

#define SMBED_NFS 4 /* the editor's kind for NFS; 1-3 are smbCnfBuildBlock's */

static struct {
	char orig[SMB_NAME_LEN]; /* NAME of the picked share; "" = a new one */
	char name[SMB_NAME_LEN];
	int kind; /* 1, 2 or 3, as smbCnfBuildBlock, or SMBED_NFS */
	char host[80]; /* IPv4 from the URL (host names are edit-as-text only) */
	int port;      /* 0 = the default, 445 or NFS's 2049 */
	char folder[128]; /* the SMB share, or the NFS export path */
	char uid[12];     /* NFS only: "" = not given (0) */
	char gid[12];
	char user[SMB_CRED_LEN];
	char pass[SMB_CRED_LEN];
	int plaintext; /* SMB1 clear-text password allowance, kept across edits */
	int pick; /* 0 = New share, else snapshot share index + 1 */
	int nfs;  /* the NFS shares page: its picker lists only NFS shares */
	SmbConfig snap;
} smbed;

/* smb://<host>[:port]/<folder> back into the draft fields. */
static int smbedSplitUrl(const char *url, char *host, int host_size, int *port,
                         char *folder, int folder_size)
{
	const char *h = url, *slash, *colon, *rest;
	long p = 445;
	int len;

	if (strncmp(url, "smb://", 6) != 0)
		return 0;
	h = url + 6;
	slash = strchr(h, '/');
	if (slash == NULL || slash == h)
		return 0;
	colon = memchr(h, ':', slash - h);
	if (colon != NULL) {
		const char *c;
		if (colon + 1 == slash)
			return 0;
		p = 0;
		for (c = colon + 1; c < slash; c++) {
			if (*c < '0' || *c > '9')
				return 0;
			p = p * 10 + (*c - '0');
			if (p > 65535)
				return 0;
		}
		if (p == 0)
			return 0;
		len = colon - h;
	} else {
		len = slash - h;
	}
	if (len <= 0 || len >= host_size)
		return 0;
	memcpy(host, h, len);
	host[len] = '\0';
	rest = slash + 1;
	len = strcspn(rest, "/");
	/* Only one path segment fits the folder field; a deeper path
	   (smb://host/share/sub) is edited as text so it is not cut short. */
	if (rest[len] == '/' && rest[len + 1] != '\0')
		return 0;
	if (len <= 0 || len >= folder_size)
		return 0;
	memcpy(folder, rest, len);
	folder[len] = '\0';
	*port = p == 445 ? 0 : (int)p;
	return 1;
}

/* An NFS URL's uid= or gid= (key with its '='), or "" */
static void smbedNfsOption(const char *url, const char *key, char *out, int out_size)
{
	const char *q = strchr(url, '?');
	size_t klen = strlen(key);

	out[0] = '\0';
	while (q != NULL) {
		q++;
		if (!strncmp(q, key, klen)) {
			int len = strcspn(q + klen, "&");
			if (len < out_size) {
				memcpy(out, q + klen, len);
				out[len] = '\0';
			}
			return;
		}
		q = strchr(q, '&');
	}
}

/* The snapshot shares this page lists (SMB or NFS): map[k] is a snapshot index. */
static int smbedPickList(int *map)
{
	int i, n = 0;

	for (i = 0; i < smbed.snap.count && i < SMB_SHARE_MAX; i++)
		if ((smbed.snap.share[i].version == SMB_PATH_NFS) == smbed.nfs)
			map[n++] = i;
	return n;
}

static void smbedLoadPick(void)
{
	const SmbShare *sh;

	smbSharesSnapshot(&smbed.snap);
	if (smbed.pick < 0 || smbed.pick > smbed.snap.count ||
	    (smbed.pick > 0 && (smbed.snap.share[smbed.pick - 1].version == SMB_PATH_NFS) != smbed.nfs))
		smbed.pick = 0;
	if (smbed.pick == 0) {
		memset(smbed.orig, 0, sizeof(smbed.orig));
		memset(smbed.name, 0, sizeof(smbed.name));
		smbed.kind = smbed.nfs ? SMBED_NFS : 2;
		memset(smbed.host, 0, sizeof(smbed.host));
		smbed.port = 0;
		memset(smbed.folder, 0, sizeof(smbed.folder));
		memset(smbed.user, 0, sizeof(smbed.user));
		memset(smbed.pass, 0, sizeof(smbed.pass));
		memset(smbed.uid, 0, sizeof(smbed.uid));
		memset(smbed.gid, 0, sizeof(smbed.gid));
		smbed.plaintext = 0;
		return;
	}
	sh = &smbed.snap.share[smbed.pick - 1];
	dashStrCopy(smbed.orig, sh->name, sizeof(smbed.orig));
	dashStrCopy(smbed.name, sh->name, sizeof(smbed.name));
	memset(smbed.uid, 0, sizeof(smbed.uid));
	memset(smbed.gid, 0, sizeof(smbed.gid));
	if (sh->version == SMB_PATH_NFS) {
		/* An invalid NFS share has no fields split out: edited as text. */
		smbed.kind = SMBED_NFS;
		smbed.plaintext = 0;
		memset(smbed.user, 0, sizeof(smbed.user));
		memset(smbed.pass, 0, sizeof(smbed.pass));
		dashStrCopy(smbed.host, sh->server, sizeof(smbed.host));
		smbed.port = sh->port == 2049 ? 0 : sh->port;
		if (strlen(sh->share) < sizeof(smbed.folder))
			dashStrCopy(smbed.folder, sh->share, sizeof(smbed.folder));
		else
			smbed.folder[0] = '\0';
		smbedNfsOption(sh->url, "uid=", smbed.uid, sizeof(smbed.uid));
		smbedNfsOption(sh->url, "gid=", smbed.gid, sizeof(smbed.gid));
		return;
	}
	smbed.kind = sh->version;
	smbed.plaintext = sh->plaintext;
	dashStrCopy(smbed.user, sh->username, sizeof(smbed.user));
	dashStrCopy(smbed.pass, sh->password, sizeof(smbed.pass));
	if (!smbedSplitUrl(sh->url, smbed.host, sizeof(smbed.host), &smbed.port,
	                   smbed.folder, sizeof(smbed.folder))) {
		/* Uneditable URL shape (a name in the share segment, or too long):
		   keep it whole so saving the block writes it back unchanged. */
		smbed.host[0] = '\0';
		smbed.folder[0] = '\0';
		smbed.port = 0;
	}
}

static int saveSharesCnf(void);

static int smbedWriteFile(const char *block)
{
	char *out, *work, *text = NULL, *old = NULL;
	const char *base = smbSharesBlocks();
	SmbConfig cfg;
	int len, i, matches = 0, valid = 0, ret = -3;

	out = (char *)malloc(SMB_CNF_MAX_SIZE + 512);
	work = (char *)malloc(SMB_CNF_MAX_SIZE + 512);
	if (out == NULL || work == NULL)
		goto out_free;
	if (base == NULL) {
		/* The first save takes an old SMB2.CNF's shares across into
		   LAUNCHSTATION.CNF. SMB2.CNF itself is never written. */
		smbSharesConfigured(SMB_PATH_V2);
		if (smbSharesConfigPath()[0] &&
		    (text = dashLoadTextFile(smbSharesConfigPath(), SMB_CNF_MAX_SIZE, NULL)) == NULL) {
			ret = -4;
			goto out_free;
		}
		base = text != NULL ? text : "";
	}
	len = smbCnfUpsertText(base, smbed.orig, block, out, SMB_CNF_MAX_SIZE + 512, work, SMB_CNF_MAX_SIZE + 512);
	free(text);
	if (len < 0 || len > SMB_CNF_MAX_SIZE) {
		ret = -1;
		goto out_free;
	}
	memcpy(work, out, len + 1);
	smbConfigParse(work, &cfg);
	for (i = 0; i < cfg.count; i++)
		if (!stricmp(cfg.share[i].name, block != NULL ? smbed.name : smbed.orig)) {
			matches++;
			if (cfg.share[i].state != SMB_STATE_INVALID)
				valid++;
		}
	if (block != NULL ? (matches != 1 || valid != 1) : matches != 0) {
		ret = -2;
		goto out_free;
	}
	if ((old = strdup(out)) == NULL)
		goto out_free;
	old = smbSharesTakeBlocks(old); /* the old blocks stay ours until the write is done */
	if (!saveSharesCnf()) {
		free(smbSharesTakeBlocks(old)); /* not written: the shares stay as they were */
		old = NULL;
		ret = -5;
	} else
		ret = smbSharesReload() ? 0 : -6;
	smbSharesSnapshot(&smbed.snap);
out_free:
	free(old);
	free(out);
	free(work);
	return ret;
}

static void smbedSave(void)
{
	char block[512];
	int n, r, i;

	if (smbed.kind == SMBED_NFS)
		n = nfsCnfBuildBlock(block, sizeof(block), smbed.name, smbed.host,
		                     smbed.port > 0 ? smbed.port : 2049, smbed.folder,
		                     smbed.uid[0] ? atol(smbed.uid) : -1, smbed.gid[0] ? atol(smbed.gid) : -1);
	else
		n = smbCnfBuildBlock(block, sizeof(block), smbed.name, smbed.kind,
		                     smbed.user, smbed.pass, smbed.host,
		                     smbed.port > 0 ? smbed.port : 445, smbed.folder, smbed.plaintext);
	if (n < 0) {
		dashModalMessage(DI_WARNING, "Share not saved",
		                 smbed.kind == SMBED_NFS ?
		                 "A field is empty or too long, the IPv4 address or port is not valid, or the export path "
		                 "does not start with /." :
		                 "A field is empty or too long, or the IPv4 address or port is not valid. "
		                 "Shares whose address is a name, or whose URL has a deeper path, are edited as text.", NULL);
		return;
	}
	r = smbedWriteFile(block);
	if (r == -4) {
		dashModalMessage(DI_WARNING, "Share not saved",
		                 "The old SMB2.CNF could not be read; nothing was changed.", NULL);
		return;
	}
	if (r == -1) {
		dashModalMessage(DI_WARNING, "Share not saved",
		                 "The share list is full or a limit was hit; edit the shares as text instead.", NULL);
		return;
	}
	if (r == -2) {
		dashModalMessage(DI_WARNING, "Share not saved",
		                 "The file would not accept this share (a NAME may already be in use).", NULL);
		return;
	}
	if (r == -6) {
		dashModalMessage(DI_WARNING, "Share saved; restart needed",
		                 "The share was saved, but the old SMB connection could not be disconnected. Restart Launchstation to apply the changed share.", NULL);
		return;
	}
	if (r != 0) {
		dashModalMessage(DI_WARNING, "Share not saved", "The settings could not be saved.", NULL);
		return;
	}
	smbSharesSnapshot(&smbed.snap);
	/* Point the picker at the saved share: the next Save replaces it, and
	   Delete enables, instead of silently appending a second copy. */
	for (i = 0; i < smbed.snap.count; i++)
		if (strcmp(smbed.snap.share[i].name, smbed.name) == 0) {
			smbed.pick = i + 1;
			dashStrCopy(smbed.orig, smbed.name, sizeof(smbed.orig));
			break;
		}
	dashModalMessage(DI_CHECK, "Share saved",
	                 smbed.kind == SMBED_NFS ? "Open Files > Network (NFS). NFS shares show only once they connect." :
	                 "Open Files > Network (SMB). SMB2/3 shares show only once they connect.", smbed.name);
}

static void smbedDelete(void)
{
	int r;

	if (smbed.pick == 0)
		return;
	r = smbedWriteFile(NULL);
	if (r != 0) {
		dashModalMessage(DI_WARNING,
		                 r == -6 ? "Share deleted; restart needed" : "Share not deleted",
		                 r == -6 ? "Saved configuration could not retire old SMB sessions. Restart Launchstation." :
		                 "The settings could not be saved.", NULL);
		return;
	}
	smbed.pick = 0;
	smbedLoadPick();
	dashModalMessage(DI_CHECK, "Share deleted", "The share was removed.", NULL);
}

static void smbedEditField(const char *title, char *buf, int size, const char *error)
{
	char edit[96];

	dashStrCopy(edit, buf, sizeof(edit));
	if (!dashModalKeyboard(title, edit, sizeof(edit)))
		return;
	if (strlen(edit) >= (size_t)size) {
		dashModalMessage(DI_WARNING, "Too long", error, NULL);
		return;
	}
	dashStrCopy(buf, edit, size);
}

static const SetItem smb_items[] = {
	{ST_CHOICE, S_SMBED_PICK, "Share", "New share, or one already saved."},
	{ST_ACTION, S_SMBED_NAME, "Name shown in Files", "Folder name under Network (SMB). Example: PS2 Games. Use a unique name; avoid / \\ : * ? \" < > |."},
	{ST_CHOICE, S_SMBED_KIND, "Server type", "Match the server: PS2-Servers SMBv2 supports up to 2.1; SMBv3 up to 3.0.2. SMB1 is legacy; one SMB1 share at most. NFS shares have their own page: Settings > Network > NFS shares."},
	{ST_ACTION, S_SMBED_IP, "Server address", "IPv4 address of the server. Example: 192.168.1.20. Host names are edited as text."},
	{ST_ACTION, S_SMBED_PORT, "Port", "TCP port from server settings. Examples: 445 standard, 1445 PS2-Servers SMB2/3, or 1025 its Desktop SMB1."},
	{ST_ACTION, S_SMBED_FOLDER, "Share name", "The exported SMB share on the server. Example: games (PS2-Servers default)."},
	{ST_ACTION, S_SMBED_USER, "Username", "Account name on the server. Example: ps2 for SMB2/3 or guest for SMB1 on PS2-Servers."},
	{ST_ACTION, S_SMBED_PASS, "Password", "Password for that server account. Leave blank only if the server allows no-password access; saved in LAUNCHSTATION.CNF as plain text."},
	{ST_ACTION, S_SMBED_SAVE, "Save share", "Saves this share in LAUNCHSTATION.CNF. Open it in Files > Network (SMB)."},
	{ST_ACTION, S_SMBED_DELETE, "Delete share", "Removes the picked share."},
	{ST_ACTION, S_SMBED_TEXT, "Edit shares as text", "Opens LAUNCHSTATION.CNF; the shares are the blocks after its # SMB shares line. For host names, deeper paths and other options."},
};

/* The same editor, for NFS shares: the picker lists only those, and a new share is NFS. */
static const SetItem nfs_items[] = {
	{ST_CHOICE, S_SMBED_PICK, "Share", "New share, or one already saved."},
	{ST_ACTION, S_SMBED_NAME, "Name shown in Files", "Folder name under Network (NFS). Example: PS2 Games. Use a unique name; avoid / \\ : * ? \" < > |."},
	{ST_ACTION, S_SMBED_IP, "Server address", "IPv4 address of the NFS server (NFSv4.0). Example: 192.168.1.20. Host names are edited as text."},
	{ST_ACTION, S_SMBED_PORT, "Port", "TCP port of the NFS server. 2049 is standard."},
	{ST_ACTION, S_SMBED_FOLDER, "Export path", "The exported path on the server, starting with /. Example: / or /srv/ps2."},
	{ST_ACTION, S_SMBED_UID, "User ID (NFS)", "The uid the PS2 sends to the NFS server. Blank sends 0, which most servers map to their anonymous user."},
	{ST_ACTION, S_SMBED_GID, "Group ID (NFS)", "The gid the PS2 sends to the NFS server. Blank sends 0."},
	{ST_ACTION, S_SMBED_SAVE, "Save share", "Saves this share in LAUNCHSTATION.CNF. Open it in Files > Network (NFS)."},
	{ST_ACTION, S_SMBED_DELETE, "Delete share", "Removes the picked share."},
	{ST_ACTION, S_SMBED_TEXT, "Edit shares as text", "Opens LAUNCHSTATION.CNF; the shares are the blocks after its # SMB shares line, NFS ones included."},
};
#endif /* SMB */

static const SetItem net_items[] = {
    {ST_IP, S_NET_IP, "IP address", "The console's address for FTP, PS2Link and UDPFS (IPCONFIG.DAT)."},
    {ST_IP, S_NET_NM, "Netmask", "Usually 255.255.255.0."},
    {ST_IP, S_NET_GW, "Gateway", "Usually your router's address."},
#if defined(ETH) || defined(UDPFS)
    {ST_CHOICE, S_HOSTWRITE, "Allow writing over the network", "Lets host: / udpfs: be written to, not only read."},
#endif
#ifdef ETH
    {ST_CHOICE, S_NET_LEGACY, "Legacy Ethernet drivers", "Use MrRexD's ps2sdk v1.0 DEV9/PS2IP pair when newer drivers load but the SMAP link never comes up. Takes effect the next time Ethernet/FTP is started."},
    {ST_ACTION, S_SMBD_USER, "SMB server user name", "Login for Tools > SMB server (memory cards). Used from the next Start."},
    {ST_ACTION, S_SMBD_PASS, "SMB server password", "Login for Tools > SMB server (memory cards). Used from the next Start."},
    {ST_ACTION, S_CLOCK_SYNC, "Sync PS2 clock from Internet", "Query 0.pool.ntp.org and write and verify the PS2 hardware RTC. Uses the IP settings above; once the network runs, changes need a save and restart."},
#endif
#ifdef SMB
    {ST_PAGE, P_SMB, "SMB shares", "Saved shares, shown as folders in Files > Network (SMB). Nothing scans the network."},
    {ST_PAGE, P_NFS, "NFS shares", "Saved NFS exports, shown as folders in Files > Network (NFS). Nothing scans the network."},
#endif
};

static const SetItem language_items[] = {
    {ST_CHOICE, S_LANGUAGE, "Language", "Dashboard labels, file operations and tools."},
    {ST_PATH, S_LANG_FILE, "Language file", "Optional .LNG overrides, including dashboard labels."},
};

static const SetItem gameid_items[] = {
    {ST_CHOICE, S_GAMEID, "Game ID launch signal for apps", "Show the brief RetroGem-compatible Game ID signal/bar when launching ELFs. Turn this off if your display adapter does not use it."},
    {ST_CHOICE, S_GAMEID_DISC, "Game ID launch signal for discs", "Show the same brief Game ID signal when starting discs."},
};

static const SetItem psu_items[] = {
    {ST_CHOICE, S_PSU_HUGE, "PSU: add game ID and title to names", NULL},
    {ST_CHOICE, S_PSU_DATE, "PSU: add date to names", NULL},
    {ST_CHOICE, S_PSU_NOOVER, "PSU: never overwrite existing files", "Enabling this also enables dated names. Turning dated names off disables this option."},
};

static const SetItem system_items[] = {
    {ST_PAGE, P_LANGUAGE, "Language", "Built-in language and external translations for file operations and tools."},
    {ST_PAGE, P_PSU, "Memory card backup files", "PSU naming and overwrite rules used by File Browser operations."},
    {ST_ACTION, S_CLOCK, "Set PS2 Clock", "Set the date and time by hand, from the Internet where available, or with the software clock for a dead CMOS battery."},
    {ST_CHOICE, S_TEMP_UNITS, "PS2 Temperature Units (50K and later)", "Celsius or Fahrenheit beside the clock. Hidden automatically when the console has no supported temperature sensor."},
    {ST_PATH, S_CNF_PATH, "Configuration folder", "Optional save target for LAUNCHSTATION.CNF. Loading searches the start folder, then mcN:/SYS-CONF/ on the launch card, then the other card."},
    {ST_ACTION, S_SAVE_AS, "Save launch & tool settings to...", "Writes LAUNCHSTATION.CNF immediately to another location; dashboard and network settings are saved separately."},
    {ST_PAGE, P_CLASSIC, "Classic screen settings", "Appearance of classic dialogs and the legacy launch menu."},
    {ST_ACTION, S_INTRO, "Show welcome message again", "Show the introduction the next time the dashboard checks for it."},
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
    {ST_ACTION, S_CHECK_UPDATES, "Check for updates", "Compare with the rolling latest release; a newer build can be downloaded, SHA-256 verified and installed in place."},
#endif
    {ST_ACTION, S_ABOUT_PROJECT, "About Launchstation", "Version, commit, project origin, upstream credits and contributors."},
};

/* These are persisted upstream settings, independent of dashboard themes. */
static const SetItem classic_items[] = {
    {ST_CHOICE, S_MENU_FRAME, "Menu frame", "Draw the frame around classic screens."},
    {ST_CHOICE, S_POPUP_OPAQUE, "Opaque popups", "Use an opaque background for classic popup menus."},
    {ST_CHOICE, S_SHOW_TITLES, "Show launch titles", "Use custom launch-key titles in the classic launch menu."},
    {ST_CHOICE, S_HIDE_PATHS, "Hide full ELF paths", "Show only the file name in the classic launch menu."},
    {ST_CHOICE, S_FB_ICONS, "File Browser icons", "Show file and folder icons in classic file-selection dialogs."},
    {ST_COLOR, S_C_COL0 + COLOR_BACKGR, "Background colour", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_FRAME, "Frame colour", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_TEXT, "Text colour", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_SELECT, "Selected text colour", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_GRAPH1, "Graph colour 1", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_GRAPH2, "Graph colour 2", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_GRAPH3, "Graph colour 3", NULL},
    {ST_COLOR, S_C_COL0 + COLOR_GRAPH4, "Graph colour 4", NULL},
};

#define NITEMS(a) ((int)(sizeof(a) / sizeof(a[0])))
static const SetItem theme_items[] = {
    {ST_PAGE, P_THEME_BG, "Background", "Scene, colours, brightness, scanlines and wallpaper image."},
    {ST_PAGE, P_THEME_COLORS, "Interface colours", "Text, selection, accent, panels and borders."},
    {ST_PAGE, P_THEME_STYLE, "Tiles & menus", "Application tile shape, selection style, corners, glow and the section tabs."},
    {ST_PAGE, P_THEME_TEXT, "Text", "Font, size, weight and shadow."},
    {ST_PAGE, P_THEME_ICONS, "Icons", "Icon colours and artwork-free application icons."},
    {ST_PAGE, P_THEME_CLOCK, "Clock", "Position, time format, seconds and date."},
    {ST_PAGE, P_THEME_WALL, "Artwork background", "Use the focused application's cover behind Home and Apps."},
};

/* Theme editor groups (order = enum DashThemeGroup, then Manage). */
static const SetItem tbg_items[] = {
    {ST_CHOICE, S_T_BG, "Background", "PS2-inspired scenes, plain colours or your own wallpaper image."},
    {ST_COLOR, S_T_BG1, "Colour", "Main colour of the background (the solid colour, gradient top or scene tint)."},
    {ST_COLOR, S_T_BG2, "Second colour", "Base or gradient bottom colour."},
    {ST_CHOICE, S_T_BRIGHT, "Brightness", "Darken the background behind the menus."},
    {ST_CHOICE, S_T_SCAN, "CRT scanlines", "An understated line pattern over the background only."},
    {ST_ACTION, S_T_IMAGE, "Wallpaper image...", "Choose a PNG (up to 1.5 MB). It is scaled to at most 640x512 and cover-fitted."},
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
    {ST_COLOR, S_T_BORDER, "Borders", "Panel outline colour and opacity (0 = no borders). Hidden when Panels is transparent."},
    {ST_COLOR, S_T_WARN, "Warnings", "Favourites, warnings and unsaved-change marks."},
    {ST_ACTION, S_T_RESETCAT, "Reset colours", "Back to the values of the theme's preset."},
};
static const SetItem tstyle_items[] = {
    {ST_CHOICE, S_T_HL, "Selection style", "Text colour is how the PS2 system menus mark the selection."},
    {ST_CHOICE, S_T_TILE, "Tile style", "Application tiles: memory card, rounded, chamfered, floating object or crystal slab."},
    {ST_CHOICE, S_T_CORNER, "Corners", NULL},
    {ST_CHOICE, S_T_GLOW, "Glow", "Soft light behind focused items."},
    {ST_CHOICE, S_T_TABS, "Section tabs", "Shown or Hidden. Hidden removes the top bar; L1 and R1 still switch sections."},
    {ST_CHOICE, S_T_CONSOLE, "Console info on HOME", "The console model, ROM and region line at the top of HOME. System information keeps the full details."},
    {ST_ACTION, S_T_RESETCAT, "Reset style", "Back to the values of the theme's preset."},
};
static const SetItem ttype_items[] = {
    {ST_CHOICE, S_T_FONT, "Font", "System uses the built-in font (or your .fnt) and its accented characters."},
    {ST_PATH, S_FONT_FILE, "System font file", "External .FNT font used when Font is set to System."},
    {ST_CHOICE, S_T_FSCALE, "Text size", "Larger sizes can shorten long names."},
    {ST_CHOICE, S_T_BOLD, "Style", "Regular or Bold. Emotion Engine uses its original font faces."},
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
    {ST_ACTION, S_T_RESETCAT, "Reset wallpaper", "Back to the values of the theme's preset."},
};
static const SetItem tmanage_items[] = {
    {ST_ACTION, S_T_RENAME, "Theme name...", NULL},
    {ST_ACTION, S_T_SAVE, "Export theme to folder...", "Writes the current preview to a shareable theme folder immediately. This does not save the Settings session."},
    {ST_ACTION, S_T_LOAD, "Load theme from folder...", "Themes are folders in THEMES/ next to DASHBOARD.CNF, each with a theme.cfg."},
    {ST_ACTION, S_T_RESTORE_PRESET, "Reset entire theme", "Undo every change and go back to the preset this theme started from."},
    {ST_ACTION, S_T_DEFAULT, "Restore default theme", "OSD Classic with its original settings."},
};

#define PAGE(name, icon, help, items) {name, icon, help, items, NITEMS(items)}
static const SetPage pages[P_COUNT] = {
    [P_ROOT] = PAGE("Settings", DI_SETTINGS, "Choose what you want to configure.", root_items),
    [P_APPEARANCE] = PAGE("Appearance", DI_PALETTE, "Preview changes here; save or discard them together.", appearance_items),
    [P_SCREEN] = PAGE("Screen calibration", DI_SETTINGS, "Fit the picture to your TV. Changes apply immediately.", display_items),
    [P_THEME] = PAGE("Customize theme", DI_PALETTE, "Choose which part of your theme to edit.", theme_items),
    [P_APPS] = PAGE("Home & applications", DI_HOME, "How applications are presented and discovered.", dash_items),
    [P_LIBRARY] = PAGE("Application library", DI_APPS, "Folders and refresh behavior for the ELF library.", library_items),
    [P_SOUND] = PAGE("Sound", DI_PLAY, "Music and interface sounds have separate volume controls.", sound_items),
    [P_INPUT] = PAGE("Input", DI_GAMEPAD, "Controller confirmation and keyboard input.", control_items),
    [P_KEYBOARD] = PAGE("USB keyboard", DI_KEY, "USB keyboard support and optional driver/key map.", keyboard_items),
    [P_LAUNCH] = PAGE("Launching", DI_PLAY, "Boot actions, button shortcuts and application handoff.", launch_items),
    [P_BOOT] = PAGE("Automatic start & boot", DI_CLOCK, "Any button cancels the automatic start countdown.", startup_items),
    [P_LKEY] = PAGE("Launch keys", DI_KEY, "Hold a button at boot, or hold SELECT and press it in the dashboard.", lkey_items),
    [P_LOADERS] = PAGE("Disc & PS1 loaders", DI_DISC, "Programs used by disc, OSDSYS and PS1 launch commands.", loader_items),
    [P_GAMEID] = PAGE("Game ID output", DI_APP, "Identification for compatible OSD/HDMI adapters.", gameid_items),
    [P_DEVICES] = PAGE("Storage & devices", DI_DEVICES, "Visible devices and driver startup preferences.", device_items),
    [P_AUTOSTART] = PAGE("Start devices automatically", DI_DEVICES, "Drivers start immediately when enabled. Discard restores preferences, not driver state.", autostart_items),
    [P_MMCE_BACKUP] = PAGE("MMCE backup & sync", DI_MC, "Backup, cloud and IGR return controls in one place.", mmce_backup_items),
    [P_NETWORK] = PAGE("Network", DI_NETWORK, "Console address for FTP, PS2Link and UDPFS.", net_items),
#ifdef SMB
    [P_SMB] = PAGE("SMB shares", DI_NETWORK, "Saved in LAUNCHSTATION.CNF; an old SMB2.CNF is read until the first save. Each share is a folder in Files > Network (SMB).", smb_items),
    [P_NFS] = PAGE("NFS shares", DI_NETWORK, "Saved in LAUNCHSTATION.CNF with the SMB shares. Each share is a folder in Files > Network (NFS).", nfs_items),
#endif
    [P_SYSTEM] = PAGE("System", DI_TOOLS, "Language, backup file rules, settings locations, updates and About.", system_items),
    [P_CLASSIC] = PAGE("Classic screen settings", DI_SETTINGS, "These settings affect classic screens; dashboard themes have their own controls.", classic_items),
    [P_LANGUAGE] = PAGE("Language", DI_TEXT, "Some extended help remains in English.", language_items),
    [P_PSU] = PAGE("Memory card backup files", DI_MC, "Naming and overwrite behavior when creating PSU backups.", psu_items),
    [P_THEME_BG] = PAGE("Background", DI_IMAGE, "Scene, brightness and static wallpaper.", tbg_items),
    [P_THEME_COLORS] = PAGE("Interface colours", DI_PALETTE, "Colours and opacity for text, focus and panels.", tcol_items),
    [P_THEME_STYLE] = PAGE("Tiles & menus", DI_VIEW_GRID, "Tile silhouettes, menu selection styling and the section tabs.", tstyle_items),
    [P_THEME_TEXT] = PAGE("Text", DI_TEXT, "Font and readability. System can use your external font file.", ttype_items),
    [P_THEME_ICONS] = PAGE("Icons", DI_APPS, "Fallback icons for applications without artwork.", ticon_items),
    [P_THEME_CLOCK] = PAGE("Clock", DI_CLOCK, "Clock placement and date/time format.", tclock_items),
    [P_THEME_WALL] = PAGE("Artwork background", DI_IMAGE, "Requires Application artwork in Home & applications.", twall_items),
    [P_THEME_MANAGE] = PAGE("Theme files & resets", DI_FOLDER, "Export writes a theme file immediately. Resets stay in this Settings session.", tmanage_items),
};

/* Root -> category -> subpage -> theme group is the deepest path. */
#define PAGE_DEPTH 4
/* Seven rows leave room for the two-line help text above the hint bar
 * (dashDrawHints draws from y = 394); eight pushed it onto the buttons. */
#define ITEM_ROWS 7
static int page, item;
static int item_top;
static float item_anim;
static struct { int page, item, top; } page_stack[PAGE_DEPTH];
static int page_depth;

static void openPage(int next)
{
	if (next < 0 || next >= P_COUNT || page_depth >= PAGE_DEPTH)
		return;
	page_stack[page_depth].page = page;
	page_stack[page_depth].item = item;
	page_stack[page_depth++].top = item_top;
	page = next;
	item = item_top = 0;
	item_anim = 0.0f;
	if (next == P_MMCE_BACKUP)
		dashToolsPrepareMmceStatus();
	if (next == P_NETWORK)
		dashEnsureIpConfig(); /* cached; serialized against worker IO */
}

static void backPage(void)
{
	if (!page_depth)
		return;
	page_depth--;
	page = page_stack[page_depth].page;
	item = page_stack[page_depth].item;
	item_top = page_stack[page_depth].top;
	item_anim = (float)item;
}

/* Unsaved-changes session: opened when SETTINGS is entered, closed when
 * the user leaves it (Save / Discard). The snapshots are what Discard
 * restores. */
static int session;
static int dash_dirty, classic_dirty, net_dirty;
static DashSettings snap_dash;
static DashTheme snap_theme;
static int snap_theme_preset, snap_theme_custom;
static SETTING snap_classic;
static char snap_ip[16], snap_nm[16], snap_gw[16];
/* what saving would have written when the snapshot was taken */
static char *snap_dash_text, *snap_cnf_text;
static u64 dirty_checked_ms;

static const char *const onoff[] = {"Off", "On"};
static const char *const tv_names[] = {"Auto", "NTSC", "PAL", "VGA", "480p", "576p", "576i"};
static const char *const view_names[] = {"Grid", "List", "Carousel"};
static const char *const recent_names[] = {"0", "4", "8", "12", "16"};
static const char *const mmce_backup_format_names[] = {"Native .mcd", "PCSX2 .ps2"};
static const char *const igr_sync_names[] = {"Off", "Local backup", "Local + Google Drive"};
static const char *const igr_after_names[] = {"Dashboard", "PS2 system menu", "Application"};
static const char *const hide_names[] = {"Show all", "Hide HDD1", "Hide HDD0+1", "Hide ATA1", "Hide ATA0+1", "Hide HDD1+ATA1", "Hide all"};
static const char *const sound_theme_names[] = {"Classic", "PS2"};
static const char *const confirm_names[] = {"Circle", "Cross"};
static const char *const hl_names[] = {"Solid bar", "Accent bar", "Outline", "Text colour"};
static const char *const tile_names[] = {"Memory card", "Rounded", "Chamfered", "Floating object", "Crystal slab"};
static const char *const corner_names[] = {"Square", "Soft", "Round"};
static const char *const font_names[DASH_FONT_COUNT] = {"Roboto", "System", "Emotion Engine"};
static const char *const font_style_names[] = {"Regular", "Bold"};
static const char *const fscale_names[] = {"90%", "95%", "100%", "105%", "110%", "115%"};
static const char *const tint_names[] = {"Text colours", "Accent", "Monochrome"};
static const char *const appicon_names[] = {"Application", "Device", "Initials"};
static const char *const clock_names[] = {"Off", "Standard", "System"};
static const char *const clockpos_names[] = {"Header", "Footer"};
static const char *const tabs_names[] = {"Shown", "Hidden"};
static const char *const hours_names[] = {"System", "24-hour", "12-hour"};
static const char *const date_names[] = {"System", "YYYY/MM/DD", "MM/DD/YYYY", "DD/MM/YYYY", "Off"};
static const char *const wall_names[] = {"Off", "Focused app"};
static const char *const pct_names[] = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%",
                                        "110%", "120%", "130%", "140%", "150%", "160%", "170%", "180%", "190%", "200%"};

static void themeChanged(void)
{
	dashThemeCommit();
	dash_dirty = 1;
}

/* Dash_Disabled_Tabs bit for the Home/Files/Tools tab rows (issue #278). */
static int tabSettingBit(int id)
{
	return 1 << (id == S_TAB_HOME ? DASH_SEC_HOME : id == S_TAB_FILES ? DASH_SEC_FILES : DASH_SEC_TOOLS);
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
			*names = pct_names, *cur = (dashCfg.effects + 5) / 10;
			return 21;
		case S_ANIM:
			*names = pct_names, *cur = (dashCfg.anim + 5) / 10;
			return 21;
		case S_LEGACY_THEME:
			*cur = dashCfg.legacy_theme;
			return 2;
		case S_ENABLED:
			*cur = dashCfg.enabled;
			return 2;
#ifdef SMB
		case S_SMBED_KIND: {
			static const char *const smb_kinds[] = { "SMB2", "SMB3", "SMB1" };
			*names = smb_kinds;
			*cur = smbed.kind == 3 ? 1 : smbed.kind == 1 ? 2 : 0;
			return 3;
		}
		case S_SMBED_PICK: {
			static const char *pick_names[SMB_SHARE_MAX + 1];
			int map[SMB_SHARE_MAX], i, n = smbedPickList(map);

			pick_names[0] = "New share";
			*cur = 0;
			for (i = 0; i < n; i++) {
				pick_names[i + 1] = smbed.snap.share[map[i]].name;
				if (map[i] + 1 == smbed.pick)
					*cur = i + 1;
			}
			*names = pick_names;
			return n + 1;
		}
#endif
		case S_SOUND:
			*cur = dashCfg.sound;
			return 2;
		case S_SVOL:
			*names = pct_names, *cur = dashCfg.sound_vol / 10;
			return 11;
		case S_STHEME:
			*names = sound_theme_names, *cur = dashCfg.sound_theme;
			return 2;
		case S_BGM:
			*cur = dashCfg.bgm;
			return 2;
		case S_BGMVOL:
			*names = pct_names, *cur = dashCfg.bgm_vol / 10;
			return 11;
		case S_START: {
			static const char *const start_names[] = {"Home", "Applications", "Files", "Tools", "Settings", "Launch keys"};
			*names = start_names;
			*cur = (dashCfg.start_section == DASH_SEC_APPS) ? 1 :
			       (dashCfg.start_section == DASH_SEC_FILES || dashCfg.start_section == DASH_SEC_DEVICES) ? 2 :
			       (dashCfg.start_section == DASH_SEC_TOOLS) ? 3 :
			       (dashCfg.start_section == DASH_SEC_SETTINGS) ? 4 :
			       (dashCfg.start_section == DASH_START_LAUNCH_KEYS) ? 5 : 0;
			return 6;
		}
		case S_FILES_MODE: {
			static const char *const modes[] = {"Files", "Devices"};
			*names = modes, *cur = dashCfg.files_mode;
			return 2;
		}
		case S_HOME_SHORTCUTS:
			*cur = dashCfg.home_shortcuts;
			return 2;
		case S_TAB_HOME:
		case S_TAB_FILES:
		case S_TAB_TOOLS:
			*cur = !(dashCfg.disabled_tabs & tabSettingBit(id));
			return 2;
		case S_TEMP_UNITS: {
			static const char *const units[] = {"Celsius", "Fahrenheit"};
			*names = units, *cur = dashCfg.temp_fahrenheit;
			return 2;
		}
		case S_ICON_BACKING:
			*cur = dashCfg.icon_backing;
			return 2;
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
		case S_SHOW_UNAVAILABLE:
			*cur = dashCfg.show_unavailable_devices;
			return 2;
		case S_REBOOT_ELF:
			*cur = setting->reboot_iop_elf_load ? 1 : 0;
			return 2;
		case S_DEVA_USB:
			*cur = (dashCfg.dev_auto & DASH_DEVA_USB) || dashBootDeviceBit() == DASH_DEVA_USB ? 1 : 0;
			return 2;
		case S_DEVA_HDD:
			*cur = (dashCfg.dev_auto & DASH_DEVA_HDD) || dashBootDeviceBit() == DASH_DEVA_HDD ? 1 : 0;
			return 2;
		case S_DEVA_ATA:
			*cur = (dashCfg.dev_auto & DASH_DEVA_ATA) || dashBootDeviceBit() == DASH_DEVA_ATA ? 1 : 0;
			return 2;
		case S_DEVA_MMCE:
			*cur = (dashCfg.dev_auto & DASH_DEVA_MMCE) || dashBootDeviceBit() == DASH_DEVA_MMCE ? 1 : 0;
			return 2;
		case S_DEVA_MX4SIO:
			*cur = (dashCfg.dev_auto & DASH_DEVA_MX4SIO) || dashBootDeviceBit() == DASH_DEVA_MX4SIO ? 1 : 0;
			return 2;
#ifdef ILINK
		case S_DEVA_ILINK:
			*cur = (dashCfg.dev_auto & DASH_DEVA_ILINK) || dashBootDeviceBit() == DASH_DEVA_ILINK ? 1 : 0;
			return 2;
#endif
		case S_DEVA_UDPFS:
			*cur = (dashCfg.dev_auto & DASH_DEVA_UDPFS) ? 1 : 0;
			return 2;
		case S_MMCE_BACKUP_FORMAT:
			*names = mmce_backup_format_names, *cur = dashCfg.mmce_backup_format;
			return 2;
		case S_MMCE_BACKUP_SAVES:
			*cur = dashCfg.mmce_backup_saves;
			return 2;
		case S_IGR_SYNC_MODE:
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
			*names = igr_sync_names;
			*cur = !dashCfg.igr_auto_backup ? 0 : (dashCfg.igr_cloud_sync ? 2 : 1);
			return 3;
#else
			*names = igr_sync_names;
			*cur = dashCfg.igr_auto_backup ? 1 : 0;
			return 2;
#endif
		case S_IGR_AUTO_BACKUP:
			*cur = dashCfg.igr_auto_backup;
			return 2;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		case S_IGR_CLOUD_SYNC:
			*cur = dashCfg.igr_cloud_sync;
			return 2;
#endif
		case S_IGR_AFTER:
			*names = igr_after_names, *cur = dashCfg.igr_after;
			return DASH_IGR_AFTER_COUNT;
#if defined(ETH) || defined(UDPFS)
		case S_HOSTWRITE:
			*cur = setting->HOSTwrite ? 1 : 0;
			return 2;
#endif
#ifdef ETH
		case S_NET_LEGACY:
			*cur = setting->legacy_net_drivers ? 1 : 0;
			return 2;
#endif
		case S_MENU_FRAME:
			*cur = setting->Menu_Frame ? 1 : 0;
			return 2;
		case S_POPUP_OPAQUE:
			*cur = setting->Popup_Opaque ? 1 : 0;
			return 2;
		case S_PATHPAD_LOCK:
			*cur = setting->PathPad_Lock ? 1 : 0;
			return 2;
		case S_LANGUAGE: {
			static const char *langs[BUILTIN_LANGUAGE_COUNT];
			int i;
			for (i = 0; i < BUILTIN_LANGUAGE_COUNT; i++)
				langs[i] = getBuiltinLanguageNativeName(i);
			*names = langs, *cur = normalizeBuiltinLanguage(setting->language);
			return BUILTIN_LANGUAGE_COUNT;
		}
		case S_DISABLE_EMBER:
			*cur = setting->disable_ember ? 1 : 0;
			return 2;
		case S_DISABLE_POPSTARTER:
			*cur = setting->disable_popstarter ? 1 : 0;
			return 2;
		case S_DISABLE_NEUTRINO:
			*cur = setting->disable_neutrino ? 1 : 0;
			return 2;
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
			/* 10%..200% */
			*names = pct_names + 1, *cur = (dashTheme.bg_bright + 5) / 10 - 1;
			return 20;
		case S_T_SCAN:
			*cur = dashTheme.scanlines;
			return 2;
		case S_T_HL:
			*names = hl_names, *cur = dashTheme.highlight;
			return DASH_HL_COUNT;
		case S_T_TILE:
			*names = tile_names, *cur = dashTheme.tile_style;
			return DASH_TILE_COUNT;
		case S_T_CORNER:
			*names = corner_names, *cur = dashTheme.corner;
			return DASH_CORNER_COUNT;
		case S_T_GLOW:
			*cur = dashTheme.glow;
			return 2;
		case S_T_TABS:
			*names = tabs_names, *cur = dashTheme.tabs;
			return DASH_TABS_COUNT;
		case S_T_CONSOLE:
			*cur = dashTheme.console_info;
			return 2;
		case S_T_PANEL_A:
			*names = pct_names, *cur = ((255 - dashTheme.panel.a) * 10 + 127) / 255;
			return 11;
		case S_T_FONT:
			*names = font_names, *cur = dashTheme.font;
			return DASH_FONT_COUNT;
		case S_T_FSCALE:
			*names = fscale_names, *cur = (dashTheme.font_scale - 90 + 2) / 5;
			return 6;
		case S_T_BOLD:
			*names = font_style_names, *cur = dashTheme.bold;
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

/* Theme choices preview at once and share the Settings save session. */
static int setThemeChoice(int id, int v)
{
	DashTheme *t = &dashTheme;

	switch (id) {
		case S_T_PRESET:
			dashThemeSetPreset(v);
			dash_dirty = 1;
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
		case S_T_TILE:
			t->tile_style = (u8)v;
			break;
		case S_T_CORNER:
			t->corner = (u8)v;
			break;
		case S_T_GLOW:
			t->glow = (u8)v;
			break;
		case S_T_TABS:
			t->tabs = (u8)v;
			break;
		case S_T_CONSOLE:
			t->console_info = (u8)v;
			break;
		case S_T_PANEL_A:
			t->panel.a = (u8)((10 - v) * 255 / 10);
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
/* Classic screen colours are GS RGBA values in SETTING; the editor works
 * on a DashColor copy that is written back after every change. */
static DashColor classic_col[COLOR_COUNT];

static int isClassicColor(int id)
{
	return id >= S_C_COL0 && id <= S_C_COL7;
}

static void classicColorStore(int id)
{
	DashColor c = classic_col[id - S_C_COL0];

	setting->color[id - S_C_COL0] = GS_SETREG_RGBA(c.r, c.g, c.b, 0);
	classic_dirty = 1;
}

static DashColor *colorField(int id, int *alpha)
{
	*alpha = 0;
	if (isClassicColor(id)) {
		u64 v = setting->color[id - S_C_COL0];
		DashColor *c = &classic_col[id - S_C_COL0];
		c->r = v & 0xFF;
		c->g = (v >> 8) & 0xFF;
		c->b = (v >> 16) & 0xFF;
		c->a = 255;
		return c;
	}
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
	int alpha, row = 0, sw = 0, nrows, i, classic;
	DashColor *c = colorField(id, &alpha);
	DashColor orig, before;
	float open = 0.0f;
	const float w = 360.0f, x = DASH_SAFE_R - w;
	float h, y;
	DashInput in;
	static const char *const ch_names[4] = {"Red", "Green", "Blue", "Opacity"};

	if (c == NULL)
		return;
	classic = isClassicColor(id);
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
		if (in.pressed & DASH_IN_UP) {
			row = (row + nrows - 1) % nrows;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_DOWN) {
			row = (row + 1) % nrows;
			dashSoundPlay(DASH_SND_CURSOR);
		}
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
			if (!classic)
				dashThemeApply(); /* live, validated on confirm */
		}
		if (in.pressed & DASH_IN_OK) {
			if (classic) {
				if (c->r != orig.r || c->g != orig.g || c->b != orig.b)
					classicColorStore(id);
			} else {
				before = *c;
				dashThemeCommit();
				dash_dirty = 1;
				if (before.r != c->r || before.g != c->g || before.b != c->b || before.a != c->a)
					dashToast(DASH_TOAST_INFO, "Adjusted to stay readable");
			}
			dashSoundPlay(DASH_SND_CONFIRM);
			dashInputFlush();
			return;
		}
		if (dashDashboardPending() || (in.pressed & DASH_IN_BACK)) {
			*c = orig;
			if (!classic)
				dashThemeApply();
			dashSoundPlay(DASH_SND_CANCEL);
			dashInputFlush();
			return;
		}
		open = dashApproach(open, 1.0f, 14.0f * dashAnimSpeed(), dashVisualUpdate());

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
				dashTextS(hx + 18, hy + 4, localizeDashboardText("Cancel"), DASH_FS_CAPTION, DASH_C_TEXT2);
			}
		}
		dashGfxPopAlpha();
		dashGfxPresent();
	}
}

static int chooseVideoMode(const char *title, const char *const *names, int n, int current)
{
	DashMenuItem choices[TV_mode_COUNT];
	int i;

	for (i = 0; i < n && i < TV_mode_COUNT; i++) {
		choices[i].icon = i == current ? DI_CHECK : -1;
		choices[i].label = localizeDashboardText(names[i]);
		choices[i].disabled = i == TV_mode_576P && !is576pSupported();
		choices[i].detail = choices[i].disabled ? "Unsupported by this console" : (i == TV_mode_576I ? "PAL, 50 Hz" : NULL);
	}
	return dashModalMenu(title, NULL, choices, i, current);
}

/* Adapted from saildot4k's timed video preview. Restore offsets as well as
 * timing because changing the GS mode can normalize screen geometry. */
static int previewVideoMode(int mode)
{
	int old_mode = setting->TV_mode;
	int old_x = setting->screen_x, old_y = setting->screen_y;
	if (mode == old_mode) return 0;
	setting->TV_mode = mode;
	updateScreenMode();
	if (dashGfxCheckScreenChange()) dashArtInvalidateVram();
	if (dashModalKeepVideo()) return 1;
	setting->TV_mode = old_mode;
	setting->screen_x = old_x;
	setting->screen_y = old_y;
	updateScreenMode();
	if (dashGfxCheckScreenChange()) dashArtInvalidateVram();
	return 0;
}

static void setChoice(int id, int v)
{
	int dash_changed = 1;

	if (setThemeChoice(id, v))
		return;
	switch (id) {
		case S_TVMODE:
			if (v < TV_mode_AUTO || v >= TV_mode_COUNT || (v == TV_mode_576P && !is576pSupported()))
				return;
			if (!previewVideoMode(v)) return;
			classic_dirty = 1, dash_changed = 0;
			break;
		case S_EFFECTS:
			dashCfg.effects = v * 10;
			break;
		case S_ANIM:
			dashCfg.anim = v * 10;
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
			dashSoundApplyVolume();
			break;
		case S_STHEME:
			dashCfg.sound_theme = v;
			dashSoundReloadTheme();
			dashSoundPlay(DASH_SND_CONFIRM);
			break;
		case S_BGM:
			dashCfg.bgm = v;
			if (v)
				dashBgmStart();
			else
				dashBgmStop();
			break;
		case S_BGMVOL:
			dashCfg.bgm_vol = v * 10;
			dashBgmSetVolume();
			break;
		case S_DEVA_USB:
		case S_DEVA_HDD:
		case S_DEVA_ATA:
		case S_DEVA_MMCE:
		case S_DEVA_MX4SIO:
#ifdef ILINK
		case S_DEVA_ILINK:
#endif
		case S_DEVA_UDPFS: {
			const int bit = (id == S_DEVA_USB) ? DASH_DEVA_USB :
			                (id == S_DEVA_HDD) ? DASH_DEVA_HDD :
			                (id == S_DEVA_ATA) ? DASH_DEVA_ATA :
			                (id == S_DEVA_MMCE) ? DASH_DEVA_MMCE :
			                (id == S_DEVA_MX4SIO) ? DASH_DEVA_MX4SIO :
			                (id == S_DEVA_ILINK) ? DASH_DEVA_ILINK : DASH_DEVA_UDPFS;
			const int boot_bit = dashBootDeviceBit();

			/* MMCE and MX4SIO cannot coexist in one IOP storage stack. Never
			 * switch away from either one when it is the device we booted
			 * from; otherwise enabling one disables the other's auto-start. */
			if (v && ((bit == DASH_DEVA_MMCE && boot_bit == DASH_DEVA_MX4SIO) ||
			          (bit == DASH_DEVA_MX4SIO && boot_bit == DASH_DEVA_MMCE))) {
				dashToast(DASH_TOAST_WARN,
				          "MMCE and MX4SIO cannot auto-start together while booted from this device");
				break;
			}
			if (v && bit == DASH_DEVA_MMCE)
				dashCfg.dev_auto &= ~DASH_DEVA_MX4SIO;
			else if (v && bit == DASH_DEVA_MX4SIO)
				dashCfg.dev_auto &= ~DASH_DEVA_MMCE;

			/* the boot device is always started: its driver is already running */
			if (v || boot_bit == bit)
				dashCfg.dev_auto |= bit;
			else
				dashCfg.dev_auto &= ~bit;
			if (v)
				dashDevicesAutoStart(); /* start it right away if it is off */
			break;
		}
		case S_START:
			dashCfg.start_section = (v == 1) ? DASH_SEC_APPS :
			                        (v == 2) ? DASH_SEC_FILES :
			                        (v == 3) ? DASH_SEC_TOOLS :
			                        (v == 4) ? DASH_SEC_SETTINGS :
			                        (v == 5) ? DASH_START_LAUNCH_KEYS : DASH_SEC_HOME;
			break;
		case S_FILES_MODE:
			dashCfg.files_mode = v;
			break;
		case S_HOME_SHORTCUTS:
			dashCfg.home_shortcuts = v;
			dashHomeRebuild();
			break;
		case S_TAB_HOME:
		case S_TAB_FILES:
		case S_TAB_TOOLS:
			/* The menu never hides Settings, so it cannot lock itself out. */
			dashCfg.disabled_tabs &= ~((1 << DASH_SEC_SETTINGS) | tabSettingBit(id));
			if (!v)
				dashCfg.disabled_tabs |= tabSettingBit(id);
			break;
		case S_TEMP_UNITS:
			dashCfg.temp_fahrenheit = v;
			break;
		case S_ICON_BACKING:
			dashCfg.icon_backing = v;
			break;
		case S_SHOW_UNAVAILABLE:
			dashCfg.show_unavailable_devices = v;
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
		case S_MMCE_BACKUP_FORMAT:
			dashCfg.mmce_backup_format = v;
			dashToolsRefreshMmceStatus();
			break;
		case S_MMCE_BACKUP_SAVES:
			dashCfg.mmce_backup_saves = v;
			break;
		case S_IGR_SYNC_MODE:
			if (v <= 0) {
				dashCfg.igr_auto_backup = 0;
				dashCfg.igr_cloud_sync = 0;
			} else if (v == 1) {
				dashCfg.igr_auto_backup = 1;
				dashCfg.igr_cloud_sync = 0;
			} else {
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
				if (!cloudGoogleIsLinked()) {
					dashToast(DASH_TOAST_WARN, "Link Google Drive before enabling cloud automatic sync");
					break;
				}
				dashCfg.igr_auto_backup = 1;
				dashCfg.igr_cloud_sync = 1;
#else
				dashToast(DASH_TOAST_WARN, "This build does not include native Google Drive support");
				break;
#endif
			}
			break;
		case S_IGR_AUTO_BACKUP:
			dashCfg.igr_auto_backup = v;
			break;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		case S_IGR_CLOUD_SYNC:
			if (v && !cloudGoogleIsLinked()) {
				dashCfg.igr_cloud_sync = 0;
				dashToast(DASH_TOAST_WARN, "Link Google Drive in Tools before enabling automatic sync");
				break;
			}
			dashCfg.igr_cloud_sync = v;
			break;
#endif
		case S_IGR_AFTER:
			dashCfg.igr_after = v;
			break;
#ifdef SMB
		case S_SMBED_KIND:
			smbed.kind = v == 1 ? 3 : v == 2 ? 1 : 2;
			dash_changed = 0;
			break;
		case S_SMBED_PICK: {
			int map[SMB_SHARE_MAX], n = smbedPickList(map);

			if (v >= 0 && v <= n && (v == 0 ? 0 : map[v - 1] + 1) != smbed.pick) {
				smbed.pick = v == 0 ? 0 : map[v - 1] + 1;
				smbedLoadPick();
			}
			dash_changed = 0;
			break;
		}
#endif
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
#if defined(ETH) || defined(UDPFS)
				case S_HOSTWRITE:
					setting->HOSTwrite = v;
					break;
#endif
#ifdef ETH
				case S_NET_LEGACY:
					setting->legacy_net_drivers = v;
					break;
#endif
				case S_MENU_FRAME:
					setting->Menu_Frame = v;
					break;
				case S_POPUP_OPAQUE:
					setting->Popup_Opaque = v;
					break;
				case S_PATHPAD_LOCK:
					setting->PathPad_Lock = v;
					break;
				case S_LANGUAGE:
					Set_Language(v);
					Load_External_Language();
					break;
				case S_DISABLE_EMBER:
					setting->disable_ember = v;
					break;
				case S_DISABLE_POPSTARTER:
					setting->disable_popstarter = v;
					break;
				case S_DISABLE_NEUTRINO:
					setting->disable_neutrino = v;
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
					if (!v)
						setting->PSU_NoOverwrite = 0; /* needs dated names */
					break;
				case S_PSU_NOOVER:
					setting->PSU_NoOverwrite = v;
					if (v)
						setting->PSU_DateNames = 1;
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
		dash_dirty = 1;
}

//--------------------------------------------------------------
// Numbers, paths, IP addresses and launch keys
//--------------------------------------------------------------
static int numberInfo(int id, int *val, int *lo, int *hi, char *text, int text_size)
{
	switch (id) {
		case S_SCREEN_X:
			*val = setting->screen_x, *lo = -gsGlobal->StartX, *hi = gsGlobal->StartX;
			snprintf(text, text_size, "%+d", *val);
			return 1;
		case S_SCREEN_Y:
			*val = setting->screen_y, *lo = -gsGlobal->StartY, *hi = gsGlobal->StartY;
			snprintf(text, text_size, "%+d", *val);
			return 1;
		case S_INIT_DELAY:
			*val = setting->Init_Delay, *lo = 0, *hi = 60;
			if (*val)
				snprintf(text, text_size, "%d s", *val);
			else
				snprintf(text, text_size, "%s", localizeDashboardText("Off"));
			return 1;
		case S_TIMEOUT:
			*val = setting->timeout, *lo = 0, *hi = 99;
			snprintf(text, text_size, "%d s", *val);
			return 1;
		case S_MMCE_BACKUP_KEEP:
			*val = dashCfg.mmce_backup_keep, *lo = 0, *hi = 99;
			if (*val == 0)
				snprintf(text, text_size, "%s", localizeDashboardText("All"));
			else
				snprintf(text, text_size, "%d", *val);
			return 1;
	}
	return 0;
}

static void setNumber(int id, int v)
{
	switch (id) {
		case S_SCREEN_X:
			setting->screen_x = v;
			updateScreenMode();
			break;
		case S_SCREEN_Y:
			setting->screen_y = v;
			updateScreenMode();
			break;
		case S_INIT_DELAY:
			setting->Init_Delay = v;
			break;
		case S_TIMEOUT:
			setting->timeout = v;
			break;
		case S_MMCE_BACKUP_KEEP:
			dashCfg.mmce_backup_keep = v;
			dash_dirty = 1;
			return;
	}
	classic_dirty = 1;
}

typedef struct
{
	char *field;
	int size;
	int mode;        /* DASH_PICK_* */
	const char *ext; /* file filter, NULL = any */
	int mc_alias;    /* store mc0:/mc1: as mc: like the classic screens */
} PathSpec;

static int pathInfo(int id, PathSpec *p)
{
	memset(p, 0, sizeof(*p));
	p->mode = DASH_PICK_FILE;
	switch (id) {
		case S_USBKBD_FILE:
			p->field = setting->usbkbd_file, p->size = sizeof(setting->usbkbd_file), p->ext = "IRX";
			return 1;
		case S_KBDMAP_FILE:
			p->field = setting->kbdmap_file, p->size = sizeof(setting->kbdmap_file), p->ext = "KBD";
			return 1;
		case S_CNF_PATH:
			p->field = setting->CNF_Path, p->size = sizeof(setting->CNF_Path), p->mode = DASH_PICK_FOLDER;
			return 1;
		case S_LANG_FILE:
			p->field = setting->lang_file, p->size = sizeof(setting->lang_file), p->ext = "LNG";
			return 1;
		case S_FONT_FILE:
			p->field = setting->font_file, p->size = sizeof(setting->font_file), p->ext = "FNT";
			return 1;
		case S_ESR_PATH:
			p->field = setting->LK_Path[SETTING_LK_ESR], p->size = MAX_PATH, p->ext = "ELF", p->mc_alias = 1;
			return 1;
		case S_OSDSYS_PATH:
			p->field = setting->LK_Path[SETTING_LK_OSDSYS], p->size = MAX_PATH, p->mc_alias = 1;
			return 1;
		case S_POPS_PATH:
			p->field = setting->popstarter_file, p->size = sizeof(setting->popstarter_file), p->ext = "ELF", p->mc_alias = 1;
			return 1;
		case S_EMBER_DIR:
			p->field = setting->ember_dir, p->size = sizeof(setting->ember_dir), p->mode = DASH_PICK_FOLDER;
			return 1;
		case S_NEUTRINO_PATH:
			p->field = setting->neutrino_file, p->size = sizeof(setting->neutrino_file), p->ext = "ELF", p->mc_alias = 1;
			return 1;
		case S_MMCE_BACKUP_PATH:
			p->field = dashCfg.igr_backup_path, p->size = sizeof(dashCfg.igr_backup_path), p->mode = DASH_PICK_FOLDER;
			return 1;
		case S_IGR_AFTER_APP:
			p->field = dashCfg.igr_after_path, p->size = sizeof(dashCfg.igr_after_path), p->ext = "ELF";
			return 1;
	}
	return 0;
}

static const char *pathLeaf(const char *path, char *buf, int size)
{
	const char *p;
	int len = strlen(path);

	if (len == 0)
		return "Not set";
	if (path[len - 1] == '/') {
		/* a folder: show it whole, it is short enough to be useful */
		return path;
	}
	p = strrchr(path, '/');
	if (p == NULL)
		p = strchr(path, ':');
	dashStrCopy(buf, p ? p + 1 : path, size);
	return buf;
}

static void mcAlias(char *path, int size)
{
	char tmp[MAX_PATH];

	if (!strncmp(path, "mc0", 3) || !strncmp(path, "mc1", 3)) {
		snprintf(tmp, sizeof(tmp), "mc%s", path + 3);
		dashStrCopy(path, tmp, size);
	}
}

/* Applies a changed path setting (fonts and languages are loaded now). */
static void pathApplied(int id)
{
	if (id == S_ESR_PATH || id == S_OSDSYS_PATH) {
		int lk = id == S_ESR_PATH ? SETTING_LK_ESR : SETTING_LK_OSDSYS;
		setting->LK_Flag[lk] = setting->LK_Path[lk][0] != '\0';
	}
	if (id == S_IGR_AFTER_APP || id == S_MMCE_BACKUP_PATH) {
		if (id == S_MMCE_BACKUP_PATH)
			dashToolsRefreshMmceStatus();
		dash_dirty = 1;
		return;
	}
	classic_dirty = 1;
	if (id == S_LANG_FILE || id == S_FONT_FILE) {
		/* these read files on the UI thread */
		if (!dashWorkerAwaitAll("Finishing up"))
			return;
		if (id == S_LANG_FILE)
			Load_External_Language();
		else if (setting->font_file[0] && loadFont(setting->font_file) == 0) {
			setting->font_file[0] = '\0';
			dashModalMessage(DI_WARNING, "Font not loaded", "The file is not a compatible font. The built-in font stays in use.", NULL);
		} else if (!setting->font_file[0])
			loadFont("");
	}
	if (id == S_ESR_PATH)
		setting->LK_Flag[SETTING_LK_ESR] = setting->LK_Path[SETTING_LK_ESR][0] ? 1 : 0;
	if (id == S_OSDSYS_PATH)
		setting->LK_Flag[SETTING_LK_OSDSYS] = setting->LK_Path[SETTING_LK_OSDSYS][0] ? 1 : 0;
}

static void startDirOf(const char *path, char *out, int size)
{
	char *slash;

	dashStrCopy(out, path, size);
	if (!strncmp(out, "mc:", 3)) {
		/* the picker needs a real slot */
		char tmp[MAX_PATH];
		snprintf(tmp, sizeof(tmp), "mc0:%s", out + 3);
		dashStrCopy(out, tmp, size);
	}
	slash = strrchr(out, '/');
	if (slash != NULL)
		slash[1] = '\0';
	else
		out[0] = '\0';
}

static void editPath(const SetItem *it)
{
	PathSpec ps;
	char out[MAX_PATH], start[MAX_PATH], title[64];

	if (!pathInfo(it->id, &ps))
		return;
	startDirOf(ps.field, start, sizeof(start));
	snprintf(title, sizeof(title), "%s", localizeDashboardText(it->label));
	out[0] = '\0';
	if (!dashPickPath(title, start, ps.mode, ps.ext, out, sizeof(out)))
		return;
	if ((int)strlen(out) >= ps.size) {
		dashModalMessage(DI_WARNING, "Path too long", "Choose a location with a shorter path.", out);
		return;
	}
	if (ps.mc_alias)
		mcAlias(out, sizeof(out));
	dashStrCopy(ps.field, out, ps.size);
	pathApplied(it->id);
}

static void clearPath(const SetItem *it)
{
	PathSpec ps;

	if (!pathInfo(it->id, &ps) || ps.field[0] == '\0')
		return;
	ps.field[0] = '\0';
	pathApplied(it->id);
	dashToast(DASH_TOAST_INFO, "Cleared");
}

static char *ipField(int id)
{
	return (id == S_NET_IP) ? ip : (id == S_NET_NM) ? netmask : gw;
}

static void editIp(const SetItem *it)
{
	char buf[16];

	dashStrCopy(buf, ipField(it->id), sizeof(buf));
	if (!dashModalIp(localizeDashboardText(it->label), localizeDashboardText(it->help), buf, sizeof(buf)) || !strcmp(buf, ipField(it->id)))
		return;
	dashStrCopy(ipField(it->id), buf, 16);
	updateIpConfigRuntimeState();
	net_dirty = 1;
}

static const char *lkeyValue(int lk, char *buf, int size)
{
	if (!setting->LK_Path[lk][0])
		return "Not set";
	if (setting->LK_Title[lk][0])
		return setting->LK_Title[lk];
	return pathLeaf(setting->LK_Path[lk], buf, size);
}

static int lkeyLocked(void)
{
	if (!setting->PathPad_Lock)
		return 0;
	dashToast(DASH_TOAST_WARN, "Unlock shortcuts in Settings > Launching");
	return 1;
}

static void editLaunchKey(const SetItem *it, int what)
{
	int lk = it->id - S_LK_0;
	char out[MAX_PATH], start[MAX_PATH], title[MAX_ELF_TITLE];

	if (lkeyLocked())
		return;
	if (what == 0) {
		snprintf(title, sizeof(title), "Launch key: %s", localizeDashboardText(it->label));
		startDirOf(setting->LK_Path[lk], start, sizeof(start));
		out[0] = '\0';
		if (!dashPickPath(title, start, DASH_PICK_LAUNCH, "ELF", out, sizeof(out)))
			return;
		mcAlias(out, sizeof(out));
		dashStrCopy(setting->LK_Path[lk], out, MAX_PATH);
		setting->LK_Flag[lk] = out[0] != '\0';
		classic_dirty = 1;
	} else if (what == 1) {
		if (!setting->LK_Path[lk][0]) {
			dashToast(DASH_TOAST_INFO, "Choose what this key starts first");
			return;
		}
		dashStrCopy(title, setting->LK_Title[lk], sizeof(title));
		if (dashModalKeyboard("Title shown for this key", title, MAX_ELF_TITLE - 1)) {
			dashStrCopy(setting->LK_Title[lk], title, MAX_ELF_TITLE);
			classic_dirty = 1;
		}
	} else if (setting->LK_Path[lk][0]) {
		setting->LK_Path[lk][0] = '\0';
		setting->LK_Flag[lk] = 0;
		setting->LK_Title[lk][0] = '\0';
		classic_dirty = 1;
		dashToast(DASH_TOAST_INFO, "Launch key cleared");
	}
	dashHomeRebuild();
}

//--------------------------------------------------------------
// Saving (background) and the unsaved-changes session
//--------------------------------------------------------------
enum {
	CFG_SAVE_CNF = 0,
	CFG_SAVE_NET
};

typedef struct
{
	int kind, target, len, ret;
	char file[64], path[MAX_PATH], loaded[MAX_PATH], override_dir[MAX_PATH];
	char *text;
} CfgSaveJob;

/* Pending is distinct from saved. Failures survive leaving/re-entering
 * Settings and are retried only on an explicit Save. */
static int cfg_pending[2], cfg_failed[2];
/* Only saves requested in this visit can make its changes unsaved again.
 * An older bookmark/automatic save failure is still visible/retryable, but
 * is not evidence that the user edited Settings. */
enum { SESSION_SAVE_CNF = 1, SESSION_SAVE_NET = 2, SESSION_SAVE_DASH = 4 };
static int session_saves;

static void cfgSaveRun(void *arg)
{
	CfgSaveJob *j = (CfgSaveJob *)arg;
	if (j->target == -2) {
		if (configSnapshotSaveTargetPath(j->file, j->loaded, j->override_dir,
		                               j->path, sizeof(j->path)) == CONFIG_SAVE_TARGET_CANCEL)
			return;
	} else
		configRefreshSaveTargetForWrite(j->target, j->path, sizeof(j->path), j->file, j->loaded);
	j->ret = j->kind == CFG_SAVE_NET ? configWriteNetworkSnapshot(j->path, j->text)
	                                  : dashWriteFile(j->path, j->text, j->len);
	if (j->ret == 0 && j->kind != CFG_SAVE_NET)
		configStampMcFolderIcon();
}

static void cfgSaveDone(void *arg)
{
	CfgSaveJob *j = (CfgSaveJob *)arg;
	char msg[96];

	cfg_pending[j->kind]--;
	cfg_failed[j->kind] = j->ret != 0;
	if (j->ret == 0) {
		dashStrCopy(j->kind == CFG_SAVE_NET ? LoadedIPConfigPath : LoadedConfigPath, j->path, MAX_PATH);
		snprintf(msg, sizeof(msg), "Saved %.88s", j->path);
		dashToast(DASH_TOAST_OK, msg);
	} else {
		snprintf(msg, sizeof(msg), "Could not save %s; retry in Settings", j->file);
		dashToast(DASH_TOAST_ERROR, msg);
	}
	free(j->text);
	free(j);
}

static int cfgSave(int kind, int target, const char *path);

/* LAUNCHSTATION.CNF now, where it was loaded from (File Browser bookmarks) */
int dashSettingsSaveCnf(void)
{
	return cfgSave(CFG_SAVE_CNF, -2, NULL);
}

#ifdef SMB
/* Share saves are written at once, with the settings as they were when
 * Settings opened, so Discard still undoes the other edits. Waits for the write. */
static int saveSharesCnf(void)
{
	SETTING *cur = session ? (SETTING *)malloc(sizeof(SETTING)) : NULL;
	int ok;

	if (session && cur == NULL)
		return 0;
	if (cur != NULL) {
		memcpy(cur, setting, sizeof(SETTING));
		memcpy(setting, &snap_classic, sizeof(SETTING));
	}
	ok = dashSettingsSaveCnf();
	if (cur != NULL) {
		memcpy(setting, cur, sizeof(SETTING));
		free(cur);
	}
	/* not cancelable: the result decides what stays in memory */
	return ok && (!dashWorkerBusy() || dashWorkerAwait(dashWorkerLastId(), "Saving settings", 0)) &&
	       !cfg_failed[CFG_SAVE_CNF];
}
#endif

static int cfgSave(int kind, int target, const char *path)
{
	CfgSaveJob *j = calloc(1, sizeof(CfgSaveJob));
	if (j == NULL)
		goto failed;
	j->kind = kind;
	j->target = target;
	j->ret = -1;
	dashStrCopy(j->file, kind == CFG_SAVE_NET ? "IPCONFIG.DAT" : dashExecContext()->cnf_path, sizeof(j->file));
	dashStrCopy(j->loaded, kind == CFG_SAVE_NET ? LoadedIPConfigPath : LoadedConfigPath, sizeof(j->loaded));
	dashStrCopy(j->override_dir, setting->CNF_Path, sizeof(j->override_dir));
	if (path != NULL)
		dashStrCopy(j->path, path, sizeof(j->path));
	if (kind == CFG_SAVE_NET) {
		j->text = malloc(50);
		if (j->text != NULL)
			j->len = snprintf(j->text, 50, "%s %s %s\n\r", ip, netmask, gw);
	} else
		j->text = configBuildSaveText(&j->len);
	if (j->text == NULL || dashWorkerSubmit(kind == CFG_SAVE_NET ? "Saving network settings" : "Saving Launchstation settings",
	                                      DASH_JOB_SAVE | DASH_JOB_VISIBLE, cfgSaveRun, cfgSaveDone, j) < 0) {
		free(j->text);
		free(j);
		goto failed;
	}
	cfg_pending[kind]++;
	cfg_failed[kind] = 0;
	return 1;
failed:
	cfg_failed[kind] = 1;
	dashToast(DASH_TOAST_ERROR, "Save could not be queued; changes are still unsaved");
	return 0;
}

static char *dashSaveText(void)
{
	int len;
	return dashStoreSettingsText(&dashCfg, &len);
}

static char *cnfSaveText(void)
{
	int len;
	return configBuildSaveText(&len);
}

/* frees `now`; unknown (no memory) counts as changed */
static int textChanged(const char *snap, char *now)
{
	int changed = (snap == NULL || now == NULL) ? 1 : strcmp(snap, now) != 0;
	free(now);
	return changed;
}

/* "Changed" means saving would write something different from when
 * SETTINGS was opened (or last saved), not "touched": picking the same
 * value again, or changing something and changing it back, leaves
 * nothing to save. A byte comparison with the snapshot rules out the
 * common unchanged case cheaply; when bytes differ (a string edited and
 * put back can leave old characters after its end) the text that would
 * be saved decides. `force` = 0 re-checks at most every 250 ms. */
static void refreshDirty(int force)
{
	u64 now = Timer();

	if (!session || (!force && now - dirty_checked_ms < 250))
		return;
	dirty_checked_ms = now;
	dash_dirty = memcmp(&dashCfg, &snap_dash, sizeof(snap_dash)) != 0 ||
	             memcmp(&dashTheme, &snap_theme, sizeof(snap_theme)) != 0 ||
	             dashThemeBasePreset() != snap_theme_preset || dashThemeIsCustom() != snap_theme_custom;
	if (dash_dirty)
		dash_dirty = textChanged(snap_dash_text, dashSaveText());
	classic_dirty = memcmp(setting, &snap_classic, sizeof(snap_classic)) != 0;
	if (classic_dirty)
		classic_dirty = textChanged(snap_cnf_text, cnfSaveText());
	net_dirty = strcmp(ip, snap_ip) || strcmp(netmask, snap_nm) || strcmp(gw, snap_gw);
}

static int failedSaves(void)
{
	return cfg_failed[CFG_SAVE_CNF] || cfg_failed[CFG_SAVE_NET] || dashStoreSettingsFailed();
}

static int dirtyOrFailed(void)
{
	return dash_dirty || classic_dirty || net_dirty ||
	       ((session_saves & SESSION_SAVE_CNF) && cfg_failed[CFG_SAVE_CNF]) ||
	       ((session_saves & SESSION_SAVE_NET) && cfg_failed[CFG_SAVE_NET]) ||
	       ((session_saves & SESSION_SAVE_DASH) && dashStoreSettingsFailed());
}

static int anyDirty(void)
{
	refreshDirty(1);
	return dirtyOrFailed();
}

/* for the per-frame "unsaved changes" line */
static int anyDirtyCached(void)
{
	refreshDirty(0);
	return dirtyOrFailed();
}

static void snapshotTexts(void)
{
	free(snap_dash_text);
	free(snap_cnf_text);
	snap_dash_text = dashSaveText();
	snap_cnf_text = cnfSaveText();
}

static void sessionSnapshot(void)
{
	menuTitleClockSnapshot();
	/* byte copies, so the comparison in refreshDirty() never sees padding */
	memcpy(&snap_dash, &dashCfg, sizeof(snap_dash));
	memcpy(&snap_theme, &dashTheme, sizeof(snap_theme));
	snap_theme_preset = dashThemeBasePreset();
	snap_theme_custom = dashThemeIsCustom();
	memcpy(&snap_classic, setting, sizeof(snap_classic));
	snapshotTexts();
	dashStrCopy(snap_ip, ip, sizeof(snap_ip));
	dashStrCopy(snap_nm, netmask, sizeof(snap_nm));
	dashStrCopy(snap_gw, gw, sizeof(snap_gw));
	dash_dirty = classic_dirty = net_dirty = 0;
}

static void sessionBegin(void)
{
	if (session)
		return;
	session = 1;
	session_saves = 0;
	sessionSnapshot();
}

static int softwareClockCheckpointAvailable(void)
{
	return dashCfg.software_clock[0] && menuTitleSoftwareClockActive();
}

/* Writes explicit settings saves, including the current software time. */
static void settingsCommit(void)
{
	int cnf_failed = 0, net_failed = 0;

	refreshDirty(1);
	if (dash_dirty || dashStoreSettingsFailed() || softwareClockCheckpointAvailable()) {
		session_saves |= SESSION_SAVE_DASH;
		dashStoreSaveSettings();
	}
	if (classic_dirty || cfg_failed[CFG_SAVE_CNF]) {
		session_saves |= SESSION_SAVE_CNF;
		cnf_failed = !cfgSave(CFG_SAVE_CNF, -2, NULL);
	}
	if (net_dirty || cfg_failed[CFG_SAVE_NET]) {
		session_saves |= SESSION_SAVE_NET;
		net_failed = !cfgSave(CFG_SAVE_NET, -2, NULL);
	}
	if (!cnf_failed && !net_failed)
		sessionSnapshot();
}

static void settingsSaveAction(void)
{
	if (!anyDirty() && !failedSaves() && !softwareClockCheckpointAvailable())
		dashToast(DASH_TOAST_INFO, "Nothing to save");
	else
		settingsCommit();
}

static int settingsSaveSoftwareClock(const char *timestamp)
{
	if (!dashModalConfirm("Save clock and settings?",
	                      "Save this time and your current Settings changes before starting the software clock?",
	                      localizeDashboardText("Save"), 0))
		return 0;
	dashStrCopy(dashCfg.software_clock, timestamp, sizeof(dashCfg.software_clock));
	menuTitleUpdateAsync(1);
	settingsCommit();
	if (!dashStoreFlushWait("Saving clock") || dashStoreSettingsPending() || dashStoreSettingsFailed()) {
		dashModalMessage(DI_WARNING, "Clock not saved",
		                 "The clock is running, but its time could not be saved. Keep the storage connected and retry Save changes before restarting.", NULL);
		return 0;
	}
	dashModalMessage(DI_CHECK, "Clock saved",
	                 "After restarting, the clock resumes this saved checkpoint. Use Save current clock to keep later runtime; time spent unplugged cannot be measured.", NULL);
	return 1;
}

/* Puts back what was there when SETTINGS was opened. */
static void settingsRevert(void)
{
	refreshDirty(1);
	if (classic_dirty) {
		int screen = setting->TV_mode != snap_classic.TV_mode || setting->screen_x != snap_classic.screen_x ||
		             setting->screen_y != snap_classic.screen_y;
		int lang = setting->language != snap_classic.language || strcmp(setting->lang_file, snap_classic.lang_file);
		int font = strcmp(setting->font_file, snap_classic.font_file);

		memcpy(setting, &snap_classic, sizeof(snap_classic));
		swapKeys = setting->swapKeys;
		if (screen)
			updateScreenMode();
		if ((lang || font) && dashWorkerAwaitAll("Finishing up")) {
			if (lang) {
				Set_Language(setting->language);
				Load_External_Language();
			}
			if (font)
				loadFont(setting->font_file);
		}
	}
	if (dash_dirty) {
		int recent = dashCfg.recent_count != snap_dash.recent_count;
		int art = dashCfg.artwork != snap_dash.artwork;
		int theme = dashCfg.sound_theme != snap_dash.sound_theme;

		dashCfg = snap_dash;
		menuTitleClockRestore();
		dashThemeRestore(&snap_theme, snap_theme_preset, snap_theme_custom);
		DashThemeSetLegacyEnabled(1);
		if (dashCfg.sound || dashCfg.bgm)
			dashSoundInit();
		if (theme)
			dashSoundReloadTheme(); /* the effects in use are still the discarded theme's */
		dashSoundApplyVolume();
		if (dashCfg.bgm)
			dashBgmStart();
		else
			dashBgmStop();
		dashBgmSetVolume();
		if (art && !dashCfg.artwork)
			dashArtFlushAll();
		if (recent)
			dashHomeRebuild();
		dashAppsRebuildView();
	}
	if (net_dirty) {
		dashStrCopy(ip, snap_ip, 16);
		dashStrCopy(netmask, snap_nm, 16);
		dashStrCopy(gw, snap_gw, 16);
		updateIpConfigRuntimeState();
	}
	dash_dirty = classic_dirty = net_dirty = 0;
	session_saves = 0;
	dashHomeRebuild();
}

/* Called before the dashboard leaves SETTINGS. 0 = stay. */
static int settingsLeave(void)
{
	char what[96];
	int n = 0, r;
	DashMenuItem items[] = {
	    {DI_SAVE, "Save changes", NULL, 0},
	    {DI_CLOSE, "Discard changes", NULL, 0},
	    {DI_BACK, "Keep editing", NULL, 0}};

	if (!session)
		return 1;
	if (!anyDirty()) {
		session = 0;
		return 1;
	}
	what[0] = '\0';
	if (dash_dirty || ((session_saves & SESSION_SAVE_DASH) && dashStoreSettingsFailed()))
		n += snprintf(what + n, sizeof(what) - n, "Dashboard");
	if (classic_dirty || ((session_saves & SESSION_SAVE_CNF) && cfg_failed[CFG_SAVE_CNF]))
		n += snprintf(what + n, sizeof(what) - n, "%sLaunchstation", n ? ", " : "");
	if (net_dirty || ((session_saves & SESSION_SAVE_NET) && cfg_failed[CFG_SAVE_NET]))
		n += snprintf(what + n, sizeof(what) - n, "%snetwork", n ? ", " : "");
	snprintf(what + n, sizeof(what) - n, " settings changed");
	r = dashModalMenu("Save your changes?", what, items, 3, 0);
	if (r == 0) {
		settingsCommit();
		if (anyDirty())
			return 0;
		session = 0;
		return 1;
	}
	if (r == 1) {
		settingsRevert();
		dashToast(DASH_TOAST_INFO, "Changes discarded");
		session = 0;
		return 1;
	}
	return 0;
}

/* "Save Launchstation settings to...": the classic save-location choice. */
typedef struct
{
	char o[MAX_PATH], c[MAX_PATH], s[MAX_PATH];
	int has;
	int finished, abandoned;
} TargetsJob;

static void targetsRun(void *arg)
{
	TargetsJob *j = (TargetsJob *)arg;

	configBuildSaveTargets(j->o, sizeof(j->o), j->c, sizeof(j->c), j->s, sizeof(j->s),
	                       dashExecContext()->cnf_path, LoadedConfigPath, &j->has);
}

static void targetsDone(void *arg)
{
	TargetsJob *j = (TargetsJob *)arg;

	j->finished = 1;
	if (j->abandoned)
		free(j);
}

static void saveClassicAs(void)
{
	TargetsJob *j = (TargetsJob *)malloc(sizeof(TargetsJob));
	DashMenuItem items[3];
	int targets[3], n = 0, r, id;
	const char *paths[3];

	if (j == NULL)
		return;
	memset(j, 0, sizeof(*j));
	id = dashWorkerSubmit("Checking memory cards", DASH_JOB_VISIBLE, targetsRun, targetsDone, j);
	if (id < 0) {
		free(j);
		dashToast(DASH_TOAST_WARN, "Save locations could not be queued; try again");
		return;
	}
	if (!dashWorkerAwait(id, "Checking memory cards", 1)) {
		j->abandoned = 1;
		return;
	}
	if (j->has) {
		items[n].icon = DI_FOLDER, items[n].label = j->o, items[n].detail = "configured folder", items[n].disabled = 0;
		targets[n] = CONFIG_SAVE_TARGET_OVERRIDE, paths[n++] = j->o;
	}
	items[n].icon = DI_APP, items[n].label = j->c, items[n].detail = "next to the ELF", items[n].disabled = 0;
	targets[n] = CONFIG_SAVE_TARGET_CWD, paths[n++] = j->c;
	items[n].icon = DI_MC, items[n].label = j->s, items[n].detail = "memory card", items[n].disabled = 0;
	targets[n] = CONFIG_SAVE_TARGET_SYSCONF, paths[n++] = j->s;
	r = dashModalMenu("Save Launchstation settings to", LoadedConfigPath[0] ? LoadedConfigPath : "not loaded from a file yet", items, n, 0);
	if (r >= 0 && cfgSave(CFG_SAVE_CNF, targets[r], paths[r])) {
		/* the classic settings portion of the session is saved now */
		memcpy(&snap_classic, setting, sizeof(snap_classic));
		free(snap_cnf_text);
		snap_cnf_text = cnfSaveText();
		classic_dirty = 0;
	}
	free(j);
}

//--------------------------------------------------------------
// Actions
//--------------------------------------------------------------
static void editScanPaths(void)
{
	while (1) {
		DashMenuItem items[DASH_MAX_SCAN + 2];
		int n = 0, r, i;

		for (i = 0; i < dashCfg.nscan; i++) {
			items[n].icon = dashLibDeviceIcon(dashCfg.scan[i]);
			items[n].label = dashCfg.scan[i];
			items[n].detail = "this folder + one subfolder";
			items[n].disabled = 0;
			n++;
		}
		items[n].icon = DI_PLUS, items[n].label = "Add folder...", items[n].detail = "choose the exact folder to scan", items[n].disabled = (dashCfg.nscan >= DASH_MAX_SCAN), n++;
		items[n].icon = DI_REFRESH, items[n].label = "Restore defaults", items[n].detail = "mc:/APPS, mc:/BOOT, mass:/APPS, mass:/BOOT", items[n].disabled = 0, n++;
		r = dashModalMenu("Application folders", "Only listed folders are scanned for .ELF files", items, n, 0);
		if (r < 0)
			break;
		if (r < dashCfg.nscan) {
			char body[DASH_PATH_MAX + 48];
			snprintf(body, sizeof(body), "Stop looking for applications in %s?", dashCfg.scan[r]);
			if (dashModalConfirm("Remove folder", body, localizeDashboardText("Remove"), 0)) {
				for (i = r; i < dashCfg.nscan - 1; i++)
					memcpy(dashCfg.scan[i], dashCfg.scan[i + 1], DASH_PATH_MAX);
				dashCfg.nscan--;
				dash_dirty = 1;
			}
		} else if (r == dashCfg.nscan) {
			char out[MAX_PATH];
			out[0] = '\0';
			if (dashPickPath("Add an application folder", "", DASH_PICK_FOLDER, NULL, out, sizeof(out)) &&
			    out[0] && strlen(out) < DASH_PATH_MAX - 2) {
				int len = strlen(out);
				if (out[len - 1] != '/')
					strcat(out, "/");
				dashStrCopy(dashCfg.scan[dashCfg.nscan++], out, DASH_PATH_MAX);
				dash_dirty = 1;
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
			dash_dirty = 1;
		}
	}
}

static void resetScreen(void)
{
	if (!dashModalConfirm(localizeDashboardText("Reset screen settings"), "Video mode Auto and a centred picture?", localizeDashboardText("Reset"), 0))
		return;
	setting->color[COLOR_BACKGR] = DEF_COLOR1;
	setting->color[COLOR_FRAME] = DEF_COLOR2;
	setting->color[COLOR_SELECT] = DEF_COLOR3;
	setting->color[COLOR_TEXT] = DEF_COLOR4;
	setting->color[COLOR_GRAPH1] = DEF_COLOR5;
	setting->color[COLOR_GRAPH2] = DEF_COLOR6;
	setting->color[COLOR_GRAPH3] = DEF_COLOR7;
	setting->color[COLOR_GRAPH4] = DEF_COLOR8;
	setting->TV_mode = TV_mode_AUTO;
	setting->screen_x = 0;
	setting->screen_y = 0;
	setting->Menu_Frame = DEF_MENU_FRAME;
	setting->Popup_Opaque = DEF_POPUP_OPAQUE;
	updateScreenMode();
	classic_dirty = 1;
}

static void resetDisplayThemeDefaults(void)
{
	if (!dashModalConfirm("Reset display & theme defaults", "Restore the screen, motion, animation and OSD Classic theme defaults?", localizeDashboardText("Reset"), 0))
		return;
	setting->color[COLOR_BACKGR] = DEF_COLOR1;
	setting->color[COLOR_FRAME] = DEF_COLOR2;
	setting->color[COLOR_SELECT] = DEF_COLOR3;
	setting->color[COLOR_TEXT] = DEF_COLOR4;
	setting->color[COLOR_GRAPH1] = DEF_COLOR5;
	setting->color[COLOR_GRAPH2] = DEF_COLOR6;
	setting->color[COLOR_GRAPH3] = DEF_COLOR7;
	setting->color[COLOR_GRAPH4] = DEF_COLOR8;
	setting->TV_mode = TV_mode_AUTO;
	setting->screen_x = 0;
	setting->screen_y = 0;
	setting->Menu_Frame = DEF_MENU_FRAME;
	setting->Popup_Opaque = DEF_POPUP_OPAQUE;
	updateScreenMode();
	dashCfg.effects = 100;
	dashCfg.anim = 100;
	dashThemeSetPreset(0);
	classic_dirty = 1;
	dash_dirty = 1;
	dashToast(DASH_TOAST_OK, "Display and theme defaults restored");
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
	dash_dirty = 1;
}

static int pickPng(char *out, int size, const char *what)
{
	char path[MAX_PATH], start[MAX_PATH], title[64];

	snprintf(title, sizeof(title), "Choose the %s", what);
	startDirOf(out[0] ? out : dashStoreDir(), start, sizeof(start));
	path[0] = '\0';
	if (!dashPickPath(title, start, DASH_PICK_FILE, "PNG", path, sizeof(path)))
		return 0;
	if (strlen(path) >= (size_t)size || strlen(path) >= DASH_PATH_MAX) {
		dashModalMessage(DI_WARNING, "Path too long", "Choose an image with a shorter path.", path);
		return 0;
	}
	dashStrCopy(out, path, size);
	return 1;
}

/* Theme folder IO runs on the background worker; the UI waits with a
 * cancellable overlay and applies the result. */
enum {
	TIO_LIST = 0,
	TIO_READ,
	TIO_WRITE
};

typedef struct
{
	int op;
	char folder[DASH_THEME_NAME_MAX];
	char names[24][DASH_THEME_NAME_MAX];
	int n;
	DashTheme t;
	char *text;
	int len;
	int ret;
	int abandoned;
} ThemeIoJob;

static void themeIoRun(void *arg)
{
	ThemeIoJob *j = (ThemeIoJob *)arg;

	if (j->op == TIO_LIST)
		j->n = dashThemeFolderList(j->names, 24);
	else if (j->op == TIO_READ)
		j->ret = dashThemeFolderRead(j->folder, &j->t);
	else
		j->ret = dashThemeFolderWrite(j->folder, j->text, j->len);
}

static void themeIoDone(void *arg)
{
	ThemeIoJob *j = (ThemeIoJob *)arg;

	if (j->abandoned) {
		free(j->text);
		free(j);
	}
}

/* 1 = finished (caller owns j), 0 = user backed out (job frees itself) */
static int themeIo(ThemeIoJob *j, const char *title)
{
	int id = dashWorkerSubmit(title, DASH_JOB_THEME | DASH_JOB_VISIBLE, themeIoRun, themeIoDone, j);

	if (id < 0) {
		free(j->text);
		free(j);
		dashToast(DASH_TOAST_WARN, "Theme operation could not be queued; try again");
		return 0;
	}

	if (dashWorkerAwait(id, title, 1))
		return 1;
	j->abandoned = 1;
	return 0;
}

static void themeSave(void)
{
	ThemeIoJob *j;
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
	if (!dashModalConfirm("Save theme", body, localizeDashboardText("Save"), 0))
		return;
	j = (ThemeIoJob *)malloc(sizeof(ThemeIoJob));
	if (j == NULL)
		return;
	memset(j, 0, sizeof(*j));
	j->op = TIO_WRITE;
	dashStrCopy(j->folder, name, sizeof(j->folder));
	j->text = dashThemeFolderText(&j->len);
	if (j->text == NULL) {
		free(j);
		return;
	}
	if (!themeIo(j, "Saving theme"))
		return;
	if (j->ret == 0)
		dashToast(DASH_TOAST_OK, "Theme saved");
	else
		dashModalMessage(DI_WARNING, "Theme not saved", "The theme folder could not be written. Check that the device is writable.", NULL);
	free(j->text);
	free(j);
}

static void themeLoad(void)
{
	ThemeIoJob *j = (ThemeIoJob *)malloc(sizeof(ThemeIoJob));
	DashMenuItem items[24];
	int i, r;
	char body[200];

	if (j == NULL)
		return;
	memset(j, 0, sizeof(*j));
	j->op = TIO_LIST;
	if (!themeIo(j, "Looking for themes"))
		return;
	if (j->n <= 0) {
		snprintf(body, sizeof(body), "Put each theme in its own folder with a theme.cfg inside %sTHEMES/. "
		                             "\"Export theme to folder\" creates one you can copy and edit.", dashStoreDir());
		dashModalMessage(DI_FOLDER, "No themes found", body, NULL);
		free(j);
		return;
	}
	for (i = 0; i < j->n; i++) {
		items[i].icon = DI_PALETTE;
		items[i].label = j->names[i];
		items[i].detail = NULL;
		items[i].disabled = 0;
	}
	r = dashModalMenu("Load theme", "THEMES folder", items, j->n, 0);
	if (r < 0) {
		free(j);
		return;
	}
	j->op = TIO_READ;
	dashStrCopy(j->folder, j->names[r], sizeof(j->folder));
	if (!themeIo(j, "Loading theme"))
		return;
	if (j->ret == 0) {
		dashTheme = j->t;
		dashThemeCommit();
		dash_dirty = 1;
		dashToast(DASH_TOAST_OK, "Theme loaded");
	} else
		dashModalMessage(DI_WARNING, "Theme not loaded", "theme.cfg is missing or could not be read.", j->folder);
	free(j);
}

static int themeSettingResettable(int id)
{
	return id == S_EFFECTS || id == S_ANIM || id == S_ARTWORK ||
	       (id >= S_T_BG && id <= S_T_WALLDIM);
}

static int resetThemeSetting(int id)
{
	DashTheme p;
	DashTheme *t = &dashTheme;

	if (!themeSettingResettable(id))
		return 0;
	if (id == S_EFFECTS) {
		dashCfg.effects = 100;
		dash_dirty = 1;
		return 1;
	}
	if (id == S_ANIM) {
		dashCfg.anim = 100;
		dash_dirty = 1;
		return 1;
	}
	if (id == S_ARTWORK) {
		dashCfg.artwork = 1;
		dash_dirty = 1;
		return 1;
	}
	dashThemeLoadPreset(dashThemeBasePreset(), &p);
	switch (id) {
		case S_T_BG: t->bg = p.bg; break;
		case S_T_BG1: t->bg1 = p.bg1; break;
		case S_T_BG2: t->bg2 = p.bg2; break;
		case S_T_BRIGHT: t->bg_bright = p.bg_bright; break;
		case S_T_SCAN: t->scanlines = p.scanlines; break;
		case S_T_IMAGE:
			dashStrCopy(t->image, p.image, sizeof(t->image));
			if (t->bg == DASH_BG_IMAGE)
				t->bg = p.bg;
			break;
		case S_T_TEXT: t->text = p.text; break;
		case S_T_TEXT2: t->text2 = p.text2; break;
		case S_T_TEXT3: t->text3 = p.text3; break;
		case S_T_SELTEXT: t->sel_text = p.sel_text; break;
		case S_T_ACCENT: t->accent = p.accent; break;
		case S_T_SELECT: t->select = p.select; break;
		case S_T_PANEL: t->panel = p.panel; break;
		case S_T_BORDER: t->border = p.border; break;
		case S_T_WARN: t->warn = p.warn; break;
		case S_T_HL: t->highlight = p.highlight; break;
		case S_T_CORNER: t->corner = p.corner; break;
		case S_T_TILE: t->tile_style = p.tile_style; break;
		case S_T_GLOW: t->glow = p.glow; break;
		case S_T_TABS: t->tabs = p.tabs; break;
		case S_T_CONSOLE: t->console_info = p.console_info; break;
		case S_T_PANEL_A: t->panel.a = p.panel.a; break;
		case S_T_FONT: t->font = p.font; break;
		case S_T_FSCALE: t->font_scale = p.font_scale; break;
		case S_T_BOLD: t->bold = p.bold; break;
		case S_T_SHADOW: t->text_shadow = p.text_shadow; break;
		case S_T_ICONTINT: t->icon_tint = p.icon_tint; break;
		case S_T_APPICON: t->app_icon_style = p.app_icon_style; break;
		case S_T_APPICON_IMG: dashStrCopy(t->app_icon, p.app_icon, sizeof(t->app_icon)); break;
		case S_T_CLOCK: t->clock = p.clock; break;
		case S_T_CLOCKPOS: t->clock_pos = p.clock_pos; break;
		case S_T_HOURS: t->clock_hours = p.clock_hours; break;
		case S_T_SECONDS: t->clock_seconds = p.clock_seconds; break;
		case S_T_DATE: t->clock_date = p.clock_date; break;
		case S_T_WALL: t->wallpaper = p.wallpaper; break;
		case S_T_WALLDIM: t->wall_dim = p.wall_dim; break;
		default: return 0;
	}
	dashThemeCommit();
	dash_dirty = 1;
	return 1;
}

static void runThemeAction(int id)
{
	switch (id) {
		case S_T_CUSTOMIZE:
			openPage(P_THEME);
			break;
		case S_T_LOAD:
			themeLoad();
			break;
		case S_T_SAVE:
			themeSave();
			break;
		case S_T_DEFAULT:
			if (dashModalConfirm(localizeDashboardText("Restore default theme"), "Replace the current theme with OSD Classic? Saved theme folders are not affected.", "Restore", 0)) {
				dashThemeSetPreset(0);
				dashCfg.effects = 100;
				dashCfg.anim = 100;
				dash_dirty = 1;
				dashToast(DASH_TOAST_OK, "Default theme restored");
			}
			break;
		case S_T_RESTORE_PRESET: {
			char body[160];
			int p = dashThemeBasePreset();
			snprintf(body, sizeof(body), "Undo every change and return to \"%s\"?", dashThemePresetInfo(p)->name);
			if (dashModalConfirm(localizeDashboardText("Reset entire theme"), body, localizeDashboardText("Reset"), 0)) {
				dashThemeSetPreset(p);
				dashCfg.effects = 100;
				dashCfg.anim = 100;
				dash_dirty = 1;
			}
			break;
		}
		case S_T_RESETCAT:
			if (page >= P_THEME_BG && page <= P_THEME_WALL) {
				dashThemeResetGroup(page - P_THEME_BG);
				dash_dirty = 1;
				dashToast(DASH_TOAST_OK, "Reset to the preset's values");
			}
			break;
		case S_T_IMAGE:
			if (pickPng(dashTheme.image, sizeof(dashTheme.image), "wallpaper image")) {
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
			if (dashModalKeyboard("Theme name", name, DASH_THEME_NAME_MAX - 1) && name[0]) {
				dashStrCopy(dashTheme.name, name, sizeof(dashTheme.name));
				themeChanged();
			}
			break;
		}
	}
}

/* Date and time as six boxed fields, like the IP editor: Left/Right picks a
 * field, Up/Down changes it by one, L1/R1 by ten, wrapping inside its range.
 * The day is kept valid for the month and year. 1 = Confirm, with the result
 * in fields[] (year, month, day, hour, minute, second); 0 = Back. */
static int editClockDateTime(const char *title, int fields[6])
{
	static const char *const labels[] = {"Year", "Month", "Day", "Hour", "Minute", "Second"};
	static const int low[] = {2000, 1, 1, 0, 0, 0};
	int high[] = {2099, 12, 31, 23, 59, 59};
	int value[6], sel = 0, i, accepted = 0;
	float open = 0.0f, sel_anim = 0.0f;
	const float w = 528.0f, h = 268.0f, bw = 64.0f, gap = 16.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	const float bx0 = x + (w - (6 * bw + 5 * gap)) * 0.5f, by = y + 120.0f;
	DashInput in;

	for (i = 0; i < 6; i++) {
		if (i == 2)
			high[2] = dashClockDaysInMonth(value[0], value[1]);
		value[i] = fields[i] < low[i] ? low[i] : fields[i] > high[i] ? high[i] : fields[i];
	}
	dashInputFlush();
	while (1) {
		int step = 0;
		float dt, oe;

		dashInputPoll(&in);
		if (in.pressed & (DASH_IN_BACK | DASH_IN_MENU))
			break;
		if (in.pressed & DASH_IN_OK) {
			accepted = 1;
			break;
		}
		if (in.pressed & (DASH_IN_LEFT | DASH_IN_RIGHT)) {
			int next = (sel + ((in.pressed & DASH_IN_LEFT) ? 5 : 1)) % 6;

			if ((sel == 0 && next == 5) || (sel == 5 && next == 0))
				sel_anim = (float)next;  //wrapping jumps instead of sliding across
			sel = next;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_UP)
			step = 1;
		if (in.pressed & DASH_IN_DOWN)
			step = -1;
		if (in.pressed & (DASH_IN_R1 | DASH_IN_R2))
			step = 10;
		if (in.pressed & (DASH_IN_L1 | DASH_IN_L2))
			step = -10;
		if (step) {
			int range = high[sel] - low[sel] + 1;

			value[sel] = low[sel] + ((value[sel] - low[sel] + step) % range + range) % range;
			high[2] = dashClockDaysInMonth(value[0], value[1]);
			if (value[2] > high[2])
				value[2] = high[2];
			dashSoundPlay(DASH_SND_CURSOR);
		}
		dt = dashVisualUpdate();
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), dt);
		oe = dashEaseOutCubic(open);
		dashRenderScene();
		dashModalBackdrop(oe);
		dashGfxPushAlpha(oe);
		dashGfxPushOffset(0, 18.0f * (1.0f - oe));
		dashModalPanel(x, y, w, h, oe);
		dashIcon(DI_CLOCK, x + 24, y + 18, 22, DASH_C_ACCENT);
		dashTextS(x + 56, y + 18, title, DASH_FS_LABEL, DASH_C_TEXT);
		dashTextFit(x + 24, y + 48, w - 48, "Local date and time (24-hour clock, 2000-2099)", DASH_FS_CAPTION, DASH_C_TEXT3);
		dashDrawSelection(bx0 + sel_anim * (bw + gap), by, bw, 44, 10, 1.0f);
		for (i = 0; i < 6; i++) {
			char num[8];
			float bx = bx0 + i * (bw + gap);

			dashTextCenter(bx + bw * 0.5f, by - 46, labels[i], DASH_FS_CAPTION, DASH_C_TEXT3);
			if (i != sel)
				dashPanel(bx, by, bw, 44, 10, DC(255, 255, 255, 16));
			snprintf(num, sizeof(num), i == 0 ? "%04d" : "%02d", value[i]);
			dashTextCenter(bx + bw * 0.5f, by + 10, num, DASH_FS_LABEL, dashItemColor(i == sel));
			if (i < 5 && i != 2)
				dashTextCenter(bx + bw + gap * 0.5f, by + 10, i < 2 ? "-" : ":", DASH_FS_LABEL, DASH_C_TEXT2);
			if (i == sel) {
				dashTextCenter(bx + bw * 0.5f, by - 20, "+", DASH_FS_BODY, DASH_C_ACCENT);
				dashTextCenter(bx + bw * 0.5f, by + 48, "-", DASH_FS_BODY, DASH_C_ACCENT);
			}
		}
		{
			float hx = x + 24, hy = y + h - 68;

			hx += dashDrawButton(hx, hy, DI_BTN_LEFT, NULL) + 12;
			hx += dashDrawButton(hx, hy, DI_BTN_RIGHT, NULL) + 4;
			hx = dashTextS(hx, hy + 4.5f, "Select field", DASH_FS_SMALL, DASH_C_TEXT2) + 20;
			hx += dashDrawButton(hx, hy, DI_BTN_UP, NULL) + 12;
			hx += dashDrawButton(hx, hy, DI_BTN_DOWN, NULL) + 4;
			dashTextS(hx, hy + 4.5f, "Change", DASH_FS_SMALL, DASH_C_TEXT2);
			hx = x + 24, hy = y + h - 36;
			hx += dashDrawButton(hx, hy, -1, "L1") + 2;
			hx += dashDrawButton(hx, hy, -1, "R1") + 4;
			hx = dashTextS(hx, hy + 4.5f, "10", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
			dashModalHintRow(hx, hy, dashOkIcon(), "Apply", dashBackIcon(), "Cancel");
		}
		dashGfxPopOffset();
		dashGfxPopAlpha();
		dashGfxPresent();
	}
	if (accepted)
		memcpy(fields, value, sizeof(value));
	dashSoundPlay(accepted ? DASH_SND_CONFIRM : DASH_SND_CANCEL);
	dashInputFlush();
	return accepted;
}

static void runAction(int id)
{
	if (id >= S_T_PRESET && id <= S_T_RESETCAT) {
		runThemeAction(id);
		return;
	}
	switch (id) {
		case S_SCREEN_RESET:
			resetScreen();
			break;
		case S_DISPLAY_THEME_RESET:
			resetDisplayThemeDefaults();
			break;
		case S_SCANPATHS:
			editScanPaths();
			break;
		case S_RESCAN:
			dashRescanLibrary(1);
			break;
		case S_CLEAR_RECENT:
			if (dashModalConfirm(localizeDashboardText("Clear recent applications"), "Remove every entry from the recent list? Applications are not affected.", localizeDashboardText("Clear"), 0)) {
				dashStoreClearRecent();
				dashHomeRebuild();
				dashToast(DASH_TOAST_OK, "Recent list cleared");
			}
			break;
		case S_INTRO:
			dashCfg.intro_done = 0;
			dash_dirty = 1;
			dashToast(DASH_TOAST_INFO, "The welcome message will be shown next time");
			break;
		case S_LAUNCHKEYS:
			openPage(P_LKEY);
			break;
		case S_MMCE_SYNC_ALL:
			dashToolsSyncAllMmceCards();
			dashToolsRefreshMmceStatus();
			break;
		case S_MMCE_RECOVER_LOCAL: {
			char history[MAX_PATH], snapshot[MAX_PATH];
			const char *parent = dashCfg.igr_backup_path;
			size_t length = strlen(parent);
			int n;

			if (length == 0) {
				dashModalMessage(DI_INFO, "Set a local backup folder first",
				                 "Choose Backup folder on this page to locate WLE-MMCE-Backups history.",
				                 "Existing history is not moved.");
				break;
			}
			n = snprintf(history, sizeof(history), "%s%sWLE-MMCE-Backups",
			             parent, (parent[length - 1] == '/' ||
			                      parent[length - 1] == ':') ? "" : "/");
			if (n <= 0 || n >= (int)sizeof(history)) {
				dashModalMessage(DI_WARNING, "Backup path is too long",
				                 "Choose a shorter local backup folder.", NULL);
				break;
			}
			snapshot[0] = '\0';
			if (!dashPickPath("Select local .mcd or .ps2 snapshot",
			                  history, DASH_PICK_FILE, NULL,
			                  snapshot, sizeof(snapshot)))
				break;
			dashBrowserRecoverCardPath(snapshot);
			break;
		}
		case S_IGR_HELPER:
			dashToolsIgrBackupSetup();
			dashToolsRefreshMmceStatus();
			break;
		case S_MMCE_LANGUAGE:
			openPage(P_LANGUAGE);
			break;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		case S_GOOGLE_ACCOUNT:
			dashToolsCloudBackup();
			dashToolsRefreshMmceStatus();
			break;
		case S_CHECK_UPDATES:
			dashToolsCheckUpdates();
			break;
#endif
		case S_ABOUT_PROJECT:
			dashShowAbout();
			break;
#ifdef ETH
		case S_SMBD_USER:
		case S_SMBD_PASS: {
			char *v = id == S_SMBD_USER ? setting->SMB_Server_User : setting->SMB_Server_Pass, edit[32];
			dashStrCopy(edit, v, sizeof(edit));
			if (dashModalKeyboard(id == S_SMBD_USER ? "SMB server user name" : "SMB server password", edit,
			                      sizeof(edit) - 1) && edit[0] && strcmp(edit, v)) {
				strcpy(v, edit);
				classic_dirty = 1;
			}
			break;
		}
#endif
#ifdef SMB
		case S_SMBED_NAME:
			smbedEditField("Files label (e.g. PS2 Games)", smbed.name, sizeof(smbed.name),
			               "At most 31 characters, without / \\ : * ? \" < > |.");
			break;
		case S_SMBED_IP:
			smbedEditField("Server IPv4 (e.g. 192.168.1.20)", smbed.host, sizeof(smbed.host),
			               "At most 79 characters. A server named by words instead of an address is edited as text.");
			break;
		case S_SMBED_PORT: {
			char buf[8];
			int def = smbed.kind == SMBED_NFS ? 2049 : 445;
			snprintf(buf, sizeof(buf), "%d", smbed.port > 0 ? smbed.port : def);
			if (dashModalKeyboard(smbed.kind == SMBED_NFS ? "TCP port (2049 default)" : "TCP port (445 default; e.g. 1445 or 1025)",
			                      buf, sizeof(buf))) {
				char *end;
				long p = strtol(buf, &end, 10);
				if (end == buf || *end != '\0' || p < 0 || p > 65535)
					dashModalMessage(DI_WARNING, "Port not changed",
					                 smbed.kind == SMBED_NFS ? "Use digits only: 0 for the default 2049, or 1-65535." :
					                 "Use digits only: 0 for the default 445, or 1-65535.", NULL);
				else
					smbed.port = p == def ? 0 : (int)p;
			}
			break;
		}
		case S_SMBED_FOLDER:
			smbedEditField(smbed.kind == SMBED_NFS ? "Export path (e.g. /srv/ps2)" : "Share on server (e.g. games)",
			               smbed.folder, sizeof(smbed.folder), "At most 127 characters.");
			break;
		case S_SMBED_USER:
			smbedEditField("Username (e.g. ps2 or guest)", smbed.user, sizeof(smbed.user),
			               "At most 31 characters.");
			break;
		case S_SMBED_PASS:
			smbedEditField("Password (blank for an open share)", smbed.pass, sizeof(smbed.pass),
			               "At most 31 characters.");
			break;
		case S_SMBED_UID:
		case S_SMBED_GID: {
			char *idv = id == S_SMBED_UID ? smbed.uid : smbed.gid;
			char buf[12];
			int k;
			dashStrCopy(buf, idv, sizeof(buf));
			if (dashModalKeyboard(id == S_SMBED_UID ? "NFS user ID (blank for 0)" : "NFS group ID (blank for 0)", buf, sizeof(buf))) {
				for (k = 0; buf[k] >= '0' && buf[k] <= '9'; k++)
					;
				if (buf[k] != '\0' || k > 9)
					dashModalMessage(DI_WARNING, "Not changed", "Use digits only (at most 9), or leave it blank.", NULL);
				else
					dashStrCopy(idv, buf, sizeof(smbed.uid));
			}
			break;
		}
		case S_SMBED_SAVE:
			smbedSave();
			break;
		case S_SMBED_DELETE:
			smbedDelete();
			break;
		case S_SMBED_TEXT: {
			static const char smb_template[] =
			    "# One share per block; the URL line ends a block. Lines starting with #\n"
			    "# are ignored -- remove the # from a block's lines to use that block.\n"
			    "#\n"
			    "# NAME  the label shown for SMB2/3 (smb:/NAME). SMB1 opens via\n"
			    "# smb1:/ without a label.\n"
			    "# USERNAME / PASSWORD  exactly as the server lists them (31 characters\n"
			    "#   each). PASSWORD = with nothing after it is a blank password.\n"
			    "# VERSION  optional: 1 = an SMB1-only server (opens via smb1:/);\n"
			    "#   2 or 3 = the smb: driver. Use 3 to mark a server that supports SMB3.\n"
			    "# URL  smb://<IP address>:<port>/<share>. Give the port (445 for most\n"
			    "#   servers) here only, never on the NAME line.\n"
			    "#\n"
			    "# PS2-Servers: its SMBv2/SMBv3 card shows IP, Port, Share, User and\n"
			    "# Password -- copy them into a block like these. The card's default\n"
			    "# port is 1445 (445 when its Take port 445 option is on).\n"
			    "# SMBv2 card with a user and password:\n"
			    "# NAME = PC\n"
			    "# USERNAME = player\n"
			    "# PASSWORD = secret\n"
			    "# URL = smb://192.168.1.100:1445/games\n"
			    "#\n"
			    "# SMBv2 card with No password enabled (default User, blank Password):\n"
			    "# NAME = PC\n"
			    "# USERNAME = ps2\n"
			    "# PASSWORD =\n"
			    "# URL = smb://192.168.1.100:1445/games\n"
			    "#\n"
			    "# SMBv3 card (default 1445; 445 with Take port 445); VERSION = 3:\n"
			    "# NAME = NGCARD\n"
			    "# VERSION = 3\n"
			    "# USERNAME = player\n"
			    "# PASSWORD = secret\n"
			    "# URL = smb://192.168.1.100:445/games\n"
			    "#\n"
			    "# PS2-Servers on Linux/macOS (use the port shown by its card):\n"
			    "# NAME = LINUX\n"
			    "# USERNAME = player\n"
			    "# PASSWORD = secret\n"
			    "# URL = smb://192.168.1.100:1445/games\n"
			    "#\n"
			    "# An SMB1-only server (PS2-Servers' SMBv1 card), opened via smb1:/:\n"
			    "# NAME = OLDBOX\n"
			    "# VERSION = 1\n"
			    "# URL = smb://192.168.0.30:445/PS2SMB\n"
			    "#\n"
			    "# An NFS export (NFSv4.0), opened via nfs:/NAME. Only NAME and URL;\n"
			    "# ?uid=N&gid=N is optional (0 when left out). The port is 2049:\n"
			    "# NAME = LINUX\n"
			    "# URL = nfs://192.168.1.100:2049/srv/ps2?uid=1000&gid=1000\n";
			char *text = NULL;
			if (smbSharesBlocks() == NULL) {
				/* An old SMB2.CNF's shares (or the examples) come across; SMB2.CNF is
				 * never written. One that exists but cannot be read stays untouched. */
				smbSharesConfigured(SMB_PATH_V2); /* reads the file; no network */
				if (smbSharesConfigPath()[0] &&
				    (text = dashLoadTextFile(smbSharesConfigPath(), SMB_CNF_MAX_SIZE, NULL)) == NULL) {
					dashModalMessage(DI_WARNING, "SMB2.CNF not read", "Its shares were not copied. Try again.", NULL);
					break;
				}
				smbSharesSetBlocks(text != NULL ? text : smb_template);
				free(text);
			}
			/* The shares are the end of LAUNCHSTATION.CNF: save it, edit it, then
			 * take the edited blocks back. */
			if (!saveSharesCnf() || !LoadedConfigPath[0])
				break;
			dashTextEditor(LoadedConfigPath);
			if ((text = dashLoadTextFile(LoadedConfigPath, 128 * 1024, NULL)) != NULL) {
				smbSharesSetSection(text);
				free(text);
			}
			if (!smbSharesReload())
				dashModalMessage(DI_WARNING, "Restart required",
				                 "The old SMB connections could not be closed. Restart Launchstation to use the edited configuration.", NULL);
			break;
		}
#endif
		case S_MISC_COMMANDS:
			dashOpenBrowser(setting->Misc);
			break;
		case S_CLOCK_HARDWARE:
			dashCfg.software_clock[0] = '\0';
			menuTitleUpdateAsync(1);
			break;
		case S_CLOCK_SOFTWARE:
		case S_CLOCK_MANUAL: {
			char input[24];
			char result[160];
			int f[6] = {2000, 1, 1, 0, 0, 0};
			int use12, datefmt;
			int ok;

			menuTitleGetClockFields(&f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &use12, &datefmt);
			if (!editClockDateTime(id == S_CLOCK_SOFTWARE ? "Set software clock" : "Set PS2 clock", f))
				break;
			snprintf(input, sizeof(input), "%04d-%02d-%02d %02d:%02d:%02d", f[0], f[1], f[2], f[3], f[4], f[5]);
			if (id == S_CLOCK_SOFTWARE) {
				settingsSaveSoftwareClock(input);
				break;
			}
			ok = dashClockSetManual(f[0], f[1], f[2], f[3], f[4], f[5], result, sizeof(result));
			dashModalMessage(ok ? DI_CHECK : DI_WARNING,
			                 ok ? "PS2 clock updated" : "Clock update failed",
			                 result, input);
			break;
		}
#ifdef ETH
		case S_CLOCK_SYNC: {
			char result[160];
			int ok;
			dashLoadingBegin("Synchronizing PS2 clock");
			ok = dashClockSyncInternet(result, sizeof(result));
			dashLoadingEnd();
			dashModalMessage(ok ? DI_CHECK : DI_WARNING,
			                 ok ? "PS2 clock synchronized" : "Clock synchronization failed",
			                 result[0] ? result : (ok ? "The hardware clock was updated." : "No reliable Internet time was available."), NULL);
			break;
		}
#endif
		case S_CLOCK: {
			/* One System row for every clock action; each still runs its own case. */
			static const int clock_actions[] = {
			    S_CLOCK_MANUAL,
#ifdef ETH
			    S_CLOCK_SYNC,
#endif
			    S_CLOCK_SOFTWARE,
			    S_CLOCK_HARDWARE,
			};
			DashMenuItem clock_items[] = {
			    {DI_CLOCK, localizeDashboardText("Set manually..."), NULL, 0},
#ifdef ETH
			    {DI_CLOCK, localizeDashboardText("Sync from Internet"), NULL, 0},
#endif
			    {DI_CLOCK, localizeDashboardText("Set software clock..."), NULL, 0},
			    {DI_CLOCK, localizeDashboardText("Use PS2 hardware clock"), NULL, 0},
			};
			int choice = dashModalMenu(localizeDashboardText("Set PS2 Clock"), NULL, clock_items, NITEMS(clock_items), 0);

			if (choice >= 0)
				runAction(clock_actions[choice]);
			break;
		}
		case S_SAVE_CLASSIC:
			settingsSaveAction();
			break;
		case S_SAVE_AS:
			saveClassicAs();
			break;
		case S_REVERT:
			if (!anyDirty())
				dashToast(DASH_TOAST_INFO, "No unsaved changes");
			else if (dashModalConfirm("Discard unsaved changes", "Undo edits since opening Settings or last choosing Save?", "Discard", 1)) {
				settingsRevert();
				dashToast(DASH_TOAST_INFO, "Changes discarded");
			}
			break;
	}
}

//--------------------------------------------------------------
static const SetItem *curItem(void)
{
	return &pages[page].items[item];
}

/* Start screen "Launch keys": called right after settingsEnter() at boot. */
void dashSettingsOpenLaunchKeys(void)
{
	if (page != P_LKEY)
		openPage(P_LKEY);
}

static void settingsEnter(void)
{
	/* A new Settings visit starts at its overview; modal dialogs keep the
	 * current page and the same session. */
	if (!session) {
		page = P_ROOT;
		page_depth = item = item_top = 0;
		item_anim = 0.0f;
	}
	/* The snapshot must see the saved IP settings, not the compile-time
	 * defaults the globals hold until the boot warm job runs: a defaults
	 * snapshot would later read as dirty and Discard would lose the file. */
	dashEnsureIpConfig();
	sessionBegin();
}

static int itemDisabled(const SetItem *it)
{
	if (it->type == ST_PAGE)
		return 0;
	switch (it->id) {
		case S_SVOL:
		case S_STHEME: return !dashCfg.sound;
		case S_BGMVOL: return !dashCfg.bgm;
		case S_USBKBD_FILE:
		case S_KBDMAP_FILE: return !setting->usbkbd_used;
		case S_FONT_FILE: return dashTheme.font != DASH_FONT_SYSTEM;
		case S_T_WALL: return !dashCfg.artwork;
		case S_T_WALLDIM: return !dashCfg.artwork || !dashTheme.wallpaper;
		case S_T_CLOCKPOS:
		case S_T_HOURS:
		case S_T_SECONDS:
		case S_T_DATE: return !dashTheme.clock;
		case S_TIMEOUT: return !setting->LK_Path[SETTING_LK_AUTO][0];
		case S_IGR_AFTER_APP: return dashCfg.igr_after != DASH_IGR_AFTER_APP;
#ifdef ILINK
		case S_DEVA_ILINK: return !consoleHasIlink();
#endif
#ifdef SMB
		case S_SMBED_USER:
		case S_SMBED_PASS: return smbed.kind == SMBED_NFS;
		case S_SMBED_UID:
		case S_SMBED_GID: return smbed.kind != SMBED_NFS;
#endif
		default: return 0;
	}
}

static const char *disabledHelp(const SetItem *it)
{
	switch (it->id) {
		case S_SVOL: return "Enable Interface sounds to adjust their volume.";
		case S_STHEME: return "Enable Interface sounds to choose a sound theme.";
		case S_BGMVOL: return "Enable Background music to adjust its volume.";
		case S_USBKBD_FILE:
		case S_KBDMAP_FILE: return "Enable USB keyboard to configure its driver and key map.";
		case S_FONT_FILE: return "Choose the System font above to use an external font file.";
		case S_T_WALL: return "Enable Application artwork in Settings > Home & applications first.";
		case S_T_WALLDIM: return "Enable Application artwork and choose a focused-app background first.";
		case S_T_CLOCKPOS:
		case S_T_HOURS:
		case S_T_SECONDS:
		case S_T_DATE: return "Enable the clock to configure its placement and format.";
		case S_TIMEOUT: return "Choose an automatic start target before setting its timeout.";
		case S_IGR_AFTER_APP: return "Set 'After IGR, open' to Application first.";
#ifdef SMB
		case S_SMBED_USER:
		case S_SMBED_PASS: return "An NFS share has no user name or password: set its user and group IDs.";
		case S_SMBED_UID:
		case S_SMBED_GID: return "Only an NFS share has user and group IDs: add it in Settings > Network > NFS shares.";
#endif
#ifdef ILINK
		case S_DEVA_ILINK:
			return console_is_PSX ? "The PSX's i.Link port only works with Sony camcorders, not with storage."
			                      : "This console has no i.Link port.";
#endif
		default: return localizeDashboardText(it->help);
	}
}

static void settingsMenu(void)
{
	int changed = anyDirty();
	int failed = failedSaves();
	int clock_save = softwareClockCheckpointAvailable();
	DashMenuItem menu[] = {
	    {DI_SAVE, !changed && failed ? "Retry failed saves" : !changed && clock_save ? "Save current clock" : "Save changes", "Save settings and current software time", !(changed || failed || clock_save)},
	    {DI_CLOSE, "Discard changes", "Undo edits since opening Settings or choosing Save", !changed},
	    {DI_BACK, "Keep editing", NULL, 0}};
	int r = dashModalMenu("Settings changes", changed ? "Changes preview immediately" : failed ? "An earlier save failed; retry is optional" : clock_save ? "Save the current clock before restarting" : "No unsaved changes", menu, NITEMS(menu), (changed || failed || clock_save) ? 0 : 2);
	if (r == 0)
		runAction(S_SAVE_CLASSIC);
	else if (r == 1)
		runAction(S_REVERT);
}

static void editPathMenu(const SetItem *it)
{
	PathSpec ps;
	int r;
	DashMenuItem menu[] = {
	    {DI_FOLDER, "Choose a location...", NULL, 0},
	    {DI_CLOSE, "Clear location", "Use the default behavior", 0}};
	if (!pathInfo(it->id, &ps))
		return;
	menu[1].disabled = !ps.field[0];
	r = dashModalMenu(localizeDashboardText(it->label), ps.field[0] ? ps.field : localizeDashboardText("Default"), menu, NITEMS(menu), 0);
	if (r == 0)
		editPath(it);
	else if (r == 1)
		clearPath(it);
}

static void editLaunchMenu(const SetItem *it)
{
	int lk = it->id - S_LK_0, r;
	DashMenuItem menu[] = {
	    {DI_FOLDER, "Choose target...", "An application or a built-in command", 0},
	    {DI_TEXT, "Edit title...", NULL, 0},
	    {DI_CLOSE, "Clear target", NULL, 0}};
	if (lkeyLocked())
		return;
	menu[2].disabled = !setting->LK_Path[lk][0];
	r = dashModalMenu(localizeDashboardText(it->label), setting->LK_Path[lk][0] ? setting->LK_Path[lk] : localizeDashboardText("Not set"), menu, NITEMS(menu), 0);
	if (r >= 0)
		editLaunchKey(it, r);
}

static void settingsInput(const DashInput *in)
{
	const SetItem *it;
	int total;

	if (in->pressed & DASH_IN_MENU) {
		settingsMenu();
		return;
	}
	it = curItem();
	total = pages[page].n;
	if (in->pressed & DASH_IN_UP) {
		if (total > 0) {
			int next = item > 0 ? item - 1 : total - 1;
			if (next != item)
				dashUiFocusPoke();
			item = next;
		}
	} else if (in->pressed & DASH_IN_DOWN) {
		if (total > 0) {
			int next = item < total - 1 ? item + 1 : 0;
			if (next != item)
				dashUiFocusPoke();
			item = next;
		}
	} else if (in->pressed & DASH_IN_L2) {
		if (item != 0) {
			dashUiFocusPoke();
			item = 0;
		}
	} else if (in->pressed & DASH_IN_R2) {
		if (item != pages[page].n - 1) {
			dashUiFocusPoke();
			item = pages[page].n - 1;
		}
	} else if (in->pressed & DASH_IN_BACK) {
		if (page_depth) {
			dashSoundPlay(DASH_SND_CANCEL);
			backPage();
		} else
			dashSwitchSection(dashBackSection());
	} else if (it->type != ST_PAGE && themeSettingResettable(it->id) && (in->pressed & DASH_IN_FAV)) {
		if (resetThemeSetting(it->id))
			dashToast(DASH_TOAST_OK, "Setting reset to preset default");
	} else if ((in->pressed & DASH_IN_LEFT) && it->type != ST_CHOICE && it->type != ST_NUMBER) {
		backPage();
	} else if (itemDisabled(it)) {
		if (in->pressed & (DASH_IN_OK | DASH_IN_LEFT | DASH_IN_RIGHT | DASH_IN_FAV))
			dashToast(DASH_TOAST_INFO, disabledHelp(it));
	} else if (it->type == ST_PAGE && (in->pressed & (DASH_IN_OK | DASH_IN_RIGHT))) {
#ifdef SMB
		if (it->id == P_SMB || it->id == P_NFS) {
			if (smbed.nfs != (it->id == P_NFS)) {
				smbed.nfs = it->id == P_NFS;
				smbed.pick = 0; /* a pick from the other page */
			}
			smbedLoadPick(); /* fresh snapshot and draft every time the page opens */
		}
#endif
		openPage(it->id);
	} else if (it->type == ST_COLOR && (in->pressed & DASH_IN_OK)) {
		colorEditor(localizeDashboardText(it->label), it->id);
	} else if (it->type == ST_CHOICE && it->id == S_T_PRESET && (in->pressed & DASH_IN_OK)) {
		choosePreset();
	} else if (it->type == ST_NUMBER && (in->pressed & (DASH_IN_LEFT | DASH_IN_RIGHT))) {
		int v, lo, hi;
		char text[24];
		if (numberInfo(it->id, &v, &lo, &hi, text, sizeof(text))) {
			v += (in->pressed & DASH_IN_LEFT) ? -1 : 1;
			if (v >= lo && v <= hi)
				setNumber(it->id, v);
		}
	} else if (it->type == ST_PATH && (in->pressed & DASH_IN_OK)) {
		editPathMenu(it);
	} else if (it->type == ST_IP && (in->pressed & DASH_IN_OK)) {
		editIp(it);
	} else if (it->type == ST_LKEY && (in->pressed & DASH_IN_OK)) {
		editLaunchMenu(it);
	} else if (it->type == ST_LKEY && (in->pressed & DASH_IN_FAV)) {
		editLaunchKey(it, 1);
	} else if (it->type == ST_CHOICE && (in->pressed & (DASH_IN_LEFT | DASH_IN_RIGHT | DASH_IN_OK))) {
		const char *const *names;
		const char *single;
		int cur, n = choiceInfo(it->id, &cur, &names, &single);
		if (n > 0) {
			if (in->pressed & (DASH_IN_LEFT | DASH_IN_RIGHT)) {
				int step = (in->pressed & DASH_IN_LEFT) ? n - 1 : 1;
				do {
					cur = (cur + step) % n;
				} while (it->id == S_TVMODE && cur == TV_mode_576P && !is576pSupported());
			} else if (it->id == S_TVMODE) {
				int r = chooseVideoMode(localizeDashboardText(it->label), names, n, cur);
				if (r < 0)
					return;
				cur = r;
			} else if (n > 2) {
				const char *display[128];
				int choice;
				for (choice = 0; choice < n && choice < 128; choice++)
					display[choice] = it->id == S_LANGUAGE || it->id == S_T_PRESET ? names[choice] : localizeDashboardText(names[choice]);
				int r = dashModalChoice(localizeDashboardText(it->label), n <= 128 ? display : names, n, cur);
				if (r < 0)
					return;
				cur = r;
			} else
				cur = (cur + 1) % n;
			setChoice(it->id, cur);
		}
	} else if (it->type == ST_ACTION && (in->pressed & DASH_IN_OK)) {
		runAction(it->id);
	}
}

static void settingsUpdate(float dt)
{
	float sp = dashAnimSpeed();

	item_anim = dashApproach(item_anim, (float)item, 16.0f * sp, dt);
}

static const char *actionStatus(int id)
{
	switch (id) {
		case S_MMCE_SYNC_ALL:
			return dashToolsMmceStatus();
		case S_IGR_HELPER:
			return dashToolsIgrStatus();
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		case S_GOOGLE_ACCOUNT:
			return dashToolsGoogleStatus();
		case S_CHECK_UPDATES:
			return "Check now";
#endif
#ifdef SMB
		case S_SMBED_PICK:
			return smbed.pick == 0 ? "New share" : smbed.orig;
		case S_SMBED_NAME:
			return smbed.name[0] ? smbed.name : "e.g. PS2 Games";
		case S_SMBED_KIND:
			return smbed.kind == SMBED_NFS ? "NFS" : smbed.kind == 1 ? "SMB1" : smbed.kind == 3 ? "SMB3" : "SMB2";
		case S_SMBED_IP:
			return smbed.host[0] ? smbed.host : "e.g. 192.168.1.20";
		case S_SMBED_PORT: {
			static char port[16];
			if (smbed.port > 0)
				snprintf(port, sizeof(port), "%d", smbed.port);
			else
				dashStrCopy(port, smbed.kind == SMBED_NFS ? "2049 (default)" : "445 (default)", sizeof(port));
			return port;
		}
		case S_SMBED_FOLDER:
			return smbed.folder[0] ? smbed.folder : smbed.kind == SMBED_NFS ? "e.g. /srv/ps2" : "e.g. games";
		case S_SMBED_UID:
			return smbed.uid[0] ? smbed.uid : "0 (default)";
		case S_SMBED_GID:
			return smbed.gid[0] ? smbed.gid : "0 (default)";
		case S_SMBED_USER:
			return smbed.user[0] ? smbed.user : "e.g. ps2 / guest";
#ifdef ETH
		case S_SMBD_USER:
			return setting->SMB_Server_User;
		case S_SMBD_PASS:
			return setting->SMB_Server_Pass;
#endif
		case S_SMBED_PASS:
			return smbed.pass[0] ? "set (hidden)" : "blank";
		case S_SMBED_SAVE:
			return smbed.pick == 0 ? "add a new share" : "replace this block";
		case S_SMBED_DELETE:
			return smbed.pick == 0 ? "pick a share first" : NULL;
#endif
		default:
			return NULL;
	}
}

static void settingsDraw(void)
{
	int i;
	/* Give the breadcrumb a real lane below the L1/R1 tab bar (it ends at
	 * y=62): ~7 px of clear space above the breadcrumb's capitals (#135). */
	/* Only the header lanes move; the list keeps ITEM_ROWS rows so its two-line
	 * help still clears the footer hints. */
	const float ix = DASH_SAFE_L, iy = 136, irow = 29, iw = DASH_SAFE_R - DASH_SAFE_L;
	const SetPage *c = &pages[page];
	char trail[192];

	trail[0] = '\0';
	for (i = 0; i < page_depth; i++) {
		int len = strlen(trail);
		snprintf(trail + len, sizeof(trail) - len, "%s%s", i ? " > " : "", localizeDashboardText(pages[page_stack[i].page].name));
	}
	if (page_depth)
		dashTextFit(ix, 66, iw - 126, trail, DASH_FS_CAPTION, DASH_C_TEXT3);
	dashDrawSectionTitle(ix, 82, localizeDashboardText(c->name), NULL);
	dashTextFit(ix, 112, iw, localizeDashboardText(c->help), DASH_FS_CAPTION, DASH_C_TEXT3);
	{
		int dirty = anyDirtyCached();
		int pending = cfg_pending[0] || cfg_pending[1] || dashStoreSettingsPending();
		int failed = failedSaves();
		const char *state = dirty ? "unsaved changes" : pending ? "saving..." : failed ? "save failed" : "no changes";
		DashColor state_col = (dirty || failed) ? DASH_C_WARM : pending ? DASH_C_ACCENT : DASH_C_TEXT3;
		/* Dedicated status lane under the header clock/date: never inherits selection text. */
		dashTextRight(ix + iw, 84, state, DASH_FS_CAPTION, state_col);
	}

	/* keep the focused row inside the visible window */
	item_top = listCenteredTop(item, c->n, ITEM_ROWS);
	if (item_anim < item_top || item_anim >= item_top + ITEM_ROWS)
		item_anim = (float)item;

	/* items */
	dashDrawPanelBox(DASH_PANE_L, iy - 6, DASH_PANE_W, ITEM_ROWS * irow + 12, 14);
	dashDrawSelection(ix, iy + dashClampf(item_anim - item_top, 0, ITEM_ROWS - 1) * irow + 2, iw, irow - 5, 9, 1.0f);
	for (i = item_top; i < c->n && i < item_top + ITEM_ROWS; i++) {
		const SetItem *it = &c->items[i];
		float y = iy + (i - item_top) * irow;
		int on = i == item, disabled = itemDisabled(it);
		DashColor col = disabled ? DASH_C_TEXT3 : dashItemColor(on);

		dashGfxPushAlpha(disabled ? DASH_DISABLED_ALPHA : 1.0f);
		{
			int row_icon = it->type == ST_PAGE ? pages[it->id].icon :
			               it->type == ST_PATH ? DI_FOLDER :
			               it->type == ST_IP ? DI_NETWORK :
			               it->type == ST_LKEY ? DI_KEY :
			               it->type == ST_ACTION ? (it->id == S_SAVE_AS ? DI_SAVE : (it->id == S_CLOCK || it->id == S_CLOCK_SYNC) ? DI_CLOCK : DI_PLAY) :
			               it->type == ST_INFO ? DI_INFO : DI_SETTINGS;
			dashIcon(row_icon, ix + 12, y + 7, 15, col);
		}
		if (it->type == ST_INFO) {
			char buf[64];
			snprintf(buf, sizeof(buf), "%s / %s", ip, netmask);
			dashText(ix + 38, y + 4.0f, localizeDashboardText(it->label), DASH_FS_SMALL, DASH_C_TEXT3);
			dashTextRight(ix + iw - 12, y + 4.0f, buf, DASH_FS_SMALL, DASH_C_TEXT2);
			dashGfxPopAlpha();
			continue;
		}
		if (it->type == ST_PAGE) {
			dashTextFit(ix + 38, y + 4.0f, iw - 70, localizeDashboardText(it->label), DASH_FS_SMALL, col);
			dashIcon(DI_PLAY, ix + iw - 22, y + 9, 12, DASH_C_TEXT3);
			dashGfxPopAlpha();
			continue;
		}
		{
			const char *ast = it->type == ST_ACTION ? actionStatus(it->id) : NULL;
			float label_w = it->type == ST_ACTION ? (ast ? iw * 0.58f : iw - 70) : iw * 0.56f - 50;
			dashTextFit(ix + 38, y + 4.0f, label_w, localizeDashboardText(it->label), DASH_FS_SMALL, col);
		}
		if (it->type == ST_COLOR) {
			int alpha;
			DashColor *cv = colorField(it->id, &alpha);
			if (cv != NULL) {
				char hex[12];
				float sx = ix + iw - 50;
				snprintf(hex, sizeof(hex), "#%02X%02X%02X", cv->r, cv->g, cv->b);
				dashTextRight(sx - 10, y + 4.0f, hex, DASH_FS_SMALL, on ? DASH_C_TEXT2 : DASH_C_TEXT3);
				dashRect(sx, y + 6, 36, 15, DC(cv->r, cv->g, cv->b, alpha ? (cv->a < 40 ? 40 : cv->a) : 255));
				dashPanelBorder(sx - 1, y + 5, 38, 17, 2, dashColorA(DASH_C_TEXT, 0.35f));
			}
		} else if (it->type == ST_CHOICE) {
			const char *const *names;
			const char *single;
			int cur, n = choiceInfo(it->id, &cur, &names, &single);
			if (n > 0 && cur >= 0 && cur < n) {
				float maxw = iw * 0.40f;
				float vw = dashTextWidth((it->id == S_LANGUAGE || it->id == S_T_PRESET ? names[cur] : localizeDashboardText(names[cur])), DASH_FS_SMALL);
				float vx;
				if (vw > maxw)
					vw = maxw;
				vx = ix + iw - 20 - vw;
				if (on && !disabled) {
					dashIcon(DI_BACK, vx - 22, y + 7, 12, DASH_C_ACCENT);
					dashIcon(DI_PLAY, ix + iw - 16, y + 8, 10, DASH_C_ACCENT);
				}
				if (on)
					dashTextScroll(vx, y + 4.0f, maxw, (it->id == S_LANGUAGE || it->id == S_T_PRESET ? names[cur] : localizeDashboardText(names[cur])), DASH_FS_SMALL, disabled ? DASH_C_TEXT3 : (n == 2 && names == onoff && cur == 1) ? DASH_C_OK : DASH_C_ACCENT);
				else
					dashTextFit(vx, y + 4.0f, maxw, (it->id == S_LANGUAGE || it->id == S_T_PRESET ? names[cur] : localizeDashboardText(names[cur])), DASH_FS_SMALL, disabled ? DASH_C_TEXT3 : DASH_C_ACCENT);
			}
		} else if (it->type == ST_NUMBER || it->type == ST_PATH || it->type == ST_IP || it->type == ST_LKEY) {
			char text[MAX_PATH], leaf[MAX_PATH];
			const char *v = text;
			int lo, hi, num, set = 1;
			PathSpec ps;

			text[0] = '\0';
			if (it->type == ST_NUMBER)
				numberInfo(it->id, &num, &lo, &hi, text, sizeof(text));
			else if (it->type == ST_PATH) {
				pathInfo(it->id, &ps);
				if (it->id == S_IGR_AFTER_APP && ps.field[0]) {
					DashApp *a = dashLibFind(ps.field);
					v = a != NULL ? a->title : pathLeaf(ps.field, leaf, sizeof(leaf));
				} else
					v = pathLeaf(ps.field, leaf, sizeof(leaf));
				set = ps.field[0] != '\0';
			} else if (it->type == ST_IP)
				v = ipField(it->id);
			else {
				v = lkeyValue(it->id - S_LK_0, leaf, sizeof(leaf));
				set = setting->LK_Path[it->id - S_LK_0][0] != '\0';
			}
			{
				float maxw = iw * 0.40f;
				float vw = dashTextWidth(v, DASH_FS_SMALL);
				float vx;
				if (vw > maxw)
					vw = maxw;
				vx = ix + iw - 20 - vw;
				if (on && !disabled && it->type == ST_NUMBER) {
					dashIcon(DI_BACK, vx - 22, y + 7, 12, DASH_C_ACCENT);
					dashIcon(DI_PLAY, ix + iw - 16, y + 8, 10, DASH_C_ACCENT);
				}
				if (on)
					dashTextScroll(vx, y + 4.0f, maxw, v, DASH_FS_SMALL, set && !disabled ? DASH_C_ACCENT : DASH_C_TEXT3);
				else
					dashTextFit(vx, y + 4.0f, maxw, v, DASH_FS_SMALL, set && !disabled ? DASH_C_ACCENT : DASH_C_TEXT3);
			}
		} else {
			const char *ast = actionStatus(it->id);
			if (ast != NULL && ast[0] != '\0')
				dashTextRight(ix + iw - 32, y + 4.0f, ast, DASH_FS_SMALL,
				              disabled ? DASH_C_TEXT3 : (on ? DASH_C_ACCENT : DASH_C_TEXT2));
			dashIcon(DI_PLAY, ix + iw - 22, y + 8, 12, DASH_C_TEXT3);
		}
		dashGfxPopAlpha();
	}
	dashDrawScrollbar(ix + iw + 10, iy, ITEM_ROWS * irow, item_top, ITEM_ROWS, c->n);
	/* help line */
	{
		const SetItem *it = curItem();
		const char *help = itemDisabled(it) ? disabledHelp(it) : localizeDashboardText(it->help);
		if (it->type == ST_CHOICE && it->id == S_T_PRESET)
			help = dashThemePresetInfo(dashThemeBasePreset())->desc;
		if (help)
			dashTextWrap(ix, iy + ITEM_ROWS * irow + 12, iw, 2, help, DASH_FS_CAPTION, DASH_C_TEXT3, 17);
	}
}

static void settingsHints(void)
{
	DashHint h[6];
	int n = 0;

	{
		const SetItem *it = curItem();
		if (it->type == ST_NUMBER)
			h[n].button = DI_BTN_LEFT, h[n].key = NULL, h[n].label = "Change", n++;
		else
			h[n].button = dashOkIcon(), h[n].key = NULL,
			h[n].label = (it->type == ST_ACTION || it->type == ST_PAGE) ? "Open" : (it->type == ST_COLOR || it->type == ST_IP) ? "Edit" :
			             (it->type == ST_PATH || it->type == ST_LKEY) ? "Choose" : "Change", n++;
		if (it->type != ST_PAGE && themeSettingResettable(it->id))
			h[n].button = DI_BTN_SQUARE, h[n].key = NULL, h[n].label = "Reset", n++;
		else if (it->type == ST_LKEY)
			h[n].button = DI_BTN_SQUARE, h[n].key = NULL, h[n].label = "Title", n++;
		if (page_depth || dashBackSection() != DASH_SEC_SETTINGS)
			h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = page_depth ? "Back" : dashSectionLabel(dashBackSection()), n++;
	}
	h[n].button = DI_BTN_TRIANGLE, h[n].key = NULL, h[n].label = "Changes", n++;
	h[n].button = -1, h[n].key = "START", h[n].label = "Menu", n++;
	dashDrawHints(h, n);
}

const DashScreen dashSettingsScreen = {settingsEnter, settingsInput, settingsUpdate, settingsDraw, settingsHints, settingsLeave};
//--------------------------------------------------------------
//End of file: dash_settings.c
//--------------------------------------------------------------
