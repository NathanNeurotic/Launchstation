"""The HTTPS body cap around the update check's release JSON.

GitHub's /releases/tags/latest answer carries the full release notes, so it grows with them
(62,972 bytes for build 1104 on 2026-11-09, against an old 64 KiB cap). This runs the real
read_response from src/cloud_http.c on a scripted TLS stream: a body the size of today's
release JSON, four times that, exactly CLOUD_HTTP_BODY_MAX, one byte over (refused from the
Content-Length header before the body arrives), a chunked body over the cap, and a body
that stops short of its Content-Length.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/cloud_http.c").read_text()
header = (root / "include/cloud_http.h").read_text()

LIVE_RELEASE_JSON = 62972  # bytes, measured 2026-11-09 (Launchstation build 1104)
cap = eval(re.search(r"#define CLOUD_HTTP_BODY_MAX \(([^)]*)\)", header).group(1))
assert cap >= 4 * LIVE_RELEASE_JSON, "CLOUD_HTTP_BODY_MAX leaves less than 4x the release JSON"
header_max = re.search(r"#define CLOUD_HTTP_HEADER_MAX (\d+)", source).group(1)


def function(signature):
    start = source.index(signature)
    depth = 0
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',
                         source[start:], re.S)
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:start + token.end()] + "\n"
    raise AssertionError("Unclosed function: " + signature)


program = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "cloud_http.h"
#define CLOUD_HTTP_HEADER_MAX ''' + header_max + r'''
#define MBEDTLS_ERR_SSL_WANT_READ -10
#define MBEDTLS_ERR_SSL_WANT_WRITE -11
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY -12
typedef struct { int ssl; } CloudTls;
static char *stream; static int stream_len, stream_pos, reads;
static int mbedtls_ssl_read(int *ssl, unsigned char *out, size_t n) {
    (void)ssl;
    if (stream_pos >= stream_len) return 0;
    int take = stream_len - stream_pos;
    if (take > 16384) take = 16384; /* one TLS record at a time */
    if ((size_t)take > n) take = (int)n;
    memcpy(out, stream + stream_pos, take); stream_pos += take; reads++;
    return take;
}
static int wait_tls_io(CloudTls *tls, int r) { (void)tls; (void)r; return 0; }
static void set_error(char *error, int error_size, const char *where, int code) {
    (void)code; snprintf(error, error_size, "%s", where);
}
''' + function("static int header_value(") + function("static int decode_chunked(") + \
    function("static int read_response(") + function("void cloudHttpResponseFree(") + r'''
static int run(const char *head, int body_len, int send_len, CloudHttpResponse *r, char *err) {
    int hl = (int)strlen(head);
    free(stream);
    stream = malloc(hl + send_len + 1);
    memcpy(stream, head, hl);
    for (int i = 0; i < send_len; i++) stream[hl + i] = 'a' + i % 26;
    stream_len = hl + send_len; stream_pos = 0; reads = 0;
    memset(r, 0, sizeof(*r)); err[0] = 0;
    (void)body_len;
    CloudTls tls = {0};
    return read_response(&tls, r, err, 128);
}
static int plain(int body_len, int send_len, CloudHttpResponse *r, char *err) {
    char head[160];
    snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n", body_len);
    return run(head, body_len, send_len, r, err);
}
int main(int argc, char **argv) {
    (void)argc;
    CloudHttpResponse r; char err[128];
    int live = atoi(argv[1]);
    int sizes[] = { live, 4 * live, CLOUD_HTTP_BODY_MAX };
    for (int i = 0; i < 3; i++) {
        assert(plain(sizes[i], sizes[i], &r, err) == 0);
        assert(r.status == 200 && r.body_len == sizes[i] && r.body[sizes[i]] == 0);
        assert(r.body[0] == 'a' && r.body[sizes[i] - 1] == 'a' + (sizes[i] - 1) % 26);
        cloudHttpResponseFree(&r);
    }
    /* One byte over: refused from the header, before the body is read. */
    assert(plain(CLOUD_HTTP_BODY_MAX + 1, CLOUD_HTTP_BODY_MAX + 1, &r, err) < 0);
    assert(strcmp(err, "HTTPS response body is too large") == 0 && r.body == NULL && reads == 1);
    /* Short body. */
    assert(plain(1000, 999, &r, err) < 0 && strcmp(err, "Truncated HTTPS response body") == 0);
    /* Chunked with no length: bounded by the receive buffer. */
    {
        int n = CLOUD_HTTP_BODY_MAX + CLOUD_HTTP_HEADER_MAX;
        char head[96]; snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%x\r\n", n);
        assert(run(head, n, n, &r, err) < 0 && strcmp(err, "HTTPS response exceeded the receive limit") == 0);
    }
    free(stream);
    puts("https body cap: live release JSON, 4x, exact cap, cap+1, short body and chunked overflow passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as temp:
    c = Path(temp) / "cap.c"
    exe = Path(temp) / "cap"
    c.write_text(program)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "include"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(LIVE_RELEASE_JSON)], check=True, timeout=30)
