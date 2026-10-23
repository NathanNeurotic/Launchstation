#ifndef INIT_H
#define INIT_H

void setInitPhaseComplete(void);
/* Presentation only: independent of IOP/PAD initialization lifetime. */
int isStartupDriverStatusQuiet(void);
void setStartupDriverStatusQuiet(int quiet);
void closeKeyboardIfOpened(void);

void Reset(void);
unsigned int getIopResetGeneration(void);
/* Called with 0 right before an IOP reboot and 1 once the core stack, pads
 * and keyboard are back (nested calls are possible). The dashboard uses it
 * to park its UI thread while a background job reboots the IOP. */
extern void (*iopResetNotify)(int phase);
/* When set and returning non-zero, implicit IOP reboots (driver stack
 * switches) are skipped; the dashboard refuses them on its IO worker. */
extern int (*iopResetBlocked)(void);
void loadUsbModules(void);
int ensureUsbKeyboardReady(void);
#ifdef DS34
void loadDs34InputModules(void);
#endif
#ifdef MMCE
int loadMmceModulesForGameID(void);
#endif
#ifdef ILINK
int isIlinkConsoleSupported(void);
int loadIlinkModules(void);
#endif
void setupPowerOff(void);
int isPowerOffReady(void);
void closeAllAndPoweroff(void);
void startKbd(void);
void ensureCoreIoStackReady(void);
void ensureMemoryCardPortAccessible(int port);
void rebootIopAndReloadCoreStack(void);
void rebootIopAndReloadCoreStackSilent(void);
int loadSecrSifModule(void);
int prepareExploitSignerIop(void);
int isPathDeviceStackLoaded(const char *path);
int isUsbMassStackLoaded(void);

/* PC CARD CompactFlash (cf: / cf0:) ownership and status queries. */
int isPcCardCfEligible(void);
int pcCardCfOwnsBus(void);
int isPcCardCfLoaded(void);
int isPcCardCfBusBusy(void);
int isPcCardCfOnline(void);
int getPcCardCfCapacitySectors(u32 *sectors_out);
int isPcCardCfPath(const char *path);
int loadPcCardCfModules(void);
int shutdownPcCardCf(void);

/* Rebuild the USB storage IOP for Ember using the launcHER module order.
 * PADMAN is deliberately omitted; Ember owns pad initialization. */
int prepareEmberUsbHandoffIop(const char *ember_path, const char *game_image,
                             char *resolved, size_t resolved_size);


#ifdef ETH
void loadNetModules(void);
int isFtpNetworkReady(void);
int ensureDashboardIpNetworkReady(void);
#endif
#ifdef UDPFS
int reloadUdpfsModules(void);
#endif

#endif
