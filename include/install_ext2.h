#ifndef INSTALL_EXT2_H
#define INSTALL_EXT2_H
#include "hdd_format.h"
#include "install_tar.h"
typedef struct InstallExt2 InstallExt2;
/* Single APA extent, with its 4 MiB attribute area already excluded. */
int installExt2Create(InstallExt2 **out, const HddFormatIo *io,
                      uint32_t start, uint32_t sectors, uint32_t serial);
int installExt2Begin(InstallExt2 *, const InstallTarEntry *relative);
int installExt2Data(InstallExt2 *, const void *, unsigned);
int installExt2End(InstallExt2 *);
/* Flush pending writes and mark clean only after every selected archive and
 * its integrity/required-file checks succeeded. */
int installExt2Finish(InstallExt2 *);
void installExt2Free(InstallExt2 *);
int installPsbbnSwap(const HddFormatIo *, uint32_t start, uint32_t sectors);
int installPsbbnMusic(const HddFormatIo *, uint32_t start, uint32_t sectors, uint32_t serial);
#endif
