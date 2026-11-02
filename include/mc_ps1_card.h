//--------------------------------------------------------------
//File name:   mc_ps1_card.h
//PS1 memory card images (128 KB: 16 blocks of 8 KB, block 0 holds the
//header frame and one 128-byte directory frame per data block) and the
//single-save files other tools export for them. Everything works on the
//whole image in memory; nothing here touches a file.
//
//Card layout (as the PS1 BIOS writes it, and as MemcardRex documents it):
//  frame 0        "MC", zeros, XOR checksum in byte 127
//  frames 1..15   one directory frame per block 1..15:
//                   0x00 u32 state: 0x51 first block of a save, 0x52 middle,
//                        0x53 last, 0xA0 free, 0xA1..0xA3 deleted (free)
//                   0x04 u32 save size in bytes (first block only)
//                   0x08 u16 next block (0..14) or 0xFFFF
//                   0x0A save name, up to 20 ASCII bytes, NUL padded
//                   0x7F XOR of bytes 0x00..0x7E
//  frames 16..35  spare-sector list, unused here (0xFF state, no link)
//  frame 63       copy of frame 0
//  block n        (8 KB at n * 8192) data of directory frame n; a save's
//                 first block starts with "SC", an icon flag, the block
//                 count and a 64-byte Shift-JIS title.
//
//Single-save files read by ps1SaveFromFile:
//  .mcs .ps1 (and .mcb .pda with the same header)
//               the save's first directory frame, then its blocks
//  .psv         PS3 export of a PS1 save: "\0VSP", save type 1 at 0x3C, name
//               at 0x64, blocks from 0x84 (PS2 .psv saves are refused)
//  .psx         Action Replay / GameShark: 54-byte header (name in bytes
//               0..19), then the blocks
//  raw          the blocks alone; the save name is the file's name
//
//Layouts follow the PS1 BIOS memory card format as documented by
//ShendoXT's MemcardRex (ps1card.cs); the code here is new. A file whose
//sizes do not fit is refused, never partly imported.
//--------------------------------------------------------------
#ifndef MC_PS1_CARD_H
#define MC_PS1_CARD_H

#include <stdint.h>

#define PS1_CARD_SIZE 131072u
#define PS1_BLOCK 8192u
#define PS1_BLOCKS 15 /* data blocks; block 0 is the directory */
#define PS1_NAME_MAX 20

enum Ps1Result {
	PS1_OK = 0,
	PS1_ERR_NOT_CARD = -1,   /* not a 128 KB PS1 card image */
	PS1_ERR_INVALID = -2,    /* a save file whose sizes or header do not fit */
	PS1_ERR_UNSUPPORTED = -3, /* recognised, but not a PS1 save (a PS2 .psv) */
	PS1_ERR_FULL = -4,       /* not enough free blocks */
	PS1_ERR_EXISTS = -5,     /* a save with that name is already on the card */
	PS1_ERR_DAMAGED = -6,    /* a save whose blocks do not add up (see damaged) */
	PS1_ERR_NOMEM = -7,
	PS1_ERR_NAME = -8        /* a save name the BIOS would not accept */
};

enum Ps1SaveFormat {
	PS1F_UNKNOWN = 0,
	PS1F_MCS,
	PS1F_PSV,
	PS1F_PSX,
	PS1F_RAW
};

typedef struct {
	int first;              /* directory frame / block number, 1..15 */
	int blocks;             /* blocks the save uses (from its links) */
	int damaged;            /* a link broken or shared with another save, or a size
	                         * field that is not blocks * 8 KB: shown, never read
	                         * or written */
	char name[PS1_NAME_MAX + 1];
	unsigned char title[65]; /* Shift-JIS title from the first block, NUL ended */
} Ps1CardSave;

/* A single save: its name and blocks * 8 KB of data (free with ps1SaveFree). */
typedef struct {
	char name[PS1_NAME_MAX + 1];
	int blocks;
	uint8_t *data;
} Ps1Save;

/* 1 when buf/len is a raw PS1 card image ("MC" frame, 128 KB). */
int ps1CardIs(const uint8_t *buf, uint32_t len);

/* An empty, formatted card in card (PS1_CARD_SIZE bytes). */
void ps1CardFormat(uint8_t *card);

/* Free blocks (free and deleted frames). */
int ps1CardFreeBlocks(const uint8_t *card);

/* Lists the saves on the card in directory order; returns the count (0..15). */
int ps1CardList(const uint8_t *card, Ps1CardSave *out, int max);

/* Copies one save off the card (first = its first block). */
int ps1CardRead(const uint8_t *card, int first, Ps1Save *out);

/* Writes a save into free blocks, lowest first; nothing else on the card
 * changes. Refuses a name already on the card and a card without room. */
int ps1CardWrite(uint8_t *card, const Ps1Save *s);

/* Marks a save deleted the way the BIOS does (0x51/0x52/0x53 become
 * 0xA1/0xA2/0xA3), which frees its blocks. */
int ps1CardDelete(uint8_t *card, int first);

/* 1 when the save name is 1..20 printable ASCII bytes. */
int ps1NameOk(const char *name);

/* Reads a single-save file of any format above. name_hint is the file's
 * name without folder (used for a raw save), or NULL. *format receives the
 * Ps1SaveFormat read. */
int ps1SaveFromFile(const uint8_t *buf, uint32_t len, const char *name_hint, Ps1Save *out, int *format);

/* A save as .mcs bytes (directory frame + blocks); free *out with free().
 * PS1_ERR_NAME when the name is not 1 to 20 plain ASCII characters. */
int ps1SaveToMcs(const Ps1Save *s, uint8_t **out, uint32_t *len);

void ps1SaveFree(Ps1Save *s);

/* Short names for labels. */
const char *ps1SaveFormatName(int format);
const char *ps1Error(int r);

#endif
