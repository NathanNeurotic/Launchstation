//--------------------------------------------------------------
//File name:   mc_card_view.c
//Read-only PS2 memory card image viewer. See include/mc_card_view.h.
//
//Format facts (Ross Ridge's description, ps2sdk mcman, cross-checked with
//the public-domain mymc format code): little endian; 512-byte pages, two
//per 1024-byte cluster; directory and FAT cluster numbers are relative to
//alloc_offset while ifc_list and the cluster numbers stored in indirect
//clusters are absolute; a FAT entry is free when bit 31 is clear, otherwise
//the low 31 bits are the next cluster and 0xFFFFFFFF ends the chain.
//Every walk is bounded and every read is checked: images may be damaged.
//--------------------------------------------------------------
#include <string.h>
#include "mc_card_view.h"

#define MCV_CLUSTER 1024u
#define MCV_PAGE 512u
#define MCV_ENT 512u
#define MCV_FAT_PER_CLUSTER 256u
#define MCV_MAX_SAVE_ENTRIES 4096u
#define MCV_MODE_EXISTS 0x8000u
#define MCV_MODE_DIR 0x0020u
#define MCV_MODE_FILE 0x0010u
#define MCV_MODE_PS1 0x1000u
#define MCV_ICON_SYS_MIN 260u
#define MCV_END 0xFFFFFFFFu

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int readPage(McView *v, uint64_t page, uint8_t *buf)
{
	if (v->src.read(v->src.ctx, page * v->page_stride, buf, MCV_PAGE) != 0)
		return MCV_ERR_IO;
	return MCV_OK;
}

static int readCluster(McView *v, uint32_t abs_cluster, uint8_t *buf)
{
	int r;

	if (abs_cluster >= v->clusters_per_card)
		return MCV_ERR_CORRUPT;
	r = readPage(v, (uint64_t)abs_cluster * 2u, buf);
	if (r != MCV_OK)
		return r;
	return readPage(v, (uint64_t)abs_cluster * 2u + 1u, buf + MCV_PAGE);
}

static int loadCluster32(McView *v, uint32_t abs_cluster, uint32_t *cache, uint32_t *cached_at)
{
	uint8_t raw[MCV_CLUSTER];
	unsigned i;
	int r;

	if (*cached_at == abs_cluster)
		return MCV_OK;
	r = readCluster(v, abs_cluster, raw);
	if (r != MCV_OK)
		return r;
	for (i = 0; i < MCV_FAT_PER_CLUSTER; i++)
		cache[i] = rd32(raw + i * 4u);
	*cached_at = abs_cluster;
	return MCV_OK;
}

/* FAT entry for allocatable cluster idx (relative to alloc_offset). */
static int fatGet(McView *v, uint32_t idx, uint32_t *val)
{
	uint32_t ind_index = idx / MCV_FAT_PER_CLUSTER;
	uint32_t dbl = ind_index / MCV_FAT_PER_CLUSTER;
	uint32_t ifc_cluster, fat_cluster;
	int r;

	if (dbl >= 32u)
		return MCV_ERR_CORRUPT;
	ifc_cluster = v->ifc_list[dbl];
	if (ifc_cluster == 0 || ifc_cluster >= v->clusters_per_card)
		return MCV_ERR_CORRUPT;
	r = loadCluster32(v, ifc_cluster, v->ifc_cache, &v->ifc_cache_cluster);
	if (r != MCV_OK)
		return r;
	fat_cluster = v->ifc_cache[ind_index % MCV_FAT_PER_CLUSTER];
	if (fat_cluster == 0 || fat_cluster >= v->clusters_per_card)
		return MCV_ERR_CORRUPT;
	r = loadCluster32(v, fat_cluster, v->fat_cache, &v->fat_cache_cluster);
	if (r != MCV_OK)
		return r;
	*val = v->fat_cache[idx % MCV_FAT_PER_CLUSTER];
	return MCV_OK;
}

/* Next cluster in a chain: *next = MCV_END at the end. */
static int chainNext(McView *v, uint32_t idx, uint32_t *next)
{
	uint32_t val;
	int r = fatGet(v, idx, &val);

	if (r != MCV_OK)
		return r;
	if (!(val & 0x80000000u))
		return MCV_ERR_CORRUPT; /* a chain must not run into a free cluster */
	if (val == MCV_END) {
		*next = MCV_END;
		return MCV_OK;
	}
	val &= 0x7FFFFFFFu;
	if (val >= v->alloc_usable)
		return MCV_ERR_CORRUPT;
	*next = val;
	return MCV_OK;
}

/* Reads len bytes at byte offset 'offset' of the chain starting at 'first'. */
static int chainRead(McView *v, uint32_t first, uint32_t offset, void *dst, uint32_t len)
{
	uint8_t raw[MCV_CLUSTER];
	uint8_t *out = (uint8_t *)dst;
	uint32_t idx = first;
	uint32_t skip = offset / MCV_CLUSTER;
	uint32_t in_cluster = offset % MCV_CLUSTER;
	uint32_t steps = 0, n;
	int r;

	while (len > 0) {
		if (idx == MCV_END || idx >= v->alloc_usable || steps > v->alloc_usable)
			return MCV_ERR_CORRUPT;
		if (skip == 0) {
			r = readCluster(v, v->alloc_offset + idx, raw);
			if (r != MCV_OK)
				return r;
			n = MCV_CLUSTER - in_cluster;
			if (n > len)
				n = len;
			memcpy(out, raw + in_cluster, n);
			out += n;
			len -= n;
			in_cluster = 0;
		} else {
			skip--;
		}
		if (len == 0)
			break;
		r = chainNext(v, idx, &idx);
		if (r != MCV_OK)
			return r;
		steps++;
	}
	return MCV_OK;
}

typedef struct DirEnt {
	uint32_t mode;
	uint32_t length;
	uint32_t cluster;
	char name[33];
} DirEnt;

static int dirEntry(McView *v, uint32_t first, uint32_t i, DirEnt *e)
{
	uint8_t raw[MCV_ENT];
	int r = chainRead(v, first, i * MCV_ENT, raw, MCV_ENT);

	if (r != MCV_OK)
		return r;
	e->mode = rd16(raw);
	e->length = rd32(raw + 4);
	e->cluster = rd32(raw + 0x10);
	memcpy(e->name, raw + 0x40, 32);
	e->name[32] = 0;
	return MCV_OK;
}

static int nameIs(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
		char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

int mcViewOpen(McView *v, const McViewSource *src)
{
	static const char magic[] = "Sony PS2 Memory Card Format ";
	uint8_t sb[MCV_PAGE];
	uint64_t pages;
	uint32_t n, usable, bb2, ppb, i;
	DirEnt e0, e1;
	int r;

	if (!v || !src || !src->read)
		return MCV_ERR_IO;
	memset(v, 0, sizeof(*v));
	v->src = *src;
	v->ifc_cache_cluster = MCV_END;
	v->fat_cache_cluster = MCV_END;

	/* variant from size: N MiB raw, or N x 1,081,344 with 16 spare bytes per page */
	v->page_stride = 0;
	for (n = 1; n <= 128; n <<= 1) {
		if (src->size == (uint64_t)n * 1048576ull) {
			v->page_stride = MCV_PAGE;
			break;
		}
		if (src->size == (uint64_t)n * 1081344ull) {
			v->page_stride = 528;
			v->has_ecc = 1;
			break;
		}
	}
	if (!v->page_stride)
		return MCV_ERR_SIZE;
	pages = src->size / v->page_stride;

	r = readPage(v, 0, sb);
	if (r != MCV_OK)
		return r;
	if (memcmp(sb, magic, 28) != 0)
		return MCV_ERR_MAGIC;
	memcpy(v->version, sb + 0x1C, 12);
	v->version[12] = 0;
	if (rd16(sb + 0x28) != MCV_PAGE || rd16(sb + 0x2A) != 2u)
		return MCV_ERR_GEOMETRY; /* only 512-byte pages, 1024-byte clusters */
	ppb = rd16(sb + 0x2C);
	if (ppb == 0 || (ppb & 1u))
		return MCV_ERR_GEOMETRY;
	v->clusters_per_card = rd32(sb + 0x30);
	if ((uint64_t)v->clusters_per_card * 2u != pages)
		return MCV_ERR_GEOMETRY;
	v->alloc_offset = rd32(sb + 0x34);
	if (v->alloc_offset == 0 || v->alloc_offset >= v->clusters_per_card)
		return MCV_ERR_GEOMETRY;
	if (rd32(sb + 0x3C) != 0)
		return MCV_ERR_GEOMETRY; /* root directory must start at relative cluster 0 */
	for (i = 0; i < 32; i++)
		v->ifc_list[i] = rd32(sb + 0x50 + i * 4u);
	v->cardtype = sb[0x150];
	v->cardflags = sb[0x151];

	/* allocatable range: never trust a single field */
	usable = v->clusters_per_card - v->alloc_offset;
	if (rd32(sb + 0x38) != 0 && rd32(sb + 0x38) < usable)
		usable = rd32(sb + 0x38);
	bb2 = rd32(sb + 0x44); /* backup erase block: the data area ends before it */
	if (bb2 != 0 && bb2 < 0x40000000u / 16u) {
		uint64_t limit = (uint64_t)bb2 * (ppb / 2u);

		if (limit > v->alloc_offset && limit - v->alloc_offset < usable)
			usable = (uint32_t)(limit - v->alloc_offset);
	}
	if (usable == 0)
		return MCV_ERR_GEOMETRY;
	v->alloc_usable = usable;

	/* the FAT must be reachable; cluster 0 holds the root directory, so its entry is allocated */
	{
		uint32_t fat0;

		r = fatGet(v, 0, &fat0);
		if (r != MCV_OK)
			return r == MCV_ERR_IO ? r : MCV_ERR_CORRUPT;
		if (!(fat0 & 0x80000000u))
			return MCV_ERR_CORRUPT;
	}

	/* a formatted card starts its root directory with "." and ".." */
	r = dirEntry(v, 0, 0, &e0);
	if (r != MCV_OK)
		return r == MCV_ERR_IO ? r : MCV_ERR_CORRUPT;
	r = dirEntry(v, 0, 1, &e1);
	if (r != MCV_OK)
		return r == MCV_ERR_IO ? r : MCV_ERR_CORRUPT;
	if (strcmp(e0.name, ".") != 0 || strcmp(e1.name, "..") != 0 || e0.length < 2u)
		return MCV_ERR_CORRUPT;
	return MCV_OK;
}

/* Fills files/bytes/title for one save directory; leaves zeros if it is damaged. */
static void describeSave(McView *v, uint32_t first, uint32_t count, McViewSave *s)
{
	uint32_t i;
	DirEnt e;

	if (count > MCV_MAX_SAVE_ENTRIES)
		return;
	for (i = 2; i < count; i++) {
		if (dirEntry(v, first, i, &e) != MCV_OK)
			return;
		if (!(e.mode & MCV_MODE_EXISTS) || !(e.mode & MCV_MODE_FILE))
			continue;
		s->files++;
		if (s->bytes > 0xFFFFFFFFu - e.length)
			s->bytes = 0xFFFFFFFFu;
		else
			s->bytes += e.length;
		if (!s->has_icon_sys && nameIs(e.name, "icon.sys") && e.length >= MCV_ICON_SYS_MIN &&
		    e.cluster != MCV_END) {
			uint8_t hdr[MCV_ICON_SYS_MIN];

			if (chainRead(v, e.cluster, 0, hdr, sizeof(hdr)) == MCV_OK && !memcmp(hdr, "PS2D", 4)) {
				s->has_icon_sys = 1;
				s->title_break = (uint16_t)rd16(hdr + 6);
				memcpy(s->title, hdr + 192, 68);
				s->title[68] = 0;
				if (s->title_break > 68u)
					s->title_break = 0;
			}
		}
	}
}

int mcViewListSaves(McView *v, McViewSave *out, int max, int *total)
{
	DirEnt root, e;
	uint32_t i;
	int stored = 0, found = 0, r;

	if (!v || !total || max < 0 || (max > 0 && !out))
		return MCV_ERR_IO;
	r = dirEntry(v, 0, 0, &root);
	if (r != MCV_OK)
		return r;
	if (root.length > MCV_MAX_SAVE_ENTRIES)
		return MCV_ERR_CORRUPT;
	for (i = 2; i < root.length; i++) {
		McViewSave tmp;

		r = dirEntry(v, 0, i, &e);
		if (r == MCV_ERR_IO)
			return r;
		if (r != MCV_OK)
			break; /* the root chain is shorter than its length claims */
		if (!(e.mode & MCV_MODE_EXISTS) || !(e.mode & MCV_MODE_DIR))
			continue;
		if (e.name[0] == 0)
			continue;
		found++;
		if (stored >= max)
			continue;
		memset(&tmp, 0, sizeof(tmp));
		memcpy(tmp.name, e.name, 32);
		tmp.name[32] = 0;
		tmp.is_ps1 = (e.mode & MCV_MODE_PS1) ? 1 : 0;
		/* the entry's length is the entry count of the save directory, "." and ".." included */
		if (e.cluster < v->alloc_usable)
			describeSave(v, e.cluster, e.length, &tmp);
		out[stored++] = tmp;
	}
	*total = found;
	return stored;
}

int mcViewFreeClusters(McView *v, uint32_t *free_clusters, uint32_t *total_clusters)
{
	uint32_t i, val, freec = 0;
	int r;

	if (!v || !free_clusters || !total_clusters)
		return MCV_ERR_IO;
	for (i = 0; i < v->alloc_usable; i++) {
		r = fatGet(v, i, &val);
		if (r != MCV_OK)
			return r;
		if (!(val & 0x80000000u))
			freec++;
	}
	*free_clusters = freec;
	*total_clusters = v->alloc_usable;
	return MCV_OK;
}
