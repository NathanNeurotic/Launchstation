#ifndef INSTALL_TAR_H
#define INSTALL_TAR_H
#include "install_archive.h"
#define INSTALL_TAR_PATH 1024
/* Unix metadata is retained by the ext2 sink. Symlink contents are data,
 * never followed while extracting. Hard links must target this filesystem. */
typedef struct {
    char name[INSTALL_TAR_PATH], link[INSTALL_TAR_PATH];
    uint32_t size, mode, uid, gid, mtime, major, minor;
    char type;
} InstallTarEntry;
typedef struct {
    void *context;
    int (*begin)(void *, const InstallTarEntry *);
    int (*data)(void *, const void *, unsigned);
    int (*end)(void *);
} InstallTarSink;
/* Bounded streaming gzip/tar reader, checks both container checksums and all
 * entries, including entries a preflight/discard sink does not write. */
int installTarGzip(const InstallArchiveIo *, uint32_t bytes, const InstallTarSink *);
#endif
