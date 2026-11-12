"""NFS shares: an nfs:// URL among the SMB shares. Run the parser on the host, and check how nfs: is wired in."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smb_shares.h"

static SmbConfig cfg;

static int parse(const char *text)
{
    char *buf = strdup(text);
    int n;

    assert(buf != NULL);
    n = smbConfigParse(buf, &cfg);
    free(buf);
    return n;
}

static const SmbShare *share(int i)
{
    assert(i < cfg.count);
    return &cfg.share[i];
}

static void expectProblem(const char *text, const char *needle)
{
    if (parse(text) != 0 || cfg.count != 1) {
        fprintf(stderr, "not one invalid share: %s\n", text);
        abort();
    }
    assert(share(0)->state == SMB_STATE_INVALID);
    if (share(0)->problem == NULL || strstr(share(0)->problem, needle) == NULL) {
        fprintf(stderr, "problem '%s' lacks '%s'\n", share(0)->problem ? share(0)->problem : "", needle);
        abort();
    }
}

int main(void)
{
    static char out[4096], work[4096];
    const char *mixed =
        "NAME = PC\nUSERNAME = player\nPASSWORD = secret\nURL = smb://192.168.0.10/PS2\n\n"
        "# the games on a Linux box, as nfs:/Games\n"
        "NAME = Games\nURL = nfs://192.168.0.30/srv/ps2\n\n"
        "name = Saves\nurl = NFS://192.168.0.30:2050/srv/ps2/saves?uid=1000&gid=100\n";
    int n;

    /* the URL's scheme decides: SMB2 and NFS shares side by side */
    assert(parse(mixed) == 3 && cfg.count == 3);
    assert(share(0)->version == SMB_PATH_V2);
    assert(share(1)->version == SMB_PATH_NFS && share(1)->state == SMB_STATE_UNTRIED);
    assert(!strcmp(share(1)->server, "192.168.0.30") && share(1)->port == 2049 && !strcmp(share(1)->share, "/srv/ps2"));
    assert(share(2)->version == SMB_PATH_NFS && share(2)->port == 2050 && !strcmp(share(2)->share, "/srv/ps2/saves"));
    assert(smbConfigValidCount(&cfg, SMB_PATH_V2) == 1 && smbConfigValidCount(&cfg, SMB_PATH_NFS) == 2);
    assert(smbConfigValidCount(&cfg, SMB_PATH_V1) == 0);
    /* the server's pseudo root is an export too */
    assert(parse("NAME = Root\nURL = nfs://10.0.0.1/\n") == 1 && !strcmp(share(0)->share, "/"));

    /* what nfsman cannot take is refused here, with a reason */
    expectProblem("NAME = N\nURL = nfs://nas/srv\n", "IP address");
    expectProblem("NAME = N\nURL = nfs://010.0.0.1/srv\n", "IP address");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1\n", "nfs://<server>/<export>");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1:0/srv\n", "port is 0");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1:x/srv\n", "not a number");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1/srv?user=1\n", "uid=N and gid=N");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1/srv?uid=\n", "not a number");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1/srv?uid=1x\n", "not a number");
    expectProblem("NAME = N\nURL = nfs://10.0.0.1/srv?uid=1234567890\n", "too large");
    expectProblem("NAME = N\nUSERNAME = me\nURL = nfs://10.0.0.1/srv\n", "NAME and URL only");
    expectProblem("NAME = N\nVERSION = 1\nURL = nfs://10.0.0.1/srv\n", "NAME and URL only");
    expectProblem("NAME = N\nURL = ftp://10.0.0.1/srv\n", "smb:// or nfs://");
    /* one NAME space for SMB and NFS shares */
    assert(parse("NAME = A\nURL = smb://10.0.0.1/s\nNAME = a\nURL = nfs://10.0.0.1/e\n") == 1);
    assert(share(1)->state == SMB_STATE_INVALID && strstr(share(1)->problem, "NAME"));
    /* an NFS share is not an SMB1 share: VERSION = 1 rules stay with SMB */
    assert(parse("NAME = A\nVERSION = 1\nURL = smb://10.0.0.1/s\nNAME = B\nURL = nfs://10.0.0.1/e\n") == 2);

    /* editing an SMB share in the file keeps the NFS blocks byte for byte */
    n = smbCnfUpsertText(mixed, "PC", "NAME = PC\nURL = smb://192.168.0.11:445/PS2\n",
                         out, sizeof(out), work, sizeof(work));
    assert(n > 0 && strstr(out, "NAME = Games\nURL = nfs://192.168.0.30/srv/ps2\n") != NULL);
    assert(strstr(out, "url = NFS://192.168.0.30:2050/srv/ps2/saves?uid=1000&gid=100\n") != NULL);
    n = smbCnfUpsertText(mixed, "Games", NULL, out, sizeof(out), work, sizeof(work));
    assert(n > 0 && strstr(out, "nfs://192.168.0.30/srv/ps2\n") == NULL && strstr(out, "NAME = PC") != NULL);

    /* the editor's block parses back to the same share */
    n = nfsCnfBuildBlock(out, sizeof(out), "Games", "192.168.0.30", 2049, "/srv/ps2", 1000, 100);
    assert(n > 0 && !strcmp(out, "NAME = Games\nURL = nfs://192.168.0.30:2049/srv/ps2?uid=1000&gid=100\n"));
    assert(parse(out) == 1 && share(0)->version == SMB_PATH_NFS && !strcmp(share(0)->share, "/srv/ps2"));
    n = nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 2050, "/e", -1, 5);
    assert(n > 0 && strstr(out, "URL = nfs://10.0.0.1:2050/e?gid=5\n") && parse(out) == 1);
    n = nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 2049, "/e", -1, -1);
    assert(n > 0 && strstr(out, "URL = nfs://10.0.0.1:2049/e\n") && parse(out) == 1);
    assert(nfsCnfBuildBlock(out, sizeof(out), "G", "nas", 2049, "/e", -1, -1) < 0);
    assert(nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 0, "/e", -1, -1) < 0);
    assert(nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 2049, "e", -1, -1) < 0);
    assert(nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 2049, "/e?x", -1, -1) < 0);
    assert(nfsCnfBuildBlock(out, sizeof(out), "", "10.0.0.1", 2049, "/e", -1, -1) < 0);
    assert(nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 2049, "/e", 1000000000, -1) < 0);
    memset(work, 'p', 300);
    work[0] = '/';
    work[300] = '\0';
    assert(nfsCnfBuildBlock(out, sizeof(out), "G", "10.0.0.1", 2049, work, -1, -1) < 0); /* URL over 255 */

    /* the device */
    assert(smbPathKind("nfs:/Games/x.iso") == SMB_PATH_NFS && smbPathKind("nfs:") == SMB_PATH_NFS);
    assert(smbPathKind("nfs4:/") == SMB_PATH_NONE && smbPathKind("nf") == SMB_PATH_NONE);
    assert(smbIsRootPath("nfs:") && smbIsRootPath("nfs:/") && smbIsRootPath("nfs://"));
    assert(!smbIsRootPath("nfs:/Games"));

    puts("NFS shares parser: OK");
    return 0;
}
'''

compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "a host C compiler is needed"
with tempfile.TemporaryDirectory() as tmp:
    test_c = Path(tmp) / "test_nfs.c"
    exe = Path(tmp) / "test_nfs"
    test_c.write_text(code)
    subprocess.run([compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "include"), str(test_c), str(root / "src/smb_cnf.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)


# Wiring checks: the parts that only run on a console.
def text(path):
    return (root / path).read_text()

init = text("src/init.c")
load_smb = init[init.index("int load_smb(int version)"):]
load_smb = load_smb[:load_smb.index("\n}\n")]
assert "IMPORT_BIN2C(nfsman_irx);" in init
assert "SifExecModuleBuffer(nfsman_irx, size_nfsman_irx, 0, NULL, &ret)" in load_smb
assert "have_nfsman = 0;" in init  # forgotten with the IOP reset, as the SMB drivers

yaml = text("ps2.yaml")
assert "  - name: nfsman\n" in yaml and "      - nfsman\n" in yaml  # built, and embedded in the ELF

shares = text("src/smb_shares.c")
assert "NFS_DEVCTL_CONNECT" in shares and "NFS_DEVCTL_DISCONNECT_ALL" in shares
assert "smb_retry_ms[4]" in shares  # indexed by version, NFS is 3

devices = text("src/dash_devices.c")
assert '"Network (NFS)", SMB_DEVICE_NFS, "nfs:/"' in devices
assert "smbSharesKnown(SMB_PATH_NFS) > 0" in devices  # listed once saved; listing reads nothing

browser = text("src/filer_browser.c")
assert "smbPathKind(path) == SMB_PATH_NFS && smbIsRootPath(path))\n\t\twrite_disabled = 1;" in browser

# Neutrino is handed SMB paths only: nfs: reads as not supported there
neutrino = text("src/neutrino_iso.c")
assert "smbPathKind(path) == SMB_PATH_V1 || smbPathKind(path) == SMB_PATH_V2)\n\t\treturn \"smb\";" in neutrino

# the share editor writes an NFS share with nfsCnfBuildBlock, and loads one as kind NFS
settings = text("src/dash_settings.c")
save = settings[settings.index("static void smbedSave(void)"):]
save = save[:save.index("\n}\n")]
assert "if (smbed.kind == SMBED_NFS)\n\t\tn = nfsCnfBuildBlock(" in save and "smbCnfBuildBlock(" in save
assert '{ "SMB2", "SMB3", "SMB1", "NFS" }' in settings
load = settings[settings.index("static void smbedLoadPick(void)"):]
load = load[:load.index("\n}\n")]
assert "if (sh->version == SMB_PATH_NFS) {" in load and "smbed.kind = SMBED_NFS;" in load
for sid in ("S_SMBED_UID", "S_SMBED_GID"):
    assert f"case {sid}:" in settings, sid

print("NFS shares wiring: OK")
