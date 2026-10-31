"""Compile the real Drive upload ledger helpers from src/cloud_google.c on the host.

The ledger lets a sync skip the Drive lookup for per-save .psu versions it
already uploaded. It must only ever match the exact folder, digest and name,
survive a torn last line, ignore a corrupt or oversized file, and never be
consulted for card images.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/cloud_google.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_cloud_upload_ledger.py" in workflow


def extract(text, signature):
    start = text.index(signature)
    at = text.index("{", start) + 1
    depth = 1
    in_string = in_char = escaped = False
    while depth:
        c = text[at]
        if in_string or in_char:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"' and in_string:
                in_string = False
            elif c == "'" and in_char:
                in_char = False
        elif c == '"':
            in_string = True
        elif c == "'":
            in_char = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        at += 1
    return text[start:at] + "\n"


# Wiring: only .psu files use the ledger, the digest is taken from the local
# bytes before the lookup is skipped, and it is recorded only after Drive
# confirmed the file (uploaded, or already there).
sync = extract(source, "static int sync_tree(")
assert 'int psu = genCmpFileExt(ent.name, "PSU");' in sync
assert "if (psu && hash_local_file(child, sha256) == 0 &&\n" \
       "                upload_ledger_has(local_dir, remote_parent, sha256, ent.name))" in sync
assert sync.index("upload_ledger_has(") < sync.index("validate_backup_file(")
# an empty or oversized .psu is refused from its directory size, before it is read
assert "ent.stat.size > MMCE_SAVE_VERSION_MAX" in sync and "ent.stat.hisize != 0" in sync
assert sync.index("ent.stat.size > MMCE_SAVE_VERSION_MAX") < sync.index("hash_local_file(")
assert sync.index("if (r < 0) {") < sync.index("upload_ledger_add(")
assert "sha256[0] ? sha256 : NULL" in sync
upload = extract(source, "static int drive_upload_file(")
assert "memcpy(sha256, known_sha256, sizeof(sha256));" in upload
assert "if (genLseek(fd, 0, SEEK_SET) != 0) {" in upload, "a known digest must still rewind before upload"

defines = "\n".join(l for l in source.splitlines() if l.startswith("#define CLOUD_UPLOAD_LEDGER"))
helpers = "".join(extract(source, sig) for sig in (
    "static int upload_ledger_line(", "static int upload_ledger_has(", "static void upload_ledger_add("))

harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
typedef int64_t s64;
#define MAX_PATH 1025
#define CLOUD_ID_MAX 192
enum { MAX_NAME = 256 };
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_RDWR O_RDWR
#define FIO_O_CREAT O_CREAT
static int open_files, fail_write;
static int genOpen(const char *path, int mode)
{
    int fd = open(path, mode, 0644);
    if (fd >= 0)
        open_files++;
    return fd;
}
static s64 genLseek(int fd, s64 where, int how) { return lseek(fd, where, how); }
static int genRead(int fd, void *buf, int size) { return (int)read(fd, buf, (size_t)size); }
static int genWrite(int fd, void *buf, int size)
{
    return fail_write ? -1 : (int)write(fd, buf, (size_t)size);
}
static int genClose(int fd)
{
    open_files--;
    return close(fd);
}
''' + defines + "\n" + helpers + r'''
int main(int argc, char **argv)
{
    int r = 0;
    fail_write = getenv("FAIL_WRITE") != NULL;
    if (argc == 6 && !strcmp(argv[1], "has"))
        r = upload_ledger_has(argv[2], argv[3], argv[4], argv[5]);
    else if (argc == 6 && !strcmp(argv[1], "add"))
        upload_ledger_add(argv[2], argv[3], argv[4], argv[5]);
    else
        return 2;
    printf("%d %d\n", r, open_files);
    return 0;
}
'''

tmp = Path(tempfile.mkdtemp(prefix="upload-ledger-"))
try:
    (tmp / "h.c").write_text(harness, encoding="utf-8")
    tool = tmp / "tool"
    subprocess.run(["gcc", "-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(tmp / "h.c"), "-o", str(tool)], check=True)

    def call(*args, env=None):
        import os
        out = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True,
                             env=dict(os.environ, **(env or {})), timeout=60)
        assert out.returncode == 0, (args, out.stdout, out.stderr[-2000:])
        r, still_open = out.stdout.split()
        assert still_open == "0", ("file left open", args)
        return int(r)

    d = tmp / "Save"
    d.mkdir()
    ledger = d / "UPLOADED.TXT"
    sha_a, sha_b = "a" * 64, "b" * 64
    name = "WLE-20261031-010000.psu"

    # nothing recorded yet, and no file is created by a lookup
    assert call("has", d, "folder1", sha_a, name) == 0 and not ledger.exists()
    call("add", d, "folder1", sha_a, name)
    assert ledger.read_text() == f"folder1 {sha_a} {name}\n"
    assert call("has", d, "folder1", sha_a, name) == 1
    assert call("has", str(d) + "/", "folder1", sha_a, name) == 1, "a trailing slash names the same ledger"
    # changed bytes, another Drive folder (account) or another name: look up again
    assert call("has", d, "folder1", sha_b, name) == 0
    assert call("has", d, "folder2", sha_a, name) == 0
    assert call("has", d, "folder1", sha_a, "WLE-20261031-020000.psu") == 0
    assert call("has", d, "older1", sha_a, name) == 0, "no partial-line match"

    call("add", d, "folder1", sha_b, "WLE-20261031-020000.psu")
    assert call("has", d, "folder1", sha_a, name) == 1
    assert call("has", d, "folder1", sha_b, "WLE-20261031-020000.psu") == 1

    # a torn last line (power cut mid-append) neither matches nor swallows the next entry
    with ledger.open("a") as f:
        f.write(f"folder1 {sha_a} WLE-torn")
    assert call("has", d, "folder1", sha_a, "WLE-torn") == 0
    call("add", d, "folder1", sha_a, "WLE-20261031-030000.psu")
    assert call("has", d, "folder1", sha_a, "WLE-20261031-030000.psu") == 1
    assert ledger.read_text().endswith(f"WLE-torn\nfolder1 {sha_a} WLE-20261031-030000.psu\n")

    # a failed write records nothing and fails nothing
    before = ledger.read_bytes()
    call("add", d, "folder1", sha_a, "WLE-20261031-040000.psu", env={"FAIL_WRITE": "1"})
    assert ledger.read_bytes() == before

    # corrupt bytes are only a miss
    junk = tmp / "Junk"
    junk.mkdir()
    (junk / "UPLOADED.TXT").write_bytes(b"\0\xff" * 100)
    assert call("has", junk, "folder1", sha_a, name) == 0

    # an oversized ledger is ignored and not grown further
    big = tmp / "Big"
    big.mkdir()
    line = f"folder1 {sha_a} {name}\n".encode()
    (big / "UPLOADED.TXT").write_bytes(line + b"x" * (64 * 1024))
    size = (big / "UPLOADED.TXT").stat().st_size
    assert call("has", big, "folder1", sha_a, name) == 0
    call("add", big, "folder1", sha_b, name)
    assert (big / "UPLOADED.TXT").stat().st_size == size

    # an empty directory name is refused
    assert call("has", "", "folder1", sha_a, name) == 0
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("PASS: Drive upload ledger: exact matches only, torn lines, failed writes, corrupt and oversized files")
