//--------------------------------------------------------------
//File name:   mc_card_write.h
//Copies and deletes saves inside a PS2 memory card image (.mcd/.bin/.mc2
//without ECC). Pure C over read/write callbacks, host-testable like
//mc_card_view.c, which it uses for every read.
//
//Crash safety comes from the order of writes. Adding a save: plan and check
//free space; write the file data and the save's directory into free
//clusters; write the FAT; then publish with one 512-byte page (the save's
//root entry, or the root's entry count). Until that last page the card reads
//exactly as before, so an interrupted write leaves lost clusters at worst,
//never a half-written save. Deleting: clear the root entry's exists bit
//first (one page), then free the chains. Every change is read back.
//
//Never use this on the image the MMCE device is serving to the console.
//
//Written from Ross Ridge's format description and the ps2sdk mcman
//structure definitions; no GPL code was used.
//--------------------------------------------------------------
#ifndef MC_CARD_WRITE_H
#define MC_CARD_WRITE_H

#include <stdint.h>
#include "mc_card_view.h"

enum {
	MCW_OK = 0,
	/* MCV_ERR_* (-1..-5) pass through unchanged */
	MCW_ERR_EXISTS = -10,      /* a save with that name is already on the card */
	MCW_ERR_NOT_FOUND = -11,   /* no save with that name */
	MCW_ERR_FULL = -12,        /* not enough free clusters; nothing was written */
	MCW_ERR_UNSUPPORTED = -13, /* ECC image, or a save holding sub-folders */
	MCW_ERR_VERIFY = -14,      /* written, but the read-back differs */
	MCW_ERR_NOMEM = -15,
	MCW_ERR_INVALID = -16      /* bad name, duplicate file names, sizes */
};

typedef int (*McWriteFn)(void *ctx, uint64_t offset, const void *buf, uint32_t len); /* 0 = ok */

typedef struct McSaveFile {
	uint8_t entry[512]; /* the file's directory entry: mode, times, attributes, name */
	uint8_t *data;
	uint32_t size;
} McSaveFile;

typedef struct McSaveData {
	uint8_t entry[512];  /* the save folder's root entry */
	uint8_t dot[512];    /* its "." and ".." entries */
	uint8_t dotdot[512];
	uint32_t nfiles;
	McSaveFile *files;
} McSaveData;

/* Reads a whole save (a root folder of files) into memory. Free with mcCardFreeSave. */
int mcCardReadSave(McView *v, const char *name, McSaveData *out);
void mcCardFreeSave(McSaveData *s);

/* Clusters a save needs on a card, not counting a new root directory cluster. */
uint32_t mcCardSaveClusters(const McSaveData *s);

/* Adds the save to the card under its own name, then reads it back.
 * v must be open on a no-ECC image; it is reopened afterwards. */
int mcCardWriteSave(McView *v, McWriteFn write, void *wctx, const McSaveData *s);

/* Removes a save and frees its clusters, then checks it is gone. Refuses if
 * any of its clusters is also used elsewhere (a damaged card). */
int mcCardDeleteSave(McView *v, McWriteFn write, void *wctx, const char *name);

#endif
