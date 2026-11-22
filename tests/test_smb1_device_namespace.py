"""SMB1 devctl calls must reach smbman, not SMB2 unit 1 (iomanX strips trailing digits)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/smb_shares.c").read_text()


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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "smb_shares.h"

#define NO_PASSWORD -1
#define PLAINTEXT_PASSWORD 0
#define HASHED_PASSWORD 1
#define SMB_DEVCTL_GETPASSWORDHASHES 0xC0DE0001
#define SMB_DEVCTL_LOGON 0xC0DE0002
#define SMB_DEVCTL_LOGOFF 0xC0DE0003
#define SMB_DEVCTL_OPENSHARE 0xC0DE0005
#define SMB_DEVCTL_LOGON_ERR_LOGON 3
#define SMB1_ERR_OPENSHARE (-(SMB_DEVCTL_LOGON_ERR_LOGON + 1))
#define SMB1_ERR_HASH (-(SMB_DEVCTL_LOGON_ERR_LOGON + 2))

typedef struct { char password[256]; } smbGetPasswordHashes_in_t;
typedef struct { unsigned char LMhash[16], NTLMhash[16]; } smbGetPasswordHashes_out_t;
typedef struct {
    char serverIP[16];
    int serverPort;
    char User[256], Password[256];
    int PasswordType;
} smbLogOn_in_t;
typedef struct {
    char ShareName[256], Password[256];
    int PasswordType;
} smbOpenShare_in_t;

static int hash_calls, logon_calls, open_calls;

static int fileXioDevctl(const char *device, int cmd, void *arg, unsigned int arglen,
                         void *buf, unsigned int buflen)
{
    const char *colon = strchr(device, ':');
    size_t len;

    assert(colon != NULL);
    len = (size_t)(colon - device);
    while (len > 0 && isdigit((unsigned char)device[len - 1]))
        len--;
    if (len == 3 && !strncmp(device, "smb", 3))
        return -22; /* smb2man's unknown devctl command: -EINVAL */

    assert(!strcmp(device, "smbold:"));
    if (cmd == (int)SMB_DEVCTL_GETPASSWORDHASHES) {
        smbGetPasswordHashes_out_t *out = buf;
        assert(arglen == sizeof(smbGetPasswordHashes_in_t) && buflen == sizeof(*out));
        memset(out, 0x5a, sizeof(*out));
        hash_calls++;
        return 0;
    }
    if (cmd == (int)SMB_DEVCTL_LOGON) {
        const smbLogOn_in_t *in = arg;
        unsigned char expected[32];
        assert(arglen == sizeof(*in) && buflen == 0);
        assert(!strcmp(in->serverIP, "192.0.2.10") && in->serverPort == 139);
        assert(!strcmp(in->User, "player") && in->PasswordType == HASHED_PASSWORD);
        memset(expected, 0x5a, sizeof(expected));
        assert(!memcmp(in->Password, expected, sizeof(expected)));
        logon_calls++;
        return 0;
    }
    assert(cmd == (int)SMB_DEVCTL_OPENSHARE && arglen == sizeof(smbOpenShare_in_t) && buflen == 0);
    assert(!strcmp(((smbOpenShare_in_t *)arg)->ShareName, "games"));
    open_calls++;
    return 0;
}
'''
code += function("static int connectSmb1(SmbShare *sh)\n{")
code += r'''
int main(void)
{
    SmbShare share;

    memset(&share, 0, sizeof(share));
    strcpy(share.server, "192.0.2.10");
    strcpy(share.username, "player");
    strcpy(share.password, "secret");
    strcpy(share.share, "games");
    share.port = 139;
    assert(connectSmb1(&share) == 0);
    assert(hash_calls == 1 && logon_calls == 1 && open_calls == 1);
    puts("PASS: SMB1 password hashing and logon reach smbman");
    return 0;
}
'''

cc = shutil.which("cc") or shutil.which("gcc")
assert cc, "a host C compiler is needed"
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "t.c"
    exe = Path(d) / "t"
    c.write_text(code)
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "include"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
