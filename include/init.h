#ifndef INIT_H
#define INIT_H

void setInitPhaseComplete(void);
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
#ifdef ILINK
int loadIlinkModules(void);
#endif
void setupPowerOff(void);
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

/* Ember cannot tolerate the launcher's homebrew SIO2MAN/PADMAN remaining
 * resident on the tested USB path. Reboot the IOP and rebuild only the USB
 * filesystem stack, deliberately leaving pad modules absent for Ember to
 * load from ROM itself. */
int prepareEmberCleanUsbIop(const char *ember_path);

#ifdef ETH
void loadNetModules(void);
int isFtpNetworkReady(void);
int ensureDashboardIpNetworkReady(void);
#endif
#ifdef UDPFS
int reloadUdpfsModules(void);
#endif

#endif
