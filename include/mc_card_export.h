#ifndef MC_CARD_EXPORT_H
#define MC_CARD_EXPORT_H

#include <tamtypes.h>

enum {
	MC_CARD_EXPORT_OK = 0,
	MC_CARD_EXPORT_OPEN = -1,
	MC_CARD_EXPORT_FORMAT = -2,
	MC_CARD_EXPORT_IO = -3,
	MC_CARD_EXPORT_VERIFY_FAILED = -4,
	MC_CARD_EXPORT_SAME_PATH = -5
};

enum {
	MC_CARD_EXPORT_WRITE = 0,
	MC_CARD_EXPORT_VERIFY = 1
};

typedef void (*McCardExportProgress)(int phase, u64 done, u64 total);

/* Copy a valid no-ECC .mcd image and verify the completed destination
 * byte-for-byte. Source and destination are never open at the same time. */
int mcCardBackupNoEcc(const char *source, const char *dest, McCardExportProgress progress);

/* Convert a no-ECC .mcd image to PCSX2's 528-byte/page .ps2 format and
 * verify the completed output byte-for-byte. */
int mcCardExportPcsx2(const char *source, const char *dest, McCardExportProgress progress);

/* Strip PCSX2's 16-byte spare/ECC area from every 512-byte page and verify
 * the resulting no-ECC .mcd image. */
int mcCardImportPcsx2(const char *source, const char *dest, McCardExportProgress progress);

/* Exposed for deterministic host tests. */
void mcCardPcsx2Page(const u8 in[512], u8 out[528]);

#endif
