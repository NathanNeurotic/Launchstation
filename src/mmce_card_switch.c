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
//Only numbered cards can be selected, and only while the device serves a
//numbered card: MMCEMAN has no way to select a Game ID card by number, and
//sd2psx / MemCard PRO2 firmware does not leave a BootCard or Game ID card for
//a card number and channel (as reported by R3Z3N). SET_CARD_CHANNEL is
//not used: older sd2psx and MemCard PRO2 firmware do not have it.
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

/* The served image is confirmed: number, channel and content all agree and
 * the device is not in the middle of opening a card. */
static int servesCard(int unit, int card, int chan, const unsigned char *want)
{
	unsigned char got[32], mine[32];
	char ap[48], p[MAX_PATH];
	int status = devctl(unit, MMCE_DEVCTL_GET_STATUS, NULL);

	if (status < 0 || (status & 1))
		return 0;
	if (devctl(unit, MMCE_DEVCTL_GET_CARD, NULL) != card || devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL) != chan)
		return 0;
	if (want == NULL) { /* that number's own image */
		if (snprintf(p, sizeof(p), "mmce%d:/MemoryCards/PS2/Card%d/Card%d-%d.mcd", unit, card, card, chan) >=
		        (int)sizeof(p) ||
		    mmceCardFingerprint(p, mine) < 0)
			return 0;
		want = mine;
	}
	return activePath(unit, ap, sizeof(ap)) && mmceCardFingerprint(ap, got) == 0 && !memcmp(want, got, 32);
}

/* The firmware keeps one pending command and acts on it later (not while
 * the console writes, not while a button on the device is held), and
 * GET_STATUS only says "busy" while a card is being opened. So a SET is
 * known to have been taken only when GET_CARD / GET_CHANNEL report the new
 * value; a second SET sent before that can replace the first one. */
static int waitReported(int unit, int cmd, int want)
{
	int poll, v;

	for (poll = 0; poll < MMCE_SWITCH_POLLS; poll++) {
		v = devctl(unit, cmd, NULL);
		if (v < 0)
			return MMCE_SWITCH_DEVICE;
		if (v == want)
			return MMCE_SWITCH_OK;
		DelayThread(1000000);
	}
	return MMCE_SWITCH_UNKNOWN;
}

int mmceSwitchToCard(const char *path)
{
	unsigned char want[32];
	u32 arg;
	int unit = mmceCardUnit(path), card, chan, r, n, c, poll, sent = 0;

	if (unit < 0 || !mmceCardParsePath(path, &card, &chan))
		return MMCE_SWITCH_NOT_NUMBERED;
	if (mmceCardFingerprint(path, want) < 0)
		return MMCE_SWITCH_NOT_CARD;
	r = waitIdle(unit);
	if (r != MMCE_SWITCH_OK)
		return r;
	n = devctl(unit, MMCE_DEVCTL_GET_CARD, NULL);
	c = devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL);
	if (n < 0 || c < 0)
		return MMCE_SWITCH_DEVICE;
	/* Card 0 is a BootCard or a Game ID card. sd2psx, and MemCard PRO2 even
	 * less, cannot be moved from those to a card number and channel through
	 * MMCEMAN; that is done on the device itself. A named card reports its
	 * position in the folder list, which looks like a card number, so the
	 * reported card must also be CardN/CardN-C.mcd by content. Nothing is
	 * sent otherwise. */
	if (n == 0 || !servesCard(unit, n, c, NULL))
		return MMCE_SWITCH_SPECIAL;

	/* Never SET_CARD to the card already selected: the firmware would reset
	 * its channel to 1 without switching. */
	if (n != card) {
		arg = (0u << 24) | (0u << 16) | (u32)card; /* regular card, by number */
		if (devctl(unit, MMCE_DEVCTL_SET_CARD, &arg) < 0)
			return MMCE_SWITCH_DEVICE;
		sent = 1;
		r = waitReported(unit, MMCE_DEVCTL_GET_CARD, card);
		if (r != MMCE_SWITCH_OK)
			return r;
		c = devctl(unit, MMCE_DEVCTL_GET_CHANNEL, NULL); /* now channel 1 */
	}
	if (c != chan) {
		arg = (0u << 16) | (u32)chan;
		if (devctl(unit, MMCE_DEVCTL_SET_CHANNEL, &arg) < 0)
			return MMCE_SWITCH_DEVICE;
		sent = 1;
		r = waitReported(unit, MMCE_DEVCTL_GET_CHANNEL, chan);
		if (r != MMCE_SWITCH_OK)
			return r;
	}

	/* Done only when number, channel and content all agree. The switch
	 * itself happens a little after the number changes, so keep looking. */
	for (poll = 0; poll < (sent ? MMCE_SWITCH_POLLS : 1); poll++) {
		if (servesCard(unit, card, chan, want))
			return MMCE_SWITCH_OK;
		if (sent)
			DelayThread(1000000);
	}
	r = devctl(unit, MMCE_DEVCTL_GET_STATUS, NULL);
	return r >= 0 && (r & 1) ? MMCE_SWITCH_BUSY : MMCE_SWITCH_UNKNOWN;
}
