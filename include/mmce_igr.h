#ifndef MMCE_IGR_H
#define MMCE_IGR_H

#include <stddef.h>

typedef struct {
    int boot_images;
    int installed;
    int removed;
    int failed;
} WleIgrInstallStats;

enum {
    WLE_IGR_STATUS_UNKNOWN = -1,
    WLE_IGR_STATUS_NOT_INSTALLED = 0,
    WLE_IGR_STATUS_CURRENT = 1,
    WLE_IGR_STATUS_OUTDATED = 2,
    WLE_IGR_STATUS_PARTIAL = 3
};

typedef struct {
    int copies;
    int current;
    int outdated;
    int missing;
    int failed;
} WleIgrStatusStats;

/* Configure OPL IGR automatic MMCE backup.
 * Direct MC/USB launches need no helper; MMCE-hosted wLaunchELF installs a
 * patched, verified helper into every discovered BootCard image. */
int wleIgrConfigure(const char *backup_root, WleIgrInstallStats *stats,
                    char *result, size_t result_size);
int wleIgrRemove(WleIgrInstallStats *stats, char *result, size_t result_size);

/* Read-only helper inspection. It never installs, removes, or starts a
 * storage stack. CURRENT means the installed bytes exactly match the helper
 * this build would install for the current application/backup paths. */
int wleIgrStatus(const char *backup_root, WleIgrStatusStats *stats);

#endif
