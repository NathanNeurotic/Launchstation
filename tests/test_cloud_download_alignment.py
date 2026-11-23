"""Exercise the production aligned HTTPS sink with fragmented and short I/O."""

from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/cloud_http.c").read_text()
sink = source[source.index("typedef struct {\n    unsigned char *bytes;"):
              source.index("static void download_progress(")]
code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLOUD_HTTP_IO 16384
#define HDD_INSTALL_ATA_TIMEOUT (-502)
static void hddInstallRecoveryObserve(int result) { (void)result; assert(0); }
#define PAYLOAD_SIZE (CLOUD_HTTP_IO * 3 + 509)
static _Alignas(64) unsigned char scratch[CLOUD_HTTP_IO];
static unsigned char payload[PAYLOAD_SIZE + 3], stored[PAYLOAD_SIZE];
static int position, writes, fail_after, short_enabled;

static int genWrite(int fd, void *data, int size)
{
    int offset = position & 511;
    int count = size;
    assert(fd == 19 && !((uintptr_t)data & 63));
    assert(size > 0 && (!offset || size <= 512 - offset));
    if (writes++ == fail_after)
        return -5;
    if (short_enabled && writes % 3 == 1 && size > 3)
        count = 3;
    assert(position + count <= PAYLOAD_SIZE);
    memcpy(stored + position, data, count);
    position += count;
    return count;
}
""" + sink + r"""
static void reset(int short_writes)
{
    memset(stored, 0, sizeof(stored));
    position = writes = 0;
    short_enabled = short_writes;
    fail_after = -1;
}

static void run(int fragment, int short_writes)
{
    CloudAlignedOutput output = {.bytes = scratch};
    int sent = 0;
    reset(short_writes);
    while (sent < PAYLOAD_SIZE) {
        int count = fragment;
        if (count > PAYLOAD_SIZE - sent)
            count = PAYLOAD_SIZE - sent;
        assert(append_aligned_output(&output, 19, payload + sent + 3, count) == 0);
        sent += count;
        assert(output.persisted == position && output.persisted + output.pending == sent);
        assert(position % CLOUD_HTTP_IO == 0);
    }
    assert(flush_aligned_output(&output, 19) == 0);
    assert(output.pending == 0 && output.persisted == PAYLOAD_SIZE);
    assert(!memcmp(payload + 3, stored, PAYLOAD_SIZE));
}

int main(void)
{
    for (int i = 0; i < PAYLOAD_SIZE; i++)
        payload[i + 3] = (i ^ (i >> 9) ^ (i >> 14)) & 255;
    run(1, 0);
    run(3581, 0);
    run(16387, 1);
    run(443, 1);

    CloudAlignedOutput output = {.bytes = scratch};
    reset(1);
    fail_after = 1;
    assert(append_aligned_output(&output, 19, payload + 3, CLOUD_HTTP_IO) < 0);
    assert(output.persisted == 3 && position == 3);
    reset(0);
    output = (CloudAlignedOutput){.bytes = scratch};
    fail_after = 0;
    assert(append_aligned_output(&output, 19, payload + 3, 31) == 0);
    assert(flush_aligned_output(&output, 19) < 0 && output.persisted == 0);
    puts("PASS: ATA-aligned HTTPS fragments, sector boundaries, tails, short writes and failures");
}
"""

with tempfile.TemporaryDirectory(prefix="cloud-download-alignment-") as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    src.write_text(code)
    compiler = shutil.which("gcc") or shutil.which("clang")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
