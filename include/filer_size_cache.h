#ifndef FILER_SIZE_CACHE_H
#define FILER_SIZE_CACHE_H
#include "launchelf.h"

/* Initialize on the main thread before starting workers. No cache call issues
 * device IO. Measurements are retained for this running application session. */
void filerSizeCacheInit(void);
int filerSizeCacheGet(const char *parent, const FILEINFO *file, u64 *size);
unsigned filerSizeCacheRevision(void);
void filerSizeCachePut(const char *parent, const FILEINFO *file, u64 size, unsigned revision);
/* Changes affect every cached ancestor and any cached subtree at this path. */
void filerSizeCacheInvalidate(const char *path);
void filerSizeCacheOpened(int fd, const char *path, int mode);
void filerSizeCacheClosed(int fd);
#endif
