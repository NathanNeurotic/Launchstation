#ifndef DASH_CARD_TOOL_H
#define DASH_CARD_TOOL_H

#include <stdint.h>

/* "Card contents..." for one memory card image: lists its saves and copies,
 * moves, deletes, exports (.psu) and imports (.psu and the formats in
 * mc_save_formats.h) them. See src/dash_card_tool.c. */
void dashCardTool(const char *image);

/* "New blank card...": an empty 8 to 64 MB .mcd in folder (on MMCE, in
 * mmceN:/WLE-New/ until it is installed), or a 128 KB PS1 card. Never writes
 * an existing file. */
void dashCardNewBlank(const char *folder);

/* PS1 card images (src/dash_ps1_card.c): 1 when path is a 128 KB image
 * starting with "MC"; the PS1 "Card contents..." (import .mcs/.psv/.psx/raw,
 * export .mcs, delete); and a blank 128 KB card in folder. dashCardTool
 * opens PS1 images here by itself. */
int dashPs1CardIsImage(const char *path);
void dashPs1CardTool(const char *image);
void dashPs1CardNewBlank(const char *folder);

/* Shared with dash_ps1_card.c: a verified safety copy of an image whose
 * current bytes are in buf, and a new file written and read back (both 0 on
 * success). Both only ever create a new file (O_EXCL): a file that appears at
 * that name after the caller checked it is left alone and the call fails. */
int dashCardSafetyCopyBuffer(const char *image, const uint8_t *buf, uint32_t len, char *made, int made_size);
int dashCardWriteVerified(const char *path, const uint8_t *buf, uint32_t len);

/* "All saves here...": the saves of every card image in folder and the
 * folders directly inside it, sorted by title, each opening the same save
 * actions as "Card contents..." on its own card. Only reads until one is used. */
void dashCardAllSaves(const char *folder);

/* "Save library here...": the .psu saves in folder and the folders directly
 * inside it, sorted by title. Copies them to a card (one verified safety copy
 * of that card, nothing on it replaced), saves marked ones as a named set (a
 * subfolder), applies a set to a card, or deletes .psu files. */
void dashCardSaveLibrary(const char *folder);

/* "Convert to .psu": a save exported by another tool (Blaze .mcm, MAX Drive
 * .max, CodeBreaker .cbs, SharkPort/X-Port .sps/.xps, PS3 .psv) written as a
 * new .psu beside it, read back and checked. The source is only read. */
void dashCardConvertSave(const char *path);

/* MMCE: show which card image the device serves (confirmed by content), and
 * switch to a numbered card with confirmation (src/mmce_card_switch.c). */
void dashMmceShowActive(int unit);
void dashMmceSwitch(const char *path);

#endif
