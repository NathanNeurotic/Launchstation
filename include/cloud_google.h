#ifndef WLE_CLOUD_GOOGLE_H
#define WLE_CLOUD_GOOGLE_H

#include <stddef.h>

typedef struct {
    char device_code[512];
    char user_code[48];
    char verification_url[192];
    int expires_in;
    int interval;
} CloudGoogleDeviceCode;

typedef struct {
    int folders_created;
    int files_seen;
    int uploaded;
    int skipped;
    int failed;
} CloudGoogleSyncStats;

typedef void (*CloudGoogleProgress)(const char *status, int done, int total);

int cloudGoogleConfigured(char *error, int error_size);
int cloudGoogleIsLinked(void);
int cloudGoogleHasLocalToken(void);
int cloudGoogleBeginLink(CloudGoogleDeviceCode *code, char *error, int error_size);
/* 0 = linked, 1 = authorization still pending, <0 = failed/expired. */
int cloudGoogleFinishLink(const CloudGoogleDeviceCode *code, char *error, int error_size);
int cloudGoogleDisconnect(char *error, int error_size);
int cloudGoogleSyncBackupRoot(const char *backup_root, CloudGoogleSyncStats *stats,
                              CloudGoogleProgress progress, char *error, int error_size);

#endif
