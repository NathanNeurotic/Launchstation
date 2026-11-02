//--------------------------------------------------------------
//File name:   dash_ps1_card.c
//"Card contents..." for PS1 card images (128 KB .mcd/.mcr/.vm1/.vmc/.bin,
//as POPStarter, sd2psx/MMCE in PS1 mode and PC emulators use them): lists
//the saves, imports .mcs/.ps1/.mcb/.pda, PS1 .psv, .psx and raw saves,
//exports a save as .mcs and deletes one. mc_ps1_card.c does the format work
//on the whole image in memory.
//
//Every write:
//  1. reads the card again and stops if it changed since it was listed;
//  2. makes a verified safety copy of the card in WLE-Safety/;
//  3. rewrites the 128 KB in place (never truncated or recreated);
//  4. reads it back and compares every byte.
//The PS2 card tool's in-use check is not needed here: vmc0:/vmc1: mount PS2
//images only, and an MMCE device answering mmce: requests is in PS2 mode, so
//it serves a PS2 card, never a PS1 image (that check also cannot read a PS1
//image and would refuse every one).
//--------------------------------------------------------------
#include "dash_internal.h"
#include "filer_shared.h"
#include "mc_ps1_card.h"
#include "dash_card_tool.h"

static uint8_t p1_before[PS1_CARD_SIZE] __attribute__((aligned(64)));
static uint8_t p1_after[PS1_CARD_SIZE] __attribute__((aligned(64)));
static uint8_t p1_now[PS1_CARD_SIZE] __attribute__((aligned(64)));
static Ps1CardSave p1_saves[PS1_BLOCKS];
static char p1_titles[PS1_BLOCKS][96];
static char p1_details[PS1_BLOCKS][64];
static DashMenuItem p1_items[PS1_BLOCKS + 1];

static const char *p1Base(const char *path)
{
	const char *s = strrchr(path, '/');

	if (s == NULL)
		s = strrchr(path, ':');
	return s != NULL ? s + 1 : path;
}

/* Reads the whole card; -1 unless it is exactly 128 KB. */
static int p1Load(const char *path, uint8_t *buf)
{
	int fd = genOpen(path, FIO_O_RDONLY), n, ok;
	uint32_t done = 0;

	if (fd < 0)
		return -1;
	ok = genLseek(fd, 0, SEEK_END) == (s64)PS1_CARD_SIZE && genLseek(fd, 0, SEEK_SET) == 0;
	while (ok && done < PS1_CARD_SIZE) {
		n = genRead(fd, buf + done, (int)(PS1_CARD_SIZE - done > 32768u ? 32768u : PS1_CARD_SIZE - done));
		if (n <= 0)
			ok = 0;
		else
			done += (uint32_t)n;
	}
	genClose(fd);
	return ok ? 0 : -1;
}

int dashPs1CardIsImage(const char *path)
{
	int fd = genOpen(path, FIO_O_RDONLY), ok;
	uint8_t magic[2];

	if (fd < 0)
		return 0;
	ok = genLseek(fd, 0, SEEK_END) == (s64)PS1_CARD_SIZE && genLseek(fd, 0, SEEK_SET) == 0 &&
	     genRead(fd, magic, 2) == 2 && magic[0] == 'M' && magic[1] == 'C';
	genClose(fd);
	return ok;
}

/* Rewrites the card in place: no create, no truncate. */
static int p1WriteAll(const char *path, const uint8_t *buf)
{
	int fd = genOpen(path, FIO_O_WRONLY), n, ok = 1;
	uint32_t done = 0;

	if (fd < 0)
		return -1;
	while (ok && done < PS1_CARD_SIZE) {
		n = genWrite(fd, (void *)(buf + done), (int)(PS1_CARD_SIZE - done > 32768u ? 32768u : PS1_CARD_SIZE - done));
		if (n <= 0)
			ok = 0;
		else
			done += (uint32_t)n;
	}
	if (genClose(fd) < 0)
		ok = 0;
	return ok ? 0 : -1;
}

static int p1Safety(const char *path, const uint8_t *buf, char *made, int made_size)
{
	return dashCardSafetyCopyBuffer(path, buf, PS1_CARD_SIZE, made, made_size);
}

/* Writes after over the card that held before; PS1_OK, or -1 and a message
 * in *why. */
static int p1Commit(const char *path, const uint8_t *before, const uint8_t *after, char *safety, int safety_size,
                    const char **why)
{
	uint8_t *now = p1_now;

	safety[0] = 0;
	if (p1Load(path, now) < 0 || memcmp(now, before, PS1_CARD_SIZE) != 0) {
		*why = "The card changed or could not be read again since it was listed. Nothing was written.";
		return -1;
	}
	dashLoadingStatus("Safety copy", 0, 1);
	if (p1Safety(path, before, safety, safety_size) < 0) {
		safety[0] = 0;
		*why = "The safety copy could not be made, so the card was not changed. The device may be full or read-only.";
		return -1;
	}
	dashLoadingStatus("Writing", 0, 1);
	if (p1WriteAll(path, after) < 0) {
		*why = "Writing the card failed. Restore it from the safety copy.";
		return -1;
	}
	if (p1Load(path, now) < 0 || memcmp(now, after, PS1_CARD_SIZE) != 0) {
		*why = "The card did not read back as written. Restore it from the safety copy.";
		return -1;
	}
	return PS1_OK;
}

static void p1Report(int r, const char *ok, const char *why, const char *safety, const char *path)
{
	char detail[MAX_PATH + 32];

	if (r == PS1_OK) {
		dashToast(DASH_TOAST_OK, ok);
		return;
	}
	if (safety[0])
		snprintf(detail, sizeof(detail), "Safety copy: %s", safety);
	else
		dashStrCopy(detail, path, sizeof(detail));
	dashModalMessage(DI_WARNING, "Not done", why, detail);
}

static int p1ReadFile(const char *path, uint8_t **out, uint32_t *len)
{
	int fd = genOpen(path, FIO_O_RDONLY), n;
	s64 size;
	uint32_t done = 0;
	uint8_t *b;

	*out = NULL;
	if (fd < 0)
		return -1;
	size = genLseek(fd, 0, SEEK_END);
	/* the largest PS1 save: 15 blocks plus the biggest header (.psv) */
	if (size <= 0 || size > (s64)(PS1_BLOCKS * PS1_BLOCK + 0x84u) || genLseek(fd, 0, SEEK_SET) != 0) {
		genClose(fd);
		return -2;
	}
	b = (uint8_t *)malloc((size_t)size);
	if (!b) {
		genClose(fd);
		return -1;
	}
	while (done < (uint32_t)size) {
		n = genRead(fd, b + done, (int)((uint32_t)size - done));
		if (n <= 0)
			break;
		done += (uint32_t)n;
	}
	genClose(fd);
	if (done != (uint32_t)size) {
		free(b);
		return -1;
	}
	*out = b;
	*len = done;
	return 0;
}

static void p1Import(const char *path)
{
	char dir[MAX_PATH], file[MAX_PATH], safety[MAX_PATH], msg[160];
	const char *why = "";
	uint8_t *buf;
	uint32_t len;
	Ps1Save s;
	int r, fmt;

	dashStrCopy(dir, path, sizeof(dir));
	dir[p1Base(path) - path] = 0;
	/* every file: raw saves have no extension, the reader decides by content */
	if (!dashPickPath("Import which PS1 save?", dir, DASH_PICK_FILE, NULL, file, sizeof(file)))
		return;
	r = p1ReadFile(file, &buf, &len);
	if (r < 0) {
		dashModalMessage(DI_WARNING, "Not imported",
		                 r == -2 ? "Too big or empty for a PS1 save." : "The file could not be read.", file);
		return;
	}
	r = ps1SaveFromFile(buf, len, p1Base(file), &s, &fmt);
	free(buf);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not imported", ps1Error(r), file);
		return;
	}
	memcpy(p1_after, p1_before, PS1_CARD_SIZE);
	r = ps1CardWrite(p1_after, &s);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not imported", ps1Error(r), s.name[0] ? s.name : file);
		ps1SaveFree(&s);
		return;
	}
	snprintf(msg, sizeof(msg), "%s, %d block%s, from a %s. The card gets a safety copy first.", s.name, s.blocks,
	         s.blocks == 1 ? "" : "s", ps1SaveFormatName(fmt));
	ps1SaveFree(&s);
	if (!dashModalConfirm("Import this save?", msg, "Import", 0))
		return;
	dashLoadingBegin("Importing save");
	r = p1Commit(path, p1_before, p1_after, safety, sizeof(safety), &why);
	dashLoadingEnd();
	p1Report(r, "Save imported and verified", why, safety, path);
}

static void p1Export(const char *path, const Ps1CardSave *sv)
{
	char dest[MAX_PATH], folder[MAX_PATH];
	uint8_t *buf = NULL;
	uint32_t len = 0;
	Ps1Save s;
	char safe[PS1_NAME_MAX + 1];
	int r, i;

	r = ps1CardRead(p1_before, sv->first, &s);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not exported", ps1Error(r), sv->name);
		return;
	}
	/* build the .mcs first: it refuses a name that is not plain ASCII (a card
	 * can hold any bytes there), before any file name is made from it */
	r = ps1SaveToMcs(&s, &buf, &len);
	if (r != PS1_OK) {
		ps1SaveFree(&s);
		dashModalMessage(DI_WARNING, "Not exported", ps1Error(r), sv->name);
		return;
	}
	dashStrCopy(folder, path, sizeof(folder));
	folder[p1Base(path) - path] = 0;
	/* the name is printable ASCII now, but path separators, characters FAT
	 * refuses and a leading dot become '_' so the file lands next to the card */
	for (i = 0; s.name[i]; i++)
		safe[i] = (strchr("/\\:*?\"<>|", s.name[i]) != NULL || (i == 0 && s.name[i] == '.')) ? '_' : s.name[i];
	safe[i] = 0;
	ps1SaveFree(&s);
	for (i = 1; i < 100; i++) {
		int w = i == 1 ? snprintf(dest, sizeof(dest), "%s%s.mcs", folder, safe) :
		                 snprintf(dest, sizeof(dest), "%s%s-%d.mcs", folder, safe, i);
		if (w >= (int)sizeof(dest)) {
			i = 100;
			break;
		}
		if (!wleExists(dest))
			break;
	}
	if (i == 100) {
		free(buf);
		dashModalMessage(DI_WARNING, "Not exported", "No free .mcs name next to the card.", folder);
		return;
	}
	dashLoadingBegin("Exporting save");
	r = dashCardWriteVerified(dest, buf, len);
	dashLoadingEnd();
	free(buf);
	if (r == 0)
		dashModalMessage(DI_MC, "Exported", "Written and read back as .mcs. The card was not changed.", dest);
	else
		dashModalMessage(DI_WARNING, "Not exported", "The .mcs could not be written. The device may be full or read-only.", dest);
}

static void p1Delete(const char *path, const Ps1CardSave *sv)
{
	char safety[MAX_PATH], msg[128];
	const char *why = "";
	int r;

	snprintf(msg, sizeof(msg), "%s, %d block%s. The card gets a safety copy first.", sv->name, sv->blocks,
	         sv->blocks == 1 ? "" : "s");
	if (!dashModalConfirm("Delete this save?", msg, "Delete", 1))
		return;
	memcpy(p1_after, p1_before, PS1_CARD_SIZE);
	r = ps1CardDelete(p1_after, sv->first);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not deleted", ps1Error(r), sv->name);
		return;
	}
	dashLoadingBegin("Deleting save");
	r = p1Commit(path, p1_before, p1_after, safety, sizeof(safety), &why);
	dashLoadingEnd();
	p1Report(r, "Save deleted", why, safety, path);
}

static void p1SaveMenu(const char *path, const Ps1CardSave *sv, const char *title)
{
	DashMenuItem it[2];
	int r;

	it[0].icon = DI_MC;
	it[0].label = "Export as .mcs...";
	it[0].detail = sv->damaged ? "its blocks do not add up" : "next to the card; the card is only read";
	it[0].disabled = sv->damaged;
	it[1].icon = DI_WARNING;
	it[1].label = "Delete from this card...";
	it[1].detail = sv->damaged ? "its blocks do not add up" : "safety copy of the card first";
	it[1].disabled = sv->damaged;
	r = dashModalMenu(title, sv->name, it, 2, 0);
	if (r == 0 && !it[0].disabled)
		p1Export(path, sv);
	else if (r == 1 && !it[1].disabled)
		p1Delete(path, sv);
}

void dashPs1CardTool(const char *image)
{
	char path[MAX_PATH], subtitle[96];
	int n, i, r, sel = 0;

	dashStrCopy(path, image, sizeof(path));
	for (;;) {
		dashLoadingBegin("Reading card");
		r = p1Load(path, p1_before);
		dashLoadingEnd();
		if (r < 0 || !ps1CardIs(p1_before, PS1_CARD_SIZE)) {
			dashModalMessage(DI_WARNING, "Cannot show this card", ps1Error(PS1_ERR_NOT_CARD), path);
			return;
		}
		n = ps1CardList(p1_before, p1_saves, PS1_BLOCKS);
		p1_items[0].icon = DI_PLUS;
		p1_items[0].label = "Import save...";
		p1_items[0].detail = ".mcs, .psv, .psx or raw; safety copy first";
		p1_items[0].disabled = 0;
		for (i = 0; i < n; i++) {
			const Ps1CardSave *sv = &p1_saves[i];

			if (sv->title[0])
				dashSjisTitle(p1_titles[i], sizeof(p1_titles[i]), sv->title);
			else
				dashStrCopy(p1_titles[i], sv->name, sizeof(p1_titles[i]));
			if (sv->damaged)
				snprintf(p1_details[i], sizeof(p1_details[i]), "%s, blocks do not add up", sv->name);
			else
				snprintf(p1_details[i], sizeof(p1_details[i]), "%s, %d block%s", sv->name, sv->blocks,
				         sv->blocks == 1 ? "" : "s");
			p1_items[i + 1].icon = sv->damaged ? DI_WARNING : DI_MC;
			p1_items[i + 1].label = p1_titles[i];
			p1_items[i + 1].detail = p1_details[i];
			p1_items[i + 1].disabled = 0;
		}
		snprintf(subtitle, sizeof(subtitle), "PS1 card: %d save%s, %d of 15 blocks free", n, n == 1 ? "" : "s",
		         ps1CardFreeBlocks(p1_before));
		if (sel > n)
			sel = n;
		r = dashModalMenu(p1Base(path), subtitle, p1_items, n + 1, sel);
		if (r < 0)
			return;
		sel = r;
		if (r == 0) {
			p1Import(path);
		} else {
			Ps1CardSave sv = p1_saves[r - 1];
			char title[96];

			dashStrCopy(title, p1_titles[r - 1], sizeof(title));
			p1SaveMenu(path, &sv, title);
		}
	}
}

void dashPs1CardNewBlank(const char *folder)
{
	char dest[MAX_PATH], base[MAX_PATH];
	size_t len;
	int i, fd, ok;

	dashStrCopy(base, folder, sizeof(base));
	len = strlen(base);
	if (len == 0 || len + 2 >= sizeof(base))
		return;
	if (base[len - 1] != '/' && base[len - 1] != ':') {
		base[len] = '/';
		base[len + 1] = 0;
	}
	for (i = 1; i < 100; i++) {
		int w = i == 1 ? snprintf(dest, sizeof(dest), "%sBlank-PS1.mcd", base) :
		                 snprintf(dest, sizeof(dest), "%sBlank-PS1-%d.mcd", base, i);
		if (w >= (int)sizeof(dest))
			return;
		if (!wleExists(dest))
			break;
	}
	if (i == 100) {
		dashModalMessage(DI_WARNING, "Choose another folder", "This folder already holds 99 blank PS1 cards.", base);
		return;
	}
	if (!dashModalConfirm("Make a blank PS1 card?", "An empty 128 KB PS1 card image is made. Nothing else is changed.",
	                      "Make", 0))
		return;
	dashLoadingBegin("Making a blank card");
	ps1CardFormat(p1_after);
	fd = genOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
	ok = fd >= 0;
	if (ok) {
		genClose(fd);
		ok = p1WriteAll(dest, p1_after) == 0 && p1Load(dest, p1_now) == 0 &&
		     memcmp(p1_now, p1_after, PS1_CARD_SIZE) == 0;
		if (!ok)
			genRemove(dest);
	}
	dashLoadingEnd();
	if (ok)
		dashModalMessage(DI_MC, "Blank PS1 card made", "Formatted and read back: 15 blocks free.", dest);
	else
		dashModalMessage(DI_WARNING, "Blank card not made", "The device may be read-only or full.", dest);
}
