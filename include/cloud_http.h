#ifndef WLE_CLOUD_HTTP_H
#define WLE_CLOUD_HTTP_H

#include <stddef.h>

#define CLOUD_HTTP_BODY_MAX (64 * 1024)
#define CLOUD_HTTP_LOCATION_MAX 768

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
int cloudHttpUploadFile(const char *method, const char *url,
                        const char *headers, const char *local_path,
                        const char *ca_path, CloudHttpResponse *response,
                        char *error, int error_size);

#endif
