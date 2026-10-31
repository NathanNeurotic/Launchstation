"""Run the SMB2.CNF parser on the host, and check how smb:/smb1: are wired in."""
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
    assert(share(0)->problem != NULL);
    if (strstr(share(0)->problem, needle) == NULL) {
        fprintf(stderr, "problem '%s' lacks '%s'\n", share(0)->problem, needle);
        abort();
    }
}

int main(void)
{
    char longv[300];

    /* the example of wLaunchELF_R3Z PR #63, as written there */
    assert(parse("# Shares for wLaunchELF's smb: device\n\n"
                 "# A shared folder \"PS2\" on a Windows PC, as smb:/PC\n"
                 "NAME = PC\nUSERNAME = player\nPASSWORD = secret\nURL = smb://192.168.0.10/PS2\n\n"
                 "# The \"games\" share of a NAS, as smb:/NAS\n"
                 "NAME = NAS\nUSERNAME = ps2\nPASSWORD = n4s-pa55word\nURL = smb://192.168.0.20/games\n") == 2);
    assert(cfg.count == 2 && !cfg.dropped && !cfg.unfinished);
    assert(!strcmp(share(0)->name, "PC") && !strcmp(share(0)->username, "player"));
    assert(!strcmp(share(0)->password, "secret") && !strcmp(share(0)->url, "smb://192.168.0.10/PS2"));
    assert(share(0)->version == 2 && share(0)->state == SMB_STATE_UNTRIED && share(0)->line == 7);
    assert(!strcmp(share(1)->name, "NAS") && share(1)->line == 13);
    assert(smbConfigValidCount(&cfg, 2) == 2 && smbConfigValidCount(&cfg, 1) == 0);

    /* keys in any case; trailing spaces and tabs are not part of a password */
    assert(parse("name=A\nUserName = u \t\npassword = p  \nurl = smb://10.0.0.2/s\n") == 1);
    assert(!strcmp(share(0)->username, "u") && !strcmp(share(0)->password, "p"));
    /* a password may hold '=' and '#'; only the first '=' splits */
    assert(parse("NAME=A\nPASSWORD = a=b#c\nURL=smb://10.0.0.2/s\n") == 1);
    assert(!strcmp(share(0)->password, "a=b#c"));

    /* BOM, CRLF, lone CR, comments with ';' */
    assert(parse("\xEF\xBB\xBFNAME = A\r\nURL = smb://10.0.0.2/s\r\n; c\rNAME = B\rURL = smb://10.0.0.3/t\r") == 2);
    assert(!strcmp(share(0)->name, "A") && !strcmp(share(1)->url, "smb://10.0.0.3/t"));

    /* PR 63 stopped at a line without '='; here only that line is skipped */
    assert(parse("NAME = A\nURL = smb://10.0.0.2/s\nthis is not a key\nNAME = B\nURL = smb://10.0.0.3/t\n") == 2);
    /* unknown keys are ignored */
    assert(parse("NAME = A\nDOMAIN = WORKGROUP\nURL = smb://10.0.0.2/s\n") == 1);

    /* keys after the last URL never form a share */
    assert(parse("NAME = A\nURL = smb://10.0.0.2/s\nNAME = B\n") == 1 && cfg.unfinished);

    /* PR 63 accepted a share with no NAME */
    expectProblem("URL = smb://10.0.0.2/s\n", "NAME is missing");
    expectProblem("NAME = a/b\nURL = smb://10.0.0.2/s\n", "character");
    expectProblem("NAME = ..\nURL = smb://10.0.0.2/s\n", "cannot be");
    /* duplicate names, in any case: the first one stays */
    assert(parse("NAME = PC\nURL = smb://10.0.0.2/s\nNAME = pc\nURL = smb://10.0.0.3/t\n") == 1);
    assert(share(0)->state == SMB_STATE_UNTRIED && share(1)->state == SMB_STATE_INVALID);
    assert(strstr(share(1)->problem, "already"));

    /* PR 63 cut long values short; here the share is refused and says why */
    memset(longv, 'x', sizeof(longv));
    longv[32] = '\0'; /* 32 characters: one too many */
    {
        char text[512];
        snprintf(text, sizeof(text), "NAME = %s\nURL = smb://10.0.0.2/s\n", longv);
        expectProblem(text, "NAME is longer than 31");
        snprintf(text, sizeof(text), "NAME = A\nPASSWORD = %s\nURL = smb://10.0.0.2/s\n", longv);
        expectProblem(text, "PASSWORD is longer than 31");
        snprintf(text, sizeof(text), "NAME = A\nUSERNAME = %s\nURL = smb://10.0.0.2/s\n", longv);
        expectProblem(text, "USERNAME is longer than 31");
        longv[31] = '\0';
        snprintf(text, sizeof(text), "NAME = %s\nURL = smb://10.0.0.2/s\n", longv);
        assert(parse(text) == 1); /* 31 characters fit */
    }
    memset(longv, 'y', sizeof(longv));
    longv[250] = '\0';
    {
        char text[600];
        snprintf(text, sizeof(text), "NAME = A\nURL = smb://10.0.0.2/%s\n", longv);
        expectProblem(text, "URL is longer than 255");
    }

    /* URL checks for SMB2 */
    expectProblem("NAME = A\nURL = //10.0.0.2/s\n", "smb://");
    expectProblem("NAME = A\nURL = smb://10.0.0.2\n", "<share>");
    expectProblem("NAME = A\nURL = smb://10.0.0.2/\n", "<share>");
    assert(parse("NAME = A\nURL = SMB://nas.local/share/sub\n") == 1); /* libsmb2 parses the rest */

    /* VERSION */
    assert(parse("NAME = OLD\nVERSION = 1\nURL = smb://192.168.1.5/PS2SMB\n") == 1);
    assert(share(0)->version == 1 && !strcmp(share(0)->server, "192.168.1.5"));
    assert(share(0)->port == 445 && !strcmp(share(0)->share, "PS2SMB"));
    assert(smbConfigValidCount(&cfg, 1) == 1 && smbConfigValidCount(&cfg, 2) == 0);
    assert(parse("NAME = OLD\nVERSION = nt1\nURL = smb://192.168.1.5:139//PS2SMB/ignored\n") == 1);
    assert(share(0)->version == 1 && share(0)->port == 139 && !strcmp(share(0)->share, "PS2SMB"));
    assert(parse("NAME = N\nVERSION = SMB3\nURL = smb://192.168.1.5/s\n") == 1 && share(0)->version == 2);
    expectProblem("NAME = A\nVERSION = 4\nURL = smb://10.0.0.2/s\n", "VERSION must be");
    /* PLAINTEXT: SMB1 only, off by default, does not leak into the next share */
    assert(parse("NAME = OLD\nVERSION = 1\nURL = smb://192.168.1.5/PS2SMB\n") == 1 && share(0)->plaintext == 0);
    assert(parse("NAME = OLD\nVERSION = 1\nPLAINTEXT = yes\nURL = smb://192.168.1.5/PS2SMB\nNAME = B\nURL = smb://10.0.0.3/t\n") == 2);
    assert(share(0)->plaintext == 1 && share(1)->plaintext == 0);
    assert(parse("NAME = OLD\nVERSION = 1\nplaintext = 0\nURL = smb://192.168.1.5/PS2SMB\n") == 1 && share(0)->plaintext == 0);
    expectProblem("NAME = A\nPLAINTEXT = 1\nURL = smb://10.0.0.2/s\n", "only with VERSION = 1");
    expectProblem("NAME = A\nVERSION = 1\nPLAINTEXT = maybe\nURL = smb://10.0.0.2/s\n", "PLAINTEXT must be");
    /* the version does not leak into the next share */
    assert(parse("NAME = A\nVERSION = 1\nURL = smb://10.0.0.2/s\nNAME = B\nURL = smb://10.0.0.3/t\n") == 2);
    assert(share(0)->version == 1 && share(1)->version == 2);

    /* SMB1 URL rules: smbman takes an IPv4 address and one share */
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://nas.local/s\n", "IP address");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://10.0.0.256/s\n", "IP address");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://10.0.0/s\n", "IP address");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://user@10.0.0.2/s\n", "USERNAME");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://10.0.0.2:/s\n", "empty port");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://10.0.0.2:99999/s\n", "65535");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://10.0.0.2:0/s\n", "port is 0");
    expectProblem("NAME = A\nVERSION = 1\nURL = smb://10.0.0.2/\n", "<share>");
    /* one SMB1 share only: smbman keeps a single session */
    assert(parse("NAME = A\nVERSION = 1\nURL = smb://10.0.0.2/s\nNAME = B\nVERSION = 1\nURL = smb://10.0.0.3/t\n") == 1);
    assert(share(1)->state == SMB_STATE_INVALID && strstr(share(1)->problem, "one SMB1"));
    /* an invalid first SMB1 share does not block a valid second one */
    assert(parse("NAME = A\nVERSION = 1\nURL = smb://bad/s\nNAME = B\nVERSION = 1\nURL = smb://10.0.0.3/t\n") == 1);
    assert(share(1)->state == SMB_STATE_UNTRIED);

    /* more than SMB_SHARE_MAX shares are counted, not stored */
    {
        char text[2048] = "";
        int i;
        for (i = 0; i < SMB_SHARE_MAX + 2; i++) {
            char one[96];
            snprintf(one, sizeof(one), "NAME = S%d\nURL = smb://10.0.0.%d/s\n", i, i + 1);
            strcat(text, one);
        }
        assert(parse(text) == SMB_SHARE_MAX && cfg.count == SMB_SHARE_MAX && cfg.dropped == 2);
    }

    /* no file at all */
    assert(smbConfigParse(NULL, &cfg) == 0 && cfg.count == 0);
    assert(parse("") == 0 && parse("\n\n# only comments\n") == 0);

    /* device names: smb1: must not be taken for smb: */
    assert(smbPathKind("smb:/PC/x") == SMB_PATH_V2 && smbPathKind("smb:") == SMB_PATH_V2);
    assert(smbPathKind("smb1:/x") == SMB_PATH_V1 && smbPathKind("smb1:") == SMB_PATH_V1);
    assert(smbPathKind("smbx:/") == SMB_PATH_NONE && smbPathKind("sm") == SMB_PATH_NONE);
    assert(smbPathKind("smb2:/") == SMB_PATH_NONE && smbPathKind(NULL) == SMB_PATH_NONE);
    assert(smbIsRootPath("smb:") && smbIsRootPath("smb:/") && smbIsRootPath("smb1://"));
    assert(!smbIsRootPath("smb:/PC") && !smbIsRootPath("smb1:/dir") && !smbIsRootPath("host:/"));

    puts("SMB2.CNF parser: OK");
    return 0;
}
'''

compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "a host C compiler is needed"
with tempfile.TemporaryDirectory() as tmp:
    test_c = Path(tmp) / "test_smb.c"
    exe = Path(tmp) / "test_smb"
    test_c.write_text(code)
    subprocess.run([compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "include"), str(test_c), str(root / "src/smb_cnf.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)

# Wiring checks: the parts that only run on a console.
def text(path):
    return (root / path).read_text()

filer = text("src/filer.c")
read_smb = filer[filer.index("int readSMB("):]
read_smb = read_smb[:read_smb.index("\n}\n")]
# the dashboard worker must never load IOP modules
assert "dashWorkerOnThread() || !load_smb(version)" in read_smb
assert "readNetDir(path, info, max, &smb_dir_open_failed)" in read_smb
assert "smbPathKind(path) != SMB_PATH_NONE)\n\t\tn = readSMB(path, info, max);" in filer

init = text("src/init.c")
load_smb = init[init.index("int load_smb(int version)"):]
load_smb = load_smb[:load_smb.index("\n}\n")]
# a driver whose start routine refused to stay resident is not loaded
assert "(ID >= 0 && ret == 0)" in load_smb
for gate in ("pcCardCfOwnsBus()", "legacy_net_drivers", "switchNetworkStack(NETWORK_STACK_ETH)", "load_ps2ip()"):
    assert gate in load_smb, gate
assert "smbSharesForget();" in init

device = text("src/filer_device.c")
assert "#ifdef SMB\n\t        smbPathKind(path) != SMB_PATH_NONE ||" in device  # DEV9-backed: excluded with PC CARD CF
assert "transferUsesUdpfsAndSmb(src_path, dst_path)" in device

browser = text("src/filer_browser.c")
assert "smbPathKind(path) == SMB_PATH_V2 && smbIsRootPath(path))\n\t\twrite_disabled = 1;" in browser

# the device list reads no file: smbSharesKnown, never smbSharesConfigured
devices = text("src/dash_devices.c")
listing = devices[devices.index("int dashDevicesList("):]
listing = listing[:listing.index("\n}\n")]
assert "smbSharesKnown(SMB_PATH_V1)" in listing and "smbSharesConfigured" not in listing

smbman = text("iop/smbman/smb_fio.c")
assert '#define SMBMAN_DEVICE_NAME "smb1"' in smbman and "SMBMAN_DEVICE_NAME," in smbman
assert "return MODULE_NO_RESIDENT_END;" in text("iop/smbman/smbman.c")

# smbman registers through iomanX (an IOP_DT_FSEXT device needs it)
imports = text("iop/smbman/imports.lst")
assert "iomanX_IMPORTS_start\nI_iomanX_AddDrv\nI_iomanX_DelDrv\niomanX_IMPORTS_end" in imports
assert "ioman_IMPORTS_start" not in imports

# server-supplied lengths are bounded before anything is received or copied
smb = text("iop/smbman/smb.c")
assert "size = (rhdrlen == 0 || rhdrlen > totalpkt_size) ? totalpkt_size : rhdrlen;" in smb
assert "if (size <= 0 || size > (int)sizeof(SMB_buf.smb))\n        return -2;" in smb
assert "if (r < (int)sizeof(ReadAndXResponse_t))\n        return -EIO;" in smb
assert "paramStart + (int)sizeof(FindFirstNext2ResponseParam_t) > r" in smb
assert "dataStart + (int)sizeof(FindFirst2ResponseData_t) > r" in smb
assert "nameLen > SMB_SEARCH_NAME_MAX" in smb
assert "passwordlen > 14" not in smb  # a PLAINTEXT = 1 password is sent whole
assert "getStringField(info->FileName, FFRspData->FileName, nameLen);" in smb
assert "(bytesProcessed + 2 <= inbytes)" in smb and "bytesProcessed + 4 > inbytes" in smb
assert "#define SMB_SEARCH_NAME_MAX" in text("iop/smbman/smb.h")
assert "NPRsp->KeyLength != sizeof(server_specs.EncryptionKey)" in smb
assert "getStringField(server_specs.PrimaryDomainServerName, (char *)&NPRsp->ByteField[NPRsp->KeyLength], name_bytes);" in smb
assert "if (DataLength < 0 || DataLength > nbytes)" in smb
assert "padding < 0 || padding > (int)(sizeof(SMB_buf.smb) - sizeof(ReadAndXResponse_t))" in smb
assert "RecvTimeout(main_socket, &dummy, sizeof(dummy), 3000)" in smb
# never a clear-text password
# clear text only when the caller passed the clear text (the share's PLAINTEXT = 1)
plain = smb[smb.index("SERVER_USE_PLAINTEXT_PASSWORD) {"):]
plain = plain[:plain.index("\n        }")]
assert "if (PasswordType != PLAINTEXT_PASSWORD)\n                return -1;" in plain
assert plain.index("return -1;") < plain.index("memcpy")
shares = text("src/smb_shares.c")
connect1 = shares[shares.index("static int connectSmb1("):]
connect1 = connect1[:connect1.index("\n}\n")]
assert connect1.count("PasswordType = PLAINTEXT_PASSWORD;") == 2 and "if (sh->plaintext) {" in connect1
assert "return SMB1_ERR_HASH;" in connect1  # no silent fallback to clear text

# every close frees its slot; a listing ends with 0, not an error
close = smbman[smbman.index("int smb_close(iop_file_t *f)"):]
close = close[:close.index("\n}\n")]
assert "goto" not in close and "f->privdata = NULL;" in close
dread = smbman[smbman.index("int smb_dread(iop_file_t *f"):]
dread = dread[:dread.index("\n}\n")]
assert "if (fh->position) {" in dread and dread.count("if (r == -EINVAL) {") == 2 and "if (info->EOS)" in dread
assert "r = -ENAMETOOLONG;" in smbman

# the dashboard worker never retries shares; a stale failure flag only acts on SMB paths
filer = text("src/filer.c")
assert "} else if (root && !dashWorkerOnThread()) {" in filer
assert "if (smb_version != SMB_PATH_NONE) {" in browser

yaml = text("ps2.yaml")
assert "      - SMBMAN_SMB1_DEVICE\n      - IOMANX_OLD_NAME_ADDDELDRV=0\n" in yaml
assert "      - smbman\n" in yaml and "      - SMB\n" in yaml
assert "src/smb_cnf.c" in yaml and "src/smb_shares.c" in yaml
assert 'EMBED_FILE(smb2man_irx, "iop/__precompiled/smb2man.irx")' in text("src/embed_blobs.S")

print("SMB shares: OK")
