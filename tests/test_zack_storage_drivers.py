"""Behavioral host checks for the safe #172-#175 Launchstation fixes.

Extracts the actual production functions and runs them under narrow C mocks.
The tests do not claim that synchronous IOP calls animate while blocked or
that MMCE/SIO2 timing changes are safe on physical hardware.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    begin = source.index(signature)
    pos = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[begin:pos] + "\n"


c = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DI_USB 1
#define DI_NETWORK 2
#define DI_SDCARD 3
#define DI_WARNING 4

typedef struct {
    int icon;
    const char *mount;
    const char *name;
    const char *status;
} DashDevice;

static int modal_calls, start_calls, frame_calls, probe_calls, end_calls;
static int worker_allowed = 1;
static char last_body[256];

static int isPcCardCfPath(const char *mount) {
    return !strncmp(mount, "cf", 2);
}
static void dashModalMessage(int icon, const char *title,
                             const char *body, const char *detail) {
    assert(icon == DI_WARNING);
    assert(!strcmp(title, "Device not found"));
    assert(detail == NULL); /* never expose internal mount paths */
    assert(body && body[0]);
    snprintf(last_body, sizeof(last_body), "%s", body);
    modal_calls++;
}
static int dashWorkerAwaitAll(const char *status) {
    assert(!strcmp(status, "Finishing up"));
    return worker_allowed;
}
static void dashLoadingBegin(const char *title) {
    assert(title && title[0]);
    start_calls++;
}
static void dashLoadingFrame(const char *status, int done, int total) {
    assert(!strcmp(status, "Starting device drivers..."));
    assert(done == 0 && total == 0);
    assert(start_calls == frame_calls + 1);
    frame_calls++;
}
static int probeMount(const char *mount) {
    assert(frame_calls == probe_calls + 1); /* frame presented before RPC */
    probe_calls++;
    return !strcmp(mount, "ready");
}
static void dashLoadingEnd(void) {
    assert(probe_calls == end_calls + 1);
    end_calls++;
}
"""

c += function("src/dash_files.c", "static void deviceNotFound(const DashDevice *d)\n{")
c += function("src/dash_devices.c", "static int startSync(const char *mount, const char *title)\n{")

c += r"""
#define MMCE 1
#define MX4SIO 1
#define TRUE 1
#define STORAGE_STACK_DEFAULT 0
#define STORAGE_STACK_MMCE 1
#define STORAGE_STACK_MX4SIO 2
#define DPRINTF(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define NETWORK_STACK_NONE 0
#define NETWORK_STACK_ETH 1
#define NETWORK_STACK_UDPFS 2
static int network_stack_mode;
static int storage_driver_stack_mode, reset_allowed, resets;
static void resetRuntimeDeviceState(int status) {
    assert(status == TRUE);
    resets++;
    if (reset_allowed) { storage_driver_stack_mode = STORAGE_STACK_DEFAULT; network_stack_mode = NETWORK_STACK_NONE; }
}
static const char *mmce_product_ids[] = {"Unknown", "SD2PSX", "MemCard PRO2", "PicoMemcard+", "PicoMemcardZero"};
static int ping_calls, ping_success_at, reset_commands, selected_port;
static void mmce_sio2_set_port(int port) { selected_port = port; }
static int mmce_cmd_ping(void) {
    return ++ping_calls == ping_success_at ? 0x100 : -1;
}
static int mmce_cmd_reset(void) { reset_commands++; return 0; }
"""
c += function("src/init.c", "static int switchStorageDriverStack(int target_mode)\n{")
c += function("src/init.c", "static int switchNetworkStack(int target_mode)\n{")
mmce = (root / "iop/mmceman/src/mmceman.c").read_text()
import re
attempts = re.search(r"^#define MMCE_PING_ATTEMPTS (\d+)$", mmce, re.M).group(1)
c += "#define MMCE_PING_ATTEMPTS " + attempts + "\n"
c += function("iop/mmceman/src/mmceman.c", "static void mmce_init(int port)\n{")

c += r"""
int main(void) {
    DashDevice mmce = {DI_SDCARD, "mmce0:", "MMCE Slot 1", "No MMCE storage found"};
    DashDevice ilink = {DI_SDCARD, "ilink:", "iLink / FireWire", "No iLink storage found"};
    DashDevice usb = {DI_USB, "usb0:", "MASS", "Not connected"};
    DashDevice udpfs = {DI_NETWORK, "udpfs:", "UDPFS", "Not connected"};
    DashDevice cf = {DI_SDCARD, "cf0:", "PC CARD CF", "CF card absent"};
    deviceNotFound(&mmce);
    assert(strstr(last_body, "MMCE Slot 1"));
    deviceNotFound(&ilink);
    assert(strstr(last_body, "iLink / FireWire"));
    deviceNotFound(&usb);
    assert(strstr(last_body, "USB storage device"));
    deviceNotFound(&udpfs);
    assert(strstr(last_body, "network share"));
    deviceNotFound(&cf);
    assert(strstr(last_body, "CF card absent"));
    assert(modal_calls == 5);

    assert(startSync("absent", "Checking absent device") == 0);
    assert(startSync("ready", "Checking present device") == 1);
    assert(start_calls == 2 && frame_calls == 2);
    assert(probe_calls == 2 && end_calls == 2);
    worker_allowed = 0;
    assert(startSync("ready", "Blocked by pending I/O") == -1);
    assert(start_calls == 2 && frame_calls == 2);
    assert(probe_calls == 2 && end_calls == 2);
    storage_driver_stack_mode = STORAGE_STACK_DEFAULT;
    assert(switchStorageDriverStack(STORAGE_STACK_MMCE) == 1 && resets == 0);
    storage_driver_stack_mode = STORAGE_STACK_MMCE;
    assert(switchStorageDriverStack(STORAGE_STACK_MMCE) == 1 && resets == 0);
    reset_allowed = 0;
    assert(switchStorageDriverStack(STORAGE_STACK_MX4SIO) == 0);
    assert(storage_driver_stack_mode == STORAGE_STACK_MMCE && resets == 1);
    reset_allowed = 1;
    assert(switchStorageDriverStack(STORAGE_STACK_MX4SIO) == 1);
    assert(storage_driver_stack_mode == STORAGE_STACK_DEFAULT && resets == 2);
    network_stack_mode = NETWORK_STACK_NONE;
    int before = resets;
    assert(switchNetworkStack(NETWORK_STACK_ETH) == 1 && resets == before);
    assert(switchNetworkStack(NETWORK_STACK_ETH) == 1 && resets == before);
    reset_allowed = 0;
    assert(switchNetworkStack(NETWORK_STACK_UDPFS) == 0);
    assert(network_stack_mode == NETWORK_STACK_ETH && resets == before + 1);
    reset_allowed = 1;
    assert(switchNetworkStack(NETWORK_STACK_UDPFS) == 1);
    assert(network_stack_mode == NETWORK_STACK_UDPFS && resets == before + 2);
    for (int port = 2; port <= 3; port++) {
        ping_calls = reset_commands = ping_success_at = 0;
        mmce_init(port);
        assert(selected_port == port && ping_calls == 2 && reset_commands == 0);
        for (int success = 1; success <= 2; success++) {
            ping_calls = reset_commands = 0;
            ping_success_at = success;
            mmce_init(port);
            assert(ping_calls == success && reset_commands == 1);
        }
    }
    puts("PASS: startup/modal, refused stack transitions and MMCE retry outcomes");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"

with tempfile.TemporaryDirectory(prefix="zack-storage-drivers-") as tmp:
    path = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    path.write_text(c, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
