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
/* Official builds embed Launchstation's own OAuth client and Google's root
 * CAs, so linking needs no user setup. The helpers below manage the optional
 * advanced overrides: a custom client in WLE-CLOUD.CNF and WLE-CLOUD-CA.PEM. */
int cloudGoogleHasBuiltinClient(void);
/* Reports only the custom WLE-CLOUD.CNF values (empty when not overridden). */
int cloudGoogleGetClientConfig(char *client_id, int client_id_size,
                               char *client_secret, int client_secret_size,
                               int *ca_present, char *error, int error_size);
int cloudGoogleSaveClientConfig(const char *client_id, const char *client_secret,
                                char *error, int error_size);
int cloudGoogleClearClientConfig(char *error, int error_size);
int cloudGoogleInstallCaBundle(const char *source_path, char *error, int error_size);
int cloudGoogleIsLinked(void);
int cloudGoogleHasLocalToken(void);
int cloudGoogleBeginLink(CloudGoogleDeviceCode *code, char *error, int error_size);
/* 0 = linked, 1 = authorization still pending, 2 = pending and Google asked
 * to poll more slowly (add 5 s to the interval), <0 = failed/expired. */
int cloudGoogleFinishLink(const CloudGoogleDeviceCode *code, char *error, int error_size);
int cloudGoogleDisconnect(char *error, int error_size);
/* Read-only browsing of backups created by this Launchstation OAuth client.
 * Each page is bounded so no full Drive file listing enters PS2 memory.
 * A null parent_id locates the app's WLE-MMCE-Backups root. */
#define CLOUD_GOOGLE_BROWSE_MAX 12
typedef struct {
    char id[192];
    char name[256];
    char sha256[65]; /* app-uploaded digest; empty for legacy/unverified items */
    int is_folder;
} CloudGoogleBrowseItem;
typedef struct {
    char folder_id[192];
    char next_page_token[256];
    int count;
    CloudGoogleBrowseItem items[CLOUD_GOOGLE_BROWSE_MAX];
} CloudGoogleBrowsePage;

int cloudGoogleListBackupHistory(const char *parent_id, const char *page_token,
                                 CloudGoogleBrowsePage *page,
                                 char *error, int error_size);

/* Download one listed Drive snapshot, verify its uploaded SHA-256 and
 * memory-card geometry, then create a NEW verified .mcd on USB/HDD.
 * Never overwrites local files or touches the MMCE filesystem. */
int cloudGoogleRecoverSnapshot(const CloudGoogleBrowseItem *snapshot,
                               const char *dest_dir, char *result_path,
                               int result_size, char *error, int error_size);

int cloudGoogleSyncBackupRoot(const char *backup_root, CloudGoogleSyncStats *stats,
                              CloudGoogleProgress progress, char *error, int error_size);

#endif
