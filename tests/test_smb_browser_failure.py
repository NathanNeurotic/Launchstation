"""Verify that Files reports the SMB driver or share failure reason instead of hiding it."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text(encoding="utf-8")


def extract_block(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


helper = extract_block("static void smbStatusMessage(int version, char *out, int out_size)")
assert "smbStatusMessage(smbPathKind(failed), why, sizeof(why));" in source
assert "smbStatusMessage(smb_version, why, sizeof(why));" in source
assert "smbStatusMessage(v, why, sizeof(why));" in source

c = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SMB_PATH_NONE = 0, SMB_PATH_V1 = 1, SMB_PATH_V2 = 2, SMB_PATH_NFS = 3 };

static int driver_loaded[4], known[4], status_calls, combined_calls, last_version;
static const char *load_error[4];
static int isSmbDriverLoaded(int v) { assert(v >= 1 && v <= 3); return driver_loaded[v]; }
static const char *smbLoadError(int v) { assert(v >= 1 && v <= 3); return load_error[v] ? load_error[v] : ""; }
static int smbSharesKnown(int v) { assert(v >= 1 && v <= 3); return known[v]; }
static void dashStrCopy(char *out, const char *s, int n) { snprintf(out, (size_t)n, "%s", s); }
static void smbSharesStatus(int v, char *out, int n) {
    status_calls++; last_version = v;
    if (v == SMB_PATH_V1)
        snprintf(out, (size_t)n, "OLDBOX (line 4): Server did not answer");
    else
        snprintf(out, (size_t)n, "Games (line 4): Could not connect");
}
static void smbSharesCombinedStatus(char *out, int n) {
    combined_calls++;
    snprintf(out, (size_t)n, "Combined: 1 share issue");
}
""" + helper + r"""
int main(void) {
    char msg[96];
    known[SMB_PATH_V2] = 1;
    load_error[SMB_PATH_V2] = "The network stack could not be started.";
    smbStatusMessage(2, msg, sizeof(msg));
    assert(!strcmp(msg, load_error[SMB_PATH_V2]) && status_calls == 0);

    load_error[SMB_PATH_V2] = "";
    smbStatusMessage(2, msg, sizeof(msg));
    assert(!strcmp(msg, "SMB driver is not ready; reopen this device to retry.") && status_calls == 0);

    driver_loaded[SMB_PATH_V2] = 1;
    smbStatusMessage(2, msg, sizeof(msg));
    assert(!strcmp(msg, "Games (line 4): Could not connect") && status_calls == 1 && last_version == 2);

    /* SMB1-only config on smb:/ must report V1 error/status, never "no SMB2/SMB3 share". */
    known[SMB_PATH_V2] = 0; known[SMB_PATH_V1] = 1;
    driver_loaded[SMB_PATH_V1] = 0; load_error[SMB_PATH_V1] = "V1 load failed";
    smbStatusMessage(SMB_PATH_V2, msg, sizeof(msg));
    assert(!strcmp(msg, "V1 load failed"));

    driver_loaded[SMB_PATH_V1] = 1;
    smbStatusMessage(SMB_PATH_V2, msg, sizeof(msg));
    assert(!strcmp(msg, "OLDBOX (line 4): Server did not answer") && last_version == SMB_PATH_V1);

    /* Combined V2+V1 uses combined status once loaded. */
    known[SMB_PATH_V2] = 1; known[SMB_PATH_V1] = 1;
    smbStatusMessage(SMB_PATH_V2, msg, sizeof(msg));
    assert(!strcmp(msg, "Combined: 1 share issue") && combined_calls == 1);

    puts("PASS: Files shows the SMB driver or share failure reason");
    return 0;
}
"""

cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="smb-browser-failure-") as tmp:
    c_file, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    c_file.write_text(c, encoding="utf-8")
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
