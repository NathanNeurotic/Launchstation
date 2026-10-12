#ifndef WLE_CLOUD_HTTP_H
#define WLE_CLOUD_HTTP_H

#include <stddef.h>

#define CLOUD_HTTP_BODY_MAX (64 * 1024)
#define CLOUD_HTTP_LOCATION_MAX 2048

typedef struct {
    int status;
    char *body;
    int body_len;
    char location[CLOUD_HTTP_LOCATION_MAX];
} CloudHttpResponse;

void cloudHttpResponseFree(CloudHttpResponse *response);
int cloudHttpRequest(const char *method, const char *host, const char *path,
                     const char *headers, const void *body, int body_len,
                     const char *ca_path, CloudHttpResponse *response,
                     char *error, int error_size);
int cloudHttpUploadFd(const char *method, const char *url,
                      const char *headers, int fd, int file_size,
                      const char *ca_path, CloudHttpResponse *response,
                      char *error, int error_size);

/* Stream an HTTPS response body directly to an already-open file descriptor.
 * Follows HTTPS redirects, enforces max_bytes and returns the SHA-256 of the
 * exact bytes written when digest is non-NULL. */
int cloudHttpDownloadFd(const char *url, int fd, const char *ca_path,
                        int max_bytes, int *out_size,
                        unsigned char digest[32],
                        char *error, int error_size);

/* Authenticated Drive media stream. Only Google's API host receives the
 * bearer token; redirects may go to verified Google content hosts without
 * credentials. The public updater downloader remains unchanged. */
int cloudHttpDownloadGoogleFd(const char *url, const char *bearer_token, int fd,
                              const char *ca_path, int max_bytes,
                              int *out_size, unsigned char digest[32],
                              char *error, int error_size);

#endif
