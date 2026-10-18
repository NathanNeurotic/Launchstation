#ifndef NEUTRINO_GAMEID_H
#define NEUTRINO_GAMEID_H

#include <stddef.h>

#define NEUTRINO_GAME_ID_SIZE 12

/* Read from ISO9660 SYSTEM.CNF without switching or loading drivers. */
int prepareNeutrinoGameID(const char *iso_path, char *gameID, size_t gameID_len);

#ifdef MMCE
/* Only use a pre-existing MMCE stack; never invalidate the ISO/ELF device. */
void setNeutrinoMmceGameID(const char *gameID, const char *iso_path, const char *elf_path);
#endif

#endif
