#ifndef MMCE_IGR_H
#define MMCE_IGR_H

#include <stddef.h>

typedef struct {
    int boot_images;
    int installed;
    int failed;
} WleIgrInstallStats;

/* Configure OPL IGR automatic MMCE backup.
 * Direct MC/USB launches need no helper; MMCE-hosted wLaunchELF installs a
 * patched, verified helper into every discovered BootCard image. */
int wleIgrConfigure(const char *backup_root, WleIgrInstallStats *stats,
                    char *result, size_t result_size);

#endif
