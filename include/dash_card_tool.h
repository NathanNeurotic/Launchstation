#ifndef DASH_CARD_TOOL_H
#define DASH_CARD_TOOL_H

/* "Card contents..." for one memory card image: lists its saves and copies,
 * moves, deletes, exports and imports (.psu) them. See src/dash_card_tool.c. */
void dashCardTool(const char *image);

/* "New blank card...": an empty 8 to 64 MB .mcd in folder (on MMCE, in
 * mmceN:/WLE-New/ until it is installed). Never writes an existing file. */
void dashCardNewBlank(const char *folder);

/* MMCE: show which card image the device serves (confirmed by content), and
 * switch to a numbered card with confirmation (src/mmce_card_switch.c). */
void dashMmceShowActive(int unit);
void dashMmceSwitch(const char *path);

#endif
