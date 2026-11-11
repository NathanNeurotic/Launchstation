//--------------------------------------------------------------
//File name:   dash_ps1_card.c
//"Card contents..." for PS1 card images (128 KB .mcd/.mcr/.vm1/.vmc/.bin/.srm,
//as POPStarter, sd2psx/MMCE in PS1 mode and PC emulators use them, and the
//DexDrive .gme, VGS .vgs/.mem, PSP/Vita .vmp and PocketStation iOS .mcx
//containers): lists the saves, imports .mcs/.ps1/.mcb/.pda, PS1 .psv, .psx
//and raw saves, exports a save as .mcs, signed PS3 .psv, .psx or raw,
//deletes one, and saves a copy of the card in another container.
//mc_ps1_card.c and mc_save_export.c do the format work in memory; a card
//is written back in the container and with the header it came in.
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
#include "mc_save_export.h"

static uint8_t p1_before[PS1_CARD_SIZE] __attribute__((aligned(64)));
static uint8_t p1_after[PS1_CARD_SIZE] __attribute__((aligned(64)));
static uint8_t p1_now[PS1_CARD_SIZE] __attribute__((aligned(64)));
static Ps1CardSave p1_saves[PS1_BLOCKS];
static char p1_titles[PS1_BLOCKS][96];
static char p1_details[PS1_BLOCKS][64];
static DashMenuItem p1_items[PS1_BLOCKS + 2];
static int p1_kind;                        /* the listed card's container */
static uint8_t p1_head[PS1_CARD_HEAD_MAX]; /* and the header it came with */

static const char *p1Base(const char *path)
{
	const char *s = strrchr(path, '/');

	if (s == NULL)
		s = strrchr(path, ':');
	return s != NULL ? s + 1 : path;
}

/* Reads the whole card file and opens its container: the raw card into raw,
 * its header into head (unless NULL). The container, or -1. */
static int p1Load(const char *path, uint8_t *raw, uint8_t *head)
{
	int fd = genOpen(path, FIO_O_RDONLY), n, kind = -1;
	s64 size;
	uint32_t done = 0;
	uint8_t *b = NULL;

	if (fd < 0)
		return -1;
	size = genLseek(fd, 0, SEEK_END);
	if (size >= (s64)PS1_CARD_SIZE && size <= (s64)PS1_CARD_FILE_MAX && genLseek(fd, 0, SEEK_SET) == 0)
		b = (uint8_t *)malloc((size_t)size);
	while (b != NULL && done < (uint32_t)size) {
		n = genRead(fd, b + done, (int)((uint32_t)size - done > 32768u ? 32768u : (uint32_t)size - done));
		if (n <= 0)
			break;
		done += (uint32_t)n;
	}
	genClose(fd);
	if (b != NULL && done == (uint32_t)size) {
		kind = ps1CardKind(b, done);
		n = kind < 0 ? -1 : ps1CardOpen(b, done, kind);
		if (n < 0) {
			kind = -1;
		} else {
			memcpy(raw, b + n, PS1_CARD_SIZE);
			if (head != NULL)
				memcpy(head, b, (uint32_t)n);
		}
	}
	free(b);
	return kind;
}

/* 1 when exactly n bytes were read, however the device splits them */
static int p1ReadAll(int fd, uint8_t *b, uint32_t n)
{
	int r;

	for (; n; b += r, n -= (uint32_t)r)
		if ((r = genRead(fd, b, (int)n)) <= 0)
			return 0;
	return 1;
}

int dashPs1CardIsImage(const char *path)
{
	int fd = genOpen(path, FIO_O_RDONLY), ok;
	uint8_t head[128];
	s64 size;
	int i;

	if (fd < 0)
		return 0;
	size = genLseek(fd, 0, SEEK_END);
	ok = size >= (s64)PS1_CARD_SIZE && size <= (s64)PS1_CARD_FILE_MAX && genLseek(fd, 0, SEEK_SET) == 0 &&
	     p1ReadAll(fd, head, sizeof(head)) && ps1CardKind(head, (uint32_t)size) >= 0;
	/* other cores' .srm saves can be 128 KB and start "MC" by chance: there
	 * every directory frame must check too before the card is written */
	for (i = 0; ok && genCmpFileExt(path, "SRM") && i <= PS1_BLOCKS; i++)
		ok = (i == 0 || p1ReadAll(fd, head, sizeof(head))) && ps1FrameOk(head);
	genClose(fd);
	return ok;
}

/* raw in a container: kind, with head (NULL for a new one). Free with free(). */
static uint8_t *p1Sealed(int kind, const uint8_t *raw, const uint8_t *head)
{
	uint8_t *b = (uint8_t *)malloc(ps1CardFileSize(kind));

	if (b != NULL && ps1CardSeal(kind, raw, head, b) < 0) {
		free(b);
		b = NULL;
	}
	return b;
}

/* Rewrites the card file in place (no create, no truncate), raw sealed in
 * kind with head. */
static int p1WriteAll(const char *path, int kind, const uint8_t *raw, const uint8_t *head)
{
	uint8_t *buf = p1Sealed(kind, raw, head);
	uint32_t done = 0, len = ps1CardFileSize(kind);
	int fd = buf != NULL ? genOpen(path, FIO_O_WRONLY) : -1, n, ok = fd >= 0;

	while (ok && done < len) {
		n = genWrite(fd, (void *)(buf + done), (int)(len - done > 32768u ? 32768u : len - done));
		if (n <= 0)
			ok = 0;
		else
			done += (uint32_t)n;
	}
	if (fd >= 0 && genClose(fd) < 0)
		ok = 0;
	free(buf);
	return ok ? 0 : -1;
}

/* The safety copy holds the card file as it was: the same container. */
static int p1Safety(const char *path, const uint8_t *raw, char *made, int made_size)
{
	uint8_t *buf = p1Sealed(p1_kind, raw, p1_head);
	int r = buf != NULL ? dashCardSafetyCopyBuffer(path, buf, ps1CardFileSize(p1_kind), made, made_size) : -1;

	free(buf);
	return r;
}

/* Writes after over the card that held before; PS1_OK, or -1 and a message
 * in *why. */
static int p1Commit(const char *path, const uint8_t *before, const uint8_t *after, char *safety, int safety_size,
                    const char **why)
{
	uint8_t *now = p1_now;

	safety[0] = 0;
	if (p1Load(path, now, NULL) != p1_kind || memcmp(now, before, PS1_CARD_SIZE) != 0) {
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
	if (p1WriteAll(path, p1_kind, after, p1_head) < 0) {
		*why = "Writing the card failed. Restore it from the safety copy.";
		return -1;
	}
	if (p1Load(path, now, NULL) != p1_kind || memcmp(now, after, PS1_CARD_SIZE) != 0) {
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

/* Reads the PS1 save in file into *s (free with ps1SaveFree); 1 when read,
 * 0 after telling why not. */
static int p1ReadSave(const char *file, Ps1Save *s, int *fmt)
{
	uint8_t *buf;
	uint32_t len;
	int r = p1ReadFile(file, &buf, &len);

	if (r < 0) {
		dashModalMessage(DI_WARNING, "Not imported",
		                 r == -2 ? "Too big or empty for a PS1 save." : "The file could not be read.", file);
		return 0;
	}
	r = ps1SaveFromFile(buf, len, p1Base(file), s, fmt);
	free(buf);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not imported", ps1Error(r), file);
		return 0;
	}
	return 1;
}

/* Writes s into card; 1 when it fits, 0 after telling why not. Frees s. */
static int p1Put(uint8_t *card, Ps1Save *s, const char *file)
{
	int r = ps1CardWrite(card, s);

	if (r != PS1_OK)
		dashModalMessage(DI_WARNING, "Not imported", ps1Error(r), s->name[0] ? s->name : file);
	ps1SaveFree(s);
	return r == PS1_OK;
}

static void p1Import(const char *path)
{
	char dir[MAX_PATH], file[MAX_PATH], safety[MAX_PATH], msg[160];
	const char *why = "";
	Ps1Save s;
	int r, fmt;

	dashStrCopy(dir, path, sizeof(dir));
	dir[p1Base(path) - path] = 0;
	/* every file: raw saves have no extension, the reader decides by content */
	if (!dashPickPath("Import which PS1 save?", dir, DASH_PICK_FILE, NULL, file, sizeof(file)) ||
	    !p1ReadSave(file, &s, &fmt))
		return;
	snprintf(msg, sizeof(msg), "%s, %d block%s, from a %s. The card gets a safety copy first.", s.name, s.blocks,
	         s.blocks == 1 ? "" : "s", ps1SaveFormatName(fmt));
	memcpy(p1_after, p1_before, PS1_CARD_SIZE);
	if (!p1Put(p1_after, &s, file) || !dashModalConfirm("Import this save?", msg, "Import", 0))
		return;
	dashLoadingBegin("Importing save");
	r = p1Commit(path, p1_before, p1_after, safety, sizeof(safety), &why);
	dashLoadingEnd();
	p1Report(r, "Save imported and verified", why, safety, path);
}

/* dest = folder + stem + ext; when that is taken and suffix is set, stem-2
 * up to stem-99. 0, or -1 when no name is free or the path is too long. */
static int p1FreeName(char *dest, int size, const char *folder, const char *stem, const char *ext, int suffix)
{
	int i, w;

	for (i = 1; i < 100; i++) {
		w = i == 1 ? snprintf(dest, size, "%s%s%s", folder, stem, ext) :
		             snprintf(dest, size, "%s%s-%d%s", folder, stem, i, ext);
		if (w >= size)
			return -1;
		if (!wleExists(dest))
			return 0;
		if (!suffix)
			return -1;
	}
	return -1;
}

static const char *const p1_fmts[] = {".mcs: PS1 single save", ".psv: PS3, signed", ".psx: Action Replay / GameShark",
                                     "raw: the blocks alone, named after the save"};

/* Writes s (freed here) into folder as format f of p1_fmts, named after the
 * save, read back. 0 when written, with dest the file; otherwise *why says
 * why and dest names the save, folder or file it is about. */
static int p1Write(const char *folder, Ps1Save *s, int f, char *dest, const char **why)
{
	char safe[PS1_NAME_MAX + 1], stem[2 * PS1_NAME_MAX + 8];
	uint8_t *buf = NULL;
	uint32_t len = 0;
	int r, i, renamable;

	/* build the file first: it refuses a name that is not plain ASCII (a card
	 * can hold any bytes there), before any file name is made from it. A raw
	 * save is the .psx without its 54-byte header. */
	r = f == 0 ? ps1SaveToMcs(s, &buf, &len) : f == 1 ? ps1SaveToPsv(s, &buf, &len) : ps1SaveToPsx(s, &buf, &len);
	dashStrCopy(dest, s->name, MAX_PATH);
	if (r != PS1_OK) {
		*why = ps1Error(r);
		ps1SaveFree(s);
		return -1;
	}
	/* the name is printable ASCII now, but path separators, characters FAT
	 * refuses and a leading dot become '_' so the file lands in folder */
	for (i = 0; s->name[i]; i++)
		safe[i] = (strchr("/\\:*?\"<>|", s->name[i]) != NULL || (i == 0 && s->name[i] == '.')) ? '_' : s->name[i];
	safe[i] = 0;
	/* a raw save is named by its file name, read back without the last
	 * dotted suffix (and FAT trims a trailing space): refuse what would
	 * come back under another name, including a name changed just above */
	if (f == 3 && (strcmp(safe, s->name) || strchr(safe, '.') != NULL || (i > 0 && safe[i - 1] == ' '))) {
		free(buf);
		*why = "This save's name cannot be kept as raw. Use .mcs or .psx.";
		ps1SaveFree(s);
		return -1;
	}
	ps1SaveFree(s);
	if (f == 1)
		mcPsvFileName(stem, sizeof(stem), safe);
	else
		dashStrCopy(stem, safe, sizeof(stem));
	/* a PS3 finds a .psv, and a raw save is read back, by its exact name */
	renamable = f == 0 || f == 2;
	if (p1FreeName(dest, MAX_PATH, folder, stem, f == 0 ? ".mcs" : f == 2 ? ".psx" : "", renamable) < 0) {
		free(buf);
		*why = renamable ? "No free name in that folder." : "A file with this name is already in that folder.";
		dashStrCopy(dest, folder, MAX_PATH);
		return -1;
	}
	r = f == 3 ? dashCardWriteVerified(dest, buf + 54, len - 54u) : dashCardWriteVerified(dest, buf, len);
	free(buf);
	*why = "The file could not be written. The device may be full or read-only.";
	return r;
}

/* p1Write for one save, with its own messages; done says where it went. */
static int p1Out(const char *folder, Ps1Save *s, int f, const char *done)
{
	char dest[MAX_PATH];
	const char *why;
	int r;

	dashLoadingBegin("Exporting save");
	r = p1Write(folder, s, f, dest, &why);
	dashLoadingEnd();
	dashModalMessage(r ? DI_WARNING : DI_MC, r ? "Not exported" : "Exported", r ? why : done, dest);
	return r;
}

static void p1Export(const char *path, const Ps1CardSave *sv)
{
	char folder[MAX_PATH];
	Ps1Save s;
	int r, f;

	f = dashModalChoice("Export as", p1_fmts, 4, 0);
	if (f < 0)
		return;
	r = ps1CardRead(p1_before, sv->first, &s);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not exported", ps1Error(r), sv->name);
		return;
	}
	dashStrCopy(folder, path, sizeof(folder));
	folder[p1Base(path) - path] = 0;
	p1Out(folder, &s, f, "Written and read back. The card was not changed.");
}

/* "Convert to .mcs / .psv / .psx / raw" for one PS1 save file of any format
 * Import save... reads, without a card image in between (wLaunchELF_R3Z's
 * archive from a file). On a memory card (a real PS1 card in mc0:/mc1: lists
 * each save as a raw file) the user picks the folder; elsewhere it goes next
 * to the file. The source is only read. */
void dashPs1SaveConvert(const char *path)
{
	char folder[MAX_PATH], hint[MAX_PATH];
	uint8_t *buf;
	uint32_t len;
	Ps1Save s;
	int r, f, fmt, on_card;

	r = p1ReadFile(path, &buf, &len);
	if (r < 0) {
		dashModalMessage(DI_WARNING, "Not converted",
		                 r == -2 ? "Too big or empty for a PS1 save." : "The file could not be read.", path);
		return;
	}
	on_card = !strncmp(path, "mc", 2) || !strncmp(path, "vmc", 3);
	/* a save on a card is named by its whole file name, dots included: the
	 * reader drops everything from the last dot, so give it an empty one */
	snprintf(hint, sizeof(hint), on_card ? "%s." : "%s", p1Base(path));
	r = ps1SaveFromFile(buf, len, hint, &s, &fmt);
	free(buf);
	if (r != PS1_OK) {
		dashModalMessage(DI_WARNING, "Not converted", ps1Error(r), path);
		return;
	}
	f = dashModalChoice("Convert to", p1_fmts, 4, fmt == PS1F_MCS); /* a .mcs offers .psv first */
	if (f < 0) {
		ps1SaveFree(&s);
		return;
	}
	if (!on_card) {
		dashStrCopy(folder, path, sizeof(folder));
		folder[p1Base(path) - path] = 0;
	} else if (!dashPickPath("Write the PS1 save to", "mass:/", DASH_PICK_FOLDER, NULL, folder, sizeof(folder))) {
		ps1SaveFree(&s);
		return;
	}
	p1Out(folder, &s, f, "Written and read back. The original was not changed.");
}

/* dashPs1SaveConvert for the marked saves `names` in dir: one format for all
 * (wLaunchELF_R3Z's archive from several saves), one folder picked when dir
 * is a memory card. A save that fails is counted and the rest still go; the
 * sources are only read. */
void dashPs1SaveConvertMany(const char *dir, const char *const *names, int n)
{
	char path[MAX_PATH], folder[MAX_PATH], dest[MAX_PATH], first[MAX_PATH] = "", body[MAX_PATH + 96];
	const char *why = NULL, *first_why = NULL;
	uint8_t *buf;
	uint32_t len;
	Ps1Save s;
	int i, r, f, fmt, done = 0, failed = 0, on_card = !strncmp(dir, "mc", 2) || !strncmp(dir, "vmc", 3);

	f = dashModalChoice("Convert to", p1_fmts, 4, genCmpFileExt(names[0], "MCS"));
	if (f < 0)
		return;
	if (!on_card)
		dashStrCopy(folder, dir, sizeof(folder));
	else if (!dashPickPath("Write the PS1 saves to", "mass:/", DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;
	dashLoadingBegin("Exporting saves");
	for (i = 0; i < n; i++) {
		dashLoadingStatus(names[i], i, n);
		r = snprintf(path, sizeof(path), "%s%s", dir, names[i]) >= (int)sizeof(path) ? -1 : p1ReadFile(path, &buf, &len);
		if (r < 0)
			why = r == -2 ? "Too big or empty for a PS1 save." : "The file could not be read.";
		else {
			/* as in dashPs1SaveConvert: a card save's whole name, dots included */
			snprintf(path, sizeof(path), on_card ? "%s." : "%s", names[i]);
			r = ps1SaveFromFile(buf, len, path, &s, &fmt);
			free(buf);
			why = ps1Error(r);
			if (r == PS1_OK)
				r = p1Write(folder, &s, f, dest, &why);
		}
		if (r == 0)
			done++;
		else if (!failed++) {
			dashStrCopy(first, names[i], sizeof(first));
			first_why = why;
		}
	}
	dashLoadingEnd();
	r = snprintf(body, sizeof(body), "%d of %d written and read back. The originals were not changed.", done, n);
	if (failed)
		snprintf(body + r, sizeof(body) - r, " %d not exported. First: %s: %s", failed, first, first_why);
	dashModalMessage(failed ? DI_WARNING : DI_MC, failed ? "Not all exported" : "Exported", body, folder);
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
	it[0].label = "Export...";
	it[0].detail = sv->damaged ? "its blocks do not add up" : ".mcs, PS3 .psv, .psx or raw; the card is only read";
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

/* Writes raw as a new card file at folder + stem + ext (see p1FreeName),
 * created with O_EXCL and removed again unless it reads back as the same
 * card. 1 when made; dest holds its path. */
static int p1MakeNew(char *dest, int size, const char *folder, const char *stem, const char *ext, int suffix, int kind,
                     const uint8_t *raw, const uint8_t *head)
{
	int fd, ok;

	dest[0] = 0;
	if (p1FreeName(dest, size, folder, stem, ext, suffix) < 0) {
		dashModalMessage(DI_WARNING, "Not made",
		                 suffix ? "No free name in this folder." : "A file with this name is already in this folder.",
		                 dest[0] ? dest : folder);
		return 0;
	}
	dashLoadingBegin("Writing card");
	fd = genOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
	ok = fd >= 0;
	if (ok) {
		genClose(fd);
		ok = p1WriteAll(dest, kind, raw, head) == 0 && p1Load(dest, p1_now, NULL) == kind &&
		     memcmp(p1_now, raw, PS1_CARD_SIZE) == 0;
		if (!ok)
			genRemove(dest);
	}
	dashLoadingEnd();
	if (!ok)
		dashModalMessage(DI_WARNING, "Not made", "The device may be read-only or full.", dest);
	return ok;
}

static const char *const p1_kinds[] = {".mcd: raw card (as .mcr .vm1 .vmc .srm)", ".gme: DexDrive", ".vgs: Connectix VGS",
                                       ".vmp: PSP / Vita, signed", ".mcx: PocketStation for iOS"};

/* The listed card, saved next to it in another (or the same) container. */
static void p1CopyAs(const char *path)
{
	char folder[MAX_PATH], stem[MAX_PATH], ext[8], dest[MAX_PATH];
	const char *base = p1Base(path), *dot = strrchr(base, '.');
	int k = dashModalChoice("Save a copy as", p1_kinds, 5, p1_kind);

	if (k < 0)
		return;
	dashStrCopy(folder, path, sizeof(folder));
	folder[base - path] = 0;
	dashStrCopy(stem, base, sizeof(stem));
	if (dot != NULL)
		stem[dot - base] = 0;
	snprintf(ext, sizeof(ext), ".%s", ps1CardKindExt(k));
	if (p1MakeNew(dest, sizeof(dest), folder, stem, ext, 1, k, p1_before, k == p1_kind ? p1_head : NULL))
		dashModalMessage(DI_MC, "Copy saved", "Written and read back. This card was not changed.", dest);
}

void dashPs1CardTool(const char *image)
{
	char path[MAX_PATH], subtitle[128];
	int n, i, r, sel = 0;

	dashStrCopy(path, image, sizeof(path));
	for (;;) {
		dashLoadingBegin("Reading card");
		p1_kind = p1Load(path, p1_before, p1_head);
		dashLoadingEnd();
		if (p1_kind < 0) {
			dashModalMessage(DI_WARNING, "Cannot show this card", ps1Error(PS1_ERR_NOT_CARD), path);
			return;
		}
		n = ps1CardList(p1_before, p1_saves, PS1_BLOCKS);
		p1_items[0].icon = DI_PLUS;
		p1_items[0].label = "Import save...";
		p1_items[0].detail = ".mcs, .psv, .psx or raw; safety copy first";
		p1_items[0].disabled = 0;
		p1_items[1].icon = DI_MC;
		p1_items[1].label = "Save a copy as...";
		p1_items[1].detail = ".mcd, .gme, .vgs, .vmp or .mcx; this card is only read";
		p1_items[1].disabled = 0;
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
			p1_items[i + 2].icon = sv->damaged ? DI_WARNING : DI_MC;
			p1_items[i + 2].label = p1_titles[i];
			p1_items[i + 2].detail = p1_details[i];
			p1_items[i + 2].disabled = 0;
		}
		snprintf(subtitle, sizeof(subtitle), "PS1 card%s%s%s: %d save%s, %d of 15 blocks free",
		         p1_kind ? " (" : "", p1_kind ? ps1CardKindName(p1_kind) : "", p1_kind ? ")" : "", n,
		         n == 1 ? "" : "s", ps1CardFreeBlocks(p1_before));
		if (sel > n + 1)
			sel = n + 1;
		r = dashModalMenu(p1Base(path), subtitle, p1_items, n + 2, sel);
		if (r < 0)
			return;
		sel = r;
		if (r == 0) {
			p1Import(path);
		} else if (r == 1) {
			p1CopyAs(path);
		} else {
			Ps1CardSave sv = p1_saves[r - 2];
			char title[96];

			dashStrCopy(title, p1_titles[r - 2], sizeof(title));
			p1SaveMenu(path, &sv, title);
		}
	}
}

/* A new PS1 card: a plain image, the names POPStarter and Ember look for,
 * or one of the containers; then Card contents opens on it. Empty, or
 * holding the n saves names[] in folder (wLaunchELF_R3Z's "Create archive"
 * of several saves): each is read and placed in memory first, so a save
 * that cannot be read or does not fit stops it before anything is written.
 * The fixed names are never given a -2 suffix. */
void dashPs1CardNew(const char *folder, const char *const *names, int n)
{
	static const char *const tails[] = {
	    ".mcd: raw card",   "SLOT0.VMC: POPStarter card 1", "SLOT1.VMC: POPStarter card 2", "MC1.vmc: Ember card 1",
	    "MC2.vmc: Ember card 2", ".VM1: PS3", ".gme: DexDrive", ".vgs: Connectix VGS", ".vmp: PSP / Vita",
	    ".mcx: PocketStation for iOS"};
	static const char *const stems[] = {NULL, "SLOT0", "SLOT1", "MC1", "MC2"};
	static const char *const exts[] = {".mcd", ".VMC", ".VMC", ".vmc", ".vmc", ".VM1", ".gme", ".vgs", ".vmp", ".mcx"};
	static const unsigned char kinds[] = {PS1C_RAW, PS1C_RAW, PS1C_RAW, PS1C_RAW, PS1C_RAW,
	                                      PS1C_RAW, PS1C_GME, PS1C_VGS, PS1C_VMP, PS1C_MCX};
	const char *stem = n > 0 ? "PS1-Saves" : "Blank-PS1", *opts[10];
	char dest[MAX_PATH], base[MAX_PATH], labels[10][40], msg[96];
	size_t len;
	int c, i, fmt;
	Ps1Save s;

	if (n < 0 || n > PS1_BLOCKS || (n > 0 && names == NULL))
		return;
	dashStrCopy(base, folder, sizeof(base));
	len = strlen(base);
	if (len == 0 || len + 2 >= sizeof(base))
		return;
	if (base[len - 1] != '/' && base[len - 1] != ':') {
		base[len] = '/';
		base[len + 1] = 0;
	}
	for (i = 0; i < 10; i++) {
		snprintf(labels[i], sizeof(labels[i]), "%s%s", tails[i][0] == '.' ? stem : "", tails[i]);
		opts[i] = labels[i];
	}
	c = dashModalChoice(n > 0 ? "PS1 card from these saves" : "New PS1 card", opts, 10, 0);
	if (c < 0)
		return;
	ps1CardFormat(p1_after);
	for (i = 0; i < n; i++) {
		if (snprintf(dest, sizeof(dest), "%s%s", base, names[i]) >= (int)sizeof(dest)) {
			dashModalMessage(DI_WARNING, "Not imported", "The path is too long.", names[i]);
			return;
		}
		if (!p1ReadSave(dest, &s, &fmt) || !p1Put(p1_after, &s, dest))
			return;
	}
	if (n > 0)
		snprintf(msg, sizeof(msg), "%d save%s, %d of 15 blocks free. The saves themselves are not changed.", n,
		         n == 1 ? "" : "s", ps1CardFreeBlocks(p1_after));
	if (!dashModalConfirm("Make a new PS1 card?",
	                      n > 0 ? msg : "An empty card is made, then opened to import saves. Nothing else is changed.",
	                      "Make", 0))
		return;
	if (!p1MakeNew(dest, sizeof(dest), base, c < 5 && stems[c] ? stems[c] : stem, exts[c], c == 0 || c >= 5, kinds[c],
	               p1_after, NULL))
		return;
	snprintf(msg, sizeof(msg), "PS1 card made and read back: %d of 15 blocks free", ps1CardFreeBlocks(p1_after));
	dashToast(DASH_TOAST_OK, msg);
	dashPs1CardTool(dest);
}
