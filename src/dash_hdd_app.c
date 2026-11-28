#include "dash_internal.h"
#include "hdd_app.h"
#include "hdd_install.h"
#include "hdd_header_injector.h"
#include "filer_shared.h"
#include "filer_size_cache.h"
#include "init.h"
#include "dash_sound.h"

#if defined(ETH) && defined(WLE_NATIVE_CLOUD) && defined(EXFAT)
#include "cloud_http.h"
#include <hdd-ioctl.h>
#include <libhdd.h>
#include <ctype.h>
#include <malloc.h>
#include <time.h>
#include <timer.h>
#include <zlib.h>

#define APP_TEMP "ata0:/LAUNCHSTATION_APP_TEMP"
#define APP_MAX_ASSET (512 * 1024)
typedef struct {
    SaveArchive save;
    HddApp app;
    HddAppJournal journal;
    HddInstall network;
    unsigned char *package;
    unsigned package_size, crc;
    char source[MAX_PATH], stage[MAX_PATH], target[128], pending[33];
    char installed_partition[33]; /* Actual ID; ownership keeps its stable base ID. */
    char settings[1024], cnf[512], info[1024];
    char icon_source[96], art_source[96];
    const char *operation;
    char location[MAX_PATH];
    int result, resumed, mounted, cleanup_error, committed, metadata_error;
    int index, count;
} AppInstall;

static void context(AppInstall *in, const char *operation, const char *path)
{
    in->operation = operation;
    in->network.error[0] = 0;
    snprintf(in->location, sizeof(in->location), "%s", path ? path : "");
    dashWorkerStatus(operation, 0, 0);
}

static int join(char *out, const char *parent, const char *leaf)
{ return snprintf(out, MAX_PATH, "%s/%s", parent, leaf) < MAX_PATH ? 0 : -ENAMETOOLONG; }
static int flush(void)
{ return fileXioDevctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0); }
static int finish(int fd, int ret)
{
    hddInstallRecoveryObserve(ret);
    if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    int closed = genClose(fd);
    return ret < 0 ? ret : closed < 0 ? closed : 0;
}
static int transfer(int fd, void *data, unsigned size, int writing)
{
    unsigned done = 0;
    while (done < size) {
        unsigned count = size - done < 65536 ? size - done : 65536;
        int n = writing ? genWrite(fd, (unsigned char *)data + done, count) : genRead(fd, (unsigned char *)data + done, count);
        if (n <= 0 || (unsigned)n > count) return n < 0 ? n : -EIO;
        done += n;
        dashWorkerStatus(NULL, done, size);
    }
    return 0;
}
static int writeFile(const char *path, const void *data, unsigned size)
{
    int fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    return fd < 0 ? fd : finish(fd, transfer(fd, (void *)data, size, 1));
}
static int readFile(const char *path, unsigned char **data, unsigned *size, unsigned maximum)
{
    int ret = 0, fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0) return fd;
    s64 length = genLseek(fd, 0, SEEK_END);
    if (length <= 0 || length > maximum || genLseek(fd, 0, SEEK_SET) != 0) ret = -EFBIG;
    else if (!(*data = malloc(length + 1))) ret = -ENOMEM;
    else { *size = length; ret = transfer(fd, *data, *size, 0); (*data)[*size] = 0; }
    return finish(fd, ret);
}
static int verifyPackage(const AppInstall *in, const char *path)
{
    unsigned char *buffer = memalign(64, 16384);
    if (!buffer) return -ENOMEM;
    int fd = genOpen(path, FIO_O_RDONLY);
    int ret = fd;
    if (fd >= 0) {
        s64 length = genLseek(fd, 0, SEEK_END);
        ret = length < 0 ? (int)length : length != in->package_size ? -EBADMSG :
              genLseek(fd, 0, SEEK_SET) == 0 ? 0 : -EIO;
        for (unsigned offset = 0; ret >= 0 && offset < in->package_size;) {
            unsigned size = in->package_size - offset;
            if (size > 16384) size = 16384;
            ret = transfer(fd, buffer, size, 0);
            if (ret >= 0 && memcmp(buffer, in->package + offset, size)) ret = -EBADMSG;
            offset += size;
        }
        ret = finish(fd, ret);
    }
    free(buffer);
    return ret;
}
static int cachePackage(AppInstall *in)
{
    char path[MAX_PATH], pending[MAX_PATH];
    if (join(path, in->stage, "PACKAGE.PSU") < 0 || join(pending, in->stage, "PACKAGE.NEW.PSU") < 0)
        return -ENAMETOOLONG;
    int cached = verifyPackage(in, path);
    if (cached == 0 || (cached != -ENOENT && cached != -EBADMSG)) return cached;
    int ret = verifyPackage(in, pending);
    if (ret == -ENOENT || ret == -EBADMSG) {
        ret = writeFile(pending, in->package, in->package_size);
        if (ret >= 0) ret = verifyPackage(in, pending);
    }
    if (ret >= 0) ret = flush();
    if (ret >= 0 && cached == -EBADMSG) ret = genRemove(path);
    if (ret >= 0) ret = fileXioRename(pending, path);
    return ret < 0 ? ret : flush();
}
static int exists(const char *path)
{
    iox_stat_t stat;
    int ret = genGetStat(path, &stat);
    return ret >= 0 ? 1 : ret == -ENOENT ? 0 : ret;
}
static int mkdirChecked(const char *path)
{
    iox_stat_t stat;
    int ret = genGetStat(path, &stat);
    if (ret >= 0) return FIO_S_ISDIR(stat.mode) ? 0 : -ENOTDIR;
    if (ret != -ENOENT) return ret;
    return genMkdir(path, 0777);
}
static int releasePfs(void)
{
    /* Keep the live app stable while it is being staged and published. */
    if (vmcMounted[0] || vmcMounted[1] || isFtpServerRunning() || isSmbServerRunning()) return -EBUSY;
    for (int i = 0; i < MOUNT_LIMIT; i++) {
        unmountParty(i);
        if (mountedParty[i][0]) return -EBUSY;
    }
    return 0;
}
static int identity(const AppInstall *in, char out[160], int package)
{
    return snprintf(out, 160, "LSAPP1\n%s\n%s\n%s%08lx\n", in->app.folder, in->app.partition,
                    package ? "PSU=" : "APP=", package ? (unsigned long)in->crc : 0ul);
}
static int checkOwner(AppInstall *in, const char *parent, int package)
{
    char path[MAX_PATH], expected[160];
    unsigned char *data = NULL; unsigned size = 0;
    int n = identity(in, expected, package), ret = join(path, parent, "LSAPP.ID");
    if (ret >= 0) ret = readFile(path, &data, &size, 159);
    if (ret >= 0) {
        if (size != (unsigned)n) ret = -EEXIST;
        else if (package < 0) {
            /* Any prior package CRC is acceptable for an owned app update. */
            unsigned prefix = (unsigned)n - 9;
            if (memcmp(data, expected, prefix) || data[n - 1] != '\n') ret = -EEXIST;
            else for (unsigned i = prefix; i < (unsigned)n - 1; i++)
                if (!isxdigit(data[i])) { ret = -EEXIST; break; }
        } else if (memcmp(data, expected, size)) ret = -EEXIST;
    }
    free(data); return ret;
}
static int writeOwner(AppInstall *in, const char *parent, int package)
{
    char path[MAX_PATH], text[160];
    int n = identity(in, text, package), ret = join(path, parent, "LSAPP.ID");
    return ret < 0 ? ret : writeFile(path, text, n);
}
static void pendingName(const AppInstall *in, uint64_t id, char name[33])
{
    snprintf(name, 33, "+LSAPP.%06x.%016llx", in->crc & 0xffffffu, (unsigned long long)id);
}
static int readJournal(AppInstall *in)
{
    HddAppJournal best = {0};
    for (unsigned slot = 0; slot < 2; slot++) {
        char path[MAX_PATH], leaf[24]; unsigned char *data = NULL; unsigned size = 0;
        HddAppJournal j;
        snprintf(leaf, sizeof(leaf), "RESUME%u.BIN", slot); join(path, in->stage, leaf);
        int ret = readFile(path, &data, &size, HDD_APP_JOURNAL_SIZE);
        if (ret >= 0 && size == HDD_APP_JOURNAL_SIZE && !hddAppJournalDecode(&j, data) && j.sequence > best.sequence) best = j;
        free(data);
        if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    }
    if (!best.sequence || best.package_crc != in->crc || best.package_size != in->package_size) return -EINVAL;
    for (unsigned i = 0; i < in->app.elf_count; i++) {
        if (strcmp(in->save.files[in->app.elf[i]].name, best.boot)) continue;
        int ret = hddAppChooseBoot(&in->save, &in->app, i);
        if (ret < 0) return ret;
        in->journal = best;
        if (best.pending_id) pendingName(in, best.pending_id, in->pending);
        return 0;
    }
    return -EINVAL;
}
static int checkpoint(AppInstall *in, unsigned phase)
{
    unsigned char data[HDD_APP_JOURNAL_SIZE]; char path[MAX_PATH], leaf[24];
    HddAppJournal next = in->journal;
    next.sequence++; next.phase = phase;
    int ret = hddAppJournalEncode(&next, data);
    snprintf(leaf, sizeof(leaf), "RESUME%u.BIN", (unsigned)(next.sequence & 1)); join(path, in->stage, leaf);
    context(in, "Saving app recovery journal", path);
    if (ret >= 0) ret = flush();
    if (ret >= 0) ret = writeFile(path, data, sizeof(data));
    if (ret >= 0) ret = flush();
    if (ret >= 0) in->journal = next;
    return ret;
}
static int mountPartition(AppInstall *in, const char *name)
{
    char path[40]; snprintf(path, sizeof(path), "hdd0:%s", name);
    context(in, "Mounting PFS partition", path);
    int ret = fileXioMount("pfs3:", path, FIO_MT_RDWR);
    if (ret >= 0) {
        in->mounted = 1;
        strcpy(mountedParty[3], path);
    }
    return ret;
}
static int unmountPartition(AppInstall *in, int ret)
{
    if (in->mounted && !hddInstallRecoveryFault()) {
        if (ret >= 0) context(in, "Unmounting PFS partition", mountedParty[3]);
        int closed = fileXioUmount("pfs3:");
        if (closed < 0 && ret >= 0) ret = closed;
        if (closed >= 0) { in->mounted = 0; mountedParty[3][0] = 0; }
    }
    return ret;
}
static int partitionOwner(AppInstall *in, const char *name, int package)
{
    int ret = mountPartition(in, name);
    if (ret >= 0) {
        context(in, "Checking launcher ownership", "pfs3:/res/LSAPP.ID");
        ret = checkOwner(in, "pfs3:/res", package);
        /* Older published launchers used a package-independent app marker. */
        if (package < 0 && ret == -EEXIST) ret = checkOwner(in, "pfs3:/res", 0);
    }
    return unmountPartition(in, ret);
}
static int stageRestartable(const char *path)
{
    static const char *const files[] = {"RESUME0.BIN", "RESUME1.BIN", "PACKAGE.PSU", "PACKAGE.NEW.PSU",
        "DP-TEMPLATE.TXT", "LIST.ICO", "DELETE.ICO", "JACKET.PNG", "ICON.SYS", "UPSTREAM.TXT"};
    iox_dirent_t entry;
    int fd = fileXioDopen(path), ret;
    if (fd < 0) return fd;
    while ((ret = fileXioDread(fd, &entry)) > 0) {
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, "..")) continue;
        unsigned i;
        for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
            if (!strcmp(entry.name, files[i])) break;
        /* Before publication starts, only disposable downloads and journals
         * exist here. Never restart over NEW/OLD app trees or unknown files. */
        if (i == sizeof(files) / sizeof(files[0]) || !FIO_S_ISREG(entry.stat.mode)) { ret = -EEXIST; break; }
    }
    int closed = fileXioDclose(fd); return ret < 0 ? ret : closed < 0 ? closed : 0;
}
static void snapshot(void *arg)
{
    AppInstall *in = arg;
    char source[MAX_PATH];
    context(in, "Reading app PSU", in->source);
    /* An earlier queued install releases every PFS mount, so mount an HDD
     * partition source again at its own turn. */
    if (!strncmp(in->source, "hdd0:/", 6)) {
        if (genFixPath(in->source, source) < 0) { in->result = -ENODEV; return; }
    } else dashStrCopy(source, in->source, sizeof(source));
    in->result = readFile(source, &in->package, &in->package_size, SAVE_ARCHIVE_LIMIT);
    if (in->result < 0) return;
    if (!saveArchiveParse(&in->save, in->package, in->package_size, "APP.PSU")) {
        snprintf(in->network.error, sizeof(in->network.error), "The selected file is not a valid, complete PSU archive.");
        in->result = -EINVAL; return;
    }
    context(in, "Checking app PSU contents", in->save.folder.name);
    in->result = hddAppInspect(&in->save, &in->app);
    if (in->result < 0) snprintf(in->network.error, sizeof(in->network.error), "%s", in->result == -ENOEXEC ?
        "The PSU does not contain a PS2 ELF executable." :
        "The PSU contains unsupported filenames, duplicate names, reserved LSAPP.ID metadata, or non-file entries.");
    in->crc = crc32(0, in->package, in->package_size);
}

static int apiCommit(AppInstall *in, const char *repository, char commit[41])
{
    char path[192]; CloudHttpResponse response = {0};
    snprintf(path, sizeof(path), "/repos/%s/git/ref/heads/main", repository);
    context(in, "Looking up HDD asset version", repository);
    int ret = cloudHttpRequest("GET", "api.github.com", path, "Accept: application/vnd.github+json\r\n", NULL, 0,
                           in->network.ca_path, &response, in->network.error, sizeof(in->network.error));
    if (ret >= 0) {
        if (response.status != 200 || !response.body) {
            snprintf(in->network.error, sizeof(in->network.error), "GitHub metadata returned HTTP %d.", response.status);
            ret = -EIO;
        } else if (!hddInstallCommit(response.body, commit)) {
            snprintf(in->network.error, sizeof(in->network.error), "GitHub returned unrecognized repository version metadata.");
            ret = -EINVAL;
        }
    }
    cloudHttpResponseFree(&response); return ret;
}
static void progress(int done, int total, void *arg)
{ dashWorkerStatus(arg, done, total); }
static int download(AppInstall *in, const char *repository, const char *commit, const char *remote, const char *leaf)
{
    char url[384], path[MAX_PATH]; int size = 0;
    int ret = join(path, in->stage, leaf);
    if (ret < 0) return ret;
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/%s/%s", repository, commit, remote);
    context(in, "Downloading HDD app asset", path);
    dashWorkerStatus(remote, 0, 0);
    int fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    if (fd < 0) return fd;
    ret = cloudHttpDownloadAlignedFd(url, fd, in->network.ca_path, APP_MAX_ASSET, &size, NULL,
                               progress, (void *)remote, in->network.error, sizeof(in->network.error));
    ret = finish(fd, ret < 0 ? ret : size > 0 ? 0 : -EIO);
    if (ret == -ENOENT) genRemove(path);
    return ret;
}
static int assets(AppInstall *in)
{
    char commit[41], db_commit[41] = "", remote[192];
    int ret = apiCommit(in, HDD_INSTALL_REPOSITORY, commit);
    if (ret < 0) return ret;
    ret = download(in, HDD_INSTALL_REPOSITORY, commit, "scripts/Game-Installer.sh", "DP-TEMPLATE.TXT");
    if (ret < 0) return ret;
    char path[MAX_PATH]; unsigned char *text = NULL; unsigned size = 0;
    join(path, in->stage, "DP-TEMPLATE.TXT");
    ret = readFile(path, &text, &size, APP_MAX_ASSET);
    if (ret >= 0) {
        ret = hddAppIconSettings((char *)text, &in->app, in->settings);
        if (ret < 0) snprintf(in->network.error, sizeof(in->network.error), "PSBBN DP's HDD icon metadata template was not recognized.");
    }
    free(text); if (ret < 0) return ret;
    for (unsigned i = 0; i < 2; i++) {
        const char *suffix = i ? "DEL" : "LST", *leaf = i ? "DELETE.ICO" : "LIST.ICO";
        snprintf(remote, sizeof(remote), "icons/ico/%s_%s.ico", in->app.asset, suffix);
        ret = download(in, HDD_INSTALL_REPOSITORY, commit, remote, leaf);
        if (ret == -ENOENT) {
            if (!i) { ret = apiCommit(in, "CosmicScale/HDD-OSD-Icon-Database", db_commit); if (ret < 0) return ret; }
            else if (!db_commit[0]) { ret = apiCommit(in, "CosmicScale/HDD-OSD-Icon-Database", db_commit); if (ret < 0) return ret; }
            snprintf(remote, sizeof(remote), "apps/%s_%s.ico", in->app.asset, suffix);
            ret = download(in, "CosmicScale/HDD-OSD-Icon-Database", db_commit, remote, leaf);
        }
        if (ret == -ENOENT) {
            snprintf(remote, sizeof(remote), "icons/ico/APP_DEFAULT_%s.ico", suffix);
            ret = download(in, HDD_INSTALL_REPOSITORY, commit, remote, leaf);
            snprintf(in->icon_source, sizeof(in->icon_source), "DP generic (matching icon unavailable)");
        }
        if (ret < 0) return ret;
    }
    if (!in->icon_source[0]) snprintf(in->icon_source, sizeof(in->icon_source), "HDD icon: %s", in->app.asset);
    snprintf(remote, sizeof(remote), "icons/art/%s.png", in->app.asset);
    ret = download(in, HDD_INSTALL_REPOSITORY, commit, remote, "JACKET.PNG");
    if (ret == -ENOENT) {
        char art_commit[41];
        ret = apiCommit(in, "CosmicScale/psbbn-art-database", art_commit);
        if (ret < 0) return ret;
        snprintf(remote, sizeof(remote), "apps/%s.png", in->app.asset);
        ret = download(in, "CosmicScale/psbbn-art-database", art_commit, remote, "JACKET.PNG");
    }
    if (ret == -ENOENT) {
        ret = download(in, HDD_INSTALL_REPOSITORY, commit, "icons/art/APP.png", "JACKET.PNG");
        strcpy(in->art_source, "DP generic artwork");
    } else snprintf(in->art_source, sizeof(in->art_source), "%s", in->app.asset);
    if (ret < 0) return ret;
    /* Reject empty/error bodies before any partition/app mutation. */
    join(path, in->stage, "JACKET.PNG"); text = NULL; size = 0;
    ret = readFile(path, &text, &size, APP_MAX_ASSET);
    if (ret >= 0 && (size < 24 || memcmp(text, "\211PNG\r\n\032\n", 8))) {
        ret = -EINVAL;
        snprintf(in->network.error, sizeof(in->network.error), "Downloaded HDD artwork is not a PNG image.");
    }
    free(text); if (ret < 0) return ret;
    join(path, in->stage, "ICON.SYS"); ret = writeFile(path, in->settings, strlen(in->settings));
    if (ret >= 0) { join(path, in->stage, "UPSTREAM.TXT"); ret = writeFile(path, commit, 40); }
    return ret;
}

static int existingPartition(AppInstall *in, iox_stat_t *stat)
{
    char path[40];
    memcpy(in->installed_partition, in->app.partition, sizeof(in->installed_partition));
    snprintf(path, sizeof(path), "hdd0:%s", in->installed_partition);
    int ret = fileXioGetStat(path, stat);
    if (ret == -ENOENT && strlen(in->app.partition) <= 25) {
        snprintf(in->installed_partition, sizeof(in->installed_partition), "%.25s.HIDDEN", in->app.partition);
        snprintf(path, sizeof(path), "hdd0:%s", in->installed_partition);
        ret = fileXioGetStat(path, stat);
    }
    return ret;
}
static int validateTarget(AppInstall *in)
{
    char partition[40]; iox_stat_t stat;
    int part = existingPartition(in, &stat), target = exists(in->target);
    if (target < 0 || (part < 0 && part != -ENOENT)) return target < 0 ? target : part;
    if (part >= 0) {
        if (stat.mode != 0x100 || stat.attr) return -EEXIST;
        int ret = partitionOwner(in, in->installed_partition, -1);
        if (ret < 0) return ret;
        in->journal.update = 1;
        /* Folder ownership is checked independently; never adopt an unrelated
         * ATA folder merely because a PP name matches. */
        if (target && (ret = checkOwner(in, in->target, -1)) < 0) return ret;
        return target ? 0 : -ENOENT;
    }
    if (target) return -EEXIST;
    snprintf(partition, sizeof(partition), "hdd0:%s", in->pending);
    part = fileXioGetStat(partition, &stat);
    if (part >= 0) return -EEXIST;
    if (part != -ENOENT) return part;
    return hddCheckAllocation(8u * 2048);
}

static int removeTree(char *path, unsigned depth)
{
    if (depth > MAX_RECURSE) return -ELOOP;
    int fd = fileXioDopen(path), ret;
    iox_dirent_t entry; char child[MAX_PATH];
    if (fd < 0) return fd;
    while ((ret = fileXioDread(fd, &entry)) > 0) {
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, "..")) continue;
        if (strpbrk(entry.name, "/\\:") || join(child, path, entry.name) < 0) { ret = -EINVAL; break; }
        ret = FIO_S_ISDIR(entry.stat.mode) ? removeTree(child, depth + 1) : genRemove(child);
        if (ret < 0) break;
    }
    if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    int closed = fileXioDclose(fd);
    return ret < 0 ? ret : closed < 0 ? closed : genRmdir(path);
}
static void prepare(void *arg)
{
    AppInstall *in = arg;
    context(in, "Releasing HDD mounts", "hdd0:");
    int ret = releasePfs();
    if (ret < 0) { in->result = ret; return; }
    uint32_t limit;
    context(in, "Reading APA Jail boundary", "hdd0:");
    ret = hddGetApaCapacity(&limit);
    if (ret < 0) { in->result = ret; return; }
    context(in, "Checking HDD status", "hdd0:");
    ret = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    if (ret) { in->result = ret < 0 ? ret : -ENODEV; return; }
    /* Only an APA Jail hybrid is suitable for a launcher targeting ata0. */
    int sectors = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
    if (sectors >= -4095 && sectors <= 0) { in->result = sectors ? sectors : -ENODEV; return; }
    if (limit >= (uint32_t)sectors) {
        snprintf(in->network.error, sizeof(in->network.error), "This HDD has no APA Jail FAT/exFAT area for ata0:/APPS.");
        in->result = -ENOTSUP; return;
    }
    context(in, "Checking free APA space", "hdd0:");
    ret = hddCheckAllocation(8u * 2048);
    /* A full, valid chain can still update an existing managed launcher. */
    if (ret < 0 && ret != -ENOSPC) { in->result = ret; return; }
    context(in, "Checking HOSDMenu installation", "hdd0:__system/osdmenu/hosdmenu.elf");
    ret = mountPartition(in, "__system");
    if (ret >= 0) ret = exists("pfs3:/osdmenu/hosdmenu.elf");
    ret = unmountPartition(in, ret == 1 ? 0 : ret < 0 ? ret : -ENOENT);
    if (ret < 0) {
        snprintf(in->network.error, sizeof(in->network.error), "Install HOSDMenu on this APA Jail HDD first.");
        in->result = ret; return;
    }
    context(in, "Preparing app download folder", APP_TEMP);
    ret = mkdirChecked(APP_TEMP);
    if (ret < 0) { in->result = ret; return; }
    char folded[33];
    for (unsigned i = 0; i <= strlen(in->app.folder); i++) folded[i] = toupper((unsigned char)in->app.folder[i]);
    unsigned id = crc32(0, (unsigned char *)folded, strlen(folded));
    snprintf(in->stage, sizeof(in->stage), APP_TEMP "/%08x-%08x", id, in->crc);
    snprintf(in->target, sizeof(in->target), "ata0:/APPS/%s", in->app.folder);
    snprintf(in->pending, sizeof(in->pending), "+LSAPP.%08x.%08x", id, in->crc);
    context(in, "Checking staged app installation", in->stage);
    int present = exists(in->stage);
    if (present < 0) { in->result = present; return; }
    if (present) {
        ret = readJournal(in);
        if (ret >= 0 && in->journal.phase >= HA_DONE) {
            /* A finished install whose cleanup failed is never resumed: its
             * launcher was published, so start a fresh install instead. */
            memset(&in->journal, 0, sizeof(in->journal));
            snprintf(in->pending, sizeof(in->pending), "+LSAPP.%08x.%08x", id, in->crc);
            ret = removeTree(in->stage, 0);
            if (ret >= 0) ret = genMkdir(in->stage, 0777);
        } else if (ret >= 0) in->resumed = 1;
        else {
            if (hddInstallRecoveryFault()) { in->result = HDD_INSTALL_ATA_TIMEOUT; return; }
            ret = stageRestartable(in->stage);
            if (ret < 0) {
                if (ret == -EEXIST) snprintf(in->network.error, sizeof(in->network.error), "The staged installation has no valid recovery journal and contains app data. It was retained to protect those files.");
                in->result = ret; return;
            }
        }
    } else ret = genMkdir(in->stage, 0777);
    if (ret < 0) { in->result = ret; return; }
    if (!in->resumed) {
        context(in, "Checking app installation destination", in->target);
        ret = validateTarget(in);
        if (ret < 0) {
            if (ret == -EEXIST) snprintf(in->network.error, sizeof(in->network.error), "An existing app folder or launcher partition has a different owner.");
            if (ret == -ENOSPC) snprintf(in->network.error, sizeof(in->network.error), "There is no suitable free APA space for an 8 MiB launcher.");
            in->result = ret; return;
        }
        in->journal.package_crc = in->crc; in->journal.package_size = in->package_size;
        strcpy(in->journal.boot, in->app.boot);
        context(in, "Saving app recovery journal", in->stage);
        ret = checkpoint(in, HA_STAGED);
        if (ret < 0) { in->result = ret; return; }
    }
    if (in->journal.update) {
        iox_stat_t stat;
        ret = existingPartition(in, &stat);
        if (ret < 0) { in->result = ret; return; }
    }
    context(in, "Staging app PSU", in->stage);
    ret = cachePackage(in);
    if (ret >= 0) {
        cloudHttpSetCaBundle(in->network.ca_data, in->network.ca_size);
        cloudHttpDownloadBatchBegin();
        ret = assets(in);
        cloudHttpDownloadBatchEnd();
        cloudHttpSetCaBundle(NULL, 0);
    }
    if (ret >= 0) ret = flush();
    if (ret >= 0) in->network.error[0] = 0;
    in->result = ret;
}

/* Copy preserves user files absent from the replacement PSU. A private staging
 * directory and bounded depth keep package files away from a live app folder. */
static int copyFile(const char *from, const char *to)
{
    unsigned char *buffer = memalign(64, 65536);
    if (!buffer) return -ENOMEM;
    int src = genOpen(from, FIO_O_RDONLY), dst = -1, ret = src < 0 ? src : 0;
    if (ret >= 0) { dst = genOpen(to, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC); if (dst < 0) ret = dst; }
    while (ret >= 0) {
        int n = genRead(src, buffer, 65536);
        if (!n) break;
        if (n < 0) { ret = n; break; }
        ret = transfer(dst, buffer, n, 1);
    }
    if (dst >= 0) ret = finish(dst, ret);
    if (src >= 0) ret = finish(src, ret);
    free(buffer); return ret;
}
static int copyTree(const char *from, const char *to, unsigned depth)
{
    if (depth > MAX_RECURSE) return -ELOOP;
    int ret = mkdirChecked(to), fd = -1;
    iox_dirent_t entry;
    char src[MAX_PATH], dst[MAX_PATH];
    if (ret >= 0) { fd = fileXioDopen(from); if (fd < 0) ret = fd; }
    while (ret >= 0 && (ret = fileXioDread(fd, &entry)) > 0) {
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, "..")) continue;
        if (strpbrk(entry.name, "/\\:") || join(src, from, entry.name) < 0 || join(dst, to, entry.name) < 0) { ret = -ENAMETOOLONG; break; }
        dashWorkerStatus(entry.name, 0, 0);
        if (FIO_S_ISDIR(entry.stat.mode)) ret = copyTree(src, dst, depth + 1);
        else if (FIO_S_ISREG(entry.stat.mode)) ret = copyFile(src, dst);
        else ret = -EINVAL;
    }
    if (fd >= 0 && !hddInstallRecoveryFault()) { int closed = fileXioDclose(fd); if (ret >= 0) ret = closed; }
    return ret;
}
static int renameAbsent(const char *from, const char *to)
{
    int present = exists(to);
    return present ? present < 0 ? present : -EEXIST : fileXioRename(from, to);
}
static int pendingPartition(AppInstall *in)
{
    char path[64]; iox_stat_t stat;
    snprintf(path, sizeof(path), "hdd0:%s", in->pending);
    int ret = fileXioGetStat(path, &stat);
    if (!in->journal.pending_id && ret >= 0) {
        if (stat.mode != 0x100 || stat.attr || stat.private_0 || stat.size != 8u * 2048 || stat.private_5 < 262144) return -EEXIST;
        /* Existing journals without a creation intent still require the marker. */
        ret = partitionOwner(in, in->pending, 1);
        if (ret < 0 && !hddInstallRecoveryFault())
            snprintf(in->network.error, sizeof(in->network.error), "Ownership of %s could not be verified. The partition was left untouched.", in->pending);
        return ret;
    }
    if (ret < 0 && ret != -ENOENT) return ret;
    if (!in->journal.pending_id) {
        uint64_t id = ((uint64_t)time(NULL) << 32) ^ GetTimerSystemTime();
        if (!id) id = 1;
        char pending[33];
        pendingName(in, id, pending);
        snprintf(path, sizeof(path), "hdd0:%s", pending);
        ret = fileXioGetStat(path, &stat);
        if (ret != -ENOENT) return ret < 0 ? ret : -EEXIST;
        in->journal.pending_id = id;
        ret = checkpoint(in, HA_CREATE);
        if (ret < 0) {
            in->journal.pending_id = 0;
            return ret;
        }
        strcpy(in->pending, pending);
        ret = -ENOENT;
    }
    if (ret == -ENOENT) {
        context(in, "Creating 8 MiB launcher partition", path);
        ret = hddCreatePfsExtent(8, in->pending);
        in->metadata_error = ret == -EIO;
        if (ret < 0) return ret;
        ret = fileXioGetStat(path, &stat);
    }
    if (ret < 0) return ret;
    if (stat.mode != 0x100 || stat.attr || stat.private_0 || stat.size != 8u * 2048 || stat.private_5 < 262144) return -EEXIST;
    /* No app files or launcher header are published until after HA_PARTITION. */
    context(in, "Formatting launcher partition", path);
    ret = hddFormatPfs(path);
    in->metadata_error = ret == -EIO;
    if (ret >= 0) ret = mountPartition(in, in->pending);
    if (ret >= 0) {
        context(in, "Creating launcher metadata folder", "pfs3:/res");
        ret = mkdirChecked("pfs3:/res");
    }
    if (ret >= 0) {
        context(in, "Saving launcher ownership", "pfs3:/res/LSAPP.ID");
        ret = writeOwner(in, "pfs3:/res", 1);
    }
    ret = unmountPartition(in, ret);
    return ret < 0 ? ret : flush();
}
static int installHeader(AppInstall *in)
{
    char path[MAX_PATH], name[40];
    unsigned char *list = NULL, *del = NULL;
    unsigned list_size = 0, del_size = 0;
    const char *part = in->journal.update ? in->installed_partition : in->pending;
    int ret = hddAppSystemCnf(&in->app, in->cnf);
    if (ret >= 0) ret = hddAppInfo(&in->app, in->info);
    if (ret >= 0) ret = mountPartition(in, part);
    if (ret >= 0) ret = mkdirChecked("pfs3:/res");
    if (ret >= 0) {
        context(in, "Writing app metadata", "pfs3:/res/info.sys");
        ret = writeFile("pfs3:/res/info.sys", in->info, strlen(in->info));
    }
    if (ret >= 0) {
        context(in, "Installing app artwork", "pfs3:/res/jkt_001.png");
        join(path, in->stage, "JACKET.PNG"); ret = copyFile(path, "pfs3:/res/jkt_001.png");
    }
    ret = unmountPartition(in, ret);
    if (ret >= 0) { join(path, in->stage, "LIST.ICO"); ret = readFile(path, &list, &list_size, APP_MAX_ASSET); }
    if (ret >= 0) { join(path, in->stage, "DELETE.ICO"); ret = readFile(path, &del, &del_size, APP_MAX_ASSET); }
    snprintf(name, sizeof(name), "%s", part);
    if (ret >= 0) {
        snprintf(path, sizeof(path), "hdd0:%s", part);
        context(in, "Writing HDD launcher header", path);
        ret = InjectHddAppAssets(name, in->cnf, in->settings, list, list_size, del, del_size);
    }
    free(list); free(del); return ret;
}
static int stageAppFiles(AppInstall *in, char *next)
{
    char path[MAX_PATH];
    int ret = in->journal.update ? checkOwner(in, in->target, -1) : 0;
    if (ret >= 0) ret = mkdirChecked("ata0:/APPS");
    if (ret >= 0) ret = exists(next);
    if (ret == 1) ret = removeTree(next, 0);
    if (ret >= 0) ret = mkdirChecked(next);
    if (ret >= 0 && in->journal.update) ret = copyTree(in->target, next, 0);
    for (unsigned i = 0; ret >= 0 && i < in->save.count; i++) {
        SaveArchiveEntry *f = &in->save.files[i];
        dashWorkerStatus(f->name, i, in->save.count);
        ret = join(path, next, f->name);
        if (ret >= 0) ret = writeFile(path, f->data, f->size);
    }
    if (ret >= 0) ret = writeOwner(in, next, 1);
    return ret;
}
static void apply(void *arg)
{
    AppInstall *in = arg;
    in->committed = 0;
    context(in, "Checking app recovery state", in->stage);
    int ret = releasePfs();
    char next[MAX_PATH], old[MAX_PATH];
    int refresh_snapshot = 0;
    join(next, in->stage, "NEW"); join(old, in->stage, "OLD");
    if (ret < 0) goto out;
    if (in->resumed && in->journal.update && in->journal.phase >= HA_FILES && in->journal.phase <= HA_MOVE_OLD) {
        ret = exists(old);
        if (ret < 0) goto out;
        refresh_snapshot = ret == 0;
    }
    if (!refresh_snapshot && in->journal.phase >= HA_FILES && in->journal.phase <= HA_PUBLISH) {
        int present = exists(next);
        ret = present < 0 ? present : present ? checkOwner(in, next, 1) :
              (!in->journal.update || in->journal.phase == HA_PUBLISH) ? checkOwner(in, in->target, 1) : -ENOENT;
        if (ret < 0) goto out;
    }
    if (in->journal.update) {
        iox_stat_t stat;
        ret = existingPartition(in, &stat);
        if (ret >= 0) ret = partitionOwner(in, in->installed_partition, -1);
    }
    else if (in->journal.phase >= HA_PARTITION) {
        char named[40]; iox_stat_t stat;
        int present = existingPartition(in, &stat);
        if (present >= 0) ret = in->journal.phase >= HA_RENAME ? partitionOwner(in, in->installed_partition, 1) : -EEXIST;
        else if (present != -ENOENT) ret = present;
        else {
            snprintf(named, sizeof(named), "hdd0:%s", in->pending);
            present = fileXioGetStat(named, &stat);
            if (present >= 0) ret = partitionOwner(in, in->pending, 1);
            else if (present != -ENOENT) ret = present;
            else {
                /* A former false-success checkpoint can outlive a lost APA
                 * entry. Recreate only this unpublished launcher, preserving
                 * the verified app files in NEW or their published ATA folder. */
                context(in, "Recovering missing launcher partition", named);
                present = exists(next);
                ret = present < 0 ? present : checkOwner(in, present ? next : in->target, 1);
                if (ret >= 0) ret = hddCheckAllocation(8u * 2048);
                if (ret >= 0) ret = checkpoint(in, HA_CREATE);
            }
        }
    }
    if (ret < 0) goto out;
    if (in->journal.phase < HA_FILES || refresh_snapshot) {
        context(in, "Staging app files", next);
        ret = stageAppFiles(in, next);
        if (ret >= 0) ret = checkpoint(in, refresh_snapshot ? in->journal.phase : HA_FILES);
        if (ret < 0) goto out;
    }
    if (in->journal.phase < HA_CREATE) { ret = checkpoint(in, HA_CREATE); if (ret < 0) goto out; }
    if (in->journal.phase < HA_PARTITION) {
        context(in, in->journal.update ? "Keeping existing launcher partition" : "Creating 8 MiB launcher partition",
                in->journal.update ? in->installed_partition : in->pending);
        ret = in->journal.update ? partitionOwner(in, in->installed_partition, -1) : pendingPartition(in);
        if (ret >= 0) ret = checkpoint(in, HA_PARTITION);
        if (ret < 0) goto out;
    }
    if (in->journal.phase < HA_MOVE_OLD) { ret = checkpoint(in, HA_MOVE_OLD); if (ret < 0) goto out; }
    if (in->journal.phase < HA_PUBLISH) {
        context(in, "Preserving previous app files", in->target);
        if (in->journal.update) {
            int present = exists(old);
            ret = present < 0 ? present : present ? checkOwner(in, old, -1) : checkOwner(in, in->target, -1);
            if (!present && ret >= 0) ret = renameAbsent(in->target, old);
            if (ret < 0) goto out;
        }
        ret = checkpoint(in, HA_PUBLISH); if (ret < 0) goto out;
    }
    if (in->journal.phase < HA_HEADER) {
        context(in, "Publishing app files", in->target);
        int present = exists(in->target);
        ret = present < 0 ? present : present ? checkOwner(in, in->target, 1) : renameAbsent(next, in->target);
        if (ret >= 0) ret = checkpoint(in, HA_HEADER);
        if (ret < 0) goto out;
    }
    if (in->journal.phase < HA_RENAME) {
        context(in, "Installing HDD icon, artwork and PATINFO launcher", in->journal.update ? in->installed_partition : in->pending);
        ret = installHeader(in);
        if (ret >= 0) ret = checkpoint(in, HA_RENAME);
        if (ret < 0) goto out;
    }
    if (in->journal.phase < HA_DONE) {
        context(in, "Publishing launcher partition", in->app.partition);
        if (!in->journal.update) {
            char from[40], to[40]; iox_stat_t stat;
            snprintf(from, sizeof(from), "hdd0:%s", in->pending); snprintf(to, sizeof(to), "hdd0:%s", in->app.partition);
            int present = existingPartition(in, &stat);
            if (present == -ENOENT) ret = fileXioRename(from, to);
            else ret = present < 0 ? present : partitionOwner(in, in->installed_partition, 1);
        }
        if (ret >= 0) ret = checkpoint(in, HA_DONE);
        if (ret < 0) goto out;
    }
    /* Success only: backups/cache remain available throughout any failure. */
    in->committed = 1;
    dashWorkerStatus("Finishing app installation", 1, 1);
    ret = removeTree(in->stage, 0);
    if (ret < 0 && !hddInstallRecoveryFault()) { in->cleanup_error = ret; ret = 0; }
out:
    in->result = unmountPartition(in, ret);
}

static void appLoading(AppInstall *in, const char *title)
{
    char label[96];
    if (in->count > 1) snprintf(label, sizeof(label), "App %d of %d: %s", in->index, in->count, title);
    else snprintf(label, sizeof(label), "%s", title);
    dashLoadingBegin(label);
    dashLoadingFrame(in->source, 0, 0);
}
static int run(AppInstall *in, const char *title, void (*work)(void *))
{
    appLoading(in, title);
    in->metadata_error = 0;
    hddInstallRecoveryBegin();
    int id = dashWorkerSubmit(title, 0, work, NULL, in);
    if (id < 0) { hddInstallRecoveryEnd(); dashLoadingEnd(); in->result = -EBUSY; return 0; }
    int done = dashHddInstallAwait(id, title);
    hddInstallRecoveryObserve(in->result);
    int fault = hddInstallRecoveryFault(), reset = hddInstallRecoveryWasReset();
    hddInstallRecoveryEnd();
    if (fault || in->metadata_error) {
        /* A failed APA mutation retains its durable journal. Reinitializing
         * HDD replays it before a subsequent explicit installer retry. */
        dashLoadingFrame("Recovering storage drivers", 0, 0);
        if ((!reset && !resetInstallerIopTransport()) || !reloadInstallerCoreAfterReset()) dashHddInstallRecoveryFailed();
        in->mounted = 0;
        if (fault) in->result = HDD_INSTALL_ATA_TIMEOUT;
        memset(mountedParty, 0, sizeof(mountedParty)); latestMount = -1;
        for (int i = 0; i < MOUNT_LIMIT; i++) Party_vmcIndex[i] = -1;
        loadHddModules(); loadAtaModules();
        in->metadata_error = 0;
    }
    dashLoadingEnd();
    return done && in->result >= 0;
}

/* The source snapshot precedes network preparation, which may reset UDPFS. */
static int installApp(const char *psu_path, int index, int count)
{
    int result = 0;
    AppInstall *in = calloc(1, sizeof(*in));
    if (!in) { dashModalMessage(DI_WARNING, "App installation stopped", "Not enough memory to read the next PSU.", psu_path); return -1; }
    in->index = index; in->count = count;
    dashStrCopy(in->source, psu_path, sizeof(in->source));
    appLoading(in, "Opening app PSU");
    if (!dashWorkerAwaitAll("Finishing folder listing")) goto cleanup;
    context(in, "Opening app PSU device", in->source);
    if (!ensurePathDeviceStackReady(in->source)) { in->result = -ENODEV; goto error; }
    if (!run(in, "Reading app PSU", snapshot)) goto error;
    if (in->result > 0) {
        const char **names = malloc(in->app.elf_count * sizeof(*names));
        if (!names) { in->result = -ENOMEM; goto error; }
        for (unsigned i = 0; i < in->app.elf_count; i++) names[i] = in->save.files[in->app.elf[i]].name;
        int selected = dashModalChoice("Choose the app's boot ELF", names, in->app.elf_count, 0);
        free(names); if (selected < 0) goto cleanup;
        hddAppChooseBoot(&in->save, &in->app, selected);
    }
    appLoading(in, "Reading HTTPS certificate");
    if (!dashHttpsCaPath(in->network.ca_path, sizeof(in->network.ca_path)) || hddInstallLoadCa(&in->network) < 0) {
        dashLoadingEnd(); result = -1;
        dashModalMessage(DI_WARNING, "CA bundle unavailable", "Could not read WLE-UPDATE-CA.PEM beside Launchstation or in its settings folder.", NULL); goto cleanup;
    }
    dashSoundShutdown();
    in->network.jail = 1;
    context(in, "Preparing network and HDD drivers", "hdd0: / ata0:");
    dashLoadingFrame("Preparing network and HDD drivers", 0, 0);
    if ((in->result = dashHddInstallPrepareStorage(&in->network)) < 0) goto error;
    if (!run(in, "Downloading HDD app assets", prepare)) goto error;
    {
        DashTableRow rows[] = {
            {"App", in->app.title}, {"Boot ELF", in->app.boot}, {"App folder", in->target},
            {"Launcher partition", in->journal.update ? in->installed_partition : in->app.partition}, {"Allocation", in->journal.update ? "Keep existing size" : "8 MiB (free APA space only)"},
            {"HDD icon", in->icon_source}, {"Artwork", in->art_source},
            {"Operation", in->resumed ? "Resume interrupted installation" : in->journal.update ? "Update managed app; preserve extra files" : "Install new app"}
        };
        if (!dashModalConfirmTable("Review app installation", rows, sizeof(rows) / sizeof(rows[0]), in->resumed ? "Resume" : "Install", 0)) goto cleanup;
    }
    if (!run(in, "Installing HDD app", apply)) goto error;
    result = 1;
    filerSizeCacheInvalidate("ata0:/APPS/"); filerSizeCacheInvalidate("hdd0:/");
    if (count == 1 || in->cleanup_error) dashModalMessage(DI_CHECK, "App installed", in->app.title, in->cleanup_error ?
        "The launcher is ready. Temporary files could not be removed; the app is installed." : "The launcher is available in HOSDMenu / PSBBN.");
    goto cleanup;
error:
    dashLoadingEnd(); result = -1;
    {
        char message[512], detail[MAX_PATH + 160];
        if (in->committed) {
            result = 1;
            dashModalMessage(DI_WARNING, "App installed; cleanup interrupted", "The app and launcher were committed. Temporary files are retained.", in->stage);
            goto cleanup;
        }
        snprintf(message, sizeof(message), "%s (error %d). %s", in->operation ? in->operation : "Installation stopped", in->result,
                 in->network.error[0] ? in->network.error : "The operation failed; see the affected device or path below.");
        snprintf(detail, sizeof(detail), "%s%s%s", in->location,
            in->journal.sequence ? "\n" : "", in->journal.sequence ?
            "Staged files are retained. Select the same PSU, or its staged PACKAGE.PSU, to resume." : "");
        dashModalMessage(DI_WARNING, "App installation incomplete", message, detail[0] ? detail : NULL);
    }
cleanup:
    dashLoadingEnd();
    saveArchiveFree(&in->save); free(in->package); hddInstallRelease(&in->network); free(in);
    return result;
}
void dashHddInstallApp(const char *psu_path)
{
    char **paths = NULL;
    int count = 1, installed = 0, result = 0;
    if (!dashWorkerAwaitAll("Preparing app installer")) return;
    if (console_is_PSX) {
        dashModalMessage(DI_WARNING, "App installer unavailable", "This installer requires a PS2 APA Jail HDD with HOSDMenu.", NULL); return;
    }
    if (vmcMounted[0] || vmcMounted[1] || isFtpServerRunning() || isSmbServerRunning()) {
        dashModalMessage(DI_WARNING, "HDD busy", "Unmount VMCs and stop file servers before installing an app.", NULL); return;
    }
    if (!dashModalConfirm("Install apps to HDD", "Requires an Internet connection to download HDD icons and artwork from PSBBN DP. Requires APA Jail with HOSDMenu; app files go on ata0: and each launcher uses an 8 MiB APA partition.", "Continue", 0)) return;
    if (!psu_path) {
        count = dashPickFiles("Choose app PSUs", "", "PSU", &paths);
        if (!count) return;
    }
    for (int i = 0; i < count; i++) {
        result = installApp(paths ? paths[i] : psu_path, i + 1, count);
        if (result != 1) break;
        installed++;
    }
    if (count > 1) {
        char message[160];
        snprintf(message, sizeof(message), "%d of %d apps installed.%s", installed, count,
                 installed == count ? "" : " Queue stopped; remaining apps were not installed.");
        dashModalMessage(installed == count ? DI_CHECK : DI_WARNING,
                         installed == count ? "Apps installed" : result < 0 ? "App installation incomplete" : "App installation cancelled", message, NULL);
    }
    for (int i = 0; paths && i < count; i++) free(paths[i]);
    free(paths);
    dashSoundMaintain(); dashBgmStart();
}
#else
void dashHddInstallApp(const char *psu_path)
{
    (void)psu_path;
    dashModalMessage(DI_WARNING, "App installer unavailable", "This build requires networking, HTTPS and ATA filesystem support.", NULL);
}
#endif
