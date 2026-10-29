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

/* A verified safety copy of image into safety, unless safety already names
 * one made for it earlier in the same batch. */
static int ctSafetyOnce(const char *image, char *safety, int safety_size)
{
	if (safety[0])
		return 0;
	dashLoadingStatus("Making a safety copy", 0, 1);
	return ctSafetyCopy(image, safety, safety_size) < 0 ? -1 : 0;
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
	if (ctSafetyOnce(image, safety, safety_size) < 0)
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

	if (ctSafetyOnce(image, safety, safety_size) < 0)
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

/* A whole .psu read and checked into s; *why says what is wrong when it is not. */
static int ctLoadPsu(const char *psu, McSaveData *s, const char **why)
{
	u8 *buf;
	s64 size;
	u32 done = 0;
	int fd, n, r;

	memset(s, 0, sizeof(*s));
	fd = genOpen(psu, FIO_O_RDONLY);
	if (fd < 0) {
		*why = "The .psu could not be opened.";
		return MCV_ERR_IO;
	}
	size = genLseek(fd, 0, SEEK_END);
	if (size <= 0 || size > CT_PSU_MAX || genLseek(fd, 0, SEEK_SET) != 0) {
		genClose(fd);
		*why = "The .psu is empty or larger than any 8 MB card save.";
		return MCW_ERR_INVALID;
	}
	buf = (u8 *)malloc((size_t)size);
	if (buf == NULL) {
		genClose(fd);
		*why = ctError(MCW_ERR_NOMEM);
		return MCW_ERR_NOMEM;
	}
	while (done < (u32)size) {
		n = genRead(fd, buf + done, (int)((u32)size - done > 65536 ? 65536 : (u32)size - done));
		if (n <= 0)
			break;
		done += (u32)n;
	}
	genClose(fd);
	r = done == (u32)size ? mcCardSaveFromPsu(buf, (uint32_t)size, s) : MCV_ERR_IO;
	free(buf);
	*why = ctError(r);
	return r;
}

static void ctImportPsu(const char *image)
{
	McSaveData s;
	char psu[MAX_PATH], dir[MAX_PATH], safety[MAX_PATH];
	const char *why;
	int r;

	safety[0] = 0;
	dashStrCopy(dir, image, sizeof(dir));
	dir[ctBaseName(image) - image] = 0;
	if (!ctWritable(image) || !dashPickPath("Import which .psu?", dir, DASH_PICK_FILE, "PSU", psu, sizeof(psu)))
		return;
	if (!ctStacks(psu, image))
		return;
	r = ctLoadPsu(psu, &s, &why);
	if (r != MCW_OK) {
		dashModalMessage(DI_WARNING, "Not imported", why, psu);
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
		const char *gone = !made ? "" :
		                   genRemove(dest) < 0 ? " The unfinished card could not be removed; delete it." :
		                                         " The new card was removed.";

		if (r == MCW_ERR_FULL && bad >= 0)
			snprintf(msg, sizeof(msg), "The saves do not fit in %d MB (stopped at %s).%s", mb, names[bad], gone);
		else if (bad >= 0)
			snprintf(msg, sizeof(msg), "%s (save %s).%s", ctError(r), names[bad], gone);
		else
			snprintf(msg, sizeof(msg), "%s%s", ctError(r), gone);
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

enum { CT_LEFT_PART = 1, CT_LEFT_CARD = 2 };

/* One new Game ID card holding every save of game id, formatted with the
 * source card's identity. It is built as stage (dest + ".PART") and only
 * gets its final name once it reads back with exactly those saves. Where
 * the device cannot rename, dest is a verified copy and the .PART is removed
 * only after it, so a dest with a .PART beside it is never a finished card.
 * *left gets CT_LEFT_PART when a .PART could not be removed and
 * CT_LEFT_CARD when a partial dest could not be (its .PART is kept). */
static int ctGameCard(const char *image, const char *stage, const char *dest, const uint8_t sb[512],
                      const uint8_t root[1024], int mb, const CtGameSave *list, int n, const char *id, int *left)
{
	CardIo io;
	AppendIo out;
	McSaveData s;
	int r, i, want = 0, got = 0, k;

	out.fd = genOpen(stage, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
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
		if (r != MCW_OK)
			break;
		r = ctOpen(&io, stage, 1);
		if (r == MCV_OK) {
			int c;

			r = mcCardWriteSave(&ct_view, ctWrite, &io, &s);
			c = ctClose(&io);
			if (r == MCW_OK && c != MCV_OK)
				r = MCW_ERR_VERIFY;
		}
		mcCardFreeSave(&s);
	}
	if (r == MCV_OK) { /* the finished card lists exactly this game's saves */
		r = ctOpen(&io, stage, 0);
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
	/* Rename where the device can; otherwise a verified copy (UDPFS and host
	 * have no rename). Either way an existing dest is never replaced. */
	if (r == MCV_OK && genRenameNoReplace(stage, dest) < 0) {
		if (wleExists(dest))
			r = MCV_ERR_IO;
		else if (mcCardBackupNoEccNewFile(stage, dest, ctSafetyProgress) != MC_CARD_EXPORT_OK) {
			r = MCV_ERR_IO;
			if (wleExists(dest) && genRemove((char *)dest) < 0) {
				*left |= CT_LEFT_CARD; /* the .PART stays beside it as the marker */
				return r;
			}
		} else if (genRemove((char *)stage) < 0)
			*left |= CT_LEFT_PART;
	}
	if (r != MCV_OK && wleExists(stage) && genRemove((char *)stage) < 0)
		*left |= CT_LEFT_PART;
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
	char where[MAX_PATH], dir[MAX_PATH + 16], dest[MAX_PATH + 48], stage[MAX_PATH + 56], base[48], msg[160],
	    body[640], fail_id[11] = "";
	const char *name = ctBaseName(image), *dot;
	CtGameSave *list = NULL;
	CardIo io;
	int unit = mmceCardUnit(image), r, n = 0, found = 0, i, j, k, len, left = 0, stuck;
	int games = 0, other = 0, done = 0, made = 0, present = 0, stale = 0, failed = 0, copied = 0, fail_r = MCV_OK;

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

	/* One folder per source card, so two cards holding the same game do not
	 * collide. On MMCE it is outside MemoryCards/, so the device never
	 * serves a half-built card. */
	dot = strrchr(name, '.');
	k = dot != NULL ? (int)(dot - name) : (int)strlen(name);
	if (k > (int)sizeof(base) - 1)
		k = sizeof(base) - 1;
	memcpy(base, name, k);
	base[k] = 0;
	if (unit >= 0)
		len = snprintf(where, sizeof(where), "mmce%d:/WLE-GameID/%s/", unit, base);
	else
		len = snprintf(where, sizeof(where), "%.*s%s-GameID/", (int)(name - image), image, base);
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
	if (unit >= 0) {
		snprintf(dir, sizeof(dir), "mmce%d:/WLE-GameID", unit);
		genMkdir(dir, fileMode);
	}
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
		snprintf(stage, sizeof(stage), "%s.PART", dest);
		genMkdir(dir, fileMode);
		if (wleExists(stage)) { /* from an interrupted earlier split, maybe beside a partial copy */
			stale++;
			continue;
		}
		if (wleExists(dest)) {
			present++;
			continue;
		}
		/* The smallest card the game's saves fit on; 8 MB for nearly all.
		 * A .PART that could not be removed blocks only this game's retries. */
		stuck = 0;
		for (k = 0; k < 4; k++) {
			r = ctGameCard(image, stage, dest, sb, root, sizes[k], list, n, list[i].id, &stuck);
			if (r != MCW_ERR_FULL || stuck)
				break;
		}
		left |= stuck;
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
		len += snprintf(body + len, sizeof(body) - len, " %d failed (%s: %s).", failed, fail_id, ctError(fail_r));
	if (left & CT_LEFT_CARD)
		len += snprintf(body + len, sizeof(body) - len,
		                " A partly copied card could not be removed; delete that game's folder before splitting again.");
	else if (left)
		len += snprintf(body + len, sizeof(body) - len,
		                " An unfinished .PART file could not be removed; delete it before splitting again.");
	if (stale)
		len += snprintf(body + len, sizeof(body) - len,
		                " %d game%s skipped: an unfinished .PART from an earlier split is in %s folder; delete that folder to try again.",
		                stale, stale == 1 ? "" : "s", stale == 1 ? "its" : "their");
	if (other)
		len += snprintf(body + len, sizeof(body) - len, " %d save%s without a game serial %s left out.", other,
		                other == 1 ? "" : "s", other == 1 ? "was" : "were");
	if (made)
		snprintf(body + len, sizeof(body) - len, "%s",
		         unit >= 0 ? " Install each with \"Install as MMCE Game ID card\"." :
		                     " Copy each folder into MemoryCards/PS2/ on the MMCE SD card.");
	snprintf(msg, sizeof(msg), "%s", failed || stale ? "Some Game ID cards not made" : made ? "Game ID cards made" : "No new Game ID card");
	dashModalMessage(failed || stale || !made ? DI_WARNING : DI_MC, msg, body, where);
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
		const char *gone = !made ? "" :
		                   genRemove(dest) < 0 ? " The unfinished file could not be removed; delete it." :
		                                         " The unfinished file was removed.";

		snprintf(msg, sizeof(msg), "%s%s", r == MCV_ERR_IO ? "The device may be read-only or full. " : ctError(r), gone);
		dashModalMessage(DI_WARNING, "Blank card not made", msg, dest);
		return;
	}
	snprintf(msg, sizeof(msg), "Formatted and read back: %u KB free.%s", (unsigned)freec,
	         unit >= 0 ? " To use it, install it with \"Install as NEW selectable MMCE card\"." : "");
	dashModalMessage(DI_MC, "Blank card made", msg, dest);
}

/* The actions for one save; -1 when none was chosen. */
static int ctSaveMenu(const char *image, const McViewSave *sv, const char *title, int writable)
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
		return -1;
	if (r == 0 || r == 1)
		ctCopy(image, name, r == 1);
	else if (r == 2)
		ctExportPsu(image, name);
	else
		ctDelete(image, name);
	return r;
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

/* ---- All saves: every card image in a folder and its direct subfolders ---- */

#define CT_ALL_CARDS 64
#define CT_ALL_SUBS 256 /* folders looked into; the card limit is separate */
#define CT_ALL_SAVES 1000
#define CT_ALL_MAX_BYTES 69206016u /* a 64 MB card with ECC spare bytes */

typedef struct {
	char path[MAX_PATH];
	int writable;
} CtAnyCard;

typedef struct {
	McViewSave sv;
	int card;
	char title[96];
	char key[96]; /* the title shared by every copy of this save, for sorting */
	char detail[128];
} CtAnySave;

static int ctAllCandidate(const iox_dirent_t *ent)
{
	if (ent->name[0] == '.' || FIO_S_ISDIR(ent->stat.mode) || ent->stat.hisize || ent->stat.size > CT_ALL_MAX_BYTES)
		return 0;
	return genCmpFileExt(ent->name, "MCD") || genCmpFileExt(ent->name, "PS2") || genCmpFileExt(ent->name, "VMC") ||
	       genCmpFileExt(ent->name, "MC2") || genCmpFileExt(ent->name, "BIN");
}

/* Candidate images in folder (ending in '/') and one level below it, except
 * in Launchstation's WLE-* folders, in directory order. Folder names are read first: MMCE allows one open handle.
 * Returns how many were stored; *more is set when some did not fit. */
static int ctAllScan(const char *folder, CtAnyCard *cards, int *more)
{
	char(*subs)[MAX_NAME];
	char dir[MAX_PATH];
	iox_dirent_t ent;
	int fd, nsub = 0, n = 0, i;

	subs = malloc(CT_ALL_SUBS * sizeof(*subs));
	if (subs == NULL)
		return -1;
	for (i = -1; i < nsub; i++) {
		if (snprintf(dir, sizeof(dir), "%s%s%s", folder, i < 0 ? "" : subs[i], i < 0 ? "" : "/") >=
		    (int)sizeof(dir))
			continue;
		fd = genDopen(dir);
		if (fd < 0)
			continue;
		memset(&ent, 0, sizeof(ent));
		while (fileXioDread(fd, &ent) > 0) {
			if (i < 0 && ent.name[0] != '.' && FIO_S_ISDIR(ent.stat.mode)) {
				if (!strncmp(ent.name, "WLE-", 4))
					; /* safety copies and other holding areas would repeat every save */
				else if (nsub < CT_ALL_SUBS)
					snprintf(subs[nsub++], MAX_NAME, "%s", ent.name);
				else
					*more = 1;
			} else if (ctAllCandidate(&ent)) {
				size_t dl = strlen(dir), nl = strlen(ent.name);

				if (n < CT_ALL_CARDS && dl + nl < MAX_PATH) {
					memcpy(cards[n].path, dir, dl);
					memcpy(cards[n++].path + dl, ent.name, nl + 1);
				} else {
					*more = 1;
				}
			}
			memset(&ent, 0, sizeof(ent));
		}
		genDclose(fd);
	}
	free(subs);
	return n;
}

static int ctAllByName(const void *a, const void *b)
{
	const CtAnySave *x = a, *y = b;
	int c = strcmp(x->sv.name, y->sv.name);

	return c != 0 ? c : x->card - y->card;
}

static int ctAllCmp(const void *a, const void *b)
{
	const CtAnySave *x = a, *y = b;
	int c = strcasecmp(x->key, y->key);

	if (c == 0)
		c = strcmp(x->sv.name, y->sv.name);

	return c != 0 ? c : x->card - y->card;
}

/* Reads every card; returns how many saves were stored, sorted by title.
 * Copies of one save (same folder name) on different cards sort together
 * under the title any of them carries, even when one has lost its icon.sys. */
static int ctAllRead(const char *folder, CtAnyCard *cards, int ncards, CtAnySave *list, int *found, int *bad)
{
	CardIo io;
	McViewSave *saves;
	int c, i, k, n = 0, got;

	*found = *bad = 0;
	/* not ct_saves: one card may fill the whole list, past the 250 a single card view holds */
	saves = malloc(CT_ALL_SAVES * sizeof(*saves));
	if (saves == NULL)
		return -1;
	for (c = 0; c < ncards; c++) {
		dashLoadingStatus(ctBaseName(cards[c].path), c, ncards);
		cards[c].writable = 0;
		if (ctOpen(&io, cards[c].path, 0) != MCV_OK) {
			(*bad)++;
			continue;
		}
		cards[c].writable = ct_view.page_stride == 512u;
		k = mcViewListSaves(&ct_view, saves, CT_ALL_SAVES - n, &got);
		ctClose(&io);
		if (k < 0) {
			(*bad)++;
			continue;
		}
		*found += got;
		for (i = 0; i < k && n < CT_ALL_SAVES; i++, n++) {
			CtAnySave *s = &list[n];

			s->sv = saves[i];
			s->card = c;
			if (s->sv.has_icon_sys && s->sv.title[0])
				dashSjisTitle(s->title, sizeof(s->title), (const unsigned char *)s->sv.title);
			else
				dashStrCopy(s->title, s->sv.name, sizeof(s->title));
			snprintf(s->detail, sizeof(s->detail), "%.48s: %s%.32s, %u KB", cards[c].path + strlen(folder),
			         s->sv.is_ps1 ? "PS1 " : "", s->sv.name, (unsigned)((s->sv.bytes + 1023u) / 1024u) % 1000000u);
		}
	}
	free(saves);
	qsort(list, (size_t)n, sizeof(*list), ctAllByName);
	for (i = 0; i < n; i = k) {
		int t = i;

		for (k = i; k < n && !strcmp(list[k].sv.name, list[i].sv.name); k++)
			if (t == i && strcmp(list[k].title, list[k].sv.name))
				t = k;
		for (c = i; c < k; c++)
			dashStrCopy(list[c].key, list[t].title, sizeof(list[c].key));
	}
	qsort(list, (size_t)n, sizeof(*list), ctAllCmp);
	return n;
}

/* A batch refusal that leaves both cards as they were; the batch goes on. */
static int ctAllSkippable(int r)
{
	return r == MCW_ERR_EXISTS || r == MCW_ERR_FULL || r == MCW_ERR_UNSUPPORTED || r == MCW_ERR_NOT_FOUND;
}

/* The marked saves copied or moved to one card, or deleted. Every card gets
 * one verified safety copy before its first change in the batch, and the
 * batch stops at the first error that is not a plain refusal. 0 when it was
 * cancelled before anything was read or written. */
static int ctAllBatch(const char *folder, const CtAnyCard *cards, int ncards, const CtAnySave *list, int n,
                      const unsigned char *marks)
{
	DashMenuItem items[3];
	char(*safety)[MAX_PATH];
	char dest[MAX_PATH], dsafety[MAX_PATH], body[640], title[48], first[33] = "", what[64];
	const char *ro = NULL;
	int nmarked = 0, i, c, r, act, move, done = 0, skipped = 0, same = 0, failed = 0, left = 0, first_r = MCV_OK;
	int from = -1;

	for (i = 0; i < n; i++)
		if (marks[i]) {
			if (from < 0)
				from = list[i].card;
			nmarked++;
			if (!cards[list[i].card].writable)
				ro = "a marked save is on a PCSX2 .ps2 card, which is read-only here";
		}
	snprintf(title, sizeof(title), "%d marked save%s", nmarked, nmarked == 1 ? "" : "s");
	items[0].icon = DI_FILE;
	items[0].label = "Copy to another card...";
	items[0].detail = "verified; that card gets one safety copy first";
	items[0].disabled = 0;
	items[1].icon = DI_CLOSE;
	items[1].label = "Move to another card...";
	items[1].detail = ro ? ro : "each removed only after it verifies there";
	items[1].disabled = ro != NULL;
	items[2].icon = DI_WARNING;
	items[2].label = "Delete...";
	items[2].detail = ro ? ro : "each card gets one safety copy first";
	items[2].disabled = ro != NULL;
	act = dashModalMenu(title, NULL, items, 3, 0);
	if (act < 0 || items[act].disabled)
		return 0;
	move = act == 1;

	dest[0] = 0;
	if (act < 2) {
		if (!dashPickPath(move ? "Move the marked saves to which card image?" : "Copy the marked saves to which card image?",
		                  folder, DASH_PICK_FILE, NULL, dest, sizeof(dest)))
			return 0;
		if (!ctStacks(cards[from].path, dest) || !ctWritable(dest))
			return 0;
	}
	/* every card that would change must be safe to write, before anything is */
	if (act > 0)
		for (c = 0; c < ncards; c++) {
			for (i = 0; i < n && !(marks[i] && list[i].card == c); i++)
				;
			if (i < n && strcmp(cards[c].path, dest) && !ctWritable(cards[c].path))
				return 0;
		}
	if (act == 2)
		snprintf(body, sizeof(body), "Each card holding a marked save gets one safety copy of the whole card first.");
	else
		snprintf(body, sizeof(body),
		         "%s gets one safety copy first. A save already there by name, or one that does not fit, is skipped.%s",
		         ctBaseName(dest), move ? " Each save is removed from its card only after it reads back correctly there, and each card it leaves gets one safety copy first." : "");
	snprintf(what, sizeof(what), "%s %d save%s?", act == 0 ? "Copy" : move ? "Move" : "Delete", nmarked,
	         nmarked == 1 ? "" : "s");
	if (!dashModalConfirm(what, body, act == 0 ? "Copy" : move ? "Move" : "Delete", act == 2))
		return 0;
	safety = malloc((size_t)ncards * sizeof(*safety));
	if (safety == NULL) {
		dashModalMessage(DI_WARNING, "Not done", ctError(MCW_ERR_NOMEM), NULL);
		return 1;
	}
	for (c = 0; c < ncards; c++)
		safety[c][0] = 0;
	dsafety[0] = 0;

	dashLoadingBegin(act == 0 ? "Copying saves" : move ? "Moving saves" : "Deleting saves");
	for (i = 0; i < n; i++) {
		const CtAnySave *s = &list[i];
		McSaveData data;

		if (!marks[i])
			continue;
		if (failed) {
			left++;
			continue;
		}
		dashLoadingStatus(s->title, done + skipped + same, nmarked);
		if (act < 2 && !strcmp(cards[s->card].path, dest)) {
			same++;
			continue;
		}
		if (act == 2) {
			r = ctDropSave(cards[s->card].path, s->sv.name, safety[s->card], MAX_PATH);
		} else {
			r = ctReadSave(cards[s->card].path, s->sv.name, &data);
			if (r == MCW_OK) {
				r = ctPutSave(dest, &data, dsafety, sizeof(dsafety));
				mcCardFreeSave(&data);
			}
			if (r == MCW_OK && move)
				r = ctDropSave(cards[s->card].path, s->sv.name, safety[s->card], MAX_PATH);
		}
		if (r == MCW_OK) {
			done++;
			continue;
		}
		if (!first[0]) {
			dashStrCopy(first, s->sv.name, sizeof(first));
			first_r = r;
		}
		if (ctAllSkippable(r))
			skipped++;
		else
			failed++;
	}
	dashLoadingEnd();

	if (done)
		c = snprintf(body, sizeof(body), "%d save%s %s%s.", done, done == 1 ? "" : "s",
		             act == 0 ? "copied" : move ? "moved" : "deleted", act < 2 ? " and verified" : "");
	else
		c = snprintf(body, sizeof(body), "Nothing %s.", act == 0 ? "copied" : move ? "moved" : "deleted");
	if (same)
		c += snprintf(body + c, sizeof(body) - c, " %d already on that card.", same);
	if (skipped)
		c += snprintf(body + c, sizeof(body) - c, " %d skipped.", skipped);
	if (failed)
		c += snprintf(body + c, sizeof(body) - c, " %d failed.", failed);
	if (skipped || failed)
		c += snprintf(body + c, sizeof(body) - c, " First: %s: %s", first, ctError(first_r));
	if (left)
		c += snprintf(body + c, sizeof(body) - c, " Stopped there; %d not tried.", left);
	{
		const char *keep = dsafety[0] ? dsafety : NULL;

		for (i = 0; keep == NULL && i < ncards; i++)
			if (safety[i][0])
				keep = safety[i];
		if (keep != NULL)
			snprintf(dest, sizeof(dest), "Safety copy: %.1000s", keep);
		dashModalMessage(failed || skipped ? DI_WARNING : DI_MC, failed ? "Not all done" : "Done", body,
		                 keep ? dest : NULL);
	}
	free(safety);
	return 1;
}

void dashCardAllSaves(const char *folder)
{
	CtAnyCard *cards;
	CtAnySave *list;
	DashMenuItem *items;
	unsigned char *marks;
	char dir[MAX_PATH], subtitle[128], marked[64];
	size_t len;
	int ncards, n, found, bad, more = 0, i, r, sel = 0, nm;

	dashStrCopy(dir, folder, sizeof(dir));
	len = strlen(dir);
	if (len == 0 || len + 2 >= sizeof(dir))
		return;
	if (dir[len - 1] != '/' && dir[len - 1] != ':') {
		dir[len] = '/';
		dir[len + 1] = 0;
	}
	cards = malloc(CT_ALL_CARDS * sizeof(*cards));
	list = malloc(CT_ALL_SAVES * sizeof(*list));
	items = malloc((CT_ALL_SAVES + 1) * sizeof(*items));
	marks = malloc(CT_ALL_SAVES + 1);
	if (cards == NULL || list == NULL || items == NULL || marks == NULL) {
		free(cards);
		free(list);
		free(items);
		free(marks);
		dashModalMessage(DI_WARNING, "Cannot list saves", ctError(MCW_ERR_NOMEM), dir);
		return;
	}
	for (;;) {
		dashLoadingBegin("Reading cards");
		ncards = ctAllScan(dir, cards, &more);
		n = ncards > 0 ? ctAllRead(dir, cards, ncards, list, &found, &bad) : 0;
		dashLoadingEnd();
		if (n < 0) {
			dashModalMessage(DI_WARNING, "Cannot list saves", ctError(MCW_ERR_NOMEM), dir);
			break;
		}
		if (ncards <= 0 || n == 0) {
			dashModalMessage(DI_WARNING, "No saves found",
			                 more ? "None of the card images read holds a save, but this folder has more folders or cards than this view reads (256 folders, 64 cards). Open a folder further down." :
			                 ncards <= 0 ? "No memory card image (.mcd, .ps2, .vmc, .mc2, .bin) is in this folder or the folders directly inside it." :
			                               "None of the card images here holds a save.",
			                 dir);
			break;
		}
		/* row 0 acts on the marked saves; saves follow, and Square marks them */
		items[0].icon = DI_CHECK;
		items[0].label = "Marked saves...";
		items[0].detail = marked;
		items[0].disabled = 0;
		marks[0] = 2;
		for (i = 0; i < n; i++) {
			items[i + 1].icon = DI_MC;
			items[i + 1].label = list[i].title;
			items[i + 1].detail = list[i].detail;
			items[i + 1].disabled = 0;
			marks[i + 1] = 0;
		}
		len = (size_t)snprintf(subtitle, sizeof(subtitle), "%d save%s on %d card%s", found, found == 1 ? "" : "s",
		                       ncards - bad, ncards - bad == 1 ? "" : "s");
		if (found > n || more)
			len += (size_t)snprintf(subtitle + len, sizeof(subtitle) - len, " (list shortened)");
		if (bad)
			snprintf(subtitle + len, sizeof(subtitle) - len, "; %d file%s not readable as a card", bad,
			         bad == 1 ? "" : "s");
		if (sel > n)
			sel = n;
		for (;;) { /* until something may have changed the cards */
			for (i = 1, nm = 0; i <= n; i++)
				nm += marks[i] == 1;
			if (nm)
				snprintf(marked, sizeof(marked), "%d marked: copy, move or delete them", nm);
			else
				snprintf(marked, sizeof(marked), "mark saves with Square first");
			dashModalSetMarks(marks);
			r = dashModalMenu("All saves", subtitle, items, n + 1, sel);
			if (r < 0)
				goto out;
			sel = r;
			for (i = 1, nm = 0; i <= n; i++) /* marks change inside the menu */
				nm += marks[i] == 1;
			if (r == 0) {
				if (nm == 0) {
					dashToast(DASH_TOAST_INFO, "Mark saves with Square first");
					continue;
				}
				if (ctAllBatch(dir, cards, ncards, list, n, marks + 1))
					break;
				continue;
			}
			{
				CtAnySave s = list[r - 1];

				if (ctSaveMenu(cards[s.card].path, &s.sv, s.title, cards[s.card].writable) >= 0)
					break;
			}
		}
	}
out:
	free(cards);
	free(list);
	free(items);
	free(marks);
}

/* ---- Save library: the .psu files in a folder; its subfolders are sets ---- */

#define CT_LIB_FILES 500
#define CT_LIB_SETS 64

typedef struct {
	char rel[2 * MAX_NAME]; /* path below the library folder: NAME.psu or set/NAME.psu */
	int set;                /* index into the set names, or -1 at the top level */
	int ok;                 /* 1 when it reads as a save */
	McViewSave sv;
	char title[96];
	char detail[128];
} CtPsu;

typedef struct {
	char folder[MAX_PATH]; /* ends in '/' */
	char (*sets)[MAX_NAME];
	int nsets, setcount[CT_LIB_SETS];
	CtPsu *list;
	int n, more;
} CtLib;

/* Name, file count, bytes and icon.sys title of a save, as Card contents lists them. */
static void ctPsuInfo(const McSaveData *s, McViewSave *sv)
{
	uint32_t i;

	memset(sv, 0, sizeof(*sv));
	memcpy(sv->name, s->entry + 0x40, 32);
	sv->name[32] = 0;
	sv->files = s->nfiles;
	for (i = 0; i < s->nfiles; i++) {
		const McSaveFile *f = &s->files[i];

		sv->bytes += f->size;
		if (!sv->has_icon_sys && !strncmp((const char *)f->entry + 0x40, "icon.sys", 32) && f->size >= 260u &&
		    !memcmp(f->data, "PS2D", 4)) {
			sv->has_icon_sys = 1;
			sv->title_break = (uint16_t)(f->data[6] | f->data[7] << 8);
			memcpy(sv->title, f->data + 192, 68);
			sv->title[68] = 0;
			if (sv->title_break > 68u)
				sv->title_break = 0;
		}
	}
}

/* out = the library folder + rel; -1 (and an empty out) when it does not fit. */
static int ctLibPath(const CtLib *lib, const char *rel, char *out, size_t size)
{
	size_t fl = strlen(lib->folder), rl = strlen(rel);

	out[0] = 0;
	if (fl + rl >= size)
		return -1;
	memcpy(out, lib->folder, fl);
	memcpy(out + fl, rel, rl + 1);
	return 0;
}

static int ctLibCmp(const void *a, const void *b)
{
	const CtPsu *x = a, *y = b;
	int c = strcasecmp(x->title, y->title);

	return c != 0 ? c : strcmp(x->rel, y->rel);
}

static int ctLibSetCmp(const void *a, const void *b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

/* Lists every .psu in lib->folder and in the folders directly inside it
 * (except Launchstation's WLE-* folders), reads each one and sorts them by
 * title. Folders holding at least one .psu are the sets. 0, or -1 without memory. */
static int ctLibScan(CtLib *lib)
{
	char dir[MAX_PATH], path[MAX_PATH];
	iox_dirent_t ent;
	McSaveData s;
	const char *why;
	int fd, i, k, nsub = 0;

	lib->n = lib->nsets = lib->more = 0;
	for (i = -1; i < nsub; i++) {
		if (snprintf(dir, sizeof(dir), "%s%s%s", lib->folder, i < 0 ? "" : lib->sets[i], i < 0 ? "" : "/") >=
		    (int)sizeof(dir))
			continue;
		fd = genDopen(dir);
		if (fd < 0)
			continue;
		memset(&ent, 0, sizeof(ent));
		while (fileXioDread(fd, &ent) > 0) {
			if (i < 0 && ent.name[0] != '.' && FIO_S_ISDIR(ent.stat.mode)) {
				if (strncmp(ent.name, "WLE-", 4) && strlen(ent.name) < MAX_NAME) {
					if (nsub < CT_LIB_SETS)
						dashStrCopy(lib->sets[nsub++], ent.name, MAX_NAME);
					else
						lib->more = 1;
				}
			} else if (ent.name[0] != '.' && !FIO_S_ISDIR(ent.stat.mode) && genCmpFileExt(ent.name, "PSU")) {
				CtPsu *p = &lib->list[lib->n];

				if (lib->n >= CT_LIB_FILES ||
				    snprintf(p->rel, sizeof(p->rel), "%s%s%s", i < 0 ? "" : lib->sets[i], i < 0 ? "" : "/",
				             ent.name) >= (int)sizeof(p->rel)) {
					lib->more = 1;
				} else {
					p->set = i;
					lib->n++;
				}
			}
			memset(&ent, 0, sizeof(ent));
		}
		genDclose(fd);
	}
	/* read each file only now: the folder listings above are closed (MMCE allows one open handle) */
	for (i = 0; i < lib->n; i++) {
		CtPsu *p = &lib->list[i];

		dashLoadingStatus(p->rel, i, lib->n);
		p->ok = ctLibPath(lib, p->rel, path, sizeof(path)) == 0 && ctLoadPsu(path, &s, &why) == MCW_OK;
		if (p->ok) {
			ctPsuInfo(&s, &p->sv);
			mcCardFreeSave(&s);
			if (p->sv.has_icon_sys && p->sv.title[0])
				dashSjisTitle(p->title, sizeof(p->title), (const unsigned char *)p->sv.title);
			else
				dashStrCopy(p->title, p->sv.name, sizeof(p->title));
			snprintf(p->detail, sizeof(p->detail), "%.48s: %.32s, %u KB", p->rel, p->sv.name,
			         (unsigned)((p->sv.bytes + 1023u) / 1024u) % 1000000u);
		} else {
			memset(&p->sv, 0, sizeof(p->sv));
			dashStrCopy(p->title, ctBaseName(p->rel), sizeof(p->title));
			snprintf(p->detail, sizeof(p->detail), "%.48s: not a usable .psu", p->rel);
		}
	}
	qsort(lib->list, (size_t)lib->n, sizeof(*lib->list), ctLibCmp);

	/* the sets: folders with at least one .psu, sorted, renumbered */
	for (k = 0; k < nsub; k++)
		lib->setcount[k] = 0;
	for (i = 0; i < lib->n; i++)
		if (lib->list[i].set >= 0)
			lib->setcount[lib->list[i].set]++;
	for (k = 0; k < nsub; k++)
		if (lib->setcount[k])
			memmove(lib->sets[lib->nsets++], lib->sets[k], MAX_NAME);
	qsort(lib->sets, (size_t)lib->nsets, MAX_NAME, ctLibSetCmp);
	for (k = 0; k < lib->nsets; k++)
		lib->setcount[k] = 0;
	for (i = 0; i < lib->n; i++) {
		CtPsu *p = &lib->list[i];
		size_t sl = p->set >= 0 ? (size_t)(strchr(p->rel, '/') - p->rel) : 0;

		if (p->set < 0)
			continue;
		for (k = 0; k < lib->nsets && (strlen(lib->sets[k]) != sl || strncmp(lib->sets[k], p->rel, sl)); k++)
			;
		p->set = k < lib->nsets ? k : -1;
		if (p->set >= 0)
			lib->setcount[k]++;
	}
	return 0;
}

/* Copies the chosen .psu files to one card image: one verified safety copy
 * of that card first, every save read back, nothing on the card replaced.
 * A save already there by name, or one that does not fit, is skipped; any
 * other failure stops. pick[i] selects lib->list[i]. 0 when cancelled. */
static int ctLibToCard(const CtLib *lib, const unsigned char *pick, const char *what)
{
	McSaveData s;
	char dest[MAX_PATH], safety[MAX_PATH], path[MAX_PATH], body[640], title[64], first[33] = "";
	const char *why;
	int i, n = 0, r, done = 0, skipped = 0, failed = 0, left = 0, first_r = MCW_OK, c;

	for (i = 0; i < lib->n; i++)
		n += pick[i] != 0;
	if (!dashPickPath(n == 1 ? "Copy the save to which card image?" : "Copy the saves to which card image?",
	                  lib->folder, DASH_PICK_FILE, NULL, dest, sizeof(dest)))
		return 0;
	if (!ctStacks(lib->folder, dest) || !ctWritable(dest))
		return 0;
	snprintf(title, sizeof(title), "%s %d save%s?", what, n, n == 1 ? "" : "s");
	snprintf(body, sizeof(body),
	         "%s gets one safety copy first. A save already there by name is skipped, never replaced; so is one that does not fit.",
	         ctBaseName(dest));
	if (!dashModalConfirm(title, body, "Copy", 0))
		return 0;

	safety[0] = 0;
	dashLoadingBegin("Copying saves");
	for (i = 0; i < lib->n; i++) {
		const CtPsu *p = &lib->list[i];

		if (!pick[i])
			continue;
		if (failed) {
			left++;
			continue;
		}
		dashLoadingStatus(p->title, done + skipped, n);
		r = ctLibPath(lib, p->rel, path, sizeof(path)) == 0 ? ctLoadPsu(path, &s, &why) : MCV_ERR_IO;
		if (r == MCW_OK) {
			r = ctPutSave(dest, &s, safety, sizeof(safety));
			mcCardFreeSave(&s);
		}
		if (r == MCW_OK) {
			done++;
			continue;
		}
		if (!first[0]) {
			dashStrCopy(first, p->ok ? p->sv.name : ctBaseName(p->rel), sizeof(first));
			first_r = r;
		}
		if (r == MCW_ERR_EXISTS || r == MCW_ERR_FULL || r == MCW_ERR_UNSUPPORTED || r == MCW_ERR_INVALID)
			skipped++;
		else
			failed++;
	}
	dashLoadingEnd();

	if (done)
		c = snprintf(body, sizeof(body), "%d save%s copied and verified.", done, done == 1 ? "" : "s");
	else
		c = snprintf(body, sizeof(body), "Nothing copied.");
	if (skipped)
		c += snprintf(body + c, sizeof(body) - c, " %d skipped.", skipped);
	if (failed)
		c += snprintf(body + c, sizeof(body) - c, " %d failed.", failed);
	if (skipped || failed)
		c += snprintf(body + c, sizeof(body) - c, " First: %s: %s", first, ctError(first_r));
	if (left)
		snprintf(body + c, sizeof(body) - c, " Stopped there; %d not tried.", left);
	if (safety[0])
		snprintf(dest, sizeof(dest), "Safety copy: %.1000s", safety);
	dashModalMessage(failed || skipped ? DI_WARNING : DI_MC, failed ? "Not all done" : "Done", body,
	                 safety[0] ? dest : NULL);
	return 1;
}

/* A set name is one folder name, not starting with '.' or WLE- (those
 * folders are not read as sets). The filename keyboard filters the rest. */
static int ctSetNameOk(const char *name)
{
	return name[0] && name[0] != '.' && name[0] != ' ' && strncmp(name, "WLE-", 4) && !strpbrk(name, "/\\:");
}

/* Copies the chosen .psu files into the set folder (made if new). A file of
 * the same name already in the set is left as it is. 0 when cancelled. */
static int ctLibSaveSet(const CtLib *lib, const unsigned char *pick)
{
	char name[72] = "", dir[MAX_PATH], path[MAX_PATH], from[MAX_PATH], body[256];
	int i, n = 0, done = 0, same = 0, failed = 0, fd;

	for (i = 0; i < lib->n; i++)
		n += pick[i] != 0;
	if (!dashModalFilename("Name of the set", name, 64, lib->folder))
		return 0;
	if (!ctSetNameOk(name)) {
		dashModalMessage(DI_WARNING, "Choose another name", "A set's name cannot start with . or WLE-.", name);
		return 0;
	}
	if (snprintf(dir, sizeof(dir), "%s%s/", lib->folder, name) >= (int)sizeof(dir))
		return 0;
	if (!dashModalConfirm(wleExists(dir) ? "Add to this set?" : "Make this set?",
	                      "The marked .psu files are copied into the set's folder and read back. A file already there by name is left as it is.",
	                      "Save", 0))
		return 0;
	genMkdir(dir, fileMode);
	fd = genDopen(dir);
	if (fd < 0) {
		dashModalMessage(DI_WARNING, "Not done", "The set's folder could not be made.", dir);
		return 1;
	}
	genDclose(fd);

	dashLoadingBegin("Saving the set");
	for (i = 0; i < lib->n; i++) {
		const CtPsu *p = &lib->list[i];
		u8 *buf;
		s64 size;
		u32 got = 0;
		int k;

		if (!pick[i])
			continue;
		dashLoadingStatus(p->title, done + same + failed, n);
		if (snprintf(path, sizeof(path), "%s%s", dir, ctBaseName(p->rel)) >= (int)sizeof(path)) {
			failed++;
			continue;
		}
		if (wleExists(path)) { /* also the file itself, when marked inside this set */
			same++;
			continue;
		}
		fd = ctLibPath(lib, p->rel, from, sizeof(from)) == 0 ? genOpen(from, FIO_O_RDONLY) : -1;
		size = fd >= 0 ? genLseek(fd, 0, SEEK_END) : -1;
		buf = size > 0 && size <= CT_PSU_MAX && genLseek(fd, 0, SEEK_SET) == 0 ? (u8 *)malloc((size_t)size) : NULL;
		while (buf != NULL && got < (u32)size && (k = genRead(fd, buf + got, (int)((u32)size - got))) > 0)
			got += (u32)k;
		if (fd >= 0)
			genClose(fd);
		if (buf != NULL && got == (u32)size && ctWriteFile(path, buf, got) == 0)
			done++;
		else
			failed++;
		free(buf);
	}
	dashLoadingEnd();
	i = snprintf(body, sizeof(body), "%d file%s copied into %s.", done, done == 1 ? "" : "s", name);
	if (same)
		i += snprintf(body + i, sizeof(body) - i, " %d already there.", same);
	if (failed)
		snprintf(body + i, sizeof(body) - i, " %d could not be copied.", failed);
	dashModalMessage(failed ? DI_WARNING : DI_MC, failed ? "Not all done" : "Done", body, dir);
	return 1;
}

/* Removes the chosen .psu files, after one confirmation. */
static int ctLibDelete(const CtLib *lib, const unsigned char *pick)
{
	char path[MAX_PATH], title[48], body[160];
	int i, n = 0, done = 0, failed = 0;

	for (i = 0; i < lib->n; i++)
		n += pick[i] != 0;
	snprintf(title, sizeof(title), "Delete %d .psu file%s?", n, n == 1 ? "" : "s");
	if (!dashModalConfirm(title, "No safety copy is made of a .psu file. Cards are not changed.", "Delete", 1))
		return 0;
	for (i = 0; i < lib->n; i++) {
		if (!pick[i])
			continue;
		if (ctLibPath(lib, lib->list[i].rel, path, sizeof(path)) < 0 || genRemove(path) < 0 || wleExists(path))
			failed++;
		else
			done++;
	}
	if (failed) {
		snprintf(body, sizeof(body), "%d deleted, %d could not be deleted.", done, failed);
		dashModalMessage(DI_WARNING, "Not all done", body, lib->folder);
	} else {
		dashToast(DASH_TOAST_OK, n == 1 ? ".psu deleted" : ".psu files deleted");
	}
	return 1;
}

/* One set, chosen from a list, copied to a card. 1 when anything may have changed. */
static int ctLibApplySet(const CtLib *lib)
{
	DashMenuItem *items;
	unsigned char *pick;
	char (*details)[48];
	char what[MAX_NAME + 16];
	int k, i, r;

	items = malloc((size_t)lib->nsets * sizeof(*items));
	details = malloc((size_t)lib->nsets * sizeof(*details));
	if (items == NULL || details == NULL) {
		free(items);
		free(details);
		return 0;
	}
	for (k = 0; k < lib->nsets; k++) {
		snprintf(details[k], sizeof(details[k]), "%d save%s", lib->setcount[k], lib->setcount[k] == 1 ? "" : "s");
		items[k].icon = DI_FOLDER;
		items[k].label = lib->sets[k];
		items[k].detail = details[k];
		items[k].disabled = 0;
	}
	r = dashModalMenu("Apply which set?", "copied to a card; nothing on it is replaced", items, lib->nsets, 0);
	free(items);
	free(details);
	if (r < 0)
		return 0;
	pick = malloc((size_t)lib->n);
	if (pick == NULL)
		return 0;
	for (i = 0; i < lib->n; i++)
		pick[i] = lib->list[i].set == r;
	snprintf(what, sizeof(what), "Apply set %.64s: copy", lib->sets[r]);
	r = ctLibToCard(lib, pick, what);
	free(pick);
	return r;
}

/* What to do with one .psu. 1 when anything may have changed. */
static int ctLibFileMenu(const CtLib *lib, int i)
{
	DashMenuItem items[2];
	unsigned char *pick;
	int r;

	items[0].icon = DI_FILE;
	items[0].label = "Copy to a card...";
	items[0].detail = lib->list[i].ok ? "verified; the card gets a safety copy first" : "not a usable .psu";
	items[0].disabled = !lib->list[i].ok;
	items[1].icon = DI_WARNING;
	items[1].label = "Delete this .psu...";
	items[1].detail = NULL;
	items[1].disabled = 0;
	r = dashModalMenu(lib->list[i].title, lib->list[i].rel, items, 2, items[0].disabled ? 1 : 0);
	if (r < 0 || items[r].disabled)
		return 0;
	pick = calloc((size_t)lib->n, 1);
	if (pick == NULL)
		return 0;
	pick[i] = 1;
	r = r == 0 ? ctLibToCard(lib, pick, "Copy") : ctLibDelete(lib, pick);
	free(pick);
	return r;
}

void dashCardSaveLibrary(const char *folder)
{
	CtLib lib;
	DashMenuItem *items = NULL;
	unsigned char *marks = NULL;
	char subtitle[96], marked[48], sets[48];
	int r, i, nm, sel = 0, act;
	size_t len;

	memset(&lib, 0, sizeof(lib));
	len = strlen(folder);
	if (len == 0 || len + 2 > sizeof(lib.folder))
		return;
	memcpy(lib.folder, folder, len + 1);
	if (lib.folder[len - 1] != '/')
		memcpy(lib.folder + len, "/", 2);
	lib.sets = malloc(CT_LIB_SETS * sizeof(*lib.sets));
	lib.list = malloc(CT_LIB_FILES * sizeof(*lib.list));
	items = malloc((CT_LIB_FILES + 2) * sizeof(*items));
	marks = malloc(CT_LIB_FILES + 2);
	if (lib.sets == NULL || lib.list == NULL || items == NULL || marks == NULL) {
		dashModalMessage(DI_WARNING, "Cannot show the saves", ctError(MCW_ERR_NOMEM), folder);
		goto out;
	}
	for (;;) { /* read again after anything that may have changed the files */
		dashLoadingBegin("Reading .psu files");
		ctLibScan(&lib);
		dashLoadingEnd();
		if (lib.n == 0) {
			dashModalMessage(DI_MC, "No .psu files here",
			                 "No .psu save files were found in this folder or the folders directly inside it.",
			                 lib.folder);
			break;
		}
		/* row 0 acts on the marked files, row 1 applies a set; files follow, and Square marks them */
		items[0].icon = DI_CHECK;
		items[0].label = "Marked saves...";
		items[0].detail = marked;
		items[0].disabled = 0;
		items[1].icon = DI_FOLDER;
		items[1].label = "Apply a set to a card...";
		if (lib.nsets)
			snprintf(sets, sizeof(sets), "%d set%s: the folders here with .psu files", lib.nsets,
			         lib.nsets == 1 ? "" : "s");
		else
			snprintf(sets, sizeof(sets), "no sets yet: mark saves, then Save as a set");
		items[1].detail = sets;
		items[1].disabled = lib.nsets == 0;
		marks[0] = marks[1] = 2;
		for (i = 0; i < lib.n; i++) {
			items[i + 2].icon = DI_MC;
			items[i + 2].label = lib.list[i].title;
			items[i + 2].detail = lib.list[i].detail;
			items[i + 2].disabled = 0;
			marks[i + 2] = 0;
		}
		snprintf(subtitle, sizeof(subtitle), "%d .psu file%s, %d set%s%s", lib.n, lib.n == 1 ? "" : "s", lib.nsets,
		         lib.nsets == 1 ? "" : "s", lib.more ? " (list shortened)" : "");
		if (sel > lib.n + 1)
			sel = lib.n + 1;
		for (;;) { /* until something may have changed the files */
			for (i = 2, nm = 0; i < lib.n + 2; i++)
				nm += marks[i] == 1;
			if (nm)
				snprintf(marked, sizeof(marked), "%d marked: copy, save as a set or delete", nm);
			else
				snprintf(marked, sizeof(marked), "mark saves with Square first");
			dashModalSetMarks(marks);
			r = dashModalMenu("Save library", subtitle, items, lib.n + 2, sel);
			if (r < 0)
				goto out;
			sel = r;
			for (i = 2, nm = 0; i < lib.n + 2; i++) /* marks change inside the menu */
				nm += marks[i] == 1;
			if (r == 1 || r >= 2) {
				if (r == 1 ? ctLibApplySet(&lib) : ctLibFileMenu(&lib, r - 2))
					break;
				continue;
			}
			if (nm == 0) {
				dashToast(DASH_TOAST_INFO, "Mark saves with Square first");
				continue;
			}
			{
				DashMenuItem acts[3];
				char title[48];
				int bad = 0;

				for (i = 0; i < lib.n; i++)
					bad += marks[i + 2] == 1 && !lib.list[i].ok;
				snprintf(title, sizeof(title), "%d marked save%s", nm, nm == 1 ? "" : "s");
				acts[0].icon = DI_FILE;
				acts[0].label = "Copy to a card...";
				acts[0].detail = bad ? "a marked file is not a usable .psu" : "verified; the card gets one safety copy first";
				acts[0].disabled = bad != 0;
				acts[1].icon = DI_FOLDER;
				acts[1].label = "Save as a set...";
				acts[1].detail = "copies them into a named folder here";
				acts[1].disabled = 0;
				acts[2].icon = DI_WARNING;
				acts[2].label = "Delete the .psu files...";
				acts[2].detail = "cards are not changed";
				acts[2].disabled = 0;
				act = dashModalMenu(title, NULL, acts, 3, bad ? 1 : 0);
				if (act < 0 || acts[act].disabled)
					continue;
				if (act == 0 ? ctLibToCard(&lib, marks + 2, "Copy") :
				    act == 1 ? ctLibSaveSet(&lib, marks + 2) :
				               ctLibDelete(&lib, marks + 2))
					break;
			}
		}
	}
out:
	free(lib.sets);
	free(lib.list);
	free(items);
	free(marks);
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
