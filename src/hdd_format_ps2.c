#include "launchelf.h"
#include "hdd_format.h"
#include "hdd_format_rpc.h"
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
    ret = SifCallRpc(&client, command, 0, &packet, send, &packet, receive, NULL, NULL);
    return ret < 0 ? ret : packet.result;
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
{ (void)ctx; dashWorkerStatus(stage, done, total); }

int hddFormatDisk(const HddFormatPlan *plan, int *started)
{
    HddFormatIo io = {NULL, readDisk, writeDisk, zeroDisk, flushDisk, apaDisk, progress};
    int ret, finish;
    *started = 0;
    packet.sectors = plan->sectors;
    ret = call(HF_BEGIN, 0, 0, 64, 64);
    if (ret < 0) return ret;
    *started = 1;
    ret = hddFormatExecute(plan, &io);
    finish = call(HF_FINISH, 0, 0, 64, 64);
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

int hddInstallFinish(const HddFormatPlan *plan, const char *mbr, int *started,
                     int (*populate)(void *, const HddFormatIo *), void *context)
{
    HddFormatIo io = {NULL, readDisk, writeDisk, zeroDisk, flushDisk, apaDisk, progress};
    HddFormatPlan expected;
    unsigned char *buffer = NULL, *verify = NULL;
    uint32_t bytes = 0, copied = 0, limit = 0;
    int fd = -1, ret = 0, finish;
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
        bytes = (uint32_t)length;
    }
    packet.sectors = plan->sectors;
    ret = call(HF_BEGIN, 0, 0, 64, 64);
    if (ret < 0) goto done;
    *started = 1;
    ret = hddApaLimit(readPhysicalSector, NULL, plan->sectors, &limit);
    if (ret < 0 || (plan->kind == HDD_FORMAT_APA_JAIL && limit != plan->apa_sectors)) { if (ret >= 0) ret = -EINVAL; goto finish; }
    if (populate) { ret = populate(context, &io); if (ret < 0) goto finish; }
    while (copied < bytes) {
        unsigned n = bytes - copied > 32768 ? 32768 : bytes - copied, read = 0;
        while (read < n) {
            int got = genRead(fd, buffer + read, n - read);
            if (got <= 0 || (unsigned)got > n - read) { ret = got < 0 ? got : -EIO; goto finish; }
            read += got;
        }
        if (!copied && memcmp(buffer, "\x01\x00\x00\x04", 4)) { ret = -EINVAL; goto finish; }
        dashWorkerStatus("Installing OSDMenu MBR", copied, bytes);
        ret = writeDisk(NULL, 8192 + copied / 512, n / 512, buffer);
        if (ret >= 0) ret = readDisk(NULL, 8192 + copied / 512, n / 512, verify);
        if (ret < 0 || memcmp(buffer, verify, n)) { if (ret >= 0) ret = -EIO; goto finish; }
        copied += n;
    }
    /* Persist the software and MBR payload before exposing its APA pointer. */
    ret = flushDisk(NULL); if (ret < 0) goto finish;
    if (mbr) {
        hddSetOsdMBR_t info;
        unsigned char header[1024] __attribute__((aligned(64)));
        info.start = 8192; info.size = bytes / 512;
        /* Update through the existing driver so its APA cache and checksum
         * preserve the A2 signature and MBR partition table. */
        ret = fileXioDevctl("hdd0:", HDIOC_SETOSDMBR, &info, sizeof(info), NULL, 0);
        if (ret >= 0) ret = readDisk(NULL, 0, 2, header);
        if (ret < 0) goto finish;
        {
            uint32_t first, count;
            memcpy(&first, header + 304, 4); memcpy(&count, header + 308, 4);
            if (first != 8192 || count != bytes / 512) { ret = -EIO; goto finish; }
        }
        ret = hddApaLimit(readPhysicalSector, NULL, plan->sectors, &limit);
        if (ret < 0) goto finish;
    }
    if (plan->kind == HDD_FORMAT_APA_JAIL) ret = hddFormatRecovery(plan, &io);
finish:
    finish = call(HF_FINISH, 0, 0, 64, 64);
    if (ret >= 0) ret = finish;
done:
    if (fd >= 0) { int closed = genClose(fd); if (ret >= 0 && closed < 0) ret = closed; }
    free(verify); free(buffer);
    return ret;
}
