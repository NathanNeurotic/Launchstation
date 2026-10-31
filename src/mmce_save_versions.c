//--------------------------------------------------------------
//File name:   mmce_save_versions.c
//Per-save versions of MMCE card backups. After a card's history copy is
//verified, each save on it is exported as a .psu into
//<card history>/WLE-Saves/<save>/WLE-<stamp>.psu, where <stamp> is the
//history copy's own, and only when the save's bytes differ from the newest
//version kept. The cloud sync uploads these with the card copies.
//
//Only the history copy is read, never the live MMCE card, and one file is
//open at a time. Uses the card viewer and the .psu writer from
//mc_card_view.c / mc_card_write.c; no GPL code was used.
//--------------------------------------------------------------
#include "launchelf.h"
#include "mc_card_view.h"
#include "mc_card_write.h"
#include "mc_sha256.h"
#include "mmce_save_versions.h"

extern int fileMode;

#define SV_IO 65536
#define SV_MARKER "SOURCE.TXT"

static McView sv_view;
static u8 sv_io[SV_IO] __attribute__((aligned(64)));

static int svJoin(char *out, int out_size, const char *a, const char *b)
{
	int n = (int)strlen(a);

	if (n > 0 && a[n - 1] != '/' && a[n - 1] != ':')
		return snprintf(out, out_size, "%s/%s", a, b) < out_size;
	return snprintf(out, out_size, "%s%s", a, b) < out_size;
}

static int svDirExists(const char *path)
{
	char probe[MAX_PATH];
	int fd;

	snprintf(probe, sizeof(probe), "%s", path);
	fd = genDopen(probe);
	if (fd < 0)
		return 0;
	genDclose(fd);
	return 1;
}

static int svMkdir(const char *path)
{
	return svDirExists(path) || genMkdir(path, fileMode) >= 0 || svDirExists(path) ? 0 : -1;
}

/* B?DATA-SYSTEM: the console's own folders, rewritten as it pleases. */
static int svSystemFolder(const char *name)
{
	return strlen(name) == 13 && name[0] == 'B' && !strcmp(name + 2, "DATA-SYSTEM");
}

static int svVersionName(const char *name)
{
	return !strncmp(name, "WLE-", 4) && genCmpFileExt(name, "psu");
}

void mmceSaveVersionFolder(const char *save_name, char *out, int out_size)
{
	char clean[40];
	int i, changed = 0;

	for (i = 0; save_name[i] && i < 32; i++) {
		char c = save_name[i];
		int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
		         c == '_' || c == '.';
		clean[i] = ok ? c : '_';
		changed |= !ok;
	}
	clean[i] = '\0';
	/* no empty name, no "." or "..", no trailing dot (FAT drops it) */
	if (i == 0 || clean[i - 1] == '.' || !strcmp(clean, "."))
		changed = 1;
	if (changed) {
		McSha256 h;
		u8 d[32];

		mcSha256Init(&h);
		mcSha256Update(&h, save_name, strlen(save_name));
		mcSha256Final(&h, d);
		snprintf(out, out_size, "%s~%02x%02x%02x%02x", clean, d[0], d[1], d[2], d[3]);
	} else
		snprintf(out, out_size, "%s", clean);
}

/* ---- the history copy, read through one open file ---- */

static int svRead(void *ctx, uint64_t offset, void *buf, uint32_t len)
{
	int fd = *(int *)ctx;
	u8 *p = buf;

	if (genLseek(fd, (s64)offset, SEEK_SET) != (s64)offset)
		return -1;
	while (len > 0) {
		int n = genRead(fd, p, len > SV_IO ? SV_IO : (int)len);
		if (n <= 0)
			return -1;
		p += n;
		len -= (uint32_t)n;
	}
	return 0;
}

static int svOpenCard(const char *history, int *fd)
{
	McViewSource src;
	s64 size;

	*fd = genOpen(history, FIO_O_RDONLY);
	if (*fd < 0)
		return MCV_ERR_IO;
	size = genLseek(*fd, 0, SEEK_END);
	if (size <= 0) {
		genClose(*fd);
		*fd = -1;
		return MCV_ERR_IO;
	}
	src.read = svRead;
	src.ctx = fd;
	src.size = (uint64_t)size;
	if (mcViewOpen(&sv_view, &src) != MCV_OK) {
		genClose(*fd);
		*fd = -1;
		return MCV_ERR_CORRUPT;
	}
	return MCV_OK;
}

/* ---- files ---- */

static int svHashFile(const char *path, u8 out[32], s64 *size_out)
{
	McSha256 h;
	s64 total = 0;
	int fd, n;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return -1;
	mcSha256Init(&h);
	while ((n = genRead(fd, sv_io, SV_IO)) > 0) {
		mcSha256Update(&h, sv_io, (size_t)n);
		total += n;
	}
	genClose(fd);
	if (n < 0)
		return -1;
	mcSha256Final(&h, out);
	if (size_out)
		*size_out = total;
	return 0;
}

/* Newest WLE-*.psu in dir (names sort by stamp): 1 found, 0 none, -1 error. */
static int svNewest(const char *dir, char *name, int name_size, int *count)
{
	iox_dirent_t ent;
	char path[MAX_PATH];
	int fd;

	name[0] = '\0';
	*count = 0;
	snprintf(path, sizeof(path), "%s", dir);
	fd = genDopen(path);
	if (fd < 0)
		return -1;
	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (!FIO_S_ISDIR(ent.stat.mode) && svVersionName(ent.name)) {
			(*count)++;
			if (name[0] == '\0' || strcmp(ent.name, name) > 0)
				snprintf(name, name_size, "%s", ent.name);
		}
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return name[0] != '\0';
}

/* Removes the oldest versions until keep remain; fresh is never removed. */
static int svPrune(const char *dir, int keep, const char *fresh)
{
	iox_dirent_t ent;
	char scan[MAX_PATH], oldest[MAX_NAME], path[MAX_PATH];
	int fd, count, guard;

	for (guard = 0; keep > 0 && guard < 1024; guard++) {
		count = 0;
		oldest[0] = '\0';
		snprintf(scan, sizeof(scan), "%s", dir);
		fd = genDopen(scan);
		if (fd < 0)
			return -1;
		memset(&ent, 0, sizeof(ent));
		while (fileXioDread(fd, &ent) > 0) {
			if (!FIO_S_ISDIR(ent.stat.mode) && svVersionName(ent.name)) {
				count++;
				if (strcmp(ent.name, fresh) && (oldest[0] == '\0' || strcmp(ent.name, oldest) < 0))
					snprintf(oldest, sizeof(oldest), "%s", ent.name);
			}
			memset(&ent, 0, sizeof(ent));
		}
		genDclose(fd);
		if (count <= keep || oldest[0] == '\0')
			return 0;
		if (!svJoin(path, sizeof(path), dir, oldest) || genRemove(path) < 0)
			return -1;
	}
	return 0;
}

/* Writes buf as dir/name: staged as .PART, read back and hashed, then
 * renamed. An existing file is never replaced. 0, or -1 with nothing left. */
static int svPublish(const char *dir, const char *name, const u8 *buf, uint32_t len, const u8 sha[32])
{
	char final[MAX_PATH], stage[MAX_PATH];
	u8 back[32];
	s64 back_size = -1;
	uint32_t done = 0;
	int fd, ok = 1;

	if (!svJoin(final, sizeof(final), dir, name) ||
	    snprintf(stage, sizeof(stage), "%s.PART", final) >= (int)sizeof(stage))
		return -1;
	if (wleExists(final))
		return -1;
	genRemove(stage); /* an interrupted earlier attempt: never published */
	fd = genOpen(stage, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
	if (fd < 0)
		return -1;
	while (ok && done < len) {
		int chunk = len - done > SV_IO ? SV_IO : (int)(len - done);
		ok = genWrite(fd, (void *)(buf + done), chunk) == chunk;
		done += (uint32_t)chunk;
	}
	if (genClose(fd) < 0)
		ok = 0;
	if (ok)
		ok = svHashFile(stage, back, &back_size) == 0 && back_size == (s64)len && !memcmp(back, sha, 32);
	if (ok)
		ok = genRenameNoReplace(stage, final) >= 0;
	if (!ok) {
		genRemove(stage);
		return -1;
	}
	return 0;
}

static int svReadMarker(const char *saves_dir, char *out, int out_size)
{
	char path[MAX_PATH];
	int fd, n;

	out[0] = '\0';
	if (!svJoin(path, sizeof(path), saves_dir, SV_MARKER))
		return -1;
	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return -1;
	n = genRead(fd, out, out_size - 1);
	genClose(fd);
	if (n < 0)
		n = 0;
	out[n] = '\0';
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
		out[--n] = '\0';
	return 0;
}

static void svWriteMarker(const char *saves_dir, const char *history_name)
{
	char path[MAX_PATH], line[MAX_NAME + 2];
	int fd, n;

	if (!svJoin(path, sizeof(path), saves_dir, SV_MARKER))
		return;
	n = snprintf(line, sizeof(line), "%s\n", history_name);
	fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
	if (fd < 0)
		return;
	if (genWrite(fd, line, n) != n) {
		genClose(fd);
		genRemove(path); /* a torn marker would only cost a re-check, but leave none */
		return;
	}
	genClose(fd);
}

/* ---- one save ---- */

/* 1 versioned, 0 unchanged, 2 skipped, -1 failed. */
static int svOneSave(const char *history, const char *saves_dir, const char *save_name, const char *stamp, int keep)
{
	McSaveData s;
	McSha256 h;
	char folder[48], dir[MAX_PATH], newest[MAX_NAME], newest_path[MAX_PATH], name[MAX_NAME];
	u8 sha[32], old_sha[32];
	u8 *psu = NULL;
	uint32_t len = 0;
	s64 old_size = -1;
	int fd, r, count, i;

	r = svOpenCard(history, &fd);
	if (r != MCV_OK)
		return -1;
	r = mcCardReadSave(&sv_view, save_name, &s);
	genClose(fd);
	if (r == MCW_ERR_UNSUPPORTED)
		return 2;
	if (r != MCW_OK)
		return -1;
	if (mcCardPsuSize(&s) > MMCE_SAVE_VERSION_MAX) {
		mcCardFreeSave(&s);
		return 2;
	}
	r = mcCardSaveToPsu(&s, &psu, &len);
	mcCardFreeSave(&s);
	if (r == MCW_ERR_UNSUPPORTED)
		return 2;
	if (r != MCW_OK || psu == NULL)
		return -1;
	mcSha256Init(&h);
	mcSha256Update(&h, psu, len);
	mcSha256Final(&h, sha);

	mmceSaveVersionFolder(save_name, folder, sizeof(folder));
	if (!svJoin(dir, sizeof(dir), saves_dir, folder) || svMkdir(dir) < 0) {
		free(psu);
		return -1;
	}
	r = svNewest(dir, newest, sizeof(newest), &count);
	if (r < 0) {
		free(psu);
		return -1;
	}
	/* Same bytes as the newest version: nothing to keep. A version that
	 * cannot be read is not proof of a change; it is a failure. */
	if (r > 0) {
		if (!svJoin(newest_path, sizeof(newest_path), dir, newest) ||
		    svHashFile(newest_path, old_sha, &old_size) < 0) {
			free(psu);
			return -1;
		}
		if (old_size == (s64)len && !memcmp(old_sha, sha, 32)) {
			free(psu);
			return 0;
		}
	}
	for (i = 0, r = -1; i < 100 && r < 0; i++) {
		char probe[MAX_PATH];

		if (i == 0)
			snprintf(name, sizeof(name), "WLE-%s.psu", stamp);
		else
			snprintf(name, sizeof(name), "WLE-%s-%02d.psu", stamp, i);
		if (!svJoin(probe, sizeof(probe), dir, name))
			break;
		if (wleExists(probe))
			continue;
		r = svPublish(dir, name, psu, len, sha);
		if (r < 0)
			break;
	}
	free(psu);
	if (r < 0)
		return -1;
	/* retention trims only committed versions, never the new one */
	svPrune(dir, keep, name);
	return 1;
}

/* ---- the card ---- */

int mmceSaveVersionsFromHistory(const char *history, const char *saves_dir, int keep, MmceSaveVersionStats *st)
{
	McViewSave *saves = NULL;
	char history_name[MAX_NAME], stamp[64], marker[MAX_NAME];
	const char *base, *dot;
	int fd, n, total = 0, i, r;

	if (history == NULL || saves_dir == NULL || st == NULL || keep < 0)
		return -1;
	memset(st, 0, sizeof(*st));
	base = strrchr(history, '/');
	base = base ? base + 1 : (strchr(history, ':') ? strchr(history, ':') + 1 : history);
	snprintf(history_name, sizeof(history_name), "%s", base);
	dot = strrchr(history_name, '.');
	if (strncmp(history_name, "WLE-", 4) || dot == NULL || dot - history_name <= 4 ||
	    dot - history_name - 4 >= (int)sizeof(stamp))
		return -1;
	snprintf(stamp, sizeof(stamp), "%.*s", (int)(dot - history_name - 4), history_name + 4);

	if (svMkdir(saves_dir) < 0)
		return -1;
	if (svReadMarker(saves_dir, marker, sizeof(marker)) == 0 && !strcmp(marker, history_name))
		return 0; /* this copy was already done */

	if (svOpenCard(history, &fd) != MCV_OK)
		return -1;
	n = mcViewListSaves(&sv_view, NULL, 0, &total);
	if (n >= 0 && total > 0) {
		saves = (McViewSave *)malloc(sizeof(*saves) * (size_t)total);
		n = saves ? mcViewListSaves(&sv_view, saves, total, &total) : -1;
	}
	genClose(fd);
	if (n < 0) {
		free(saves);
		return -1;
	}

	st->saves = n;
	for (i = 0; i < n; i++) {
		if (svSystemFolder(saves[i].name)) {
			st->skipped++;
			continue;
		}
		r = svOneSave(history, saves_dir, saves[i].name, stamp, keep);
		if (r == 1)
			st->versioned++;
		else if (r == 0)
			st->unchanged++;
		else if (r == 2)
			st->skipped++;
		else
			st->failed++;
	}
	free(saves);
	/* Only a clean pass is recorded, so a failed save is tried again. */
	if (st->failed == 0)
		svWriteMarker(saves_dir, history_name);
	return 0;
}

int mmceSaveVersionValidate(const char *path)
{
	McSaveData s;
	u8 *buf;
	s64 size;
	int fd, ok;
	uint32_t done = 0;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return -1;
	size = genLseek(fd, 0, SEEK_END);
	if (size <= 0 || size > (s64)MMCE_SAVE_VERSION_MAX || genLseek(fd, 0, SEEK_SET) != 0) {
		genClose(fd);
		return -1;
	}
	buf = (u8 *)malloc((size_t)size);
	if (buf == NULL) {
		genClose(fd);
		return -1;
	}
	while (done < (uint32_t)size) {
		int n = genRead(fd, buf + done, (uint32_t)size - done > SV_IO ? SV_IO : (int)((uint32_t)size - done));
		if (n <= 0)
			break;
		done += (uint32_t)n;
	}
	genClose(fd);
	ok = done == (uint32_t)size && mcCardSaveFromPsu(buf, (uint32_t)size, &s) == MCW_OK;
	if (ok)
		mcCardFreeSave(&s);
	free(buf);
	return ok ? 0 : -1;
}
