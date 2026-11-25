"""Compile the actual MX4SIO card-detect step and check the unmount order.

sd_detect() holds the SIO2 lock while it checks the card. bdm_disconnect_bd() unmounts the
filesystem, which takes the filesystem lock; a file operation holds that lock while it waits for
the SIO2 lock. So the disconnect must come after the SIO2 unlock, or the two wait on each other
forever. No SIO2 or console acceptance.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "iop/mx4sio_bd/src/mx4sio.c"


def function(source, name):
    start = source.index("static void " + name + "()")
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define SPISD_RESULT_OK 0
#define INTR_NONE 2
#define M_PRINTF(...) do {} while (0)
#define M_DEBUG(...) do {} while (0)
struct block_device { int unused; };
static struct block_device bd;
static struct { int initialized; } sdcard;
static int locked, status, recover, disconnects, connects;
static void mx_sio2_lock(int mode) { (void)mode; assert(!locked); locked = 1; }
static void mx_sio2_unlock(int mode) { (void)mode; assert(locked); locked = 0; }
static int spisd_init_card(void) { assert(locked); return SPISD_RESULT_OK; }
static int spisd_get_card_info(void) { assert(locked); return SPISD_RESULT_OK; }
static uint16_t spisd_read_status_register(void) { assert(locked); return (uint16_t)status; }
static int spisd_recover(void) { assert(locked); return recover; }
static void bdm_connect_bd(struct block_device *b) { assert(b == &bd); connects++; }
static void bdm_disconnect_bd(struct block_device *b) {
    assert(b == &bd);
    assert(!locked); /* the unmount takes the filesystem lock: never under the SIO2 lock */
    disconnects++;
}
@FUNCTION@
int main(void) {
    /* card fine */
    sdcard.initialized = 1; status = 0; recover = 0;
    sd_detect();
    assert(!locked && disconnects == 0 && sdcard.initialized == 1);
    /* status bad, recovery works: stays connected */
    status = 1; recover = SPISD_RESULT_OK;
    sd_detect();
    assert(!locked && disconnects == 0 && sdcard.initialized == 1);
    /* status bad, recovery fails: one disconnect, after the unlock */
    recover = 1;
    sd_detect();
    assert(!locked && disconnects == 1 && sdcard.initialized == 0);
    /* not initialised: init path connects under the lock (bdm only queues the mount) */
    sd_detect();
    assert(!locked && connects == 1 && sdcard.initialized == 1 && disconnects == 1);
    puts("mx4sio detect unlock: ok");
    return 0;
}
"""


def main():
    source = SOURCE.read_text()
    program = HARNESS.replace("@FUNCTION@", function(source, "sd_detect"))
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "detect.c"
        exe = Path(tmp) / "detect"
        c.write_text(program)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Werror", "-o", str(exe), str(c)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    sys.exit(main())
