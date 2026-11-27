//--------------------------------------------------------------
//File name:   riptopl_iso.c
//Start a PS2 ISO through RiptOPL's boot-argument launch.
//A separate loader choice next to Neutrino: RiptOPL runs the game on its own core, which carries the
//pad emulator, so a USB pad works inside the game.
//--------------------------------------------------------------
#include "popstarter_internal.h"
#include <ctype.h>
#include "neutrino_gameid.h"
#include "riptopl_iso.h"

static void setMessage(char *message, size_t message_size, const char *text, const char *detail)
{
	if (message == NULL || message_size == 0)
		return;
	if (detail != NULL && detail[0] != '\0')
		snprintf(message, message_size, "%s: %s", text, detail);
	else
		snprintf(message, message_size, "%s", text);
}

static int elfReadable(const char *path)
{
	unsigned char ident[4];
	int fd, rd;

	fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;
	rd = genRead(fd, ident, sizeof(ident));
	genClose(fd);
	return rd == (int)sizeof(ident) && ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
}

/* RiptOPL's own BDM autolaunch word for the device the ISO is on, or NULL when it cannot start from it.
 * "bdm" covers every block-device transport; "ata" is its faster internal-HDD route. MMCE, UDPFS, SMB
 * and NFS are not block devices to RiptOPL's boot-argument launch. */
static const char *bdmKindForPath(const char *path)
{
	if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3) || !strncmp(path, "mx4sio", 6) ||
	    !strncmp(path, "ilink", 5))
		return "bdm";
	if (!strncmp(path, "ata", 3))
		return "ata";
	return NULL;
}

/* Case-insensitive compare of a directory name (not NUL-terminated) with an upper-case word. */
static int sameDir(const char *s, size_t len, const char *want)
{
	size_t i;

	if (strlen(want) != len)
		return 0;
	for (i = 0; i < len; i++)
		if (toupper((unsigned char)s[i]) != (unsigned char)want[i])
			return 0;
	return 1;
}

/* RiptOPL finds the ISO itself, at <device>:/DVD/<name> or <device>:/CD/<name>. Reads which of the two
 * the file is in; anything deeper or elsewhere is not found by its boot-argument launch. */
static int mediaForPath(const char *path, const char **media, const char **name)
{
	const char *colon = strchr(path, ':');
	const char *dir, *slash;
	size_t len;

	if (colon == NULL || colon[1] != '/')
		return 0;
	dir = colon + 2; /* first component after "dev:/" */
	slash = strchr(dir, '/');
	if (slash == NULL)
		return 0; /* file at the device root */
	len = (size_t)(slash - dir);
	if (sameDir(dir, len, "DVD"))
		*media = "DVD";
	else if (sameDir(dir, len, "CD"))
		*media = "CD";
	else
		return 0;
	if (strchr(slash + 1, '/') != NULL || slash[1] == '\0')
		return 0; /* a subfolder inside DVD/ or CD/ */
	*name = slash + 1;
	return 1;
}

static int findOnRoot(const char *root, char *out, size_t out_size)
{
	static const char *const forms[] = {
	    "%s/APPS/RIPTOPL-GIP/RIPTOPL.ELF",
	    "%s/APPS/RIPTOPL/RIPTOPL.ELF",
	    "%s/RIPTOPL/RIPTOPL.ELF",
	    "%s/APPS/RIPTOPL.ELF",
	    "%s/RIPTOPL.ELF"};
	int i;

	for (i = 0; i < (int)(sizeof(forms) / sizeof(forms[0])); i++) {
		if (snprintf(out, out_size, forms[i], root) >= (int)out_size)
			continue;
		if (elfReadable(out))
			return 1;
	}
	return 0;
}

/* RIPTOPL.ELF from the setting first; then the game's device; then the two memory cards. */
static int findRiptopl(const char *iso_path, char *out, size_t out_size)
{
	char root[32];
	const char *colon = strchr(iso_path, ':');
	int root_len;

	if (setting != NULL && setting->riptopl_file[0] != '\0' && elfReadable(setting->riptopl_file)) {
		snprintf(out, out_size, "%s", setting->riptopl_file);
		return 1;
	}
	if (colon != NULL) {
		root_len = (int)(colon - iso_path) + 1;
		if (root_len > 1 && root_len < (int)sizeof(root)) {
			snprintf(root, sizeof(root), "%.*s", root_len, iso_path);
			if (findOnRoot(root, out, out_size))
				return 1;
		}
	}
	return findOnRoot("mc0:", out, out_size) || findOnRoot("mc1:", out, out_size);
}

int RiptoplPrepareIsoLaunch(const char *iso_path, char *elf_path, size_t elf_size, char *message, size_t message_size)
{
	char gameID[NEUTRINO_GAME_ID_SIZE], args[600];
	const char *kind, *media = NULL, *name = NULL;
	int n;

	if (!IsNeutrinoIsoPath(iso_path)) {
		setMessage(message, message_size, "Not a PS2 ISO image", iso_path);
		return 0;
	}
	kind = bdmKindForPath(iso_path);
	if (kind == NULL) {
		setMessage(message, message_size, "RiptOPL starts games from USB, MX4SIO, iLink or the internal HDD only", iso_path);
		return 0;
	}
	if (!mediaForPath(iso_path, &media, &name)) {
		setMessage(message, message_size, "RiptOPL needs the ISO directly inside the DVD or CD folder of the device", iso_path);
		return 0;
	}
	if (!prepareNeutrinoGameID(iso_path, gameID, sizeof(gameID))) {
		setMessage(message, message_size, "The game ID could not be read from this ISO", iso_path);
		return 0;
	}
	if (!findRiptopl(iso_path, elf_path, elf_size)) {
		setMessage(message, message_size, "RIPTOPL.ELF not found (put it in APPS/RIPTOPL on the game's device)", NULL);
		return 0;
	}

	/* One argument per line: file name with extension, game ID, media, device kind. RiptOPL reads
	 * exactly these four after its own path (see autoLaunchBDMGame). */
	n = snprintf(args, sizeof(args), "%s\n%s\n%s\n%s\n", name, gameID, media, kind);
	if (n <= 0 || n >= (int)sizeof(args)) {
		setMessage(message, message_size, "RiptOPL launch arguments are too long", name);
		return 0;
	}
	if (LaunchArgsLoadFromBuffer("RiptOPL", args, n, message, message_size) <= 0)
		return 0;

	/* LaunchArgsLoadFromBuffer leaves a "loaded N arguments" note; only an error should reach the user. */
	if (message != NULL && message_size > 0)
		message[0] = '\0';
	return 1;
}

/* The ELF for a disc: the disc-specific path in Settings, else the general RiptOPL path, else the memory
 * cards (a disc has no device to search). */
int RiptoplFindElf(char *elf_path, size_t elf_size)
{
	if (setting != NULL && setting->riptopl_disc_file[0] != '\0' && elfReadable(setting->riptopl_disc_file)) {
		snprintf(elf_path, elf_size, "%s", setting->riptopl_disc_file);
		return 1;
	}
	if (setting != NULL && setting->riptopl_file[0] != '\0' && elfReadable(setting->riptopl_file)) {
		snprintf(elf_path, elf_size, "%s", setting->riptopl_file);
		return 1;
	}
	return findOnRoot("mc0:", elf_path, elf_size) || findOnRoot("mc1:", elf_path, elf_size);
}

/* Launch argument for a disc: RiptOPL boots the disc in its drive with the pad settings in arg_line. */
int RiptoplPrepareDiscLaunch(char *elf_path, size_t elf_size, const char *arg_line, char *message, size_t message_size)
{
	char buf[96];
	int len;

	if (!RiptoplFindElf(elf_path, elf_size)) {
		setMessage(message, message_size, "RIPTOPL.ELF not found (set its path in Settings > Disc & image loaders)", NULL);
		return 0;
	}
	len = snprintf(buf, sizeof(buf), "%s\n", (arg_line != NULL && arg_line[0] != '\0') ? arg_line : "disc");
	if (len <= 0 || len >= (int)sizeof(buf))
		return 0;
	if (LaunchArgsLoadFromBuffer("RiptOPL disc", buf, len, message, message_size) <= 0)
		return 0;
	if (message != NULL && message_size > 0)
		message[0] = '\0';
	return 1;
}