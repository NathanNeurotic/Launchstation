"""Check updater trust anchors and production TLS handshake failure handling.

Adapted from saildot4k/Launchstation (1d647f3) for src/cloud_http.c.

The C harness supplies deterministic Mbed TLS and PS2 peers. It exercises
verification policy, diagnostics and cleanup, not cryptography or console RPCs.
Live certificate-chain checks remain a separate manual validation.
"""
from pathlib import Path
import hashlib
import re
import shutil
import ssl
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/cloud_http.c").read_text()
bundle = (root / "res/WLE-UPDATE-CA.PEM").read_text()
certificates = re.findall(r"-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----", bundle)
fingerprints = {
    hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert)).hexdigest()
    for cert in certificates
}
required = {
    # GitHub API/website ECC chain; absent in the original failing bundle.
    "c90f26f0fb1b4018b22227519b5ca2b53e2ca5b3be5cf18efe1bef47380c5383",
    # Retained Sectigo RSA and Let's Encrypt release-download roots.
    "7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06",
    "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6",
}
assert required <= fingerprints, "Updater CA bundle lacks a required reviewed trust anchor"
assert len(certificates) == len(fingerprints), "Duplicate updater root certificates"
assert len(bundle.encode()) <= 256 * 1024


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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef uint32_t u32;
typedef struct { int sock, ssl, conf, ca, ctr, entropy; } CloudTls;
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED -9984
#define MBEDTLS_ERR_SSL_WANT_READ -10
#define MBEDTLS_ERR_SSL_WANT_WRITE -11
#define MBEDTLS_X509_BADCERT_EXPIRED 0x01u
#define MBEDTLS_X509_BADCERT_REVOKED 0x02u
#define MBEDTLS_X509_BADCERT_CN_MISMATCH 0x04u
#define MBEDTLS_X509_BADCERT_NOT_TRUSTED 0x08u
#define MBEDTLS_X509_BADCERT_FUTURE 0x0200u
#define MBEDTLS_SSL_IS_CLIENT 1
#define MBEDTLS_SSL_TRANSPORT_STREAM 2
#define MBEDTLS_SSL_PRESET_DEFAULT 3
#define MBEDTLS_SSL_VERIFY_REQUIRED 4
typedef int (*RngFn)(void *,unsigned char *,size_t);
static int handshake_result, parse_result, want_once, timeout;
static int required_mode, hostname_set, frees, handshakes, waits, verifies, connects, disconnects;
static uint32_t verify_flags;
static u32 cloud_entropy_network_ticks;
static const char cloud_google_roots_pem[] = "GTS";
#define INIT(name) static void name(int *p) { *p=1; }
INIT(mbedtls_ssl_init)
INIT(mbedtls_ssl_config_init)
INIT(mbedtls_x509_crt_init)
INIT(mbedtls_ctr_drbg_init)
INIT(mbedtls_entropy_init)
static u32 cpu_ticks(void) { return 17; }
static void mbedtls_strerror(int code,char *out,size_t cap) {
    snprintf(out,cap,"mock TLS error %d",code);
}
static unsigned char *read_file(const char *path,int *size) {
    assert(!strcmp(path,"WLE-UPDATE-CA.PEM"));
    unsigned char *out=malloc(3);assert(out);memcpy(out,"CA",3);*size=2;return out;
}
static int builtin_roots;
static int mbedtls_x509_crt_parse(int *ca,const unsigned char *pem,size_t n) {
    assert(*ca);
    if(pem==(const unsigned char *)cloud_google_roots_pem) {
        assert(n==sizeof(cloud_google_roots_pem));builtin_roots++;
    } else {
        assert(n==3 && !strcmp((const char *)pem,"CA"));
    }
    return parse_result;
}
static int ps2_tcp_connect(int *sock,const char *host) {
    assert(!strcmp(host,"api.github.com"));*sock=42;connects++;return 0;
}
static int mbedtls_entropy_func(void *ctx,unsigned char *out,size_t n) {
    (void)ctx;(void)out;(void)n;return 0;
}
static int mbedtls_ctr_drbg_random(void *ctx,unsigned char *out,size_t n) {
    (void)ctx;(void)out;(void)n;return 0;
}
static int mbedtls_ctr_drbg_seed(int *ctr,RngFn fn,void *entropy,const unsigned char *p,size_t n) {
    assert(*ctr && fn && entropy && p && n && connects==1);return 0;
}
static int mbedtls_ssl_config_defaults(int *conf,int client,int transport,int preset) {
    assert(*conf && client==MBEDTLS_SSL_IS_CLIENT && transport==MBEDTLS_SSL_TRANSPORT_STREAM);
    assert(preset==MBEDTLS_SSL_PRESET_DEFAULT);return 0;
}
static void mbedtls_ssl_conf_authmode(int *conf,int mode) {
    assert(*conf && mode==MBEDTLS_SSL_VERIFY_REQUIRED);required_mode++;
}
static void mbedtls_ssl_conf_ca_chain(int *conf,int *ca,void *crl) {
    assert(*conf && *ca && !crl);
}
static void mbedtls_ssl_conf_rng(int *conf,RngFn fn,void *ctr) {
    assert(*conf && fn && ctr);
}
static int mbedtls_ssl_setup(int *ssl,int *conf) { assert(*ssl && *conf);return 0; }
static int mbedtls_ssl_set_hostname(int *ssl,const char *host) {
    assert(*ssl && !strcmp(host,"api.github.com"));hostname_set++;return 0;
}
static int ps2_tls_send(void *ctx,const unsigned char *p,size_t n) {
    (void)ctx;(void)p;(void)n;return 0;
}
static int ps2_tls_recv(void *ctx,unsigned char *p,size_t n) {
    (void)ctx;(void)p;(void)n;return 0;
}
static void mbedtls_ssl_set_bio(int *ssl,void *sock,
                              int (*send)(void *,const unsigned char *,size_t),
                              int (*recv)(void *,unsigned char *,size_t),void *timeout_cb) {
    assert(*ssl && *(int *)sock==42 && send && recv && !timeout_cb);
}
static int mbedtls_ssl_handshake(int *ssl) {
    assert(*ssl && required_mode==1 && hostname_set==1);handshakes++;
    if(want_once) { want_once=0;return MBEDTLS_ERR_SSL_WANT_READ; }
    return handshake_result;
}
static uint32_t mbedtls_ssl_get_verify_result(const int *ssl) {
    assert(*ssl && handshakes && !frees);verifies++;return verify_flags;
}
#if !defined(MBEDTLS_X509_REMOVE_INFO)
static int mbedtls_x509_crt_verify_info(char *out,size_t cap,const char *prefix,uint32_t flags) {
    (void)prefix;assert(flags==MBEDTLS_X509_BADCERT_REVOKED);
    return snprintf(out,cap,"certificate revoked");
}
#endif
static int wait_tls_io(CloudTls *tls,int result) {
    assert(tls->sock==42 && result==MBEDTLS_ERR_SSL_WANT_READ);waits++;return timeout?-1:0;
}
static int ps2ipc_disconnect(int sock) { assert(sock==42);disconnects++;return 0; }
static void mbedtls_ssl_free(int *ssl) { assert(*ssl);frees++; }
static void mbedtls_ssl_config_free(int *conf) { assert(*conf); }
static void mbedtls_x509_crt_free(int *ca) { assert(*ca); }
static void mbedtls_ctr_drbg_free(int *ctr) { assert(*ctr); }
static void mbedtls_entropy_free(int *entropy) { assert(*entropy); }
#define mbedtls_ssl_close_notify(ssl) assert(!"cleanup must not write to a failed TLS session")
'''
program += function("static void set_error(")
program += function("static void tls_free(")
program += function("static void tls_verify_error(")
program += function("static int tls_open(")
program += r'''
static void reset(int result,uint32_t flags) {
    handshake_result=result;verify_flags=flags;
    parse_result=want_once=timeout=0;
    required_mode=hostname_set=frees=handshakes=waits=verifies=connects=disconnects=0;
}
static void expect_failure(uint32_t flags,const char *detail) {
    CloudTls tls;char error[256];
    reset(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED,flags);
    assert(tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error))<0);
    assert(required_mode==1 && hostname_set==1 && verifies && frees==1);
    assert(strstr(error,detail));
}
int main(void) {
    CloudTls tls;char error[256];
    reset(0,0);
    assert(!tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error)));
    assert(required_mode==1 && hostname_set==1 && verifies==1 && !frees);
    expect_failure(MBEDTLS_X509_BADCERT_NOT_TRUSTED,"CA bundle beside the ELF");
    expect_failure(MBEDTLS_X509_BADCERT_EXPIRED,"verify the PS2 date and time");
    expect_failure(MBEDTLS_X509_BADCERT_FUTURE,"verify the PS2 date and time");
    expect_failure(MBEDTLS_X509_BADCERT_CN_MISMATCH,"hostname check failed for api.github.com");
#if !defined(MBEDTLS_X509_REMOVE_INFO)
    expect_failure(MBEDTLS_X509_BADCERT_REVOKED,"certificate revoked");
#else
    expect_failure(MBEDTLS_X509_BADCERT_REVOKED,"TLS certificate verification failed");
#endif
    expect_failure((uint32_t)-1,"TLS certificate verification failed");
    expect_failure(0,"TLS certificate verification failed");
    reset(-1234,(uint32_t)-1);
    assert(tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error))<0);
    assert(strstr(error,"TLS handshake") && !strstr(error,"PS2 date") && !verifies && frees==1);
    reset(0,MBEDTLS_X509_BADCERT_NOT_TRUSTED);
    assert(tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error))<0);
    assert(strstr(error,"api.github.com") && strstr(error,"CA bundle") && frees==1);
    reset(0,0);want_once=1;
    assert(!tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error)));
    assert(handshakes==2 && waits==1 && !frees);
    reset(0,0);want_once=timeout=1;
    assert(tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error))<0);
    assert(strstr(error,"handshake timed out") && frees==1);
    reset(0,0);parse_result=-1;
    assert(tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error))<0);
    assert(strstr(error,"parse cloud CA bundle") && !connects && !handshakes && frees==1);
    /* Cloud callers without an override file use the built-in roots. */
    reset(0,0);builtin_roots=0;
    assert(!tls_open(&tls,"api.github.com","",error,sizeof(error)));
    assert(builtin_roots==1 && verifies==1 && !frees);
    /* Repeated fatal sessions release TCP and every TLS context before retry. */
    for(int i=0;i<100;i++) {
        reset(-29056,(uint32_t)-1);
        assert(tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error))<0);
        assert(disconnects==1 && frees==1 && tls.sock==-1);
        reset(0,0);
        assert(!tls_open(&tls,"api.github.com","WLE-UPDATE-CA.PEM",error,sizeof(error)));
        tls_free(&tls);
        assert(disconnects==1 && frees==1 && tls.sock==-1);
    }
    puts("PASS: reviewed updater roots; strict TLS, failed-handshake diagnostics, timeouts and cleanup");
    return 0;
}
'''

compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-tls-") as tmp:
    src, binary = Path(tmp) / "tls.c", Path(tmp) / "tls"
    src.write_text(program)
    for flags in ([], ["-DMBEDTLS_X509_REMOVE_INFO"]):
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        *flags, str(src), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=5)
