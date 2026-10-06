#include "popstarter_internal.h"

#define EMBER_NAME_MAX 180
#define EMBER_BIOS_SIZE (512 * 1024)

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

/*
 * Ember's working layout is:
 *   <device>/EMBER/ember.elf
 *   <device>/EMBER/bios.bin
 *   <device>/EMBER/games/<Game>/<disc image>
 *
 * argv[1] is the bare <Game> directory name. A loose CUE or a nested image may
 * boot in Ember itself, but its MC1.vmc/MC2.vmc resolution no longer follows
 * the selected image. Refuse those layouts so every File Browser entry we
 * offer can save in the same folder it launched from.
 */
static int parseEmberGamePath(const char *path,
                              char *ember_elf, size_t ember_elf_size,
                              char *bios_path, size_t bios_path_size,
                              char *game_name, size_t game_name_size)
{
	const char *p, *ember, *games, *game, *game_end, *image;
	size_t ember_len, game_len;
	char sep;

	if (path == NULL || strchr(path, ':') == NULL || !emberExt(path))
		return 0;

	ember = NULL;
	for (p = path; *p != '\0'; p++) {
		if (!isSep(*p))
			continue;
		if (!segmentCaseEqual(p + 1, "EMBER"))
			continue;
		ember = p + 1;
		break;
	}
	if (ember == NULL)
		return 0;

	games = ember + 5;
	if (!isSep(*games) || !segmentCaseEqual(games + 1, "games"))
		return 0;
	game = games + 1 + 5;
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

	if (game_name != NULL && game_name_size > 0) {
		if (game_len >= game_name_size)
			return 0;
		memcpy(game_name, game, game_len);
		game_name[game_len] = '\0';
	}

	sep = *games;
	ember_len = (size_t)((ember + 5) - path);
	if (ember_elf != NULL && ember_elf_size > 0) {
		if (ember_len + 1 + strlen("ember.elf") + 1 > ember_elf_size)
			return 0;
		snprintf(ember_elf, ember_elf_size, "%.*s%cember.elf", (int)ember_len, path, sep);
	}
	if (bios_path != NULL && bios_path_size > 0) {
		if (ember_len + 1 + strlen("bios.bin") + 1 > bios_path_size)
			return 0;
		snprintf(bios_path, bios_path_size, "%.*s%cbios.bin", (int)ember_len, path, sep);
	}
	return 1;
}

int IsEmberGamePath(const char *path)
{
	return parseEmberGamePath(path, NULL, 0, NULL, 0, NULL, 0);
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

int LaunchEmberGame(const char *path, char *message, size_t message_size)
{
	POPSTARTER_CANDIDATE_LIST candidates;
	char ember_elf[MAX_PATH], bios_path[MAX_PATH], game_name[EMBER_NAME_MAX + 1];
	char selected[MAX_PATH], fullpath[MAX_PATH], party[MAX_PATH];
	int exec_kind, ret;

	if (!parseEmberGamePath(path,
	                        ember_elf, sizeof(ember_elf),
	                        bios_path, sizeof(bios_path),
	                        game_name, sizeof(game_name))) {
		setEmberMessage(message, message_size,
		                "Ember games must be directly inside EMBER/games/<Game>/", path);
		return 0;
	}
	if (!readableFile(path)) {
		setEmberMessage(message, message_size, "Ember game image could not be read", path);
		return 0;
	}
	if (!validBios(bios_path)) {
		setEmberMessage(message, message_size,
		                "Ember needs a user-supplied 512 KB bios.bin beside ember.elf", bios_path);
		return 0;
	}

	initPopstarterCandidates(&candidates);
	addPopstarterCandidate(&candidates, ember_elf);
	ret = preparePopstarterCandidateLaunch(&candidates,
	                                       selected, sizeof(selected),
	                                       fullpath, sizeof(fullpath),
	                                       party, sizeof(party), &exec_kind);
	if (ret != POPSTARTER_OK) {
		setEmberMessage(message, message_size, "Ember executable was not found or is invalid", ember_elf);
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

	CleanUpForExec();
	RunLoaderElfKeepIOPWithArgv0(fullpath, party, fullpath, exec_kind);
	return 1;
}
