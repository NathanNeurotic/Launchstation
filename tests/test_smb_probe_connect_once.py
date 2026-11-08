"""Exercise the actual SMB probe branch without requiring PS2 IOP modules.

A fresh load_smb() already calls smbSharesConnect(); probing must not call
it twice, but an existing loaded driver must still permit a manual retry.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/dash_devices.c").read_text()
# This probe optimization relies on the production loader doing the initial
# connection. Fail the test if that cross-file contract changes.
loader = (root / "src/init.c").read_text().split("int load_smb(int version)", 1)[1].split("\n}", 1)[0]
assert "smbSharesConnect(version, 1);" in loader
start = src.index("\telse if (smbPathKind(m) != SMB_PATH_NONE) {")
end = src.index("\n\t}\n#endif", start) + len("\n\t}")
branch = src[start:end]
code = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SMB_PATH_NONE 0
static int loaded, can_load, can_connect, connected;
static int loads, connects;
static int smbPathKind(const char *m) {
    return !strncmp(m, "smb1:", 5) ? 1 : !strncmp(m, "smb:", 4) ? 2 : 0;
}
static int isSmbDriverLoaded(int version) {
    assert(version == 1 || version == 2);
    return loaded;
}
static int smbSharesConnectedCount(int version) {
    assert(version == 1 || version == 2);
    return connected;
}
static void smbSharesConnect(int version, int retry) {
    assert((version == 1 || version == 2) && retry);
    connects++;
    if (can_connect) connected = 1;
}
static int load_smb(int version) {
    assert(version == 1 || version == 2);
    loads++;
    if (!can_load) return 0;
    loaded = 1;
    smbSharesConnect(version, 1);
    return 1;
}
static int probe(const char *m) {
    int ok = 0;
    if (0) {}
""" + branch + r"""
    return ok;
}
static void reset(void) {
    loads = connects = loaded = connected = 0;
    can_load = can_connect = 1;
}
int main(void) {
    reset();
    assert(probe("smb:") == 1);
    assert(loads == 1 && connects == 1 && connected);
    reset();
    loaded = 1;
    assert(probe("smb1:") == 1);
    assert(loads == 0 && connects == 1);
    reset();
    can_load = 0;
    assert(probe("smb:") == 0);
    assert(loads == 1 && connects == 0);
    reset();
    can_connect = 0;
    assert(probe("smb:") == 0);
    assert(loads == 1 && connects == 1);
    puts("PASS: SMB initial driver load connects once; existing driver retries");
    return 0;
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host compiler required"
with tempfile.TemporaryDirectory(prefix="smb-probe-connect-") as tmp:
    c, exe = Path(tmp) / "probe.c", Path(tmp) / "probe"
    c.write_text(code, encoding="utf-8")
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
