#include "launchelf.h"
#include "hdd_format.h"
#include "hdd_install.h"
#include "hdd_format_rpc.h"
#include "save_sign.h"
#include "hdd_install_recovery.h"
#include "init.h"
#include "dash_internal.h"
#include <loadfile.h>
#include <sifrpc.h>

extern u8 hdd_format_irx[];
extern int size_hdd_format_irx;

static SifRpcClientData_t client __attribute__((aligned(64)));
static HddFormatPacket packet __attribute__((aligned(64)));
static unsigned generation;
static int loaded;

int hddFormatLoad(void)
{
    int ret, id, tries;
    unsigned gen = getIopResetGeneration();
    if (!loaded || generation != gen) {
        memset(&client, 0, sizeof(client));
        id = SifExecModuleBuffer(hdd_format_irx, size_hdd_format_irx, 0, NULL, &ret);
        if (id < 0 || ret != 0) return -EIO;
        loaded = 1; generation = gen;
    }
    for (tries = 0; tries < 200; tries++) {
        ret = SifBindRpc(&client, HDD_FORMAT_RPC, 0);
        if (ret < 0) return ret;
        if (client.server) return 0;
        DelayThread(10000);
    }
    return -ETIMEDOUT;
}

static int call(int command, uint32_t lba, unsigned count, unsigned send, unsigned receive)
{
    int ret;
    packet.lba = lba; packet.count = count;
    hddInstallRawTrace(command, lba, count, 1, 0);
    ret = SifCallRpc(&client, command, 0, &packet, send, &packet, receive, NULL, NULL);
    ret = ret < 0 ? ret : packet.result;
    hddInstallRecoveryObserve(ret);
    hddInstallRawTrace(command, lba, count, 0, ret);
    return ret;
}

static int readDisk(void *ctx, uint32_t lba, unsigned count, void *data)
{
    int ret;
    (void)ctx;
    if (!count || count > HDD_FORMAT_CHUNK) return -EINVAL;
    ret = call(HF_READ, lba, count, 64, 64 + count * 512);
    if (ret >= 0) memcpy(data, packet.data, count * 512);
    return ret;
}

static int writeDisk(void *ctx, uint32_t lba, unsigned count, const void *data)
{
    (void)ctx;
    if (!count || count > HDD_FORMAT_CHUNK) return -EINVAL;
    memcpy(packet.data, data, count * 512);
    return call(HF_WRITE, lba, count, 64 + count * 512, 64);
}

static int zeroDisk(void *ctx, uint32_t lba, uint32_t count)
{
    (void)ctx;
    while (count) {
        unsigned n = count > 2048 ? 2048 : count;
        int ret = call(HF_ZERO, lba, n, 64, 64);
        if (ret < 0) return ret;
        lba += n; count -= n;
    }
    return 0;
}
static int flushDisk(void *ctx)
{ (void)ctx; return call(HF_FLUSH, 0, 0, 64, 64); }
static int apaDisk(void *ctx)
{ (void)ctx; return hddFormat(); }
static void progress(void *ctx, const char *stage, uint32_t done, uint32_t total)
{
    static const char *const stages[] = {"Clearing old disk metadata", "Clearing old GPT backup",
        "Formatting APA system partitions", "Clearing exFAT allocation table", "Backing up APA recovery metadata"};
    (void)ctx; hddInstallStatus(stage, done, total);
    for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) if (!strcmp(stage, stages[i])) {
        uint64_t part = total ? (uint64_t)(done > total ? total : done) * 200 / total : 0;
        hddInstallOverall(i * 200 + part, 1000); break;
    }
}

int hddFormatDisk(const HddFormatPlan *plan, int *started)
{
    HddFormatIo io = {NULL, readDisk, writeDisk, zeroDisk, flushDisk, apaDisk, progress};
    int ret, finish;
    *started = 0;
    packet.sectors = plan->sectors;
    hddInstallLog("FORMAT begin: capacity=%u sectors; ATA will be detached", (unsigned)plan->sectors);
    ret = call(HF_BEGIN, 0, 0, 64, 64);
    hddInstallLog("FORMAT begin returned %d", ret);
    if (ret < 0) return ret;
    *started = 1;
    ret = hddFormatExecute(plan, &io);
    hddInstallLog("FORMAT execute returned %d", ret);
    finish = hddInstallRecoveryFault() ? HDD_INSTALL_ATA_TIMEOUT : call(HF_FINISH, 0, 0, 64, 64);
    return ret < 0 ? ret : finish;
}

static int readApa(void *context, uint32_t lba, void *sector)
{
    hddAtaTransfer_t transfer __attribute__((aligned(64)));
    memset(&transfer, 0, sizeof(transfer));
    transfer.lba = lba; transfer.size = 1;
    return fileXioDevctl(context, HDIOC_READSECTOR, &transfer, sizeof(transfer), sector, 512);
}

int hddGetApaCapacity(uint32_t *sectors)
{
    int total = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
    if (total >= -4095 && total <= 0) return total ? total : -ENODEV;
    return hddApaLimit(readApa, "hdd0:", (uint32_t)total, sectors);
}

int hddCheckAllocation(uint32_t sectors)
{
    int total = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
    if (total >= -4095 && total <= 0) return total ? total : -ENODEV;
    return hddApaCanAllocate(readApa, "hdd0:", (uint32_t)total, sectors);
}

static int readPhysicalSector(void *ctx, uint32_t lba, void *sector)
{ return readDisk(ctx, lba, 1, sector); }

static int mbrPointerMatches(uint32_t start, uint32_t sectors)
{
    unsigned char header[1024] __attribute__((aligned(64)));
    uint32_t installed_start, installed_sectors;
    int ret = readDisk(NULL, 0, 2, header);
    if (ret < 0) return ret;
    memcpy(&installed_start, header + 304, sizeof(installed_start));
    memcpy(&installed_sectors, header + 308, sizeof(installed_sectors));
    return installed_start == start && installed_sectors == sectors;
}

int hddInstallFinish(const HddFormatPlan *plan, const char *mbr, const char *mbr_sha1, int *started,
                     int (*populate)(void *, const HddFormatIo *), void *context)
{
    HddFormatIo io = {NULL, readDisk, writeDisk, zeroDisk, flushDisk, apaDisk, progress};
    HddFormatPlan expected;
    unsigned char *buffer = NULL, *verify = NULL, digest[20];
    SaveSha1 hash;
    char hex[41], header[40];
    const uint32_t mbr_start = 8192;
    uint32_t bytes = 0, copied = 0, limit = 0;
    int fd = -1, ret = 0, finish, active_mbr = 0;
    if (!started) return -EINVAL;
    *started = 0;
    if (!plan || plan->kind == HDD_FORMAT_EXFAT ||
        hddFormatPlan(&expected, plan->kind, plan->sectors, plan->reserve_mb, plan->serial) < 0 ||
        memcmp(&expected, plan, sizeof(expected))) return -EINVAL;
    if (!mbr && !populate && plan->kind != HDD_FORMAT_APA_JAIL) return 0;
    if (mbr) {
        s64 length;
        buffer = malloc(32768); verify = malloc(32768);
        if (!buffer || !verify) { ret = -ENOMEM; goto done; }
        fd = genOpen(mbr, FIO_O_RDONLY);
        if (fd < 0) { ret = fd; goto done; }
        length = genLseek(fd, 0, SEEK_END);
        if (length < 512 || length > 1024 * 1024 || length % 512 || genLseek(fd, 0, SEEK_SET) < 0) { ret = -EINVAL; goto done; }
        if (!mbr_sha1) { ret = -EINVAL; goto done; }
        bytes = (uint32_t)length;
        saveHashInit(&hash);
        snprintf(header, sizeof(header), "blob %u", bytes);
        saveHashUpdate(&hash, (const uint8_t *)header, strlen(header) + 1);
    }
    packet.sectors = plan->sectors;
    packet.write_limit = plan->exfat_start;
    hddInstallLog("RAW INSTALL begin capacity=%u write-limit=%u", (unsigned)plan->sectors, (unsigned)plan->exfat_start);
    ret = call(HF_INSTALL_BEGIN, 0, 0, 64, 64);
    hddInstallLog("RAW INSTALL begin returned %d", ret);
    if (ret < 0) goto done;
    *started = 1;
    ret = hddApaLimit(readPhysicalSector, NULL, plan->sectors, &limit);
    hddInstallLog("APA boundary rc=%d limit=%u expected=%u", ret, (unsigned)limit,
                  (unsigned)(plan->kind == HDD_FORMAT_APA_JAIL ? plan->apa_sectors : plan->sectors));
    if (ret < 0 || limit != (plan->kind == HDD_FORMAT_APA_JAIL ? plan->apa_sectors : plan->sectors)) { if (ret >= 0) ret = -EINVAL; goto finish; }
    if (populate) {
        hddInstallLog("Starting PSBBN filesystem population");
        ret = populate(context, &io);
        hddInstallLog("PSBBN filesystem population rc=%d", ret);
        if (ret < 0) goto finish;
    }
    hddInstallPhase(HDD_INSTALL_PHASE_FINISH);
    if (mbr) {
        ret = mbrPointerMatches(mbr_start, bytes / 512);
        if (ret < 0) goto finish;
        active_mbr = ret;
    }
    while (copied < bytes) {
        unsigned n = bytes - copied > 32768 ? 32768 : bytes - copied, read = 0;
        uint32_t lba = mbr_start + copied / 512;
        while (read < n) {
            int got = genRead(fd, buffer + read, n - read);
            if (got <= 0 || (unsigned)got > n - read) { ret = got < 0 ? got : -EIO; goto finish; }
            read += got;
        }
        if (!copied && memcmp(buffer, "\x01\x00\x00\x04", 4)) { ret = -EINVAL; goto finish; }
        saveHashUpdate(&hash, buffer, n);
        if (active_mbr) {
            hddInstallStatus("Verifying active OSDMenu MBR", copied, bytes);
            ret = readDisk(NULL, lba, n / 512, verify);
            if (ret < 0) goto finish;
            /* If these bytes differ, rewrite from this sector onward.
             * The preceding sectors have already been verified. */
            active_mbr = !memcmp(buffer, verify, n);
        }
        if (!active_mbr) {
            hddInstallStatus("Installing OSDMenu MBR", copied, bytes);
            ret = writeDisk(NULL, lba, n / 512, buffer);
            if (ret >= 0) ret = readDisk(NULL, lba, n / 512, verify);
            if (ret < 0 || memcmp(buffer, verify, n)) { if (ret >= 0) ret = -EIO; goto finish; }
        }
        copied += n;
        hddInstallProgress("Installing OSDMenu MBR", copied, bytes);
        hddInstallOverall(copied, (uint64_t)bytes * 4);
    }
    if (mbr) {
        saveHashFinish(&hash, digest);
        for (unsigned i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (strcmp(hex, mbr_sha1)) { ret = -EIO; goto finish; }
    }
    /* Persist the software and MBR payload before exposing its APA pointer. */
    ret = flushDisk(NULL); if (ret < 0) goto finish;
    if (mbr && active_mbr) {
        hddInstallLog("OSDMenu MBR is already active and verified");
    } else if (mbr) {
        hddSetOsdMBR_t info;
        info.start = mbr_start; info.size = bytes / 512;
        /* Update through the existing driver so its APA cache and checksum
         * preserve the A2 signature and MBR partition table. */
        hddInstallLog("Activating verified OSDMenu MBR: LBA=%u bytes=%u", (unsigned)mbr_start, (unsigned)bytes);
        ret = fileXioDevctl("hdd0:", HDIOC_SETOSDMBR, &info, sizeof(info), NULL, 0);
        if (ret >= 0) ret = mbrPointerMatches(info.start, info.size);
        if (ret <= 0) { if (!ret) ret = -EIO; goto finish; }
        ret = hddApaLimit(readPhysicalSector, NULL, plan->sectors, &limit);
        if (ret < 0) goto finish;
    }
    /* The recovery volume is a creation-time snapshot, not a post-install
     * rewrite target. Keeping it unchanged preserves the last readable copy. */
finish:
    hddInstallLog("Finishing raw install: rc=%d", ret);
    finish = hddInstallRecoveryFault() ? HDD_INSTALL_ATA_TIMEOUT : call(HF_FINISH, 0, 0, 64, 64);
    hddInstallLog("Raw install flush/end rc=%d", finish);
    if (ret >= 0) ret = finish;
done:
    if (fd >= 0 && !hddInstallRecoveryFault()) { int closed = genClose(fd); if (ret >= 0 && closed < 0) ret = closed; }
    if (ret >= 0) hddInstallOverall(900, 1000);
    free(verify); free(buffer);
    return ret;
}
