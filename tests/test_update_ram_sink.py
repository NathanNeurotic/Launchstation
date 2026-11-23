"""The updater's RAM download sink at its capacity boundary.

Runs the real download_write and download_chunked_consume from src/cloud_http.c with fd
CLOUD_HTTP_MEM_FD: a fixed-length body of exactly cap bytes and of cap + 1, and a chunked
body of each size. A cap-sized body must land whole in the buffer with every byte hashed;
one byte more must be refused before anything is copied past the buffer.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/cloud_http.c").read_text()


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


decoder = re.search(r"typedef struct \{\n    char line\[40\];.*?\} CloudChunkDecoder;\n",
                    source, re.S).group(0)
sink = re.search(r"static unsigned char \*mem_sink;\n", source).group(0)

program = r'''
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CLOUD_HTTP_MEM_FD (-2)
#define CLOUD_HTTP_IO 16384
#define CLOUD_HTTP_HEADER_MAX 12288
#define HDD_INSTALL_ATA_TIMEOUT (-502)
static void hddInstallRecoveryObserve(int result) { (void)result; abort(); }
typedef struct { unsigned long sum; unsigned long bytes; } mbedtls_sha256_context;
static int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *d, size_t n) {
    for (size_t i = 0; i < n; i++) c->sum = c->sum * 131 + d[i];
    c->bytes += n;
    return 0;
}
static int genWrite(int fd, void *d, int n) { (void)fd; (void)d; (void)n; abort(); }
static void set_error(char *error, int error_size, const char *where, int code) {
    (void)code; snprintf(error, (size_t)error_size, "%s", where);
}
''' + decoder + sink + source[source.index("typedef struct {\n    unsigned char *bytes;"):
        source.index("static void download_progress(")] + function("static int download_write(") + \
    function("static int download_chunked_consume(") + function("static int download_framing(") + r'''
#define CAP 5000
static unsigned char payload[CAP + 1];
static unsigned char buffer[CAP + 16];

static unsigned long expected_sum(int n) {
    mbedtls_sha256_context c = {0, 0};
    mbedtls_sha256_update(&c, payload, (size_t)n);
    return c.sum;
}

/* Fixed length: the body arrives in 1,000-byte reads, the last one short. */
static int fixed(int n, int *written, mbedtls_sha256_context *sha, char *err) {
    int at = 0;
    while (at < n) {
        int take = n - at > 1000 ? 1000 : n - at;
        if (download_write(CLOUD_HTTP_MEM_FD, payload + at, take, CAP, written,
                           sha, NULL, err, 64) < 0)
            return -1;
        at += take;
    }
    return 0;
}

/* Chunked: 700-byte chunks, the stream fed to the decoder in 333-byte reads. */
static int chunked(int n, int *written, mbedtls_sha256_context *sha, char *err) {
    static unsigned char stream[2 * (CAP + 1) + 4096];
    int len = 0, at = 0, r = 0;
    CloudChunkDecoder dec;
    memset(&dec, 0, sizeof(dec));
    while (at < n) {
        int take = n - at > 700 ? 700 : n - at;
        len += sprintf((char *)stream + len, "%x\r\n", take);
        memcpy(stream + len, payload + at, (size_t)take);
        len += take;
        stream[len++] = '\r';
        stream[len++] = '\n';
        at += take;
    }
    len += sprintf((char *)stream + len, "0\r\n\r\n");
    for (at = 0; at < len && r == 0; at += 333) {
        int take = len - at > 333 ? 333 : len - at;
        r = download_chunked_consume(&dec, stream + at, take, CLOUD_HTTP_MEM_FD,
                                     CAP, written, sha, NULL, err, 64);
    }
    return r < 0 ? -1 : (r == 1 ? 0 : -2);
}

static void run(const char *name,
                int (*feed)(int, int *, mbedtls_sha256_context *, char *)) {
    for (int n = CAP; n <= CAP + 1; n++) {
        mbedtls_sha256_context sha = {0, 0};
        char err[64] = "";
        int written = 0, r;
        memset(buffer, 0xA5, sizeof(buffer));
        mem_sink = buffer;
        r = feed(n, &written, &sha, err);
        mem_sink = NULL;
        for (size_t i = CAP; i < sizeof(buffer); i++)
            assert(buffer[i] == 0xA5); /* nothing past the buffer */
        if (n == CAP) {
            assert(r == 0);
            assert(written == CAP);
            assert(!memcmp(buffer, payload, CAP));
            assert(sha.bytes == CAP && sha.sum == expected_sum(CAP));
        } else {
            assert(r == -1);
            assert(!strcmp(err, "HTTPS download exceeds the size limit"));
            assert(written <= CAP);
        }
    }
    printf("PASS %s: cap bytes kept and hashed, cap + 1 refused\n", name);
}

static void framing(void) {
    static const char *const malformed[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 3x",
        "HTTP/1.1 200 OK\r\nContent-Length: +3",
        "HTTP/1.1 200 OK\r\nContent-Length: 2147483648",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 3",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nTransfer-Encoding: chunked",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked",
    };
    int length = -1, chunked = 0;
    assert(download_framing("HTTP/1.1 200 OK\r\nContent-Length:\t3 ", &length, &chunked) == 0);
    assert(length == 3 && !chunked);
    length = -1;
    assert(download_framing("HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked \r\nX-Test: x", &length, &chunked) == 0);
    assert(length == -1 && chunked);
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        length = -1; chunked = 0;
        assert(download_framing(malformed[i], &length, &chunked) < 0);
    }
}

static void trailers(void) {
    CloudChunkDecoder decoder = {0};
    mbedtls_sha256_context sha = {0};
    char error[64];
    int written = 0;
    const unsigned char first[] = "0\r\nX-Test: ok\r\n\r";
    const unsigned char final[] = "\n";
    assert(download_chunked_consume(&decoder, first, sizeof(first)-1, CLOUD_HTTP_MEM_FD,
                                   CAP, &written, &sha, NULL, error, sizeof(error)) == 0);
    assert(download_chunked_consume(&decoder, final, sizeof(final)-1, CLOUD_HTTP_MEM_FD,
                                   CAP, &written, &sha, NULL, error, sizeof(error)) == 1);
    const unsigned char extra[] = "0\r\n\r\nx";
    memset(&decoder, 0, sizeof(decoder));
    assert(download_chunked_consume(&decoder, extra, sizeof(extra)-1, CLOUD_HTTP_MEM_FD,
                                   CAP, &written, &sha, NULL, error, sizeof(error)) < 0);
}

int main(void) {
    for (int i = 0; i <= CAP; i++) payload[i] = (unsigned char)(i * 7 + 3);
    run("fixed length", fixed);
    run("chunked", chunked);
    framing();
    trailers();
    puts("PASS: strict length and transfer framing, split chunk trailers and unexpected trailing bytes");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / "ram_sink.c"
    binary = Path(tmp) / "ram_sink"
    c_file.write_text(program)
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Werror", "-fsanitize=address,undefined",
                    "-o", str(binary), str(c_file)], check=True)
    print(subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout,
          end="")
