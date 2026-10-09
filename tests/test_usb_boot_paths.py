"""Exercise production boot-path normalization and USB stack selection.

Compile extracted Launchstation C functions in exFAT and legacy configurations.
This does not emulate USB/IOP hardware behavior.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/main_startup.c").read_text()


def extract(signature):
    pos = source.index(signature)
    start = pos
    pos = source.index("{", pos) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


helpers = [
    "static int isDevicePathTerminator(char c)\\n{",
    "static int isBootUsbMassPath(const char *path)\\n{",
    "static int isBootUsbStackPath(const char *path)\\n{",
    "static int isBootAtaPath(const char *path)\\n{",
    "static int isBootIlinkPath(const char *path)\\n{",
    "static int isBootHddPath(const char *path)\\n{",
    "static int isBootXfromPath(const char *path)\\n{",
    "static int isSlashRootedBootPathDevice(const char *path)\\n{",
    "static void normalizeBootPath(char *path)\\n{",
]
# Written as escaped signatures so the test does not depend on line endings.
functions = "".join(extract(sig.replace("\\n", "\n")) for sig in helpers)

program = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 256
"""
program += functions
program += r"""
static void normalized(const char *input, const char *expected) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s", input);
    normalizeBootPath(path);
    assert(!strcmp(path, expected));
}
int main(void) {
    normalized("usb0:APPS\\L.ELF", "usb0:/APPS/L.ELF");
    normalized("ata:APPS/L.ELF", "ata0:/APPS/L.ELF");
    normalized("mass:/APPS/L.ELF", "mass:/APPS/L.ELF");
    normalized("mass9:APPS/L.ELF", "mass9:/APPS/L.ELF");
    assert(isBootUsbStackPath("usb0:/APPS/L.ELF"));
    assert(isBootUsbStackPath("usb1:/APPS/L.ELF"));
    assert(isBootUsbStackPath("mass:/APPS/L.ELF"));
    assert(!isBootUsbStackPath("ata0:/APPS/L.ELF"));
#ifdef EXFAT
    normalized("mass0:/APPS/L.ELF", "mass0:/APPS/L.ELF");
    normalized("mass0:APPS/L.ELF", "mass0:/APPS/L.ELF");
    assert(!isBootUsbStackPath("mass0:/APPS/L.ELF"));
    assert(!isBootUsbStackPath("mass9:/APPS/L.ELF"));
    assert(isSlashRootedBootPathDevice("mass0:/APPS/L.ELF"));
#else
    normalized("mass0:/APPS/L.ELF", "mass:/APPS/L.ELF");
    normalized("mass0:APPS/L.ELF", "mass:/APPS/L.ELF");
    assert(isBootUsbStackPath("mass0:/APPS/L.ELF"));
#endif
    puts("PASS: boot paths preserve shared exFAT BDM identity and legacy USB alias");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="launchstation-usb-boot-") as tmp:
    c = Path(tmp) / "boot.c"
    exe = Path(tmp) / "boot"
    c.write_text(program)
    for variant in ("exfat", "legacy"):
        flags = ["-DEXFAT=1"] if variant == "exfat" else []
        subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", *flags,
                        str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=5)
