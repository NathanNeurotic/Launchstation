#include "launchelf.h"
#include "hdd_install.h"
#include "cloud_http.h"
#include "install_archive.h"
#include "save_archive.h"
#include "save_sign.h"
#include "hdd_header_injector.h"
#include "dash_internal.h"

#include <time.h>
#define EXTRAS_URL "https://archive.org/download/psbbn-definitive-english-patch-v2/extras.zip"
/* Immutable archive referenced by DP's HOSDMenu-Installer.sh. Recorded from
 * https://archive.org/metadata/psbbn-definitive-english-patch-v2 . */
#define EXTRAS_SHA1 "9e664b10e261b718a0c597363fc1e023601f92cf"
#define EXTRAS_SIZE 5034301u

static const char *const app_folders[] = {"APP_WLE-R3Z", "SYS_R3CONFIGURATOR"};
static const char *const app_boot[] = {"WLE-R3Z.ELF", "r3configurator.elf"};
static const char *const app_names[] = {"wLaunchELF-R3Z", "R3CONFIGURATOR"};
static const char *const app_partitions[] = {"PP.APP_WLE-R3Z", "PP.SYS_R3CONFIGURATOR"};

static int pathJoin(char *out, size_t capacity, const char *parent, const char *leaf)
{
    size_t n = strlen(parent);
    return snprintf(out, capacity, "%s%s%s", parent, n && parent[n - 1] == '/' ? "" : "/", leaf) < (int)capacity ? 0 : -ENAMETOOLONG;
}
static int readAll(int fd, void *data, unsigned size)
{
    unsigned done = 0;
    while (done < size) {
        int n = genRead(fd, (unsigned char *)data + done, size - done);
        if (n <= 0 || (unsigned)n > size - done) return n < 0 ? n : -EIO;
        done += n;
    }
    return 0;
}
static int writeAll(int fd, const void *data, unsigned size)
{
    unsigned done = 0;
    while (done < size) {
        int n = genWrite(fd, (void *)((const unsigned char *)data + done), size - done);
        if (n <= 0 || (unsigned)n > size - done) return n < 0 ? n : -EIO;
        done += n;
    }
    return 0;
}
static int closeResult(int fd, int result)
{
    int closed = genClose(fd);
    return result < 0 ? result : closed < 0 ? closed : 0;
}
static int checkHash(HddInstall *in, unsigned asset)
{
    unsigned char buffer[32768], digest[20];
    char path[MAX_PATH], hex[41], header[40];
    SaveSha1 hash;
    uint32_t done = 0;
    int fd, ret = 0;
    if (pathJoin(path, sizeof(path), in->stage, hddInstallAssetName(asset)) < 0) return -ENAMETOOLONG;
    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0) return fd;
    if (genLseek(fd, 0, SEEK_END) != in->sizes[asset] || genLseek(fd, 0, SEEK_SET) < 0) { genClose(fd); return -EIO; }
    saveHashInit(&hash);
    if (asset != HI_EXTRAS && asset != HI_SYSTEM && asset != HI_LANGUAGE) {
        int n = snprintf(header, sizeof(header), "blob %u", (unsigned)in->sizes[asset]);
        saveHashUpdate(&hash, (const uint8_t *)header, n + 1);
    }
    while (done < in->sizes[asset]) {
        unsigned n = in->sizes[asset] - done > sizeof(buffer) ? sizeof(buffer) : in->sizes[asset] - done;
        ret = readAll(fd, buffer, n);
        if (ret < 0) break;
        saveHashUpdate(&hash, buffer, n); done += n;
    }
    ret = closeResult(fd, ret);
    if (ret < 0) return ret;
    saveHashFinish(&hash, digest);
    for (unsigned i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return strcmp(hex, in->hashes[asset]) ? -EIO : 0;
}
static int safeLeaf(const char *name)
{
    if (!name || !*name || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
    for (; *name; name++) if ((unsigned char)*name < 32 || strchr("/\\:", *name)) return 0;
    return 1;
}
static int loadApp(HddInstall *in, unsigned index, SaveArchive *save, uint8_t **data)
{
    char path[MAX_PATH];
    int fd, ret, boot = 0, title = 0, icon = 0;
    unsigned asset = index ? HI_R3CONFIG : HI_WLE;
    memset(save, 0, sizeof(*save)); *data = NULL;
    if (pathJoin(path, sizeof(path), in->stage, hddInstallAssetName(asset)) < 0) return -ENAMETOOLONG;
    *data = malloc(in->sizes[asset]);
    if (!*data) return -ENOMEM;
    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0) { free(*data); *data = NULL; return fd; }
    ret = closeResult(fd, readAll(fd, *data, in->sizes[asset]));
    if (ret < 0 || !saveArchiveParse(save, *data, in->sizes[asset], path) ||
        save->ps1 || strcmp(save->folder.name, app_folders[index])) goto invalid;
    for (unsigned i = 0; i < save->count; i++) {
        SaveArchiveEntry *entry = &save->files[i];
        if (!safeLeaf(entry->name)) goto invalid;
        for (unsigned j = 0; j < i; j++) if (!strcmp(entry->name, save->files[j].name)) goto invalid;
        if (!strcmp(entry->name, app_boot[index]))
            boot = entry->size >= 52 && !memcmp(entry->data, "\x7f" "ELF\x01\x01", 6) && entry->data[18] == 8;
        if (!strcmp(entry->name, "title.cfg")) title = entry->size > 0 && entry->size <= 16384;
        if (!strcmp(entry->name, "list.icn")) icon = entry->size > 0 && entry->size < 1024 * 1024;
    }
    if (boot && title && icon) return 0;
invalid:
    saveArchiveFree(save); free(*data); *data = NULL;
    return ret < 0 ? ret : -EINVAL;
}

typedef struct { int input, output; } ZipContext;
static int zipRead(void *ctx, uint32_t offset, void *data, unsigned size)
{
    ZipContext *z = ctx;
    if (genLseek(z->input, offset, SEEK_SET) != offset) return -EIO;
    return readAll(z->input, data, size);
}
static int zipWrite(void *ctx, const void *data, unsigned size)
{
    ZipContext *z = ctx;
    return z->output < 0 ? 0 : writeAll(z->output, data, size);
}
static int osdResources(HddInstall *in, const char *destination)
{
    char path[MAX_PATH], entry[128];
    ZipContext ctx = {-1, -1};
    InstallArchiveIo io = {&ctx, zipRead, zipWrite};
    int ret;
    if (pathJoin(path, sizeof(path), in->stage, "extras.zip") < 0) return -ENAMETOOLONG;
    ctx.input = genOpen(path, FIO_O_RDONLY);
    if (ctx.input < 0) return ctx.input;
    for (unsigned i = 0; i < 7; i++) {
        if (destination) {
            if (pathJoin(path, sizeof(path), destination, hddInstallOsdFiles[i]) < 0) { ret = -ENAMETOOLONG; goto done; }
            ctx.output = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
            if (ctx.output < 0) { ret = ctx.output; goto done; }
        }
        snprintf(entry, sizeof(entry), "extras/%s", hddInstallOsdFiles[i]);
        dashWorkerStatus(destination ? "Installing HDD-OSD resources" : "Checking HDD-OSD resources", i, 7);
        ret = installZipExtract(&io, in->sizes[HI_EXTRAS], entry, 4u * 1024 * 1024, NULL);
        if (ctx.output >= 0) { ret = closeResult(ctx.output, ret); ctx.output = -1; }
        if (ret < 0) { if (destination) genRemove(path); goto done; }
    }
    ret = 0;
done:
    return closeResult(ctx.input, ret);
}

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
static const char *transfer_name;
static void transferProgress(int done, int total)
{
    dashWorkerStatus(transfer_name, done, total);
}
static int request(HddInstall *in, const char *path, CloudHttpResponse *response)
{
    const char headers[] = "User-Agent: Launchstation-PS2\r\nAccept: application/vnd.github+json\r\n";
    int ret;
    memset(response, 0, sizeof(*response));
    ret = cloudHttpRequest("GET", "api.github.com", path, headers, NULL, 0, in->ca_path,
                           response, in->error, sizeof(in->error));
    if (ret < 0 || response->status != 200 || !response->body) {
        if (ret >= 0) snprintf(in->error, sizeof(in->error), "GitHub returned HTTP %d. Try again later.", response->status);
        cloudHttpResponseFree(response); return ret < 0 ? ret : -EIO;
    }
    return 0;
}
int hddInstallDownload(HddInstall *in)
{
    CloudHttpResponse response;
    char path[256], url[512], local[MAX_PATH];
    int ret, fd, size;
    in->ready = 0; in->error[0] = 0;
    dashWorkerStatus("Reading PSBBN DP repository", 0, 0);
    ret = request(in, HDD_INSTALL_API "/git/ref/heads/main", &response);
    if (ret < 0) return ret;
    ret = hddInstallCommit(response.body, in->commit) ? 0 : -EINVAL;
    cloudHttpResponseFree(&response);
    if (ret < 0) goto invalid;
    /* Directory listings contain only metadata, avoiding GitHub's base64
     * file contents and keeping responses within the HTTPS client's limit. */
    for (unsigned directory = 0; directory < 3; directory++) {
        if (!(in->flags & (directory ? HDD_INSTALL_APPS : HDD_INSTALL_HOSD)) || (directory == 2 && !(in->flags & HDD_INSTALL_PSBBN))) continue;
        snprintf(path, sizeof(path), HDD_INSTALL_API "/contents/%s?ref=%s",
                 directory == 2 ? "icons/art" : directory ? "games/APPS" : "scripts/assets/osdmenu", in->commit);
        ret = request(in, path, &response);
        if (ret < 0) return ret;
        for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
            if (!hddInstallNeedsAsset(in->flags, asset) || !hddInstallAssetPath(asset) ||
                directory != (asset == HI_WLE_ART || asset == HI_R3_ART ? 2u : (unsigned)(asset == HI_WLE || asset == HI_R3CONFIG))) continue;
            if (!hddInstallAssetMetadata(response.body, hddInstallAssetName(asset), in->hashes[asset], &in->sizes[asset])) {
                ret = -EINVAL; break;
            }
        }
        cloudHttpResponseFree(&response);
        if (ret < 0) goto invalid;
    }
    if (in->flags & HDD_INSTALL_HOSD) {
        in->sizes[HI_EXTRAS] = EXTRAS_SIZE;
        strcpy(in->hashes[HI_EXTRAS], EXTRAS_SHA1);
    }
    if (in->flags & HDD_INSTALL_PSBBN) {
        memset(&response, 0, sizeof(response));
        ret = cloudHttpRequest("GET", "archive.org", "/metadata/psbbn-definitive-patch-v4.1", NULL, NULL, 0,
                               in->ca_path, &response, in->error, sizeof(in->error));
        if (ret >= 0 && response.status == 200 && response.body) {
            static const char *const prefixes[] = {"psbbn-definitive-patch-v", "language-pak-eng-v"};
            for (unsigned i = 0; i < 2; i++)
                if (!hddInstallArchiveMetadata(response.body, prefixes[i], in->archives[i],
                                               in->hashes[HI_SYSTEM + i], &in->sizes[HI_SYSTEM + i])) ret = -EINVAL;
        } else if (ret >= 0) ret = -EIO;
        cloudHttpResponseFree(&response);
        if (ret < 0) goto invalid;
    }
    for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
        if (!hddInstallNeedsAsset(in->flags, asset)) continue;
        if (pathJoin(local, sizeof(local), in->stage, hddInstallAssetName(asset)) < 0) { ret = -ENAMETOOLONG; goto invalid; }
        if (asset == HI_EXTRAS) snprintf(url, sizeof(url), "%s", EXTRAS_URL);
        else if (asset == HI_SYSTEM || asset == HI_LANGUAGE)
            snprintf(url, sizeof(url), "https://archive.org/download/psbbn-definitive-patch-v4.1/%s", in->archives[asset - HI_SYSTEM]);
        else snprintf(url, sizeof(url), "https://raw.githubusercontent.com/" HDD_INSTALL_REPOSITORY "/%s/%s", in->commit, hddInstallAssetPath(asset));
        fd = genOpen(local, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
        if (fd < 0) { ret = fd; goto invalid; }
        size = 0;
        transfer_name = hddInstallAssetName(asset);
        cloudHttpProgress = transferProgress;
        ret = cloudHttpDownloadFd(url, fd, in->ca_path, in->sizes[asset], &size, NULL,
                                  in->error, sizeof(in->error));
        cloudHttpProgress = NULL;
        ret = closeResult(fd, ret);
        if (ret < 0 || (uint32_t)size != in->sizes[asset]) { if (ret >= 0) ret = -EIO; goto invalid; }
        ret = checkHash(in, asset);
        if (ret < 0) goto invalid;
    }
    if (in->flags & HDD_INSTALL_APPS) for (unsigned i = 0; i < 2; i++) {
        SaveArchive save; uint8_t *data;
        ret = loadApp(in, i, &save, &data);
        if (ret < 0) goto invalid;
        saveArchiveFree(&save); free(data);
    }
    if (in->flags & HDD_INSTALL_HOSD) {
        unsigned char header[52];
        for (unsigned i = HI_MBR; i <= HI_HOSD; i++) {
            pathJoin(local, sizeof(local), in->stage, hddInstallAssetName(i));
            fd = genOpen(local, FIO_O_RDONLY);
            if (fd < 0) { ret = fd; goto invalid; }
            ret = closeResult(fd, readAll(fd, header, sizeof(header)));
            if (ret < 0) goto invalid;
            if ((i == HI_MBR && (in->sizes[i] < 512 || in->sizes[i] > 1024 * 1024 || in->sizes[i] % 512 ||
                                memcmp(header, "\x01\x00\x00\x04", 4))) ||
                (i == HI_HOSD && (memcmp(header, "\x7f" "ELF\x01\x01", 6) || header[18] != 8))) { ret = -EINVAL; goto invalid; }
        }
        if (!(in->flags & HDD_INSTALL_PSBBN)) {
            ret = osdResources(in, NULL);
            if (ret < 0) goto invalid;
        }
    }
    if (in->flags & HDD_INSTALL_PSBBN) {
        ret = hddInstallPsbbnPreflight(in);
        if (ret < 0) goto invalid;
    }
    in->ready = 1;
    return 0;
invalid:
    if (!in->error[0]) snprintf(in->error, sizeof(in->error), "Downloaded installer files failed verification (error %d). The HDD has not been changed.", ret);
    return ret;
}
#else
int hddInstallDownload(HddInstall *in)
{
    snprintf(in->error, sizeof(in->error), "This build does not include native HTTPS networking.");
    return -ENOSYS;
}
#endif

/* Identify our cache by contents, not a USB unit number that can change
 * across IOP resets. An ambiguous duplicate is never deleted or installed. */
int hddInstallClaimStage(HddInstall *in)
{
    char path[MAX_PATH];
    SaveSha1 hash;
    u64 ticks = Timer();
    time_t now = time(NULL);
    int fd, ret;
    if (in->owned) return -EEXIST;
    if (pathJoin(path, sizeof(path), in->stage, "owner.bin") < 0) return -ENAMETOOLONG;
    saveHashInit(&hash); saveHashUpdate(&hash, (const uint8_t *)&ticks, sizeof(ticks));
    saveHashUpdate(&hash, (const uint8_t *)&now, sizeof(now));
    saveHashUpdate(&hash, (const uint8_t *)in->stage, strlen(in->stage));
    saveHashFinish(&hash, in->owner);
    fd = genOpen(path, FIO_O_CREAT | FIO_O_EXCL | FIO_O_WRONLY);
    if (fd < 0) return fd;
    ret = closeResult(fd, writeAll(fd, in->owner, sizeof(in->owner)));
    if (ret < 0) { genRemove(path); return ret; }
    in->owned = 1; return 0;
}
static int stageMatches(HddInstall *in, const char *stage)
{
    char path[MAX_PATH];
    unsigned char owner[20];
    int fd, ret;
    if (pathJoin(path, sizeof(path), stage, "owner.bin") < 0) return 0;
    fd = genOpen(path, FIO_O_RDONLY); if (fd < 0) return 0;
    ret = genLseek(fd, 0, SEEK_END) == sizeof(owner) && genLseek(fd, 0, SEEK_SET) == 0 ? readAll(fd, owner, sizeof(owner)) : -EINVAL;
    ret = closeResult(fd, ret);
    return ret == 0 && !memcmp(owner, in->owner, sizeof(owner));
}
int hddInstallFindStage(HddInstall *in)
{
    char candidate[sizeof(in->stage)], found[sizeof(in->stage)];
    const char *tail;
    int matches = 0;
    if (!in->owned || !in->stage[0]) return -ENOENT;
    if (strncmp(in->stage, "usb", 3) || !(tail = strchr(in->stage, ':')))
        return stageMatches(in, in->stage) ? 0 : -ENOENT;
    for (unsigned unit = 0; unit < 10; unit++) {
        if (snprintf(candidate, sizeof(candidate), "usb%u%s", unit, tail) >= (int)sizeof(candidate)) return -ENAMETOOLONG;
        if (stageMatches(in, candidate)) {
            if (++matches > 1) return -EEXIST;
            strcpy(found, candidate);
        }
    }
    if (!matches) return -ENOENT;
    strcpy(in->stage, found); return 0;
}

void hddInstallCleanup(HddInstall *in)
{
    char path[MAX_PATH];
    if (hddInstallFindStage(in) < 0) return;
    for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
        if (pathJoin(path, sizeof(path), in->stage, hddInstallAssetName(asset)) == 0) genRemove(path);
    }
    if (pathJoin(path, sizeof(path), in->stage, "owner.bin") == 0) genRemove(path);
    snprintf(path, sizeof(path), "%s", in->stage);
    genRmdir(path);
    in->stage[0] = 0; in->owned = in->ready = 0;
}

static int makeDirectory(const char *path, int visible)
{
    iox_stat_t stat;
    int ret = genMkdir(path, 0777);
    if (ret < 0) {
        if (fileXioGetStat(path, &stat) < 0 || !FIO_S_ISDIR(stat.mode)) return ret;
    }
    if (!visible) return 0;
    memset(&stat, 0, sizeof(stat)); stat.attr = HDD_INSTALL_BROWSER_ATTR;
    return fileXioChStat(path, &stat, FIO_CST_ATTR);
}
static int writeNew(const char *path, const void *data, unsigned size)
{
    int fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL), ret;
    if (fd < 0) return fd;
    ret = closeResult(fd, writeAll(fd, data, size));
    if (ret < 0) { char remove[MAX_PATH]; snprintf(remove, sizeof(remove), "%s", path); genRemove(remove); }
    return ret;
}
static int copyAsset(HddInstall *in, unsigned asset, const char *destination)
{
    unsigned char *buffer;
    char source[MAX_PATH];
    uint32_t done = 0;
    int input, output, ret = 0;
    if (pathJoin(source, sizeof(source), in->stage, hddInstallAssetName(asset)) < 0) return -ENAMETOOLONG;
    buffer = malloc(32768);
    if (!buffer) return -ENOMEM;
    input = genOpen(source, FIO_O_RDONLY);
    if (input < 0) { free(buffer); return input; }
    output = genOpen(destination, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL);
    if (output < 0) { genClose(input); free(buffer); return output; }
    while (done < in->sizes[asset]) {
        unsigned n = in->sizes[asset] - done > 32768 ? 32768 : in->sizes[asset] - done;
        ret = readAll(input, buffer, n);
        if (ret >= 0) ret = writeAll(output, buffer, n);
        if (ret < 0) break;
        done += n; dashWorkerStatus(hddInstallAssetName(asset), done, in->sizes[asset]);
    }
    ret = closeResult(output, ret); ret = closeResult(input, ret); free(buffer);
    return ret;
}
static int installApps(HddInstall *in)
{
    const char *base = in->jail ? "ata0:/APPS" : "pfs2:/APPS";
    char folder[MAX_PATH], path[MAX_PATH];
    int ret = makeDirectory(base, !in->jail);
    if (ret < 0) return ret;
    for (unsigned index = 0; index < 2; index++) {
        SaveArchive save; uint8_t *data;
        ret = loadApp(in, index, &save, &data);
        if (ret < 0) return ret;
        ret = pathJoin(folder, sizeof(folder), base, save.folder.name);
        if (ret >= 0) ret = makeDirectory(folder, !in->jail);
        for (unsigned i = 0; ret >= 0 && i < save.count; i++) {
            ret = pathJoin(path, sizeof(path), folder, save.files[i].name);
            if (ret >= 0) ret = writeNew(path, save.files[i].data, save.files[i].size);
            dashWorkerStatus(app_names[index], i + 1, save.count);
        }
        if (ret >= 0 && in->jail) {
            char elf[MAX_PATH];
            const SaveArchiveEntry *icon = NULL;
            for (unsigned i = 0; i < save.count; i++) if (!strcmp(save.files[i].name, "list.icn")) icon = &save.files[i];
            ret = hddMakeFilesystem(128, (char *)app_partitions[index], FS_GROUP_APPLICATION);
            if (ret != 128) ret = ret < 0 ? ret : -ENOSPC;
            else if (!icon || pathJoin(elf, sizeof(elf), folder, app_boot[index]) < 0) ret = -EINVAL;
            else ret = InjectHddAppLauncher(app_partitions[index], elf, app_names[index], icon->data, icon->size);
            if (ret >= 0 && (in->flags & HDD_INSTALL_PSBBN)) {
                char partition[64], info[1024];
                int n, closed;
                snprintf(partition, sizeof(partition), "hdd0:%s", app_partitions[index]);
                ret = fileXioMount("pfs3:", partition, FIO_MT_RDWR);
                if (ret >= 0) {
                    ret = makeDirectory("pfs3:/res", 0);
                    n = snprintf(info, sizeof(info),
                        "title = [APP] %s\ntitle_id = %s\ntitle_sub_id = 0\nrelease_date = \n"
                        "developer_id = \npublisher_id = Homebrew\nnote = \ncontent_web = \n"
                        "image_topviewflag = 0\nimage_type = 0\nimage_count = 1\nimage_viewsec = 600\n"
                        "copyright_viewflag = 0\ncopyright_imgcount = 0\ngenre = \nparental_lock = 1\n"
                        "effective_date = 0\nexpire_date = 0\nviolence_flag = 0\ncontent_type = 255\ncontent_subtype = 0\n",
                        app_names[index], index ? "SYS-R3CONFIG" : "APP-WLER3Z");
                    if (ret >= 0) ret = writeNew("pfs3:/res/info.sys", info, n);
                    if (ret >= 0) ret = copyAsset(in, index ? HI_R3_ART : HI_WLE_ART, "pfs3:/res/jkt_001.png");
                    closed = fileXioUmount("pfs3:"); if (ret >= 0) ret = closed;
                }
            }
        }
        saveArchiveFree(&save); free(data);
        if (ret < 0) return ret;
    }
    return 0;
}

int hddInstallWriteConfigs(HddInstall *in)
{
    char config[4096];
    for (unsigned i = 0; i < 2; i++) {
        int ret = hddInstallConfig(in, i == 0, config, sizeof(config));
        if (ret >= 0) ret = writeNew(i ? "pfs1:/osdmenu/OSDMENU.CNF" : "pfs1:/osdmenu/OSDMBR.CNF", config, ret);
        if (ret < 0) return ret;
    }
    return 0;
}

int hddInstallApply(HddInstall *in)
{
    int ret = -EINVAL, mounts = 0;
    static const char *const partitions[] = {"hdd0:__system", "hdd0:__sysconf", "hdd0:__common"};
    static const char *const pfs[] = {"pfs0:", "pfs1:", "pfs2:"};
    if (!in->ready || !in->flags || (in->jail ? in->flags != (HDD_INSTALL_HOSD | HDD_INSTALL_APPS | HDD_INSTALL_PSBBN) :
        !!(in->flags & ~(HDD_INSTALL_HOSD | HDD_INSTALL_APPS)))) goto failed;
    ret = hddInstallFindStage(in);
    if (ret < 0) {
        snprintf(in->error, sizeof(in->error), "The temporary installer folder is %s after storage reload. The HDD is formatted; software installation has not started.",
                 ret == -EEXIST ? "duplicated" : "missing");
        return ret;
    }
    /* Recheck after the storage reset: a removable staging device must still
     * contain precisely the files that were checked before formatting. */
    for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
        if (!hddInstallNeedsAsset(in->flags, asset)) continue;
        ret = checkHash(in, asset);
        if (ret < 0) goto failed;
    }
    if (in->flags & HDD_INSTALL_PSBBN) {
        ret = hddInstallPsbbnPartitions(in);
        if (ret < 0) goto failed;
    }
    for (unsigned i = 0; i < 3; i++) {
        ret = fileXioMount(pfs[i], partitions[i], FIO_MT_RDWR);
        if (ret < 0) goto failed;
        mounts++;
    }
    if (in->flags & HDD_INSTALL_APPS) {
        ret = installApps(in);
        if (ret < 0) goto failed;
    }
    if (in->flags & HDD_INSTALL_HOSD) {
        const char *dirs[] = {"pfs0:/osdmenu", "pfs0:/osd100", "pfs1:/osdmenu"};
        for (unsigned i = 0; i < 3; i++) { ret = makeDirectory(dirs[i], 0); if (ret < 0) goto failed; }
        ret = copyAsset(in, HI_HOSD, "pfs0:/osdmenu/hosdmenu.elf");
        if (ret >= 0) ret = copyAsset(in, HI_VERSION, "pfs0:/osdmenu/version.txt");
        if (ret >= 0 && !(in->flags & HDD_INSTALL_PSBBN)) ret = osdResources(in, "pfs0:/osd100");
        if (ret < 0) goto failed;
        if (!(in->flags & HDD_INSTALL_PSBBN)) {
            ret = hddInstallWriteConfigs(in);
            if (ret < 0) goto failed;
        }
    }
    ret = 0;
failed:
    while (mounts) {
        int closed = fileXioUmount(pfs[--mounts]);
        if (ret >= 0 && closed < 0) ret = closed;
    }
    if (ret < 0) snprintf(in->error, sizeof(in->error), "Software installation failed (error %d). The HDD is formatted, but installation is incomplete.", ret);
    return ret;
}
