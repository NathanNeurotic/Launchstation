#ifndef HDD_INSTALL_RECOVERY_H
#define HDD_INSTALL_RECOVERY_H

/* One serialized installer worker. Keep its RPC buffers alive until the UI
 * acknowledges that the old IOP has stopped; never kill a thread mid-RPC. */
#define HDD_INSTALL_ATA_TIMEOUT (-502)
#define HDD_INSTALL_RPC_TIMEOUT_MS 60000u
#define HDD_INSTALL_RECOVERY_LIMIT 2u
void hddInstallRecoveryBegin(void);
void hddInstallRecoveryEnd(void);
int hddInstallRecoveryActive(void);
int hddInstallRecoveryFault(void);
void hddInstallRecoveryObserve(int result);
void hddInstallRecoveryRequestReset(void);
int hddInstallRecoveryResetRequested(void);
void hddInstallRecoveryResetComplete(void);
int hddInstallRecoveryWasReset(void);

#endif
