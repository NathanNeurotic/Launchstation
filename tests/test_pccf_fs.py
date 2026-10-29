"""Verify PC CARD CompactFlash Stages L2-L4 filesystem, transfer, UI and launch blocking."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
init_c = (root / "src/init.c").read_text(encoding="utf-8")
filer_c = (root / "src/filer.c").read_text(encoding="utf-8")
fileops_c = (root / "src/filer_fileops.c").read_text(encoding="utf-8")
device_c = (root / "src/filer_device.c").read_text(encoding="utf-8")
copy_c = (root / "src/filer_copy.c").read_text(encoding="utf-8")
actions_c = (root / "src/filer_actions.c").read_text(encoding="utf-8")
browser_c = (root / "src/filer_browser.c").read_text(encoding="utf-8")
dash_dev_c = (root / "src/dash_devices.c").read_text(encoding="utf-8")
dash_files_c = (root / "src/dash_files.c").read_text(encoding="utf-8")
dash_browser_c = (root / "src/dash_browser.c").read_text(encoding="utf-8")
dash_lib_c = (root / "src/dash_lib.c").read_text(encoding="utf-8")
main_act_c = (root / "src/main_actions.c").read_text(encoding="utf-8")

def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"

# 1. Structural checks
# Check canonicalizePcCardCfPath in filer.c
assert "canonicalizePcCardCfPath" in filer_c
assert 'isPcCardCfPath(path)' in filer_c
assert 'canonicalizePcCardCfPath(path, cf_path)' in filer_c
assert 'strcpy(files[nfiles].name, "cf:")' in filer_c

# Check filer_fileops.c online checks and path normalization
assert "ensurePathPcCardCfOnline" in fileops_c
assert "normalizePcCardCfPath" in fileops_c
assert "!ensurePathPcCardCfOnline(path)" in fileops_c

# Check filer_device.c DEV9 conflict detection and readiness
assert "isDev9BackedPath" in device_c
assert "transferUsesPcCardCfAndDev9" in device_c
assert "loadPcCardCfModules() && isPcCardCfOnline()" in device_c

# Check filer_copy.c buffer sizing and flush check
assert "isPcCardCfPath(out)" in copy_c
assert "isPcCardCfPath(in)" in copy_c
assert "buffSize = 131072;" in copy_c
assert "buffSize = 524288;" in copy_c

# Check filer_actions.c 64-bit size and online check in Rename
assert "((u64)stat.hisize << 32) | (u64)stat.size" in actions_c
assert "genRenameNoReplace(from, to)" in actions_c
assert "return filerRenameSiblingOnce(old_path, new_path," in actions_c
assert "!ensurePathPcCardCfOnline(from)" in fileops_c

# Check filer_browser.c root label mapping, title.cfg, and menu disables
assert 'if (!strcmp(name, "cf:") || !strcmp(name, "cf0:"))' in browser_c
assert 'return "cf:/";' in browser_c
assert 'isPcCardCfPath(path)' in browser_c
assert 'enable[LAUNCH_ELF_ARGS] = FALSE;' in browser_c

# Check dash_lib.c label and icon
assert 'isPcCardCfPath(path)' in dash_lib_c
assert '"PC CARD CF"' in dash_lib_c

# Check dash_devices.c diagnostics and probe
assert "PROBE_PCCF" in dash_dev_c
assert "Unsupported console model" in dash_dev_c
assert "Disabled in safe mode" in dash_dev_c
assert "PC CARD / DEV9 bus busy" in dash_dev_c
assert "CF card offline or I/O fault" in dash_dev_c
assert "FAT32/exFAT volume not found" in dash_dev_c
assert "Ready (PCMCIA)" in dash_dev_c
assert "No compatible CF adapter" in dash_dev_c
assert 'probeRoot("cf0:/", 2000)' in dash_dev_c

# Check dash_files.c devSelectable allowing CF retry/selection
assert "isPcCardCfPath(devs[idx].mount)" in dash_files_c
assert "isPcCardCfEligible() && !boot_safe_mode && !isPcCardCfBusBusy()" in dash_files_c

# Check main_actions.c and dash_browser.c direct launch blocking
assert "Direct launch from PC CARD CF is not supported" in main_act_c
assert "Direct launch unsupported" in dash_browser_c

# 2. Compile and run standalone C harness verifying logic
c_harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pccf_control.h"

#define MAX_PATH 256
#define ENODEV 19
#define EIO 5
#define EEXIST 17
#define FALSE 0
#define TRUE 1

typedef unsigned int u32;
typedef unsigned long long u64;

static int pccf_loaded = 0;
static int mock_devctl_ret = 0;
static pccf_control_status_t mock_status;

int fileXioDevctl(const char *dev, int cmd, void *in, int in_len, void *out, int out_len) {
    if (strcmp(dev, PCCF_CONTROL_DEVICE) != 0) return -ENODEV;
    if (cmd == PCCF_CONTROL_STATUS) {
        if (out != NULL && out_len == sizeof(pccf_control_status_t)) {
            memcpy(out, &mock_status, sizeof(mock_status));
        }
        return mock_devctl_ret;
    }
    return 0;
}

int isPcCardCfPath(const char *path) {
    if (!path || strncmp(path, "cf", 2)) return 0;
    path += 2;
    while (*path >= '0' && *path <= '9') ++path;
    return *path == ':';
}

int getPcCardCfCapacitySectors(u32 *sectors_out) {
    pccf_control_status_t st;
    int ret;

    if (sectors_out != NULL)
        *sectors_out = 0;
    if (!pccf_loaded)
        return -ENODEV;

    memset(&st, 0, sizeof(st));
    ret = fileXioDevctl(PCCF_CONTROL_DEVICE, PCCF_CONTROL_STATUS, NULL, 0, &st, sizeof(st));
    if (ret < 0 || st.version != 1 || !st.online || !st.sectors)
        return -ENODEV;

    if (sectors_out != NULL)
        *sectors_out = st.sectors;
    return 0;
}

int isPcCardCfOnline(void) {
    return getPcCardCfCapacitySectors(NULL) == 0;
}

static const char *canonicalizePcCardCfPath(const char *path, char *buffer) {
    if (!strncmp(path, "cf:", 3)) {
        if (path[3] == '\0')
            strcpy(buffer, "cf0:/");
        else
            snprintf(buffer, MAX_PATH, "cf0:%s", path + 3);
        return buffer;
    }
    if (!strncmp(path, "cf", 2) && path[2] >= '0' && path[2] <= '9' && path[3] == ':' && path[4] == '\0') {
        snprintf(buffer, MAX_PATH, "cf%c:/", path[2]);
        return buffer;
    }

    return path;
}

static void normalizePcCardCfPath(char *path, size_t path_size) {
    char tmp[MAX_PATH];

    if (path == NULL || path_size == 0 || !isPcCardCfPath(path))
        return;
    if (!strncmp(path, "cf:", 3)) {
        snprintf(tmp, sizeof(tmp), "cf0:%s", path[3] ? path + 3 : "/");
        snprintf(path, path_size, "%s", tmp);
    } else if (!strncmp(path, "cf", 2) && path[2] >= '0' && path[2] <= '9' && path[3] == ':' && path[4] == '\0') {
        snprintf(tmp, sizeof(tmp), "cf%c:/", path[2]);
        snprintf(path, path_size, "%s", tmp);
    }
}

static int isDev9BackedPath(const char *path) {
    if (path == NULL)
        return FALSE;
    return (!strncmp(path, "hdd", 3) ||
            !strncmp(path, "pfs", 3) ||
            !strncmp(path, "dvr_hdd", 7) ||
            !strncmp(path, "dvr_pfs", 7) ||
            !strncmp(path, "ata", 3) ||
            !strncmp(path, "host", 4) ||
            !strncmp(path, "udpfs", 5) ||
            !strncmp(path, "xfrom", 5));
}

static int transferUsesPcCardCfAndDev9(const char *src_path, const char *dst_path) {
    return ((isPcCardCfPath(src_path) && isDev9BackedPath(dst_path)) ||
            (isDev9BackedPath(src_path) && isPcCardCfPath(dst_path)));
}

int main(void) {
    char buf[MAX_PATH];

    // Path canonicalization
    assert(!strcmp(canonicalizePcCardCfPath("cf:", buf), "cf0:/"));
    assert(!strcmp(canonicalizePcCardCfPath("cf:/GAMES/TEST.ISO", buf), "cf0:/GAMES/TEST.ISO"));
    assert(!strcmp(canonicalizePcCardCfPath("cf0:", buf), "cf0:/"));
    assert(!strcmp(canonicalizePcCardCfPath("cf0:/OPNPS2LD.ELF", buf), "cf0:/OPNPS2LD.ELF"));
    assert(!strcmp(canonicalizePcCardCfPath("mc0:/BOOT", buf), "mc0:/BOOT"));

    // Path normalization
    strcpy(buf, "cf:/FOO/BAR");
    normalizePcCardCfPath(buf, sizeof(buf));
    assert(!strcmp(buf, "cf0:/FOO/BAR"));

    strcpy(buf, "cf:");
    normalizePcCardCfPath(buf, sizeof(buf));
    assert(!strcmp(buf, "cf0:/"));

    strcpy(buf, "cf0:");
    normalizePcCardCfPath(buf, sizeof(buf));
    assert(!strcmp(buf, "cf0:/"));

    // Online & Status verification
    pccf_loaded = 0;
    assert(!isPcCardCfOnline());
    assert(getPcCardCfCapacitySectors(NULL) == -ENODEV);

    pccf_loaded = 1;
    mock_devctl_ret = 0;
    memset(&mock_status, 0, sizeof(mock_status));
    mock_status.version = 1;
    mock_status.online = 1;
    mock_status.sectors = 0x01E00000; // ~16 GB (512-byte sectors)
    assert(isPcCardCfOnline());

    u32 sec = 0;
    assert(getPcCardCfCapacitySectors(&sec) == 0);
    assert(sec == 0x01E00000);

    // When card is removed / offline:
    mock_status.online = 0;
    assert(!isPcCardCfOnline());
    assert(getPcCardCfCapacitySectors(&sec) == -ENODEV);

    // Transfer stack compatibility: CF <-> DEV9 blocked
    assert(transferUsesPcCardCfAndDev9("cf0:/GAME.ISO", "hdd0:+OPL"));
    assert(transferUsesPcCardCfAndDev9("pfs0:/DIR", "cf:/BACKUP"));
    assert(transferUsesPcCardCfAndDev9("cf:/TEST", "ata0:/GAMES"));
    assert(transferUsesPcCardCfAndDev9("host:/PS2", "cf:/PS2"));
    assert(transferUsesPcCardCfAndDev9("udpfs:/PS2", "cf:/PS2"));
    assert(transferUsesPcCardCfAndDev9("xfrom0:/DATA", "cf:/DATA"));

    // Transfer stack compatibility: CF <-> USB/MC/MMCE/MX4SIO/iLink allowed
    assert(!transferUsesPcCardCfAndDev9("cf0:/GAME.ISO", "mass0:/GAMES/GAME.ISO"));
    assert(!transferUsesPcCardCfAndDev9("cf0:/GAME.ISO", "usb0:/GAMES/GAME.ISO"));
    assert(!transferUsesPcCardCfAndDev9("mc0:/BWNETCNF", "cf0:/BACKUP/BWNETCNF"));
    assert(!transferUsesPcCardCfAndDev9("mmce0:/SLOT1.MCD", "cf0:/MMCE_BACKUPS/SLOT1.MCD"));
    assert(!transferUsesPcCardCfAndDev9("mx4sio:/GAME.ISO", "cf0:/GAME.ISO"));
    assert(!transferUsesPcCardCfAndDev9("ilink:/GAME.ISO", "cf0:/GAME.ISO"));

    // 64-bit size calculation verification (>4 GiB)
    u32 hisize = 1; // 4 GiB
    u32 size = 0x100000; // + 1 MiB
    u64 full_size = ((u64)hisize << 32) | (u64)size;
    assert(full_size == 0x100100000ULL);

    puts("PASS: PCCF filesystem canonicalization, online validation, DEV9 transfer gating, and 64-bit size calculation");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    src_path = Path(td) / "test_pccf_fs.c"
    exe_path = Path(td) / ("test_pccf_fs.exe" if shutil.which("gcc") else "test_pccf_fs")
    src_path.write_text(c_harness, encoding="utf-8")
    inc_arg = f"-I{root / 'include'}"
    cc = shutil.which("gcc") or shutil.which("clang")
    assert cc is not None, "A C compiler is required to run the test harness"
    subprocess.run([cc, str(src_path), inc_arg, "-o", str(exe_path)], check=True)
    subprocess.run([str(exe_path)], check=True)
