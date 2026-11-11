"""SMB2.CNF without a port reaches smb2man as smb://ip:445/share; with one, unchanged."""
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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "smb_shares.h"
#include "smb2man.h"

static char sent_url[512];
static int fileXioDevctl(const char *device, int cmd, void *arg, unsigned int arglen,
                         void *buf, unsigned int buflen)
{
    const smb2Connect_in_t *in = arg;
    (void)buf; (void)buflen;
    assert(!strcmp(device, SMB_DEVICE_V2) && cmd == (int)SMB2_DEVCTL_CONNECT);
    assert(arglen == sizeof(*in) && memchr(in->url, 0, sizeof(in->url)));
    strcpy(sent_url, in->url);
    return 0;
}
'''
code += function("static int connectSmb2(SmbShare *sh)\n{")
code += r'''
static const char *sent(const char *text)
{
    static char buf[1024];
    SmbConfig cfg;
    snprintf(buf, sizeof(buf), "%s", text);
    assert(smbConfigParse(buf, &cfg) == 1);
    sent_url[0] = '\0';
    assert(connectSmb2(&cfg.share[0]) == 0);
    return sent_url;
}

int main(void)
{
    assert(!strcmp(sent("NAME = A\nURL = smb://192.168.1.1/games\n"), "smb://192.168.1.1:445/games"));
    assert(!strcmp(sent("NAME = A\nURL = smb://192.168.1.1:445/games\n"), "smb://192.168.1.1:445/games"));
    assert(!strcmp(sent("NAME = A\nURL = smb://192.168.1.1:1445/games/sub\n"), "smb://192.168.1.1:1445/games"));
    assert(!strcmp(sent("NAME = A\nURL = smb://192.168.1.1/games"), "smb://192.168.1.1:445/games"));
    puts("PASS: SMB2 shares reach the driver with an explicit port");
    return 0;
}
'''

cc = shutil.which("cc") or shutil.which("gcc")
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "t.c"
    c.write_text(code)
    exe = Path(d) / "t"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "include"), str(c), str(root / "src/smb_cnf.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
