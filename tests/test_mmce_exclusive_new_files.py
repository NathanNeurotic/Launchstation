"""Execute the actual exclusive-write helper with preexisting files and failures.

Stock sd2psXtd forwards O_EXCL through MMCEMAN FS_OPEN to SdFat.
This test isolates the new-file path without altering the legacy replacement APIs.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/mc_card_export.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")


def function(signature):
    at = source.index(signature)
    p = source.index("{", at) + 1
    level = 1
    while level:
        if p >= len(source):
            raise AssertionError("Unbalanced C function: " + signature)
        level += (source[p] == "{") - (source[p] == "}")
        p += 1
    return source[at:p]


writer = function("static int write_exact_at_new(")
copy = function("static int mcCardBackupNoEccImpl(")
convert = function("static int mcCardImportPcsx2Impl(")
assert "FIO_O_CREAT | FIO_O_EXCL" in writer
assert "FIO_O_TRUNC" not in writer
assert "*owned = 1;" in writer
assert writer.index("*owned = 1;") < writer.index("genWrite(")
for fn in (copy, convert):
    assert "write_exact_at_new(" in fn
    assert "if (!only_new || owned)" in fn
assert "mcCardBackupNoEccNewFile(stage, dest" in browser
assert "mcCardImportPcsx2NewFile(source, stage" in browser
assert "mcCardBackupNoEccNewFile(source, stage" in browser
assert "fileXioDread(fd, &ent) > 0" in browser
assert "python3 tests/test_mmce_exclusive_new_files.py" in workflow

h = r"""
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef uint64_t u64;
typedef int64_t s64;
typedef unsigned char u8;
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
static int fail_write;
static int genOpen(const char *p, int flags) { return open(p, flags, 0600); }
static int genWrite(int fd, void *buf, int len) {
    return fail_write ? -1 : (int)write(fd, buf, (size_t)len);
}
static s64 genLseek(int fd, s64 pos, int whence) {
    return (s64)lseek(fd, (off_t)pos, whence);
}
static int genClose(int fd) { return close(fd); }
"""
h += writer + "\n" + r"""
int main(int argc, char **argv) {
    unsigned char buf[6] = {'A', 'B', 'C', 'X', 'Y', 'Z'};
    unsigned char got[6];
    int owned = 0, fd;
    if (argc != 3) return 3;
    assert(write_exact_at_new(argv[1], 0, buf, 3, 1, &owned) < 0);
    assert(!owned);
    fd = open(argv[1], O_RDONLY); assert(fd >= 0);
    assert(read(fd, got, 4) == 4);
    assert(!memcmp(got, "KEEP", 4)); close(fd);
    assert(write_exact_at_new(argv[2], 0, buf, 3, 1, &owned) == 0);
    assert(owned);
    assert(write_exact_at_new(argv[2], 3, buf + 3, 3, 0, &owned) == 0);
    fd = open(argv[2], O_RDONLY); assert(fd >= 0);
    assert(read(fd, got, 6) == 6);
    assert(!memcmp(got, "ABCXYZ", 6)); close(fd);
    assert(write_exact_at_new(argv[2], 0, buf, 3, 1, &owned) < 0);
    assert(fail_write == 0);
    puts("PASS: exclusive create protects existing files; sequential chunks remain intact");
}
"""
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="mmce-exclusive-") as td:
    td = Path(td)
    existing, new = td / "existing.mcd.PART", td / "new.mcd.PART"
    existing.write_bytes(b"KEEP")
    src = td / "test.c"
    exe = td / "test"
    src.write_text(h, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(existing), str(new)], check=True)
    assert existing.read_bytes() == b"KEEP"
    assert new.read_bytes() == b"ABCXYZ"

print("PASS: MMCE new-only outputs cannot truncate preexisting stage/destination")
