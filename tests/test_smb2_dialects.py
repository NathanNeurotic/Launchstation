"""smb2man exact-dialect selection around its existing two-stage negotiation."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "iop/smb2man/smb2man.c").read_text()


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMB2_CONNECT_ERR_CONN 1002
#define SMB2_CONNECT_ERR_PROT 1003
#define SMB2_CONNECT_ERR_SIGNING 1004
#define SMB2_CONNECT_ERR_LOGON 1005
#define SMB2_CONNECT_ERR_SHARE 1006
#define SMB2_CONNECT_ERR_MEMORY 1008

typedef struct usmb2_context {
    int fd;
    char username[32], password[32];
    unsigned char buf[8];
    int signing;
} usmb2_context;
typedef struct smb2man_share {
    usmb2_context *u;
    char ipstr[16], share[64], user[32], password[32], name[32];
    uint16_t dialect;
    unsigned gen;
    int protocol;
} smb2man_share;

static smb2man_share shares[1];
static unsigned session_gen;
static int refuse_2, negotiate_result, calls, fail_reconnect, tcp_calls;
static uint16_t offered[2][2];

static void smb2man_free_session(int s) { shares[s].u = NULL; }
static int smb2man_tcp_connect(const struct smb2man_share *sh) {
    (void)sh;
    tcp_calls++;
    return fail_reconnect && tcp_calls > 1 ? -1 : 10;
}
static void smb2man_sock_close(int fd) { (void)fd; }
static int smb2man_negotiate(struct smb2man_share *sh, struct usmb2_context *u, uint16_t d0, uint16_t d1,
                             int *sign_required)
{
    (void)sh; (void)u;
    assert(calls < 2);
    offered[calls][0] = d0; offered[calls][1] = d1;
    calls++;
    *sign_required = 0;
    if (negotiate_result) return negotiate_result;
    return refuse_2 && d0 == 0x0202 ? -2 : d0;
}
static int usmb2_sessionsetup(usmb2_context *u) { (void)u; return 0; }
static int usmb2_treeconnect(usmb2_context *u, const char *unc) { (void)u; (void)unc; return 0; }
static uint16_t rd16(const void *p) { (void)p; return 0; }
'''
code += function("static int smb2man_connect(int s)\n{")
code += r'''
static void reset(int protocol, int refuse) {
    free(shares[0].u);
    memset(shares, 0, sizeof(shares));
    shares[0].protocol = protocol;
    refuse_2 = refuse;
    negotiate_result = 0;
    fail_reconnect = 0;
    tcp_calls = calls = 0;
    memset(offered, 0, sizeof(offered));
}
int main(void) {
    reset(2, 1);
    assert(smb2man_connect(0) == -SMB2_CONNECT_ERR_PROT && calls == 1);
    assert(offered[0][0] == 0x0202 && offered[0][1] == 0x0210);

    reset(3, 0);
    assert(smb2man_connect(0) == 0 && calls == 1);
    assert(offered[0][0] == 0x0300 && offered[0][1] == 0x0302);

    reset(0, 1);
    assert(smb2man_connect(0) == 0 && calls == 2);
    assert(offered[0][0] == 0x0202 && offered[0][1] == 0x0210);
    assert(offered[1][0] == 0x0300 && offered[1][1] == 0x0302);

    reset(0, 1);
    fail_reconnect = 1;
    assert(smb2man_connect(0) == -SMB2_CONNECT_ERR_CONN && calls == 1 && tcp_calls == 2);

    reset(2, 0);
    negotiate_result = -1;
    assert(smb2man_connect(0) == -SMB2_CONNECT_ERR_CONN && calls == 1);
    free(shares[0].u);
    puts("PASS: exact SMB2/SMB3 and automatic two-stage negotiation");
    return 0;
}
'''
compiler = shutil.which("cc") or shutil.which("gcc")
assert compiler, "a host C compiler is needed"
with tempfile.TemporaryDirectory(prefix="smb2-dialects-") as tmp:
    c = Path(tmp) / "dialects.c"
    exe = Path(tmp) / "dialects"
    c.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
