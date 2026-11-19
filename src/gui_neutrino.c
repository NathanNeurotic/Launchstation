#include "launchelf.h"
#include "gui_neutrino.h"
#include "dash.h"
#include "dash_internal.h"
#include "dash_sound.h"
#include "neutrino_gameid.h"
#include "init.h"
#include "filer_shared.h"
#include <limits.h>

static const struct {
	int mode;
	const char *label;
} compatibility_options[] = {
	{0, "Fast disc reads"},
	{2, "Synchronous disc reads"},
	{3, "Unhook system calls"},
	{5, "Emulate dual-layer DVD"},
	{7, "Fix game buffer overrun"}
};

static const struct {
	const char *argument;
	const char *label;
} video_modes[] = {
	{"", "Original (480i / 576i)"},
	{"fp1", "240p / 288p (Auto PAL/NTSC)"},
	{"fp2", "480p / 576p (Auto PAL/NTSC)"},
	{"1080ix1", "1080i (Width x1)"},
	{"1080ix2", "1080i (Width x2)"},
	{"1080ix3", "1080i (Width x3)"}
};

static const char *video_compatibility_modes[] = {
	"Off",
	"Field flipping 1 (GSM/OPL)",
	"Field flipping 2",
	"Field flipping 3"
};

enum {
	NEUTRINO_COMPAT_COUNT = sizeof(compatibility_options) / sizeof(compatibility_options[0]),
	NEUTRINO_VIDEO_COUNT = sizeof(video_modes) / sizeof(video_modes[0]),
	NEUTRINO_VIDEO_COMPAT_COUNT = sizeof(video_compatibility_modes) / sizeof(video_compatibility_modes[0]),
	NEUTRINO_VIDEO_MODE = NEUTRINO_COMPAT_COUNT,
	NEUTRINO_VIDEO_COMPATIBILITY,
	NEUTRINO_DEBUG_COLORS,
	NEUTRINO_LOGO,
	NEUTRINO_RUN,
	NEUTRINO_CANCEL
};

/* A VMC name is a file name in <device>/VMC/: no folders, no control characters. */
int neutrinoVmcNameValid(const char *name)
{
	size_t i, len = strlen(name);

	if (len >= NEUTRINO_VMC_NAME_MAX)
		return 0;
	for (i = 0; i < len; i++)
		if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] == 0x7F || name[i] == '/' ||
		    name[i] == '\\' || name[i] == ':')
			return 0;
	return 1;
}

int buildNeutrinoOptionArgs(const NEUTRINO_LAUNCH_OPTIONS *options, NEUTRINO_OPTION_ARGS *args)
{
	unsigned int allowed_modes = 0;
	int i, pos, argc = 0;

	if (options == NULL || args == NULL)
		return -1;
	memset(args, 0, sizeof(*args));
	for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++)
		allowed_modes |= 1u << compatibility_options[i].mode;
	if ((options->game_compatibility & ~allowed_modes) ||
	    options->video_mode < 0 || options->video_mode >= NEUTRINO_VIDEO_COUNT ||
	    options->video_compatibility < 0 || options->video_compatibility >= NEUTRINO_VIDEO_COMPAT_COUNT)
		return -1;

	if (options->game_compatibility) {
		strcpy(args->game_arg, "-gc=");
		pos = 4;
		for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++) {
			if (options->game_compatibility & (1u << compatibility_options[i].mode))
				args->game_arg[pos++] = '0' + compatibility_options[i].mode;
		}
		args->game_arg[pos] = '\0';
		args->argv[argc++] = args->game_arg;
	}
	if (options->video_mode || options->video_compatibility) {
		if (options->video_compatibility)
			snprintf(args->video_arg, sizeof(args->video_arg), "-gsm=%s:%d",
			         video_modes[options->video_mode].argument, options->video_compatibility);
		else
			snprintf(args->video_arg, sizeof(args->video_arg), "-gsm=%s", video_modes[options->video_mode].argument);
		args->argv[argc++] = args->video_arg;
	}
	for (i = 0; i < 2; i++)
		if (!neutrinoVmcNameValid(options->vmc[i]))
			return -1;
	if (options->debug_colors)
		args->argv[argc++] = "-dbc";
	if (options->logo)
		args->argv[argc++] = "-logo";
	return argc;
}


/* Per-game Neutrino settings: NEUT-<GAME ID>.cfg, plain $Key=value text (see
 * docs/NEUTRINO_GAME_CONFIG.md), written by Game settings and editable by hand.
 * They are saved on the game's own device, <device or share root>CFG/NEUT-<ID>.cfg,
 * never on the memory card: a save that cannot be written there fails and says so.
 * Lookup order: that file, then the app config folder's NEUT-<ID>.cfg and the older
 * NEUT-<ID>-A/B.CNF "NTP1|..." profiles, which are only ever read, never modified
 * or deleted. No game image, VMC, or LAUNCHSTATION.CNF is ever touched. Only these
 * per-game files live on the game device; the app's own settings stay in the app
 * config folder. */
static char active_profile_gameid[NEUTRINO_GAME_ID_SIZE];
static char active_profile_path[MAX_PATH]; /* the game's image path: tells which device it lives on */

static int profileGameIdValid(const char *id)
{
	int i;

	if (id == NULL || strlen(id) != NEUTRINO_GAME_ID_SIZE - 1 || id[4] != '_' || id[8] != '.')
		return 0;
	for (i = 0; i < 4; i++)
		if (id[i] < 'A' || id[i] > 'Z')
			return 0;
	for (i = 5; i < NEUTRINO_GAME_ID_SIZE - 1; i++)
		if (i != 8 && (id[i] < '0' || id[i] > '9'))
			return 0;
	return 1;
}

static unsigned int profileChecksum(const char *buf, int size)
{
	unsigned int hash = 2166136261u;
	int i;

	for (i = 0; i < size; i++) {
		hash ^= (unsigned char)buf[i];
		hash *= 16777619u;
	}
	return hash;
}

static int profilePath(const char *id, int slot, char *out, int size)
{
	char filename[40];

	if (!profileGameIdValid(id) || (slot != 0 && slot != 1 && slot != 2))
		return 0;
	if (slot == 2)
		snprintf(filename, sizeof(filename), "NEUT-%s.cfg", id); /* current OPL-style file */
	else
		snprintf(filename, sizeof(filename), "NEUT-%s-%c.CNF", id, slot ? 'B' : 'A'); /* legacy */
	return dashStoreBuildPath(out, size, filename) == 0;
}

/* Length of the root of the device or share the game's image is on ("mass:/",
 * "smb:/NAME/"), 0 when the path is not on one (memory card, none). */
static int profileDeviceRoot(const char *iso)
{
	static const char *const devices[] = { "mass", "usb", "ata", "mx4sio", "mmce", "ilink", "udpfs" };
	static const char *const shares[] = { "smb1:/", "smb:/", "nfs:/" };
	const char *p;
	size_t i, n;

	if (iso == NULL)
		return 0;
	for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++)
		if (!strncmp(iso, devices[i], strlen(devices[i]))) {
			p = strstr(iso, ":/");
			return p != NULL ? (int)(p + 2 - iso) : 0;
		}
	for (i = 0; i < sizeof(shares) / sizeof(shares[0]); i++) {
		n = strlen(shares[i]);
		if (!strncmp(iso, shares[i], n)) {
			p = strchr(iso + n, '/');
			return p != NULL && p > iso + n ? (int)(p + 1 - iso) : 0;
		}
	}
	return 0;
}

/* <root>CFG/NEUT-<ID>.cfg (dir: <root>CFG), only on a device whose stack is already
 * loaded for this launch. */
static int profileDevicePath(const char *id, char *file, int file_size, char *dir, int dir_size)
{
	int root = profileDeviceRoot(active_profile_path);

	if (!profileGameIdValid(id) || root == 0 || !isPathDeviceStackLoaded(active_profile_path))
		return 0;
	if (snprintf(dir, dir_size, "%.*sCFG", root, active_profile_path) >= dir_size ||
	    snprintf(file, file_size, "%s/NEUT-%s.cfg", dir, id) >= file_size)
		return 0;
	return 1;
}

/* A profile lookup must never switch the IOP stack underneath the ISO.
 * In particular MC access while MMCE/MX4SIO owns SIO2 is not assumed safe.
 * Hardware validation may later permit more specific per-slot exceptions. */
static int profileStorageSafe(void)
{
	const char *dir = dashStoreDir();

	if (dir == NULL || dir[0] == '\0' || !isPathDeviceStackLoaded(dir))
		return 0;
	if (!strncmp(dir, "mc", 2)) {
#ifdef MX4SIO
		if (mx4sio_driver_running)
			return 0;
#endif
#ifdef MMCE
		if (isPathDeviceStackLoaded("mmce0:/"))
			return 0;
#endif
	}
	return 1;
}

static int profileOptionsValid(const NEUTRINO_LAUNCH_OPTIONS *options)
{
	NEUTRINO_OPTION_ARGS encoded;

	if (options->debug_colors < 0 || options->debug_colors > 1 ||
	    options->logo < 0 || options->logo > 1)
		return 0;
	return buildNeutrinoOptionArgs(options, &encoded) >= 0;
}

static int profileReadSlot(const char *id, int slot, NEUTRINO_LAUNCH_OPTIONS *out, unsigned int *revision)
{
	char path[MAX_PATH], parsed_id[NEUTRINO_GAME_ID_SIZE];
	char *text, *checksum_separator;
	unsigned int parsed_rev, compat, checksum;
	NEUTRINO_LAUNCH_OPTIONS parsed;
	int len, consumed = 0, payload_len, ok = 0;

	if (!profileStorageSafe() || !profilePath(id, slot, path, sizeof(path)))
		return 0;
	text = dashLoadTextFile(path, 128, &len);
	if (text == NULL)
		return 0;
	if (len <= 0 || (int)strlen(text) != len)
		goto done;
	memset(&parsed, 0, sizeof(parsed));
	if (sscanf(text, "NTP1|%11[^|]|%8x|%8x|%1d|%1d|%1d|%1d|%8x%n",
	           parsed_id, &parsed_rev, &compat, &parsed.video_mode,
	           &parsed.video_compatibility, &parsed.debug_colors,
	           &parsed.logo, &checksum, &consumed) != 8)
		goto done;
	if (consumed <= 0 || text[consumed] != '\n' || text[consumed + 1] != '\0' ||
	    strcmp(parsed_id, id) || parsed_rev == 0)
		goto done;
	checksum_separator = strrchr(text, '|');
	if (checksum_separator == NULL)
		goto done;
	payload_len = (int)(checksum_separator - text);
	if (payload_len < 1 || payload_len >= len ||
	    profileChecksum(text, payload_len) != checksum)
		goto done;
	parsed.game_compatibility = compat;
	if (!profileOptionsValid(&parsed))
		goto done;
	*out = parsed;
	*revision = parsed_rev;
	ok = 1;
done:
	free(text);
	return ok;
}

static int profileReadLatest(const char *id, NEUTRINO_LAUNCH_OPTIONS *out,
                             unsigned int *revision, int *slot)
{
	NEUTRINO_LAUNCH_OPTIONS a, b;
	unsigned int a_rev = 0, b_rev = 0;
	int have_a, have_b;

	have_a = profileReadSlot(id, 0, &a, &a_rev);
	have_b = profileReadSlot(id, 1, &b, &b_rev);
	if (!have_a && !have_b)
		return 0;
	if (have_b && (!have_a || b_rev > a_rev)) {
		*out = b;
		*revision = b_rev;
		*slot = 1;
	} else {
		*out = a;
		*revision = a_rev;
		*slot = 0;
	}
	return 1;
}

/* ---- OPL-style KEY=value file (same $Key=value lines as RiptOPL's per-game .cfg) ---- */
/* $Compatibility is RiptOPL's bit mask: 0x02 sync reads (-gc 2), 0x04 unhook syscalls (3),
 * 0x10 dual-layer DVD (5), 0x40 buffer overrun fix (7). Fast reads (-gc 0) has no OPL bit. */
static unsigned int profileCompatBit(int mode)
{
	return mode == 2 ? 0x02u : mode == 3 ? 0x04u : mode == 5 ? 0x10u : mode == 7 ? 0x40u : 0;
}

static int profileIntValue(const char *v, int max, int *out)
{
	char *end;
	long n;

	if (*v == '\0')
		return 0;
	n = strtol(v, &end, 10);
	if (*end != '\0' || n < 0 || n > max)
		return 0;
	*out = (int)n;
	return 1;
}

/* Parses a NEUT-<ID>.cfg text. Unknown keys and # lines are ignored; a bad
 * value, or a missing last line `$End=1`, rejects the whole file (the caller then falls back to the old profile). */
static int profileParseText(char *text, NEUTRINO_LAUNCH_OPTIONS *out)
{
	NEUTRINO_LAUNCH_OPTIONS o;
	char *line = text, *next, *eq, *val, *end, *tok, *save;
	int i, n, fast = 0, complete = 0;

	memset(&o, 0, sizeof(o));
	while (line != NULL && *line) {
		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		end = line + strlen(line);
		while (end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';
		if (complete && line[0] != '\0')
			return 0; /* $End=1 is the last line: anything after it is not a saved file */
		eq = strchr(line, '=');
		if (line[0] == '$' && eq != NULL) {
			*eq = '\0';
			val = eq + 1;
			end = eq;
			while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
				*--end = '\0';
			while (*val == ' ' || *val == '\t')
				val++;
			if (!strcmp(line, "$Compatibility")) {
				if (!profileIntValue(val, 255, &n))
					return 0;
				for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++)
					if (profileCompatBit(compatibility_options[i].mode) &&
					    (n & profileCompatBit(compatibility_options[i].mode)))
						o.game_compatibility |= 1u << compatibility_options[i].mode;
			} else if (!strcmp(line, "$End")) {
				/* Last line of a saved file: a cut-off save or read never has it. */
				if (!profileIntValue(val, 1, &n))
					return 0;
				complete = n == 1;
			} else if (!strcmp(line, "$NeutrinoFastReads")) {
				if (!profileIntValue(val, 1, &fast))
					return 0;
			} else if (!strcmp(line, "$NeutrinoVideo")) {
				if (!profileIntValue(val, NEUTRINO_VIDEO_COUNT - 1, &o.video_mode))
					return 0;
			} else if (!strcmp(line, "$NeutrinoGsmComp")) {
				if (!profileIntValue(val, NEUTRINO_VIDEO_COMPAT_COUNT - 1, &o.video_compatibility))
					return 0;
			} else if (!strcmp(line, "$NeutrinoArgs")) {
				/* Only -dbc and -logo are handled here; other tokens are ignored. */
				for (tok = strtok_r(val, " \t", &save); tok != NULL; tok = strtok_r(NULL, " \t", &save)) {
					if (!strcmp(tok, "-dbc"))
						o.debug_colors = 1;
					else if (!strcmp(tok, "-logo"))
						o.logo = 1;
				}
			} else if (!strcmp(line, "$VMC_0") || !strcmp(line, "$VMC_1")) {
				if (!neutrinoVmcNameValid(val))
					return 0;
				strcpy(o.vmc[line[5] - '0'], val);
			}
		}
		line = next;
	}
	if (!complete)
		return 0;
	if (fast)
		o.game_compatibility |= 1u << 0;
	if (!profileOptionsValid(&o))
		return 0;
	*out = o;
	return 1;
}

static int profileReadAt(const char *path, NEUTRINO_LAUNCH_OPTIONS *out)
{
	char *text;
	int len, ok;

	text = dashLoadTextFile(path, 2048, &len);
	if (text == NULL)
		return 0;
	ok = profileParseText(text, out);
	free(text);
	return ok;
}

/* The game device's file first, then the app config folder's. */
static int profileReadFile(const char *id, NEUTRINO_LAUNCH_OPTIONS *out)
{
	char path[MAX_PATH], dir[MAX_PATH];

	if (profileDevicePath(id, path, sizeof(path), dir, sizeof(dir)) && profileReadAt(path, out))
		return 1;
	if (!profileStorageSafe() || !profilePath(id, 2, path, sizeof(path)))
		return 0;
	return profileReadAt(path, out);
}

static int profileActiveFileInvalid(const char *id)
{
	NEUTRINO_LAUNCH_OPTIONS parsed;
	char path[MAX_PATH], dir[MAX_PATH], *text;
	int invalid = 0;

	if (!profileDevicePath(id, path, sizeof(path), dir, sizeof(dir)))
		return 0;
	text = dashLoadTextFile(path, 2048, NULL);
	if (text != NULL) {
		invalid = !profileParseText(text, &parsed);
		free(text);
	}
	return invalid;
}

/* The saved settings for a game: the KEY=value file if it is valid, else the
 * newest valid legacy NTP1 profile. */
static int profileLoad(const char *id, NEUTRINO_LAUNCH_OPTIONS *out)
{
	unsigned int revision;
	int slot;

	return profileReadFile(id, out) || profileReadLatest(id, out, &revision, &slot);
}

/* 1: saved on the game's device. 0: not saved. */
static int profileSave(const char *id, const NEUTRINO_LAUNCH_OPTIONS *options)
{
	NEUTRINO_LAUNCH_OPTIONS read_back;
	char path[MAX_PATH], dir[MAX_PATH], text[512];
	int n, i;

	if (!profileGameIdValid(id) || !profileOptionsValid(options) || !dashWorkerAwaitAll("Finishing up"))
		return 0;
	{
		int mask = 0;
		for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++)
			if (options->game_compatibility & (1u << compatibility_options[i].mode))
				mask |= (int)profileCompatBit(compatibility_options[i].mode);
		n = snprintf(text, sizeof(text),
		             "$Compatibility=%d\n$NeutrinoFastReads=%d\n$NeutrinoVideo=%d\n$NeutrinoGsmComp=%d\n"
		             "$NeutrinoArgs=%s%s%s\n$VMC_0=%s\n$VMC_1=%s\n$End=1\n",
		             mask, (int)(options->game_compatibility & 1u), options->video_mode, options->video_compatibility,
		             options->debug_colors ? "-dbc" : "", options->debug_colors && options->logo ? " " : "",
		             options->logo ? "-logo" : "", options->vmc[0], options->vmc[1]);
	}
	if (n <= 0 || n >= (int)sizeof(text))
		return 0;
	/* The game's own device only: one file in CFG/, created if it is missing (the
	 * folder RiptOPL uses). A failed or cut-short write leaves a file without $End=1,
	 * which the loader ignores, and the save fails; it is never redirected to the
	 * memory card. */
	if (!profileDevicePath(id, path, sizeof(path), dir, sizeof(dir)))
		return 0;
	if (!strncmp(path, "udpfs", 5) && !setting->HOSTwrite)
		return 0; /* network writing is off, as in the File Browser */
	{
		int wrote = dashWriteFile(path, text, n);
		if (wrote < 0 && genMkdir(dir, fileMode) >= 0)
			wrote = dashWriteFile(path, text, n);
		return wrote >= 0 && profileReadAt(path, &read_back) && memcmp(&read_back, options, sizeof(*options)) == 0;
	}
}


static int moveNeutrinoSelection(int selection, int direction)
{
	if (direction < 0) {
		if (selection == 0)
			return NEUTRINO_CANCEL;
		if (selection >= NEUTRINO_RUN)
			return selection - 2;
		if (selection >= NEUTRINO_DEBUG_COLORS)
			return NEUTRINO_VIDEO_COMPATIBILITY;
		return selection - 1;
	}
	if (selection >= NEUTRINO_RUN)
		return 0;
	if (selection >= NEUTRINO_DEBUG_COLORS)
		return selection + 2;
	return selection + 1;
}

/* Fill each option row's value from the current launch options. Run game is
 * row zero, so each option's row is its option ID plus one. */
static void neutrinoOptionDetails(const NEUTRINO_LAUNCH_OPTIONS *options, DashMenuItem *items)
{
	int i;

	for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++)
		items[i + 1].detail = options->game_compatibility & (1u << compatibility_options[i].mode) ? "On" : "Off";
	items[NEUTRINO_VIDEO_MODE + 1].detail = video_modes[options->video_mode].label;
	items[NEUTRINO_VIDEO_COMPATIBILITY + 1].detail = video_compatibility_modes[options->video_compatibility];
	items[NEUTRINO_DEBUG_COLORS + 1].detail = options->debug_colors ? "On" : "Off";
	items[NEUTRINO_LOGO + 1].detail = options->logo ? "On" : "Off";
}

typedef struct {
	NEUTRINO_LAUNCH_OPTIONS *options;
	DashMenuItem *items;
} NeutrinoOptionsMenu;

/* Changes a row in place (dashModalSetEdit): Left/Right step a value or flip a
 * switch, Confirm flips a switch, opens the list for the two video rows, or
 * saves/resets the game profile. Run game and Cancel are left to the menu. */
static int editNeutrinoOption(int row, int direction, void *arg)
{
	NeutrinoOptionsMenu *menu = arg;
	NEUTRINO_LAUNCH_OPTIONS *options = menu->options;
	const int option = row - 1;
	int i, choice;

	if (row == 0)
		return FALSE;
	if (option < NEUTRINO_COMPAT_COUNT)
		options->game_compatibility ^= 1u << compatibility_options[option].mode;
	else if (option == NEUTRINO_VIDEO_MODE) {
		if (direction)
			options->video_mode = (options->video_mode + direction + NEUTRINO_VIDEO_COUNT) % NEUTRINO_VIDEO_COUNT;
		else {
			const char *labels[NEUTRINO_VIDEO_COUNT];
			for (i = 0; i < NEUTRINO_VIDEO_COUNT; i++) labels[i] = video_modes[i].label;
			choice = dashModalChoice("Video mode", labels, NEUTRINO_VIDEO_COUNT, options->video_mode);
			if (choice >= 0) options->video_mode = choice;
		}
	} else if (option == NEUTRINO_VIDEO_COMPATIBILITY) {
		if (direction)
			options->video_compatibility = (options->video_compatibility + direction + NEUTRINO_VIDEO_COMPAT_COUNT) % NEUTRINO_VIDEO_COMPAT_COUNT;
		else {
			choice = dashModalChoice("Video compatibility", video_compatibility_modes, NEUTRINO_VIDEO_COMPAT_COUNT, options->video_compatibility);
			if (choice >= 0) options->video_compatibility = choice;
		}
	} else if (option == NEUTRINO_DEBUG_COLORS)
		options->debug_colors = !options->debug_colors;
	else if (option == NEUTRINO_LOGO)
		options->logo = !options->logo;
	else if (!direction && active_profile_gameid[0] && row == NEUTRINO_CANCEL) {
		int saved = profileSave(active_profile_gameid, options);
		dashToast(saved ? DASH_TOAST_OK : DASH_TOAST_WARN,
		          saved ? "Saved to the game's device" : "Could not save: the game's device did not accept the file");
		return TRUE;
	} else if (!direction && active_profile_gameid[0] && row == NEUTRINO_CANCEL + 1)
		memset(options, 0, sizeof(*options));
	else
		return FALSE;
	neutrinoOptionDetails(options, menu->items);
	return TRUE;
}

/* One menu session for every change, so the popup keeps its focus and scroll
 * instead of reopening after each value. The classic shell continues to use
 * the upstream dialog below. */
/* settings_only (GAMES Triangle): row zero saves and closes instead of running. */
static int showDashboardNeutrinoOptions(NEUTRINO_LAUNCH_OPTIONS *options, int settings_only)
{
	DashMenuItem items[NEUTRINO_CANCEL + 3];
	NeutrinoOptionsMenu menu = {options, items};
	const int profile_enabled = active_profile_gameid[0] != '\0';
	const int cancel_index = profile_enabled ? NEUTRINO_CANCEL + 2 : NEUTRINO_CANCEL;
	int i;

	/* ISO and profile preparation may have restarted the storage IOP, and the
	 * main dashboard loop is paused while these options are open. */
	dashSoundMaintain();
	memset(items, 0, sizeof(items));
	items[0].icon = settings_only ? DI_SAVE : DI_PLAY;
	items[0].label = settings_only ? "Save and close" : "Run game";
	for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++) {
		items[i + 1].icon = DI_INFO;
		items[i + 1].label = compatibility_options[i].label;
	}
	items[NEUTRINO_VIDEO_MODE + 1].icon = DI_SETTINGS;
	items[NEUTRINO_VIDEO_MODE + 1].label = "Video mode";
	items[NEUTRINO_VIDEO_COMPATIBILITY + 1].icon = DI_SETTINGS;
	items[NEUTRINO_VIDEO_COMPATIBILITY + 1].label = "Video compatibility";
	items[NEUTRINO_DEBUG_COLORS + 1].icon = DI_INFO;
	items[NEUTRINO_DEBUG_COLORS + 1].label = "Debug colors";
	items[NEUTRINO_LOGO + 1].icon = DI_INFO;
	items[NEUTRINO_LOGO + 1].label = "PS2 logo";
	if (profile_enabled) {
		items[NEUTRINO_CANCEL].icon = DI_SAVE;
		items[NEUTRINO_CANCEL].label = "Save for this game";
		items[NEUTRINO_CANCEL + 1].icon = DI_UNDO;
		items[NEUTRINO_CANCEL + 1].label = settings_only ? "Reset to defaults" : "Reset this launch";
	}
	items[cancel_index].icon = DI_BACK;
	items[cancel_index].label = "Cancel";
	neutrinoOptionDetails(options, items);
	dashModalSetEdit(editNeutrinoOption, &menu);
	return dashModalMenu("Neutrino launch options",
	                     profile_enabled ? active_profile_gameid : "For this launch only",
	                     items, cancel_index + 1, 0) == 0;
}

static void drawNeutrinoHeading(const char *heading, int y)
{
	int width = printXY(heading, 0, 0, 0, FALSE, 0);

	printXY(heading, (SCREEN_WIDTH - width) / 2, y, setting->color[COLOR_TEXT], TRUE, 0);
}

static void drawNeutrinoOption(const char *text, int item, int selection, int x, int y)
{
	u64 color = setting->color[item == selection ? COLOR_SELECT : COLOR_TEXT];

	printXY(text, x, y, color, TRUE, 0);
	if (item == selection)
		drawChar(LEFT_CUR, x - 2 * FONT_WIDTH, y, color);
}

int showNeutrinoLaunchOptions(NEUTRINO_LAUNCH_OPTIONS *options)
{
	char text[128];
	int selection = 0, event = 1, post_event = 0;
	int width = SCREEN_WIDTH - 2 * SCREEN_MARGIN;
	int height = 17 * FONT_HEIGHT;
	int left = (SCREEN_WIDTH - width) / 2;
	int top = (SCREEN_HEIGHT - height) / 2;
	int x = left + 3 * FONT_WIDTH;
	int right_x = left + width / 2 + 3 * FONT_WIDTH;
	int y = top + FONT_HEIGHT / 2;
	int i, direction;

	if (options == NULL)
		return FALSE;
	memset(options, 0, sizeof(*options));
	if (DashIsActive()) {
		NEUTRINO_LAUNCH_OPTIONS saved;
		if (active_profile_gameid[0] && profileActiveFileInvalid(active_profile_gameid)) {
			dashToast(DASH_TOAST_WARN, "Game settings file is invalid; edit it as text before saving");
			return FALSE;
		}
		if (active_profile_gameid[0] && dashWorkerAwaitAll("Loading game profile") &&
		    profileLoad(active_profile_gameid, &saved))
			*options = saved;
		return showDashboardNeutrinoOptions(options, 0);
	}
	while (1) {
		waitPadReady(0, 0);
		if (readpad()) {
			if ((new_pad & PAD_TRIANGLE) || (!swapKeys && (new_pad & PAD_CROSS)) ||
			    (swapKeys && (new_pad & PAD_CIRCLE)))
				return FALSE;
			if (new_pad & PAD_UP) {
				selection = moveNeutrinoSelection(selection, -1);
				event = 1;
			} else if (new_pad & PAD_DOWN) {
				selection = moveNeutrinoSelection(selection, 1);
				event = 1;
			} else if (new_pad & (PAD_LEFT | PAD_RIGHT)) {
				direction = (new_pad & PAD_LEFT) ? -1 : 1;
				if (selection == NEUTRINO_VIDEO_MODE)
					options->video_mode = (options->video_mode + direction + NEUTRINO_VIDEO_COUNT) % NEUTRINO_VIDEO_COUNT;
				else if (selection == NEUTRINO_VIDEO_COMPATIBILITY)
					options->video_compatibility = (options->video_compatibility + direction + NEUTRINO_VIDEO_COMPAT_COUNT) % NEUTRINO_VIDEO_COMPAT_COUNT;
				else if (selection == NEUTRINO_DEBUG_COLORS || selection == NEUTRINO_LOGO)
					selection = (selection == NEUTRINO_DEBUG_COLORS) ? NEUTRINO_LOGO : NEUTRINO_DEBUG_COLORS;
				else if (selection == NEUTRINO_RUN || selection == NEUTRINO_CANCEL)
					selection = (selection == NEUTRINO_RUN) ? NEUTRINO_CANCEL : NEUTRINO_RUN;
				event = 1;
			} else if ((swapKeys && (new_pad & PAD_CROSS)) || (!swapKeys && (new_pad & PAD_CIRCLE))) {
				if (selection < NEUTRINO_COMPAT_COUNT)
					options->game_compatibility ^= 1u << compatibility_options[selection].mode;
				else if (selection == NEUTRINO_VIDEO_MODE)
					options->video_mode = (options->video_mode + 1) % NEUTRINO_VIDEO_COUNT;
				else if (selection == NEUTRINO_VIDEO_COMPATIBILITY)
					options->video_compatibility = (options->video_compatibility + 1) % NEUTRINO_VIDEO_COMPAT_COUNT;
				else if (selection == NEUTRINO_DEBUG_COLORS)
					options->debug_colors = !options->debug_colors;
				else if (selection == NEUTRINO_LOGO)
					options->logo = !options->logo;
				else
					return selection == NEUTRINO_RUN;
				event = 1;
			}
		}

		if (event || post_event) {
			clrScr(setting->color[COLOR_BACKGR]);
			setScrTmp("", "");
			drawPopSprite(setting->color[COLOR_BACKGR], left, top, left + width, top + height);
			drawFrame(left, top, left + width, top + height, setting->color[COLOR_FRAME]);
			drawNeutrinoHeading("Game Compatibility", y);
			for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++) {
				snprintf(text, sizeof(text), "[%c] %s",
				         (options->game_compatibility & (1u << compatibility_options[i].mode)) ? 'X' : ' ',
				         compatibility_options[i].label);
				drawNeutrinoOption(text, i, selection, x, y + (i + 1) * FONT_HEIGHT);
			}
			drawNeutrinoHeading("Video Modes", y + 7 * FONT_HEIGHT);
			snprintf(text, sizeof(text), "Video Mode: < %s >", video_modes[options->video_mode].label);
			drawNeutrinoOption(text, NEUTRINO_VIDEO_MODE, selection, x, y + 8 * FONT_HEIGHT);
			snprintf(text, sizeof(text), "Video Compatibility: < %s >", video_compatibility_modes[options->video_compatibility]);
			drawNeutrinoOption(text, NEUTRINO_VIDEO_COMPATIBILITY, selection, x, y + 9 * FONT_HEIGHT);
			snprintf(text, sizeof(text), "Debug Colors: %s", options->debug_colors ? LNG(YES) : LNG(NO));
			drawNeutrinoOption(text, NEUTRINO_DEBUG_COLORS, selection, x, y + 11 * FONT_HEIGHT);
			snprintf(text, sizeof(text), "PS2 Logo: %s", options->logo ? LNG(YES) : LNG(NO));
			drawNeutrinoOption(text, NEUTRINO_LOGO, selection, right_x, y + 11 * FONT_HEIGHT);
			drawNeutrinoOption("Run", NEUTRINO_RUN, selection, x, y + 13 * FONT_HEIGHT);
			drawNeutrinoOption(LNG(Cancel), NEUTRINO_CANCEL, selection, right_x, y + 13 * FONT_HEIGHT);
			snprintf(text, sizeof(text), "D-pad: %s  \xFF" "%c: %s  \xFF" "3: %s",
			         LNG(Select), swapKeys ? '1' : '0', LNG(Change), LNG(Cancel));
			printXY(text, x, y + 15 * FONT_HEIGHT, setting->color[COLOR_TEXT], TRUE, 0);
		}
		drawScr();
		post_event = event;
		event = 0;
	}
}

/* The context is restricted to one synchronous UI call and always cleared.
 * Classic/no-ID launches retain their historical launch-only defaults. */
int showNeutrinoLaunchOptionsForGame(NEUTRINO_LAUNCH_OPTIONS *options, const char *game_id, const char *game_path)
{
	int result;

	active_profile_gameid[0] = '\0';
	active_profile_path[0] = '\0';
	if (profileGameIdValid(game_id)) {
		snprintf(active_profile_gameid, sizeof(active_profile_gameid), "%s", game_id);
		if (game_path != NULL)
			snprintf(active_profile_path, sizeof(active_profile_path), "%s", game_path);
	}
	result = showNeutrinoLaunchOptions(options);
	active_profile_gameid[0] = '\0';
	active_profile_path[0] = '\0';
	return result;
}

/* GAMES Triangle "Game settings": edit this game's saved profile without
 * starting it. Returns 1 saved, 0 closed unsaved, -1 no valid Game ID. */
int editNeutrinoGameProfile(const char *game_id, const char *game_path)
{
	NEUTRINO_LAUNCH_OPTIONS options, saved;
	int result = 0;

	if (!profileGameIdValid(game_id))
		return -1;
	/* Backing out of the wait must not open defaults that a save would store. */
	if (!dashWorkerAwaitAll("Loading game profile"))
		return 0;
	memset(&options, 0, sizeof(options));
	snprintf(active_profile_gameid, sizeof(active_profile_gameid), "%s", game_id);
	snprintf(active_profile_path, sizeof(active_profile_path), "%s", game_path != NULL ? game_path : "");
	if (profileActiveFileInvalid(game_id)) {
		dashToast(DASH_TOAST_WARN, "Game settings file is invalid; edit it as text before saving");
		goto out;
	}
	if (profileLoad(game_id, &saved))
		options = saved;
	if (showDashboardNeutrinoOptions(&options, 1)) {
		result = profileSave(game_id, &options);
		dashToast(result ? DASH_TOAST_OK : DASH_TOAST_WARN,
		          result ? "Saved to the game's device" : "Could not save: the game's device did not accept the file");
	}
out:
	active_profile_gameid[0] = '\0';
	active_profile_path[0] = '\0';
	return result;
}
