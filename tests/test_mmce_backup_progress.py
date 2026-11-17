"""MMCE "Back up all cards": the screen keeps moving and the card is not
reopened for every 64 KB.

Builds the real src/mc_card_export.c and the real compare/progress code from
src/mmce_backup.c on the host. Paths like mmce0:/x and mass0:/x map to files
in a temp folder; every open is counted per device.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
backup_src = (root / "src/mmce_backup.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mmce_backup_progress.py" in workflow


def extract(signature, src=backup_src):
    start = src.index(signature)
    pos = src.index("{", start) + 1
    depth = 1
    while depth:
        depth += (src[pos] == "{") - (src[pos] == "}")
        pos += 1
    return src[start:pos] + "\n"


tick_block = backup_src[backup_src.index("/* Progress inside one card."):
                        backup_src.index("static int files_equal_serial(")]

shim = r"""
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef unsigned char u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define MAX_PATH 1024
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define FIO_O_EXCL O_EXCL
extern char host_root[512];
extern int mmce_opens, other_opens, max_mmce_open, mmce_open_now;
static int fd_is_mmce[4096];
static const char *host_path(const char *p, char *out) {
    const char *c = strchr(p, ':');
    snprintf(out, 1024, "%s/%.*s_%s", host_root, (int)(c - p), p, c + 2);
    return out;
}
static int genOpen(const char *p, int f) {
    char h[1024];
    int fd = open(host_path(p, h), f, 0644);
    if (fd >= 0) {
        fd_is_mmce[fd] = !strncmp(p, "mmce", 4);
        if (fd_is_mmce[fd]) { mmce_opens++; if (++mmce_open_now > max_mmce_open) max_mmce_open = mmce_open_now; }
        else other_opens++;
    }
    return fd;
}
static int genRead(int fd, void *b, int n) { return (int)read(fd, b, (size_t)n); }
static int genWrite(int fd, void *b, int n) { return (int)write(fd, b, (size_t)n); }
static s64 genLseek(int fd, s64 o, int w) { return (s64)lseek(fd, (off_t)o, w); }
static int genClose(int fd) { if (fd_is_mmce[fd]) mmce_open_now--; return close(fd); }
static int genRemove(const char *p) { char h[1024]; return unlink(host_path(p, h)); }
"""

work = Path(tempfile.mkdtemp(prefix="mmce-progress-"))
try:
    inc = work / "inc"
    inc.mkdir()
    (inc / "launchelf.h").write_text(shim)
    shutil.copy(root / "include/mc_card_export.h", inc / "mc_card_export.h")
    (inc / "tamtypes.h").write_text("")

    harness = r"""
#include "launchelf.h"
#include "mc_card_export.h"
#define MMCE_BACKUP_IO 65536
typedef void (*MmceBackupProgress)(const char *source, int done, int total, int permille);
char host_root[512];
int mmce_opens, other_opens, max_mmce_open, mmce_open_now;
static u8 compare_a[MMCE_BACKUP_IO], compare_b[MMCE_BACKUP_IO];
""" + extract("static int read_chunk(") + extract("static int read_next(") + extract("static s64 file_size(") \
        + tick_block + extract("static int files_equal_serial(") + r"""
static int ticks, last_permille = -1, backwards;
static void on_tick(const char *s, int done, int total, int permille) {
    (void)s; (void)done; (void)total;
    if (permille < last_permille) backwards++;
    last_permille = permille; ticks++;
}
static void reset(void) { mmce_opens = other_opens = max_mmce_open = mmce_open_now = 0;
                          ticks = 0; last_permille = -1; backwards = 0; }
int main(int argc, char **argv) {
    int r;
    snprintf(host_root, sizeof(host_root), "%s", argv[1]);
    (void)argc;

    /* Copy an MMCE card to USB: the card is opened for the header check and
     * once more for both passes, not once per 64 KB chunk. */
    reset();
    memset(&card_tick, 0, sizeof(card_tick));
    card_tick.cb = on_tick; card_tick.total = 1; card_tick.done = 1;
    tick_step(333, 667);
    r = mcCardBackupNoEcc("mmce0:/card.mcd", "mass0:/copy.mcd", tick_export);
    printf("copy r=%d mmce=%d max=%d ticks=%d last=%d back=%d\n", r, mmce_opens, max_mmce_open, ticks, last_permille, backwards);

    /* Compare against that history copy, card held open. */
    reset();
    card_tick.last = 0; tick_step(0, 333);
    r = files_equal_serial("mmce0:/card.mcd", "mass0:/copy.mcd");
    printf("same r=%d mmce=%d max=%d ticks=%d last=%d back=%d\n", r, mmce_opens, max_mmce_open, ticks, last_permille, backwards);

    reset();
    r = files_equal_serial("mmce0:/card.mcd", "mass0:/other.mcd");
    printf("diff r=%d\n", r);

    /* Both on MMCE: the old open-per-chunk path, never two MMCE files open. */
    reset();
    card_tick.cb = NULL;
    r = mcCardBackupNoEcc("mmce0:/card.mcd", "mmce1:/copy.mcd", NULL);
    printf("mmce2mmce r=%d mmce=%d max=%d\n", r, mmce_opens, max_mmce_open);

    reset();
    r = mcCardExportPcsx2("mmce0:/card.mcd", "mass0:/card.ps2", NULL);
    printf("pcsx2 r=%d mmce=%d max=%d\n", r, mmce_opens, max_mmce_open);

    /* No callback (IGR backup at boot): nothing to call. */
    reset();
    r = files_equal_serial("mmce0:/card.mcd", "mass0:/copy.mcd");
    printf("quiet r=%d ticks=%d\n", r, ticks);
    return 0;
}
"""
    (work / "h.c").write_text(harness)
    exe = work / "h"
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I", str(inc), str(work / "h.c"), str(root / "src/mc_card_export.c"),
                    "-o", str(exe)], check=True)

    # 1 MiB no-ECC card: 512-byte pages, 2 pages per cluster, 1024 clusters.
    size = 512 * 2 * 1024
    header = bytearray(64)
    header[:28] = b"Sony PS2 Memory Card Format "
    header[40:42] = (512).to_bytes(2, "little")
    header[42:44] = (2).to_bytes(2, "little")
    header[48:52] = (1024).to_bytes(4, "little")
    card = bytes(header) + os.urandom(size - len(header))
    (work / "mmce0_card.mcd").write_bytes(card)
    other = bytearray(card)
    other[-1] ^= 0xFF
    (work / "mass0_other.mcd").write_bytes(bytes(other))

    out = subprocess.run([str(exe), str(work)], check=True, capture_output=True, text=True).stdout
    lines = {l.split()[0]: dict(kv.split("=") for kv in l.split()[1:]) for l in out.splitlines()}
    print(out, end="")
    chunks = size // 65536

    c = lines["copy"]
    assert c["r"] == "0" and (work / "mass0_copy.mcd").read_bytes() == card
    assert int(c["mmce"]) == 2, c                      # header check + one handle for both passes
    assert c["max"] == "1", c                          # never two MMCE files open
    assert int(c["ticks"]) >= 2 * chunks and c["back"] == "0" and c["last"] == "1000", c

    s = lines["same"]
    assert s["r"] == "1" and s["mmce"] == "2" and s["max"] == "1", s   # size check + one handle (was 1 + 1 per chunk)
    assert int(s["ticks"]) >= chunks and s["back"] == "0" and s["last"] == "333", s
    assert lines["diff"]["r"] == "0"

    m = lines["mmce2mmce"]
    assert m["r"] == "0" and m["max"] == "1", m
    assert int(m["mmce"]) > 4 * chunks, m              # unchanged per-chunk path
    assert (work / "mmce1_copy.mcd").read_bytes() == card

    p = lines["pcsx2"]
    assert p["r"] == "0" and int(p["mmce"]) == 2 and p["max"] == "1", p
    assert (work / "mass0_card.ps2").stat().st_size == size // 512 * 528

    assert lines["quiet"] == {"r": "1", "ticks": "0"}
finally:
    shutil.rmtree(work, ignore_errors=True)

print("PASS: MMCE backup draws through each card and reads the card through one handle per copy")
