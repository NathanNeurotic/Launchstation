#include "popstarter_internal.h"

#define EMBER_CUE_MAX (64 * 1024)

static int cueToken(char **line, char *out, size_t size)
{
	char *p = *line, *start;
	size_t n;
	while (*p == ' ' || *p == '\t') p++;
	if (*p == '"') {
		start = ++p;
		while (*p && *p != '"') p++;
		if (*p != '"') return 0;
		n = p - start;
		p++;
	} else {
		start = p;
		while (*p && *p != ' ' && *p != '\t') p++;
		n = p - start;
	}
	if (!n || n >= size) return 0;
	memcpy(out, start, n);
	out[n] = '\0';
	*line = p;
	return 1;
}

static int cueRelativeFile(const char *cue, const char *file, char *out, size_t size)
{
	const char *slash = strrchr(cue, '/'), *back = strrchr(cue, '\\');
	int length;
	/* CUE FILE paths are relative. Do not probe another device for an ID. */
	if (!file[0] || strchr(file, ':') || file[0] == '/' || file[0] == '\\')
		return 0;
	if (back && (!slash || back > slash)) slash = back;
	if (!slash) slash = strchr(cue, ':');
	if (!slash) return 0;
	length = snprintf(out, size, "%.*s%s", (int)(slash - cue + 1), cue, file);
	if (length <= 0 || (size_t)length >= size) return 0;
	for (char *p = out; *p; p++)
		if (*p == '\\') *p = '/';
	return 1;
}

/* INDEX 01 is a file-sector offset, not a disc time including the usual
 * 150-sector lead-in. PREGAP and INDEX 00 must not be added to it. */
static int cueDataTrack(char *text, const char *cue, char *bin, size_t bin_size,
                         PS1_IMAGE_READER *image)
{
	char file[MAX_PATH] = "", key[32], value[MAX_PATH], type[32];
	char *line = text;
	int binary = 0, selected = 0;
	if (!strncmp(line, "\xEF\xBB\xBF", 3)) line += 3;
	while (*line) {
		char *end = line, *next, *cursor;
		while (*end && *end != '\r' && *end != '\n') end++;
		next = end;
		while (*next == '\r' || *next == '\n') next++;
		*end = '\0';
		cursor = line;
		if (cueToken(&cursor, key, sizeof(key))) {
			if (!stricmp(key, "FILE")) {
				if (selected) return 0; /* selected track was missing INDEX 01 */
				if (!cueToken(&cursor, file, sizeof(file)) || !cueToken(&cursor, type, sizeof(type))) return 0;
				binary = !stricmp(type, "BINARY");
			} else if (!stricmp(key, "TRACK")) {
				char *number_end;
				long number;
				if (selected) return 0;
				if (!cueToken(&cursor, value, sizeof(value)) || !cueToken(&cursor, type, sizeof(type))) return 0;
				number = strtol(value, &number_end, 10);
				if (*number_end || number < 1 || number > 99) return 0;
				if (!stricmp(type, "AUDIO")) { line = next; continue; }
				if (!binary) return 0;
				if (!stricmp(type, "MODE1/2352") || !stricmp(type, "MODE2/2352")) image->sector_size = 2352;
				else if (!stricmp(type, "MODE1/2048") || !stricmp(type, "MODE2/2048")) image->sector_size = 2048;
				else if (!stricmp(type, "MODE2/2336")) image->sector_size = 2336;
				else return 0;
				selected = 1;
			} else if (selected && !stricmp(key, "INDEX")) {
				unsigned int minute, second, frame;
				char extra;
				if (!cueToken(&cursor, type, sizeof(type)) || !cueToken(&cursor, value, sizeof(value))) return 0;
				if (strcmp(type, "01")) { line = next; continue; }
				if (sscanf(value, "%3u:%2u:%2u%c", &minute, &second, &frame, &extra) != 3 || minute >= 1000 || second >= 60 || frame >= 75)
					return 0;
				image->offset = ((s64)minute * 60 * 75 + second * 75 + frame) * image->sector_size;
				return cueRelativeFile(cue, file, bin, bin_size);
			}
		}
		line = next;
	}
	return 0;
}

static int emberCueGameID(int fd, const char *cue, char *gameID, size_t gameID_len)
{
	char bin[MAX_PATH], *text;
	PS1_IMAGE_READER image = {-1, 0, 0};
	s64 size = genLseek(fd, 0, SEEK_END);
	int done = 0, result = 0;
	if (size <= 0 || size > EMBER_CUE_MAX || genLseek(fd, 0, SEEK_SET) != 0)
		return 0;
	text = malloc((size_t)size + 1);
	if (text == NULL) return 0;
	while (done < size) {
		int n = genRead(fd, text + done, (int)size - done);
		if (n <= 0 || n > size - done) goto finish;
		done += n;
	}
	text[done] = '\0';
	if (!cueDataTrack(text, cue, bin, sizeof(bin), &image)) goto finish;
	image.fd = openPathForRead(bin, NULL, 0);
	if (image.fd < 0) goto finish;
	result = buildPs1ImageGameID(&image, cue, gameID, gameID_len);
	genClose(image.fd);
finish:
	free(text);
	return result;
}

int buildEmberGameID(const char *path, char *gameID, size_t gameID_len)
{
	char cue[MAX_PATH], *dot;
	int fd, result = 0;
	if (gameID == NULL || gameID_len == 0) return 0;
	gameID[0] = '\0';
	if (gameID_len < 12 || path == NULL) return 0;
	if (genCmpFileExt(path, "CUE")) {
		fd = openPathForRead(path, NULL, 0);
		if (fd < 0) return 0;
		result = emberCueGameID(fd, path, gameID, gameID_len);
		genClose(fd);
		return result;
	}
	/* The browser also accepts BIN/EXE. Honour a same-stem CUE's track
 * offset if one exists, rather than treating pregap bytes as sector zero. */
	if (snprintf(cue, sizeof(cue), "%s", path) >= (int)sizeof(cue)) return 0;
	dot = strrchr(cue, '.');
	if (!dot || strlen(dot) != 4) return 0;
	memcpy(dot, ".cue", 5);
	fd = openPathForRead(cue, NULL, 0);
	if (fd < 0) {
		memcpy(dot, ".CUE", 5);
		fd = openPathForRead(cue, NULL, 0);
	}
	if (fd >= 0) {
		result = emberCueGameID(fd, cue, gameID, gameID_len);
		genClose(fd);
		return result;
	}
	if (genCmpFileExt(path, "BIN")) {
		PS1_IMAGE_READER image = {-1, 0, 2352};
		const int sizes[] = {2352, 2048, 2336};
		image.fd = openPathForRead(path, NULL, 0);
		if (image.fd < 0) return 0;
		for (int i = 0; i < 3 && !result; i++) {
			image.sector_size = sizes[i];
			result = buildPs1ImageGameID(&image, path, gameID, gameID_len);
		}
		genClose(image.fd);
	}
	/* A standalone PS-X EXE has no ISO SYSTEM.CNF or PVD timestamp. */
	return result;
}
