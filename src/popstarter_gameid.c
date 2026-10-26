#include "popstarter_internal.h"
#include "main_gameid.h"

static const char *getFinalNameComponent(const char *path)
{
	const char *name;
	const char *sep;

	if (path == NULL)
		return NULL;

	name = path;
	sep = strrchr(path, '/');
	if (sep != NULL)
		name = sep + 1;
	sep = strrchr(path, '\\');
	if (sep != NULL && sep + 1 > name)
		name = sep + 1;
	sep = strrchr(path, ':');
	if (sep != NULL && sep + 1 > name)
		name = sep + 1;

	return name;
}

/* CosmicScale serial conventions, also used by OPL's PS1 RetroGEM resolver.
 * Keep this local to PS1 identity; generic ELF/MMCE names have other contracts. */
static int normalizePs1Serial(const char *raw, char *out, size_t size)
{
	char serial[11];
	size_t i, n = 0;
	int slusp = !prefixCaseCmp(raw, "SLUSP", 5);
	if (size < 12)
		return 0;
	for (i = 0; raw[i] && raw[i] != ';'; i++) {
		unsigned char c = (unsigned char)raw[i];
		if ((slusp && i == 4) || c == '_' || c == '-' || c == '.')
			continue;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) || n == 10)
			return 0;
		serial[n++] = c;
	}
	serial[n] = '\0';
	if (n >= 9 && !strncmp(serial, "LSP", 3)) {
		for (i = 3; i < n; i++)
			if (serial[i] < '0' || serial[i] > '9')
				return 0;
		snprintf(out, size, "%.8s.%s", serial, serial + 8);
		return 1;
	}
	if (n != 9)
		return 0;
	for (i = 4; i < n; i++)
		if (serial[i] < '0' || serial[i] > '9')
			return 0;
	snprintf(out, size, "%.4s_%.3s.%.2s", serial, serial + 4, serial + 7);
	return 1;
}

static int buildGameIDFromPopstarterName(const char *name, char *gameID, size_t gameID_len)
{
	const char *id_name;
	char token[32];
	size_t len;

	id_name = getFinalNameComponent(name);
	if (id_name == NULL || id_name[0] == '\0')
		return 0;

	if (!prefixCaseCmp(id_name, "XX.", 3) ||
	    !prefixCaseCmp(id_name, "__.", 3) ||
	    !prefixCaseCmp(id_name, "PP.", 3))
		id_name += 3;
	len = strcspn(id_name, "; \t\r\n\"");
	if (len < sizeof(token)) {
		memcpy(token, id_name, len);
		token[len] = '\0';
		if (len > 4 && (!stricmp(token + len - 4, ".VCD") ||
		                !stricmp(token + len - 4, ".CUE") ||
		                !stricmp(token + len - 4, ".BIN")))
			token[len - 4] = '\0';
		if (normalizePs1Serial(token, gameID, gameID_len))
			return 1;
	}

	return buildLaunchGameID(id_name, gameID, gameID_len);
}

static int isLikelyTitleID(const char *gameID)
{
	char normalized[12];
	if (gameID != NULL && normalizePs1Serial(gameID, normalized, sizeof(normalized)))
		return 1;
	return (gameID != NULL &&
	        strlen(gameID) >= 11 &&
	        gameID[4] == '_' &&
	        (gameID[7] == '.' || gameID[8] == '.'));
}

static int buildVcdPvdTimestampGameID(const char *path, char *gameID, size_t gameID_len)
{
	unsigned char pvd[POPSTARTER_CD_SECTOR_DATA_SIZE] __attribute__((aligned(16)));
	int fd;

	if (gameID == NULL || gameID_len == 0)
		return 0;
	gameID[0] = '\0';

	fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;

	if (!readVcdIsoPrimaryVolumeDescriptor(fd, pvd)) {
		genClose(fd);
		return 0;
	}
	genClose(fd);

	return buildPS1GenericGameIDFromTimestamp((const char *)pvd + PS1_PVD_VOLUME_TIMESTAMP_OFFSET,
	                                          gameID, gameID_len);
}

static int lineStartsWithBootKey(const char *line)
{
	const char *p;

	if (line == NULL)
		return 0;

	p = line;
	while (*p == ' ' || *p == '\t')
		p++;
	if (prefixCaseCmp(p, "BOOT", 4))
		return 0;
	p += 4;
	return (*p == '=' || *p == ' ' || *p == '\t');
}

static int parseSystemCnfBootGameID(const char *cnf, char *gameID, size_t gameID_len)
{
	char boot_path[MAX_PATH];
	const char *line;
	const char *line_end;
	const char *value;
	size_t value_len;

	if (cnf == NULL || gameID == NULL || gameID_len == 0)
		return 0;

	gameID[0] = '\0';
	line = cnf;
	while (*line != '\0') {
		line_end = line;
		while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n')
			line_end++;

		if (lineStartsWithBootKey(line)) {
			value = line + strcspn(line, "=");
			if (value < line_end && *value == '=') {
				value++;
				while (value < line_end && (*value == ' ' || *value == '\t'))
					value++;

				value_len = 0;
				while (value + value_len < line_end &&
				       value[value_len] != ' ' &&
				       value[value_len] != '\t')
					value_len++;
				if (value_len > 0 && value_len < sizeof(boot_path)) {
					memcpy(boot_path, value, value_len);
					boot_path[value_len] = '\0';
					if (buildGameIDFromPopstarterName(boot_path, gameID, gameID_len) &&
					    isLikelyTitleID(gameID))
						return 1;
					gameID[0] = '\0';
				}
			}
		}

		line = line_end;
		while (*line == '\r' || *line == '\n')
			line++;
	}

	return 0;
}

static int buildVcdSystemCnfGameID(const char *path, char *gameID, size_t gameID_len)
{
	char cnf[POPSTARTER_CD_SECTOR_DATA_SIZE + 1];
	int fd;
	int ret;

	if (gameID == NULL || gameID_len == 0)
		return 0;
	gameID[0] = '\0';

	fd = openPathForRead(path, NULL, 0);
	if (fd < 0)
		return 0;

	ret = 0;
	if (readVcdSystemCnf(fd, cnf, sizeof(cnf)))
		ret = parseSystemCnfBootGameID(cnf, gameID, gameID_len);
	genClose(fd);

	return ret;
}

int buildPopstarterVcdGameID(const char *path, char *gameID, size_t gameID_len)
{
	char hdd_device[6];
	char partition[MAX_PART_NAME + 1];
	const char *subpath;
	int ret;

	if (gameID == NULL || gameID_len == 0)
		return 0;
	gameID[0] = '\0';

	if (buildVcdSystemCnfGameID(path, gameID, gameID_len))
		return 1;
	if (buildVcdPvdTimestampGameID(path, gameID, gameID_len))
		return 1;

	ret = splitPopstarterHddLaunchPath(path, hdd_device, sizeof(hdd_device), partition, sizeof(partition), &subpath);
	if (ret > 0) {
		if (!strncmp(partition, "__.POPS", 7))
			return buildGameIDFromPopstarterName(subpath, gameID, gameID_len);
		if ((!strncmp(partition, "__.", 3) || !prefixCaseCmp(partition, "PP.", 3)) &&
		    subpath != NULL && !stricmp(subpath, "/IMAGE0.VCD"))
			return buildGameIDFromPopstarterName(partition, gameID, gameID_len);
	}

	return buildGameIDFromPopstarterName(path, gameID, gameID_len);
}

/* Same serial, timestamp database and name fallback as a VCD of this disc. */
int buildPs1ImageGameID(const PS1_IMAGE_READER *image, const char *fallback,
                       char *gameID, size_t gameID_len)
{
	char cnf[POPSTARTER_CD_SECTOR_DATA_SIZE + 1];
	unsigned char pvd[POPSTARTER_CD_SECTOR_DATA_SIZE] __attribute__((aligned(16)));
	if (gameID == NULL || gameID_len < 12 || image == NULL)
		return 0;
	gameID[0] = '\0';
	if (readPs1SystemCnf(image, cnf, sizeof(cnf)) &&
	    parseSystemCnfBootGameID(cnf, gameID, gameID_len))
		return 1;
	/* Require a real ISO filesystem before using timestamp/name fallbacks. */
	if (!readPs1IsoPrimaryVolumeDescriptor(image, pvd))
		return 0;
	if (buildPS1GenericGameIDFromTimestamp((const char *)pvd + PS1_PVD_VOLUME_TIMESTAMP_OFFSET,
	                                     gameID, gameID_len))
		return 1;
	return buildGameIDFromPopstarterName(fallback, gameID, gameID_len);
}
