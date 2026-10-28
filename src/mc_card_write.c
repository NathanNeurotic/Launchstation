//--------------------------------------------------------------
//File name:   mc_card_write.c
//Save copy and delete inside PS2 memory card images. See
//include/mc_card_write.h for the write order that keeps this crash-safe.
//--------------------------------------------------------------
#include <stdlib.h>
#include <string.h>
#include "mc_card_write.h"

#define CL 1024u
#define ENT 512u
#define M_EXISTS 0x8000u
#define M_DIR 0x0020u
#define M_FILE 0x0010u
#define C_END 0xFFFFFFFFu
#define C_FREE 0x7FFFFFFFu
#define MAX_ENTRIES 4096u
#define MAX_DEPTH 8u

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* A name field is 32 bytes; a usable name is 1..31 bytes, NUL-terminated,
 * not "." or "..", without '/'. */
static int nameOk(const uint8_t *name)
{
	uint32_t n;

	for (n = 0; n < 32 && name[n]; n++)
		if (name[n] == '/')
			return 0;
	if (n == 0 || n == 32)
		return 0;
	if (name[0] == '.' && (n == 1 || (n == 2 && name[1] == '.')))
		return 0;
	return 1;
}

static int nameEq(const uint8_t *a, const char *b)
{
	return strncmp((const char *)a, b, 32) == 0;
}

/* Next cluster of a chain, strictly: the entry must be in use. */
static int nextOf(McView *v, uint32_t c, uint32_t *next)
{
	uint32_t val;
	int r = mcViewFatGet(v, c, &val);

	if (r != MCV_OK)
		return r;
	if (!(val & 0x80000000u))
		return MCV_ERR_CORRUPT;
	*next = (val == C_END) ? C_END : (val & 0x7FFFFFFFu);
	if (*next != C_END && *next >= v->alloc_usable)
		return MCV_ERR_CORRUPT;
	return MCV_OK;
}

/* The n-th cluster of a chain (0 = first). */
static int clusterAt(McView *v, uint32_t first, uint32_t n, uint32_t *out)
{
	uint32_t c = first;
	int r;

	if (c >= v->alloc_usable)
		return MCV_ERR_CORRUPT;
	while (n-- > 0) {
		r = nextOf(v, c, &c);
		if (r != MCV_OK)
			return r;
		if (c == C_END)
			return MCV_ERR_CORRUPT;
	}
	*out = c;
	return MCV_OK;
}

/* ---------------------------------------------------------------- reading */

typedef struct RootFind {
	const char *name;
	int64_t found;       /* index of the live folder entry with that name, or -1 */
	uint32_t free_slot;  /* first unused slot at index >= 2, or the root length */
	uint32_t length;
	uint8_t entry[ENT];
} RootFind;

static int rootFindCb(McView *v, uint32_t i, const uint8_t *raw, void *ctx)
{
	RootFind *f = (RootFind *)ctx;
	uint32_t mode = rd16(raw);

	(void)v;
	if (i < 2)
		return MCV_OK;
	if (!(mode & M_EXISTS)) {
		if (f->free_slot == f->length)
			f->free_slot = i;
		return MCV_OK;
	}
	if (f->found < 0 && nameEq(raw + 0x40, f->name)) {
		f->found = i;
		memcpy(f->entry, raw, ENT);
	}
	return MCV_OK;
}

static int rootLength(McView *v, uint32_t *length)
{
	uint8_t buf[CL];
	int r = mcViewReadClusterRel(v, 0, buf);

	if (r != MCV_OK)
		return r;
	*length = rd32(buf + 4);
	if (*length < 2 || *length > MAX_ENTRIES)
		return MCV_ERR_CORRUPT;
	return MCV_OK;
}

static int rootFind(McView *v, const char *name, RootFind *f)
{
	int r;

	memset(f, 0, sizeof(*f));
	f->name = name;
	f->found = -1;
	r = rootLength(v, &f->length);
	if (r != MCV_OK)
		return r;
	f->free_slot = f->length;
	return mcViewWalkDir(v, 0, f->length, rootFindCb, f);
}

typedef struct SaveScan {
	McSaveData *s;
	uint32_t nfiles;
	int fill;
	int bad;
} SaveScan;

static int saveScanCb(McView *v, uint32_t i, const uint8_t *raw, void *ctx)
{
	SaveScan *sc = (SaveScan *)ctx;
	uint32_t mode = rd16(raw);

	(void)v;
	if (i == 0 || i == 1) {
		if (sc->fill)
			memcpy(i == 0 ? sc->s->dot : sc->s->dotdot, raw, ENT);
		return MCV_OK;
	}
	if (!(mode & M_EXISTS))
		return MCV_OK; /* a deleted file's slot */
	if (mode & M_DIR) {
		sc->bad = MCW_ERR_UNSUPPORTED;
		return MCW_ERR_UNSUPPORTED;
	}
	if (!(mode & M_FILE) || !nameOk(raw + 0x40)) {
		sc->bad = MCV_ERR_CORRUPT;
		return MCV_ERR_CORRUPT;
	}
	if (sc->fill)
		memcpy(sc->s->files[sc->nfiles].entry, raw, ENT);
	sc->nfiles++;
	return MCV_OK;
}

void mcCardFreeSave(McSaveData *s)
{
	uint32_t i;

	if (!s)
		return;
	if (s->files) {
		for (i = 0; i < s->nfiles; i++)
			free(s->files[i].data);
		free(s->files);
	}
	memset(s, 0, sizeof(*s));
}

int mcCardReadSave(McView *v, const char *name, McSaveData *out)
{
	RootFind f;
	SaveScan sc;
	uint32_t i, first, count;
	uint64_t total = 0;
	int r;

	if (!v || !name || !out)
		return MCW_ERR_INVALID;
	memset(out, 0, sizeof(*out));
	r = rootFind(v, name, &f);
	if (r != MCV_OK)
		return r;
	if (f.found < 0 || !(rd16(f.entry) & M_DIR))
		return MCW_ERR_NOT_FOUND;
	first = rd32(f.entry + 0x10);
	count = rd32(f.entry + 4);
	if (count < 2 || count > MAX_ENTRIES || first >= v->alloc_usable)
		return MCV_ERR_CORRUPT;
	memcpy(out->entry, f.entry, ENT);

	memset(&sc, 0, sizeof(sc));
	sc.s = out;
	r = mcViewWalkDir(v, first, count, saveScanCb, &sc);
	if (r != MCV_OK)
		return sc.bad ? sc.bad : r;
	if (sc.nfiles > 0) {
		out->files = (McSaveFile *)calloc(sc.nfiles, sizeof(McSaveFile));
		if (!out->files)
			return MCW_ERR_NOMEM;
	}
	sc.nfiles = 0;
	sc.fill = 1;
	r = mcViewWalkDir(v, first, count, saveScanCb, &sc);
	out->nfiles = sc.nfiles;
	if (r != MCV_OK) {
		mcCardFreeSave(out);
		return sc.bad ? sc.bad : r;
	}

	for (i = 0; i < out->nfiles; i++) {
		McSaveFile *fl = &out->files[i];
		uint32_t len = rd32(fl->entry + 4);

		total += len;
		if (total > (uint64_t)v->alloc_usable * CL) {
			mcCardFreeSave(out);
			return MCV_ERR_CORRUPT;
		}
		fl->size = len;
		fl->data = (uint8_t *)malloc(len ? len : 1);
		if (!fl->data) {
			mcCardFreeSave(out);
			return MCW_ERR_NOMEM;
		}
		if (len) {
			r = mcViewReadChain(v, rd32(fl->entry + 0x10), 0, fl->data, len);
			if (r != MCV_OK) {
				mcCardFreeSave(out);
				return r;
			}
		}
	}
	return MCW_OK;
}

uint32_t mcCardSaveClusters(const McSaveData *s)
{
	uint32_t i, n = (s->nfiles + 2u + 1u) / 2u; /* the save's own directory */

	for (i = 0; i < s->nfiles; i++)
		n += (s->files[i].size + CL - 1u) / CL;
	return n;
}

/* ---------------------------------------------------------------- writing */

typedef struct FatPatch {
	uint32_t abs;
	uint32_t val[256];
} FatPatch;

typedef struct Writer {
	McView *v;
	McWriteFn write;
	void *ctx;
	FatPatch *fat;
	uint32_t nfat;
	uint32_t scan;
} Writer;

static int writerPatch(Writer *w, uint32_t abs, FatPatch **out)
{
	uint8_t raw[CL];
	FatPatch *grown;
	uint32_t i;
	int r;

	for (i = 0; i < w->nfat; i++) {
		if (w->fat[i].abs == abs) {
			*out = &w->fat[i];
			return MCV_OK;
		}
	}
	r = w->v->src.read(w->v->src.ctx, (uint64_t)abs * CL, raw, CL);
	if (r != 0)
		return MCV_ERR_IO;
	grown = (FatPatch *)realloc(w->fat, (w->nfat + 1u) * sizeof(FatPatch));
	if (!grown)
		return MCW_ERR_NOMEM;
	w->fat = grown;
	w->fat[w->nfat].abs = abs;
	for (i = 0; i < 256; i++)
		w->fat[w->nfat].val[i] = rd32(raw + i * 4u);
	*out = &w->fat[w->nfat++];
	return MCV_OK;
}

static int writerFatGet(Writer *w, uint32_t idx, uint32_t *val)
{
	uint32_t abs, i;
	int r = mcViewFatLocate(w->v, idx, &abs);

	if (r != MCV_OK)
		return r;
	for (i = 0; i < w->nfat; i++) {
		if (w->fat[i].abs == abs) {
			*val = w->fat[i].val[idx % 256u];
			return MCV_OK;
		}
	}
	return mcViewFatGet(w->v, idx, val);
}

static int writerFatSet(Writer *w, uint32_t idx, uint32_t val)
{
	FatPatch *p;
	uint32_t abs;
	int r = mcViewFatLocate(w->v, idx, &abs);

	if (r != MCV_OK)
		return r;
	r = writerPatch(w, abs, &p);
	if (r != MCV_OK)
		return r;
	p->val[idx % 256u] = val;
	return MCV_OK;
}

/* Takes the lowest free cluster and marks it end-of-chain, in memory only. */
static int writerAlloc(Writer *w, uint32_t *out)
{
	uint32_t c, val;
	int r;

	for (c = w->scan; c < w->v->alloc_usable; c++) {
		r = writerFatGet(w, c, &val);
		if (r != MCV_OK)
			return r;
		if (!(val & 0x80000000u)) {
			r = writerFatSet(w, c, C_END);
			if (r != MCV_OK)
				return r;
			w->scan = c + 1u;
			*out = c;
			return MCV_OK;
		}
	}
	return MCW_ERR_FULL;
}

static int writerChain(Writer *w, uint32_t n, uint32_t *chain)
{
	uint32_t i;
	int r;

	for (i = 0; i < n; i++) {
		r = writerAlloc(w, &chain[i]);
		if (r != MCV_OK)
			return r;
		if (i > 0) {
			r = writerFatSet(w, chain[i - 1], 0x80000000u | chain[i]);
			if (r != MCV_OK)
				return r;
		}
	}
	return MCV_OK;
}

static int writeAt(Writer *w, uint64_t offset, const void *buf, uint32_t len)
{
	return w->write(w->ctx, offset, buf, len) == 0 ? MCV_OK : MCV_ERR_IO;
}

static int writeCluster(Writer *w, uint32_t rel, const uint8_t *buf)
{
	return writeAt(w, (uint64_t)(w->v->alloc_offset + rel) * CL, buf, CL);
}

/* One directory entry is one 512-byte page. */
static int writeEntry(Writer *w, uint32_t dir_first, uint32_t index, const uint8_t *ent)
{
	uint32_t c;
	int r = clusterAt(w->v, dir_first, index / 2u, &c);

	if (r != MCV_OK)
		return r;
	return writeAt(w, (uint64_t)(w->v->alloc_offset + c) * CL + (index % 2u) * ENT, ent, ENT);
}

static int writerFlush(Writer *w)
{
	uint8_t raw[CL];
	uint32_t i, j;
	int r;

	for (i = 0; i < w->nfat; i++) {
		for (j = 0; j < 256; j++)
			wr32(raw + j * 4u, w->fat[i].val[j]);
		r = writeAt(w, (uint64_t)w->fat[i].abs * CL, raw, CL);
		if (r != MCV_OK)
			return r;
	}
	mcViewDropCaches(w->v);
	return MCV_OK;
}

static void writerEnd(Writer *w)
{
	free(w->fat);
	w->fat = NULL;
	w->nfat = 0;
}

static int reopen(McView *v)
{
	McViewSource src = v->src;

	return mcViewOpen(v, &src);
}

static int validSave(const McSaveData *s)
{
	uint32_t i, j;
	uint64_t total = 0;

	if (!s || !nameOk(s->entry + 0x40) || s->nfiles > MAX_ENTRIES - 2u ||
	    (s->nfiles > 0 && !s->files))
		return 0;
	for (i = 0; i < s->nfiles; i++) {
		const McSaveFile *f = &s->files[i];

		if (!nameOk(f->entry + 0x40) || (f->size > 0 && !f->data))
			return 0;
		total += f->size;
		for (j = 0; j < i; j++)
			if (!strncmp((const char *)f->entry + 0x40, (const char *)s->files[j].entry + 0x40, 32))
				return 0;
	}
	return total <= 128ull * 1024ull * 1024ull;
}

static int sameSave(const McSaveData *a, const McSaveData *b)
{
	uint32_t i;

	if (a->nfiles != b->nfiles || strncmp((const char *)a->entry + 0x40, (const char *)b->entry + 0x40, 32))
		return 0;
	for (i = 0; i < a->nfiles; i++) {
		const McSaveFile *x = &a->files[i], *y = &b->files[i];

		if (x->size != y->size || strncmp((const char *)x->entry + 0x40, (const char *)y->entry + 0x40, 32) ||
		    (rd16(x->entry) | M_EXISTS | M_FILE) != rd16(y->entry) ||
		    (x->size && memcmp(x->data, y->data, x->size)))
			return 0;
	}
	return 1;
}

int mcCardWriteSave(McView *v, McWriteFn write, void *wctx, const McSaveData *s)
{
	RootFind f;
	Writer w;
	McSaveData back;
	uint8_t *dirbuf = NULL, rootent[ENT], buf[CL];
	uint32_t *chain = NULL;
	uint32_t i, slot, need, freec, total, dir_n, dir_cl, last = 0, grow_c = C_END;
	uint32_t root_clusters, max_cl = 0;
	int grow = 0, r;
	char name[33];

	if (!v || !write || !s)
		return MCW_ERR_INVALID;
	if (v->page_stride != 512u)
		return MCW_ERR_UNSUPPORTED; /* ECC images would need their spare bytes rewritten */
	if (!validSave(s))
		return MCW_ERR_INVALID;
	memcpy(name, s->entry + 0x40, 32);
	name[32] = 0;

	/* Plan. Nothing below writes until the free-space check has passed. */
	r = rootFind(v, name, &f);
	if (r != MCV_OK)
		return r;
	if (f.found >= 0)
		return MCW_ERR_EXISTS;
	slot = f.free_slot;
	if (slot == f.length) {
		if (f.length + 1u > MAX_ENTRIES)
			return MCW_ERR_FULL;
		root_clusters = (f.length + 1u) / 2u; /* clusters holding the current entries */
		if (slot % 2u == 0) {
			/* The new slot starts a cluster: reuse one the chain already has, or grow it. */
			r = clusterAt(v, 0, root_clusters - 1u, &last);
			if (r != MCV_OK)
				return r;
			r = nextOf(v, last, &grow_c);
			if (r != MCV_OK)
				return r;
			grow = (grow_c == C_END);
		}
	}
	dir_n = s->nfiles + 2u;
	dir_cl = (dir_n + 1u) / 2u;
	need = mcCardSaveClusters(s) + (grow ? 1u : 0u);
	r = mcViewFreeClusters(v, &freec, &total);
	if (r != MCV_OK)
		return r;
	if (freec < need)
		return MCW_ERR_FULL;

	for (i = 0; i < s->nfiles; i++)
		if ((s->files[i].size + CL - 1u) / CL > max_cl)
			max_cl = (s->files[i].size + CL - 1u) / CL;
	if (dir_cl > max_cl)
		max_cl = dir_cl;
	chain = (uint32_t *)malloc(max_cl * sizeof(uint32_t));
	dirbuf = (uint8_t *)calloc(dir_cl, CL);
	if (!chain || !dirbuf) {
		free(chain);
		free(dirbuf);
		return MCW_ERR_NOMEM;
	}
	memset(&w, 0, sizeof(w));
	w.v = v;
	w.write = write;
	w.ctx = wctx;
	w.scan = 1;

	/* 1. File data into free clusters: invisible until the FAT and the entry exist. */
	for (i = 0; i < s->nfiles && r == MCV_OK; i++) {
		const McSaveFile *fl = &s->files[i];
		uint32_t n = (fl->size + CL - 1u) / CL, k;
		uint8_t *ent = dirbuf + (2u + i) * ENT;

		memcpy(ent, fl->entry, ENT);
		wr16(ent, rd16(fl->entry) | M_EXISTS | M_FILE);
		wr32(ent + 4, fl->size);
		wr32(ent + 0x10, C_END);
		wr32(ent + 0x14, 0);
		if (n == 0)
			continue;
		r = writerChain(&w, n, chain);
		for (k = 0; k < n && r == MCV_OK; k++) {
			uint32_t off = k * CL, len = fl->size - off > CL ? CL : fl->size - off;

			memset(buf, 0, CL);
			memcpy(buf, fl->data + off, len);
			r = writeCluster(&w, chain[k], buf);
		}
		wr32(ent + 0x10, chain[0]);
	}

	/* 2. The save's directory: "." points back at its root slot. */
	if (r == MCV_OK)
		r = writerChain(&w, dir_cl, chain);
	if (r == MCV_OK) {
		memcpy(dirbuf, s->dot, ENT);
		wr16(dirbuf, rd16(s->dot) | M_EXISTS | M_DIR);
		wr32(dirbuf + 0x10, 0);
		wr32(dirbuf + 0x14, slot);
		memcpy(dirbuf + 0x40, ".\0", 2);
		memcpy(dirbuf + ENT, s->dotdot, ENT);
		wr16(dirbuf + ENT, rd16(s->dotdot) | M_EXISTS | M_DIR);
		memcpy(dirbuf + ENT + 0x40, "..\0", 3);
		for (i = 0; i < dir_cl && r == MCV_OK; i++)
			r = writeCluster(&w, chain[i], dirbuf + i * CL);
	}

	/* 3. Its root entry. */
	memcpy(rootent, s->entry, ENT);
	wr16(rootent, rd16(s->entry) | M_EXISTS | M_DIR);
	wr32(rootent + 4, dir_n);
	wr32(rootent + 0x10, r == MCV_OK ? chain[0] : C_END);
	wr32(rootent + 0x14, 0);
	if (r == MCV_OK && grow) {
		r = writerAlloc(&w, &grow_c);
		if (r == MCV_OK) {
			memset(buf, 0, CL);
			memcpy(buf, rootent, ENT); /* unreachable until the root length grows */
			r = writeCluster(&w, grow_c, buf);
		}
		if (r == MCV_OK)
			r = writerFatSet(&w, last, 0x80000000u | grow_c);
	}

	/* 4. FAT. A failure from here on leaves lost clusters, not a broken save. */
	if (r == MCV_OK)
		r = writerFlush(&w);
	writerEnd(&w);
	free(chain);
	free(dirbuf);
	if (r != MCV_OK) {
		mcViewDropCaches(v);
		return r;
	}

	/* 5. Publish with one page. */
	if (slot < f.length) {
		r = writeEntry(&w, 0, slot, rootent);
	} else {
		if (!grow)
			r = writeEntry(&w, 0, slot, rootent);
		if (r == MCV_OK)
			r = mcViewReadClusterRel(v, 0, buf);
		if (r == MCV_OK) {
			wr32(buf + 4, f.length + 1u);
			r = writeAt(&w, (uint64_t)v->alloc_offset * CL, buf, ENT);
		}
	}
	mcViewDropCaches(v);
	if (r != MCV_OK)
		return r;

	/* 6. Read it back through a fresh view. */
	r = reopen(v);
	if (r != MCV_OK)
		return MCW_ERR_VERIFY;
	r = mcCardReadSave(v, name, &back);
	if (r != MCW_OK)
		return MCW_ERR_VERIFY;
	r = sameSave(s, &back) ? MCW_OK : MCW_ERR_VERIFY;
	mcCardFreeSave(&back);
	return r;
}

/* ---------------------------------------------------------------- deleting */

typedef struct Marks {
	uint8_t *bits;
	uint32_t n;
	uint32_t depth;
	int dup; /* a cluster was reached twice */
} Marks;

static int markChain(McView *v, Marks *m, uint32_t first)
{
	uint32_t c = first, steps = 0;
	int r;

	while (c != C_END) {
		if (c >= m->n || ++steps > m->n)
			return MCV_ERR_CORRUPT;
		if (m->bits[c / 8u] & (1u << (c % 8u)))
			m->dup = 1;
		m->bits[c / 8u] |= (uint8_t)(1u << (c % 8u));
		r = nextOf(v, c, &c);
		if (r != MCV_OK)
			return r;
	}
	return MCV_OK;
}

static int markDirCb(McView *v, uint32_t i, const uint8_t *raw, void *ctx);

static int markDir(McView *v, Marks *m, uint32_t first, uint32_t count)
{
	int r;

	if (count < 2 || count > MAX_ENTRIES || m->depth >= MAX_DEPTH)
		return MCV_ERR_CORRUPT;
	r = markChain(v, m, first);
	if (r != MCV_OK)
		return r;
	m->depth++;
	r = mcViewWalkDir(v, first, count, markDirCb, m);
	m->depth--;
	return r;
}

static int markDirCb(McView *v, uint32_t i, const uint8_t *raw, void *ctx)
{
	Marks *m = (Marks *)ctx;
	uint32_t mode = rd16(raw), first = rd32(raw + 0x10);

	if (i < 2 || !(mode & M_EXISTS))
		return MCV_OK;
	if (mode & M_DIR)
		return markDir(v, m, first, rd32(raw + 4));
	if (rd32(raw + 4) == 0 || first == C_END)
		return MCV_OK;
	return markChain(v, m, first);
}

typedef struct OthersCtx {
	Marks *m;
	int64_t skip;
} OthersCtx;

static int othersCb(McView *v, uint32_t i, const uint8_t *raw, void *ctx)
{
	OthersCtx *o = (OthersCtx *)ctx;

	if ((int64_t)i == o->skip)
		return MCV_OK;
	return markDirCb(v, i, raw, o->m);
}

int mcCardDeleteSave(McView *v, McWriteFn write, void *wctx, const char *name)
{
	RootFind f;
	Writer w;
	Marks mine, others;
	OthersCtx o;
	uint8_t ent[ENT];
	uint32_t c;
	int r;

	if (!v || !write || !name)
		return MCW_ERR_INVALID;
	if (v->page_stride != 512u)
		return MCW_ERR_UNSUPPORTED;
	r = rootFind(v, name, &f);
	if (r != MCV_OK)
		return r;
	if (f.found < 0 || !(rd16(f.entry) & M_DIR))
		return MCW_ERR_NOT_FOUND;

	/* Which clusters are this save's, and are any of them also used elsewhere? */
	memset(&mine, 0, sizeof(mine));
	memset(&others, 0, sizeof(others));
	mine.n = others.n = v->alloc_usable;
	mine.bits = (uint8_t *)calloc((v->alloc_usable + 7u) / 8u, 1);
	others.bits = (uint8_t *)calloc((v->alloc_usable + 7u) / 8u, 1);
	if (!mine.bits || !others.bits) {
		free(mine.bits);
		free(others.bits);
		return MCW_ERR_NOMEM;
	}
	r = markDir(v, &mine, rd32(f.entry + 0x10), rd32(f.entry + 4));
	if (r == MCV_OK && mine.dup)
		r = MCV_ERR_CORRUPT;
	if (r == MCV_OK)
		r = markChain(v, &others, 0); /* the root directory itself */
	if (r == MCV_OK) {
		o.m = &others;
		o.skip = f.found;
		others.depth = 1;
		r = mcViewWalkDir(v, 0, f.length, othersCb, &o);
	}
	for (c = 0; r == MCV_OK && c < v->alloc_usable; c++)
		if ((mine.bits[c / 8u] & others.bits[c / 8u]) & (1u << (c % 8u)))
			r = MCV_ERR_CORRUPT; /* cross-linked: freeing would damage another save */
	free(others.bits);
	if (r != MCV_OK) {
		free(mine.bits);
		return r;
	}

	memset(&w, 0, sizeof(w));
	w.v = v;
	w.write = write;
	w.ctx = wctx;

	/* 1. The save disappears with one page. */
	memcpy(ent, f.entry, ENT);
	wr16(ent, rd16(ent) & ~M_EXISTS);
	r = writeEntry(&w, 0, (uint32_t)f.found, ent);

	/* 2. Then its clusters are freed. A failure here leaves lost clusters only. */
	for (c = 0; r == MCV_OK && c < v->alloc_usable; c++)
		if (mine.bits[c / 8u] & (1u << (c % 8u)))
			r = writerFatSet(&w, c, C_FREE);
	free(mine.bits);
	if (r == MCV_OK)
		r = writerFlush(&w);
	writerEnd(&w);
	mcViewDropCaches(v);
	if (r != MCV_OK)
		return r;

	/* 3. Check. */
	r = reopen(v);
	if (r != MCV_OK)
		return MCW_ERR_VERIFY;
	r = rootFind(v, name, &f);
	if (r != MCV_OK || f.found >= 0)
		return MCW_ERR_VERIFY;
	return MCW_OK;
}
