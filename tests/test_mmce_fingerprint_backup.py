"""Run the MMCE backup's real fingerprint and sidecar code on host files.

fp_read, card_fingerprint, read_sidecar, store_sidecar, write_sidecar and
fingerprint_matches
are compiled from src/mmce_backup.c with POSIX stand-ins for genOpen & co, and
linked with the real card viewer. The cards come from tests/mc_fixture.py.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).parent))
import mc_fixture as fx

root = Path(__file__).resolve().parents[1]
source = (root / "src/mmce_backup.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mmce_fingerprint_backup.py" in workflow


def extract(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


defs = source[source.index("#define MMCE_FP_TAG "):source.index("static int bcd_to_int(")]
funcs = "".join(extract(s) for s in (
    "static int fp_read(", "static int card_fingerprint(", "static int sidecar_path(",
    "static int read_sidecar(", "static int store_sidecar(", "static void write_sidecar(", "static int fingerprint_matches("))

harness = r'''
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mc_card_view.h"
typedef unsigned char u8;
typedef unsigned int u32;
typedef long long s64;
#define MAX_PATH 1024
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
static int opens, reads, max_open, now_open;
static int fail_write;
static int genOpen(const char *p, int f) {
    int fd = open(p, f, 0644);
    if (fd >= 0) { opens++; now_open++; if (now_open > max_open) max_open = now_open; }
    return fd;
}
static int genClose(int fd) { now_open--; return close(fd); }
static s64 genLseek(int fd, s64 off, int wh) { return lseek(fd, off, wh); }
static int genRead(int fd, void *b, int n) { reads++; return (int)read(fd, b, (size_t)n); }
static int genWrite(int fd, const void *b, int n) { return fail_write ? n - 1 : (int)write(fd, b, (size_t)n); }
static int genRemove(const char *p) { return unlink(p); }
''' + defs + funcs + r'''
int main(int argc, char **argv) {
    u8 fp[MCV_FINGERPRINT_SIZE];
    int skips = -1;
    const char *cmd = argv[1];
    if (argc < 3) return 2;
    if (!strcmp(cmd, "fp")) {
        int r = card_fingerprint(argv[2], fp), i;
        printf("r=%d opens=%d max_open=%d reads=%d fp=", r, opens, max_open, reads);
        for (i = 0; i < MCV_FINGERPRINT_SIZE; i++) printf("%02x", fp[i]);
        printf("\n");
    } else if (!strcmp(cmd, "seal")) {
        fail_write = argc > 3;
        write_sidecar(argv[2]);
        printf("sidecar=%d\n", read_sidecar(argv[2], fp, &skips));
    } else if (!strcmp(cmd, "match")) {
        int consume = argc > 4 && atoi(argv[4]), m;
        fail_write = argc > 5;
        m = fingerprint_matches(argv[2], argv[3], consume);
        fail_write = 0;
        if (read_sidecar(argv[3], fp, &skips) < 0) skips = -1;
        printf("match=%d max_open=%d skips=%d\n", m, max_open, skips);
    }
    return 0;
}
'''

cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
tmp = Path(tempfile.mkdtemp(prefix="mmce-fp-"))
exe = tmp / "fp"
(tmp / "h.c").write_text(harness, encoding="utf-8")
subprocess.run([cc, "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", f"-I{root/'include'}", str(tmp / "h.c"),
                str(root / "src/mc_card_view.c"), str(root / "src/mc_sha256.c"), "-o", str(exe)], check=True)


def run(*a):
    out = subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, timeout=60)
    assert out.returncode == 0, (a, out.stderr[-600:])
    return dict(kv.split("=", 1) for kv in out.stdout.split())


def card(history=b"h" * 900, save=b"s" * 4000, ecc=False, extra=False):
    c = fx.Card(8, ecc)
    c.add_save("BADATA-SYSTEM", {"history": history})
    c.add_save("BASLUS-12345SAVE", {"icon.sys": fx.icon_sys(b"Game", 0), "SAVE.DAT": save})
    for i in range(40 if extra else 0):
        c.add_save(f"BESLES-{i:05d}", {"d": bytes([i]) * 2000})
    return c.finish().image()


live = tmp / "Card1-1.mcd"
hist = tmp / "WLE-20261028-010203.mcd"
live.write_bytes(card(extra=True))
shutil.copy(live, hist)

d = run("fp", live)
assert d["r"] == "0" and d["max_open"] == "1", d          # one open file at a time (MMCE rule)
reads = int(d["reads"])
assert reads * 4096 < 512 * 1024, d                         # index only: far below 8 MiB
print(f"  fingerprint of an 8 MiB card with 41 saves: {reads} block reads ({reads * 4} KiB)")

# no sidecar yet: cannot tell, so the caller does a full compare
assert run("match", live, hist)["match"] == "-1"
assert run("seal", hist)["sidecar"] == "0"
fp_file = Path(str(hist) + ".FP")
assert fp_file.stat().st_size == 37 and fp_file.read_bytes()[:4] == b"LSF2"
d = run("match", live, hist)
assert d == {"match": "1", "max_open": "1", "skips": "0"}, d   # a read-only scan uses up nothing

# Same-size rewrite under a stuck clock is invisible to the index, so a match
# is trusted MMCE_FP_MAX_SKIPS (8) backups in a row, then a full compare is due.
for i in range(1, 9):
    d = run("match", live, hist, 1)
    assert d == {"match": "1", "max_open": "1", "skips": str(i)}, (i, d)
assert run("match", live, hist, 1) == {"match": "-1", "max_open": "1", "skips": "8"}
assert run("match", live, hist)["match"] == "-1"            # the pending scan compares too
assert run("seal", hist)["sidecar"] == "0"                  # full compare said same: count restarts
assert run("match", live, hist, 1)["skips"] == "1"
# a count that cannot be stored never skips: the sidecar goes, a full compare runs
d = run("match", live, hist, 1, "fail")
assert d["match"] == "-1" and d["skips"] == "-1" and not fp_file.exists(), d
assert run("seal", hist)["sidecar"] == "0"
# an LSF1 sidecar from the previous build has no count: not trusted
fp_file.write_bytes(b"LSF1" + fp_file.read_bytes()[4:36])
assert run("match", live, hist, 1)["match"] == "-1"
assert run("seal", hist)["sidecar"] == "0"

# a game start rewrites only the system history: still unchanged
live.write_bytes(card(history=b"H" * 900, extra=True))  # rewritten in place
assert run("match", live, hist)["match"] == "1"
# a new or resized save: changed
live.write_bytes(card(save=b"s" * 4100, extra=True))
assert run("match", live, hist)["match"] == "0"
live.write_bytes(card(extra=False))
assert run("match", live, hist)["match"] == "0"

# a PCSX2 (ECC) history copy matches the raw card it was made from
ecc_hist = tmp / "WLE-20261028-010204.ps2"
ecc_hist.write_bytes(card(ecc=True))
live.write_bytes(card())
assert run("seal", ecc_hist)["sidecar"] == "0"
assert run("match", live, ecc_hist)["match"] == "1"

# damaged or foreign sidecars are never trusted
good = fp_file.read_bytes()
for bad in (good[:36], good + b"x", b"XXXX" + good[4:], b""):
    fp_file.write_bytes(bad)
    assert run("match", live, hist)["match"] == "-1", bad[:6]
fp_file.write_bytes(good)
# an unreadable card is "cannot tell", not "unchanged"
live.write_bytes(b"\0" * 8388608)
assert run("match", live, hist)["match"] == "-1"
live.unlink()
assert run("match", live, hist)["match"] == "-1"

# a short write leaves no sidecar behind
fp_file.unlink()
assert run("seal", hist, "fail")["sidecar"] == "-1"
assert not fp_file.exists()
# a history copy that is not a card gets no sidecar
junk = tmp / "WLE-junk.mcd"
junk.write_bytes(b"\x55" * 8388608)
assert run("seal", junk)["sidecar"] == "-1"
assert not Path(str(junk) + ".FP").exists()

print("PASS: MMCE backup fingerprint sidecars: index-only reads, one open file, "
      "history-only change ignored, full compare at least every 8 skips, "
      "damaged sidecars and cards fall back to a full compare")
