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
/* GUI setup helpers. Client secret is optional for TV/limited-input clients. */
int cloudGoogleGetClientConfig(char *client_id, int client_id_size,
                               char *client_secret, int client_secret_size,
                               int *ca_present, char *error, int error_size);
int cloudGoogleSaveClientConfig(const char *client_id, const char *client_secret,
                                char *error, int error_size);
int cloudGoogleInstallCaBundle(const char *source_path, char *error, int error_size);
int cloudGoogleIsLinked(void);
int cloudGoogleHasLocalToken(void);
int cloudGoogleBeginLink(CloudGoogleDeviceCode *code, char *error, int error_size);
/* 0 = linked, 1 = authorization still pending, <0 = failed/expired. */
int cloudGoogleFinishLink(const CloudGoogleDeviceCode *code, char *error, int error_size);
int cloudGoogleDisconnect(char *error, int error_size);
int cloudGoogleSyncBackupRoot(const char *backup_root, CloudGoogleSyncStats *stats,
                              CloudGoogleProgress progress, char *error, int error_size);

#endif
