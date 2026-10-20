#ifndef FILER_COPY_H
#define FILER_COPY_H

#include "launchelf.h"

/* UI-thread paste diagnostics: one bounded record, no transfer-time I/O. */
void filerCopyDiagBegin(const char *source, const char *destination);
void filerCopyDiagStage(const char *stage);
void filerCopyDiagFormat(char *out, int size, int result);

int filerGetGameTitle(const char *path, const FILEINFO *file, unsigned char *out);

#endif
