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

static int driver_loaded, status_calls, last_version;
static const char *load_error;
static int isSmbDriverLoaded(int v) { assert(v == 1 || v == 2); return driver_loaded; }
static const char *smbLoadError(int v) { assert(v == 1 || v == 2); return load_error; }
static void dashStrCopy(char *out, const char *s, int n) { snprintf(out, (size_t)n, "%s", s); }
static void smbSharesStatus(int v, char *out, int n) {
    status_calls++; last_version = v;
    snprintf(out, (size_t)n, "Games (line 4): Could not connect");
}
""" + helper + r"""
int main(void) {
    char msg[96];
    load_error = "The network stack could not be started.";
    smbStatusMessage(2, msg, sizeof(msg));
    assert(!strcmp(msg, load_error) && status_calls == 0);

    load_error = "";
    smbStatusMessage(2, msg, sizeof(msg));
    assert(!strcmp(msg, "SMB driver is not ready; reopen this device to retry.") && status_calls == 0);

    driver_loaded = 1;
    smbStatusMessage(2, msg, sizeof(msg));
    assert(!strcmp(msg, "Games (line 4): Could not connect") && status_calls == 1 && last_version == 2);
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
