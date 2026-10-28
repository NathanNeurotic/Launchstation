//--------------------------------------------------------------
//File name:   mc_card_view.h
//Read-only viewer for PS2 memory card images (.mcd/.bin/.mc2 without
//ECC, .ps2 with ECC). Pure C over a read callback so it runs on the
//host in tests. Nothing here ever writes.
//
//Written from Ross Ridge's public format description and the ps2sdk
//mcman structure definitions; no GPL code was used.
//--------------------------------------------------------------
#ifndef MC_CARD_VIEW_H
#define MC_CARD_VIEW_H

#include <stdint.h>

enum {
	MCV_OK = 0,
	MCV_ERR_IO = -1,       /* a read failed */
	MCV_ERR_SIZE = -2,     /* size is not a known card image size */
	MCV_ERR_MAGIC = -3,    /* not a PS2 memory card */
	MCV_ERR_GEOMETRY = -4, /* superblock fields are inconsistent */
	MCV_ERR_CORRUPT = -5   /* FAT/directory structure is damaged */
};

typedef int (*McViewReadFn)(void *ctx, uint64_t offset, void *buf, uint32_t len); /* 0 = ok */

typedef struct McViewSource {
	McViewReadFn read;
	void *ctx;
	uint64_t size; /* total image bytes */
} McViewSource;

typedef struct McView {
	McViewSource src;
	uint32_t page_stride; /* 512 (no ECC) or 528 (ECC image) */
	uint32_t clusters_per_card;
	uint32_t alloc_offset;
	uint32_t alloc_usable; /* allocatable clusters, clamped by every bound the superblock gives */
	uint32_t ifc_list[32];
	uint8_t has_ecc;
	uint8_t cardtype;
	uint8_t cardflags;
	char version[13];
	uint32_t ifc_cache_cluster; /* one-cluster caches keep sequential FAT walks cheap */
	uint32_t ifc_cache[256];
	uint32_t fat_cache_cluster;
	uint32_t fat_cache[256];
} McView;

typedef struct McViewSave {
	char name[33];
	uint32_t files;       /* regular files inside */
	uint32_t bytes;       /* sum of their lengths */
	uint8_t has_icon_sys;
	uint8_t is_ps1;
	uint16_t title_break; /* byte offset where title line 2 starts */
	char title[69];       /* raw Shift-JIS bytes from icon.sys, NUL-terminated */
} McViewSave;

/* Detects the image variant, validates the superblock and the root directory. */
int mcViewOpen(McView *v, const McViewSource *src);

/* Lists the saves (top-level directories). Stores up to max entries and returns how many
 * were stored (>= 0), or a negative MCV_ERR_*. *total receives the number found. A save
 * whose own directory is damaged is still listed, with files = bytes = 0. */
int mcViewListSaves(McView *v, McViewSave *out, int max, int *total);

/* Counts free clusters (1024 bytes each). Free means FAT bit 31 clear. */
int mcViewFreeClusters(McView *v, uint32_t *free_clusters, uint32_t *total_clusters);

/* Fingerprint of the card's filesystem index: SHA-256 over the superblock and
 * every directory entry (mode, size, times, first cluster, name), walking every
 * folder. File contents are not read, so a 16 MiB card costs a few hundred KB
 * of reads. Two cards with the same fingerprint have the same files with the
 * same sizes, dates and locations; anything written through mcman changes it.
 * Not covered: writers that rewrite raw pages without touching the index, and
 * the contents of B?DATA-SYSTEM (the PS2 rewrites its history file on every game
 * start; only that the folder exists is hashed). Use a full compare when those
 * matter. ECC and no-ECC images of the same card give the same fingerprint.
 * Returns MCV_OK or a negative MCV_ERR_*; on error *out is undefined. */
#define MCV_FINGERPRINT_SIZE 32
int mcViewFingerprint(McView *v, uint8_t out[MCV_FINGERPRINT_SIZE]);

#endif
