"""Verify that Files reports the SMB probe reason instead of hiding it."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text(encoding="utf-8")


def function(signature):
    begin = source.index(signature)
    pos = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[begin:pos] + "\n"


helper = function("static void smbStatusMessage(int version, char *out, int out_size)\n{")
activation = function("static void activate(void)\n{")
list_done = function("static void listDone(void *arg)\n{")
assert "smbStatusMessage(smbPathKind(path), why, sizeof(why))" in activation
assert activation.index("smbStatusMessage(smbPathKind(path)") < activation.index(
    'dashToast(DASH_TOAST_WARN, "Device not found")'
)
assert "smbStatusMessage(v, why, sizeof(why))" in list_done

c = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int driver_loaded;
static int status_calls, last_version;
static const char *load_error;

static int isSmbDriverLoaded(int version) {
    assert(version == 1 || version == 2);
    return driver_loaded;
}
static const char *smbLoadError(int version) {
    assert(version == 1 || version == 2);
    return load_error;
}
static void dashStrCopy(char *out, const char *value, int size) {
    snprintf(out, (size_t)size, "%s", value);
}
static void smbSharesStatus(int version, char *out, int size) {
    status_calls++;
    last_version = version;
    snprintf(out, (size_t)size, "Games (line 4): Could not connect");
}
"""
c += helper
c += r"""
int main(void) {
    char message[96];

    load_error = "The network stack could not be started.";
    smbStatusMessage(2, message, sizeof(message));
    assert(!strcmp(message, load_error));
    assert(status_calls == 0);

    load_error = "";
    smbStatusMessage(2, message, sizeof(message));
    assert(!strcmp(message, "SMB driver is not ready; reopen this device to retry."));
    assert(status_calls == 0);

    driver_loaded = 1;
    smbStatusMessage(2, message, sizeof(message));
    assert(!strcmp(message, "Games (line 4): Could not connect"));
    assert(status_calls == 1 && last_version == 2);

    puts("PASS: Files shows the SMB driver or share failure reason");
    return 0;
}
"""

compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="smb-browser-failure-") as tmp:
    c_file = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c_file.write_text(c, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(c_file), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
