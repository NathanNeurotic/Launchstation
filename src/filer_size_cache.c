#include "filer_size_cache.h"
#include "filer_internal.h"
#include "init.h"

#define SIZE_CACHE_ENTRIES 128
#define SIZE_CACHE_WRITERS 32

typedef struct {
	char *path;
	u64 bytes;
} SizeEntry;
typedef struct {
	char *path;
	int fd;
} SizeWriter;
static SizeEntry entries[SIZE_CACHE_ENTRIES];
static SizeWriter writers[SIZE_CACHE_WRITERS];
static int cache_sema = -1, replacement, disabled;
static unsigned generation, revision;

void filerSizeCacheInit(void)
{
	ee_sema_t sema;

	if (cache_sema >= 0)
		return;
	memset(&sema, 0, sizeof(sema));
	sema.init_count = sema.max_count = 1;
	cache_sema = CreateSema(&sema);
	generation = getIopResetGeneration();
}

static void clearEntries(void)
{
	for (int i = 0; i < SIZE_CACHE_ENTRIES; i++) {
		free(entries[i].path);
		entries[i].path = NULL;
	}
	revision++;
}

static int lockCache(void)
{
	unsigned current;

	if (cache_sema < 0 || WaitSema(cache_sema) < 0)
		return 0;
	current = getIopResetGeneration();
	if (current != generation) {
		clearEntries();
		for (int i = 0; i < SIZE_CACHE_WRITERS; i++) {
			free(writers[i].path);
			writers[i].path = NULL;
		}
		generation = current;
		disabled = 0;
	}
	return 1;
}

/* Stable HDD identities survive PFS slot reuse. Preserve case on PFS/MC;
 * normalize the case-insensitive FAT family and its unnumbered aliases. */
static int cacheKey(const char *path, char key[MAX_PATH])
{
	char mapped[MAX_PATH];
	const char *tail, *party = NULL;
	int prefix = 0, unit = 0, fold = 0, length, written;

	if (!path || !path[0] || strlen(path) >= MAX_PATH)
		return 0;
	if (!strncmp(path, "pfs", 3))
		prefix = 3;
#ifdef DVRP
	else if (!strncmp(path, "dvr_pfs", 7))
		prefix = 7;
#endif
	if (prefix && path[prefix] >= '0' && path[prefix] <= '9' && path[prefix + 1] == ':') {
		unit = path[prefix] - '0';
		if (unit >= MOUNT_LIMIT)
			return 0;
		party = prefix == 3 ? mountedParty[unit] : NULL;
#ifdef DVRP
		if (prefix == 7)
			party = mountedDVRPParty[unit];
#endif
		if (!party || !(tail = strchr(party, ':')) || !tail[1])
			return 0;
		written = snprintf(mapped, sizeof(mapped), "%.*s:/%s/%s", (int)(tail - party), party,
		                   tail + 1, path + prefix + 2);
	} else if (!strncmp(path, "mass:", 5)) {
		written = snprintf(mapped, sizeof(mapped), "usb0:/%s", path + 5);
#ifndef EXFAT
	} else if (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':') {
		written = snprintf(mapped, sizeof(mapped), "usb%c:/%s", path[4], path + 6);
#endif
	} else if (!strncmp(path, "usb:", 4) || !strncmp(path, "ata:", 4)) {
		written = snprintf(mapped, sizeof(mapped), "%.3s0:/%s", path, path + 4);
	} else if (!strncmp(path, "cf:", 3)) {
		written = snprintf(mapped, sizeof(mapped), "cf0:/%s", path + 3);
	} else if (!strncmp(path, "mx4sio:", 7)) {
		written = snprintf(mapped, sizeof(mapped), "mx4sio0:/%s", path + 7);
	} else {
		written = snprintf(mapped, sizeof(mapped), "%s", path);
	}
	if (written < 0 || written >= (int)sizeof(mapped) || !(tail = strchr(mapped, ':')))
		return 0;
	fold = !strncmp(mapped, "usb", 3) || !strncmp(mapped, "mass", 4) ||
	       !strncmp(mapped, "ata", 3) || !strncmp(mapped, "mx4sio", 6) ||
	       !strncmp(mapped, "mmce", 4) || !strncmp(mapped, "cf", 2);
	length = (int)(tail - mapped) + 1;
	if (length >= MAX_PATH - 1)
		return 0;
	memcpy(key, mapped, length);
	key[length++] = '/';
	for (tail++; *tail; tail++) {
		char c = *tail == '\\' ? '/' : *tail;
		if (c == '/' && key[length - 1] == '/')
			continue;
		if (length >= MAX_PATH - 1)
			return 0;
		key[length++] = fold && c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
	}
	while (length > 1 && key[length - 1] == '/' && key[length - 2] != ':')
		length--;
	key[length] = 0;
	return 1;
}

static int entryKey(const char *parent, const FILEINFO *file, char key[MAX_PATH])
{
	char path[MAX_PATH];
	size_t plen;
	int n;

	if (!parent || !file || !(file->stats.AttrFile & sceMcFileAttrSubdir) ||
	    !strcmp(file->name, ".") || !strcmp(file->name, ".."))
		return 0;
	plen = strlen(parent);
	n = snprintf(path, sizeof(path), "%s%s%s", parent,
	             plen > 0 && parent[plen - 1] != '/' && parent[plen - 1] != ':' ? "/" : "", file->name);
	return n >= 0 && n < (int)sizeof(path) && cacheKey(path, key);
}

static int below(const char *child, const char *parent)
{
	size_t n = strlen(parent);

	if (n == 0)
		return 0;
	return !strncmp(child, parent, n) && (!child[n] || parent[n - 1] == '/' || child[n] == '/');
}

static int related(const char *a, const char *b)
{
	if (below(a, b) || below(b, a))
		return 1;
#ifdef EXFAT
	/* Numbered mass slots share the BDM namespace with USB/ATA/MX4SIO.
	 * Their owner may change: invalidate conservatively across BDM aliases. */
	if (!strncmp(a, "mass", 4) || !strncmp(b, "mass", 4)) {
		const char *other = !strncmp(a, "mass", 4) ? b : a;
		return !strncmp(other, "usb", 3) || !strncmp(other, "ata", 3) ||
		       !strncmp(other, "mx4sio", 6) || !strncmp(other, "mass", 4);
	}
#endif
	return 0;
}

static void invalidateKey(const char *key)
{
	revision++;
	for (int i = 0; i < SIZE_CACHE_ENTRIES; i++) {
		if (entries[i].path && related(entries[i].path, key)) {
			free(entries[i].path);
			entries[i].path = NULL;
		}
	}
}

int filerSizeCacheGet(const char *parent, const FILEINFO *file, u64 *size)
{
	char key[MAX_PATH];
	int found = 0;

	if (!size || !lockCache())
		return 0;
	if (!disabled && entryKey(parent, file, key)) {
		for (int i = 0; i < SIZE_CACHE_ENTRIES; i++) {
			if (entries[i].path && !strcmp(entries[i].path, key)) {
				*size = entries[i].bytes;
				found = 1;
				break;
			}
		}
	}
	SignalSema(cache_sema);
	return found;
}

unsigned filerSizeCacheRevision(void)
{
	unsigned value = 0;

	if (!lockCache())
		return value;
	value = revision;
	SignalSema(cache_sema);
	return value;
}

void filerSizeCachePut(const char *parent, const FILEINFO *file, u64 size, unsigned measured_revision)
{
	char key[MAX_PATH];
	int slot = -1;

	if (!lockCache())
		return;
	if (disabled || measured_revision != revision || !entryKey(parent, file, key))
		goto done;
	for (int i = 0; i < SIZE_CACHE_WRITERS; i++) {
		if (writers[i].path && related(writers[i].path, key))
			goto done;
	}
	for (int i = 0; i < SIZE_CACHE_ENTRIES; i++) {
		if (entries[i].path && !strcmp(entries[i].path, key)) {
			slot = i;
			break;
		}
		if (!entries[i].path && slot < 0)
			slot = i;
	}
	if (slot < 0) {
		slot = replacement;
		replacement = (replacement + 1) % SIZE_CACHE_ENTRIES;
	}
	if (!entries[slot].path || strcmp(entries[slot].path, key)) {
		char *saved = malloc(strlen(key) + 1);
		if (!saved)
			goto done;
		strcpy(saved, key);
		free(entries[slot].path);
		entries[slot].path = saved;
	}
	entries[slot].bytes = size;
done:
	SignalSema(cache_sema);
}

void filerSizeCacheInvalidate(const char *path)
{
	char key[MAX_PATH];

	if (!lockCache())
		return;
	if (cacheKey(path, key))
		invalidateKey(key);
	else
		clearEntries(); /* unresolved mount/path: never retain a stale total */
	SignalSema(cache_sema);
}

void filerSizeCacheClosed(int fd)
{
	if (!lockCache())
		return;
	for (int i = 0; i < SIZE_CACHE_WRITERS; i++) {
		if (writers[i].path && writers[i].fd == fd) {
			invalidateKey(writers[i].path);
			free(writers[i].path);
			writers[i].path = NULL;
		}
	}
	SignalSema(cache_sema);
}

void filerSizeCacheOpened(int fd, const char *path, int mode)
{
	char key[MAX_PATH];
	int slot, access = mode & (FIO_O_RDONLY | FIO_O_WRONLY | FIO_O_RDWR);

	if (fd < 0)
		return;
	filerSizeCacheClosed(fd); /* also discard tracking for a reused descriptor */
	if ((access != FIO_O_WRONLY && access != FIO_O_RDWR && !(mode & (FIO_O_CREAT | FIO_O_TRUNC))) || !lockCache())
		return;
	if (!cacheKey(path, key))
		goto fail;
	invalidateKey(key);
	for (slot = 0; slot < SIZE_CACHE_WRITERS && writers[slot].path; slot++)
		;
	if (slot == SIZE_CACHE_WRITERS)
		goto fail;
	writers[slot].path = malloc(strlen(key) + 1);
	if (!writers[slot].path)
		goto fail;
	strcpy(writers[slot].path, key);
	writers[slot].fd = fd;
	SignalSema(cache_sema);
	return;
fail:
	clearEntries();
	disabled = 1; /* tracking exhaustion must fail closed, rather than show stale sizes */
	SignalSema(cache_sema);
}
