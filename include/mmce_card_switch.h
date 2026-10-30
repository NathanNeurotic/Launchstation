#ifndef MMCE_CARD_SWITCH_H
#define MMCE_CARD_SWITCH_H

/* Which card image an MMCE device is serving, and a guarded switch.
 * See src/mmce_card_switch.c. */

enum {
	MMCE_SERVED_NO = 0,       /* confirmed by content: a different card is served */
	MMCE_SERVED_YES = 1,      /* same content as the card being served */
	MMCE_SERVED_SELECTED = 2, /* the device reports this card/channel as selected */
	MMCE_SERVED_UNKNOWN = 3   /* could not tell; treat as served */
};

enum {
	MMCE_SWITCH_OK = 0,
	MMCE_SWITCH_DEVICE = -1,       /* the device did not answer */
	MMCE_SWITCH_BUSY = -2,         /* still busy after 15 s */
	MMCE_SWITCH_UNKNOWN = -3,      /* could not confirm by content */
	MMCE_SWITCH_NOT_NUMBERED = -4, /* not MemoryCards/PS2/CardN/CardN-C.mcd */
	MMCE_SWITCH_NOT_CARD = -5,     /* the image could not be read as a PS2 card */
	MMCE_SWITCH_SPECIAL = -6       /* the device serves a BootCard, Game ID or named card
	                                  * (or one not confirmed by content); nothing sent */
};

typedef struct {
	int card;     /* GET_CARD, as the device reports it (may be stale; 0 for BootCard) */
	int channel;  /* GET_CHANNEL */
	int matches;  /* card images whose content matches the served card */
	int by_number;/* the reported number's image matched; no scan was needed */
	char path[1025];
} MmceActiveCard;

typedef void (*MmceScanProgress)(const char *path, int done);

/* -1 if not an mmce0:/mmce1: path, else the unit. */
int mmceCardUnit(const char *path);
/* 1 and the numbers for mmceN:/MemoryCards/PS2/CardN/CardN-C.mcd, else 0. */
int mmceCardParsePath(const char *path, int *card, int *channel);
/* MMCE_SERVED_*: anything but NO means do not write this image. Non-MMCE paths are NO. */
int mmceCardMaybeServed(const char *path);
/* 1 when the device reports this numbered card and channel and serves its
 * content; 0 otherwise (also for a different card with equal content). */
int mmceCardIsActive(const char *path);
/* MMCE_SWITCH_OK with out->path when exactly one image matches the served
 * content; MMCE_SWITCH_UNKNOWN with out->matches 0 or > 1 otherwise. */
int mmceActiveCard(int unit, MmceActiveCard *out, MmceScanProgress progress);
/* Selects CardN, channel C, one command at a time, each confirmed by the
 * device's own report; succeeds only when number, channel and content all
 * agree afterwards. Refuses (MMCE_SWITCH_SPECIAL) unless the device serves a
 * numbered card confirmed by content: MMCEMAN cannot leave a BootCard or Game
 * ID card by number, and a named card's number is not a CardN. */
int mmceSwitchToCard(const char *path);

#endif
