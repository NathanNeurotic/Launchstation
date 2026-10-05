//--------------------------------------------------------------
// File name: mmce_backup.c
// Reusable verified MMCE card backup/history backend.
//--------------------------------------------------------------
#include "launchelf.h"
#include "filer_shared.h"
#include "main_title.h"
#include "mc_card_export.h"
#include "mmce_backup.h"

static int bcdToInt(u8 value)
{
	return ((value >> 4) * 10) + (value & 0x0F);
}

static int backupStamp(char *out, size_t out_size)
{
	sceCdCLOCK clock;
	int year, month, day, hour, minute, second;

	if (out == NULL || out_size == 0)
		return 0;

	memset(&clock, 0, sizeof(clock));
	if (sceCdReadClock(&clock) == 0 || clock.stat != 0) {
		snprintf(out, out_size, "00000000-000000-S%010llu",
		         (unsigned long long)(Timer() / 1000));
		return 1;
	}

	year = 2000 + bcdToInt(clock.year);
	month = bcdToInt(clock.month & 0x7F);
	day = bcdToInt(clock.day);
	hour = bcdToInt(clock.hour);
	minute = bcdToInt(clock.minute);
	second = bcdToInt(clock.second);
	menuTitleConvertTimestampToLocalTime(&year, &month, &day, &hour, &minute, &second);
	snprintf(out, out_size, "%04d%02d%02d-%02d%02d%02d",
	         year, month, day, hour, minute, second);
	return 1;
}

static int joinPath(char *out, size_t out_size, const char *folder, const char *name)
{
	size_t len;

	if (out == NULL || out_size == 0 || folder == NULL || name == NULL)
		return 0;
	len = strlen(folder);
	if (len > 0 && folder[len - 1] != '/' && folder[len - 1] != ':')
		return snprintf(out, out_size, "%s/%s", folder, name) < (int)out_size;
	return snprintf(out, out_size, "%s%s", folder, name) < (int)out_size;
}

static int stampedName(int unit, const char *folder, const char *ext,
                       char *name, size_t name_size)
{
	char stamp[32], candidate[MAX_PATH];
	int suffix;

	if (unit < 0 || unit > 1 || !backupStamp(stamp, sizeof(stamp)) ||
	    ext == NULL || ext[0] == '\0')
		return 0;

	for (suffix = 0; suffix < 100; suffix++) {
		if (suffix == 0)
			snprintf(name, name_size, "MMCE%d-ActiveCard-%s.%s", unit, stamp, ext);
		else
			snprintf(name, name_size, "MMCE%d-ActiveCard-%s-%02d.%s",
			         unit, stamp, suffix, ext);
		if (!joinPath(candidate, sizeof(candidate), folder, name))
			return 0;
		if (!wleExists(candidate))
			return 1;
	}
	return 0;
}

static int historyMatch(int unit, const char *name)
{
	char prefix[32];
	const char *dot;

	if (name == NULL)
		return 0;
	snprintf(prefix, sizeof(prefix), "MMCE%d-ActiveCard-", unit);
	if (strncmp(name, prefix, strlen(prefix)) != 0)
		return 0;
	dot = strrchr(name, '.');
	return dot != NULL && !stricmp(dot, ".mcd");
}

static int historyScan(const char *folder, int unit, char *oldest, size_t oldest_size)
{
	iox_dirent_t ent;
	char path[MAX_PATH];
	int fd, count = 0;

	if (oldest != NULL && oldest_size > 0)
		oldest[0] = '\0';
	snprintf(path, sizeof(path), "%s", folder);
	fd = genDopen(path);
	if (fd < 0)
		return -1;

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (historyMatch(unit, ent.name)) {
			count++;
			if (oldest != NULL && oldest_size > 0 &&
			    (oldest[0] == '\0' || strcmp(ent.name, oldest) < 0))
				snprintf(oldest, oldest_size, "%s", ent.name);
		}
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return count;
}

static int historyNewestOther(const char *folder, int unit, const char *exclude,
                              char *newest, size_t newest_size)
{
	iox_dirent_t ent;
	char path[MAX_PATH];
	int fd;

	if (newest == NULL || newest_size == 0)
		return -1;
	newest[0] = '\0';
	snprintf(path, sizeof(path), "%s", folder);
	fd = genDopen(path);
	if (fd < 0)
		return -1;

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (historyMatch(unit, ent.name) &&
		    (exclude == NULL || strcmp(ent.name, exclude) != 0) &&
		    (newest[0] == '\0' || strcmp(ent.name, newest) > 0))
			snprintf(newest, newest_size, "%s", ent.name);
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return newest[0] != '\0' ? 1 : 0;
}

static int filesEqual(const char *a, const char *b)
{
	static u8 buf_a[16384] __attribute__((aligned(64)));
	static u8 buf_b[16384] __attribute__((aligned(64)));
	int fa = -1, fb = -1, result = -1;
	s64 size_a, size_b;

	fa = genOpen(a, FIO_O_RDONLY);
	if (fa < 0)
		goto done;
	fb = genOpen(b, FIO_O_RDONLY);
	if (fb < 0)
		goto done;

	size_a = genLseek(fa, 0, SEEK_END);
	size_b = genLseek(fb, 0, SEEK_END);
	if (size_a < 0 || size_b < 0)
		goto done;
	if (size_a != size_b) {
		result = 0;
		goto done;
	}
	if (genLseek(fa, 0, SEEK_SET) != 0 || genLseek(fb, 0, SEEK_SET) != 0)
		goto done;

	result = 1;
	while (size_a > 0) {
		int want = size_a > (s64)sizeof(buf_a) ? (int)sizeof(buf_a) : (int)size_a;
		int ra = genRead(fa, buf_a, want);
		int rb = genRead(fb, buf_b, want);
		if (ra != want || rb != want) {
			result = -1;
			break;
		}
		if (memcmp(buf_a, buf_b, want) != 0) {
			result = 0;
			break;
		}
		size_a -= want;
	}

done:
	if (fb >= 0)
		genClose(fb);
	if (fa >= 0)
		genClose(fa);
	return result;
}

static int historyPrune(const char *folder, int unit, int keep)
{
	char oldest[MAX_NAME], path[MAX_PATH];
	int count, guard = 0;

	if (keep <= 0)
		return 0;

	while (guard++ < 256) {
		count = historyScan(folder, unit, oldest, sizeof(oldest));
		if (count < 0)
			return -1;
		if (count <= keep)
			return 0;
		if (oldest[0] == '\0' || !joinPath(path, sizeof(path), folder, oldest))
			return -1;
		if (genRemove(path) < 0)
			return -1;
	}
	return -1;
}

int mmceUnitFromPath(const char *path)
{
	if (path == NULL || strncmp(path, "mmce", 4) != 0 ||
	    path[4] < '0' || path[4] > '1' || path[5] != ':')
		return -1;
	return path[4] - '0';
}

int mmceBackupActiveHistory(int unit, const char *folder, int keep,
                            McCardExportProgress progress,
                            char *out_path, size_t out_path_size)
{
	char source[MAX_PATH], dest[MAX_PATH], name[48];
	char previous_name[MAX_NAME], previous_path[MAX_PATH];
	int prep, r, newest, same = -1;

	if (unit < 0 || unit > 1 || folder == NULL || folder[0] == '\0')
		return MMCE_BACKUP_ERR_NAME;

	snprintf(source, sizeof(source), "mmce%d:/__active_card__.mcd", unit);
	if (!stampedName(unit, folder, "mcd", name, sizeof(name)) ||
	    !joinPath(dest, sizeof(dest), folder, name))
		return MMCE_BACKUP_ERR_NAME;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep == TRANSFER_STACK_INCOMPATIBLE)
		return MMCE_BACKUP_ERR_STACK_INCOMPATIBLE;
	if (prep != TRANSFER_STACK_READY)
		return MMCE_BACKUP_ERR_STACK;

	r = mcCardBackupActiveMmce(source, dest, progress);
	if (r != MC_CARD_EXPORT_OK)
		return r;

	if (out_path != NULL && out_path_size > 0)
		snprintf(out_path, out_path_size, "%s", dest);

	newest = historyNewestOther(folder, unit, name, previous_name, sizeof(previous_name));
	if (newest > 0 && joinPath(previous_path, sizeof(previous_path), folder, previous_name))
		same = filesEqual(dest, previous_path);

	if (same == 1) {
		if (genRemove(dest) < 0)
			return MMCE_BACKUP_HISTORY_DUPLICATE_REMOVE_WARN;
		if (out_path != NULL && out_path_size > 0)
			out_path[0] = '\0';
		if (historyPrune(folder, unit, keep) < 0)
			return MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN;
		return MMCE_BACKUP_HISTORY_UNCHANGED;
	}

	if (historyPrune(folder, unit, keep) < 0)
		return MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN;
	return MMCE_BACKUP_HISTORY_CREATED;
}

int mmceExportActivePcsx2ToFolder(int unit, const char *folder,
                                  McCardExportProgress progress,
                                  char *out_path, size_t out_path_size)
{
	char source[MAX_PATH], dest[MAX_PATH], name[56];
	int prep, r;

	if (unit < 0 || unit > 1 || folder == NULL || folder[0] == '\0')
		return MMCE_BACKUP_ERR_NAME;

	snprintf(source, sizeof(source), "mmce%d:/__active_card__.mcd", unit);
	if (!stampedName(unit, folder, "ps2", name, sizeof(name)) ||
	    !joinPath(dest, sizeof(dest), folder, name))
		return MMCE_BACKUP_ERR_NAME;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep == TRANSFER_STACK_INCOMPATIBLE)
		return MMCE_BACKUP_ERR_STACK_INCOMPATIBLE;
	if (prep != TRANSFER_STACK_READY)
		return MMCE_BACKUP_ERR_STACK;

	r = mcCardExportActiveMmcePcsx2(source, dest, progress);
	if (r == MC_CARD_EXPORT_OK && out_path != NULL && out_path_size > 0)
		snprintf(out_path, out_path_size, "%s", dest);
	return r == MC_CARD_EXPORT_OK ? MMCE_BACKUP_HISTORY_CREATED : r;
}

static int namedHistoryMatch(const char *prefix, const char *name)
{
	size_t n;
	const char *dot;

	if (prefix == NULL || prefix[0] == '\0' || name == NULL)
		return 0;
	n = strlen(prefix);
	if (strncmp(name, prefix, n) != 0 || name[n] != '-')
		return 0;
	dot = strrchr(name, '.');
	return dot != NULL && !stricmp(dot, ".mcd");
}

static int namedStampedName(const char *prefix, const char *folder,
                            char *name, size_t name_size)
{
	char stamp[32], candidate[MAX_PATH];
	int suffix;

	if (prefix == NULL || prefix[0] == '\0' || strchr(prefix, '/') != NULL ||
	    strchr(prefix, ':') != NULL || !backupStamp(stamp, sizeof(stamp)))
		return 0;

	for (suffix = 0; suffix < 100; suffix++) {
		if (suffix == 0)
			snprintf(name, name_size, "%s-%s.mcd", prefix, stamp);
		else
			snprintf(name, name_size, "%s-%s-%02d.mcd", prefix, stamp, suffix);
		if (!joinPath(candidate, sizeof(candidate), folder, name))
			return 0;
		if (!wleExists(candidate))
			return 1;
	}
	return 0;
}

static int namedHistoryNewest(const char *folder, const char *prefix,
                              const char *exclude, char *newest, size_t newest_size)
{
	iox_dirent_t ent;
	char path[MAX_PATH];
	int fd;

	if (newest == NULL || newest_size == 0)
		return -1;
	newest[0] = '\0';
	snprintf(path, sizeof(path), "%s", folder);
	fd = genDopen(path);
	if (fd < 0)
		return -1;

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (namedHistoryMatch(prefix, ent.name) &&
		    (exclude == NULL || strcmp(ent.name, exclude) != 0) &&
		    (newest[0] == '\0' || strcmp(ent.name, newest) > 0))
			snprintf(newest, newest_size, "%s", ent.name);
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return newest[0] != '\0' ? 1 : 0;
}

static int namedHistoryPrune(const char *folder, const char *prefix, int keep)
{
	iox_dirent_t ent;
	char path[MAX_PATH], oldest[MAX_NAME];
	int fd, count, guard = 0;

	if (keep <= 0)
		return 0;

	while (guard++ < 256) {
		count = 0;
		oldest[0] = '\0';
		snprintf(path, sizeof(path), "%s", folder);
		fd = genDopen(path);
		if (fd < 0)
			return -1;
		memset(&ent, 0, sizeof(ent));
		while (fileXioDread(fd, &ent) > 0) {
			if (namedHistoryMatch(prefix, ent.name)) {
				count++;
				if (oldest[0] == '\0' || strcmp(ent.name, oldest) < 0)
					snprintf(oldest, sizeof(oldest), "%s", ent.name);
			}
			memset(&ent, 0, sizeof(ent));
		}
		genDclose(fd);
		if (count <= keep)
			return 0;
		if (oldest[0] == '\0' || !joinPath(path, sizeof(path), folder, oldest))
			return -1;
		if (genRemove(path) < 0)
			return -1;
	}
	return -1;
}

int mmceBackupCardFileHistory(const char *source, const char *folder,
                              const char *prefix, int keep,
                              McCardExportProgress progress,
                              char *out_path, size_t out_path_size)
{
	char dest[MAX_PATH], name[MAX_NAME], previous[MAX_NAME], previous_path[MAX_PATH];
	int prep, r, newest, same = -1;

	if (source == NULL || source[0] == '\0' || folder == NULL || folder[0] == '\0')
		return MMCE_BACKUP_ERR_NAME;
	if (!namedStampedName(prefix, folder, name, sizeof(name)) ||
	    !joinPath(dest, sizeof(dest), folder, name))
		return MMCE_BACKUP_ERR_NAME;

	prep = prepareTransferDeviceStacks(source, dest);
	if (prep == TRANSFER_STACK_INCOMPATIBLE)
		return MMCE_BACKUP_ERR_STACK_INCOMPATIBLE;
	if (prep != TRANSFER_STACK_READY)
		return MMCE_BACKUP_ERR_STACK;

	r = mcCardBackupNoEcc(source, dest, progress);
	if (r != MC_CARD_EXPORT_OK)
		return r;

	if (out_path != NULL && out_path_size > 0)
		snprintf(out_path, out_path_size, "%s", dest);

	newest = namedHistoryNewest(folder, prefix, name, previous, sizeof(previous));
	if (newest > 0 && joinPath(previous_path, sizeof(previous_path), folder, previous))
		same = filesEqual(dest, previous_path);

	if (same == 1) {
		if (genRemove(dest) < 0)
			return MMCE_BACKUP_HISTORY_DUPLICATE_REMOVE_WARN;
		if (out_path != NULL && out_path_size > 0)
			out_path[0] = '\0';
		if (namedHistoryPrune(folder, prefix, keep) < 0)
			return MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN;
		return MMCE_BACKUP_HISTORY_UNCHANGED;
	}

	if (namedHistoryPrune(folder, prefix, keep) < 0)
		return MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN;
	return MMCE_BACKUP_HISTORY_CREATED;
}

static int ensureDirectory(const char *path)
{
	iox_stat_t st;

	if (genGetStat(path, &st) >= 0)
		return FIO_S_ISDIR(st.mode) ? 0 : -1;
	return genMkdir(path, fileMode) >= 0 ? 0 : -1;
}

int mmceEnsureDirectoryTree(const char *path)
{
	char work[MAX_PATH];
	char *colon, *p;

	if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(work))
		return -1;
	snprintf(work, sizeof(work), "%s", path);
	colon = strchr(work, ':');
	if (colon == NULL)
		return -1;

	for (p = colon + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		if (p == colon + 1)
			continue;
		*p = '\0';
		if (ensureDirectory(work) < 0)
			return -1;
		*p = '/';
	}
	return ensureDirectory(work);
}

int mmceEnsureAutoBackupFolder(int unit, char *out, size_t out_size)
{
	char root[MAX_PATH], app[MAX_PATH], backups[MAX_PATH];

	if (unit < 0 || unit > 1 || out == NULL || out_size == 0)
		return -1;

	snprintf(root, sizeof(root), "mmce%d:/", unit);
	snprintf(app, sizeof(app), "mmce%d:/WLE", unit);
	snprintf(backups, sizeof(backups), "mmce%d:/WLE/Backups", unit);

	if (!ensurePathDeviceStackReady(root))
		return -1;
	if (mmceEnsureDirectoryTree(app) < 0 || mmceEnsureDirectoryTree(backups) < 0)
		return -1;

	snprintf(out, out_size, "%s", backups);
	return 0;
}
