//--------------------------------------------------------------
//File name:   mmce_card_switch.c
//Which card image an MMCE device (sd2psx) is serving to the console, and a
//guarded switch to another numbered card.
//
//The device's own answer (GET_CARD / GET_CHANNEL) can be stale after the
//user changes cards with the buttons on the device, so it is always checked
//against content: the filesystem-index fingerprint of the synthetic
//mmceN:/__active_card__.mcd (the file the firmware has open) against the
//candidate image. Writers ask mmceCardMaybeServed() and refuse on 1, which
//covers "served", "the device says it is selected" and "cannot tell".
//
//Card layout (sd2psXtd): mmceN:/MemoryCards/PS2/CardN/CardN-C.mcd.
//Behaviour follows the SD2Cloud audit's description; no GPL code was used.
//--------------------------------------------------------------
#include "launchelf.h"
#include "mmce_backup.h"
#include "mmce_card_switch.h"

#define MMCE_DEVCTL_GET_STATUS 0x2
#define MMCE_DEVCTL_GET_CARD 0x3
#define MMCE_DEVCTL_SET_CARD 0x4
#define MMCE_DEVCTL_GET_CHANNEL 0x5
#define MMCE_DEVCTL_SET_CHANNEL 0x6
#define MMCE_SWITCH_POLLS 15 /* 1 s each: upstream's bound for a card switch */
#define MMCE_SCAN_MAX 512

static int devctl(int unit, int cmd, u32 *arg)
{
	char dev[8];

	snprintf(dev, sizeof(dev), "mmce%d:", unit);
	return fileXioDevctl(dev, cmd, arg, arg ? sizeof(*arg) : 0, NULL, 0);
}

int mmceCardUnit(const char *path)
{
	if (path == NULL || strncmp(path, "mmce", 4) != 0 || path[4] < '0' || path[4] > '1' || path[5] != ':')
		return -1;
	return path[4] - '0';
}

static int number(const char **p, int max)
{
	int n = 0, digits = 0;

	while (**p >= '0' && **p <= '9' && digits < 6) {
		n = n * 10 + (**p - '0');
		(*p)++;
		digits++;
	}
	return digits && n >= 1 && n <= max ? n : -1;
}

int mmceCardParsePath(const char *path, int *card, int *channel)
{
	static const char prefix[] = ":/MemoryCards/PS2/Card";
	const char *p;
	int n, c, n2;

	if (mmceCardUnit(path) < 0 || strncasecmp(path + 5, prefix, sizeof(prefix) - 1) != 0)
		return 0;
	p = path + 5 + sizeof(prefix) - 1;
	n = number(&p, 0xFFFF);
	if (n < 0 || *p != '/' || strncasecmp(p + 1, "Card", 4) != 0)
		return 0;
	p += 5;
	n2 = number(&p, 0xFFFF);
	if (n2 != n || *p != '-')
		return 0;
	p++;
	c = number(&p, 0xFFFF);
	if (c < 0 || strcasecmp(p, ".mcd") != 0)
		return 0;
	*card = n;
	*channel = c;
	return 1;
}

static int activePath(int unit, char *out, int size)
{
	return snprintf(out, size, "mmce%d:/__active_card__.mcd", unit) < size;
}

int mmceCardMaybeServed(const char *path)
{
	unsigned char mine[32], active[32];
	char ap[48];
	int unit = mmceCardUnit(path), n, c, card, chan;

	if (unit < 0)
		return MMCE_SERVED_NO;
	n = devctl(unit, MMCE_DEVCTL_GET_CARD, NULL);
	c = devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL);
	if (n > 0 && c > 0 && mmceCardParsePath(path, &card, &chan) && card == n && chan == c)
		return MMCE_SERVED_SELECTED;
	if (mmceCardFingerprint(path, mine) < 0)
		return MMCE_SERVED_UNKNOWN;
	if (!activePath(unit, ap, sizeof(ap)) || mmceCardFingerprint(ap, active) < 0)
		return MMCE_SERVED_UNKNOWN;
	return memcmp(mine, active, sizeof(mine)) ? MMCE_SERVED_NO : MMCE_SERVED_YES;
}

int mmceCardIsActive(const char *path)
{
	unsigned char mine[32], active[32];
	char ap[48];
	int unit = mmceCardUnit(path), card, chan;

	if (unit < 0 || !mmceCardParsePath(path, &card, &chan) ||
	    devctl(unit, MMCE_DEVCTL_GET_CARD, NULL) != card || devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL) != chan)
		return 0;
	return mmceCardFingerprint(path, mine) == 0 && activePath(unit, ap, sizeof(ap)) &&
	       mmceCardFingerprint(ap, active) == 0 && !memcmp(mine, active, sizeof(mine));
}

/* Every .mcd one level below MemoryCards/PS2/, in directory order. */
static int scanCards(int unit, void (*fn)(const char *path, void *ctx), void *ctx)
{
	char base[48], dir[MAX_PATH], path[MAX_PATH];
	char (*names)[MAX_NAME];
	iox_dirent_t ent;
	int fd, n = 0, i, seen = 0;

	names = malloc(MMCE_SCAN_MAX * sizeof(*names));
	if (names == NULL)
		return -1;
	snprintf(base, sizeof(base), "mmce%d:/MemoryCards/PS2/", unit);
	snprintf(dir, sizeof(dir), "%s", base);
	fd = genDopen(dir);
	if (fd < 0) {
		free(names);
		return -1;
	}
	memset(&ent, 0, sizeof(ent));
	/* folder names first: one open handle at a time */
	while (fileXioDread(fd, &ent) > 0 && n < MMCE_SCAN_MAX) {
		if (ent.name[0] != '.' && FIO_S_ISDIR(ent.stat.mode))
			snprintf(names[n++], MAX_NAME, "%s", ent.name);
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	for (i = 0; i < n; i++) {
		char files[8][MAX_NAME];
		int nf = 0, k;

		if (snprintf(dir, sizeof(dir), "%s%s/", base, names[i]) >= (int)sizeof(dir))
			continue;
		fd = genDopen(dir);
		if (fd < 0)
			continue;
		memset(&ent, 0, sizeof(ent));
		while (fileXioDread(fd, &ent) > 0) {
			if (nf < 8 && !FIO_S_ISDIR(ent.stat.mode) && genCmpFileExt(ent.name, "MCD"))
				snprintf(files[nf++], MAX_NAME, "%s", ent.name);
			memset(&ent, 0, sizeof(ent));
		}
		genDclose(fd);
		for (k = 0; k < nf; k++)
			if (snprintf(path, sizeof(path), "%s%s", dir, files[k]) < (int)sizeof(path)) {
				fn(path, ctx);
				seen++;
			}
	}
	free(names);
	return seen;
}

typedef struct {
	unsigned char active[32];
	MmceActiveCard *out;
	int matches;
	MmceScanProgress progress;
	int done;
} FindCtx;

static void findOne(const char *path, void *ctx)
{
	FindCtx *f = (FindCtx *)ctx;
	unsigned char fp[32];

	if (f->progress)
		f->progress(path, ++f->done);
	if (mmceCardFingerprint(path, fp) < 0 || memcmp(fp, f->active, sizeof(fp)))
		return;
	if (f->matches++ == 0)
		snprintf(f->out->path, sizeof(f->out->path), "%s", path);
}

int mmceActiveCard(int unit, MmceActiveCard *out, MmceScanProgress progress)
{
	unsigned char fp[32];
	char ap[48], expect[MAX_PATH];
	FindCtx f;

	memset(out, 0, sizeof(*out));
	out->card = devctl(unit, MMCE_DEVCTL_GET_CARD, NULL);
	out->channel = devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL);
	if (out->card < 0 && out->channel < 0)
		return MMCE_SWITCH_DEVICE;
	if (!activePath(unit, ap, sizeof(ap)) || mmceCardFingerprint(ap, f.active) < 0)
		return MMCE_SWITCH_UNKNOWN; /* number only; nothing confirmed */

	/* The reported number first: one fingerprint when the device is right. */
	if (out->card > 0 && out->channel > 0 &&
	    snprintf(expect, sizeof(expect), "mmce%d:/MemoryCards/PS2/Card%d/Card%d-%d.mcd", unit, out->card,
	             out->card, out->channel) < (int)sizeof(expect) &&
	    mmceCardFingerprint(expect, fp) == 0 && !memcmp(fp, f.active, sizeof(fp))) {
		snprintf(out->path, sizeof(out->path), "%s", expect);
		out->matches = 1;
		out->by_number = 1;
	}
	/* Otherwise (BootCard, a Game ID folder, or a stale number) by content. */
	f.out = out;
	f.matches = 0;
	f.progress = progress;
	f.done = 0;
	if (!out->by_number) {
		if (scanCards(unit, findOne, &f) < 0)
			return MMCE_SWITCH_UNKNOWN;
		out->matches = f.matches;
	}
	return out->matches == 1 ? MMCE_SWITCH_OK : MMCE_SWITCH_UNKNOWN;
}

static int waitIdle(int unit)
{
	int poll, status;

	for (poll = 0; poll < MMCE_SWITCH_POLLS; poll++) {
		status = devctl(unit, MMCE_DEVCTL_GET_STATUS, NULL);
		if (status < 0)
			return MMCE_SWITCH_DEVICE;
		if (!(status & 1))
			return MMCE_SWITCH_OK;
		DelayThread(1000000);
	}
	return MMCE_SWITCH_BUSY;
}

int mmceSwitchToCard(const char *path)
{
	unsigned char want[32], got[32];
	char ap[48];
	u32 arg;
	int unit = mmceCardUnit(path), card, chan, r, n, c;

	if (unit < 0 || !mmceCardParsePath(path, &card, &chan))
		return MMCE_SWITCH_NOT_NUMBERED;
	if (mmceCardFingerprint(path, want) < 0)
		return MMCE_SWITCH_NOT_CARD;
	r = waitIdle(unit);
	if (r != MMCE_SWITCH_OK)
		return r;

	arg = (0u << 24) | (0u << 16) | (u32)card; /* regular card, by number */
	if (devctl(unit, MMCE_DEVCTL_SET_CARD, &arg) < 0)
		return MMCE_SWITCH_DEVICE;
	r = waitIdle(unit);
	if (r != MMCE_SWITCH_OK)
		return r;
	c = devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL);
	if (c != chan) {
		arg = (0u << 16) | (u32)chan;
		if (devctl(unit, MMCE_DEVCTL_SET_CHANNEL, &arg) < 0)
			return MMCE_SWITCH_DEVICE;
		r = waitIdle(unit);
		if (r != MMCE_SWITCH_OK)
			return r;
	}

	/* Done only when number, channel and content all agree. */
	n = devctl(unit, MMCE_DEVCTL_GET_CARD, NULL);
	c = devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL);
	if (n != card || c != chan)
		return MMCE_SWITCH_UNKNOWN;
	if (!activePath(unit, ap, sizeof(ap)) || mmceCardFingerprint(ap, got) < 0 || memcmp(want, got, sizeof(want)))
		return MMCE_SWITCH_UNKNOWN;
	return MMCE_SWITCH_OK;
}
