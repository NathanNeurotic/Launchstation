"""Exercise real SMB root reads across driver versions and thread contexts."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
filer = (root / "src/filer.c").read_text()
cnf = (root / "src/smb_cnf.c").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct { int unused; } FILEINFO;
enum { SMB_PATH_NONE = 0, SMB_PATH_V1 = 1, SMB_PATH_V2 = 2, SMB_PATH_NFS = 3 };
#define SMB_DEVICE_V1 "smbold:"
#define SMB_DEVICE_V2 "smb:"
#define SMB_DEVICE_NFS "nfs:"
#define MAX_PATH 1024
static int smb_dir_open_failed;
static int loaded[4], configured[4], connected[4], load_ok[4], connect_ok[4];
static int load_attempts[4], connect_attempts[4], net_calls, worker, auto_v1;
static char net_path[1024];

static int isSmbDriverLoaded(int version) { return loaded[version]; }
static int load_smb(int version) {
    load_attempts[version]++;
    if (load_ok[version]) loaded[version] = 1;
    return load_ok[version];
}
static int smbSharesConfigured(int version) { return configured[version]; }
static int smbSharesKnown(int version) { return configured[version]; }
static int smbSharesConnectedCount(int version) { return connected[version]; }
static __attribute__((unused)) int smbSharesActivePath(const char *path, char *out, int size) {
    if (auto_v1 && !strncmp(path, "smb:/AUTO/", 10))
        return snprintf(out, size, "smbold:/%s", path + 10) < size;
    return snprintf(out, size, "%s", path) < size;
}
static void smbSharesConnect(int version, int retry_failed) {
    (void)retry_failed;
    connect_attempts[version]++;
    if (configured[version] && loaded[version] && connect_ok[version])
        connected[version] = configured[version];
}
static int dashWorkerOnThread(void) { return worker; }
static int smbPathKind(const char *path);
static int readNetDir(const char *path, FILEINFO *info, int max, int *failed) {
    (void)info; (void)max; (void)failed;
    net_calls++;
    snprintf(net_path, sizeof(net_path), "%s", path);
    return connected[smbPathKind(path)] ? 1 : 0;
}
"""
code += function(cnf, "int smbPathKind(")
code += function(cnf, "int smbIsRootPath(")
code += function(filer, "int readSMB(")
code += r"""
static void reset_case(void) {
    memset(loaded, 0, sizeof(loaded));
    memset(configured, 0, sizeof(configured));
    memset(connected, 0, sizeof(connected));
    memset(load_attempts, 0, sizeof(load_attempts));
    memset(connect_attempts, 0, sizeof(connect_attempts));
    load_ok[SMB_PATH_V1] = load_ok[SMB_PATH_V2] = load_ok[SMB_PATH_NFS] = 1;
    connect_ok[SMB_PATH_V1] = connect_ok[SMB_PATH_V2] = connect_ok[SMB_PATH_NFS] = 1;
    net_calls = worker = auto_v1 = smb_dir_open_failed = 0;
    net_path[0] = 0;
}

int main(void) {
    FILEINFO files[4];
    assert(smbSharesConfigured != NULL);

    reset_case();
    configured[SMB_PATH_V1] = 1;
    assert(readSMB("smb:/", files, 4) == 0);
    assert(load_attempts[SMB_PATH_V1] == 1 && load_attempts[SMB_PATH_V2] == 0);
    assert(connect_attempts[SMB_PATH_V1] == 1 && connect_attempts[SMB_PATH_V2] == 0);
    assert(connected[SMB_PATH_V1] == 1 && !smb_dir_open_failed && net_calls == 0);

    reset_case();
    configured[SMB_PATH_V1] = 1;
    assert(readSMB("smbold:/", files, 4) == 1);
    assert(load_attempts[SMB_PATH_V1] == 1 && connect_attempts[SMB_PATH_V1] == 1);
    assert(!smb_dir_open_failed && net_calls == 1);

    reset_case();
    configured[SMB_PATH_V2] = 1;
    assert(readSMB("smb:/", files, 4) == 1);
    assert(load_attempts[SMB_PATH_V2] == 1 && connect_attempts[SMB_PATH_V2] == 1);
    assert(load_attempts[SMB_PATH_V1] == 0 && !smb_dir_open_failed);

    reset_case();
    configured[SMB_PATH_V1] = configured[SMB_PATH_V2] = 1;
    loaded[SMB_PATH_V1] = connected[SMB_PATH_V1] = 1;
    assert(readSMB("smb:/", files, 4) == 1);
    assert(load_attempts[SMB_PATH_V2] == 1 && load_attempts[SMB_PATH_V1] == 0);
    assert(connected[SMB_PATH_V2] == 1 && !smb_dir_open_failed);

    reset_case();
    configured[SMB_PATH_V1] = 1;
    worker = 1;
    assert(readSMB("smb:/", files, 4) == -ENODEV);
    assert(load_attempts[SMB_PATH_V1] == 0 && load_attempts[SMB_PATH_V2] == 0);
    assert(connect_attempts[SMB_PATH_V1] == 0 && connect_attempts[SMB_PATH_V2] == 0);

    reset_case();
    configured[SMB_PATH_V1] = 1;
    loaded[SMB_PATH_V1] = connected[SMB_PATH_V1] = 1;
    worker = 1;
    assert(readSMB("smb:/", files, 4) == 0);
    assert(load_attempts[SMB_PATH_V1] == 0 && load_attempts[SMB_PATH_V2] == 0);
    assert(!smb_dir_open_failed && net_calls == 0);

    /* Mixed config, working SMB1, unavailable SMB2 driver. The dashboard adds
       the SMB1 row independently once this read returns. */
    reset_case();
    configured[SMB_PATH_V1] = configured[SMB_PATH_V2] = 1;
    loaded[SMB_PATH_V1] = connected[SMB_PATH_V1] = 1;
    load_ok[SMB_PATH_V2] = 0;
    worker = 1;
    assert(readSMB("smb:/", files, 4) == 0);
    assert(!smb_dir_open_failed && net_calls == 0 && load_attempts[SMB_PATH_V2] == 0);
    worker = 0;
    assert(readSMB("smb:/", files, 4) == 0);
    assert(!smb_dir_open_failed && net_calls == 0 && load_attempts[SMB_PATH_V2] == 1);

    /* A loaded SMB2 driver without a connected share is also optional. */
    loaded[SMB_PATH_V2] = 1;
    connect_ok[SMB_PATH_V2] = 0;
    assert(readSMB("smb:/", files, 4) == 0);
    assert(!smb_dir_open_failed && net_calls == 0 && connect_attempts[SMB_PATH_V2] == 1);
    connect_ok[SMB_PATH_V2] = 1;
    assert(readSMB("smb:/", files, 4) == 1);
    assert(!smb_dir_open_failed && net_calls == 1);

    /* Cached library/scan paths follow an Automatic share's SMB1 session. */
    reset_case();
    configured[SMB_PATH_V2] = 1;
    loaded[SMB_PATH_V1] = connected[SMB_PATH_V1] = 1;
    loaded[SMB_PATH_V2] = 1;
    auto_v1 = worker = 1;
    assert(readSMB("smb:/AUTO/APPS/", files, 4) == 1);
    assert(!strcmp(net_path, "smbold:/APPS/") && !smb_dir_open_failed);
    assert(load_attempts[SMB_PATH_V1] == 0 && load_attempts[SMB_PATH_V2] == 0);

    puts("PASS: SMB root reads connect on the UI thread and preserve a usable SMB1 root in mixed configurations");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="smb-root-read-") as tmp:
    source = Path(tmp) / "root.c"
    exe = Path(tmp) / "root"
    source.write_text(code)
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A C host compiler is required"
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
