//--------------------------------------------------------------
//File name:   dash_card_tool.c
//"Card contents..." for memory card images in the File Browser: lists the
//saves inside a .mcd/.ps2/.bin image, and copies, moves, deletes, exports and
//imports (.psu) single saves without mounting the image.
//
//Every write follows the same steps:
//  1. refuse an MMCE image the device is serving to the console right now
//     (mmce_card_switch.c: GET_CARD/GET_CHANNEL, then content; unknown refuses);
//  2. check name and free space with the image opened read-only;
//  3. make a verified safety copy of the whole image in WLE-Safety/;
//  4. write through mc_card_write.c (crash-safe order, read-back);
//  5. close, reopen read-only and check the result once more.
//Only one file is open at a time (the MMCE driver allows one).
//--------------------------------------------------------------
#include "dash_internal.h"
#include "filer_shared.h"
#include "mc_card_view.h"
#include "mc_card_write.h"
#include "mc_card_export.h"
#include "mmce_backup.h"
#include "mmce_card_switch.h"
#include "dash_card_tool.h"
#include <libcdvd.h>

#define CT_BLOCK 4096
#define CT_WAYS 4
#define CT_MAX_SAVES 250
#define CT_SAFETY_KEEP 10
#define CT_PSU_MAX (8u * 1024u * 1024u + 512u * 1024u)

typedef struct {
	int fd;
	s64 size;
	s64 tag[CT_WAYS];
	int valid[CT_WAYS];
	int next;
} CardIo;

static u8 ct_cache[CT_WAYS][CT_BLOCK] __attribute__((aligned(64)));
static McView ct_view;
static McViewSave ct_saves[CT_MAX_SAVES];
static char ct_titles[CT_MAX_SAVES][96];
static char ct_details[CT_MAX_SAVES][96];
static DashMenuItem ct_items[CT_MAX_SAVES + 3];

static int ctRead(void *ctx, uint64_t offset, void *buf, uint32_t len)
{
	CardIo *io = (CardIo *)ctx;
	u8 *out = (u8 *)buf;

	while (len > 0) {
		s64 base = (s64)(offset / CT_BLOCK) * CT_BLOCK;
		u32 in = (u32)(offset - (uint64_t)base), n = CT_BLOCK - in;
		int way = -1, i;

		if (n > len)
			n = len;
		for (i = 0; i < CT_WAYS; i++)
			if (io->tag[i] == base)
				way = i;
		if (way < 0) {
			int want = CT_BLOCK, done = 0;

			if (base >= io->size)
				return -1;
			if (io->size - base < want)
				want = (int)(io->size - base);
			way = io->next;
			io->next = (io->next + 1) % CT_WAYS;
			io->tag[way] = -1;
			if (genLseek(io->fd, base, SEEK_SET) != base)
				return -1;
			while (done < want) {
				int got = genRead(io->fd, ct_cache[way] + done, want - done);
				if (got <= 0)
					return -1;
				done += got;
			}
			io->tag[way] = base;
			io->valid[way] = want;
		}
		if ((int)(in + n) > io->valid[way])
			return -1;
		memcpy(out, ct_cache[way] + in, n);
		out += n;
		offset += n;
		len -= n;
	}
	return 0;
}

static int ctWrite(void *ctx, uint64_t offset, const void *buf, uint32_t len)
{
	CardIo *io = (CardIo *)ctx;
	u32 done = 0;
	int i;

	if ((s64)(offset + len) > io->size)
		return -1; /* never grow the image */
	for (i = 0; i < CT_WAYS; i++)
		if (io->tag[i] >= 0 && (uint64_t)io->tag[i] < offset + len &&
		    offset < (uint64_t)io->tag[i] + CT_BLOCK)
			io->tag[i] = -1;
	if (genLseek(io->fd, (s64)offset, SEEK_SET) != (s64)offset)
		return -1;
	while (done < len) {
		int put = genWrite(io->fd, (u8 *)buf + done, (int)(len - done));
		if (put <= 0)
			return -1;
		done += (u32)put;
	}
	return 0;
}

static int ctOpen(CardIo *io, const char *path, int writable)
{
	McViewSource src;
	int i, r;

	memset(io, 0, sizeof(*io));
	for (i = 0; i < CT_WAYS; i++)
		io->tag[i] = -1;
	io->fd = genOpen(path, writable ? FIO_O_RDWR : FIO_O_RDONLY);
	if (io->fd < 0)
		return MCV_ERR_IO;
	io->size = genLseek(io->fd, 0, SEEK_END);
	if (io->size <= 0) {
		genClose(io->fd);
		io->fd = -1;
		return MCV_ERR_IO;
	}
	src.read = ctRead;
	src.ctx = io;
	src.size = (uint64_t)io->size;
	r = mcViewOpen(&ct_view, &src);
	if (r != MCV_OK) {
		genClose(io->fd);
		io->fd = -1;
	}
	return r;
}

static int ctClose(CardIo *io)
{
	int r = 0;

	if (io->fd >= 0)
		r = genClose(io->fd);
	io->fd = -1;
	return r < 0 ? MCV_ERR_IO : MCV_OK;
}

static const char *ctError(int r)
{
	switch (r) {
		case MCV_ERR_IO:
			return "The image could not be read or written.";
		case MCV_ERR_SIZE:
		case MCV_ERR_MAGIC:
			return "This is not a PS2 memory card image.";
		case MCV_ERR_GEOMETRY:
		case MCV_ERR_CORRUPT:
			return "The card's file system is damaged. Nothing was changed.";
		case MCW_ERR_EXISTS:
			return "A save with this name is already on that card. Delete it there first.";
		case MCW_ERR_NOT_FOUND:
			return "The save is no longer on the card.";
		case MCW_ERR_FULL:
			return "There is not enough free space on that card. Nothing was written.";
		case MCW_ERR_UNSUPPORTED:
			return "Saves with sub-folders, and PCSX2 .ps2 cards, can only be read here.";
		case MCW_ERR_VERIFY:
			return "The card did not read back as written. Restore the safety copy.";
		case MCW_ERR_NOMEM:
			return "Not enough memory for this save.";
		default:
			return "The save or file is not valid.";
	}
}

static const char *ctBaseName(const char *path)
{
	const char *s = strrchr(path, '/');

	if (s == NULL)
		s = strrchr(path, ':');
	return s != NULL ? s + 1 : path;
}

/* 1 when path may be written. Refused while a VMC is mounted (the driver
 * caches the card's tables and would write them back over the change; which
 * image is mounted is not recorded, so any mount refuses), and for an MMCE
 * image the device is serving, reports as selected, or cannot rule out. */
static int ctWritable(const char *path)
{
	if (vmcMounted[0] || vmcMounted[1]) {
		dashModalMessage(DI_WARNING, "Unmount the VMC first",
		                 "A virtual memory card is mounted as vmc0: or vmc1:. If it is this card, the VMC driver would write its own copy of the card's tables back over the change. Unmount it, then try again.",
		                 path);
		return 0;
	}
	switch (mmceCardMaybeServed(path)) {
		case MMCE_SERVED_NO:
			return 1;
		case MMCE_SERVED_YES:
			dashModalMessage(DI_WARNING, "This card is in use",
			                 "The MMCE device is serving this card to the console right now, or a card with exactly the same contents that cannot be told apart from it. Switch to another card first, or copy the save to a different card.",
			                 path);
			return 0;
		case MMCE_SERVED_SELECTED:
			dashModalMessage(DI_WARNING, "This card is in use",
			                 "The MMCE device reports this card as the selected one. Switch to another card first, or copy the save to a different card.",
			                 path);
			return 0;
		default:
			dashModalMessage(DI_WARNING, "Card not changed",
			                 "Launchstation could not read this card or the card the MMCE device is serving, so it cannot rule out that they are the same.",
			                 path);
			return 0;
	}
}

static void ctSafetyProgress(int phase, u64 done, u64 total)
{
	int scaled = total ? (int)((done * 1000) / total) : 0;
	dashLoadingStatus(phase == MC_CARD_EXPORT_VERIFY ? "Safety copy: verifying" : "Safety copy: writing", scaled, 1000);
}

static int ctSafetyFolder(const char *image, char *out, int size)
{
	int unit = mmceCardUnit(image), len;

	if (unit >= 0) /* outside MemoryCards/, where the firmware never looks */
		return snprintf(out, size, "mmce%d:/WLE-Safety/", unit) < size;
	len = (int)(ctBaseName(image) - image);
	if (len <= 0 || len + 12 >= size)
		return 0;
	memcpy(out, image, len);
	strcpy(out + len, "WLE-Safety/");
	return 1;
}

/* "<base>-NNNN.mcd": returns NNNN, or -1 for any other name. */
static int ctSafetyNumber(const char *name, const char *base)
{
	int len = strlen(base), i, n = 0;

	if (strncmp(name, base, len) != 0 || name[len] != '-')
		return -1;
	for (i = 1; i <= 4; i++) {
		if (name[len + i] < '0' || name[len + i] > '9')
			return -1;
		n = n * 10 + (name[len + i] - '0');
	}
	return genCmpFileExt(name, "MCD") && name[len + 5] == '.' && strlen(name + len + 5) == 4 ? n : -1;
}

/* Verified copy of the whole image; keeps the newest CT_SAFETY_KEEP. */
static int ctSafetyCopy(const char *image, char *made, int made_size)
{
	char folder[MAX_PATH], dir[MAX_PATH], base[48], path[MAX_PATH];
	const char *name = ctBaseName(image), *dot;
	iox_dirent_t ent;
	int fd, n, high = 0, count, oldest, len, r, guard;

	if (!ctSafetyFolder(image, folder, sizeof(folder)))
		return -1;
	dot = strrchr(name, '.');
	len = dot != NULL ? (int)(dot - name) : (int)strlen(name);
	if (len > (int)sizeof(base) - 1)
		len = sizeof(base) - 1;
	memcpy(base, name, len);
	base[len] = 0;

	dashStrCopy(dir, folder, sizeof(dir));
	dir[strlen(dir) - 1] = 0;
	genMkdir(dir, fileMode);
	dashStrCopy(dir, folder, sizeof(dir));
	fd = genDopen(dir);
	if (fd < 0)
		return -1;
	memset(&ent, 0, sizeof(ent));
	while (fileXioDread(fd, &ent) > 0) {
		n = ctSafetyNumber(ent.name, base);
		if (n > high)
			high = n;
		memset(&ent, 0, sizeof(ent));
	}
	genDclose(fd);
	if (high >= 9999)
		return -1;
	if (snprintf(made, made_size, "%s%s-%04d.mcd", folder, base, high + 1) >= made_size)
		return -1;
	r = mcCardBackupNoEccNewFile(image, made, ctSafetyProgress);
	if (r != MC_CARD_EXPORT_OK)
		return -1;

	/* prune: the oldest numbers go first; the copy just made is the newest */
	for (guard = 0; guard < 64; guard++) {
		count = 0;
		oldest = -1;
		dashStrCopy(dir, folder, sizeof(dir));
		fd = genDopen(dir);
		if (fd < 0)
			break;
		memset(&ent, 0, sizeof(ent));
		while (fileXioDread(fd, &ent) > 0) {
			n = ctSafetyNumber(ent.name, base);
			if (n >= 0) {
				count++;
				if (oldest < 0 || n < oldest)
					oldest = n;
			}
			memset(&ent, 0, sizeof(ent));
		}
		genDclose(fd);
		if (count <= CT_SAFETY_KEEP || oldest < 0 || oldest == high + 1)
			break;
		if (snprintf(path, sizeof(path), "%s%s-%04d.mcd", folder, base, oldest) >= (int)sizeof(path) ||
		    genRemove(path) < 0)
			break;
	}
	return 0;
}

/* Name not taken and enough free clusters, checked read-only so a refusal
 * costs no safety copy. mcCardWriteSave checks again before writing. */
static int ctPrecheck(const char *image, const McSaveData *s)
{
	CardIo io;
	uint32_t freec, total;
	char name[33];
	int r;

	r = ctOpen(&io, image, 0);
	if (r != MCV_OK)
		return r;
	if (ct_view.page_stride != 512u)
		r = MCW_ERR_UNSUPPORTED;
	if (r == MCV_OK)
		r = mcViewFreeClusters(&ct_view, &freec, &total);
	if (r == MCV_OK && freec < mcCardSaveClusters(s) + 1u)
		r = MCW_ERR_FULL;
	if (r == MCV_OK) { /* by name, so a card with any number of saves is checked in full */
		memcpy(name, s->entry + 0x40, 32);
		name[32] = 0;
		r = mcCardFindSave(&ct_view, name);
		if (r == MCW_ERR_NOT_FOUND)
			r = MCV_OK;
	}
	ctClose(&io);
	return r;
}

/* Safety copy, write, close, then read the save back through a fresh open. */
static int ctPutSave(const char *image, const McSaveData *s, char *safety, int safety_size)
{
	CardIo io;
	McSaveData back;
	char name[33];
	int r, c;

	r = ctPrecheck(image, s);
	if (r != MCV_OK)
		return r;
	dashLoadingStatus("Making a safety copy", 0, 1);
	if (ctSafetyCopy(image, safety, safety_size) < 0)
		return MCV_ERR_IO;
	dashLoadingStatus("Writing the save", 0, 1);
	r = ctOpen(&io, image, 1);
	if (r != MCV_OK)
		return r;
	r = mcCardWriteSave(&ct_view, ctWrite, &io, s);
	c = ctClose(&io);
	if (r != MCW_OK)
		return r;
	if (c != MCV_OK)
		return MCW_ERR_VERIFY;

	memcpy(name, s->entry + 0x40, 32);
	name[32] = 0;
	r = ctOpen(&io, image, 0);
	if (r == MCV_OK) {
		r = mcCardReadSave(&ct_view, name, &back);
		if (r == MCW_OK) {
			r = mcCardSameSave(s, &back) ? MCW_OK : MCW_ERR_VERIFY;
			mcCardFreeSave(&back);
		}
		ctClose(&io);
	}
	return r == MCW_OK ? MCW_OK : MCW_ERR_VERIFY;
}

static int ctDropSave(const char *image, const char *name, char *safety, int safety_size)
{
	CardIo io;
	int r, c, n, found;

	dashLoadingStatus("Making a safety copy", 0, 1);
	if (ctSafetyCopy(image, safety, safety_size) < 0)
		return MCV_ERR_IO;
	dashLoadingStatus("Deleting the save", 0, 1);
	r = ctOpen(&io, image, 1);
	if (r != MCV_OK)
		return r;
	r = mcCardDeleteSave(&ct_view, ctWrite, &io, name);
	c = ctClose(&io);
	if (r != MCW_OK)
		return r;
	if (c != MCV_OK)
		return MCW_ERR_VERIFY;

	/* gone, and the card still lists cleanly */
	r = ctOpen(&io, image, 0);
	if (r != MCV_OK)
		return MCW_ERR_VERIFY;
	n = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &found);
	if (n < 0 || mcCardFindSave(&ct_view, name) != MCW_ERR_NOT_FOUND)
		r = MCW_ERR_VERIFY;
	ctClose(&io);
	return r;
}

static int ctReadSave(const char *image, const char *name, McSaveData *s)
{
	CardIo io;
	int r = ctOpen(&io, image, 0);

	if (r != MCV_OK)
		return r;
	r = mcCardReadSave(&ct_view, name, s);
	ctClose(&io);
	return r;
}

static void ctDone(int r, const char *ok, const char *safety, const char *where)
{
	char detail[MAX_PATH + 32];

	if (r == MCW_OK) {
		dashToast(DASH_TOAST_OK, ok);
		return;
	}
	if (safety[0])
		snprintf(detail, sizeof(detail), "Safety copy: %s", safety);
	else
		dashStrCopy(detail, where, sizeof(detail));
	dashModalMessage(DI_WARNING, "Not done", ctError(r), detail);
}

static int ctStacks(const char *a, const char *b)
{
	if (prepareTransferDeviceStacks(a, b) == TRANSFER_STACK_READY)
		return 1;
	dashModalMessage(DI_WARNING, "Cannot do that here",
	                 "The two devices need drivers that cannot stay active together.", b);
	return 0;
}

static void ctCopy(const char *image, const char *name, int move)
{
	McSaveData s;
	char dest[MAX_PATH], dir[MAX_PATH], safety[MAX_PATH], safety2[MAX_PATH];
	int r;

	safety[0] = safety2[0] = 0;
	dashStrCopy(dir, image, sizeof(dir));
	dir[ctBaseName(image) - image] = 0;
	if (!dashPickPath(move ? "Move save to which card image?" : "Copy save to which card image?",
	                  dir, DASH_PICK_FILE, NULL, dest, sizeof(dest)))
		return;
	if (!strcmp(dest, image)) {
		dashModalMessage(DI_WARNING, "Choose another card", "The save is already on this card.", dest);
		return;
	}
	if (!ctStacks(image, dest))
		return;
	if (!ctWritable(dest) || (move && !ctWritable(image)))
		return;
	if (!dashModalConfirm(move ? "Move this save?" : "Copy this save?",
	                      move ? "Both cards get a safety copy first. The save is removed here only after it reads back correctly on the other card." :
	                             "The other card gets a safety copy first.",
	                      move ? "Move" : "Copy", 0))
		return;

	dashLoadingBegin(move ? "Moving save" : "Copying save");
	r = ctReadSave(image, name, &s);
	if (r == MCW_OK) {
		r = ctPutSave(dest, &s, safety, sizeof(safety));
		mcCardFreeSave(&s);
	}
	if (r == MCW_OK && move)
		r = ctDropSave(image, name, safety2, sizeof(safety2));
	dashLoadingEnd();
	ctDone(r, move ? "Save moved and verified" : "Save copied and verified", safety2[0] ? safety2 : safety,
	       dest);
}

static void ctDelete(const char *image, const char *name)
{
	char safety[MAX_PATH];
	int r;

	safety[0] = 0;
	if (!ctWritable(image))
		return;
	if (!dashModalConfirm("Delete this save?", "A safety copy of the whole card is made first.", "Delete", 1))
		return;
	dashLoadingBegin("Deleting save");
	r = ctDropSave(image, name, safety, sizeof(safety));
	dashLoadingEnd();
	ctDone(r, "Save deleted", safety, image);
}

static int ctWriteFile(const char *path, const uint8_t *buf, uint32_t len)
{
	static u8 back[CT_BLOCK] __attribute__((aligned(64)));
	u32 done = 0;
	int fd, n, ok = 1;

	fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
	if (fd < 0)
		return -1;
	while (ok && done < len) {
		n = genWrite(fd, (void *)(buf + done), (int)(len - done > 65536 ? 65536 : len - done));
		if (n <= 0)
			ok = 0;
		else
			done += (u32)n;
	}
	if (genClose(fd) < 0)
		ok = 0;
	if (ok) { /* read it back */
		fd = genOpen(path, FIO_O_RDONLY);
		ok = fd >= 0 && genLseek(fd, 0, SEEK_END) == (s64)len && genLseek(fd, 0, SEEK_SET) == 0;
		for (done = 0; ok && done < len; done += (u32)n) {
			n = genRead(fd, back, (int)(len - done > CT_BLOCK ? CT_BLOCK : len - done));
			ok = n > 0 && !memcmp(back, buf + done, n);
		}
		if (fd >= 0)
			genClose(fd);
	}
	if (!ok)
		genRemove((char *)path);
	return ok ? 0 : -1;
}

static void ctExportPsu(const char *image, const char *name)
{
	McSaveData s;
	uint8_t *buf = NULL;
	uint32_t len = 0;
	char folder[MAX_PATH], dest[MAX_PATH];
	int r;

	dashStrCopy(folder, image, sizeof(folder));
	folder[ctBaseName(image) - image] = 0;
	if (!dashPickPath("Export .psu to", folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;
	if (snprintf(dest, sizeof(dest), "%s%s.psu", folder, name) >= (int)sizeof(dest)) {
		dashToast(DASH_TOAST_WARN, "Destination path is too long");
		return;
	}
	if (wleExists(dest) && !dashModalConfirm("Replace existing .psu?", dest, "Replace", 1))
		return;
	if (!ctStacks(image, dest))
		return;
	dashLoadingBegin("Exporting save");
	r = ctReadSave(image, name, &s);
	if (r == MCW_OK) {
		r = mcCardSaveToPsu(&s, &buf, &len);
		mcCardFreeSave(&s);
	}
	if (r == MCW_OK && ctWriteFile(dest, buf, len) < 0)
		r = MCV_ERR_IO;
	free(buf);
	dashLoadingEnd();
	if (r == MCW_OK)
		dashToast(DASH_TOAST_OK, ".psu written and verified");
	else
		dashModalMessage(DI_WARNING, "Not exported", ctError(r), dest);
}

static void ctImportPsu(const char *image)
{
	McSaveData s;
	char psu[MAX_PATH], dir[MAX_PATH], safety[MAX_PATH];
	u8 *buf;
	s64 size;
	u32 done = 0;
	int fd, n, r;

	safety[0] = 0;
	dashStrCopy(dir, image, sizeof(dir));
	dir[ctBaseName(image) - image] = 0;
	if (!ctWritable(image) || !dashPickPath("Import which .psu?", dir, DASH_PICK_FILE, "PSU", psu, sizeof(psu)))
		return;
	if (!ctStacks(psu, image))
		return;

	fd = genOpen(psu, FIO_O_RDONLY);
	if (fd < 0) {
		dashModalMessage(DI_WARNING, "Not imported", "The .psu could not be opened.", psu);
		return;
	}
	size = genLseek(fd, 0, SEEK_END);
	if (size <= 0 || size > CT_PSU_MAX || genLseek(fd, 0, SEEK_SET) != 0) {
		genClose(fd);
		dashModalMessage(DI_WARNING, "Not imported", "The .psu is empty or larger than any 8 MB card save.", psu);
		return;
	}
	buf = (u8 *)malloc((size_t)size);
	if (buf == NULL) {
		genClose(fd);
		dashModalMessage(DI_WARNING, "Not imported", ctError(MCW_ERR_NOMEM), psu);
		return;
	}
	while (done < (u32)size) {
		n = genRead(fd, buf + done, (int)((u32)size - done > 65536 ? 65536 : (u32)size - done));
		if (n <= 0)
			break;
		done += (u32)n;
	}
	genClose(fd);
	r = done == (u32)size ? mcCardSaveFromPsu(buf, (uint32_t)size, &s) : MCV_ERR_IO;
	free(buf);
	if (r != MCW_OK) {
		dashModalMessage(DI_WARNING, "Not imported", ctError(r), psu);
		return;
	}
	if (!dashModalConfirm("Import this save?", "The card gets a safety copy first.", "Import", 0)) {
		mcCardFreeSave(&s);
		return;
	}
	dashLoadingBegin("Importing save");
	r = ctPutSave(image, &s, safety, sizeof(safety));
	mcCardFreeSave(&s);
	dashLoadingEnd();
	ctDone(r, "Save imported and verified", safety, image);
}

typedef struct {
	int fd;
	uint64_t pos;
} AppendIo;

/* mcCardFormat writes front to back; anything else is refused. */
static int ctAppend(void *ctx, uint64_t offset, const void *buf, uint32_t len)
{
	AppendIo *a = (AppendIo *)ctx;
	uint32_t done = 0;
	int put;

	if (offset != a->pos)
		return -1;
	while (done < len) {
		put = genWrite(a->fd, (u8 *)buf + done, (int)(len - done));
		if (put <= 0)
			return -1;
		done += (uint32_t)put;
	}
	a->pos += len;
	return 0;
}

/* A new card image of another size holding every save; the source is only read. */
static void ctResize(const char *image)
{
	static const char *const sizes[] = {"8 MB", "16 MB", "32 MB", "64 MB"};
	static uint8_t sb[512], root[1024];
	char folder[MAX_PATH], dir[MAX_PATH], dest[MAX_PATH], base[48], msg[160];
	char (*names)[33] = NULL;
	const char *name = ctBaseName(image), *dot;
	CardIo io;
	AppendIo out;
	McSaveData s;
	uint32_t freec, total;
	int unit = mmceCardUnit(image), choice, mb, r, n = 0, found = 0, i, len, made = 0, bad = -1;

	r = ctOpen(&io, image, 0);
	if (r == MCV_OK) {
		r = mcCardReadIdentity(&ct_view, sb, root);
		if (r == MCV_OK)
			r = mcViewFreeClusters(&ct_view, &freec, &total);
		if (r == MCV_OK) {
			n = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &found);
			if (n < 0)
				r = n;
		}
		ctClose(&io);
	}
	if (r == MCV_OK && found > n) {
		dashModalMessage(DI_WARNING, "Cannot copy this card",
		                 "This card holds more saves than this tool can copy at once (250). Move some saves to another card first.",
		                 image);
		return;
	}
	if (r == MCV_OK && n > 0) {
		names = malloc((size_t)n * sizeof(*names));
		if (names == NULL)
			r = MCW_ERR_NOMEM;
		for (i = 0; names != NULL && i < n; i++)
			dashStrCopy(names[i], ct_saves[i].name, sizeof(names[i]));
	}
	if (r != MCV_OK) {
		free(names);
		dashModalMessage(DI_WARNING, "Cannot copy this card", ctError(r), image);
		return;
	}
	choice = dashModalChoice("New card size", sizes, 4, total > 33000u ? 3 : total > 16500u ? 2 : total > 8250u ? 1 : 0);
	if (choice < 0) {
		free(names);
		return;
	}
	mb = 8 << choice;

	/* Outside MemoryCards/ on MMCE, so the device never serves a half-built card. */
	if (unit >= 0)
		snprintf(folder, sizeof(folder), "mmce%d:/WLE-Resized/", unit);
	else {
		dashStrCopy(folder, image, sizeof(folder));
		folder[name - image] = 0;
	}
	dot = strrchr(name, '.');
	len = dot != NULL ? (int)(dot - name) : (int)strlen(name);
	if (len > (int)sizeof(base) - 1)
		len = sizeof(base) - 1;
	memcpy(base, name, len);
	base[len] = 0;
	dest[0] = 0;
	for (i = 1; i < 100; i++) {
		int w = i == 1 ? snprintf(dest, sizeof(dest), "%s%s-%dMB.mcd", folder, base, mb) :
		                 snprintf(dest, sizeof(dest), "%s%s-%dMB-%d.mcd", folder, base, mb, i);
		if (w >= (int)sizeof(dest) || !wleExists(dest))
			break;
	}
	snprintf(msg, sizeof(msg), "%d save%s copied to a new %d MB card image. This card is not changed.", n,
	         n == 1 ? " is" : "s are", mb);
	if (i == 100 || !ctStacks(image, dest) || !dashModalConfirm("Make a new card?", msg, "Make", 0)) {
		free(names);
		return;
	}

	dashLoadingBegin("Making a new card");
	if (unit >= 0) {
		dashStrCopy(dir, folder, sizeof(dir));
		dir[strlen(dir) - 1] = 0;
		genMkdir(dir, fileMode);
	}
	out.fd = genOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
	out.pos = 0;
	r = out.fd < 0 ? MCV_ERR_IO : MCV_OK;
	if (r == MCV_OK) {
		made = 1;
		dashLoadingStatus("Formatting", 0, n + 1);
		r = mcCardFormat(sb, root, (uint32_t)mb, ctAppend, &out);
		if (genClose(out.fd) < 0 && r == MCV_OK)
			r = MCV_ERR_IO;
	}
	for (i = 0; r == MCV_OK && i < n; i++) {
		dashLoadingStatus(names[i], i + 1, n + 1);
		r = ctReadSave(image, names[i], &s);
		if (r != MCW_OK)
			break;
		r = ctOpen(&io, dest, 1);
		if (r == MCV_OK) {
			int c;

			r = mcCardWriteSave(&ct_view, ctWrite, &io, &s);
			c = ctClose(&io);
			if (r == MCW_OK && c != MCV_OK)
				r = MCW_ERR_VERIFY;
		}
		mcCardFreeSave(&s);
		if (r != MCV_OK)
			bad = i;
	}
	if (r == MCV_OK) { /* the finished card lists exactly the source's saves */
		int got = 0, k;

		r = ctOpen(&io, dest, 0);
		if (r == MCV_OK) {
			k = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &got);
			ctClose(&io);
			if (k != n || got != n)
				r = MCW_ERR_VERIFY;
		}
	}
	dashLoadingEnd();
	if (r != MCV_OK) {
		if (made)
			genRemove(dest);
		if (r == MCW_ERR_FULL && bad >= 0)
			snprintf(msg, sizeof(msg), "The saves do not fit in %d MB (stopped at %s). The new card was removed.", mb,
			         names[bad]);
		else if (bad >= 0)
			snprintf(msg, sizeof(msg), "%s (save %s). The new card was removed.", ctError(r), names[bad]);
		else
			snprintf(msg, sizeof(msg), "%s The new card was removed.", ctError(r));
		dashModalMessage(DI_WARNING, "New card not made", msg, dest);
	} else {
		dashModalMessage(DI_MC, "New card made",
		                 unit >= 0 ? "Every save was copied and read back. To use it, install it with \"Install as NEW selectable MMCE card\"." :
		                             "Every save was copied and read back.",
		                 dest);
	}
	free(names);
}

typedef struct {
	char name[33];
	char id[11];
} CtGameSave;

/* One new Game ID card at dest holding every save of game id, formatted
 * with the source card's identity. Removed again unless it reads back with
 * exactly those saves. */
static int ctGameCard(const char *image, const char *dest, const uint8_t sb[512], const uint8_t root[1024], int mb,
                      const CtGameSave *list, int n, const char *id, int *bad)
{
	CardIo io;
	AppendIo out;
	McSaveData s;
	int r, i, want = 0, got = 0, k;

	*bad = -1;
	out.fd = genOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
	out.pos = 0;
	if (out.fd < 0)
		return MCV_ERR_IO;
	r = mcCardFormat(sb, root, (uint32_t)mb, ctAppend, &out);
	if (genClose(out.fd) < 0 && r == MCV_OK)
		r = MCV_ERR_IO;
	for (i = 0; r == MCV_OK && i < n; i++) {
		if (strcmp(list[i].id, id))
			continue;
		want++;
		r = ctReadSave(image, list[i].name, &s);
		if (r != MCW_OK) {
			*bad = i;
			break;
		}
		r = ctOpen(&io, dest, 1);
		if (r == MCV_OK) {
			int c;

			r = mcCardWriteSave(&ct_view, ctWrite, &io, &s);
			c = ctClose(&io);
			if (r == MCW_OK && c != MCV_OK)
				r = MCW_ERR_VERIFY;
		}
		mcCardFreeSave(&s);
		if (r != MCV_OK)
			*bad = i;
	}
	if (r == MCV_OK) { /* the finished card lists exactly this game's saves */
		r = ctOpen(&io, dest, 0);
		if (r == MCV_OK) {
			k = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &got);
			ctClose(&io);
			if (k != want || got != want)
				r = MCW_ERR_VERIFY;
			for (i = 0; r == MCV_OK && i < k; i++) {
				char sid[11];

				if (!mcSaveGameId(ct_saves[i].name, sid) || strcmp(sid, id))
					r = MCW_ERR_VERIFY;
			}
		}
	}
	if (r != MCV_OK)
		genRemove((char *)dest);
	return r;
}

/* One Game ID card per game (<ID>/<ID>-1.mcd, the folder layout sd2psXtd-
 * style MMCE devices pick by the running game), each holding that game's
 * saves. The source is only read, and an existing card is never replaced. */
static void ctSplitGameId(const char *image)
{
	static const int sizes[] = {8, 16, 32, 64};
	static uint8_t sb[512], root[1024];
	/* where is kept 32 bytes short of MAX_PATH, so dir and dest still fit it */
	char where[MAX_PATH], dir[MAX_PATH + 16], dest[MAX_PATH + 48], base[48], msg[160], body[512], fail_id[11] = "";
	const char *name = ctBaseName(image), *dot;
	CtGameSave *list = NULL;
	CardIo io;
	int unit = mmceCardUnit(image), r, n = 0, found = 0, i, j, k, len, bad;
	int games = 0, other = 0, done = 0, made = 0, present = 0, failed = 0, copied = 0, fail_r = MCV_OK;

	r = ctOpen(&io, image, 0);
	if (r == MCV_OK) {
		r = mcCardReadIdentity(&ct_view, sb, root);
		if (r == MCV_OK) {
			n = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &found);
			if (n < 0)
				r = n;
		}
		ctClose(&io);
	}
	if (r == MCV_OK && found > n) {
		dashModalMessage(DI_WARNING, "Cannot split this card",
		                 "This card holds more saves than this tool can copy at once (250). Move some saves to another card first.",
		                 image);
		return;
	}
	if (r == MCV_OK && n > 0) {
		list = malloc((size_t)n * sizeof(*list));
		if (list == NULL)
			r = MCW_ERR_NOMEM;
	}
	if (r != MCV_OK) {
		dashModalMessage(DI_WARNING, "Cannot split this card", ctError(r), image);
		return;
	}
	for (i = 0; i < n; i++) {
		dashStrCopy(list[i].name, ct_saves[i].name, sizeof(list[i].name));
		if (ct_saves[i].is_ps1 || !mcSaveGameId(list[i].name, list[i].id)) {
			list[i].id[0] = 0;
			other++;
			continue;
		}
		for (j = 0; j < i && strcmp(list[j].id, list[i].id); j++)
			;
		if (j == i)
			games++;
	}
	if (games == 0) {
		free(list);
		dashModalMessage(DI_WARNING, "No game saves to split",
		                 "No save on this card is named after a game's serial (like BASLUS-21050), so no Game ID card can be made.",
		                 image);
		return;
	}

	/* Outside MemoryCards/ on MMCE, so the device never serves a half-built card. */
	if (unit >= 0)
		len = snprintf(where, sizeof(where), "mmce%d:/WLE-GameID/", unit);
	else {
		dot = strrchr(name, '.');
		k = dot != NULL ? (int)(dot - name) : (int)strlen(name);
		if (k > (int)sizeof(base) - 1)
			k = sizeof(base) - 1;
		memcpy(base, name, k);
		base[k] = 0;
		len = snprintf(where, sizeof(where), "%.*s%s-GameID/", (int)(name - image), image, base);
	}
	if (len >= (int)sizeof(where) - 32) {
		free(list);
		return;
	}
	snprintf(body, sizeof(body), "%d game%s a Game ID card of %s own, %d save%s in all.%s This card is not changed.",
	         games, games == 1 ? " gets" : "s each get", games == 1 ? "its" : "their", n - other, n - other == 1 ? "" : "s", other ? " Saves without a game serial are left out." : "");
	if (!ctStacks(image, where) || !dashModalConfirm("Split into Game ID cards?", body, "Split", 0)) {
		free(list);
		return;
	}

	dashLoadingBegin("Making Game ID cards");
	dashStrCopy(dir, where, sizeof(dir));
	dir[strlen(dir) - 1] = 0;
	genMkdir(dir, fileMode);
	for (i = 0; i < n; i++) {
		if (!list[i].id[0])
			continue;
		for (j = 0; j < i && strcmp(list[j].id, list[i].id); j++)
			;
		if (j < i)
			continue;
		dashLoadingStatus(list[i].id, done++, games);
		snprintf(dir, sizeof(dir), "%s%s", where, list[i].id);
		snprintf(dest, sizeof(dest), "%s/%s-1.mcd", dir, list[i].id);
		genMkdir(dir, fileMode);
		if (wleExists(dest)) {
			present++;
			continue;
		}
		/* The smallest card the game's saves fit on; 8 MB for nearly all. */
		for (k = 0; k < 4; k++) {
			r = ctGameCard(image, dest, sb, root, sizes[k], list, n, list[i].id, &bad);
			if (r != MCW_ERR_FULL)
				break;
		}
		if (r == MCV_OK) {
			made++;
			for (j = i; j < n; j++)
				copied += !strcmp(list[j].id, list[i].id);
		} else if (failed++ == 0) {
			dashStrCopy(fail_id, list[i].id, sizeof(fail_id));
			fail_r = r;
		}
	}
	dashLoadingEnd();
	free(list);

	len = snprintf(body, sizeof(body), "%d Game ID card%s made and read back, with %d save%s.", made,
	               made == 1 ? "" : "s", copied, copied == 1 ? "" : "s");
	if (present)
		len += snprintf(body + len, sizeof(body) - len, " %d game%s already had a card there; left as %s.", present,
		                present == 1 ? "" : "s", present == 1 ? "it was" : "they were");
	if (failed)
		len += snprintf(body + len, sizeof(body) - len, " %d failed and %s removed (%s: %s).", failed,
		                failed == 1 ? "was" : "were", fail_id, ctError(fail_r));
	if (other)
		len += snprintf(body + len, sizeof(body) - len, " %d save%s without a game serial %s left out.", other,
		                other == 1 ? "" : "s", other == 1 ? "was" : "were");
	if (made)
		snprintf(body + len, sizeof(body) - len, "%s",
		         unit >= 0 ? " Install each with \"Install as MMCE Game ID card\"." :
		                     " Copy each folder into MemoryCards/PS2/ on the MMCE SD card.");
	snprintf(msg, sizeof(msg), "%s", failed ? "Some Game ID cards not made" : made ? "Game ID cards made" : "No new Game ID card");
	dashModalMessage(failed || !made ? DI_WARNING : DI_MC, msg, body, where);
}

static int ctBcd(u8 v)
{
	return (v >> 4) * 10 + (v & 0x0F);
}

/* Card timestamps are console RTC time, as the PS2 itself stores them. */
static int ctStamp(uint8_t stamp[8])
{
	sceCdCLOCK clock;
	int year;

	memset(&clock, 0, sizeof(clock));
	if (sceCdReadClock(&clock) == 0 || clock.stat != 0)
		return 0;
	year = 2000 + ctBcd(clock.year);
	stamp[0] = 0;
	stamp[1] = (uint8_t)ctBcd(clock.second);
	stamp[2] = (uint8_t)ctBcd(clock.minute);
	stamp[3] = (uint8_t)ctBcd(clock.hour);
	stamp[4] = (uint8_t)ctBcd(clock.day);
	stamp[5] = (uint8_t)ctBcd(clock.month & 0x7F);
	stamp[6] = (uint8_t)year;
	stamp[7] = (uint8_t)(year >> 8);
	return 1;
}

/* An empty card image made from nothing. Only a new file is written, with
 * O_EXCL, and it is removed again unless it reads back as an empty card. */
void dashCardNewBlank(const char *folder)
{
	static const char *const sizes[] = {"8 MB", "16 MB", "32 MB", "64 MB"};
	static uint8_t sb[512], root[1024];
	char where[MAX_PATH], dest[MAX_PATH], msg[160];
	uint8_t stamp[8];
	CardIo io;
	AppendIo out;
	uint32_t freec, total;
	int unit = mmceCardUnit(folder), choice, mb, r, i, made = 0, found = 0, n = 0;
	size_t len;

	if (!strncmp(folder, "mc", 2) || !strncmp(folder, "vmc", 3)) {
		dashModalMessage(DI_WARNING, "Choose another folder",
		                 "A card image does not fit on a memory card. Choose a folder on USB, HDD, MMCE or the network.",
		                 folder);
		return;
	}
	choice = dashModalChoice("New card size", sizes, 4, 0);
	if (choice < 0)
		return;
	mb = 8 << choice;

	/* Outside MemoryCards/ on MMCE, so the device never serves a half-built
	 * card; "Install as NEW selectable MMCE card" moves it into place. */
	if (unit >= 0)
		snprintf(where, sizeof(where), "mmce%d:/WLE-New/", unit);
	else {
		dashStrCopy(where, folder, sizeof(where));
		len = strlen(where);
		if (len == 0 || len + 2 >= sizeof(where))
			return;
		if (where[len - 1] != '/' && where[len - 1] != ':') {
			where[len] = '/';
			where[len + 1] = 0;
		}
	}
	dest[0] = 0;
	for (i = 1; i < 100; i++) {
		int w = i == 1 ? snprintf(dest, sizeof(dest), "%sBlank-%dMB.mcd", where, mb) :
		                 snprintf(dest, sizeof(dest), "%sBlank-%dMB-%d.mcd", where, mb, i);
		if (w >= (int)sizeof(dest))
			return;
		if (!wleExists(dest))
			break;
	}
	if (i == 100) {
		dashModalMessage(DI_WARNING, "Choose another folder", "This folder already holds 99 blank cards of that size.", where);
		return;
	}
	snprintf(msg, sizeof(msg), "An empty %d MB PS2 card image is made. Nothing else is changed.", mb);
	if (!dashModalConfirm("Make a blank card?", msg, "Make", 0))
		return;

	dashLoadingBegin("Making a blank card");
	if (unit >= 0) {
		char dir[MAX_PATH];

		dashStrCopy(dir, where, sizeof(dir));
		dir[strlen(dir) - 1] = 0;
		genMkdir(dir, fileMode);
	}
	mcCardBlankIdentity(sb, root, ctStamp(stamp) ? stamp : NULL);
	out.fd = genOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
	out.pos = 0;
	r = out.fd < 0 ? MCV_ERR_IO : MCV_OK;
	if (r == MCV_OK) {
		made = 1;
		dashLoadingStatus("Formatting", 0, 1);
		r = mcCardFormat(sb, root, (uint32_t)mb, ctAppend, &out);
		if (genClose(out.fd) < 0 && r == MCV_OK)
			r = MCV_ERR_IO;
	}
	if (r == MCV_OK) { /* reopen read-only: a whole, empty card of the chosen size */
		r = ctOpen(&io, dest, 0);
		if (r == MCV_OK) {
			if (io.size != (s64)mb << 20)
				r = MCW_ERR_VERIFY;
			if (r == MCV_OK)
				r = mcViewFreeClusters(&ct_view, &freec, &total);
			if (r == MCV_OK) {
				n = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &found);
				if (n != 0 || found != 0 || total == 0 || freec + 1u != total)
					r = MCW_ERR_VERIFY;
			}
			ctClose(&io);
		}
	}
	dashLoadingEnd();
	if (r != MCV_OK) {
		if (made)
			genRemove(dest);
		snprintf(msg, sizeof(msg), "%s%s", r == MCV_ERR_IO ? "The device may be read-only or full. " : ctError(r),
		         made ? " The unfinished file was removed." : "");
		dashModalMessage(DI_WARNING, "Blank card not made", msg, dest);
		return;
	}
	snprintf(msg, sizeof(msg), "Formatted and read back: %u KB free.%s", (unsigned)freec,
	         unit >= 0 ? " To use it, install it with \"Install as NEW selectable MMCE card\"." : "");
	dashModalMessage(DI_MC, "Blank card made", msg, dest);
}

static void ctSaveMenu(const char *image, const McViewSave *sv, const char *title, int writable)
{
	static const char *const ro = "PCSX2 .ps2 cards are read-only here";
	DashMenuItem items[4];
	char name[33];
	int r;

	dashStrCopy(name, sv->name, sizeof(name));
	items[0].icon = DI_FILE;
	items[0].label = "Copy to another card...";
	items[0].detail = "verified; the other card gets a safety copy first";
	items[0].disabled = 0;
	items[1].icon = DI_CLOSE;
	items[1].label = "Move to another card...";
	items[1].detail = writable ? "removed here only after it verifies there" : ro;
	items[1].disabled = !writable;
	items[2].icon = DI_MC;
	items[2].label = "Export as .psu...";
	items[2].detail = NULL;
	items[2].disabled = 0;
	items[3].icon = DI_WARNING;
	items[3].label = "Delete from this card...";
	items[3].detail = writable ? "safety copy of the card first" : ro;
	items[3].disabled = !writable;
	r = dashModalMenu(title, name, items, 4, 0);
	if (r < 0 || items[r].disabled)
		return;
	if (r == 0 || r == 1)
		ctCopy(image, name, r == 1);
	else if (r == 2)
		ctExportPsu(image, name);
	else
		ctDelete(image, name);
}

void dashCardTool(const char *image)
{
	CardIo io;
	char path[MAX_PATH], subtitle[96];
	uint32_t freec = 0, total = 0;
	int r, n, found, i, sel = 0, writable;

	dashStrCopy(path, image, sizeof(path));
	for (;;) {
		dashLoadingBegin("Reading card");
		r = ctOpen(&io, path, 0);
		n = 0;
		found = 0;
		if (r == MCV_OK) {
			writable = ct_view.page_stride == 512u;
			r = mcViewFreeClusters(&ct_view, &freec, &total);
			if (r == MCV_OK)
				n = mcViewListSaves(&ct_view, ct_saves, CT_MAX_SAVES, &found);
			if (n < 0)
				r = n;
			ctClose(&io);
		}
		dashLoadingEnd();
		if (r != MCV_OK) {
			dashModalMessage(DI_WARNING, "Cannot show this card", ctError(r), path);
			return;
		}

		ct_items[0].icon = DI_PLUS;
		ct_items[0].label = "Import .psu...";
		ct_items[0].detail = writable ? "the card gets a safety copy first" : "PCSX2 .ps2 cards are read-only here";
		ct_items[0].disabled = !writable;
		ct_items[1].icon = DI_MC;
		ct_items[1].label = "Copy to a new card of another size...";
		ct_items[1].detail = "8 to 64 MB; this card is only read";
		ct_items[1].disabled = 0;
		ct_items[2].icon = DI_MC;
		ct_items[2].label = "Split into Game ID cards...";
		ct_items[2].detail = "one card per game, for MMCE; this card is only read";
		ct_items[2].disabled = 0;
		for (i = 0; i < n; i++) {
			const McViewSave *sv = &ct_saves[i];

			if (sv->has_icon_sys && sv->title[0])
				dashSjisTitle(ct_titles[i], sizeof(ct_titles[i]), (const unsigned char *)sv->title);
			else
				dashStrCopy(ct_titles[i], sv->name, sizeof(ct_titles[i]));
			snprintf(ct_details[i], sizeof(ct_details[i]), "%s%.32s, %u file%s, %u KB", sv->is_ps1 ? "PS1 " : "",
			         sv->name, (unsigned)sv->files % 10000u, sv->files == 1 ? "" : "s",
			         (unsigned)((sv->bytes + 1023u) / 1024u) % 1000000u);
			ct_items[i + 3].icon = DI_MC;
			ct_items[i + 3].label = ct_titles[i];
			ct_items[i + 3].detail = ct_details[i];
			ct_items[i + 3].disabled = 0;
		}
		snprintf(subtitle, sizeof(subtitle), "%d save%s, %u KB free of %u KB%s", found, found == 1 ? "" : "s",
		         (unsigned)freec, (unsigned)total, found > n ? " (list shortened)" : "");
		if (sel > n + 2)
			sel = n + 2;
		r = dashModalMenu(ctBaseName(path), subtitle, ct_items, n + 3, sel);
		if (r < 0 || ct_items[r].disabled)
			return;
		sel = r;
		if (r == 0) {
			ctImportPsu(path);
		} else if (r == 1) {
			ctResize(path);
		} else if (r == 2) {
			ctSplitGameId(path);
		} else {
			char title[96];
			McViewSave sv = ct_saves[r - 3];

			dashStrCopy(title, ct_titles[r - 3], sizeof(title));
			ctSaveMenu(path, &sv, title, writable);
		}
	}
}

static void ctScanProgress(const char *path, int done)
{
	dashLoadingStatus(ctBaseName(path), done, done + 1);
}

void dashMmceShowActive(int unit)
{
	MmceActiveCard a;
	char body[160];
	int r;

	dashLoadingBegin("Finding the card in use");
	r = mmceActiveCard(unit, &a, ctScanProgress);
	dashLoadingEnd();
	if (r == MMCE_SWITCH_OK) {
		if (a.card > 0)
			snprintf(body, sizeof(body), "The device reports card %d, channel %d. %s", a.card, a.channel,
			         a.by_number ? "The content matches that image." : "The image below matches by content.");
		else
			snprintf(body, sizeof(body), "The image below matches the served card by content.");
		dashModalMessage(DI_MC, "MMCE card in use", body, a.path);
	} else if (r == MMCE_SWITCH_DEVICE) {
		dashModalMessage(DI_WARNING, "MMCE card in use", "The MMCE device did not answer.", NULL);
	} else {
		snprintf(body, sizeof(body), "The device reports card %d, channel %d, but %s.", a.card, a.channel,
		         a.matches > 1 ? "several card images have the same content" :
		                         "no card image under MemoryCards/PS2 could be matched by content");
		dashModalMessage(DI_WARNING, "Not confirmed", body, a.matches > 1 ? a.path : NULL);
	}
}

void dashMmceSwitch(const char *path)
{
	int card, chan, r;
	char body[192];

	if (!mmceCardParsePath(path, &card, &chan)) {
		dashModalMessage(DI_WARNING, "Cannot switch to this image",
		                 "Only numbered cards (MemoryCards/PS2/CardN/CardN-C.mcd) can be selected from here.", path);
		return;
	}
	if (mmceCardIsActive(path)) {
		dashToast(DASH_TOAST_INFO, "This card is already in use");
		return;
	}
	snprintf(body, sizeof(body),
	         "The console's memory card in MMCE slot %d becomes card %d, channel %d. Games and Launchstation will see that card from now on.",
	         mmceCardUnit(path) + 1, card, chan);
	if (!dashModalConfirm("Switch MMCE card?", body, "Switch", 0))
		return;
	dashLoadingBegin("Switching card");
	r = mmceSwitchToCard(path);
	dashLoadingEnd();
	if (r == MMCE_SWITCH_OK)
		dashToast(DASH_TOAST_OK, "Card switched and confirmed");
	else
		dashModalMessage(DI_WARNING, "Switch not confirmed",
		                 r == MMCE_SWITCH_BUSY ? "The device stayed busy for 15 seconds." :
		                 r == MMCE_SWITCH_DEVICE ? "The MMCE device did not answer." :
		                 r == MMCE_SWITCH_NOT_CARD ? "This image is not a readable PS2 memory card." :
		                                             "Afterwards the device did not report this card, or the card it serves has different content. Check the device's screen.",
		                 path);
}
