//--------------------------------------------------------------
// Native HTTPS transport for wLaunchELF cloud backups.
// Original implementation against public HTTPS/TLS interfaces.
//--------------------------------------------------------------
#include "launchelf.h"
#include "cloud_http.h"

#include <ctype.h>
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <ps2ips.h>
#include <timer.h>
#include <unistd.h>
#include <time.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#define CLOUD_HTTP_HEADER_MAX 12288
#define CLOUD_HTTP_IO 16384
#define CLOUD_IO_TIMEOUT_MS 15000
#define CLOUD_IPPROTO_TCP 6
#define CLOUD_IOCPARM_MASK 0x7fU
#define CLOUD_IOC_IN 0x80000000UL
#define CLOUD_FIONBIO (CLOUD_IOC_IN | ((((long)sizeof(unsigned long)) & CLOUD_IOCPARM_MASK) << 16) | ('f' << 8) | 126)

/* Use libps2ips' RPC entry points directly. ps2build does not install the
 * internal ps2ipc.h header, so keep the small public ABI surface declared here.
 * The IPv4 sockaddr and hostent views match the PS2IP RPC wire layout. */
struct hostent;
extern int ps2ipc_disconnect(int s);
extern int ps2ipc_connect(int s, const struct sockaddr *name, int namelen);
extern int ps2ipc_recv(int s, void *mem, int len, unsigned int flags);
extern int ps2ipc_send(int s, const void *dataptr, int size, unsigned int flags);
extern int ps2ipc_socket(int domain, int type, int protocol);
extern int ps2ipc_select(int maxfdp1, fd_set *readset, fd_set *writeset,
                         fd_set *exceptset, struct timeval *timeout);
extern int ps2ipc_ioctl(int s, long cmd, void *argp);
extern int ps2ipc_getpeername(int s, struct sockaddr *name, int *namelen);
extern struct hostent *ps2ipc_gethostbyname(const char *name);

typedef struct {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
} CloudHostEnt;

typedef struct {
    u8 sin_len;
    u8 sin_family;
    u16 sin_port;
    u32 sin_addr;
    char sin_zero[8];
} CloudSockaddrIn;

static u16 cloud_htons(u16 value)
{
    return (u16)((value << 8) | (value >> 8));
}


typedef struct {
    int sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_ctr_drbg_context ctr;
    mbedtls_entropy_context entropy;
} CloudTls;

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    clock_t ticks = clock();
    if (ticks == (clock_t)-1 || CLOCKS_PER_SEC <= 0)
        return 0;
    return (mbedtls_ms_time_t)(((u64)ticks * 1000ULL) / (u64)CLOCKS_PER_SEC);
}

static u64 cloud_entropy_counter;
static u32 cloud_entropy_network_ticks;

typedef struct {
    unsigned char sdk_random[32];
    u32 cpu_ticks_sample[32];
    clock_t clock_sample[8];
    sceCdCLOCK rtc;
    u64 timer_ms;
    u64 counter;
    u32 network_ticks;
} CloudEntropyMaterial;

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    size_t produced = 0;
    (void)data;

    if (olen == NULL || output == NULL)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = 0;

    /* PS2SDK's getrandom() ultimately depends on a weak _getentropy()
     * implementation, so never present it to TLS as the sole source. Fold it
     * together with full-width COP0 Count samples, RTC state, user/action
     * timing carried by Timer(), a monotonic local counter, and DNS/TCP timing,
     * then condition each block through SHA-256. This is defense-in-depth for
     * hardware without a documented TRNG, not a claim that cpu_ticks is one. */
    while (produced < len) {
        CloudEntropyMaterial material;
        unsigned char digest[32];
        ssize_t got;
        size_t take;
        int i;

        memset(&material, 0, sizeof(material));
        got = getrandom(material.sdk_random, sizeof(material.sdk_random), 0);
        if (got < 0)
            memset(material.sdk_random, 0, sizeof(material.sdk_random));

        for (i = 0; i < 32; i++) {
            material.cpu_ticks_sample[i] = cpu_ticks();
            if ((i & 3) == 0)
                material.clock_sample[i >> 2] = clock();
        }
        memset(&material.rtc, 0, sizeof(material.rtc));
        sceCdReadClock(&material.rtc);
        material.timer_ms = Timer();
        material.counter = ++cloud_entropy_counter;
        material.network_ticks = cloud_entropy_network_ticks ^ cpu_ticks();

        if (mbedtls_sha256((const unsigned char *)&material, sizeof(material),
                           digest, 0) != 0)
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;

        take = len - produced;
        if (take > sizeof(digest))
            take = sizeof(digest);
        memcpy(output + produced, digest, take);
        produced += take;
    }

    *olen = produced;
    return 0;
}

static int ps2_tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    int sock = *(int *)ctx;
    int r;

    if (len > INT_MAX)
        len = INT_MAX;
    r = ps2ipc_send(sock, buf, (int)len, 0);
    return r < 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : r;
}

static int ps2_tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    int sock = *(int *)ctx;
    int r;

    if (len > INT_MAX)
        len = INT_MAX;
    r = ps2ipc_recv(sock, buf, (int)len, 0);
    return r < 0 ? MBEDTLS_ERR_SSL_WANT_READ : r;
}

static int socket_wait(int sock, int want_write, int timeout_ms)
{
    fd_set readfds, writefds, exceptfds;
    struct timeval tv;
    int r;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    if (want_write)
        FD_SET(sock, &writefds);
    else
        FD_SET(sock, &readfds);
    FD_SET(sock, &exceptfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    r = ps2ipc_select(sock + 1,
                      want_write ? NULL : &readfds,
                      want_write ? &writefds : NULL,
                      &exceptfds, &tv);
    if (r <= 0)
        return r;
    if (FD_ISSET(sock, &exceptfds))
        return -1;
    return FD_ISSET(sock, want_write ? &writefds : &readfds) ? 1 : 0;
}

static int wait_tls_io(CloudTls *tls, int result)
{
    int want_write = result == MBEDTLS_ERR_SSL_WANT_WRITE;
    return socket_wait(tls->sock, want_write, CLOUD_IO_TIMEOUT_MS) > 0 ? 0 : -1;
}

static int ps2_tcp_connect(int *sock, const char *host)
{
    CloudHostEnt *entry;
    CloudSockaddrIn addr, peer;
    u32 nonblock = 1;
    int peer_len;
    int r;

    if (sock == NULL || host == NULL)
        return -1;
    entry = (CloudHostEnt *)(void *)ps2ipc_gethostbyname(host);
    if (entry == NULL || entry->h_addr_list == NULL || entry->h_addr_list[0] == NULL)
        return -1;

    *sock = ps2ipc_socket(AF_INET, SOCK_STREAM, CLOUD_IPPROTO_TCP);
    if (*sock < 0)
        return -1;
    if (ps2ipc_ioctl(*sock, CLOUD_FIONBIO, &nonblock) < 0) {
        ps2ipc_disconnect(*sock);
        *sock = -1;
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = cloud_htons(443);
    memcpy(&addr.sin_addr, entry->h_addr_list[0], sizeof(addr.sin_addr));

    r = ps2ipc_connect(*sock, (struct sockaddr *)&addr, sizeof(addr));
    if (r < 0) {
        if (socket_wait(*sock, 1, CLOUD_IO_TIMEOUT_MS) <= 0) {
            ps2ipc_disconnect(*sock);
            *sock = -1;
            return -1;
        }
        memset(&peer, 0, sizeof(peer));
        peer_len = sizeof(peer);
        if (ps2ipc_getpeername(*sock, (struct sockaddr *)&peer, &peer_len) < 0) {
            ps2ipc_disconnect(*sock);
            *sock = -1;
            return -1;
        }
    }
    return 0;
}

static void set_error(char *error, int error_size, const char *where, int code)
{
    char detail[128];
    if (error == NULL || error_size <= 0)
        return;
    if (code != 0) {
        mbedtls_strerror(code, detail, sizeof(detail));
        snprintf(error, error_size, "%s: %s (%d)", where, detail, code);
    } else
        snprintf(error, error_size, "%s", where);
}

static unsigned char *read_file(const char *path, int *size)
{
    unsigned char *buf;
    s64 len;
    int fd, done = 0;

    if (size)
        *size = 0;
    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0)
        return NULL;
    len = genLseek(fd, 0, SEEK_END);
    if (len <= 0 || len > 256 * 1024 || genLseek(fd, 0, SEEK_SET) != 0) {
        genClose(fd);
        return NULL;
    }
    buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        genClose(fd);
        return NULL;
    }
    while (done < len) {
        int r = genRead(fd, buf + done, (int)(len - done));
        if (r <= 0) {
            free(buf);
            genClose(fd);
            return NULL;
        }
        done += r;
    }
    genClose(fd);
    buf[len] = 0;
    if (size)
        *size = (int)len;
    return buf;
}

static void tls_free(CloudTls *tls)
{
    if (tls == NULL)
        return;
    mbedtls_ssl_close_notify(&tls->ssl);
    if (tls->sock >= 0) {
        ps2ipc_disconnect(tls->sock);
        tls->sock = -1;
    }
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_x509_crt_free(&tls->ca);
    mbedtls_ctr_drbg_free(&tls->ctr);
    mbedtls_entropy_free(&tls->entropy);
}

static int tls_open(CloudTls *tls, const char *host, const char *ca_path,
                    char *error, int error_size)
{
    static const unsigned char personal[] = "wLaunchELF native cloud";
    unsigned char *ca_data = NULL;
    int ca_len = 0;
    u32 network_start;
    int r;

    memset(tls, 0, sizeof(*tls));
    tls->sock = -1;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_x509_crt_init(&tls->ca);
    mbedtls_ctr_drbg_init(&tls->ctr);
    mbedtls_entropy_init(&tls->entropy);

    ca_data = read_file(ca_path, &ca_len);
    if (ca_data == NULL) {
        set_error(error, error_size, "Could not read WLE-CLOUD-CA.PEM", 0);
        goto fail;
    }
    r = mbedtls_x509_crt_parse(&tls->ca, ca_data, (size_t)ca_len + 1);
    free(ca_data);
    ca_data = NULL;
    if (r < 0) {
        set_error(error, error_size, "Could not parse cloud CA bundle", r);
        goto fail;
    }

    /* Establish TCP first so DNS/connect timing contributes to the entropy
     * conditioner. No TLS data or OAuth credential is sent before the DRBG is
     * successfully seeded and the authenticated handshake completes. */
    network_start = cpu_ticks();
    r = ps2_tcp_connect(&tls->sock, host);
    cloud_entropy_network_ticks ^= (cpu_ticks() - network_start) ^ cpu_ticks();
    if (r != 0) {
        set_error(error, error_size, "TCP connection", 0);
        goto fail;
    }
    r = mbedtls_ctr_drbg_seed(&tls->ctr, mbedtls_entropy_func, &tls->entropy,
                              personal, sizeof(personal) - 1);
    if (r != 0) {
        set_error(error, error_size, "TLS random generator", r);
        goto fail;
    }
    r = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) {
        set_error(error, error_size, "TLS configuration", r);
        goto fail;
    }
    mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls->conf, &tls->ca, NULL);
    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctr);
    r = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    if (r != 0) {
        set_error(error, error_size, "TLS setup", r);
        goto fail;
    }
    r = mbedtls_ssl_set_hostname(&tls->ssl, host);
    if (r != 0) {
        set_error(error, error_size, "TLS hostname", r);
        goto fail;
    }
    mbedtls_ssl_set_bio(&tls->ssl, &tls->sock, ps2_tls_send, ps2_tls_recv, NULL);

    while ((r = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (wait_tls_io(tls, r) == 0)
                continue;
            set_error(error, error_size, "TLS handshake timed out", 0);
            goto fail;
        }
        set_error(error, error_size, "TLS handshake", r);
        goto fail;
    }
    {
        uint32_t verify_flags = mbedtls_ssl_get_verify_result(&tls->ssl);
        if (verify_flags != 0) {
            if (verify_flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) {
                set_error(error, error_size,
                          "TLS certificate date check failed; verify the PS2 date and time", 0);
            } else {
#if !defined(MBEDTLS_X509_REMOVE_INFO)
                char verify_info[192];
                if (mbedtls_x509_crt_verify_info(verify_info, sizeof(verify_info), "",
                                                 verify_flags) >= 0)
                    snprintf(error, error_size, "TLS certificate verification: %.160s", verify_info);
                else
#endif
                    set_error(error, error_size, "TLS certificate verification failed", 0);
            }
            goto fail;
        }
    }
    return 0;

fail:
    free(ca_data);
    tls_free(tls);
    return -1;
}

static int ssl_write_all(CloudTls *tls, const void *data, int len,
                         char *error, int error_size)
{
    const unsigned char *p = data;
    int done = 0;
    while (done < len) {
        int r = mbedtls_ssl_write(&tls->ssl, p + done, (size_t)(len - done));
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (wait_tls_io(tls, r) == 0)
                continue;
            set_error(error, error_size, "HTTPS write timed out", 0);
            return -1;
        }
        if (r <= 0) {
            set_error(error, error_size, "HTTPS write", r);
            return -1;
        }
        done += r;
    }
    return 0;
}

static int header_value(const char *headers, const char *name, char *out, int out_size)
{
    const char *p = headers;
    size_t n = strlen(name);

    while (p && *p) {
        const char *e = strstr(p, "\r\n");
        int line_len = e ? (int)(e - p) : (int)strlen(p);
        if (line_len > (int)n && !strncasecmp(p, name, n) && p[n] == ':') {
            const char *v = p + n + 1;
            int copy;
            while (*v == ' ' || *v == '\t')
                v++;
            copy = line_len - (int)(v - p);
            if (copy >= out_size)
                copy = out_size - 1;
            memcpy(out, v, copy);
            out[copy] = 0;
            return 1;
        }
        p = e ? e + 2 : NULL;
    }
    if (out_size > 0)
        out[0] = 0;
    return 0;
}

static int decode_chunked(char *buf, int len)
{
    char *r = buf, *w = buf, *end = buf + len;
    while (r < end) {
        char *line_end = strstr(r, "\r\n");
        unsigned long chunk;
        char *num_end;
        if (line_end == NULL)
            return -1;
        *line_end = 0;
        chunk = strtoul(r, &num_end, 16);
        if (num_end == r)
            return -1;
        r = line_end + 2;
        if (chunk == 0) {
            *w = 0;
            return (int)(w - buf);
        }
        if (chunk > (unsigned long)(end - r) || w + chunk > buf + len)
            return -1;
        memmove(w, r, chunk);
        w += chunk;
        r += chunk;
        if (r + 2 > end || r[0] != '\r' || r[1] != '\n')
            return -1;
        r += 2;
    }
    return -1;
}

static int read_response(CloudTls *tls, CloudHttpResponse *response,
                         char *error, int error_size)
{
    char *raw, *header_end, value[64];
    int cap = CLOUD_HTTP_HEADER_MAX + CLOUD_HTTP_BODY_MAX + 1;
    int used = 0, header_len, body_len;
    int content_length = -1;
    int chunked = 0;

    raw = malloc(cap);
    if (raw == NULL) {
        set_error(error, error_size, "Out of memory reading HTTPS response", 0);
        return -1;
    }

    while (used < cap - 1) {
        int r = mbedtls_ssl_read(&tls->ssl, (unsigned char *)raw + used, (size_t)(cap - 1 - used));
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (wait_tls_io(tls, r) == 0)
                continue;
            free(raw);
            set_error(error, error_size, "HTTPS read timed out", 0);
            return -1;
        }
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0)
            break;
        if (r < 0) {
            free(raw);
            set_error(error, error_size, "HTTPS read", r);
            return -1;
        }
        used += r;
        raw[used] = 0;
        header_end = strstr(raw, "\r\n\r\n");
        if (header_end != NULL) {
            char saved = *header_end;
            *header_end = 0;
            if (header_value(raw, "Content-Length", value, sizeof(value)))
                content_length = atoi(value);
            if (header_value(raw, "Transfer-Encoding", value, sizeof(value)) &&
                strstr(value, "chunked") != NULL)
                chunked = 1;
            *header_end = saved;
            header_len = (int)(header_end + 4 - raw);
            if (header_len > CLOUD_HTTP_HEADER_MAX) {
                free(raw);
                set_error(error, error_size, "HTTPS response headers are too large", 0);
                return -1;
            }
            if (!chunked && content_length > CLOUD_HTTP_BODY_MAX) {
                free(raw);
                set_error(error, error_size, "HTTPS response body is too large", 0);
                return -1;
            }
            if (!chunked && content_length >= 0 && used >= header_len + content_length)
                break;
        }
    }
    if (used == cap - 1) {
        free(raw);
        set_error(error, error_size, "HTTPS response exceeded the receive limit", 0);
        return -1;
    }
    raw[used] = 0;
    header_end = strstr(raw, "\r\n\r\n");
    if (header_end == NULL) {
        free(raw);
        set_error(error, error_size, "Malformed HTTPS response", 0);
        return -1;
    }
    if (sscanf(raw, "HTTP/%*d.%*d %d", &response->status) != 1) {
        free(raw);
        set_error(error, error_size, "Missing HTTPS status", 0);
        return -1;
    }

    header_len = (int)(header_end + 4 - raw);
    body_len = used - header_len;
    *header_end = 0;

    if (header_len > CLOUD_HTTP_HEADER_MAX) {
        free(raw);
        set_error(error, error_size, "HTTPS response headers are too large", 0);
        return -1;
    }
    if (!chunked && header_value(raw, "Content-Length", value, sizeof(value))) {
        int declared = atoi(value);
        if (declared < 0 || declared > CLOUD_HTTP_BODY_MAX) {
            free(raw);
            set_error(error, error_size, "HTTPS response body is too large", 0);
            return -1;
        }
        if (body_len < declared) {
            free(raw);
            set_error(error, error_size, "Truncated HTTPS response body", 0);
            return -1;
        }
    }

    header_value(raw, "Location", response->location, sizeof(response->location));
    if (header_value(raw, "Transfer-Encoding", value, sizeof(value)) &&
        strstr(value, "chunked") != NULL) {
        int decoded = decode_chunked(raw + header_len, body_len);
        if (decoded < 0) {
            free(raw);
            set_error(error, error_size, "Malformed chunked HTTPS response", 0);
            return -1;
        }
        body_len = decoded;
    } else if (header_value(raw, "Content-Length", value, sizeof(value))) {
        int declared = atoi(value);
        if (declared >= 0 && declared < body_len)
            body_len = declared;
    }

    response->body = malloc((size_t)body_len + 1);
    if (response->body == NULL) {
        free(raw);
        set_error(error, error_size, "Out of memory storing HTTPS response", 0);
        return -1;
    }
    memcpy(response->body, raw + header_len, body_len);
    response->body[body_len] = 0;
    response->body_len = body_len;
    free(raw);
    return 0;
}

void cloudHttpResponseFree(CloudHttpResponse *response)
{
    if (response == NULL)
        return;
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static int request_open(CloudTls *tls, const char *method, const char *host,
                        const char *path, const char *headers, int content_len,
                        const char *ca_path, char *error, int error_size)
{
    char *request;
    int len, cap;

    if (tls_open(tls, host, ca_path, error, error_size) < 0)
        return -1;
    cap = 1024 + (headers ? (int)strlen(headers) : 0) + (int)strlen(path) + (int)strlen(host);
    request = malloc(cap);
    if (request == NULL) {
        tls_free(tls);
        set_error(error, error_size, "Out of memory creating HTTPS request", 0);
        return -1;
    }
    len = snprintf(request, cap,
                   "%s %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "User-Agent: wLaunchELF-R3Z/Cloud\r\n"
                   "Accept: application/json\r\n"
                   "Connection: close\r\n"
                   "%s"
                   "Content-Length: %d\r\n\r\n",
                   method, path, host, headers ? headers : "", content_len);
    if (len <= 0 || len >= cap || ssl_write_all(tls, request, len, error, error_size) < 0) {
        free(request);
        tls_free(tls);
        return -1;
    }
    free(request);
    return 0;
}

int cloudHttpRequest(const char *method, const char *host, const char *path,
                     const char *headers, const void *body, int body_len,
                     const char *ca_path, CloudHttpResponse *response,
                     char *error, int error_size)
{
    CloudTls tls;

    if (response == NULL || method == NULL || host == NULL || path == NULL ||
        ca_path == NULL || body_len < 0)
        return -1;
    memset(response, 0, sizeof(*response));
    if (request_open(&tls, method, host, path, headers, body_len, ca_path, error, error_size) < 0)
        return -1;
    if (body_len > 0 && ssl_write_all(&tls, body, body_len, error, error_size) < 0) {
        tls_free(&tls);
        return -1;
    }
    if (read_response(&tls, response, error, error_size) < 0) {
        tls_free(&tls);
        return -1;
    }
    tls_free(&tls);
    return 0;
}

static int split_https_url(const char *url, char *host, int host_size,
                           char *path, int path_size)
{
    const char *p, *slash;
    int host_len;
    if (url == NULL || strncmp(url, "https://", 8))
        return -1;
    p = url + 8;
    slash = strchr(p, '/');
    if (slash == NULL)
        slash = p + strlen(p);
    host_len = (int)(slash - p);
    if (host_len <= 0 || host_len >= host_size)
        return -1;
    memcpy(host, p, host_len);
    host[host_len] = 0;
    if (*slash)
        snprintf(path, path_size, "%s", slash);
    else
        snprintf(path, path_size, "/");
    return 0;
}

int cloudHttpUploadFd(const char *method, const char *url,
                      const char *headers, int fd, int file_size,
                      const char *ca_path, CloudHttpResponse *response,
                      char *error, int error_size)
{
    CloudTls tls;
    char host[256], path[1024];
    unsigned char *buf;
    int sent = 0;

    if (fd < 0 || file_size < 0 ||
        split_https_url(url, host, sizeof(host), path, sizeof(path)) < 0) {
        set_error(error, error_size, "Invalid HTTPS upload source", 0);
        return -1;
    }
    if (genLseek(fd, 0, SEEK_SET) != 0) {
        set_error(error, error_size, "Could not rewind local backup", 0);
        return -1;
    }
    memset(response, 0, sizeof(*response));
    if (request_open(&tls, method, host, path, headers, file_size, ca_path, error, error_size) < 0)
        return -1;

    buf = memalign(64, CLOUD_HTTP_IO);
    if (buf == NULL) {
        tls_free(&tls);
        set_error(error, error_size, "Out of memory streaming cloud upload", 0);
        return -1;
    }
    while (sent < file_size) {
        int remaining = file_size - sent;
        int want = remaining > CLOUD_HTTP_IO ? CLOUD_HTTP_IO : remaining;
        int got = genRead(fd, buf, want);
        if (got <= 0 || ssl_write_all(&tls, buf, got, error, error_size) < 0) {
            free(buf);
            tls_free(&tls);
            return -1;
        }
        sent += got;
    }
    free(buf);
    if (read_response(&tls, response, error, error_size) < 0) {
        tls_free(&tls);
        return -1;
    }
    tls_free(&tls);
    return 0;
}
