"""Run mmceman's real close/dclose/open/dopen on a host compiler with a mocked SIO2.

Defect fixed (sd2cloud audit 2026-10-28): a close or dclose that timed out or
got a bad reply kept its slot in the 16-entry handle pool, so after enough
failures every mmceN: open and dopen failed until reboot. Also, open/dopen
returned a bare -1 when the card itself said "no such file", so callers could
not tell absence from a bus failure. The fix is ported from RiptOPL's
modules/mmce/mmceman/src/mmce_fs.c (the owner's MIT-licensed driver).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "iop/mmceman/src/mmce_fs.c").read_text(encoding="utf-8")
workflow = (root / ".github/workflows/compile.yml").read_text(encoding="utf-8")
assert "python3 tests/test_mmce_handle_release.py" in workflow


def extract(signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


close_fn = extract("int mmce_fs_close(")
dclose_fn = extract("int mmce_fs_dclose(")
dopen_fn = extract("int mmce_fs_dopen(")
open_tail = source[source.index("int mmce_fs_open("):]
open_tail = open_tail[:open_tail.index("int mmce_fs_close(")]
assert "return rdbuf[0x1] == 0xff ? -ENOENT : -1;" in open_tail
assert "return -ENOENT;" in dopen_fn

harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef long long s64;
typedef struct { int unit; void *privdata; } iomanX_iop_file_t;
#define MMCE_ID 0x8B
#define MMCE_RESERVED 0xff
#define MMCE_REPLY_CONST 0xAA
#define MMCE_CMD_FS_CLOSE 0x41
#define MMCE_CMD_FS_DOPEN 0x48
#define MMCE_CMD_FS_DCLOSE 0x49
#define MMCE_FS_MAX_FD 16
#define DPRINTF(...) do {} while (0)
static int timeout_1s;
static int mmce_fs_fds[MMCE_FS_MAX_FD];
static int mmce_fs_active_fd[MMCE_FS_MAX_FD];
static s64 mmce_fs_active_pos[MMCE_FS_MAX_FD];
static s64 mmce_fs_active_size[MMCE_FS_MAX_FD];
/* Mock bus: mode 0 ok, 1 timeout, 2 bad reply const, 3 card error, 4 card "missing" */
static int mode, calls;
static void mmce_sio2_lock(void) {}
static void mmce_sio2_unlock(void) {}
static void mmce_fs_update_unit(int unit) { (void)unit; }
static int mmce_fs_wire_name_ok(const char *n) { return n && *n; }
static int mmce_fs_slot_index(iomanX_iop_file_t *file) {
    int *slot = (int *)file->privdata;
    if (slot < &mmce_fs_fds[0] || slot >= &mmce_fs_fds[MMCE_FS_MAX_FD]) return -1;
    return slot - &mmce_fs_fds[0];
}
static int *mmce_fs_find_free_handle(void) {
    int i;
    for (i = 0; i < MMCE_FS_MAX_FD; i++) if (mmce_fs_fds[i] < 0) return &mmce_fs_fds[i];
    return NULL;
}
static int mmce_sio2_tx_rx_pio(int tx, int rx, const void *w, u8 *r, int *t) {
    (void)tx; (void)w; (void)t;
    calls++;
    if (mode == 1) return -1;
    if (r != NULL && rx > 0) {
        memset(r, 0, (size_t)rx);
        if (rx > 1) r[1] = (mode == 2) ? 0x55 : MMCE_REPLY_CONST;
        if (rx == 3) r[1] = (mode == 4) ? 0xff : 3; /* fd packet */
        if (rx > 4) r[4] = (mode == 3) ? 1 : 0;
    }
    return 0;
}
''' + close_fn + dclose_fn + dopen_fn + r'''
static int take(iomanX_iop_file_t *f, int value) {
    f->privdata = mmce_fs_find_free_handle();
    assert(f->privdata != NULL);
    *(int *)f->privdata = value;
    return mmce_fs_slot_index(f);
}
int main(void) {
    iomanX_iop_file_t f;
    int m, slot, round;
    memset(mmce_fs_fds, -1, sizeof(mmce_fs_fds));
    for (m = 0; m <= 3; m++) {
        mode = m; f.unit = 0;
        slot = take(&f, 5);
        (void)mmce_fs_close(&f);
        assert(mmce_fs_fds[slot] == -1 && "close must free the slot on every path");
        assert(f.privdata != NULL);
        slot = take(&f, 6);
        (void)mmce_fs_dclose(&f);
        assert(mmce_fs_fds[slot] == -1 && "dclose must free the slot on every path");
    }
    /* Pool survives far more failed closes than it has slots. */
    mode = 1;
    for (round = 0; round < 100; round++) {
        take(&f, 7);
        assert(mmce_fs_close(&f) == -1);
    }
    mode = 3;
    for (round = 0; round < 100; round++) { take(&f, 7); (void)mmce_fs_dclose(&f); }
    for (m = 0; m < MMCE_FS_MAX_FD; m++) assert(mmce_fs_fds[m] == -1);
    /* Clean close returns 0; a card error is still reported. */
    mode = 0; take(&f, 5); assert(mmce_fs_close(&f) == 0);
    mode = 3; take(&f, 5); assert(mmce_fs_close(&f) == 1);
    mode = 0; take(&f, 5); assert(mmce_fs_dclose(&f) == 0 && f.privdata == NULL);
    /* dopen: absence is -ENOENT, a bus failure stays -1, success takes a slot. */
    mode = 4; f.privdata = NULL; assert(mmce_fs_dopen(&f, "/MemoryCards") == -ENOENT);
    assert(f.privdata == NULL);
    mode = 1; assert(mmce_fs_dopen(&f, "/MemoryCards") == -1);
    mode = 0; assert(mmce_fs_dopen(&f, "/MemoryCards") == 0);
    assert(*(int *)f.privdata == 3);
    for (m = 0; m < MMCE_FS_MAX_FD; m++) mmce_fs_fds[m] = -1;
    puts("PASS: mmceman close/dclose free the handle slot on every path; absence is -ENOENT");
    return 0;
}
'''

cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="mmce-handle-release-") as td:
    src = Path(td) / "h.c"
    exe = Path(td) / "h"
    src.write_text(harness, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Wno-unused-function",
                    "-Wno-unused-variable", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
