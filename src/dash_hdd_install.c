#include "dash_internal.h"
#include "hdd_install.h"
#include "init.h"
#include "filer_shared.h"

int dashHddInstallChoose(HddInstall *in, int jail)
{
    memset(in, 0, sizeof(*in)); in->jail = jail;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
#ifndef EXFAT
    if (jail) return 1; /* no ATA filesystem driver in this build */
#endif
    {
        static const char *const profiles[] = {
            "Format only",
            "Install WLE-R3Z and R3CONFIGURATOR",
            "Install OSDMenu MBR and HOSDMenu",
            "Install MBR, HOSDMenu and included apps"
        };
        static const unsigned flags[] = {0, HDD_INSTALL_APPS, HDD_INSTALL_HOSD, HDD_INSTALL_HOSD | HDD_INSTALL_APPS};
        static const char *const jail_profiles[] = {
            "Format only", "Install PSBBN DP (English), MBR, HOSDMenu and apps"
        };
        int selected = dashModalChoice("Software after formatting", jail ? jail_profiles : profiles, jail ? 2 : 4, 0);
        if (selected < 0) return 0;
        in->flags = jail && selected ? HDD_INSTALL_PSBBN | HDD_INSTALL_HOSD | HDD_INSTALL_APPS : flags[selected];
        if (in->flags & HDD_INSTALL_PSBBN)
            dashModalMessage(DI_INFO, "PSBBN DP installation",
                "Requires a compatible Sony Network Adapter. Installs the English system, with 1 GiB each for music and contents. Keep the download device connected until installation finishes.", NULL);
    }
#endif
    return 1;
}

int dashHddInstallPrepare(HddInstall *in)
{
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
    char parent[MAX_PATH], path[sizeof(in->stage)];
    int made = 0;
    if (!in->flags) return 1;
    if (!dashWorkerAwaitAll("Preparing installer")) return 0;
    if (!dashHttpsCaPath(in->ca_path, sizeof(in->ca_path))) {
        dashModalMessage(DI_WARNING, "Installer needs the CA bundle",
            "WLE-UPDATE-CA.PEM was not found beside Launchstation or in its settings folder.", NULL);
        return 0;
    }
    if (!ensureDashboardIpNetworkReady()) {
        dashModalMessage(DI_WARNING, "Internet unavailable", dashboardIpNetworkError(), NULL);
        return 0;
    }
    if (!dashPickPath("Temporary downloads: USB or MMCE folder", "", DASH_PICK_FOLDER, NULL, parent, sizeof(parent))) return 0;
    if (genFixPath(parent, path) < 0) return 0;
    snprintf(parent, sizeof(parent), "%s", path);
    /* EXFAT's numbered massN aliases can change when ATA is reconnected.
     * Use its stable usbN namespace; genFixPath maps legacy mass: to usb0:. */
    if ((strncmp(parent, "usb", 3) || parent[3] < '0' || parent[3] > '9' || parent[4] != ':') && strncmp(parent, "mmce0:", 6) && strncmp(parent, "mmce1:", 6)
#ifndef EXFAT
        && strncmp(parent, "mass", 4)
#endif
       ) {
        dashModalMessage(DI_WARNING, "Choose USB or MMCE storage",
            "The installer downloads must be kept on a different device while the HDD is formatted.", NULL);
        return 0;
    }
    /* Picking a device may switch its driver stack. Restore the network and
     * prepare both sources before handing bounded file IO to the worker. */
    if (!ensureDashboardIpNetworkReady() || prepareTransferDeviceStacks(in->ca_path, parent) < 0) {
        dashModalMessage(DI_WARNING, "Installer unavailable", "The network or download device could not be prepared.", NULL);
        return 0;
    }
    for (int attempt = 0; attempt < 100; attempt++) {
        size_t n = strlen(parent);
        if (snprintf(path, sizeof(path), "%s%sLSINSTALL%02d", parent, n && parent[n - 1] == '/' ? "" : "/", attempt) >= (int)sizeof(path)) break;
        if (genMkdir(path, 0777) >= 0) { made = 1; break; }
        /* An existing cache is left untouched; only an unused name is ours. */
        if (!wleExists(path)) break;
    }
    if (!made) {
        dashModalMessage(DI_WARNING, "Download folder unavailable", "A temporary installer folder could not be created.", parent);
        return 0;
    }
    snprintf(in->stage, sizeof(in->stage), "%s", path);
    if (hddInstallClaimStage(in) < 0) {
        genRmdir(path); in->stage[0] = 0;
        dashModalMessage(DI_WARNING, "Download folder unavailable", "The installer could not mark its temporary folder.", NULL);
        return 0;
    }
    return 1;
#else
    (void)in;
    return 0;
#endif
}
