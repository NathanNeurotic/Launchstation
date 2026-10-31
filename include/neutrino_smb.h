#ifndef NEUTRINO_SMB_H
#define NEUTRINO_SMB_H
#include <stddef.h>
#include "smb_shares.h"

/* Pure, bounded conversion of a configured browser share into Neutrino's
 * SMB 3.0 backend. No network activity and no credentials in errors. */
int neutrinoSmbResolve(const SmbConfig *cfg, const char *path, SmbShare *share,
                      char *dvd, size_t dvd_size, const char **problem);
int neutrinoSmbConfig(const char *original, const SmbShare *share,
                     const char *ip, const char *mask, const char *gw,
                     char *output, size_t output_size);
#ifdef SMB
int smbSharesNeutrino(const char *path, SmbShare *share, char *dvd,
                     size_t dvd_size, const char **problem);
#endif
#endif
