#include "popstarter_internal.h"
#include "gui_neutrino.h"
#include "dash.h"
#include "neutrino_gameid.h"
#include "main_gameid.h"
#include "init.h"

#define NEUTRINO_ISO_PVD_OFFSET (16 * 2048)

static void setMessage(char *message, size_t message_size, const char *text, const char *path)
{
	if (message == NULL || message_size == 0)
		return;
	if (path != NULL && path[0] != '\0')
		snprintf(message, message_size, "%s: %s", text, path);
	else
		snprintf(message, message_size, "%s", text);
}

int IsNeutrinoIsoPath(const char *path)
{
	return path != NULL && genCmpFileExt(path, "ISO");
}

static int fileExists(const char *path)
{
	int fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;
	genClose(fd);
	return 1;
}

static int neutrinoElfReadable(const char *path)
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

static int neutrinoInstallComplete(const char *elf_path)
{
	char base[MAX_PATH], check[MAX_PATH];
	char *slash;
	int len;

	if (elf_path == NULL || elf_path[0] == '\0' || !neutrinoElfReadable(elf_path))
		return 0;

	snprintf(base, sizeof(base), "%s", elf_path);
	slash = strrchr(base, '/');
	if (slash == NULL)
		slash = strrchr(base, ':');
	if (slash == NULL)
		return 1; /* custom path with no derivable cwd: let Neutrino decide */
	slash[1] = '\0';

	len = snprintf(check, sizeof(check), "%sconfig/system.toml", base);
	if (len > 0 && len < (int)sizeof(check) && fileExists(check))
		return 1;
	len = snprintf(check, sizeof(check), "%ssystem.toml", base);
	return len > 0 && len < (int)sizeof(check) && fileExists(check);
}

static int addCompleteCandidate(POPSTARTER_CANDIDATE_LIST *candidates, const char *path)
{
	if (!neutrinoInstallComplete(path))
		return 0;
	return addPopstarterCandidate(candidates, path);
}

static int findCompleteOnRoot(const char *root, char *out, size_t out_size)
{
	static const char *const forms[] = {
	    "%s/NEUTRINO/neutrino.elf",
	    "%s/neutrino/neutrino.elf",
	    "%s/NEUTRINO/NEUTRINO.ELF",
	    "%s/neutrino/NEUTRINO.ELF"};
	int i;

	for (i = 0; i < (int)(sizeof(forms) / sizeof(forms[0])); i++) {
		if (snprintf(out, out_size, forms[i], root) >= (int)out_size)
			continue;
		if (neutrinoInstallComplete(out))
			return 1;
	}
	return 0;
}

static int deviceRootCandidate(const char *iso_path, char *out, size_t out_size)
{
	char root[32];
	const char *colon;
	int root_len;

	colon = strchr(iso_path, ':');
	if (colon == NULL)
		return 0;
	root_len = (int)(colon - iso_path) + 1;
	if (root_len <= 1 || root_len >= (int)sizeof(root))
		return 0;
	snprintf(root, sizeof(root), "%.*s", root_len, iso_path);
	return findCompleteOnRoot(root, out, out_size);
}

static const char *neutrinoBsdForPath(const char *path)
{
	if (path == NULL)
		return NULL;
	if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3))
		return "usb";
	if (!strncmp(path, "mx4sio", 6))
		return "mx4sio";
	if (!strncmp(path, "mmce", 4))
		return "mmce";
	if (!strncmp(path, "ilink", 5))
		return "ilink";
	if (!strncmp(path, "ata", 3))
		return "ata";
	if (!strncmp(path, "udpfs", 5))
		return "udpfs";
	return NULL;
}

static int validateIso(const char *path, char *opened_path, size_t opened_size)
{
	unsigned char pvd[2048] __attribute__((aligned(16)));
	int fd, rd;

	fd = openPathForRead(path, opened_path, opened_size);
	if (fd < 0)
		return 0;
	if (genLseek(fd, NEUTRINO_ISO_PVD_OFFSET, SEEK_SET) != NEUTRINO_ISO_PVD_OFFSET) {
		genClose(fd);
		return 0;
	}
	rd = genRead(fd, pvd, sizeof(pvd));
	genClose(fd);
	return rd == (int)sizeof(pvd) && pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5);
}

static int buildArgs(const char *bsd, const char *dvd_path, const char *neutrino_path,
                     const NEUTRINO_LAUNCH_OPTIONS *options,
                     char *buffer, size_t buffer_size, char *message, size_t message_size)
{
	char cwd[MAX_PATH], *slash;
	NEUTRINO_OPTION_ARGS option_args;
	int n, appended, option_argc, i;

	snprintf(cwd, sizeof(cwd), "%s", neutrino_path);
	slash = strrchr(cwd, '/');
	if (slash == NULL)
		slash = strrchr(cwd, ':');
	if (slash == NULL) {
		setMessage(message, message_size, "Cannot determine Neutrino working directory", neutrino_path);
		return 0;
	}
	slash[1] = '\0';

	n = snprintf(buffer, buffer_size, "-bsd=%s\n-dvd=%s\n-qb\n-cwd=%s\n", bsd, dvd_path, cwd);
	if (n <= 0 || n >= (int)buffer_size) {
		setMessage(message, message_size, "Neutrino launch arguments are too long", dvd_path);
		return 0;
	}

	/* R3Z Neutrino -gc/-gsm/-dbc/-logo options use our existing keep-IOP
	 * argument buffer, not the older reset-IOP game launcher. */
	option_argc = buildNeutrinoOptionArgs(options, &option_args);
	if (option_argc < 0) {
		setMessage(message, message_size, "Invalid Neutrino launch options", NULL);
		return 0;
	}
	for (i = 0; i < option_argc; i++) {
		appended = snprintf(buffer + n, buffer_size - n, "%s\n", option_args.argv[i]);
		if (appended < 0 || appended >= (int)(buffer_size - n)) {
			setMessage(message, message_size, "Neutrino launch arguments are too long", dvd_path);
			return 0;
		}
		n += appended;
	}
	if (LaunchArgsLoadFromBuffer("Neutrino ISO", buffer, n, message, message_size) <= 0)
		return 0;
	return 1;
}

int LaunchNeutrinoIso(const char *path, char *message, size_t message_size)
{
	POPSTARTER_CANDIDATE_LIST candidates;
	NEUTRINO_LAUNCH_OPTIONS options;
	char gameID[NEUTRINO_GAME_ID_SIZE];
	int have_gameid = 0, tried_gameid = 0;
	char candidate[MAX_PATH];
	char selected[MAX_PATH], fullpath[MAX_PATH], party[MAX_PATH];
	char opened_iso[MAX_PATH], args[1400];
	const char *bsd;
	int exec_kind, ret;

	/* Do not probe the ISO or candidates when the Neutrino route is disabled. */
	if (setting != NULL && setting->disable_neutrino) {
		setMessage(message, message_size, "Neutrino support disabled in Launching settings", NULL);
		return 0;
	}

	if (!IsNeutrinoIsoPath(path)) {
		setMessage(message, message_size, "Not a PS2 ISO image", path);
		return 0;
	}

	bsd = neutrinoBsdForPath(path);
	if (bsd == NULL) {
		setMessage(message, message_size, "Neutrino ISO launching is not supported from this device", path);
		return 0;
	}
	if (!validateIso(path, opened_iso, sizeof(opened_iso))) {
		setMessage(message, message_size, "ISO9660 image could not be read", path);
		return 0;
	}

	/* For dashboard profiles, read the bounded ISO Game ID once before
	 * presenting options. Never mount or switch another storage stack merely
	 * to discover a profile, and never write beside the selected ISO. */
	if (DashIsActive()) {
		tried_gameid = 1;
		have_gameid = prepareNeutrinoGameID(opened_iso, gameID, sizeof(gameID));
	}
	/* Choosing Cancel still happens before any Neutrino ELF preparation. */
	if (!(have_gameid ?
	      showNeutrinoLaunchOptionsForGame(&options, gameID) :
	      showNeutrinoLaunchOptions(&options))) {
		if (message != NULL && message_size > 0)
			message[0] = '\0';
		return 0;
	}

	/* Reuse the preflight Game ID if already read. Classic launches continue
	 * their previous lazy read only when RetroGem or MMCE actually needs it. */
	if (!tried_gameid && (!setting->cdrom_disable_gameid
#ifdef MMCE
	    || isPathDeviceStackLoaded("mmce0:/")
#endif
	))
		have_gameid = prepareNeutrinoGameID(opened_iso, gameID, sizeof(gameID));

	initPopstarterCandidates(&candidates);
	/* Same deterministic policy used by RiptOPL: explicit path, game device,
	 * then the two memory-card slots. Only complete Neutrino installs qualify. */
	if (setting != NULL && setting->neutrino_file[0] != '\0')
		addCompleteCandidate(&candidates, setting->neutrino_file);
	if (deviceRootCandidate(path, candidate, sizeof(candidate)))
		addPopstarterCandidate(&candidates, candidate);
	if (findCompleteOnRoot("mc0:", candidate, sizeof(candidate)))
		addPopstarterCandidate(&candidates, candidate);
	if (findCompleteOnRoot("mc1:", candidate, sizeof(candidate)))
		addPopstarterCandidate(&candidates, candidate);

	if (candidates.count == 0) {
		setMessage(message, message_size, "Neutrino not found (set its path in Settings)", NULL);
		return 0;
	}
	ret = preparePopstarterCandidateLaunch(&candidates, selected, sizeof(selected),
	                                       fullpath, sizeof(fullpath), party, sizeof(party), &exec_kind);
	if (ret != POPSTARTER_OK) {
		setMessage(message, message_size, "Neutrino executable could not be prepared", selected);
		return 0;
	}
	if (!buildArgs(bsd, opened_iso, fullpath, &options, args, sizeof(args), message, message_size))
		return 0;

	/* Neutrino consumes the already-mounted device stack. Drain every
	 * dashboard RPC user and release libpad, but never reset that storage IOP. */
#ifdef MMCE
	/* No MMCE initialization here: only adapt already active hardware, and
	 * protect any slot holding the source ISO or Neutrino executable. */
	if (have_gameid)
		setNeutrinoMmceGameID(gameID, opened_iso, selected);
#endif
	if (!CleanUpForKeepIopExec("Preparing Neutrino"))
		return 0;
	if (have_gameid && !setting->cdrom_disable_gameid)
		displayRetroGemGameID(gameID, 2);
	RunLoaderElfKeepIOP(fullpath, party, selected, exec_kind);
	return 1;
}
