"""Exercise SMB connection state across fallback and preferred-card config reads."""
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
#include <stdlib.h>
#include <string.h>
#include "smb_shares.h"
#include "smb2man.h"

#define MAX_PATH 1024
#define SMB_RETRY_MS 30000
#define SMB_DEVCTL_LOGOFF 0xC0DE0003
#define DPRINTF(...) ((void)0)
static SmbConfig smb_cfg;
static int smb_cfg_loaded, smb_cfg_mc1_pending;
static char smb_cfg_path[MAX_PATH];
static unsigned int smb_retry_ms[3];
static void smbReadConfig(void);

static unsigned int now;
static const char *config_text;
static int mc1_refused, preferred, read_calls, connect_calls, connect_result;
static int disconnect_calls, disconnect1_calls, disconnect2_calls;
static int disconnect1_result, disconnect2_result;

static char *smbReadConfigText(int *refused)
{
    read_calls++;
    *refused = mc1_refused;
    snprintf(smb_cfg_path, sizeof(smb_cfg_path), "%s",
             preferred ? "mc1:/SYS-CONF/SMB2.CNF" : "mc0:/SYS-CONF/SMB2.CNF");
    return config_text != NULL ? strdup(config_text) : NULL;
}

static int connectSmb1(SmbShare *sh) { (void)sh; connect_calls++; return connect_result; }
static int connectSmb2(SmbShare *sh) { (void)sh; connect_calls++; return connect_result; }
static int isSmbDriverLoaded(int version) { return version == SMB_PATH_V1 || version == SMB_PATH_V2; }
static int fileXioDevctl(const char *device, int cmd, void *arg, unsigned int arglen,
                         void *buf, unsigned int buflen)
{
    (void)arg; (void)arglen; (void)buf; (void)buflen;
    disconnect_calls++;
    if (!strcmp(device, SMB_DEVICE_V2) && cmd == (int)SMB2_DEVCTL_DISCONNECT_ALL) {
        disconnect2_calls++;
        return disconnect2_result;
    }
    assert(!strcmp(device, SMB_DEVICE_V1) && cmd == (int)SMB_DEVCTL_LOGOFF);
    disconnect1_calls++;
    return disconnect1_result;
}
static const char *smb1Problem(int ret) { (void)ret; return "mock SMB1 failure"; }
static unsigned int Timer(void) { return now; }
'''
code += source[source.index("static unsigned int smb_cfg_text_hash;"):source.index("static void smbReadConfig(void)\n{")]
code += function("static void smbReadConfig(void)\n{")
code += function("void smbSharesConnect(int version, int retry_failed)\n{")
code += function("int smbSharesConnectedCount(int version)\n{")
code += function("void smbSharesForget(void)\n{")
code += function("int smbSharesReload(void)\n{")
code += r'''
static void reset_case(void)
{
    memset(&smb_cfg, 0, sizeof(smb_cfg));
    smb_cfg_loaded = smb_cfg_mc1_pending = 0;
    smb_cfg_path[0] = '\0';
    memset(smb_retry_ms, 0, sizeof(smb_retry_ms));
    now = 1000;
    config_text = "NAME=A\nURL=smb://10.0.0.2/s\n";
    mc1_refused = 1;
    preferred = 0;
    read_calls = connect_calls = 0;
    connect_result = 0;
    disconnect_calls = disconnect1_calls = disconnect2_calls = 0;
    disconnect1_result = disconnect2_result = 0;
}

int main(void)
{
    assert(fileXioDevctl != NULL);
    reset_case();
    smbSharesConnect(2, 1);
    assert(read_calls == 1 && connect_calls == 1);
    assert(smb_cfg.share[0].state == SMB_STATE_CONNECTED && smb_cfg_mc1_pending);
    smbSharesConnect(2, 1);
    assert(smb_cfg.share[0].state == SMB_STATE_CONNECTED);
    assert(connect_calls == 1);

    reset_case();
    connect_result = -1;
    smbSharesConnect(2, 1);
    assert(connect_calls == 1 && smb_cfg.share[0].state == SMB_STATE_FAILED);
    smbSharesConnect(2, 1);
    assert(connect_calls == 1 && smb_cfg.share[0].state == SMB_STATE_FAILED);

    reset_case();
    connect_result = -1;
    smbSharesConnect(2, 1);
    config_text = "NAME=A\nURL=smb://10.0.0.3/t\n";
    smbSharesConnect(2, 1);
    assert(read_calls == 2 && connect_calls == 2);
    assert(!strcmp(smb_cfg.share[0].url, "smb://10.0.0.3/t"));

    reset_case();
    connect_result = -1;
    smbSharesConnect(2, 1);
    mc1_refused = 0;
    preferred = 1;
    smbSharesConnect(2, 1);
    assert(read_calls == 2 && !smb_cfg_mc1_pending);
    assert(!strcmp(smb_cfg_path, "mc1:/SYS-CONF/SMB2.CNF"));

    reset_case();
    smbSharesConnect(2, 1);
    config_text = "NAME=A\nURL=smb://10.0.0.3/t\n";
    smbSharesConnect(2, 1);
    assert(disconnect_calls == 1 && connect_calls == 2);
    assert(!strcmp(smb_cfg.share[0].url, "smb://10.0.0.3/t"));

    reset_case();
    smbSharesConnect(2, 1);
    disconnect2_result = -1;
    config_text = "NAME=A\nURL=smb://10.0.0.3/t\n";
    smbSharesConnect(2, 1);
    assert(disconnect_calls == 1 && connect_calls == 1);
    assert(!strcmp(smb_cfg.share[0].url, "smb://10.0.0.2/s"));
    assert(smb_cfg.share[0].state == SMB_STATE_CONNECTED);

    /* A partial disconnect must never leave the successful dialect marked
     * CONNECTED when its IOP session has already been closed. */
    reset_case();
    config_text = "NAME=A\\nURL=smb://10.0.0.2/s\\n"
                  "NAME=OLD\\nVERSION=1\\nURL=smb://10.0.0.2/legacy\\n";
    smbSharesConnect(2, 1);
    smbSharesConnect(1, 1);
    assert(smbSharesConnectedCount(2) == 1 && smbSharesConnectedCount(1) == 1);
    disconnect2_result = -1;
    assert(!smbSharesReload());
    assert(disconnect1_calls == 1 && disconnect2_calls == 1);
    assert(smbSharesConnectedCount(1) == 0 && smbSharesConnectedCount(2) == 1);
    { int before = connect_calls;
      smbSharesConnect(1, 0);
      assert(connect_calls == before + 1 && smbSharesConnectedCount(1) == 1); }

    reset_case();
    config_text = "NAME=A\\nURL=smb://10.0.0.2/s\\n"
                  "NAME=OLD\\nVERSION=1\\nURL=smb://10.0.0.2/legacy\\n";
    smbSharesConnect(2, 1);
    smbSharesConnect(1, 1);
    disconnect1_result = -1;
    assert(!smbSharesReload());
    assert(smbSharesConnectedCount(2) == 0 && smbSharesConnectedCount(1) == 1);
    { int before = connect_calls;
      smbSharesConnect(2, 0);
      assert(connect_calls == before + 1 && smbSharesConnectedCount(2) == 1); }

    puts("PASS: SMB fallback and partial disconnect keep EE and IOP state synchronized");
    return 0;
}
'''

compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="smb-connect-state-") as tmp:
    source_path = Path(tmp) / "connect_state.c"
    binary_path = Path(tmp) / "connect_state"
    source_path.write_text(code)
    subprocess.run([compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DSMB",
                    "-I", str(root / "include"), str(source_path), str(root / "src/smb_cnf.c"),
                    "-o", str(binary_path)], check=True)
    subprocess.run([str(binary_path)], check=True, timeout=5)
