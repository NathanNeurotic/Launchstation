/* Host harness for src/dash_card_tool.c: the real module, the real card
 * viewer and writer, and POSIX stand-ins for the PS2 file calls and the
 * dashboard dialogs. Device paths map into a folder:
 *   mmce0:/x -> <root>/mmce0/x, mass:/x -> <root>/mass/x
 * and mmce0:/__active_card__.mcd opens the image named by ACTIVE.
 *
 * NORENAME makes rename unsupported and FAIL_REMOVE makes remove fail.
 * Dialog answers come from argv, in order: m<N> menu choice (-1 = Back),
 * p<path> picker result, c<0|1> confirm, k<N> choice. An empty queue answers Back/No.
 * Every dialog is printed, and the harness aborts if two files are ever open
 * at once (the MMCE driver allows one). */
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include "dash_internal.h"
#include "filer_shared.h"
#include "mc_card_export.h"
#include "mc_card_view.h"
#include "mmce_backup.h"
#include "dash_card_tool.h"
#include "mmce_card_switch.h"
#include <libcdvd.h>

static const char *root, *active;
/* the simulated device: what it serves, what it reports, how long it is busy */
static int dev_card = -1, dev_chan = -1, rep_card = -1, rep_chan = -1, busy, stale, devctls;
static char **queue;
static int nqueue, qpos, open_files, max_open, writes, fail_write_at = -1;
int fileMode = 0777;
int vmcMounted[2];

static void map(const char *path, char *out)
{
	const char *colon = strchr(path, ':');

	if (!strcmp(path, "mmce0:/__active_card__.mcd")) {
		if (!strcmp(active, "auto"))
			snprintf(out, MAX_PATH, "%s/mmce0/MemoryCards/PS2/Card%d/Card%d-%d.mcd", root, dev_card, dev_card,
			         dev_chan);
		else
			snprintf(out, MAX_PATH, "%s", active);
		return;
	}
	if (colon == NULL) {
		fprintf(stderr, "unmapped path %s\n", path);
		abort();
	}
	snprintf(out, MAX_PATH, "%s/%.*s%s", root, (int)(colon - path), path, colon + 1);
}

int genOpen(const char *path, int mode)
{
	char p[MAX_PATH];
	int fd;

	map(path, p);
	fd = open(p, mode, 0644);
	if (fd >= 0) {
		if (++open_files > 1) {
			fprintf(stderr, "two files open at once: %s\n", path);
			abort();
		}
		if (open_files > max_open)
			max_open = open_files;
	}
	return fd;
}
s64 genLseek(int fd, s64 where, int how) { return lseek(fd, where, how); }
int genRead(int fd, void *buf, int size) { return (int)read(fd, buf, (size_t)size); }
int genWrite(int fd, void *buf, int size)
{
	if (fail_write_at >= 0 && writes >= fail_write_at)
		return -1;
	writes++;
	return (int)write(fd, buf, (size_t)size);
}
int genClose(int fd)
{
	open_files--;
	return close(fd);
}

static DIR *dirs[8];
int genDopen(char *path)
{
	char p[MAX_PATH];
	int i;

	map(path, p);
	for (i = 0; i < 8 && dirs[i]; i++)
		;
	if (i == 8 || !(dirs[i] = opendir(p)))
		return -1;
	return i;
}
int genDclose(int fd)
{
	closedir(dirs[fd]);
	dirs[fd] = NULL;
	return 0;
}
int fileXioDread(int fd, iox_dirent_t *ent)
{
	struct dirent *d = readdir(dirs[fd]);
	struct stat st;

	if (!d)
		return 0;
	snprintf(ent->name, sizeof(ent->name), "%s", d->d_name);
	ent->stat.mode = fstatat(dirfd(dirs[fd]), d->d_name, &st, 0) == 0 && S_ISDIR(st.st_mode) ? 0x1000 : 0x2000;
	return 1;
}
int genMkdir(const char *path, int mode)
{
	char p[MAX_PATH];

	map(path, p);
	return mkdir(p, (mode_t)mode);
}
int genRemove(char *path)
{
	char p[MAX_PATH];

	map(path, p);
	printf("REMOVE|%s\n", path);
	if (getenv("FAIL_REMOVE"))
		return -1;
	return unlink(p);
}
/* NORENAME: a device without rename (UDPFS), so the caller must copy. */
int genRenameNoReplace(const char *from, const char *to)
{
	char a[MAX_PATH], b[MAX_PATH];

	map(from, a);
	map(to, b);
	printf("RENAME|%s|%s\n", from, to);
	if (getenv("NORENAME"))
		return -38;
	if (access(b, F_OK) == 0)
		return -17;
	return rename(a, b) == 0 ? 0 : -5;
}
int genCmpFileExt(const char *name, const char *ext)
{
	const char *dot = strrchr(name, '.');

	return dot && !strcasecmp(dot + 1, ext);
}
int wleExists(const char *path)
{
	char p[MAX_PATH];

	map(path, p);
	return access(p, F_OK) == 0;
}
void dashStrCopy(char *dst, const char *src, int n)
{
	snprintf(dst, (size_t)n, "%s", src);
}
void dashSjisTitle(char *out, size_t size, const unsigned char *title)
{
	snprintf(out, size, "T:%s", (const char *)title);
}
int prepareTransferDeviceStacks(const char *a, const char *b)
{
	(void)a;
	(void)b;
	return TRANSFER_STACK_READY;
}

static const char *next(char kind)
{
	if (qpos < nqueue && queue[qpos][0] == kind)
		return queue[qpos++] + 1;
	if (qpos < nqueue) {
		fprintf(stderr, "dialog %c, script has %s\n", kind, queue[qpos]);
		abort();
	}
	return NULL;
}

int fileXioDevctl(const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
	u32 a = arg ? *(u32 *)arg : 0;

	(void)buf;
	(void)buflen;
	if (strcmp(name, "mmce0:") || (arg && arglen != 4) || open_files) {
		fprintf(stderr, "bad devctl %s len %u open %d\n", name, arglen, open_files);
		abort();
	}
	devctls++;
	if (dev_card < 0)
		return -1;
	switch (cmd) {
		case 2:
			if (busy > 0) {
				busy--;
				return 1;
			}
			return 0;
		case 3:
			return rep_card;
		case 5:
			return rep_chan;
		case 4:
			printf("SET_CARD|%08x\n", a);
			dev_card = (int)(a & 0xFFFF);
			dev_chan = 1;
			if (!stale)
				rep_card = dev_card, rep_chan = dev_chan;
			if (getenv("SWITCH_BUSY"))
				busy = atoi(getenv("SWITCH_BUSY"));
			return 0;
		case 6:
			printf("SET_CHANNEL|%08x\n", a);
			dev_chan = (int)(a & 0xFFFF);
			if (!stale)
				rep_chan = dev_chan;
			return 0;
	}
	return -1;
}
int DelayThread(int usec)
{
	(void)usec;
	return 0;
}

void dashToast(int kind, const char *msg) { printf("TOAST|%d|%s\n", kind, msg); }
int dashModalMenu(const char *title, const char *subtitle, const DashMenuItem *items, int n, int initial)
{
	const char *a = next('m');
	int i;

	(void)initial;
	printf("MENU|%s|%s", title, subtitle);
	for (i = 0; i < n; i++)
		printf("|%s%s%s%s", items[i].disabled ? "!" : "", items[i].label, items[i].detail ? "~" : "",
		       items[i].detail ? items[i].detail : "");
	printf("\n");
	return a ? atoi(a) : -1;
}
int dashModalChoice(const char *title, const char *const *options, int n, int current)
{
	const char *a = next('k');

	printf("CHOICE|%s|%d|%d|%s\n", title, n, current, options[0]);
	return a ? atoi(a) : -1;
}
int dashPickPath(const char *title, const char *start, int mode, const char *ext, char *out, int size)
{
	const char *a = next('p');

	printf("PICK|%s|%s|%d|%s\n", title, start, mode, ext ? ext : "");
	if (!a)
		return 0;
	snprintf(out, (size_t)size, "%s", a);
	return 1;
}
int dashModalConfirm(const char *title, const char *body, const char *ok, int danger)
{
	const char *a = next('c');

	printf("CONFIRM|%s|%s|%s|%d\n", title, body, ok, danger);
	return a ? atoi(a) : 0;
}
void dashModalMessage(int icon, const char *title, const char *body, const char *detail)
{
	printf("MESSAGE|%d|%s|%s|%s\n", icon, title, body, detail ? detail : "");
}
void dashLoadingBegin(const char *t) { (void)t; }
void dashLoadingStatus(const char *s, int d, int t) { (void)s, (void)d, (void)t; }
void dashLoadingEnd(void) {}

/* Stand-ins for the real verified helpers (tested by their own suites). */
static int readAll(const char *path, u8 **buf, s64 *len)
{
	int fd = genOpen(path, FIO_O_RDONLY);

	if (fd < 0)
		return -1;
	*len = genLseek(fd, 0, SEEK_END);
	genLseek(fd, 0, SEEK_SET);
	*buf = malloc((size_t)*len);
	if (genRead(fd, *buf, (int)*len) != (int)*len) {
		genClose(fd);
		return -1;
	}
	genClose(fd);
	return 0;
}

int mcCardBackupNoEccNewFile(const char *source, const char *dest, McCardExportProgress progress)
{
	u8 *buf;
	s64 len;
	int fd, ok;

	(void)progress;
	if (readAll(source, &buf, &len) < 0)
		return MC_CARD_EXPORT_IO;
	fd = genOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
	if (fd < 0) {
		free(buf);
		return MC_CARD_EXPORT_IO;
	}
	ok = genWrite(fd, buf, (int)len) == (int)len;
	genClose(fd);
	free(buf);
	printf("SAFETY|%s|%d\n", dest, ok);
	return ok ? MC_CARD_EXPORT_OK : MC_CARD_EXPORT_IO;
}

static int fpRead(void *ctx, uint64_t off, void *buf, uint32_t len)
{
	return pread(*(int *)ctx, buf, len, (off_t)off) == (ssize_t)len ? 0 : -1;
}

int mmceCardFingerprint(const char *path, unsigned char out[32])
{
	static McView v;
	McViewSource src;
	int fd = genOpen(path, FIO_O_RDONLY), r;

	if (fd < 0)
		return -1;
	src.read = fpRead;
	src.ctx = &fd;
	src.size = (uint64_t)lseek(fd, 0, SEEK_END);
	r = mcViewOpen(&v, &src);
	if (r == MCV_OK)
		r = mcViewFingerprint(&v, out);
	genClose(fd);
	return r == MCV_OK ? 0 : -1;
}

/* 2026-10-29 12:34:56 in BCD, as the console RTC reports it; NOCLOCK fails the read. */
int sceCdReadClock(sceCdCLOCK *c)
{
	if (getenv("NOCLOCK"))
		return 0;
	memset(c, 0, sizeof(*c));
	c->second = 0x56, c->minute = 0x34, c->hour = 0x12, c->day = 0x29, c->month = 0x10, c->year = 0x26;
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 4)
		return 2;
	root = argv[1];
	active = argv[2];
	if (getenv("FAIL_WRITE_AT"))
		fail_write_at = atoi(getenv("FAIL_WRITE_AT"));
	if (getenv("CARD")) {
		dev_card = rep_card = atoi(getenv("CARD"));
		dev_chan = rep_chan = getenv("CHANNEL") ? atoi(getenv("CHANNEL")) : 1;
	}
	if (getenv("REPORT_CARD"))
		rep_card = atoi(getenv("REPORT_CARD"));
	stale = getenv("STALE") != NULL;
	vmcMounted[1] = getenv("VMC") != NULL;
	busy = getenv("BUSY") ? atoi(getenv("BUSY")) : 0;
	queue = argv + 4;
	nqueue = argc - 4;
	if (!strncmp(argv[3], "switch:", 7)) {
		printf("SWITCH|%d\n", mmceSwitchToCard(argv[3] + 7));
	} else if (!strcmp(argv[3], "active")) {
		MmceActiveCard a;
		int r = mmceActiveCard(0, &a, NULL);
		printf("ACTIVE|%d|%d|%d|%d|%d|%s\n", r, a.card, a.channel, a.matches, a.by_number, a.path);
	} else if (!strncmp(argv[3], "isactive:", 9)) {
		printf("ISACTIVE|%d\n", mmceCardIsActive(argv[3] + 9));
	} else if (!strncmp(argv[3], "pick-switch:", 12)) {
		dashMmceSwitch(argv[3] + 12);
	} else if (!strncmp(argv[3], "blank:", 6)) {
		dashCardNewBlank(argv[3] + 6);
	} else if (!strncmp(argv[3], "served:", 7)) {
		printf("SERVED|%d\n", mmceCardMaybeServed(argv[3] + 7));
	} else {
		dashCardTool(argv[3]);
	}
	if (qpos != nqueue) {
		fprintf(stderr, "script not used up at %s\n", queue[qpos]);
		return 4;
	}
	printf("END|open=%d|max_open=%d|writes=%d\n", open_files, max_open, writes);
	return open_files == 0 ? 0 : 5;
}
