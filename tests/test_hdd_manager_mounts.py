"""Exercise HDD Manager usage probes against a four-handle HDD model."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_hdd.c").read_text()
header = (root / "include/launchelf.h").read_text()


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdd_filesystem.h"
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u32 lba, size; unsigned char data[]; } hddAtaTransfer_t;
typedef int GameInfo;
typedef int HddHeaderSourcePartition;
#define MAX_PART_NAME 32
#define MAX_NAME 64
#define MAX_PATH 1024
#define MAX_PARTITIONS 64
#define FIO_MT_RDWR 0
#define APA_TYPE_FREE 0
#define APA_TYPE_MBR 1
#define APA_TYPE_PFS 0x100
#define HDIOC_STATUS 10
#define HDIOC_TOTALSECTOR 11
#define PDIOC_ZONEFREE 12
#define PDIOC_ZONESZ 13
#define HDIOC_READSECTOR 14
'''
for macro, text in (("MOUNT_LIMIT", header), ("SECTORS_PER_MB", source), ("HDD_MB", source)):
    code += re.search(r"^#define " + macro + r" .+$", text, re.M).group() + "\n"
start = source.index("enum {")
end = source.index("} HddJob;") + len("} HddJob;")
code += source[start:end] + "\n"
code += r'''
static char mountedParty[MOUNT_LIMIT][MAX_NAME];
static char driver_mounts[MOUNT_LIMIT][MAX_NAME];
static int latestMount = -1;
static int Party_vmcIndex[MOUNT_LIMIT];
static u32 pfs_mount_used[MOUNT_LIMIT], pfs_mount_clock;
static int mounts, unmounts, attempts[MOUNT_LIMIT], mount_error, zone_error, zone_size;
static unsigned fail_unmount;
static int live_scans, scan_position, busy, submissions;
static u64 now;
static int driver_status, read_error, format_reads;
static u32 driver_sectors;
static unsigned char format_boot[512], format_mbr[512];
static u32 format_volume_lba;
static HddJob *pfs_job, *queued;
static void (*queued_run)(void *), (*queued_done)(void *);
static void dashStrCopy(char *out, const char *in, int size) { snprintf(out, size, "%s", in); }
static int mountedCount(void) {
    int n = 0;
    for (int i = 0; i < MOUNT_LIMIT; i++) n += driver_mounts[i][0] != 0;
    return n;
}
static int slotFor(const char *dev) {
    for (int i = 0; i < MOUNT_LIMIT; i++) if (!strcmp(mountedParty[i], dev)) return i;
    return -1;
}
static int fileXioMount(const char *pfs, const char *dev, int mode) {
    int i = pfs[3] - '0';
    assert(mode == FIO_MT_RDWR && i >= 0 && i < MOUNT_LIMIT);
    mounts++;
    if (mount_error) return mount_error;
    if (driver_mounts[i][0]) return -EBUSY;
    for (int k = 0; k < MOUNT_LIMIT; k++) if (!strcmp(driver_mounts[k], dev)) return -EBUSY;
    if (mountedCount() + live_scans >= 4) return -EMFILE;
    dashStrCopy(driver_mounts[i], dev, sizeof(driver_mounts[i]));
    return 0;
}
static void unmountParty(int i) {
    assert(i >= 0 && i < MOUNT_LIMIT && Party_vmcIndex[i] < 0);
    attempts[i]++;
    if (fail_unmount & (1u << i)) return;
    assert(driver_mounts[i][0]);
    driver_mounts[i][0] = mountedParty[i][0] = 0;
    if (latestMount == i) latestMount = -1;
    unmounts++;
}
typedef struct { unsigned mode; u32 size; int private_0; } Stat;
typedef struct { char name[MAX_NAME]; Stat stat; } iox_dirent_t;
static int hddCheckPresent(void) { return driver_status < 3 ? 0 : -1; }
static int hddCheckFormatted(void) { return driver_status == 0 ? 0 : -1; }
static int fileXioDopen(const char *dev) {
    assert(!strcmp(dev, "hdd0:"));
    if (mountedCount() + live_scans >= 4) return -EMFILE;
    live_scans++; scan_position = 0; return 100;
}
static int fileXioDread(int fd, iox_dirent_t *de) {
    const char *names[] = {"+A", "+B", "+A", "+C"};
    const int sizes[] = {128, 256, 256, 512};
    assert(fd == 100 && live_scans == 1);
    if (scan_position == 4) return 0;
    memset(de, 0, sizeof(*de));
    dashStrCopy(de->name, names[scan_position], sizeof(de->name));
    de->stat.mode = APA_TYPE_PFS;
    de->stat.size = sizes[scan_position] * SECTORS_PER_MB;
    de->stat.private_0 = scan_position == 0;
    scan_position++; return 1;
}
static int fileXioDclose(int fd) { assert(fd == 100 && live_scans == 1); live_scans--; return 0; }
static int fileXioDevctl(const char *dev, int cmd, void *arg, int alen, void *buf, int blen) {
    if (cmd == HDIOC_READSECTOR) {
        hddAtaTransfer_t *request = arg;
        assert(!strcmp(dev, "hdd0:") && request && alen == sizeof(*request));
        assert(request->size == 1 && request->lba < driver_sectors && buf && blen == 512);
        assert((uintptr_t)request % 64 == 0 && (uintptr_t)buf % 64 == 0);
        format_reads++;
        if (read_error) return read_error;
        if (!format_volume_lba || request->lba == format_volume_lba)
            memcpy(buf, format_boot, 512);
        else if (request->lba == 0)
            memcpy(buf, format_mbr, 512);
        else
            memset(buf, 0, 512);
        return 0;
    }
    assert(!arg && !alen && !buf && !blen);
    if (cmd == HDIOC_STATUS) return driver_status;
    if (cmd == HDIOC_TOTALSECTOR) return (int)driver_sectors;
    int i = dev[3] - '0';
    assert(!strncmp(dev, "pfs", 3) && driver_mounts[i][0]);
    if (cmd == PDIOC_ZONEFREE) return zone_error ? zone_error : 262144;
    assert(cmd == PDIOC_ZONESZ); return zone_size;
}
static void dashWorkerStatus(const char *s, int done, int total) { (void)s; (void)done; (void)total; }
static int dashWorkerBusy(void) { return busy; }
static u64 Timer(void) { return now; }
static int dashWorkerSubmit(const char *label, int flags, void (*run)(void *), void (*done)(void *), void *arg) {
    assert(!strcmp(label, "Reading partition") && !flags && !busy && !queued);
    queued = arg; queued_run = run; queued_done = done;
    submissions++; busy = 1; return submissions;
}
'''
for signature in ("static int pfsMakeRoom(", "static int pfsMountForInfo(",
                  "static int hddReadFormatSector(", "static void scanRun(", "static void pfsRun(",
                  "static HddJob *jobNew(", "static void hddJobFree(",
                  "static int findPart("):
    code += function(signature)
code += 'static void hddJobRun(void *arg) { HddJob *j = arg; assert(j->op == HOP_PFS); pfsRun(j); }\n'
code += function("static void pfsDone(") + function("static void loadPfsInfo(")
code += r'''
#define DI_HDD 1
static char status_body[320];
static const char *hddStatusInfoText(int status) { return status == 0 ? "APA formatted" : "Drive status"; }
static const char *hddStatusShortText(int status) { return status == 0 ? "READY" : "NO APA"; }
static void dashModalMessage(int icon, const char *title, const char *body, const char *detail) {
    assert(icon == DI_HDD && !strcmp(title, "HDD status") && detail);
    dashStrCopy(status_body, body, sizeof(status_body));
}
'''
code += function("static void actStatus(")
code += r'''
static void put32(unsigned offset, u32 value) {
    for (unsigned i = 0; i < 4; i++) format_boot[offset + i] = value >> (i * 8);
}
static void formatBoot(unsigned format) {
    memset(format_boot, 0, sizeof(format_boot));
    format_boot[510] = 0x55; format_boot[511] = 0xaa;
    format_boot[0] = 0xeb; format_boot[2] = 0x90;
    if (format == HDD_FS_EXFAT) {
        format_boot[1] = 0x76; memcpy(format_boot + 3, "EXFAT   ", 8);
        put32(72, 1000000); put32(80, 24); put32(84, 8192);
        put32(88, 8216); put32(92, 100000); put32(96, 2);
        format_boot[105] = 1; format_boot[108] = 9; format_boot[110] = 1;
    } else {
        format_boot[1] = 0x58; format_boot[12] = 2; format_boot[13] = 8;
        format_boot[14] = 32; format_boot[16] = 2;
        put32(32, 1000000); put32(36, 1024); put32(44, 2);
    }
}
static void mbrEntry(unsigned slot, unsigned kind, u32 start, u32 length) {
    unsigned offset = 446 + slot * 16;
    format_mbr[offset + 4] = kind;
    for (unsigned i = 0; i < 4; i++) {
        format_mbr[offset + 8 + i] = start >> (8 * i);
        format_mbr[offset + 12 + i] = length >> (8 * i);
    }
}
static void jailBoot(unsigned format) {
    formatBoot(format); format_volume_lba = 329728;
    format_mbr[510] = 0x55; format_mbr[511] = 0xaa;
    memcpy(format_mbr + 4, "APA", 4);
    memcpy(format_mbr + 224, "APAJ-A2", 8);
    mbrEntry(0, 0x17, 2048, 262144);
    mbrEntry(1, 0x17, 264192, 65536);
    mbrEntry(2, 0x07, format_volume_lba, 1000000);
}
static void reset(void) {
    assert(!pfs_job && !queued && !live_scans);
    free(hm.parts); memset(&hm, 0, sizeof(hm));
    hm.parts = calloc(32, sizeof(HddPart)); assert(hm.parts); hm.n = 32;
    for (int i = 0; i < hm.n; i++) {
        snprintf(hm.parts[i].name, sizeof(hm.parts[i].name), "+TEST%d", i);
        hm.parts[i].type = HP_PFS; hm.parts[i].raw_mb = 4096 + i;
    }
    memset(mountedParty, 0, sizeof(mountedParty));
    memset(driver_mounts, 0, sizeof(driver_mounts));
    memset(pfs_mount_used, 0, sizeof(pfs_mount_used));
    memset(attempts, 0, sizeof(attempts));
    for (int i = 0; i < MOUNT_LIMIT; i++) Party_vmcIndex[i] = -1;
    latestMount = -1; pfs_mount_clock = 0;
    mounts = unmounts = busy = submissions = mount_error = zone_error = 0;
    fail_unmount = 0; zone_size = 8192; now = 100;
    driver_status = read_error = format_reads = 0; driver_sectors = 4096 * SECTORS_PER_MB;
    memset(format_boot, 0, sizeof(format_boot));
    memset(format_mbr, 0, sizeof(format_mbr)); format_volume_lba = 0;
}
static void complete(void) {
    assert(queued && pfs_job == queued && busy);
    HddJob *j = queued; queued = NULL;
    queued_run(j); queued_done(j); busy = 0;
    assert(!pfs_job);
}
static void probe(int i) {
    loadPfsInfo(i); assert(queued); complete();
    assert(hm.parts[i].pfs_state == 1);
    assert(hm.parts[i].pfs_total == hm.parts[i].raw_mb && hm.parts[i].pfs_free == 2048);
}
static void preMount(int i, int part) {
    snprintf(mountedParty[i], sizeof(mountedParty[i]), "hdd0:+TEST%d", part);
    dashStrCopy(driver_mounts[i], mountedParty[i], sizeof(driver_mounts[i])); latestMount = i;
}
int main(void) {
    reset();
    for (int i = 0; i < 20; i++) {
        probe(i); assert(mountedCount() <= 3);
        for (int previous = i > 2 ? i - 2 : 0; previous <= i; previous++) {
            char dev[48]; snprintf(dev, sizeof(dev), "hdd0:+TEST%d", previous);
            assert(slotFor(dev) >= 0);
        }
        if (i) {
            int reads = submissions, changes = unmounts, count = mounts;
            loadPfsInfo(i - 1); loadPfsInfo(i);
            assert(!queued && submissions == reads && unmounts == changes && mounts == count);
        }
    }
    assert(mounts == 20 && unmounts == 17);
    /* A cached selection refreshes recency even though it needs no disk read. */
    reset(); probe(0); probe(1); probe(2); loadPfsInfo(0); probe(3);
    assert(slotFor("hdd0:+TEST0") >= 0 && slotFor("hdd0:+TEST2") >= 0);
    assert(slotFor("hdd0:+TEST1") < 0);
    /* Cached figures survive mount eviction; revisiting does not remount. */
    int reads = submissions, count = mounts; loadPfsInfo(1);
    assert(submissions == reads && mounts == count && hm.parts[1].pfs_state == 1);
    /* Already-mounted partitions are reused without duplicate raw opens. */
    reset(); for (int i = 0; i < 4; i++) preMount(i, i);
    probe(0); assert(!mounts && !unmounts && mountedCount() == 4);
    probe(4); assert(mountedCount() == 3 && mounts == 1 && unmounts == 2);
    assert(slotFor("hdd0:+TEST0") >= 0);
    /* Refresh from four existing mounts reserves a handle before Dopen.
     * Main and subpartition sizes are summed once, without opening them. */
    reset(); for (int i = 0; i < 4; i++) preMount(i, i);
    HddJob scan = {0}; scanRun(&scan);
    assert(scan.ret == 0 && scan.n == 3 && scan.used_mb == 1152);
    assert(scan.filesystem == HDD_FS_APA && format_reads == 1);
    assert(scan.parts[0].raw_mb == 384 && scan.parts[1].raw_mb == 256 && scan.parts[2].raw_mb == 512);
    assert(mountedCount() == 3 && unmounts == 1 && !live_scans); free(scan.parts);
    /* Never evict a VMC's mount or the most recently used mount. */
    reset(); probe(0); probe(1); probe(2);
    int pinned = slotFor("hdd0:+TEST0"); Party_vmcIndex[pinned] = 0;
    probe(3); assert(slotFor("hdd0:+TEST0") == pinned && !attempts[pinned]);
    /* Failed unmounts retain bookkeeping and another old slot is tried. */
    reset(); probe(0); probe(1); probe(2);
    int failed = slotFor("hdd0:+TEST0"); fail_unmount = 1u << failed;
    probe(3); assert(attempts[failed] == 1 && slotFor("hdd0:+TEST0") == failed);
    assert(slotFor("hdd0:+TEST1") < 0 && slotFor("hdd0:+TEST2") >= 0);
    /* If all eligible unmounts fail, retry after a second, not each frame. */
    reset(); probe(0); probe(1); probe(2); fail_unmount = 15;
    loadPfsInfo(3); complete();
    assert(hm.parts[3].pfs_state == 0 && hm.parts[3].pfs_error == -EBUSY);
    reads = submissions; now += 999; loadPfsInfo(3); assert(submissions == reads && !queued);
    fail_unmount = 0; now++; probe(3);
    /* VMC pressure reports temporary busy without disturbing any mount. */
    reset(); probe(0); probe(1); probe(2);
    for (int i = 0; i < MOUNT_LIMIT; i++) if (mountedParty[i][0]) Party_vmcIndex[i] = 0;
    loadPfsInfo(3); complete();
    assert(hm.parts[3].pfs_state == 0 && !unmounts && mountedCount() == 3);
    /* Invalid targets do not trigger blanket mount retries/unmounts. */
    reset(); probe(0); probe(1); mount_error = -EINVAL;
    loadPfsInfo(2); complete();
    assert(hm.parts[2].pfs_state == -1 && hm.parts[2].pfs_error == -EINVAL);
    assert(mounts == 3 && !unmounts && mountedCount() == 2);
    /* Preserve errors instead of casting negative zone counts to huge sizes. */
    const int errors[] = {-EIO, -EBUSY, -EMFILE, -EAGAIN};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        reset(); zone_error = errors[i]; loadPfsInfo(0); complete();
        assert(hm.parts[0].pfs_error == errors[i]);
        assert(hm.parts[0].pfs_state == (i == 0 ? -1 : 0));
        assert(!hm.parts[0].pfs_free);
    }
    reset(); zone_size = 0; loadPfsInfo(0); complete();
    assert(hm.parts[0].pfs_state == -1 && hm.parts[0].pfs_error == -EIO);
    reset(); busy = 1; loadPfsInfo(0); assert(!submissions); busy = 0;
    /* APA presence must not prevent detection of the other APA-Jail area. */
    for (unsigned format = HDD_FS_EXFAT; format <= HDD_FS_FAT32; format <<= 1) {
        reset(); jailBoot(format);
        memset(&scan, 0, sizeof(scan)); scanRun(&scan);
        assert(scan.ret == 0 && scan.formatted && scan.n == 3 && scan.filesystem == (HDD_FS_APA | format));
        assert(!scan.filesystem_error && format_reads == 4 && !mounts && !unmounts);
        hm.filesystem = scan.filesystem; hm.status = scan.status; actStatus();
        assert(strstr(status_body, "APA Jail") && strstr(status_body, "APA partitions") && strstr(status_body, "File Browser"));
        free(scan.parts);
    }
    /* Additional-probe failures cannot erase known APA or its partition list. */
    reset(); read_error = -EIO;
    memset(&scan, 0, sizeof(scan)); scanRun(&scan);
    assert(scan.ret == 0 && scan.formatted && scan.n == 3 && scan.filesystem == HDD_FS_APA);
    assert(scan.filesystem_error == -EIO);
    hm.filesystem = scan.filesystem; hm.filesystem_error = scan.filesystem_error; actStatus();
    assert(strstr(status_body, "APA partitions were detected") && strstr(status_body, "additional"));
    free(scan.parts);
    /* Detect the filesystem on HDD0 without mounting or touching ATA aliases. */
    for (unsigned format = HDD_FS_EXFAT; format <= HDD_FS_FAT32; format <<= 1) {
        reset(); driver_status = 1; formatBoot(format);
        memset(&scan, 0, sizeof(scan)); scanRun(&scan);
        assert(scan.present && !scan.formatted && scan.filesystem == format && !scan.filesystem_error);
        assert(!scan.parts && !scan.n && !mounts && !unmounts && !live_scans && format_reads == 1);
        hm.filesystem = scan.filesystem; hm.status = scan.status; actStatus();
        assert(strstr(status_body, hddFilesystemName(format)) && strstr(status_body, "File Browser"));
        assert(!strstr(status_body, "Format the HDD"));
    }
    reset(); driver_status = 1; formatBoot(HDD_FS_EXFAT); driver_sectors = 0xc0000000u;
    memset(&scan, 0, sizeof(scan)); scanRun(&scan);
    assert(scan.filesystem == HDD_FS_EXFAT && scan.size_mb == driver_sectors / SECTORS_PER_MB);
    reset(); driver_status = 1; read_error = -EIO;
    memset(&scan, 0, sizeof(scan)); scanRun(&scan);
    assert(!scan.filesystem && scan.filesystem_error == -EIO && format_reads == 1);
    hm.status = 1; hm.filesystem_error = scan.filesystem_error; actStatus();
    assert(strstr(status_body, "check failed") && strstr(status_body, "Refresh"));
    reset(); driver_status = 1;
    memset(&scan, 0, sizeof(scan)); scanRun(&scan);
    assert(!scan.filesystem && !scan.filesystem_error && format_reads == 1);
    hm.status = 1; actStatus(); assert(strstr(status_body, "No supported filesystem"));
    for (driver_status = 2; driver_status <= 3; driver_status++) {
        format_reads = 0; memset(&scan, 0, sizeof(scan)); scanRun(&scan);
        assert(!scan.filesystem && !format_reads && !scan.formatted);
    }
    free(hm.parts);
    puts("PASS: HDD0 APA/exFAT/FAT32 scan reporting, capacities above 1 TiB, locked/missing drives and read errors");
    puts("PASS: bounded recent mounts, cached navigation, existing mounts, scan handle reserve, VMC protection, failed unmounts, transient retries and usage errors");
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "A host C compiler is required"
with tempfile.TemporaryDirectory(prefix="hdd-mounts-") as directory:
    c, executable = Path(directory) / "check.c", Path(directory) / "check"
    c.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wformat-truncation=2", "-I", str(root / "include"),
                    str(c), str(root / "src/hdd_filesystem.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)
