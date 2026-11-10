//--------------------------------------------------------------
//File name:   mc_save_export.h
//Writes saves and PS1 cards in the formats other consoles and tools use,
//and opens the PS1 card containers below. Everything works on buffers in
//memory; nothing here touches a file.
//
//  .psv   PS3 export of a save, signed so a PS3 accepts it:
//           0x00 "\0VSP"   0x08 20-byte seed   0x1C 20-byte HMAC-SHA1
//           0x38 0x14 (PS1) or 0x2C (PS2)      0x3C save type, 1 or 2
//         PS1: 0x40 save size, 0x44 0x84 (data offset), 0x48 0x200,
//              0x5C save size, 0x60 0x9003, 0x64 name (20), data from 0x84
//         PS2: 0x40 bytes of file data, 0x44/0x48 icon.sys offset/size,
//              0x4C..0x63 the three icons' offset/size, 0x64 file count,
//              0x68 created, 0x70 modified, 0x78 count + 2, 0x7C mode,
//              0x80 folder name (32), then one 60-byte record per file
//              (created, modified, size, mode, name (32), offset) and the data.
//         The signature key comes from the seed through AES-128 with the
//         PS3's PS1 or PS2 key; the HMAC covers the whole file with the
//         signature field zeroed.
//  .psx   Action Replay / GameShark: 54-byte header (name in 0..19), blocks.
//  raw    the blocks alone, named after the save.
//
//PS1 card containers (each wraps the 128 KB raw card):
//  .gme   DexDrive: 3904-byte header ("123-456-STD"; bytes 0x16.. and 0x26..
//         repeat each directory frame's state and next-block bytes)
//  .vgs .mem  Connectix VGS: 64-byte header ("VgsM")
//  .vmp   PSP / Vita: 128-byte header ("\0PMV", 0x80), signed like a PS1
//         .psv with the seed at 0x0C and the signature at 0x20
//  .mcx   PocketStation for iOS: 128-byte header, the card, a SHA-256 of
//         both, all AES-128-CBC encrypted
//
//Format facts as documented by bucanero's Apollo Save Tool and
//psv-save-converter and used by R3Z3N's wLaunchELF_R3Z; the code here is new.
//AES and HMAC-SHA1 are Mbed TLS, which Launchstation already links for cloud
//TLS; SHA-256 is mc_sha256.c.
//--------------------------------------------------------------
#ifndef MC_SAVE_EXPORT_H
#define MC_SAVE_EXPORT_H

#include <stdint.h>
#include "mc_card_write.h"
#include "mc_ps1_card.h"

enum Ps1CardKind {
	PS1C_RAW = 0, /* .mcd .mcr .vm1 .vmc .bin and POPStarter / Ember cards */
	PS1C_GME,
	PS1C_VGS,
	PS1C_VMP,
	PS1C_MCX
};

#define PS1_CARD_HEAD_MAX 3904u /* the largest container header (.gme) */
#define PS1_CARD_FILE_MAX (PS1_CARD_HEAD_MAX + PS1_CARD_SIZE)

/* The container a file of len bytes is, by its size and magic (an .mcx has
 * none: its size alone, and ps1CardOpen checks its hash). Reads only the
 * first 16 bytes of file. -1 when none fits. */
int ps1CardKind(const uint8_t *file, uint32_t len);

/* Opens a container in place: an .mcx is decrypted and its SHA-256 checked.
 * Returns the offset of the raw card in file, or -1 when it does not open or
 * holds no PS1 card. The header stays in file[0 .. offset). */
int ps1CardOpen(uint8_t *file, uint32_t len, int kind);

/* The file size of a container, and its header size. */
uint32_t ps1CardFileSize(int kind);
uint32_t ps1CardHeadSize(int kind);

/* Wraps a raw card into out (ps1CardFileSize(kind) bytes). head is the
 * header the card came with (ps1CardHeadSize bytes) or NULL for a new one;
 * the .gme directory copy, the .vmp signature and the .mcx hash and
 * encryption are always made fresh. 0, or -1. */
int ps1CardSeal(int kind, const uint8_t *raw, const uint8_t *head, uint8_t *out);

/* Lowercase extension and a label for a container. */
const char *ps1CardKindExt(int kind);
const char *ps1CardKindName(int kind);

/* A PS1 save as a signed .psv or as .psx; free *out with free(). */
int ps1SaveToPsv(const Ps1Save *s, uint8_t **out, uint32_t *len);
int ps1SaveToPsx(const Ps1Save *s, uint8_t **out, uint32_t *len);

/* A PS2 save as a signed .psv; free *out with free(). MCW_OK or an MCW_ERR. */
int mcSaveToPsv(const McSaveData *s, uint8_t **out, uint32_t *len);

/* A PS2 save as a .zip of its folder and files, stored (uncompressed), for a
 * PC: the folder's and files' times become DOS times; the card attributes are
 * not kept. Free *out with free(). MCW_OK or an MCW_ERR. */
int mcSaveToZip(const McSaveData *s, uint8_t **out, uint32_t *len);

/* The file name a PS3 looks for: the first 12 characters of the save name,
 * the rest as hex, then ".PSV". out needs 2 * 32 + 5 bytes. */
void mcPsvFileName(char *out, uint32_t size, const char *name);

/* Signs a .psv (PS1 or PS2, by its save type) or a .vmp in place. 0, or -1. */
int mcPsvSign(uint8_t *buf, uint32_t len);
int ps1VmpSign(uint8_t *buf, uint32_t len);

#endif
