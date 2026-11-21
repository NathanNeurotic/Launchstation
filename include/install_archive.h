#ifndef INSTALL_ARCHIVE_H
#define INSTALL_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

/* Read the complete requested range or return a negative errno. Writers
 * receive only decompressed bytes; an error leaves the output unpublished. */
typedef struct {
    void *context;
    int (*read)(void *, uint32_t offset, void *, unsigned size);
    int (*write)(void *, const void *, unsigned size);
} InstallArchiveIo;

int installZipExtract(const InstallArchiveIo *io, uint32_t archive_size,
                        const char *entry_name, uint32_t maximum, uint32_t *written);

#endif
