"""Run mmceman's real dread on a host compiler with a scripted SIO2 wire.

iomanX reads a dread result of 0 as end of directory and a negative one as a failure.
The driver returned -1 when the card reported the end of a listing (so every complete
MMCE listing ended as a read error) and returned the card's iterator descriptor for an
entry (0 when the card's descriptor 0 was free, which ended the listing before its first
entry). Same change as upstream ps2-mmce/mmceman pull request #11 (AKuHAK).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "iop/mmceman/src/mmce_fs.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mmce_dread_returns.py" in workflow


def extract(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned int u32;
typedef struct { u32 mode, attr, size; u8 ctime[8], atime[8], mtime[8]; u32 hisize, priv[6]; } iox_stat_t;
typedef struct { iox_stat_t stat; char name[256]; void *unknown; } iox_dirent_t;
typedef struct { int unit; void *privdata; } iomanX_iop_file_t;
#define MMCE_ID 0x8B
#define MMCE_RESERVED 0xff
#define MMCE_REPLY_CONST 0xAA
#define MMCE_CMD_FS_DREAD 0x4b
#define DPRINTF(...) do {} while (0)
static int timeout_1s, locked;
static void mmce_sio2_lock(void) { assert(!locked); locked = 1; }
static void mmce_sio2_unlock(void) { assert(locked); locked = 0; }
static void mmce_fs_update_unit(int unit) { (void)unit; }
/* Each transfer takes the next scripted reply. */
enum { TIMEOUT, ACK, STATUS, LAST };
static int kinds[8], values[8], step, steps;
static void wire(int n, const int *k, const int *v) {
    for (steps = 0, step = 0; steps < n; steps++) { kinds[steps] = k[steps]; values[steps] = v[steps]; }
}
static int mmce_sio2_tx_rx_pio(int tx, int rx, const void *w, void *out, int *t) {
    u8 *r = out;
    (void)tx; (void)w; (void)t;
    assert(step < steps);
    int kind = kinds[step], value = values[step];
    step++;
    if (kind == TIMEOUT) return -1;
    if (r != NULL && rx > 0) memset(r, 0, (size_t)rx);
    if (kind == ACK && rx > 1) r[1] = MMCE_REPLY_CONST;
    if (kind == STATUS) { r[1] = MMCE_REPLY_CONST; r[4] = (u8)value; }
    if (kind == LAST) r[1] = (u8)value; /* the card's iterator descriptor */
    return 0;
}
''' + extract("int mmce_fs_dread(") + r'''
int main(void) {
    int fd = 3;
    iomanX_iop_file_t f = { 0, &fd };
    iox_dirent_t d;
    static const int entry[] = { STATUS, ACK, ACK, LAST }, zero[] = { 0, 0, 0, 0 }, five[] = { 0, 0, 0, 5 };
    static const int end[] = { STATUS }, one[] = { 1 }, lost[] = { TIMEOUT };
    wire(4, entry, zero);
    assert(mmce_fs_dread(&f, &d) == 1 && !locked && "an entry whose iterator fd is 0 is still an entry");
    wire(4, entry, five);
    assert(mmce_fs_dread(&f, &d) == 1 && !locked && "an entry is 1 whatever the iterator fd");
    wire(1, end, one);
    assert(mmce_fs_dread(&f, &d) == 0 && !locked && "end of listing is 0, not an error");
    wire(1, lost, zero);
    assert(mmce_fs_dread(&f, &d) == -1 && !locked && "a bus timeout stays -1");
    puts("PASS: mmceman dread returns 1 per entry, 0 at the end of a listing, -1 on a bus failure");
    return 0;
}
'''

cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="mmce-dread-") as td:
    src = Path(td) / "h.c"
    exe = Path(td) / "h"
    src.write_text(harness, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Wno-unused-function",
                    "-Wno-unused-variable", "-Wno-pointer-sign", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
