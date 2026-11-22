"""Exercise PSBBN partition creation against the production APA size parser."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/hdd_install_psbbn.c").read_text()
driver = (root / "iop/ps2hdd_osd/hdd_fio.c").read_text()


def function(text, signature):
    start = text.index(signature)
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', text[start:], re.S)
    depth = 0
    for token in tokens:
        depth += (token.group() == "{") - (token.group() == "}")
        if not depth and token.group() == "}":
            return text[start:start + token.end()] + "\n"
    raise AssertionError(signature)


code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hdd_install.h"
#define APA_PRINTF(...) ((void)0)
#define FIO_O_CREAT 1
#define FIO_O_EXCL 2
#define FIO_O_RDWR 4
#define FIO_MT_RDWR 1
#define FS_GROUP_APPLICATION 1
#define HDIOC_FLUSH 1
typedef struct { unsigned private_5, size, mode, attr, private_0; } iox_stat_t;
static const unsigned expected_mb[8] = {512, 128, 512, 512, 128, 256, 2048, 1024};
static const char *const expected_names[8] = {
    "__linux.1", "__linux.2", "__linux.4", "__linux.5",
    "__linux.6", "__linux.7", "__linux.9", "__linux.8"
};
static unsigned allocated, opened, closed, stats, contents, mounted, flushed, mkdirs, current_lba;
static unsigned starts[8];
'''
start = driver.index("#ifndef APA_8MB_PARTITION_SIZE")
end = driver.index("///////////////////////////////////////////////////////////////////////////////", start)
code += driver[start:end]
code += function(driver, "static int fioPartitionSizeLookUp(char *str)\n{")
code += r'''
static void dashWorkerStatus(const char *status, int done, int total) {
    assert(status && total == 9 && done == (int)opened);
}
int hddCheckAllocation(uint32_t sectors) {
    assert(allocated < 8 && sectors == expected_mb[allocated] * 2048u);
    allocated++;
    return 0;
}
static int fileXioOpen(const char *path, int flags, int mode) {
    char name[32], size[16], type[16];
    unsigned i = opened++;
    assert(i < 8 && flags == 7 && mode == 0);
    assert(sscanf(path, "hdd0:%31[^,],,,%15[^,],%15s", name, size, type) == 3);
    assert(!strcmp(name, expected_names[i]));
    int sectors = fioPartitionSizeLookUp(size);
    if (sectors < 0) {
        fprintf(stderr, "Driver rejected %s: %s\n", name, size);
        return sectors;
    }
    assert(sectors == (int)(expected_mb[i] * 2048u));
    assert(!strcmp(type, i == 1 ? "EXT2SWAP" : "EXT2"));
    current_lba = (current_lba + sectors - 1) & ~(sectors - 1);
    starts[i] = current_lba;
    current_lba += sectors;
    return 100 + i;
}
static int fileXioClose(int fd) {
    assert(fd == 99 + (int)opened);
    closed++;
    return 0;
}
static int fileXioGetStat(const char *path, iox_stat_t *out) {
    unsigned i = opened - 1;
    char expected[64];
    stats++;
    snprintf(expected, sizeof(expected), "hdd0:%s", expected_names[i]);
    assert(!strcmp(path, expected));
    memset(out, 0, sizeof(*out));
    out->private_5 = starts[i];
    out->size = expected_mb[i] * 2048u;
    out->mode = i == 1 ? 0x82 : 0x83;
    return 0;
}
static int hddMakeFilesystem(int mb, char *name, int group) {
    assert(opened == 8 && closed == 8 && stats == 8 && mb == 1024);
    assert(group == FS_GROUP_APPLICATION && !strcmp(name, "__contents"));
    contents++;
    return mb;
}
static int fileXioMount(const char *mount, const char *partition, int flags) {
    assert(contents == 1 && !mounted && !strcmp(mount, "pfs0:"));
    assert(!strcmp(partition, "hdd0:__common") && flags == FIO_MT_RDWR);
    mounted++;
    return 0;
}
static int genMkdir(const char *path, int mode) {
    assert(mounted && !strcmp(path, "pfs0:/Your Saves") && mode == 0777);
    mkdirs++;
    return 0;
}
static int fileXioUmount(const char *mount) {
    assert(mounted && !strcmp(mount, "pfs0:"));
    mounted--;
    return 0;
}
static int fileXioDevctl(const char *dev, int command, void *a, int n, void *b, int m) {
    assert(!strcmp(dev, "hdd0:") && command == HDIOC_FLUSH);
    assert(!a && !b && !n && !m && !mounted);
    flushed++;
    return 0;
}
'''
code += source[source.index("static const char *const linux_names"):source.index("static const char *const pfs_names")]
code += function(source, "int hddInstallPsbbnPartitions(")
code += r'''
static void reset(void) {
    allocated = opened = closed = stats = contents = mounted = flushed = mkdirs = 0;
    current_lba = 4194304;
}
int main(void) {
    HddInstall in = {.jail = 1};
    assert(fioPartitionSizeLookUp("2048M") == -EINVAL);
    assert(fioPartitionSizeLookUp("1024M") == -EINVAL);
    reset();
    assert(hddInstallPsbbnPartitions(&in) == 0);
    assert(allocated == 8 && opened == 8 && closed == 8 && stats == 8);
    assert(contents == 1 && !mounted && mkdirs == 1 && flushed == 1);
    for (unsigned i = 0; i < 8; i++) {
        assert(in.linux[i].start == starts[i]);
        assert(in.linux[i].sectors == expected_mb[i] * 2048u);
    }
    reset();
    in.jail = 0;
    assert(hddInstallPsbbnPartitions(&in) == -EINVAL && !allocated && !opened);
    puts("PASS: PSBBN partitions use stock APA size syntax and preserve extent layout");
}
'''
with tempfile.TemporaryDirectory(prefix="hdd-installer-partitions-") as directory:
    d = Path(directory)
    (d / "check.c").write_text(code)
    for mini in (False, True):
        subprocess.run([
            "gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
            *(["-DAPA_8MB_PARTITION_SIZE"] if mini else []), str(d / "check.c"), "-o", str(d / "check")
        ], check=True)
        subprocess.run([str(d / "check")], check=True, timeout=10)
