#include "launchelf.h"
#include "gui_neutrino.h"
#include "dash.h"
#include "dash_internal.h"
#include "neutrino_gameid.h"
#include "init.h"
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
	if (options->debug_colors)
		args->argv[argc++] = "-dbc";
	if (options->logo)
		args->argv[argc++] = "-logo";
	return argc;
}


/* Versioned two-slot Neutrino profiles. A save overwrites only the older (or
 * invalid) slot; the newest verified slot remains intact if an I/O write is
 * interrupted. No game image, VMC, or LAUNCHELF.CNF is ever modified. */
static char active_profile_gameid[NEUTRINO_GAME_ID_SIZE];

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

	if (!profileGameIdValid(id) || (slot != 0 && slot != 1))
		return 0;
	snprintf(filename, sizeof(filename), "NEUT-%s-%c.CNF", id, slot ? 'B' : 'A');
	return dashStoreBuildPath(out, size, filename) == 0;
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

static int profileSave(const char *id, const NEUTRINO_LAUNCH_OPTIONS *options)
{
	NEUTRINO_LAUNCH_OPTIONS previous, read_back;
	unsigned int last_rev = 0, verify_rev = 0, next_rev;
	int last_slot = 1, target, n, len, slot_read = -1;
	char path[MAX_PATH], payload[112], record[128];

	if (!profileGameIdValid(id) || !profileOptionsValid(options) ||
	    !profileStorageSafe() || !dashWorkerAwaitAll("Finishing up"))
		return 0;
	(void)profileReadLatest(id, &previous, &last_rev, &last_slot);
	if (last_rev == UINT_MAX)
		return 0;
	next_rev = last_rev + 1;
	target = 1 - last_slot;
	if (!profilePath(id, target, path, sizeof(path)))
		return 0;
	n = snprintf(payload, sizeof(payload), "NTP1|%s|%08X|%08X|%d|%d|%d|%d",
	             id, next_rev, options->game_compatibility, options->video_mode,
	             options->video_compatibility, options->debug_colors, options->logo);
	if (n <= 0 || n >= (int)sizeof(payload))
		return 0;
	len = snprintf(record, sizeof(record), "%s|%08X\n", payload, profileChecksum(payload, n));
	if (len <= 0 || len >= (int)sizeof(record) ||
	    dashWriteFile(path, record, len) < 0)
		return 0;
	return profileReadLatest(id, &read_back, &verify_rev, &slot_read) &&
	       slot_read == target && verify_rev == next_rev &&
	       memcmp(&read_back, options, sizeof(*options)) == 0;
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

/* Keep dashboard rendering, input and audio maintenance on their existing
 * modal path. The classic shell continues to use the upstream dialog below. */
static int showDashboardNeutrinoOptions(NEUTRINO_LAUNCH_OPTIONS *options)
{
	DashMenuItem items[NEUTRINO_CANCEL + 3];
	const int profile_enabled = active_profile_gameid[0] != '\0';
	const int cancel_index = profile_enabled ? NEUTRINO_CANCEL + 2 : NEUTRINO_CANCEL;
	const int item_count = cancel_index + 1;
	int selection = 0, i, choice;

	for (;;) {
		memset(items, 0, sizeof(items));
		for (i = 0; i < NEUTRINO_COMPAT_COUNT; i++) {
			items[i].icon = DI_INFO;
			items[i].label = compatibility_options[i].label;
			items[i].detail = options->game_compatibility & (1u << compatibility_options[i].mode) ? "On" : "Off";
		}
		items[NEUTRINO_VIDEO_MODE].icon = DI_SETTINGS;
		items[NEUTRINO_VIDEO_MODE].label = "Video mode";
		items[NEUTRINO_VIDEO_MODE].detail = video_modes[options->video_mode].label;
		items[NEUTRINO_VIDEO_COMPATIBILITY].icon = DI_SETTINGS;
		items[NEUTRINO_VIDEO_COMPATIBILITY].label = "Video compatibility";
		items[NEUTRINO_VIDEO_COMPATIBILITY].detail = video_compatibility_modes[options->video_compatibility];
		items[NEUTRINO_DEBUG_COLORS].icon = DI_INFO;
		items[NEUTRINO_DEBUG_COLORS].label = "Debug colors";
		items[NEUTRINO_DEBUG_COLORS].detail = options->debug_colors ? "On" : "Off";
		items[NEUTRINO_LOGO].icon = DI_INFO;
		items[NEUTRINO_LOGO].label = "PS2 logo";
		items[NEUTRINO_LOGO].detail = options->logo ? "On" : "Off";
		items[NEUTRINO_RUN].icon = DI_PLAY;
		items[NEUTRINO_RUN].label = "Run game";
		if (profile_enabled) {
			items[NEUTRINO_CANCEL].icon = DI_STAR;
			items[NEUTRINO_CANCEL].label = "Save for this game";
			items[NEUTRINO_CANCEL + 1].icon = DI_REFRESH;
			items[NEUTRINO_CANCEL + 1].label = "Reset this launch";
		}
		items[cancel_index].icon = DI_BACK;
		items[cancel_index].label = "Cancel";
		selection = dashModalMenu("Neutrino launch options",
		                          profile_enabled ? active_profile_gameid : "For this launch only",
		                          items, item_count, selection);
		if (selection < 0 || selection == cancel_index)
			return FALSE;
		if (selection == NEUTRINO_RUN)
			return TRUE;
		if (profile_enabled && selection == NEUTRINO_CANCEL) {
			int saved = profileSave(active_profile_gameid, options);
			dashToast(saved ? DASH_TOAST_OK : DASH_TOAST_WARN,
			          saved ? "Saved Neutrino game profile" : "Could not save game profile");
			continue;
		}
		if (profile_enabled && selection == NEUTRINO_CANCEL + 1) {
			memset(options, 0, sizeof(*options));
			continue;
		}
		if (selection < NEUTRINO_COMPAT_COUNT)
			options->game_compatibility ^= 1u << compatibility_options[selection].mode;
		else if (selection == NEUTRINO_VIDEO_MODE) {
			const char *labels[NEUTRINO_VIDEO_COUNT];
			for (i = 0; i < NEUTRINO_VIDEO_COUNT; i++) labels[i] = video_modes[i].label;
			choice = dashModalChoice("Video mode", labels, NEUTRINO_VIDEO_COUNT, options->video_mode);
			if (choice >= 0) options->video_mode = choice;
		} else if (selection == NEUTRINO_VIDEO_COMPATIBILITY) {
			choice = dashModalChoice("Video compatibility", video_compatibility_modes, NEUTRINO_VIDEO_COMPAT_COUNT, options->video_compatibility);
			if (choice >= 0) options->video_compatibility = choice;
		} else if (selection == NEUTRINO_DEBUG_COLORS)
			options->debug_colors = !options->debug_colors;
		else if (selection == NEUTRINO_LOGO)
			options->logo = !options->logo;
	}
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
		unsigned int revision;
		int slot;
		if (active_profile_gameid[0] && dashWorkerAwaitAll("Loading game profile") &&
		    profileReadLatest(active_profile_gameid, &saved, &revision, &slot))
			*options = saved;
		return showDashboardNeutrinoOptions(options);
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
int showNeutrinoLaunchOptionsForGame(NEUTRINO_LAUNCH_OPTIONS *options, const char *game_id)
{
	int result;

	active_profile_gameid[0] = '\0';
	if (profileGameIdValid(game_id))
		snprintf(active_profile_gameid, sizeof(active_profile_gameid), "%s", game_id);
	result = showNeutrinoLaunchOptions(options);
	active_profile_gameid[0] = '\0';
	return result;
}
