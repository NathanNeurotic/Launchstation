#ifndef RIPTOPL_ISO_H
#define RIPTOPL_ISO_H

#include <stddef.h>

/*
 * Start a PS2 ISO through RiptOPL's boot-argument launch, as a separate choice next to Neutrino.
 *
 * RiptOPL reads <file name> <game ID> <DVD|CD> <bdm|ata> from its command line and boots that game on
 * its own core, which is where its pad emulator (PADEMU) lives. This prepares the four arguments and
 * finds RIPTOPL.ELF; the caller then starts the ELF with those arguments already loaded.
 *
 * Returns 1 and fills elf_path when the arguments are loaded; returns 0 with a reason in message
 * otherwise (no arguments are left loaded in that case).
 */
int RiptoplPrepareIsoLaunch(const char *iso_path, char *elf_path, size_t elf_size, char *message, size_t message_size);

/* Finds the RIPTOPL.ELF to use for discs: the Settings path, else a memory card root. Returns 1 when found. */
int RiptoplFindElf(char *elf_path, size_t elf_size);

/* arg_line is the single RiptOPL argument for the disc, "disc,<on>,<pad>,<macro>". */
int RiptoplPrepareDiscLaunch(char *elf_path, size_t elf_size, const char *arg_line, char *message, size_t message_size);

#endif
