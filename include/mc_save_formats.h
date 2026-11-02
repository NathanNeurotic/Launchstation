//--------------------------------------------------------------
//File name:   mc_save_formats.h
//Reads single PS2 saves in the formats other tools export and turns each
//into the McSaveData that mc_card_write.c copies onto a card image or
//writes out as a .psu:
//
//  .psu       EMS / uLaunchELF: the save's directory entries and data as on
//             the card (mc_card_write.c reads these; here only detected).
//  .mcm       Blaze Memory Maxx: 80-byte header, zlib stream of
//             [32-byte name][u32 size][data] records; no attributes or
//             times, so defaults are made up. The save folder's name is the
//             file's name without its extension.
//  .max       Action Replay / GameShark MAX Drive: "Ps2PowerSave" header,
//             LZARI-compressed [u32 size][32-byte name][data] records padded
//             so each record ends 8 bytes before a 16-byte boundary; the
//             header names the folder; no times.
//  .cbs       CodeBreaker: "CFU" header with the folder name, times and
//             mode, then an RC4-scrambled zlib stream of 64-byte entries
//             (times, size, mode, name) each followed by its data.
//  .sps .xps  SharkPort / X-Port: "SharkPortSave" header, three
//             length-prefixed strings, then 98-byte (or longer) entries with
//             byte-swapped modes and times, each followed by its data.
//  .psv       PS3 export of a PS2 save: 64-byte signed header, icon table,
//             the folder entry with times and mode, then one 60-byte record
//             per file (times, size, mode, name, offset).
//
//Layouts follow Ross Ridge's public-domain mymc (ps2save.py, lzari.py) and
//the PSV notes in bucanero's psv-save-converter; the code here is new.
//Every field read is bounds-checked; a file that does not fit its own
//declared sizes is refused with MCW_ERR_INVALID, never partly imported.
//--------------------------------------------------------------
#ifndef MC_SAVE_FORMATS_H
#define MC_SAVE_FORMATS_H

#include <stdint.h>
#include "mc_card_write.h"

enum McSaveFormat {
	MCSF_UNKNOWN = 0,
	MCSF_PSU,
	MCSF_MCM,
	MCSF_MAX,
	MCSF_CBS,
	MCSF_SPS,
	MCSF_PSV
};

/* The format of a whole file in memory. ext_hint is the file's extension
 * (any case, with or without the dot) or NULL; it decides .mcm, which has no
 * magic number, and breaks ties for a .psu whose header happens to look like
 * something else. */
int mcSaveFormatDetect(const uint8_t *buf, uint32_t len, const char *ext_hint);

/* Lowercase extension and a short name for a format, for labels. */
const char *mcSaveFormatExt(int format);
const char *mcSaveFormatName(int format);

/* 1 when the lowercase extension (with or without the dot) names a format
 * this file can read, .psu included. */
int mcSaveFormatKnownExt(const char *ext);

/* Reads a save of any known format into out (free with mcCardFreeSave).
 * name_hint is the file's base name without extension (for .mcm, which does
 * not store the folder name) or NULL. stamp is the console time in card
 * order (see mcCardBlankIdentity), or NULL for zero, used where the format
 * stores no times. *format receives the McSaveFormat read, or MCSF_UNKNOWN.
 * Returns MCW_OK, MCW_ERR_INVALID, MCW_ERR_UNSUPPORTED (a format that is
 * recognised but cannot be read, such as a PS1 .psv) or MCW_ERR_NOMEM. */
int mcSaveFormatRead(const uint8_t *buf, uint32_t len, const char *ext_hint, const char *name_hint,
                     const uint8_t stamp[8], McSaveData *out, int *format);

/* The individual readers behind mcSaveFormatRead. */
int mcSaveFromMcm(const uint8_t *buf, uint32_t len, const char *folder_name, const uint8_t stamp[8], McSaveData *out);
int mcSaveFromMax(const uint8_t *buf, uint32_t len, const uint8_t stamp[8], McSaveData *out);
int mcSaveFromCbs(const uint8_t *buf, uint32_t len, const uint8_t stamp[8], McSaveData *out);
int mcSaveFromSps(const uint8_t *buf, uint32_t len, McSaveData *out);
int mcSaveFromPsv(const uint8_t *buf, uint32_t len, McSaveData *out);

/* Okumura's LZARI, as MAX Drive saves use it: decodes src into out_len bytes
 * of out. Returns 0, or -1 when the stream runs out or is malformed. */
int mcLzariDecode(const uint8_t *src, uint32_t src_len, uint8_t *out, uint32_t out_len);

#endif
