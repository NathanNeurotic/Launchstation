#ifndef SAVE_ARCHIVE_H
#define SAVE_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

#define SAVE_ARCHIVE_LIMIT (16 * 1024 * 1024)
#define SAVE_ARCHIVE_FILES 1024
#define PS1_CARD_SIZE 131072
#define PS1_BLOCK_SIZE 8192

typedef struct {
	char name[33];
	uint16_t mode;
	uint8_t created[8], modified[8];
	const uint8_t *data;
	size_t size;
} SaveArchiveEntry;

/* Entry data borrows the input buffer until saveArchiveFree(). */
typedef struct {
	int ps1;
	SaveArchiveEntry folder;
	SaveArchiveEntry *files;
	unsigned count;
	uint8_t *decoded;
} SaveArchive;

int saveArchiveSupported(const char *name);
int saveArchiveParse(SaveArchive *save, uint8_t *data, size_t size, const char *filename);
void saveArchiveFree(SaveArchive *save);
enum { SAVE_OUTPUT_PSU, SAVE_OUTPUT_PSV, SAVE_OUTPUT_MCS, SAVE_OUTPUT_PSX };
/* Newly allocated output; caller owns it. Reject incompatible save/format pairs. */
int saveArchiveBuild(const SaveArchive *save, int format, uint8_t **output, size_t *length);
int saveArchiveFilename(const SaveArchive *save, int format, char *out, size_t size);
/* Reads/writes must complete the requested range or return 0. A NULL writer
 * validates metadata and measures output. Payload pointers in SaveArchive are
 * unused; storage remains owned by the caller. Writers support a final PSV
 * signature patch at offset 28. Only caller-owned transfer buffers hold data. */
typedef struct {
	int (*read)(void *context, unsigned file, size_t offset, void *data, size_t size);
	int (*write)(void *context, size_t offset, const void *data, size_t size);
	void *context;
	uint8_t *buffer, *buffer2;
	size_t buffer_size;
	void (*yield)(void *context); /* Optional drawing-only yield between hash blocks. */
} SaveArchiveStreamIo;
int saveArchiveStream(const SaveArchive *save, int format, const SaveArchiveStreamIo *io, size_t *length);
/* Locate a complete raw card inside raw, GME, VGS or VMP containers. */
int ps1CardOffset(const uint8_t *data, size_t size);
void ps1CardFormat(uint8_t *data);
int ps1CardAppend(uint8_t *card, const char *name, const uint8_t *data, size_t size);

#endif
