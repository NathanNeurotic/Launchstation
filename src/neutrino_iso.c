#include "popstarter_internal.h"

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
	if (slash == NULL) {
		slash = strrchr(base, ':');
		if (slash == NULL)
			return 0;
		slash[1] = '\0';
	} else
		slash[1] = '\0';

	len = snprintf(check, sizeof(check), "%sconfig/system.toml", base);
	if (len <= 0 || len >= (int)sizeof(check) || !fileExists(check))
		return 0;
	len = snprintf(check, sizeof(check), "%smodules/ee_core.elf", base);
	return len > 0 && len < (int)sizeof(check) && fileExists(check);
}

static int addCompleteCandidate(POPSTARTER_CANDIDATE_LIST *candidates, const char *path)
{
	if (!neutrinoInstallComplete(path))
		return 0;
	return addPopstarterCandidate(candidates, path);
}

static int deviceRootCandidate(const char *iso_path, char *out, size_t out_size)
{
	const char *colon;
	int root_len;

	colon = strchr(iso_path, ':');
	if (colon == NULL)
		return 0;
	root_len = (int)(colon - iso_path) + 1;
	if (root_len <= 1 || root_len + 24 >= (int)out_size)
		return 0;
	snprintf(out, out_size, "%.*s/neutrino/neutrino.elf", root_len, iso_path);
	return 1;
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
                     char *buffer, size_t buffer_size, char *message, size_t message_size)
{
	char cwd[MAX_PATH], *slash;
	int n;

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
	if (LaunchArgsLoadFromBuffer("Neutrino ISO", buffer, n, message, message_size) <= 0)
		return 0;
	return 1;
}

int LaunchNeutrinoIso(const char *path, char *message, size_t message_size)
{
	POPSTARTER_CANDIDATE_LIST candidates;
	char candidate[MAX_PATH];
	char selected[MAX_PATH], fullpath[MAX_PATH], party[MAX_PATH];
	char opened_iso[MAX_PATH], args[1400];
	const char *bsd;
	int exec_kind, ret;

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

	initPopstarterCandidates(&candidates);
	/* Same deterministic policy used by RiptOPL: explicit path, game device,
	 * then the two memory-card slots. Only complete Neutrino installs qualify. */
	if (setting != NULL && setting->neutrino_file[0] != '\0')
		addCompleteCandidate(&candidates, setting->neutrino_file);
	if (deviceRootCandidate(path, candidate, sizeof(candidate)))
		addCompleteCandidate(&candidates, candidate);
	addCompleteCandidate(&candidates, "mc0:/neutrino/neutrino.elf");
	addCompleteCandidate(&candidates, "mc1:/neutrino/neutrino.elf");

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
	if (!buildArgs(bsd, opened_iso, fullpath, args, sizeof(args), message, message_size))
		return 0;

	/* Neutrino's quick-boot path consumes the already-mounted device stack.
	 * Never apply the generic reset-IOP preference to this handoff. */
	CleanUpForExec();
	RunLoaderElfKeepIOP(fullpath, party, selected, exec_kind);
	return 1;
}
