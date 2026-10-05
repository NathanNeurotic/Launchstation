//--------------------------------------------------------------
// File name: mmce_igr_scan.c
// Changed-card fallback for IGR paths that switched through BootCard.
//--------------------------------------------------------------
#include "launchelf.h"
#include "mmce_backup.h"
#include "mmce_igr_scan.h"

#define IGR_STATE_MAGIC "WLEMC01"
#define IGR_STATE_MAGIC_LEN 7
#define IGR_STATE_MAX_CARDS 512
#define IGR_STATE_PATH 256

typedef struct {
	char path[IGR_STATE_PATH];
	u64 size;
	u8 mtime[8];
} IgrCardState;

static IgrCardState current_cards[IGR_STATE_MAX_CARDS];
static IgrCardState previous_cards[IGR_STATE_MAX_CARDS];

static int readFull(int fd, void *buf, int size)
{
	u8 *p = (u8 *)buf;
	int done = 0;

	while (done < size) {
		int r = genRead(fd, p + done, size - done);
		if (r <= 0)
			return -1;
		done += r;
	}
	return 0;
}

static int writeFull(int fd, const void *buf, int size)
{
	const u8 *p = (const u8 *)buf;
	int done = 0;

	while (done < size) {
		int r = genWrite(fd, (void *)(p + done), size - done);
		if (r <= 0)
			return -1;
		done += r;
	}
	return 0;
}

static int isDotName(const char *name)
{
	return name == NULL || !strcmp(name, ".") || !strcmp(name, "..");
}

static int isMcd(const char *name)
{
	const char *dot = name ? strrchr(name, '.') : NULL;
	return dot != NULL && !stricmp(dot, ".mcd");
}

static int addState(const char *path, const iox_stat_t *stat, int *count)
{
	IgrCardState *s;

	if (*count >= IGR_STATE_MAX_CARDS || path == NULL || stat == NULL ||
	    strlen(path) >= IGR_STATE_PATH)
		return -1;

	s = &current_cards[*count];
	memset(s, 0, sizeof(*s));
	snprintf(s->path, sizeof(s->path), "%s", path);
	s->size = ((u64)stat->hisize << 32) | stat->size;
	memcpy(s->mtime, stat->mtime, sizeof(s->mtime));
	(*count)++;
	return 0;
}

static int scanCardFolder(const char *folder, int *count)
{
	iox_dirent_t ent;
	char dir[MAX_PATH], path[MAX_PATH];
	int fd;

	snprintf(dir, sizeof(dir), "%s", folder);
	fd = genDopen(dir);
	if (fd < 0)
		return -1;

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (!isDotName(ent.name) && !FIO_S_ISDIR(ent.stat.mode) && isMcd(ent.name)) {
			if (snprintf(path, sizeof(path), "%s/%s", folder, ent.name) >= (int)sizeof(path) ||
			    addState(path, &ent.stat, count) < 0) {
				genDclose(fd);
				return -1;
			}
		}
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return 0;
}

static int scanVariantRoot(int unit, const char *variant, int *count)
{
	iox_dirent_t ent;
	char root[MAX_PATH], path[MAX_PATH];
	int fd;

	snprintf(root, sizeof(root), "mmce%d:/MemoryCards/%s", unit, variant);
	fd = genDopen(root);
	if (fd < 0)
		return 0; /* Variant root not present on this card. */

	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		if (isDotName(ent.name)) {
			memset(&ent, 0, sizeof(ent));
			continue;
		}

		if (snprintf(path, sizeof(path), "%s/%s", root, ent.name) >= (int)sizeof(path)) {
			genDclose(fd);
			return -1;
		}
		if (FIO_S_ISDIR(ent.stat.mode)) {
			if (scanCardFolder(path, count) < 0) {
				genDclose(fd);
				return -1;
			}
		} else if (isMcd(ent.name) && addState(path, &ent.stat, count) < 0) {
			genDclose(fd);
			return -1;
		}
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	return 0;
}

static int scanAllCards(int unit)
{
	static const char *const variants[] = {"PS2", "COH", "SC2", "PROT"};
	int i, count = 0;

	memset(current_cards, 0, sizeof(current_cards));
	for (i = 0; i < 4; i++) {
		if (scanVariantRoot(unit, variants[i], &count) < 0)
			return -1;
	}
	return count;
}

static int loadPrevious(const char *backup_root)
{
	char path[MAX_PATH], magic[8];
	u32 count;
	int fd;

	memset(previous_cards, 0, sizeof(previous_cards));
	if (snprintf(path, sizeof(path), "%s/.igr-state.bin", backup_root) >= (int)sizeof(path))
		return 0;
	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return 0;

	memset(magic, 0, sizeof(magic));
	if (readFull(fd, magic, IGR_STATE_MAGIC_LEN) < 0 ||
	    memcmp(magic, IGR_STATE_MAGIC, IGR_STATE_MAGIC_LEN) != 0 ||
	    readFull(fd, &count, sizeof(count)) < 0 ||
	    count > IGR_STATE_MAX_CARDS ||
	    readFull(fd, previous_cards, count * sizeof(previous_cards[0])) < 0) {
		genClose(fd);
		memset(previous_cards, 0, sizeof(previous_cards));
		return 0; /* Corrupt/missing state => safe first-run behavior. */
	}
	genClose(fd);
	return (int)count;
}

static int findPrevious(const char *path, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		if (!strcmp(previous_cards[i].path, path))
			return i;
	}
	return -1;
}

static int stateChanged(const IgrCardState *now, const IgrCardState *old)
{
	if (old == NULL)
		return 1;
	return now->size != old->size || memcmp(now->mtime, old->mtime, sizeof(now->mtime)) != 0;
}

static int saveCurrent(const char *backup_root, int count)
{
	char path[MAX_PATH];
	u32 n = (u32)count;
	int fd, ret = -1;

	if (snprintf(path, sizeof(path), "%s/.igr-state.bin", backup_root) >= (int)sizeof(path))
		return -1;
	fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
	if (fd < 0)
		return -1;

	if (writeFull(fd, IGR_STATE_MAGIC, IGR_STATE_MAGIC_LEN) == 0 &&
	    writeFull(fd, &n, sizeof(n)) == 0 &&
	    writeFull(fd, current_cards, count * sizeof(current_cards[0])) == 0)
		ret = 0;
	if (genClose(fd) < 0)
		ret = -1;
	return ret;
}

static int historyParts(int unit, const char *source, const char *backup_root,
                        char *history, size_t history_size,
                        char *prefix, size_t prefix_size)
{
	char marker[48], rel[IGR_STATE_PATH], *variant, *folder, *file, *slash, *dot;

	snprintf(marker, sizeof(marker), "mmce%d:/MemoryCards/", unit);
	if (strncmp(source, marker, strlen(marker)) != 0)
		return -1;
	snprintf(rel, sizeof(rel), "%s", source + strlen(marker));

	variant = rel;
	slash = strchr(variant, '/');
	if (slash == NULL)
		return -1;
	*slash = '\0';
	folder = slash + 1;
	slash = strchr(folder, '/');
	if (slash == NULL)
		return -1;
	*slash = '\0';
	file = slash + 1;
	if (strchr(file, '/') != NULL)
		return -1;

	snprintf(prefix, prefix_size, "%s", file);
	dot = strrchr(prefix, '.');
	if (dot != NULL)
		*dot = '\0';
	if (prefix[0] == '\0')
		return -1;

	if (snprintf(history, history_size, "%s/Cards/%s/%s",
	             backup_root, variant, folder) >= (int)history_size)
		return -1;
	return mmceEnsureDirectoryTree(history);
}

int mmceIgrScanChangedCards(int unit, const char *backup_root,
                            MmceIgrScanResult *result)
{
	MmceIgrScanResult local;
	char history[MAX_PATH], prefix[MAX_NAME], saved[MAX_PATH];
	int current_count, previous_count, i, old_idx, r, all_backups_ok = 1;

	memset(&local, 0, sizeof(local));
	if (unit < 0 || unit > 1 || backup_root == NULL || backup_root[0] == '\0')
		return -1;

	current_count = scanAllCards(unit);
	if (current_count < 0) {
		local.failures++;
		if (result) *result = local;
		return -1;
	}
	local.cards_seen = current_count;
	previous_count = loadPrevious(backup_root);

	for (i = 0; i < current_count; i++) {
		old_idx = findPrevious(current_cards[i].path, previous_count);
		if (!stateChanged(&current_cards[i], old_idx >= 0 ? &previous_cards[old_idx] : NULL))
			continue;

		local.cards_changed++;
		if (historyParts(unit, current_cards[i].path, backup_root,
		                 history, sizeof(history), prefix, sizeof(prefix)) < 0) {
			local.failures++;
			all_backups_ok = 0;
			if (old_idx >= 0)
				current_cards[i] = previous_cards[old_idx];
			else
				current_cards[i].path[0] = '\0';
			continue;
		}

		saved[0] = '\0';
		r = mmceBackupCardFileHistory(current_cards[i].path, history, prefix,
		                              MMCE_BACKUP_HISTORY_KEEP_DEFAULT,
		                              NULL, saved, sizeof(saved));
		if (r == MMCE_BACKUP_HISTORY_CREATED) {
			local.backups_created++;
		} else if (r == MMCE_BACKUP_HISTORY_UNCHANGED) {
			local.backups_unchanged++;
		} else if (r == MMCE_BACKUP_HISTORY_CREATED_PRUNE_WARN ||
		           r == MMCE_BACKUP_HISTORY_DUPLICATE_REMOVE_WARN) {
			local.warnings++;
		} else {
			local.failures++;
			all_backups_ok = 0;
			if (old_idx >= 0)
				current_cards[i] = previous_cards[old_idx];
			else
				current_cards[i].path[0] = '\0';
		}
	}

	/* Compact failed first-seen entries so they remain absent from the state
	 * file and are retried on the next IGR. */
	if (!all_backups_ok) {
		int out = 0;
		for (i = 0; i < current_count; i++) {
			if (current_cards[i].path[0] != '\0') {
				if (out != i)
					current_cards[out] = current_cards[i];
				out++;
			}
		}
		current_count = out;
	}

	if (saveCurrent(backup_root, current_count) < 0) {
		local.failures++;
		all_backups_ok = 0;
	}

	if (result != NULL)
		*result = local;
	return all_backups_ok ? 0 : -1;
}
