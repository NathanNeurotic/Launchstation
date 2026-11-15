#ifndef NEUTRINO_GAMEID_H
#define NEUTRINO_GAMEID_H

#include <stddef.h>

#define NEUTRINO_GAME_ID_SIZE 12

/* Read from ISO9660 SYSTEM.CNF without switching or loading drivers. */
int prepareNeutrinoGameID(const char *iso_path, char *gameID, size_t gameID_len);

#ifdef MMCE
/* Manager-only control never invalidates the ISO/ELF device. Absent or
 * protected slots are skipped; 0 means a responding adapter failed to switch. */
int setNeutrinoMmceGameID(const char *gameID, const char *iso_path, const char *elf_path);
#endif

#endif
