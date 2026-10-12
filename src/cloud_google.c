//--------------------------------------------------------------
// Google OAuth device flow + Drive backup transport for wLaunchELF.
// This is an original implementation against Google's documented APIs.
//--------------------------------------------------------------
#include "launchelf.h"
#include "cloud_google.h"
#include "cloud_http.h"
#include "dash_internal.h"
#include "filer_shared.h"
#include "init.h"
#include "mc_card_export.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/sha256.h>

#define CLOUD_CLIENT_MAX 384
#define CLOUD_TOKEN_MAX 2048
#define CLOUD_ID_MAX 192
#define CLOUD_JSON_MAX 8192
#define CLOUD_TREE_DEPTH 16

typedef struct {
    char client_id[CLOUD_CLIENT_MAX];
    char client_secret[CLOUD_CLIENT_MAX];
} GoogleClient;

typedef struct {
    GoogleClient client;
    char ca_path[MAX_PATH];
    char token_path[MAX_PATH];
    char token_stage_path[MAX_PATH];
    char access_token[CLOUD_TOKEN_MAX];
    char refresh_token[CLOUD_TOKEN_MAX];
    int token_stage_complete;
} GoogleSession;

static void set_error(char *error, int error_size, const char *text)
{
    if (error && error_size > 0)
        snprintf(error, error_size, "%s", text ? text : "Cloud operation failed");
}

static void strcopy(char *out, int out_size, const char *value)
{
    if (out && out_size > 0)
        snprintf(out, out_size, "%s", value ? value : "");
}

static int json_string(const char *json, const char *key, char *out, int out_size)
{
    char needle[96];
    const char *p, *end;
    int n = 0;

    if (!json || !key || !out || out_size <= 0)
        return 0;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p)
        return 0;
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p++ != ':')
        return 0;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p++ != '"')
        return 0;
    end = p;
    while (*end) {
        if (*end == '"' && (end == p || end[-1] != '\\'))
            break;
        end++;
    }
    if (*end != '"')
        return 0;
    while (p < end && n < out_size - 1) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n': out[n++] = '\n'; break;
                case 'r': out[n++] = '\r'; break;
                case 't': out[n++] = '\t'; break;
                case '\\': out[n++] = '\\'; break;
                case '"': out[n++] = '"'; break;
                default: out[n++] = *p; break;
            }
            p++;
        } else
            out[n++] = *p++;
    }
    out[n] = 0;
    return 1;
}

static int json_int(const char *json, const char *key, int def)
{
    char needle[96];
    const char *p;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = json ? strstr(json, needle) : NULL;
    if (!p)
        return def;
    p += strlen(needle);
    while (*p && (isspace((unsigned char)*p) || *p == ':'))
        p++;
    return *p ? atoi(p) : def;
}

static int append_url(char *out, int out_size, int at, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)value;
    while (*p && at < out_size - 1) {
        unsigned char c = *p++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out[at++] = (char)c;
        else {
            if (at + 3 >= out_size)
                return -1;
            out[at++] = '%';
            out[at++] = hex[c >> 4];
            out[at++] = hex[c & 15];
        }
    }
    if (*p)
        return -1;
    out[at] = 0;
    return at;
}

static int form_field(char *out, int out_size, int at, const char *key, const char *value)
{
    if (at && at < out_size - 1)
        out[at++] = '&';
    at = append_url(out, out_size, at, key);
    if (at < 0 || at >= out_size - 1)
        return -1;
    out[at++] = '=';
    out[at] = 0;
    return append_url(out, out_size, at, value ? value : "");
}

static int json_escape(const char *value, char *out, int out_size)
{
    int at = 0;
    const unsigned char *p = (const unsigned char *)value;
    while (*p && at < out_size - 1) {
        unsigned char c = *p++;
        if (c == '"' || c == '\\') {
            if (at + 2 >= out_size)
                return -1;
            out[at++] = '\\';
            out[at++] = (char)c;
        } else if (c >= 0x20)
            out[at++] = (char)c;
    }
    if (*p)
        return -1;
    out[at] = 0;
    return at;
}

static int q_escape(const char *value, char *out, int out_size)
{
    int at = 0;
    const char *p = value;
    while (*p && at < out_size - 1) {
        if (*p == '\\' || *p == '\'') {
            if (at + 2 >= out_size)
                return -1;
            out[at++] = '\\';
        }
        out[at++] = *p++;
    }
    if (*p)
        return -1;
    out[at] = 0;
    return at;
}

static int client_kv(const char *key, char *value, void *user)
{
    GoogleClient *client = user;
    if (!stricmp(key, "Google_Client_ID"))
        strcopy(client->client_id, sizeof(client->client_id), value);
    else if (!stricmp(key, "Google_Client_Secret"))
        strcopy(client->client_secret, sizeof(client->client_secret), value);
    return 0;
}

static int token_kv(const char *key, char *value, void *user)
{
    GoogleSession *s = user;
    if (!stricmp(key, "Refresh_Token"))
        strcopy(s->refresh_token, sizeof(s->refresh_token), value);
    else if (!stricmp(key, "Stage_Complete"))
        s->token_stage_complete = atoi(value) == 1;
    return 0;
}

static int load_session(GoogleSession *s, int need_token, char *error, int error_size)
{
    char path[MAX_PATH];
    char *text;
    int len;

    memset(s, 0, sizeof(*s));
    dashStoreInit();
    if (dashStoreBuildPath(s->ca_path, sizeof(s->ca_path), "WLE-CLOUD-CA.PEM") < 0 ||
        dashStoreBuildPath(s->token_path, sizeof(s->token_path), "WLE-CLOUD.TOK") < 0 ||
        dashStoreBuildPath(s->token_stage_path, sizeof(s->token_stage_path), "WLE-CLOUD.TOK.NEW") < 0 ||
        dashStoreBuildPath(path, sizeof(path), "WLE-CLOUD.CNF") < 0) {
        set_error(error, error_size, "Cloud settings folder is unavailable");
        return -1;
    }

#ifdef WLE_GOOGLE_CLIENT_ID
    strcopy(s->client.client_id, sizeof(s->client.client_id), WLE_GOOGLE_CLIENT_ID);
#endif
#ifdef WLE_GOOGLE_CLIENT_SECRET
    strcopy(s->client.client_secret, sizeof(s->client.client_secret), WLE_GOOGLE_CLIENT_SECRET);
#endif

    text = dashLoadTextFile(path, 16 * 1024, &len);
    if (text != NULL) {
        dashKvParse(text, client_kv, &s->client);
        free(text);
    }
    if (s->client.client_id[0] == 0) {
        set_error(error, error_size,
                  "No Google OAuth client. Put Google_Client_ID in WLE-CLOUD.CNF.");
        return -1;
    }
    if (!wleExists(s->ca_path)) {
        set_error(error, error_size,
                  "WLE-CLOUD-CA.PEM is missing from the dashboard settings folder.");
        return -1;
    }

    if (need_token) {
        /* A .NEW file is a verified staging copy left only if power was lost
         * while replacing the primary token. Prefer it so interrupted writes
         * recover without filesystem-specific rename semantics. */
        text = dashLoadTextFile(s->token_stage_path, 8 * 1024, &len);
        if (text != NULL) {
            dashKvParse(text, token_kv, s);
            free(text);
            if (!s->token_stage_complete)
                s->refresh_token[0] = 0;
        }
        if (s->refresh_token[0] == 0) {
            text = dashLoadTextFile(s->token_path, 8 * 1024, &len);
            if (text != NULL) {
                dashKvParse(text, token_kv, s);
                free(text);
            }
        }
        if (s->refresh_token[0] == 0) {
            set_error(error, error_size, "Google Drive is not connected.");
            return -1;
        }
    }
    return 0;
}

static int save_refresh_token(GoogleSession *s, const char *token,
                              char *error, int error_size)
{
    GoogleSession verify;
    char *text, *saved;
    int len, saved_len;

    len = (int)strlen(token) + 128;
    text = malloc(len);
    if (!text) {
        set_error(error, error_size, "Out of memory saving Google token");
        return -1;
    }
    len = snprintf(text, len, "# Launchstation Google Drive OAuth token\r\nRefresh_Token = %s\r\nStage_Complete = 1\r\n", token);

    /* Write and read back a recovery copy before touching the primary file.
     * If power fails during the primary write, load_session() prefers this
     * verified staging token on the next boot. */
    if (dashWriteFile(s->token_stage_path, text, len) != 0) {
        free(text);
        set_error(error, error_size, "Could not stage WLE-CLOUD.TOK");
        return -1;
    }
    memset(&verify, 0, sizeof(verify));
    saved = dashLoadTextFile(s->token_stage_path, 8 * 1024, &saved_len);
    if (saved != NULL) {
        dashKvParse(saved, token_kv, &verify);
        free(saved);
    }
    if (!verify.token_stage_complete || strcmp(verify.refresh_token, token) != 0) {
        genRemove(s->token_stage_path);
        free(text);
        set_error(error, error_size, "Could not verify staged Google token");
        return -1;
    }

    if (dashWriteFile(s->token_path, text, len) != 0) {
        free(text);
        set_error(error, error_size,
                  "Could not save WLE-CLOUD.TOK; recovery copy retained");
        return -1;
    }
    free(text);
    genRemove(s->token_stage_path);
    return 0;
}

static int oauth_post(GoogleSession *s, const char *path, const char *form,
                      CloudHttpResponse *response, char *error, int error_size)
{
    const char headers[] = "Content-Type: application/x-www-form-urlencoded\r\n";
    if (!ensureDashboardIpNetworkReady()) {
        set_error(error, error_size, "Ethernet could not be started");
        return -1;
    }
    return cloudHttpRequest("POST", "oauth2.googleapis.com", path, headers,
                            form, (int)strlen(form), s->ca_path, response,
                            error, error_size);
}

static int refresh_access_token(GoogleSession *s, char *error, int error_size)
{
    char form[4096];
    int at = 0;
    CloudHttpResponse response;

    at = form_field(form, sizeof(form), at, "client_id", s->client.client_id);
    if (at >= 0 && s->client.client_secret[0])
        at = form_field(form, sizeof(form), at, "client_secret", s->client.client_secret);
    if (at >= 0)
        at = form_field(form, sizeof(form), at, "refresh_token", s->refresh_token);
    if (at >= 0)
        at = form_field(form, sizeof(form), at, "grant_type", "refresh_token");
    if (at < 0) {
        set_error(error, error_size, "OAuth request is too large");
        return -1;
    }
    if (oauth_post(s, "/token", form, &response, error, error_size) < 0)
        return -1;
    if (response.status != 200 || !json_string(response.body, "access_token",
                                               s->access_token, sizeof(s->access_token))) {
        char oauth_error[128];
        if (json_string(response.body, "error", oauth_error, sizeof(oauth_error)))
            snprintf(error, error_size, "Google OAuth: %s", oauth_error);
        else
            snprintf(error, error_size, "Google OAuth returned HTTP %d", response.status);
        cloudHttpResponseFree(&response);
        return -1;
    }
    cloudHttpResponseFree(&response);
    return 0;
}

int cloudGoogleConfigured(char *error, int error_size)
{
    GoogleSession s;
    return load_session(&s, 0, error, error_size) == 0;
}

static int cloudConfigLineSafe(const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    while (*p) {
        if (*p == '\r' || *p == '\n' || *p < 0x20)
            return 0;
        p++;
    }
    return 1;
}

int cloudGoogleGetClientConfig(char *client_id, int client_id_size,
                               char *client_secret, int client_secret_size,
                               int *ca_present, char *error, int error_size)
{
    GoogleClient client;
    char cnf_path[MAX_PATH], ca_path[MAX_PATH];
    char *text;
    int len;

    memset(&client, 0, sizeof(client));
    if (client_id && client_id_size > 0)
        client_id[0] = '\0';
    if (client_secret && client_secret_size > 0)
        client_secret[0] = '\0';
    if (ca_present)
        *ca_present = 0;

    dashStoreInit();
    if (dashStoreBuildPath(cnf_path, sizeof(cnf_path), "WLE-CLOUD.CNF") < 0 ||
        dashStoreBuildPath(ca_path, sizeof(ca_path), "WLE-CLOUD-CA.PEM") < 0) {
        set_error(error, error_size, "Cloud settings folder is unavailable");
        return -1;
    }

#ifdef WLE_GOOGLE_CLIENT_ID
    strcopy(client.client_id, sizeof(client.client_id), WLE_GOOGLE_CLIENT_ID);
#endif
#ifdef WLE_GOOGLE_CLIENT_SECRET
    strcopy(client.client_secret, sizeof(client.client_secret), WLE_GOOGLE_CLIENT_SECRET);
#endif
    text = dashLoadTextFile(cnf_path, 16 * 1024, &len);
    if (text != NULL) {
        dashKvParse(text, client_kv, &client);
        free(text);
    }

    if (client_id && client_id_size > 0)
        strcopy(client_id, client_id_size, client.client_id);
    if (client_secret && client_secret_size > 0)
        strcopy(client_secret, client_secret_size, client.client_secret);
    if (ca_present)
        *ca_present = wleExists(ca_path);
    if (error && error_size > 0)
        error[0] = '\0';
    return 0;
}

int cloudGoogleSaveClientConfig(const char *client_id, const char *client_secret,
                                char *error, int error_size)
{
    char path[MAX_PATH];
    char text[1024];
    int len;

    if (client_id == NULL || client_id[0] == '\0') {
        set_error(error, error_size, "Google OAuth client ID cannot be empty.");
        return -1;
    }
    if (strlen(client_id) >= CLOUD_CLIENT_MAX ||
        (client_secret != NULL && strlen(client_secret) >= CLOUD_CLIENT_MAX) ||
        !cloudConfigLineSafe(client_id) || !cloudConfigLineSafe(client_secret)) {
        set_error(error, error_size, "OAuth client values are too long or contain invalid control characters.");
        return -1;
    }
    dashStoreInit();
    if (dashStoreBuildPath(path, sizeof(path), "WLE-CLOUD.CNF") < 0) {
        set_error(error, error_size, "Cloud settings folder is unavailable");
        return -1;
    }

    len = snprintf(text, sizeof(text),
                   "# Launchstation Google Drive OAuth configuration\r\n"
                   "Google_Client_ID = %s\r\n"
                   "Google_Client_Secret = %s\r\n",
                   client_id, client_secret ? client_secret : "");
    if (len <= 0 || len >= (int)sizeof(text) || dashWriteFile(path, text, len) != 0) {
        set_error(error, error_size, "Could not save WLE-CLOUD.CNF.");
        return -1;
    }
    if (error && error_size > 0)
        error[0] = '\0';
    return 0;
}

int cloudGoogleInstallCaBundle(const char *source_path, char *error, int error_size)
{
    char dest[MAX_PATH];
    char *pem;
    int len, r;

    if (source_path == NULL || source_path[0] == '\0') {
        set_error(error, error_size, "Choose a PEM certificate bundle first.");
        return -1;
    }
    dashStoreInit();
    if (dashStoreBuildPath(dest, sizeof(dest), "WLE-CLOUD-CA.PEM") < 0) {
        set_error(error, error_size, "Cloud settings folder is unavailable");
        return -1;
    }
    if (!stricmp(source_path, dest)) {
        if (!wleExists(dest)) {
            set_error(error, error_size, "The selected CA bundle does not exist.");
            return -1;
        }
        return 0;
    }

    pem = dashLoadTextFile(source_path, 256 * 1024, &len);
    if (pem == NULL || len <= 0) {
        free(pem);
        set_error(error, error_size, "Could not read the selected CA bundle.");
        return -1;
    }
    if (strstr(pem, "-----BEGIN CERTIFICATE-----") == NULL ||
        strstr(pem, "-----END CERTIFICATE-----") == NULL) {
        free(pem);
        set_error(error, error_size, "The selected file does not contain PEM certificates.");
        return -1;
    }
    r = dashWriteFile(dest, pem, len);
    free(pem);
    if (r != 0) {
        set_error(error, error_size, "Could not save WLE-CLOUD-CA.PEM.");
        return -1;
    }
    if (error && error_size > 0)
        error[0] = '\0';
    return 0;
}

int cloudGoogleIsLinked(void)
{
    GoogleSession s;
    char error[8];
    return load_session(&s, 1, error, sizeof(error)) == 0;
}

int cloudGoogleHasLocalToken(void)
{
    char token_path[MAX_PATH], stage_path[MAX_PATH];

    dashStoreInit();
    if (dashStoreBuildPath(token_path, sizeof(token_path), "WLE-CLOUD.TOK") < 0 ||
        dashStoreBuildPath(stage_path, sizeof(stage_path), "WLE-CLOUD.TOK.NEW") < 0)
        return 0;
    return wleExists(token_path) || wleExists(stage_path);
}

int cloudGoogleBeginLink(CloudGoogleDeviceCode *code, char *error, int error_size)
{
    GoogleSession s;
    CloudHttpResponse response;
    char form[2048];
    int at = 0;

    if (code == NULL)
        return -1;
    memset(code, 0, sizeof(*code));
    if (load_session(&s, 0, error, error_size) < 0)
        return -1;
    at = form_field(form, sizeof(form), at, "client_id", s.client.client_id);
    if (at >= 0)
        at = form_field(form, sizeof(form), at, "scope",
                        "https://www.googleapis.com/auth/drive.file");
    if (at < 0) {
        set_error(error, error_size, "OAuth device request is too large");
        return -1;
    }
    if (oauth_post(&s, "/device/code", form, &response, error, error_size) < 0)
        return -1;
    if (response.status != 200 ||
        !json_string(response.body, "device_code", code->device_code, sizeof(code->device_code)) ||
        !json_string(response.body, "user_code", code->user_code, sizeof(code->user_code))) {
        snprintf(error, error_size, "Google device authorization returned HTTP %d", response.status);
        cloudHttpResponseFree(&response);
        return -1;
    }
    if (!json_string(response.body, "verification_url", code->verification_url, sizeof(code->verification_url)) &&
        !json_string(response.body, "verification_uri", code->verification_url, sizeof(code->verification_url))) {
        set_error(error, error_size, "Google did not provide a verification URL.");
        cloudHttpResponseFree(&response);
        return -1;
    }
    code->expires_in = json_int(response.body, "expires_in", 1800);
    code->interval = json_int(response.body, "interval", 5);
    cloudHttpResponseFree(&response);
    return 0;
}

int cloudGoogleFinishLink(const CloudGoogleDeviceCode *code, char *error, int error_size)
{
    GoogleSession s;
    CloudHttpResponse response;
    char form[4096], refresh[CLOUD_TOKEN_MAX], oauth_error[128];
    int at = 0;

    if (code == NULL || code->device_code[0] == 0)
        return -1;
    if (load_session(&s, 0, error, error_size) < 0)
        return -1;
    at = form_field(form, sizeof(form), at, "client_id", s.client.client_id);
    if (at >= 0 && s.client.client_secret[0])
        at = form_field(form, sizeof(form), at, "client_secret", s.client.client_secret);
    if (at >= 0)
        at = form_field(form, sizeof(form), at, "device_code", code->device_code);
    if (at >= 0)
        at = form_field(form, sizeof(form), at, "grant_type",
                        "urn:ietf:params:oauth:grant-type:device_code");
    if (at < 0)
        return -1;

    if (oauth_post(&s, "/token", form, &response, error, error_size) < 0)
        return -1;
    if (response.status == 200 &&
        json_string(response.body, "refresh_token", refresh, sizeof(refresh))) {
        int r = save_refresh_token(&s, refresh, error, error_size);
        cloudHttpResponseFree(&response);
        return r;
    }

    oauth_error[0] = 0;
    json_string(response.body, "error", oauth_error, sizeof(oauth_error));
    cloudHttpResponseFree(&response);
    if (!strcmp(oauth_error, "authorization_pending") || !strcmp(oauth_error, "slow_down")) {
        set_error(error, error_size, "Authorization is not complete yet.");
        return 1;
    }
    if (oauth_error[0])
        snprintf(error, error_size, "Google OAuth: %s", oauth_error);
    else
        set_error(error, error_size, "Google did not return a refresh token.");
    return -1;
}

int cloudGoogleDisconnect(char *error, int error_size)
{
    char token_path[MAX_PATH], stage_path[MAX_PATH];
    int failed = 0;

    dashStoreInit();
    if (dashStoreBuildPath(token_path, sizeof(token_path), "WLE-CLOUD.TOK") < 0 ||
        dashStoreBuildPath(stage_path, sizeof(stage_path), "WLE-CLOUD.TOK.NEW") < 0) {
        set_error(error, error_size, "Cloud settings folder is unavailable");
        return -1;
    }
    if (wleExists(token_path) && genRemove(token_path) < 0)
        failed = 1;
    if (wleExists(stage_path) && genRemove(stage_path) < 0)
        failed = 1;
    if (failed) {
        set_error(error, error_size, "Could not remove saved Google token");
        return -1;
    }
    return 0;
}

static int drive_request(GoogleSession *s, const char *method, const char *path,
                         const char *content_type, const char *body,
                         CloudHttpResponse *response, char *error, int error_size)
{
    char *headers;
    int cap, body_len = body ? (int)strlen(body) : 0;
    cap = (int)strlen(s->access_token) + 256;
    headers = malloc(cap);
    if (!headers)
        return -1;
    snprintf(headers, cap, "Authorization: Bearer %s\r\nContent-Type: %s\r\n",
             s->access_token, content_type ? content_type : "application/json");
    if (cloudHttpRequest(method, "www.googleapis.com", path, headers,
                         body, body_len, s->ca_path, response, error, error_size) < 0) {
        free(headers);
        return -1;
    }
    free(headers);
    return 0;
}

static int drive_find_child(GoogleSession *s, const char *parent, const char *name,
                            int folder, const char *sha256, char *id, int id_size,
                            char *error, int error_size)
{
    char escaped_name[MAX_NAME * 2], escaped_parent[CLOUD_ID_MAX * 2];
    char query[1280], encoded[3840], path[4352];
    CloudHttpResponse response;
    int at;

    if (q_escape(name, escaped_name, sizeof(escaped_name)) < 0 ||
        q_escape(parent, escaped_parent, sizeof(escaped_parent)) < 0)
        return -1;
    if (!folder && sha256 && sha256[0]) {
        snprintf(query, sizeof(query),
                 "'%s' in parents and name='%s' and trashed=false "
                 "and mimeType!='application/vnd.google-apps.folder' "
                 "and appProperties has { key='wle_sha256' and value='%s' }",
                 escaped_parent, escaped_name, sha256);
    } else {
        snprintf(query, sizeof(query),
                 "'%s' in parents and name='%s' and trashed=false%s",
                 escaped_parent, escaped_name,
                 folder ? " and mimeType='application/vnd.google-apps.folder'" :
                          " and mimeType!='application/vnd.google-apps.folder'");
    }
    at = append_url(encoded, sizeof(encoded), 0, query);
    if (at < 0)
        return -1;
    snprintf(path, sizeof(path),
             "/drive/v3/files?q=%s&spaces=drive&pageSize=2&fields=files(id,name,mimeType)",
             encoded);
    if (drive_request(s, "GET", path, "application/json", NULL,
                      &response, error, error_size) < 0)
        return -1;
    if (response.status != 200) {
        snprintf(error, error_size, "Drive list returned HTTP %d", response.status);
        cloudHttpResponseFree(&response);
        return -1;
    }
    if (!json_string(response.body, "id", id, id_size))
        id[0] = 0;
    cloudHttpResponseFree(&response);
    return id[0] ? 1 : 0;
}


/* Drive browsing is deliberately metadata-only. Files are never fetched or
 * written by this API, and it only walks the app-owned backup root. */
static int drive_safe_id(const char *id)
{
    const unsigned char *p = (const unsigned char *)id;
    if (id == NULL || !*id || strlen(id) >= CLOUD_ID_MAX)
        return 0;
    for (; *p; ++p)
        if (!isalnum(*p) && *p != '-' && *p != '_')
            return 0;
    return 1;
}

/* Extract exactly one JSON object, respecting quoted braces and escapes.
 * Google may add fields in the future, but we only request three strings. */
static int drive_browse_parse(const char *json, CloudGoogleBrowsePage *page)
{
    const char *p, *start, *end;
    char item[768], mime[96];
    int inside = 0, escaped = 0, depth, len;

    if (json == NULL || page == NULL)
        return -1;
    p = strstr(json, "\"files\"");
    if (!p || !(p = strchr(p, '[')))
        return -1;
    ++p;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ','))
            ++p;
        if (*p == ']')
            return 0;
        if (*p != '{')
            return -1;
        start = p;
        depth = 0;
        inside = escaped = 0;
        for (; *p; ++p) {
            if (inside) {
                if (escaped)
                    escaped = 0;
                else if (*p == '\\')
                    escaped = 1;
                else if (*p == '"')
                    inside = 0;
            } else if (*p == '"') {
                inside = 1;
            } else if (*p == '{') {
                depth++;
            } else if (*p == '}' && --depth == 0) {
                ++p;
                break;
            }
        }
        if (depth != 0 || inside)
            return -1;
        end = p;
        len = (int)(end - start);
        if (len <= 0 || len >= (int)sizeof(item))
            return -1;
        memcpy(item, start, len);
        item[len] = 0;
        if (page->count < CLOUD_GOOGLE_BROWSE_MAX) {
            CloudGoogleBrowseItem *e = &page->items[page->count];
            if (!json_string(item, "id", e->id, sizeof(e->id)) ||
                !json_string(item, "name", e->name, sizeof(e->name)) ||
                !json_string(item, "mimeType", mime, sizeof(mime)))
                return -1;
            if (!drive_safe_id(e->id) ||
                e->name[0] == '\0' || !strcmp(e->name, ".") ||
                !strcmp(e->name, ".."))
                return -1;
            e->is_folder = !strcmp(mime, "application/vnd.google-apps.folder");
            if (!e->is_folder)
                json_string(item, "wle_sha256", e->sha256, sizeof(e->sha256));
            if (e->is_folder ||
                (!strncmp(e->name, "WLE-", 4) &&
                 (genCmpFileExt(e->name, "MCD") ||
                  genCmpFileExt(e->name, "PS2"))))
                page->count++;
            else
                memset(e, 0, sizeof(*e));
        }
    }
    return -1;
}

int cloudGoogleListBackupHistory(const char *parent_id, const char *page_token,
                                 CloudGoogleBrowsePage *page,
                                 char *error, int error_size)
{
    GoogleSession session;
    CloudHttpResponse response;
    char root[CLOUD_ID_MAX], escaped[CLOUD_ID_MAX * 2];
    char query[384], encoded[1152], path[2304];
    int n, at;

    if (page == NULL)
        return -1;
    memset(page, 0, sizeof(*page));
    if (load_session(&session, 1, error, error_size) < 0)
        return -1;
    if (!ensureDashboardIpNetworkReady()) {
        set_error(error, error_size, "Network is unavailable for Drive history browsing");
        return -1;
    }
    if (refresh_access_token(&session, error, error_size) < 0)
        return -1;
    if (parent_id == NULL || parent_id[0] == '\0') {
        n = drive_find_child(&session, "root", "WLE-MMCE-Backups", 1, NULL,
                             root, sizeof(root), error, error_size);
        if (n <= 0) {
            if (n == 0)
                set_error(error, error_size, "No Launchstation backup folder found on Drive");
            return -1;
        }
    } else {
        if (!drive_safe_id(parent_id)) {
            set_error(error, error_size, "Invalid Google Drive history folder ID");
            return -1;
        }
        snprintf(root, sizeof(root), "%s", parent_id);
    }
    snprintf(page->folder_id, sizeof(page->folder_id), "%s", root);
    if (q_escape(root, escaped, sizeof(escaped)) < 0)
        return -1;
    n = snprintf(query, sizeof(query), "'%s' in parents and trashed=false", escaped);
    if (n <= 0 || n >= (int)sizeof(query))
        return -1;
    at = append_url(encoded, sizeof(encoded), 0, query);
    if (at < 0)
        return -1;
    n = snprintf(path, sizeof(path),
                 "/drive/v3/files?q=%s&spaces=drive&pageSize=%d"
                 "&fields=nextPageToken,files(id,name,mimeType,appProperties)&orderBy=name",
                 encoded, CLOUD_GOOGLE_BROWSE_MAX);
    if (n <= 0 || n >= (int)sizeof(path))
        return -1;
    if (page_token != NULL && *page_token != '\0') {
        int at_path;
        if (strlen(page_token) > 240) {
            set_error(error, error_size, "Invalid cloud-history pagination token");
            return -1;
        }
        at_path = n;
        n = snprintf(path + at_path, sizeof(path) - at_path, "&pageToken=");
        if (n < 0 || n >= (int)(sizeof(path) - at_path))
            return -1;
        if (append_url(path, sizeof(path), at_path + n, page_token) < 0)
            return -1;
    }
    memset(&response, 0, sizeof(response));
    if (drive_request(&session, "GET", path, "application/json", NULL,
                      &response, error, error_size) < 0)
        return -1;
    if (response.status != 200 ||
        drive_browse_parse(response.body, page) < 0) {
        snprintf(error, error_size, "Drive history listing failed (HTTP %d)", response.status);
        cloudHttpResponseFree(&response);
        return -1;
    }
    json_string(response.body, "nextPageToken", page->next_page_token,
                sizeof(page->next_page_token));
    cloudHttpResponseFree(&response);
    return 0;
}


#define CLOUD_RECOVERY_MAX_BYTES (96 * 1024 * 1024)

/* The digest lives in Drive metadata created by our verified uploader.
 * A plain filename, file ID or HTTP success is never sufficient to publish
 * an image that will later be used as a PS2 memory card. */
static int cloud_valid_digest(const char *hex)
{
    int i;
    if (hex == NULL || strlen(hex) != 64)
        return 0;
    for (i = 0; i < 64; ++i)
        if (!isxdigit((unsigned char)hex[i]))
            return 0;
    return 1;
}

static int cloud_digest_matches(const unsigned char digest[32], const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    int i;
    if (!cloud_valid_digest(hex))
        return 0;
    for (i = 0; i < 32; ++i) {
        if (tolower((unsigned char)hex[2 * i]) != digits[digest[i] >> 4] ||
            tolower((unsigned char)hex[2 * i + 1]) != digits[digest[i] & 15])
            return 0;
    }
    return 1;
}

/* Valid recovered images are created only on a device with standard
 * same-directory rename semantics, never an actively emulated MMCE card. */
static int cloud_recovery_path(const char *folder, char *result, size_t result_size,
                               char *download, size_t download_size,
                               char *stage, size_t stage_size)
{
    int i, n, a, b;
    size_t len;

    if (folder == NULL || !strchr(folder, ':') ||
        (strncmp(folder, "usb", 3) && strncmp(folder, "mass", 4) &&
         strncmp(folder, "ata", 3) && strncmp(folder, "hdd", 3)))
        return 0;
    len = strlen(folder);
    if (len == 0)
        return 0;
    for (i = 1; i <= 999; i++) {
        n = snprintf(result, result_size, "%s%sRECOVER-CLOUD-%03d.mcd",
                     folder, folder[len - 1] == ':' || folder[len - 1] == '/' ? "" : "/", i);
        a = snprintf(stage, stage_size, "%s.PART", result);
        b = snprintf(download, download_size, "%s%sRECOVER-CLOUD-%03d.DOWNLOAD.PART",
                     folder, folder[len - 1] == ':' || folder[len - 1] == '/' ? "" : "/", i);
        if (n <= 0 || n >= (int)result_size || a <= 0 || a >= (int)stage_size ||
            b <= 0 || b >= (int)download_size)
            return 0;
        if (!wleExists(result) && !wleExists(stage) && !wleExists(download))
            return 1;
    }
    return 0;
}

int cloudGoogleRecoverSnapshot(const CloudGoogleBrowseItem *snapshot,
                               const char *dest_dir, char *result_path,
                               int result_size, char *error, int error_size)
{
    GoogleSession session;
    char result[MAX_PATH], stage[MAX_PATH], download[MAX_PATH], url[512];
    unsigned char digest[32];
    int is_pcsx2, bytes = 0, fd = -1, r, result_created = 0;

    if (result_path && result_size > 0)
        result_path[0] = '\0';
    if (snapshot == NULL || snapshot->is_folder || dest_dir == NULL ||
        result_path == NULL || result_size <= 0 ||
        !drive_safe_id(snapshot->id) ||
        !cloud_valid_digest(snapshot->sha256) ||
        (!genCmpFileExt(snapshot->name, "MCD") &&
         !genCmpFileExt(snapshot->name, "PS2"))) {
        set_error(error, error_size,
                  "This item lacks valid snapshot metadata and cannot be recovered");
        return -1;
    }
    is_pcsx2 = genCmpFileExt(snapshot->name, "PS2");
    if (!cloud_recovery_path(dest_dir, result, sizeof(result),
                             download, sizeof(download), stage, sizeof(stage))) {
        set_error(error, error_size,
                  "Choose a writable USB/HDD folder with an unused recovery filename");
        return -1;
    }
    if (strlen(result) >= (size_t)result_size) {
        set_error(error, error_size, "Recovery destination path is too long");
        return -1;
    }
    /* Start local storage before Ethernet so network startup cannot reuse
     * a mount whose driver has not yet been initialized. */
    if (!ensurePathDeviceStackReady(dest_dir)) {
        set_error(error, error_size, "Could not start the recovery storage device");
        return -1;
    }
    if (load_session(&session, 1, error, error_size) < 0)
        return -1;
    if (!ensureDashboardIpNetworkReady()) {
        set_error(error, error_size, "Ethernet is unavailable for Drive recovery");
        return -1;
    }
    if (refresh_access_token(&session, error, error_size) < 0)
        return -1;
    r = snprintf(url, sizeof(url),
                 "https://www.googleapis.com/drive/v3/files/%s?alt=media", snapshot->id);
    if (r <= 0 || r >= (int)sizeof(url))
        return -1;

    /* Never reuse a stale interrupted download or a published file. */
    if (wleExists(result) || wleExists(stage) || wleExists(download)) {
        set_error(error, error_size, "A recovery filename was claimed; choose another");
        return -1;
    }
    fd = genOpen(download, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    if (fd < 0) {
        set_error(error, error_size, "Could not create staged cloud download");
        return -1;
    }
    r = cloudHttpDownloadGoogleFd(url, session.access_token, fd, session.ca_path,
                                  CLOUD_RECOVERY_MAX_BYTES, &bytes, digest,
                                  error, error_size);
    if (genClose(fd) < 0)
        r = -1;
    fd = -1;
    if (r < 0)
        goto fail;
    if (bytes <= 0 || !cloud_digest_matches(digest, snapshot->sha256)) {
        set_error(error, error_size, "Downloaded card SHA-256 differs from backup history");
        goto fail;
    }
    if ((is_pcsx2 ? mcCardValidatePcsx2(download) :
                     mcCardValidateNoEcc(download)) != MC_CARD_EXPORT_OK) {
        set_error(error, error_size, "Downloaded image failed PS2 card geometry verification");
        goto fail;
    }

    r = is_pcsx2 ? mcCardImportPcsx2(download, stage, NULL) :
                    mcCardBackupNoEcc(download, stage, NULL);
    if (r != MC_CARD_EXPORT_OK ||
        mcCardValidateNoEcc(stage) != MC_CARD_EXPORT_OK) {
        set_error(error, error_size, "Could not create a verified local card copy");
        goto fail;
    }
    if (wleExists(result) || fileXioRename(stage, result) < 0) {
        set_error(error, error_size, "Could not publish the verified recovery image");
        goto fail;
    }
    result_created = 1;
    if (mcCardValidateNoEcc(result) != MC_CARD_EXPORT_OK) {
        set_error(error, error_size, "Published card geometry verification failed");
        goto fail;
    }

    /* The original Drive item and local history remain untouched. */
    genRemove(download);
    snprintf(result_path, result_size, "%s", result);
    return 0;
fail:
    if (fd >= 0)
        genClose(fd);
    genRemove(download);
    genRemove(stage);
    if (result_created)
        genRemove(result);
    return -1;
}

static int drive_create_folder(GoogleSession *s, const char *parent, const char *name,
                               char *id, int id_size, char *error, int error_size)
{
    char escaped[MAX_NAME * 2], escaped_parent[CLOUD_ID_MAX * 2], body[1024];
    CloudHttpResponse response;

    if (json_escape(name, escaped, sizeof(escaped)) < 0 ||
        json_escape(parent, escaped_parent, sizeof(escaped_parent)) < 0)
        return -1;
    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"mimeType\":\"application/vnd.google-apps.folder\","
             "\"parents\":[\"%s\"]}", escaped, escaped_parent);
    if (drive_request(s, "POST", "/drive/v3/files?fields=id", "application/json",
                      body, &response, error, error_size) < 0)
        return -1;
    if ((response.status != 200 && response.status != 201) ||
        !json_string(response.body, "id", id, id_size)) {
        snprintf(error, error_size, "Drive folder creation returned HTTP %d", response.status);
        cloudHttpResponseFree(&response);
        return -1;
    }
    cloudHttpResponseFree(&response);
    return 0;
}

static int drive_ensure_folder(GoogleSession *s, const char *parent, const char *name,
                               char *id, int id_size, CloudGoogleSyncStats *stats,
                               char *error, int error_size)
{
    int r = drive_find_child(s, parent, name, 1, NULL, id, id_size, error, error_size);
    if (r != 0)
        return r < 0 ? -1 : 0;
    if (drive_create_folder(s, parent, name, id, id_size, error, error_size) < 0)
        return -1;
    stats->folders_created++;
    return 0;
}

static int is_google_upload_location(const char *url)
{
    static const char prefix[] = "https://www.googleapis.com/";
    return url != NULL && !strncmp(url, prefix, sizeof(prefix) - 1);
}

static int hash_open_backup(int fd, int file_size, char out[65])
{
    mbedtls_sha256_context ctx;
    unsigned char digest[32];
    unsigned char *buf;
    static const char hex[] = "0123456789abcdef";
    int done = 0, i, ok = 0;

    if (fd < 0 || file_size < 0 || genLseek(fd, 0, SEEK_SET) != 0)
        return -1;
    buf = memalign(64, 16 * 1024);
    if (!buf)
        return -1;

    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0)
        goto out;
    while (done < file_size) {
        int remaining = file_size - done;
        int want = remaining > 16 * 1024 ? 16 * 1024 : remaining;
        int got = genRead(fd, buf, want);
        if (got <= 0 || mbedtls_sha256_update(&ctx, buf, (size_t)got) != 0)
            goto out;
        done += got;
    }
    if (mbedtls_sha256_finish(&ctx, digest) != 0)
        goto out;
    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
    ok = 1;
out:
    mbedtls_sha256_free(&ctx);
    free(buf);
    if (genLseek(fd, 0, SEEK_SET) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

static int drive_upload_file(GoogleSession *s, const char *parent, const char *name,
                             const char *local_path, char *error, int error_size)
{
    char escaped[MAX_NAME * 2], escaped_parent[CLOUD_ID_MAX * 2], body[1280];
    char headers[CLOUD_TOKEN_MAX + 512], sha256[65], remote_id[CLOUD_ID_MAX];
    CloudHttpResponse response;
    s64 size64;
    int size, fd, r;

    if (json_escape(name, escaped, sizeof(escaped)) < 0 ||
        json_escape(parent, escaped_parent, sizeof(escaped_parent)) < 0)
        return -1;
    fd = genOpen(local_path, FIO_O_RDONLY);
    if (fd < 0) {
        set_error(error, error_size, "Could not open local backup for cloud sync");
        return -1;
    }
    size64 = genLseek(fd, 0, SEEK_END);
    if (size64 < 0 || size64 > 0x7fffffffLL) {
        genClose(fd);
        set_error(error, error_size, "Local backup is too large for cloud upload");
        return -1;
    }
    size = (int)size64;
    if (hash_open_backup(fd, size, sha256) < 0) {
        genClose(fd);
        set_error(error, error_size, "Could not hash local backup");
        return -1;
    }

    /* Remote identity is content-bound, not filename-only. A same-name file
     * with different bytes is intentionally preserved as another Drive item. */
    r = drive_find_child(s, parent, name, 0, sha256, remote_id,
                         sizeof(remote_id), error, error_size);
    if (r != 0) {
        genClose(fd);
        return r < 0 ? -1 : 1;
    }

    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"parents\":[\"%s\"],"
             "\"appProperties\":{\"wle_sha256\":\"%s\"}}",
             escaped, escaped_parent, sha256);
    snprintf(headers, sizeof(headers),
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json; charset=UTF-8\r\n"
             "X-Upload-Content-Type: application/octet-stream\r\n"
             "X-Upload-Content-Length: %d\r\n",
             s->access_token, size);

    if (cloudHttpRequest("POST", "www.googleapis.com",
                         "/upload/drive/v3/files?uploadType=resumable&fields=id",
                         headers, body, (int)strlen(body), s->ca_path,
                         &response, error, error_size) < 0) {
        genClose(fd);
        return -1;
    }
    if ((response.status != 200 && response.status != 201) || response.location[0] == 0) {
        snprintf(error, error_size, "Drive resumable upload start returned HTTP %d", response.status);
        cloudHttpResponseFree(&response);
        genClose(fd);
        return -1;
    }
    {
        char location[CLOUD_HTTP_LOCATION_MAX];
        strcopy(location, sizeof(location), response.location);
        cloudHttpResponseFree(&response);
        if (!is_google_upload_location(location)) {
            genClose(fd);
            set_error(error, error_size,
                      "Drive returned an unexpected resumable-upload host");
            return -1;
        }
        snprintf(headers, sizeof(headers),
                 "Authorization: Bearer %s\r\nContent-Type: application/octet-stream\r\n",
                 s->access_token);
        r = cloudHttpUploadFd("PUT", location, headers, fd, size, s->ca_path,
                              &response, error, error_size);
    }
    genClose(fd);
    if (r < 0)
        return -1;
    if (response.status != 200 && response.status != 201) {
        snprintf(error, error_size, "Drive upload returned HTTP %d", response.status);
        cloudHttpResponseFree(&response);
        return -1;
    }
    cloudHttpResponseFree(&response);
    return 0;
}
static int is_backup_file(const char *name)
{
    return name != NULL && !strncmp(name, "WLE-", 4) &&
           (genCmpFileExt(name, "MCD") || genCmpFileExt(name, "PS2"));
}

static int validate_backup_file(const char *name, const char *path)
{
    if (genCmpFileExt(name, "MCD"))
        return mcCardValidateNoEcc(path) == MC_CARD_EXPORT_OK;
    if (genCmpFileExt(name, "PS2"))
        return mcCardValidatePcsx2(path) == MC_CARD_EXPORT_OK;
    return 0;
}

static int sync_tree(GoogleSession *s, const char *local_dir, const char *remote_parent,
                     int depth, CloudGoogleSyncStats *stats, CloudGoogleProgress progress,
                     int *done, char *error, int error_size)
{
    iox_dirent_t ent;
    char scan[MAX_PATH], child[MAX_PATH], remote_id[CLOUD_ID_MAX];
    int fd;

    if (depth > CLOUD_TREE_DEPTH) {
        set_error(error, error_size, "Cloud backup tree is too deep");
        return -1;
    }
    snprintf(scan, sizeof(scan), "%s", local_dir);
    fd = genDopen(scan);
    if (fd < 0) {
        set_error(error, error_size, "Could not open local backup history");
        return -1;
    }
    memset(&ent, 0, sizeof(ent));
    while (fileXioDread(fd, &ent) > 0) {
        int r;
        if (!strcmp(ent.name, ".") || !strcmp(ent.name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (snprintf(child, sizeof(child), "%s%s%s", local_dir,
                     (local_dir[strlen(local_dir) - 1] == '/' ? "" : "/"),
                     ent.name) >= (int)sizeof(child)) {
            stats->failed++;
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (FIO_S_ISDIR(ent.stat.mode)) {
            if (drive_ensure_folder(s, remote_parent, ent.name, remote_id,
                                    sizeof(remote_id), stats, error, error_size) < 0 ||
                sync_tree(s, child, remote_id, depth + 1, stats, progress,
                          done, error, error_size) < 0) {
                genDclose(fd);
                return -1;
            }
        } else if (is_backup_file(ent.name)) {
            stats->files_seen++;
            (*done)++;
            if (progress)
                progress(ent.name, *done, 0);

            /* Exporters verify newly written backups byte-for-byte, but an
             * abrupt power loss can still leave a truncated final-named file.
             * Re-check exact card geometry/size before it crosses the cloud
             * trust boundary. Valid legacy history remains uploadable. */
            if (!validate_backup_file(ent.name, child)) {
                stats->failed++;
                memset(&ent, 0, sizeof(ent));
                continue;
            }

            r = drive_upload_file(s, remote_parent, ent.name, child,
                                  error, error_size);
            if (r < 0) {
                stats->failed++;
                genDclose(fd);
                return -1;
            }
            if (r > 0)
                stats->skipped++;
            else
                stats->uploaded++;
        }
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
    return 0;
}

int cloudGoogleSyncBackupRoot(const char *backup_root, CloudGoogleSyncStats *stats,
                              CloudGoogleProgress progress, char *error, int error_size)
{
    GoogleSession s;
    char local_root[MAX_PATH], remote_root[CLOUD_ID_MAX];
    int done = 0;

    if (stats == NULL || backup_root == NULL || backup_root[0] == 0)
        return -1;
    memset(stats, 0, sizeof(*stats));
    if (!strncmp(backup_root, "udpfs:", 6)) {
        set_error(error, error_size,
                  "UDPFS cannot be the cloud source because Google Drive uses the Ethernet stack.");
        return -1;
    }
    if (load_session(&s, 1, error, error_size) < 0)
        return -1;
    if (!ensureDashboardIpNetworkReady()) {
        set_error(error, error_size, "Ethernet could not be started");
        return -1;
    }
    if (!ensurePathDeviceStackReady(backup_root)) {
        set_error(error, error_size, "Could not start the backup destination device");
        return -1;
    }
    if (snprintf(local_root, sizeof(local_root), "%s%sWLE-MMCE-Backups",
                 backup_root,
                 (backup_root[strlen(backup_root) - 1] == '/' ||
                  backup_root[strlen(backup_root) - 1] == ':') ? "" : "/") >=
        (int)sizeof(local_root)) {
        set_error(error, error_size, "Backup path is too long");
        return -1;
    }
    {
        char probe[MAX_PATH];
        int dd;
        snprintf(probe, sizeof(probe), "%s", local_root);
        dd = genDopen(probe);
        if (dd < 0) {
            set_error(error, error_size, "No WLE-MMCE-Backups history exists yet");
            return -1;
        }
        genDclose(dd);
    }

    if (refresh_access_token(&s, error, error_size) < 0)
        return -1;
    if (drive_ensure_folder(&s, "root", "WLE-MMCE-Backups", remote_root,
                            sizeof(remote_root), stats, error, error_size) < 0)
        return -1;
    if (sync_tree(&s, local_root, remote_root, 0, stats, progress,
                  &done, error, error_size) < 0)
        return -1;
    return 0;
}
