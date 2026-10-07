#include "popstarter_internal.h"

#define EMBER_NAME_MAX 180
#define EMBER_BIOS_SIZE (512 * 1024)
#define EMBER_COPY_CHUNK (32 * 1024)

static void setEmberMessage(char *message, size_t message_size, const char *text, const char *detail)
{
	if (message == NULL || message_size == 0)
		return;
	if (detail != NULL && detail[0] != '\0')
		snprintf(message, message_size, "%s: %s", text, detail);
	else
		snprintf(message, message_size, "%s", text);
}

static int isSep(char c)
{
	return c == '/' || c == '\\';
}

static int segmentCaseEqual(const char *value, const char *word)
{
	int i;

	if (value == NULL || word == NULL)
		return 0;
	for (i = 0; word[i] != '\0'; i++) {
		if (value[i] == '\0' || isSep(value[i]))
			return 0;
		if (wle_ascii_tolower((unsigned char)value[i]) !=
		    wle_ascii_tolower((unsigned char)word[i]))
			return 0;
	}
	return isSep(value[i]);
}

static int emberExt(const char *path)
{
	return genCmpFileExt(path, "CUE") || genCmpFileExt(path, "BIN") || genCmpFileExt(path, "EXE");
}

static char pathSep(const char *path)
{
	const char *slash;
	const char *backslash;

	if (path == NULL)
		return '/';
	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
		return '\\';
	return '/';
}

static int appendFile(char *out, size_t out_size, const char *dir, const char *name)
{
	int len, n;
	char sep;

	if (out == NULL || out_size == 0 || dir == NULL || dir[0] == '\0' || name == NULL)
		return 0;
	len = (int)strlen(dir);
	sep = pathSep(dir);
	if (isSep(dir[len - 1]))
		n = snprintf(out, out_size, "%s%s", dir, name);
	else
		n = snprintf(out, out_size, "%s%c%s", dir, sep, name);
	return n > 0 && (size_t)n < out_size;
}

/*
 * Ember's save-safe layout is relative to whichever directory contains
 * ember.elf:
 *
 *   <umbrella>/ember.elf
 *   <umbrella>/bios.bin
 *   <umbrella>/games/<Game>/<disc image>
 *
 * The umbrella name/location is deliberately NOT fixed. The selected image
 * tells us which device-local umbrella it belongs to; argv[1] is the bare
 * <Game> folder name. This lets VCD continue to route to POPStarter while
 * CUE/BIN/EXE routes to Ember automatically.
 */
static int parseEmberGamePath(const char *path,
                              char *umbrella, size_t umbrella_size,
                              char *game_name, size_t game_name_size)
{
	const char *p, *games_sep = NULL, *game, *game_end, *image;
	size_t root_len, game_len;

	if (path == NULL || strchr(path, ':') == NULL || !emberExt(path))
		return 0;

	for (p = path; *p != '\0'; p++) {
		if (!isSep(*p))
			continue;
		if (segmentCaseEqual(p + 1, "games")) {
			games_sep = p;
			break;
		}
	}
	if (games_sep == NULL)
		return 0;

	game = games_sep + 1 + 5; /* separator + "games" */
	if (!isSep(*game))
		return 0;
	game++;
	if (*game == '\0')
		return 0;

	game_end = game;
	while (*game_end != '\0' && !isSep(*game_end))
		game_end++;
	if (!isSep(*game_end) || game_end == game)
		return 0;

	image = game_end + 1;
	if (*image == '\0')
		return 0;
	for (p = image; *p != '\0'; p++) {
		if (isSep(*p))
			return 0; /* image must be directly inside games/<Game>/ */
	}
	if (!emberExt(image))
		return 0;

	game_len = (size_t)(game_end - game);
	if (game_len == 0 || game_len > EMBER_NAME_MAX)
		return 0;
	if (game_len >= 2 && game[0] == '.' && game[1] == '.')
		return 0;
	for (p = game; p < game_end; p++) {
		if (*p == ':' || isSep(*p))
			return 0;
	}

	root_len = (size_t)(games_sep - path);
	if (root_len == 0 || umbrella == NULL || umbrella_size == 0 || root_len >= umbrella_size)
		return 0;
	memcpy(umbrella, path, root_len);
	umbrella[root_len] = '\0';

	if (game_name != NULL && game_name_size > 0) {
		if (game_len >= game_name_size)
			return 0;
		memcpy(game_name, game, game_len);
		game_name[game_len] = '\0';
	}
	return 1;
}

int IsEmberGamePath(const char *path)
{
	char umbrella[MAX_PATH];

	return parseEmberGamePath(path, umbrella, sizeof(umbrella), NULL, 0);
}

static int readableFile(const char *path)
{
	int fd;

	fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;
	genClose(fd);
	return 1;
}

static int readableElf(const char *path)
{
	unsigned char ident[4];
	int fd, rd;

	fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;
	rd = genRead(fd, ident, sizeof(ident));
	genClose(fd);
	return rd == (int)sizeof(ident) && ident[0] == 0x7f &&
	       ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
}

static int validBios(const char *path)
{
	int fd;
	int size;

	fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;
	size = genLseek(fd, 0, SEEK_END);
	genClose(fd);
	return size == EMBER_BIOS_SIZE;
}

static int isUsbEmberPath(const char *path)
{
	if (path == NULL)
		return 0;
	return (!strncmp(path, "usb", 3) &&
	        (path[3] == ':' || (path[3] >= '0' && path[3] <= '9' && path[4] == ':'))) ||
	       (!strncmp(path, "mass", 4) &&
	        (path[4] == ':' || (path[4] >= '0' && path[4] <= '9' && path[5] == ':')));
}

static int samePath(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return 0;
	return wle_stricmp(a, b) == 0;
}

static int copyElf(const char *source, const char *dest)
{
	unsigned char *buf;
	int in_fd, out_fd, rd, wr, ok = 0;

	in_fd = openPathForRead(source, NULL, 0);
	if (in_fd < 0)
		return 0;
	out_fd = genOpen(dest, FIO_O_CREAT | FIO_O_WRONLY | FIO_O_TRUNC);
	if (out_fd < 0) {
		genClose(in_fd);
		return 0;
	}
	buf = (unsigned char *)malloc(EMBER_COPY_CHUNK);
	if (buf == NULL)
		goto finish;

	for (;;) {
		rd = genRead(in_fd, buf, EMBER_COPY_CHUNK);
		if (rd < 0)
			goto finish;
		if (rd == 0) {
			ok = 1;
			break;
		}
		wr = genWrite(out_fd, buf, rd);
		if (wr != rd)
			goto finish;
	}
finish:
	if (buf != NULL)
		free(buf);
	genClose(out_fd);
	genClose(in_fd);
	if (!ok)
		genRemove(dest);
	return ok;
}

static int configuredEmberElf(char *out, size_t out_size)
{
	if (setting == NULL || setting->ember_dir[0] == '\0')
		return 0;
	if (!appendFile(out, out_size, setting->ember_dir, "ember.elf"))
		return 0;
	return readableElf(out);
}

/*
 * Prefer an ember.elf already inside the selected game's own umbrella.
 * If it is absent, a configured Ember directory can donate its ember.elf to
 * that umbrella. Only the executable is staged: bios.bin is user-supplied and
 * remains explicitly owned by the target umbrella.
 */
static int ensureLocalEmber(const char *umbrella, char *ember_elf, size_t ember_elf_size,
                            char *message, size_t message_size)
{
	char configured[MAX_PATH];

	if (!appendFile(ember_elf, ember_elf_size, umbrella, "ember.elf")) {
		setEmberMessage(message, message_size, "Ember path is too long", umbrella);
		return 0;
	}
	if (readableFile(ember_elf)) {
		if (readableElf(ember_elf))
			return 1;
		setEmberMessage(message, message_size,
		                "The target umbrella already contains an invalid ember.elf", ember_elf);
		return 0;
	}

	if (!configuredEmberElf(configured, sizeof(configured))) {
		setEmberMessage(message, message_size,
		                "ember.elf is missing; set an Ember directory in Settings", ember_elf);
		return 0;
	}
	if (samePath(configured, ember_elf)) {
		setEmberMessage(message, message_size, "Configured ember.elf could not be read", configured);
		return 0;
	}
	if (!copyElf(configured, ember_elf) || !readableElf(ember_elf)) {
		setEmberMessage(message, message_size,
		                "Could not stage configured ember.elf into the game's umbrella", ember_elf);
		return 0;
	}
	return 1;
}

int LaunchEmberGame(const char *path, char *message, size_t message_size)
{
	POPSTARTER_CANDIDATE_LIST candidates;
	char umbrella[MAX_PATH], ember_elf[MAX_PATH], bios_path[MAX_PATH];
	char game_name[EMBER_NAME_MAX + 1];
	char selected[MAX_PATH], fullpath[MAX_PATH], party[MAX_PATH];
	int exec_kind, ret;

	if (!parseEmberGamePath(path, umbrella, sizeof(umbrella),
	                        game_name, sizeof(game_name))) {
		setEmberMessage(message, message_size,
		                "Ember images must be directly inside <Ember directory>/games/<Game>/", path);
		return 0;
	}
	if (!readableFile(path)) {
		setEmberMessage(message, message_size, "Ember game image could not be read", path);
		return 0;
	}
	bios_path[0] = '\0';
	if (!appendFile(bios_path, sizeof(bios_path), umbrella, "bios.bin")) {
		setEmberMessage(message, message_size, "Ember BIOS path is too long", umbrella);
		return 0;
	}
	if (!validBios(bios_path)) {
		setEmberMessage(message, message_size,
		                "Ember needs a user-supplied 512 KB bios.bin in this game's umbrella", bios_path);
		return 0;
	}
	if (!ensureLocalEmber(umbrella, ember_elf, sizeof(ember_elf), message, message_size))
		return 0;

	initPopstarterCandidates(&candidates);
	addPopstarterCandidate(&candidates, ember_elf);
	ret = preparePopstarterCandidateLaunch(&candidates,
	                                       selected, sizeof(selected),
	                                       fullpath, sizeof(fullpath),
	                                       party, sizeof(party), &exec_kind);
	if (ret != POPSTARTER_OK) {
		setEmberMessage(message, message_size, "Ember executable could not be prepared", ember_elf);
		return 0;
	}

	/*
	 * Ember derives cwd from target argv[0] and joins argv[1..] into the bare
	 * game-folder name. Preserve the resolved live path as argv[0], add exactly
	 * one game argument, and never reset/tear down the IOP storage stack.
	 */
	LaunchArgsClear();
	if (LaunchArgsLoadFromBuffer("Ember PS1 game", game_name, strlen(game_name),
	                             message, message_size) <= 0)
		return 0;

	if (!CleanUpForEmberExec()) {
		LaunchArgsClear();
		setEmberMessage(message, message_size,
		                "Ember launch cancelled while finishing background I/O", NULL);
		return 0;
	}

	/*
	 * Hardware has now proven RiptOPL's R1 failure mode on USB: Ember reaches
	 * its own pad setup but reports "pad init rc=-1" when the launcher's
	 * homebrew SIO2MAN/PADMAN remain resident. padEnd() only releases our EE
	 * RPC client; it cannot remove those IRX modules. For USB, rebuild a
	 * minimal pad-free IOP (IOMANX/fileXio/USBD/BDM/BDMFS/USBMASS) and let
	 * Ember load rom0:SIO2MAN + rom0:PADMAN itself, just like its launcher.
	 */
	if (isUsbEmberPath(ember_elf) && !prepareEmberCleanUsbIop(ember_elf)) {
		DPRINTF("[EMBER] clean USB IOP bootstrap failed; returning to OSDSYS\n");
		LaunchArgsClear();
		ExecOSD(0, NULL);
		return 0;
	}

	RunLoaderElfKeepIOPWithArgv0(fullpath, party, fullpath, exec_kind);
	return 1;
}
