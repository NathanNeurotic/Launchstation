#include "launchelf.h"
#include "hdd_install.h"
#include "cloud_http.h"
#include "install_archive.h"
#include "save_archive.h"
#include "save_sign.h"
#include "hdd_header_injector.h"
#include "dash_internal.h"
#include "hdd_install_recovery.h"
#include "hdd_install_resume.h"

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
    hddInstallRecoveryObserve(result);
    if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    int closed = genClose(fd);
    hddInstallRecoveryObserve(closed);
    return result < 0 ? result : closed < 0 ? closed : 0;
}
void hddInstallRelease(HddInstall *in)
{
    free(in->ca_data); in->ca_data = NULL; in->ca_size = 0;
    free(in->launcher_files); in->launcher_files = NULL; in->launcher_count = 0;
}
int hddInstallLoadCa(HddInstall *in)
{
    s64 size;
    int fd, ret;
    free(in->ca_data); in->ca_data = NULL; in->ca_size = 0;
    hddInstallLog("Reading CA bundle: %s", in->ca_path);
    fd = genOpen(in->ca_path, FIO_O_RDONLY);
    if (fd < 0) return fd;
    size = genLseek(fd, 0, SEEK_END);
    if (size <= 0 || size > 256 * 1024 || genLseek(fd, 0, SEEK_SET) != 0) return closeResult(fd, -EINVAL);
    in->ca_data = malloc((unsigned)size + 1);
    if (!in->ca_data) return closeResult(fd, -ENOMEM);
    ret = closeResult(fd, readAll(fd, in->ca_data, (unsigned)size));
    if (ret < 0) { free(in->ca_data); in->ca_data = NULL; return ret; }
    in->ca_data[size] = 0;
    in->ca_size = (unsigned)size + 1;
    hddInstallLog("CA snapshot ready: %u bytes in EE memory", in->ca_size);
    return 0;
}
int hddInstallMountStage(HddInstall *in)
{ return in->jail ? 0 : fileXioMount("pfs2:", "hdd0:__common", FIO_MT_RDWR); }
int hddInstallUnmountStage(HddInstall *in)
{ return hddInstallRecoveryFault() ? HDD_INSTALL_ATA_TIMEOUT : in->jail ? 0 : fileXioUmount("pfs2:"); }
int hddInstallCreateStage(HddInstall *in)
{
    int ret;

    if (!in->ca_data || !in->ca_size || !in->stage[0]) return -EINVAL;
    if (in->owned) {
        ret = hddInstallFindStage(in);
        if (ret < 0) return ret;
    } else {
        ret = genMkdir(in->stage, 0777);
        /* A timed-out claim may have left a partial owner marker behind.
         * Keep any existing directory and claim a separate one. */
        for (unsigned slot = 1; ret == -EEXIST && in->recovery_attempts && slot <= HDD_INSTALL_RECOVERY_LIMIT; slot++) {
            snprintf(in->stage, sizeof(in->stage), "%s/PSBBN_DP_TEMP_%u", in->jail ? "ata0:" : "pfs2:", slot);
            ret = genMkdir(in->stage, 0777);
        }
        hddInstallRecoveryObserve(ret);
        if (ret < 0) return ret;
        ret = hddInstallClaimStage(in);
        hddInstallRecoveryObserve(ret);
        if (ret < 0) {
            if (!hddInstallRecoveryFault()) genRmdir(in->stage);
            return ret;
        }
    }
    in->stage_ready = 1;
    return hddInstallResumeSave(in);
}

static int checkStageHash(const HddInstall *in, const char *name, uint32_t size,
                          const char *expected, int raw)
{
    unsigned char buffer[32768] __attribute__((aligned(64))), digest[20];
    char path[MAX_PATH], hex[41], header[40];
    SaveSha1 hash;
    uint32_t done = 0;
    int fd, ret = 0;
    if (pathJoin(path, sizeof(path), in->stage, name) < 0) return -ENAMETOOLONG;
    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0) return fd;
    s64 length = genLseek(fd, 0, SEEK_END);
    if (length < 0 || length != size) return closeResult(fd, length < 0 ? (int)length : -EIO);
    s64 offset = genLseek(fd, 0, SEEK_SET);
    if (offset != 0) return closeResult(fd, offset < 0 ? (int)offset : -EIO);
    saveHashInit(&hash);
    if (!raw) {
        int n = snprintf(header, sizeof(header), "blob %u", (unsigned)size);
        saveHashUpdate(&hash, (const uint8_t *)header, n + 1);
    }
    while (done < size) {
        unsigned n = size - done > sizeof(buffer) ? sizeof(buffer) : size - done;
        ret = readAll(fd, buffer, n);
        if (ret < 0) break;
        saveHashUpdate(&hash, buffer, n); done += n;
    }
    ret = closeResult(fd, ret);
    if (ret < 0) return ret;
    saveHashFinish(&hash, digest);
    for (unsigned i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return strcmp(hex, expected) ? -EIO : 0;
}
static int checkHash(HddInstall *in, unsigned asset)
{
    int raw = asset == HI_EXTRAS || asset == HI_SYSTEM || asset == HI_LANGUAGE ||
              asset == HI_PS2LINUX || asset == HI_CHANNELS;

    return checkStageHash(in, hddInstallAssetName(asset), in->sizes[asset], in->hashes[asset], raw);
}
static void launcherStageName(unsigned index, char name[32])
{ snprintf(name, 32, "launcher-%03u.bin", index); }
static int findLauncher(const HddInstall *in, const char *path)
{
    for (unsigned i = 0; i < in->launcher_count; i++) if (!strcmp(in->launcher_files[i].path, path)) return (int)i;
    return -ENOENT;
}
static const char *launcherIcon(const HddInstall *in)
{ return in->game_launcher == HDD_GAME_NEUTRINO ? "icons/ico/APP_NHDDL_LST.ico" : "icons/ico/APP_OPL_LST.ico"; }
static const char *launcherArt(const HddInstall *in)
{ return in->game_launcher == HDD_GAME_NEUTRINO ? "icons/art/APP_NHDDL.png" : "icons/art/APP_OPL.png"; }
static const char *const launcher_required[] = {
        "scripts/assets/OPL/OPNPS2LD.ELF", "scripts/assets/NHDDL/nhddl.elf", "scripts/assets/neutrino/neutrino.elf",
        "scripts/assets/neutrino/config/system.toml", "scripts/assets/neutrino/version.txt",
        "scripts/assets/OPL/version.txt", "scripts/assets/NHDDL/version.txt",
        "scripts/licenses/OPL", "scripts/licenses/nhddl", "scripts/licenses/neutrino"
};
static int launcherDestination(const char *source, char target[MAX_PATH])
{
    if (!strcmp(source, "scripts/assets/OPL/OPNPS2LD.ELF")) strcpy(target, "pfs0:/launcher/OPNPS2LD.ELF");
    else if (!strcmp(source, "scripts/assets/NHDDL/nhddl.elf")) strcpy(target, "pfs0:/launcher/nhddl.elf");
    else if (!strcmp(source, "scripts/assets/OPL/version.txt")) strcpy(target, "pfs0:/launcher/OPL-version.txt");
    else if (!strcmp(source, "scripts/assets/NHDDL/version.txt")) strcpy(target, "pfs0:/launcher/NHDDL-version.txt");
    else if (!strncmp(source, "scripts/assets/OPL/LNG/", sizeof("scripts/assets/OPL/LNG/") - 1)) snprintf(target, MAX_PATH, "ata0:/LNG/%s", source + sizeof("scripts/assets/OPL/LNG/") - 1);
    else if (!strncmp(source, "scripts/assets/neutrino/", sizeof("scripts/assets/neutrino/") - 1)) snprintf(target, MAX_PATH, "ata0:/neutrino/%s", source + sizeof("scripts/assets/neutrino/") - 1);
    else if (!strncmp(source, "scripts/licenses/", 17)) snprintf(target, MAX_PATH, "pfs0:/launcher/licenses/%s", source + 17);
    else return -EINVAL;
    return 0;
}
static int launcherManifest(HddInstall *in)
{
    if (!in->launcher_files || !in->launcher_count || in->launcher_count > HDD_INSTALL_LAUNCHER_FILES ||
        in->game_launcher > HDD_GAME_NEUTRINO) return -EINVAL;
    for (unsigned i = 0; i < sizeof(launcher_required) / sizeof(launcher_required[0]); i++) {
        int index = findLauncher(in, launcher_required[i]);
        if (index < 0 || !in->launcher_files[index].size) {
            hddInstallLog("Missing/empty launcher asset: %s", launcher_required[i]); return -ENOENT;
        }
    }
    if (findLauncher(in, launcherIcon(in)) < 0 || findLauncher(in, launcherArt(in)) < 0) return -ENOENT;
    for (unsigned i = 0; i < in->launcher_count; i++) {
        char target[MAX_PATH];
        if (strncmp(in->launcher_files[i].path, "icons/", 6) && launcherDestination(in->launcher_files[i].path, target) < 0) {
            hddInstallLog("Unmapped launcher asset: %s", in->launcher_files[i].path); return -EINVAL;
        }
    }
    return 0;
}
static int checkLaunchers(HddInstall *in)
{
    int ready;
    if (!(in->flags & HDD_INSTALL_PSBBN)) return 0;
    ready = launcherManifest(in); if (ready < 0) return ready;
    for (unsigned i = 0; i < in->launcher_count; i++) {
        char name[32]; int ret;
        launcherStageName(i, name);
        ret = checkStageHash(in, name, in->launcher_files[i].size, in->launcher_files[i].hash, 0);
        if (ret < 0) return ret;
        if (!strcmp(in->launcher_files[i].path, launcher_required[0]) || !strcmp(in->launcher_files[i].path, launcher_required[1]) ||
            !strcmp(in->launcher_files[i].path, launcher_required[2])) {
            char path[MAX_PATH]; unsigned char header[52]; int fd;
            if (in->launcher_files[i].size < sizeof(header) || pathJoin(path, sizeof(path), in->stage, name) < 0) return -EINVAL;
            fd = genOpen(path, FIO_O_RDONLY); if (fd < 0) return fd;
            ret = closeResult(fd, readAll(fd, header, sizeof(header)));
            if (ret < 0) return ret;
            if (memcmp(header, "\x7f" "ELF\x01\x01", 6) || header[18] != 8) return -EINVAL;
        }
    }
    return 0;
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
    ret = checkHash(in, asset);
    if (ret < 0) return ret;
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
        for (unsigned j = 0; j < i; j++)
            if ((!in->jail && !strcmp(entry->name, save->files[j].name)) ||
                (in->jail && !stricmp(entry->name, save->files[j].name))) goto invalid;
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
    ret = checkHash(in, HI_EXTRAS);
    if (ret < 0) return ret;
    if (pathJoin(path, sizeof(path), in->stage, "extras.zip") < 0) return -ENAMETOOLONG;
    ctx.input = genOpen(path, FIO_O_RDONLY);
    if (ctx.input < 0) return ctx.input;
    for (unsigned i = 0; i < 7; i++) {
        if (destination) {
            if (pathJoin(path, sizeof(path), destination, hddInstallOsdFiles[i]) < 0) { ret = -ENAMETOOLONG; goto done; }
            ctx.output = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | (in->owned && (in->recovery_attempts || in->resumed) ? FIO_O_TRUNC : FIO_O_EXCL));
            if (ctx.output < 0) { ret = ctx.output; goto done; }
        }
        snprintf(entry, sizeof(entry), "extras/%s", hddInstallOsdFiles[i]);
        hddInstallStatus(destination ? "Installing HDD-OSD resources" : "Checking HDD-OSD resources", i, 7);
        ret = installZipExtract(&io, in->sizes[HI_EXTRAS], entry, 4u * 1024 * 1024, NULL);
        if (ctx.output >= 0) { ret = closeResult(ctx.output, ret); ctx.output = -1; }
        if (ret < 0) { if (destination) genRemove(path); goto done; }
    }
    ret = 0;
done:
    return closeResult(ctx.input, ret);
}

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
typedef struct { const char *name; uint64_t completed, total; unsigned expected; } InstallTransfer;
static uint64_t downloadBytes(const HddInstall *in)
{
    uint64_t total = 0;
    for (unsigned i = 0; i < HDD_INSTALL_ASSETS; i++) if (hddInstallSelectedAsset(in, i)) total += in->sizes[i];
    for (unsigned i = 0; i < in->launcher_count; i++) total += in->launcher_files[i].size;
    return total;
}
static void transferProgress(int done, int total, void *context)
{
    InstallTransfer *transfer = context;
    (void)total; /* Repository metadata supplies a size even for chunked HTTP. */
    hddInstallProgress(transfer->name, done, transfer->expected);
    hddInstallOverall(transfer->completed + (unsigned)done, transfer->total);
}
static int request(HddInstall *in, const char *path, CloudHttpResponse *response)
{
    const char headers[] = "User-Agent: Launchstation-PS2\r\nAccept: application/vnd.github+json\r\n";
    int ret;
    memset(response, 0, sizeof(*response));
    hddInstallLog("GET https://api.github.com%s", path);
    hddInstallLogFlush(in);
    ret = cloudHttpRequest("GET", "api.github.com", path, headers, NULL, 0, in->ca_path,
                       response, in->error, sizeof(in->error));
    hddInstallLog("GitHub response: rc=%d HTTP=%d body=%d bytes", ret, response->status, response->body_len);
    hddInstallLogFlush(in);
    if (ret < 0 || response->status != 200 || !response->body) {
        if (ret >= 0) snprintf(in->error, sizeof(in->error), "GitHub returned HTTP %d. Try again later.", response->status);
        cloudHttpResponseFree(response); return ret < 0 ? ret : -EIO;
    }
    return 0;
}
static int upstreamText(HddInstall *in, const char *file, CloudHttpResponse *response)
{
    char path[256]; int ret;
    memset(response, 0, sizeof(*response));
    snprintf(path, sizeof(path), "/" HDD_INSTALL_REPOSITORY "/%s/%s", in->commit, file);
    ret = cloudHttpRequest("GET", "raw.githubusercontent.com", path, NULL, NULL, 0,
                       in->ca_path, response, in->error, sizeof(in->error));
    if (ret < 0 || response->status != 200 || !response->body) {
        cloudHttpResponseFree(response); return ret < 0 ? ret : -EIO;
    }
    return 0;
}
static int archiveMetadata(HddInstall *in, CloudHttpResponse *response)
{
    char path[128]; int ret;
    memset(response, 0, sizeof(*response));
    if (!in->archive_item[0]) return -EINVAL;
    snprintf(path, sizeof(path), "/metadata/%s", in->archive_item);
    hddInstallLog("GET https://archive.org%s", path);
    ret = cloudHttpRequest("GET", "archive.org", path, NULL, NULL, 0,
                       in->ca_path, response, in->error, sizeof(in->error));
    if (ret < 0 || response->status != 200 || !response->body) {
        cloudHttpResponseFree(response); return ret < 0 ? ret : -EIO;
    }
    return 0;
}
int hddInstallLanguageChoices(HddInstall *in, HddInstallLanguage *languages, unsigned capacity)
{
    CloudHttpResponse response = {0}, script = {0}, english = {0};
    char prefix[32], name[128], hash[41]; uint32_t size;
    int ret, count, available = 0;
    /* The CA source can have been unmounted by network preparation. */
    cloudHttpSetCaBundle(in->ca_data, in->ca_size);
    hddInstallStatus("Reading supported PSBBN languages", 0, 0);
    ret = request(in, HDD_INSTALL_API "/git/ref/heads/main", &response);
    if (ret >= 0 && !hddInstallCommit(response.body, in->commit)) ret = -EINVAL;
    cloudHttpResponseFree(&response);
    if (ret >= 0) ret = upstreamText(in, "scripts/PSBBN-Installer.sh", &script);
    if (ret >= 0) ret = upstreamText(in, "scripts/assets/lang/eng.txt", &english);
    if (ret >= 0 && !hddInstallArchiveItem(script.body, in->archive_item)) ret = -EINVAL;
    if (ret >= 0) ret = hddInstallLanguages(script.body, english.body, languages, capacity);
    count = ret;
    cloudHttpResponseFree(&script); cloudHttpResponseFree(&english);
    if (ret >= 0) ret = archiveMetadata(in, &response);
    if (ret >= 0) {
        for (int i = 0; i < count; i++) {
            snprintf(prefix, sizeof(prefix), "language-pak-%s-v", languages[i].code);
            if (!hddInstallArchiveMetadata(response.body, prefix, name, hash, &size)) continue;
            if (!strcmp(languages[i].code, "jpn") &&
                !hddInstallArchiveMetadata(response.body, "channels-jpn-v", name, hash, &size)) continue;
            languages[available++] = languages[i];
        }
        ret = available ? available : -ENOENT;
    }
    cloudHttpResponseFree(&response);
    if (ret < 0 && !in->error[0]) snprintf(in->error, sizeof(in->error), "Could not read supported PSBBN languages (error %d). The HDD has not been changed.", ret);
    cloudHttpSetCaBundle(NULL, 0);
    return ret;
}
typedef struct {
    HddInstall *in;
    char dirs[16][192];
    unsigned count, current;
    uint32_t bytes;
} LauncherListing;
static int collectLauncher(void *context, const char *name, const char *hash, uint32_t size, int directory)
{
    LauncherListing *list = context;
    HddInstall *in = list->in;
    HddInstallFile *file;
    char path[192];
    const char *parent = list->dirs[list->current];
    if (name[0] == '.') return 0; /* Same hidden-file exclusion as DP's rsync. */
    if (snprintf(path, sizeof(path), "%s/%s", parent, name) >= (int)sizeof(path)) return -ENAMETOOLONG;
    if (!strncmp(parent, "icons/", 6)) {
        if (directory || (strcmp(path, launcherIcon(in)) && strcmp(path, launcherArt(in)))) return 0;
    } else if (!strcmp(parent, "scripts/licenses")) {
        if (directory || (strcmp(name, "OPL") && strcmp(name, "neutrino") && strcmp(name, "nhddl"))) return 0;
    }
    /* These commit-pinned runtime trees use URL-safe names. Never follow
     * server-provided download URLs, symlinks or arbitrary repository roots. */
    if (strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != strlen(name)) return -EINVAL;
    if (directory) {
        if (list->count >= 16) return -E2BIG;
        for (unsigned i = 0; i < list->count; i++) if (!strcmp(list->dirs[i], path)) return -EINVAL;
        strcpy(list->dirs[list->count++], path); return 0;
    }
    if (in->launcher_count >= HDD_INSTALL_LAUNCHER_FILES || list->bytes > 64u * 1024 * 1024 - size ||
        findLauncher(in, path) >= 0) return -EINVAL;
    file = &in->launcher_files[in->launcher_count++];
    strcpy(file->path, path); strcpy(file->hash, hash); file->size = size; list->bytes += size;
    return 0;
}
static int launcherMetadata(HddInstall *in)
{
    LauncherListing list = {.in = in, .count = 6};
    static const char *const roots[] = {"scripts/assets/OPL", "scripts/assets/NHDDL", "scripts/assets/neutrino",
                                       "icons/art", "icons/ico", "scripts/licenses"};
    free(in->launcher_files); in->launcher_count = 0;
    in->launcher_files = calloc(HDD_INSTALL_LAUNCHER_FILES, sizeof(*in->launcher_files));
    if (!in->launcher_files) return -ENOMEM;
    for (unsigned i = 0; i < 6; i++) strcpy(list.dirs[i], roots[i]);
    for (; list.current < list.count; list.current++) {
        CloudHttpResponse response; char path[320]; int ret;
        snprintf(path, sizeof(path), HDD_INSTALL_API "/contents/%s?ref=%s", list.dirs[list.current], in->commit);
        ret = request(in, path, &response); if (ret < 0) return ret;
        ret = hddInstallDirectory(response.body, collectLauncher, &list);
        cloudHttpResponseFree(&response);
        if (ret < 0) return ret;
    }
    hddInstallLog("Launcher manifest: %u files, %u bytes; default %s", in->launcher_count, (unsigned)list.bytes, hddInstallGameName(in->game_launcher));
    return launcherManifest(in);
}
static int downloadLaunchers(HddInstall *in, const char **phase, uint64_t completed, uint64_t total)
{
    for (unsigned i = 0; i < in->launcher_count; i++) {
        HddInstallFile *file = &in->launcher_files[i];
        char name[32], local[MAX_PATH], url[512];
        int fd, size = 0, ret;
        launcherStageName(i, name);
        if (pathJoin(local, sizeof(local), in->stage, name) < 0) return -ENAMETOOLONG;
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/" HDD_INSTALL_REPOSITORY "/%s/%s", in->commit, file->path);
        if (i < in->downloaded_launchers && checkStageHash(in, name, file->size, file->hash, 0) == 0) {
            completed += file->size; hddInstallOverall(completed, total); continue;
        }
        if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
        if (i < in->downloaded_launchers) in->downloaded_launchers = i;
        fd = genOpen(local, FIO_O_WRONLY | FIO_O_CREAT | ((in->recovery_attempts || in->resumed) ? FIO_O_TRUNC : FIO_O_EXCL)); if (fd < 0) return fd;
        *phase = file->path;
        hddInstallLog("LAUNCHER download %u/%u: %s (%u bytes)", i + 1, in->launcher_count, file->path, (unsigned)file->size);
        hddInstallLogFlush(in);
        ret = 0;
        if (file->size) { InstallTransfer transfer = {file->path, completed, total, file->size};
          ret = cloudHttpDownloadAlignedFd(url, fd, in->ca_path, file->size, &size, NULL,
                                transferProgress, &transfer, in->error, sizeof(in->error)); }
        ret = closeResult(fd, ret);
        if (ret < 0 || (uint32_t)size != file->size) return ret < 0 ? ret : -EIO;
        in->downloaded_launchers = i + 1;
        completed += file->size;
    }
    return checkLaunchers(in);
}
static void installerHttpsStatus(const char *status, void *context)
{
    const char *const *phase = context;
    char text[128];
    snprintf(text, sizeof(text), "%.48s: %.72s", *phase, status);
    hddInstallProgress(text, 0, 0);
}
static int resolveInstaller(HddInstall *in, const char **phase)
{
    CloudHttpResponse response;
    char path[256];
    int ret;
    in->ready = 0; in->error[0] = 0;
    hddInstallStatus("Reading PSBBN DP repository", 0, 0);
    ret = 0;
    if (!in->commit[0]) {
        ret = request(in, HDD_INSTALL_API "/git/ref/heads/main", &response);
        if (ret < 0) return ret;
        ret = hddInstallCommit(response.body, in->commit) ? 0 : -EINVAL;
        cloudHttpResponseFree(&response);
    }
    hddInstallLog("Pinned PSBBN DP Git commit: %s rc=%d", in->commit, ret);
    if (ret < 0) goto invalid;
    /* Directory listings contain only metadata, avoiding GitHub's base64
     * file contents and keeping responses within the HTTPS client's limit. */
    for (unsigned directory = 0; directory < 4; directory++) {
        if (!(in->flags & (directory ? HDD_INSTALL_APPS : HDD_INSTALL_HOSD)) || (directory == 2 && !(in->flags & HDD_INSTALL_PSBBN)) || (directory == 3 && !(in->flags & HDD_INSTALL_PS2LINUX) && !hddInstallSelectedAsset(in, HI_KERNEL_JPN))) continue;
        snprintf(path, sizeof(path), HDD_INSTALL_API "/contents/%s?ref=%s",
                 directory == 3 ? "scripts/assets/kernel" : directory == 2 ? "icons/art" : directory ? "games/APPS" : "scripts/assets/osdmenu", in->commit);
        *phase = directory == 3 ? "PS2 Linux kernels" : directory ? "App metadata" : "OSDMenu metadata";
        ret = request(in, path, &response);
        if (ret < 0) return ret;
        for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
            if (!hddInstallSelectedAsset(in, asset) || !hddInstallAssetPath(asset) ||
                directory != (asset >= HI_KERNEL_NTSC ? 3u : asset == HI_WLE_ART || asset == HI_R3_ART ? 2u : (unsigned)(asset == HI_WLE || asset == HI_R3CONFIG))) continue;
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
        *phase = "OPL / Neutrino package metadata";
        ret = launcherMetadata(in);
        if (ret < 0) goto invalid;
    }
    if (in->flags & HDD_INSTALL_PSBBN) {
        *phase = "PSBBN release metadata";
        ret = archiveMetadata(in, &response);
        if (ret >= 0) {
            char language_prefix[32];
            const char *prefixes[] = {"psbbn-definitive-patch-v", language_prefix};
            snprintf(language_prefix, sizeof(language_prefix), "language-pak-%s-v", in->language.code);
            for (unsigned i = 0; i < 2; i++)
                if (!hddInstallArchiveMetadata(response.body, prefixes[i], in->archives[i],
                                               in->hashes[HI_SYSTEM + i], &in->sizes[HI_SYSTEM + i])) ret = -EINVAL;
            if ((in->flags & HDD_INSTALL_PS2LINUX) && !hddInstallLinuxMetadata(response.body,
                    in->hashes[HI_PS2LINUX], &in->sizes[HI_PS2LINUX])) ret = -EINVAL;
            if (hddInstallSelectedAsset(in, HI_CHANNELS) && !hddInstallArchiveMetadata(response.body,
                    "channels-jpn-v", in->channels, in->hashes[HI_CHANNELS], &in->sizes[HI_CHANNELS])) ret = -EINVAL;
        }
        cloudHttpResponseFree(&response);
        if (ret < 0) goto invalid;
    }
    in->download_bytes = downloadBytes(in);
    in->manifest_ready = in->download_bytes != 0;
    return in->download_bytes ? 0 : -EINVAL;
invalid:
    if (!in->error[0])
        snprintf(in->error, sizeof(in->error), "Could not resolve installer files (error %d). The HDD has not been changed.", ret);
    return ret;
}

int hddInstallResolve(HddInstall *in)
{
    const char *phase = "PSBBN DP repository";
    int ret;

    cloudHttpSetCaBundle(in->ca_data, in->ca_size);
    cloudHttpSetStatusCallback(installerHttpsStatus, &phase);
    ret = resolveInstaller(in, &phase);
    cloudHttpSetStatusCallback(NULL, NULL);
    cloudHttpSetCaBundle(NULL, 0);
    return ret;
}

static int downloadInstaller(HddInstall *in, const char **phase)
{
    char url[512], local[MAX_PATH];
    int ret, fd, size;
    uint64_t downloaded = 0, download_total = downloadBytes(in);

    if (!in->manifest_ready || !download_total) return -EINVAL;
    for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
        if (!hddInstallSelectedAsset(in, asset)) continue;
        if (pathJoin(local, sizeof(local), in->stage, hddInstallAssetName(asset)) < 0) { ret = -ENAMETOOLONG; goto invalid; }
        if ((in->downloaded_assets & (1u << asset)) && checkHash(in, asset) == 0) {
            downloaded += in->sizes[asset]; hddInstallOverall(downloaded, download_total); continue;
        }
        if (hddInstallRecoveryFault()) { ret = HDD_INSTALL_ATA_TIMEOUT; goto invalid; }
        in->downloaded_assets &= ~(1u << asset);
        if (asset == HI_EXTRAS) snprintf(url, sizeof(url), "%s", EXTRAS_URL);
        else if (asset == HI_PS2LINUX)
            snprintf(url, sizeof(url), "https://archive.org/download/%s/PS2Linux.tar.gz", in->archive_item);
        else if (asset == HI_SYSTEM || asset == HI_LANGUAGE || asset == HI_CHANNELS)
            snprintf(url, sizeof(url), "https://archive.org/download/%s/%s", in->archive_item,
                     asset == HI_CHANNELS ? in->channels : in->archives[asset - HI_SYSTEM]);
        else snprintf(url, sizeof(url), "https://raw.githubusercontent.com/" HDD_INSTALL_REPOSITORY "/%s/%s", in->commit, hddInstallAssetPath(asset));
        fd = genOpen(local, FIO_O_WRONLY | FIO_O_CREAT | ((in->recovery_attempts || in->resumed) ? FIO_O_TRUNC : FIO_O_EXCL));
        if (fd < 0) { ret = fd; goto invalid; }
        size = 0; *phase = hddInstallAssetName(asset);
        hddInstallLog("DOWNLOAD %u/%u: %s", asset + 1, (unsigned)HDD_INSTALL_ASSETS, *phase);
        hddInstallLog("URL: %s", url);
        hddInstallLog("Destination: %s; expected=%u bytes", local, (unsigned)in->sizes[asset]);
        hddInstallLogFlush(in);
        {
            InstallTransfer transfer = {hddInstallAssetName(asset), downloaded, download_total, in->sizes[asset]};
            ret = cloudHttpDownloadAlignedFd(url, fd, in->ca_path, in->sizes[asset], &size, NULL,
                                  transferProgress, &transfer, in->error, sizeof(in->error));
        }
        ret = closeResult(fd, ret);
        hddInstallLog("DOWNLOAD result rc=%d bytes=%d expected=%u", ret, size, (unsigned)in->sizes[asset]);
        hddInstallLogFlush(in);
        if (ret < 0 || (uint32_t)size != in->sizes[asset]) { if (ret >= 0) ret = -EIO; goto invalid; }
        in->downloaded_assets |= 1u << asset;
        ret = hddInstallResumeSave(in); if (ret < 0) goto invalid;
        downloaded += in->sizes[asset];
    }
    if (in->flags & HDD_INSTALL_PSBBN) {
        ret = downloadLaunchers(in, phase, downloaded, download_total);
        if (ret < 0) goto invalid;
        *phase = "Checking installer files";
    }
    hddInstallPhase(HDD_INSTALL_PHASE_CHECK);
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
    in->ready = 1;
    return hddInstallResumeSave(in);
invalid:
    hddInstallLog("ERROR download/verification rc=%d detail=%s", ret, in->error);
    hddInstallLogFlush(in);
    if (!in->error[0]) snprintf(in->error, sizeof(in->error), "Downloaded installer files failed verification (error %d). The HDD is formatted; installation is incomplete.", ret);
    return ret;
}
int hddInstallDownload(HddInstall *in)
{
    const char *phase = "PSBBN DP repository";
    int ret;
    hddInstallPhase(HDD_INSTALL_PHASE_DOWNLOAD);
    cloudHttpSetStatusCallback(installerHttpsStatus, &phase);
    cloudHttpSetCaBundle(in->ca_data, in->ca_size);
    cloudHttpDownloadBatchBegin();
    ret = downloadInstaller(in, &phase);
    cloudHttpDownloadBatchEnd();
    cloudHttpSetCaBundle(NULL, 0);
    cloudHttpSetStatusCallback(NULL, NULL);
    return ret;
}
#else
int hddInstallResolve(HddInstall *in)
{
    (void)in;
    return -ENOSYS;
}
int hddInstallDownload(HddInstall *in)
{
    snprintf(in->error, sizeof(in->error), "This build does not include native HTTPS networking.");
    return -ENOSYS;
}
#endif

/* Only delete a temporary folder whose marker belongs to this install. */
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
    if (ret < 0) {
        if (!hddInstallRecoveryFault()) genRemove(path);
        return ret;
    }
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
    return in->owned && in->stage[0] && stageMatches(in, in->stage) ? 0 : -ENOENT;
}

static int removeStagedFile(const char *path)
{
    iox_stat_t stat;
    int ret = genRemove((char *)path);
    if (ret >= 0) return 0;
    ret = fileXioGetStat(path, &stat);
    return ret == -ENOENT ? 0 : ret < 0 ? ret : -EIO;
}

int hddInstallCleanup(HddInstall *in)
{
    char path[MAX_PATH];
    int ret = 0, result;
    if (hddInstallRecoveryFault()) return HDD_INSTALL_ATA_TIMEOUT;
    in->ready = 0;
    if (!in->cleanup_pending) {
        result = hddInstallFindStage(in);
        if (result < 0) return result;
        for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
            if (pathJoin(path, sizeof(path), in->stage, hddInstallAssetName(asset)) < 0) {
                ret = ret < 0 ? ret : -ENAMETOOLONG;
                continue;
            }
            result = removeStagedFile(path);
            if (result < 0 && ret >= 0) ret = result;
        }
        for (unsigned i = 0; i < in->launcher_count; i++) {
            char name[32];
            launcherStageName(i, name);
            result = pathJoin(path, sizeof(path), in->stage, name);
            if (result >= 0) result = removeStagedFile(path);
            if (result < 0 && ret >= 0) ret = result;
        }
        if (ret < 0) return ret;
        ret = hddInstallResumeRemove(in);
        if (ret < 0) return ret;
        if (hddInstallLogEnabled()) {
            hddInstallLog("Staged payloads cleaned; retaining owner marker and INSTALLER.LOG");
            hddInstallLogFlush(in);
            return 0;
        }
        if (pathJoin(path, sizeof(path), in->stage, "owner.bin") < 0) return -ENAMETOOLONG;
        ret = removeStagedFile(path);
        if (ret < 0) return ret;
        in->cleanup_pending = 1;
    }
    snprintf(path, sizeof(path), "%s", in->stage);
    ret = genRmdir(path);
    if (ret < 0) return ret;
    in->stage[0] = 0; in->owned = 0; in->cleanup_pending = 0;
    return 0;
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
static int writeNew(HddInstall *in, const char *path, const void *data, unsigned size)
{
    int fd, ret;
    unsigned done = 0;
    hddInstallLog("WRITE %s (%u bytes)", path, size);
    fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | (in->owned && (in->recovery_attempts || in->resumed) ? FIO_O_TRUNC : FIO_O_EXCL));
    if (fd < 0) return fd;
    hddInstallProgress(path, 0, size);
    ret = 0;
    while (done < size) {
        unsigned n = size - done > 32768 ? 32768 : size - done;
        ret = writeAll(fd, (const unsigned char *)data + done, n);
        if (ret < 0) break;
        done += n; hddInstallProgress(path, done, size);
    }
    ret = closeResult(fd, ret);
    if (ret < 0) { char remove[MAX_PATH]; snprintf(remove, sizeof(remove), "%s", path); genRemove(remove); }
    return ret;
}
static int copyFile(HddInstall *in, const char *name, uint32_t size, const char *destination, int replace)
{
    unsigned char *buffer;
    char source[MAX_PATH];
    uint32_t done = 0;
    int input, output, ret = 0;
    if (pathJoin(source, sizeof(source), in->stage, name) < 0) return -ENAMETOOLONG;
    buffer = malloc(32768);
    if (!buffer) return -ENOMEM;
    input = genOpen(source, FIO_O_RDONLY);
    if (input < 0) { free(buffer); return input; }
    hddInstallLog("COPY %s -> %s (%u bytes)", source, destination, (unsigned)size);
    output = genOpen(destination, FIO_O_WRONLY | FIO_O_CREAT | (replace ? FIO_O_TRUNC : FIO_O_EXCL));
    if (output < 0) { genClose(input); free(buffer); return output; }
    hddInstallProgress(destination, 0, size);
    while (done < size) {
        unsigned n = size - done > 32768 ? 32768 : size - done;
        ret = readAll(input, buffer, n);
        if (ret >= 0) ret = writeAll(output, buffer, n);
        if (ret < 0) break;
        done += n; hddInstallProgress(destination, done, size);
    }
    ret = closeResult(output, ret); ret = closeResult(input, ret); free(buffer);
    return ret;
}
static int copyAsset(HddInstall *in, unsigned asset, const char *destination)
{
    int ret = checkHash(in, asset);
    if (ret < 0) return ret;
    return copyFile(in, hddInstallAssetName(asset), in->sizes[asset], destination, in->recovery_attempts || in->resumed);
}
int hddInstallPfsPartition(HddInstall *in, unsigned mb, const char *name, unsigned slot)
{
    char path[96];
    iox_stat_t st;
    if (slot >= HDD_INSTALL_PFS_COUNT || !mb || mb > UINT32_MAX / 2048u) return -EINVAL;
    snprintf(path, sizeof(path), "hdd0:%s", name);
    int ret = fileXioGetStat(path, &st);
    if (in->pfs_start[slot]) {
        if (ret < 0) return ret;
        if (st.private_5 != in->pfs_start[slot]) return -EEXIST;
    } else {
        if (ret != -ENOENT) return ret < 0 ? ret : -EEXIST;
        ret = hddMakeFilesystem(mb, (char *)name, FS_GROUP_APPLICATION);
        if (ret != (int)mb) return ret < 0 ? ret : -ENOSPC;
        ret = fileXioGetStat(path, &st);
        if (ret < 0) return ret;
    }
    if (st.mode != 0x100 || st.size != mb * 2048u || st.attr || st.private_0 ||
        in->apa_sectors < st.size || st.private_5 < 2048 ||
        st.private_5 > in->apa_sectors - st.size) return -EINVAL;
    if (!in->pfs_start[slot]) {
        in->pfs_start[slot] = st.private_5;
        ret = hddInstallResumeSave(in);
        if (ret < 0) return ret;
    }
    return mb;
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
            if (ret >= 0) ret = writeNew(in, path, save.files[i].data, save.files[i].size);
        }
        if (ret >= 0 && in->jail) {
            char elf[MAX_PATH];
            const SaveArchiveEntry *icon = NULL;
            for (unsigned i = 0; i < save.count; i++) if (!strcmp(save.files[i].name, "list.icn")) icon = &save.files[i];
            hddInstallLog("Create app launcher: %s (128 MiB)", app_partitions[index]);
            hddInstallLogFlush(in);
            ret = hddInstallPfsPartition(in, 128, app_partitions[index], HDD_INSTALL_PFS_APP + index);
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
                    if (ret >= 0) ret = writeNew(in, "pfs3:/res/info.sys", info, n);
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

/* Installed after the system/language archives so their older launcher copies
 * cannot replace this commit's binaries. Neutrino's complete runtime stays on
 * exFAT, matching NHDDL's ATA layout and DP's game installer. */
static int installLaunchers(HddInstall *in)
{
    static const char *const folders[] = {"APPS", "ART", "CFG", "CHT", "LNG", "THM", "VMC", "CD", "DVD", "POPS", "nhddl"};
    char target[MAX_PATH], name[32];
    int ret;
    uint64_t total = 0, copied = 0;
    if (!(in->flags & HDD_INSTALL_PSBBN)) return 0;
    if (!in->jail || !in->launcher_count || !in->launcher_files) return -EINVAL;
    for (unsigned i = 0; i < in->launcher_count; i++)
        if (strncmp(in->launcher_files[i].path, "icons/", 6)) total += in->launcher_files[i].size;
    for (unsigned i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        snprintf(target, sizeof(target), "ata0:/%s", folders[i]);
        ret = makeDirectory(target, 0); if (ret < 0) return ret;
    }
    for (unsigned i = 0; i < in->launcher_count; i++) {
        const HddInstallFile *file = &in->launcher_files[i];
        const char *source = file->path;
        if (!strncmp(source, "icons/", 6)) continue; /* PP.LAUNCHER metadata below. */
        ret = launcherDestination(source, target);
        if (ret < 0) return ret;
        /* Target paths derive only from the validated manifest's leaf names. */
        for (char *p = strchr(target, ':'); p && *p; p++) if (*p == '/' && p[1] && p[-1] != ':') {
            *p = 0; ret = makeDirectory(target, 0); *p = '/';
            if (ret < 0) return ret;
        }
        launcherStageName(i, name);
        ret = copyFile(in, name, file->size, target, 1);
        if (ret < 0) return ret;
        copied += file->size;
        if (total) hddInstallOverall(900 + 80 * copied / total, 1000);
    }
    {
        char config[256], path[128];
        const char *language = !strcmp(in->language.code, "por") ? "Portuguese_BR" : in->language.name;
        snprintf(path, sizeof(path), "scripts/assets/OPL/LNG/lang_%s.lng", language);
        if (!*language || findLauncher(in, path) < 0) language = "English";
        int n = snprintf(config, sizeof(config), "enable_coverart=1\ndefault_device=0\nusb_mode=2\napp_mode=2\nenable_bdm_hdd=1\nlanguage_text=%s\nswap_select_btn=0\n", language);
        return writeNew(in, "ata0:/conf_opl.cfg", config, n);
    }
}

static int installGamePartition(HddInstall *in)
{
    const char *title = hddInstallGameName(in->game_launcher);
    const char *id = in->game_launcher == HDD_GAME_NEUTRINO ? "APP_NHDDL" : "APP_OPL";
    char path[MAX_PATH], name[32], elf[128], info[1024];
    unsigned char *icon;
    int index, art, fd, ret, n;
    if (!(in->flags & HDD_INSTALL_PSBBN)) return 0;
    index = findLauncher(in, launcherIcon(in)); art = findLauncher(in, launcherArt(in));
    if (index < 0 || art < 0 || in->launcher_files[index].size > 1024 * 1024) return -EINVAL;
    icon = malloc(in->launcher_files[index].size); if (!icon) return -ENOMEM;
    launcherStageName(index, name);
    ret = pathJoin(path, sizeof(path), in->stage, name);
    if (ret >= 0) {
        fd = genOpen(path, FIO_O_RDONLY);
        ret = fd < 0 ? fd : closeResult(fd, readAll(fd, icon, in->launcher_files[index].size));
    }
    if (ret >= 0) {
        hddInstallLog("Creating PP.LAUNCHER (128 MiB): %s", title);
        ret = hddInstallPfsPartition(in, 128, "PP.LAUNCHER", HDD_INSTALL_PFS_GAME);
        ret = ret == 128 ? 0 : ret < 0 ? ret : -ENOSPC;
    }
    snprintf(elf, sizeof(elf), "hdd0:__system:pfs:/launcher/%s", hddInstallGameElf(in->game_launcher));
    if (ret >= 0) ret = InjectHddAppLauncherWithArg("PP.LAUNCHER", elf, title, icon, in->launcher_files[index].size,
                                        in->game_launcher == HDD_GAME_NEUTRINO ? "-mode=ata" : NULL);
    free(icon);
    if (ret < 0) return ret;
    ret = fileXioMount("pfs3:", "hdd0:PP.LAUNCHER", FIO_MT_RDWR); if (ret < 0) return ret;
    ret = makeDirectory("pfs3:/res", 0);
    n = snprintf(info, sizeof(info),
        "title = [LCH] %s\ntitle_id = %s\ntitle_sub_id = 0\nrelease_date = \n"
        "developer_id = \npublisher_id = Homebrew\nnote = \ncontent_web = \n"
        "image_topviewflag = 0\nimage_type = 0\nimage_count = 1\nimage_viewsec = 600\n"
        "copyright_viewflag = 0\ncopyright_imgcount = 0\ngenre = \nparental_lock = 1\n"
        "effective_date = 0\nexpire_date = 0\nviolence_flag = 0\ncontent_type = 255\ncontent_subtype = 0\n", title, id);
    if (ret >= 0) ret = writeNew(in, "pfs3:/res/info.sys", info, n);
    launcherStageName(art, name);
    if (ret >= 0) ret = copyFile(in, name, in->launcher_files[art].size, "pfs3:/res/jkt_001.png", in->owned && (in->recovery_attempts || in->resumed));
    { int closed = fileXioUmount("pfs3:"); if (ret >= 0) ret = closed; }
    return ret;
}

int hddInstallWriteConfigs(HddInstall *in)
{
    char config[4096];
    int installed = installLaunchers(in);
    if (installed < 0) return installed;
    for (unsigned i = 0; i < 2; i++) {
        int ret = hddInstallConfig(in, i == 0, config, sizeof(config));
        if (ret >= 0) ret = writeNew(in, i ? "pfs1:/osdmenu/OSDMENU.CNF" : "pfs1:/osdmenu/OSDMBR.CNF", config, ret);
        if (ret < 0) return ret;
    }
    /* Install kernels after the system archive, which may contain older copies. */
    if (hddInstallSelectedAsset(in, HI_KERNEL_JPN)) {
        int ret = checkHash(in, HI_KERNEL_JPN);
        if (ret >= 0) ret = copyFile(in, hddInstallAssetName(HI_KERNEL_JPN), in->sizes[HI_KERNEL_JPN], "pfs0:/p2lboot/vmlinux", 1);
        if (ret < 0) return ret;
    }
    if (in->flags & HDD_INSTALL_PS2LINUX) {
        int ret = copyAsset(in, HI_KERNEL_NTSC, "pfs0:/p2lboot/ps2-linux-ntsc");
        if (ret < 0) return ret;
        ret = copyAsset(in, HI_KERNEL_VGA, "pfs0:/p2lboot/ps2-linux-vga");
        if (ret < 0) return ret;
    }
    hddInstallOverall(980, 1000);
    return 0;
}

int hddInstallApply(HddInstall *in)
{
    int ret = -EINVAL, mounts = 0;
    static const char *const partitions[] = {"hdd0:__system", "hdd0:__sysconf", "hdd0:__common"};
    static const char *const pfs[] = {"pfs0:", "pfs1:", "pfs2:"};
    hddInstallPhase(HDD_INSTALL_PHASE_INSTALL);
    if (!in->ready || !in->flags || (in->jail ? (in->flags & ~HDD_INSTALL_PS2LINUX) != (HDD_INSTALL_HOSD | HDD_INSTALL_APPS | HDD_INSTALL_PSBBN) :
        !!(in->flags & ~(HDD_INSTALL_HOSD | HDD_INSTALL_APPS)))) goto failed;
    ret = hddInstallMountStage(in);
    if (ret < 0) goto failed;
    ret = hddInstallFindStage(in);
    if (ret < 0) {
        hddInstallUnmountStage(in);
        snprintf(in->error, sizeof(in->error), "The temporary installer folder is missing or has a different owner. The HDD is formatted; software installation has not started.");
        return ret;
    }
    /* Validate staged bytes before mutating any installed file. */
    for (unsigned asset = 0; asset < HDD_INSTALL_ASSETS; asset++) {
        if (!hddInstallSelectedAsset(in, asset)) continue;
        ret = checkHash(in, asset);
        if (ret < 0) break;
    }
    if (ret >= 0) ret = checkLaunchers(in);
    { int closed = hddInstallUnmountStage(in); if (ret >= 0) ret = closed; }
    if (ret < 0) goto failed;
    if (in->flags & HDD_INSTALL_PSBBN) {
        ret = hddInstallPsbbnPartitions(in);
        if (ret < 0) goto failed;
    }
    for (unsigned i = 0; i < 3; i++) {
        hddInstallLog("MOUNT %s -> %s", partitions[i], pfs[i]);
        ret = fileXioMount(pfs[i], partitions[i], FIO_MT_RDWR);
        hddInstallLog("MOUNT rc=%d", ret);
        if (ret < 0) goto failed;
        mounts++;
    }
    if (in->flags & HDD_INSTALL_APPS) {
        ret = installApps(in);
        if (ret < 0) goto failed;
    }
    ret = installGamePartition(in);
    if (ret < 0) goto failed;
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
    hddInstallRecoveryObserve(ret);
    if (ret < 0) hddInstallLog("ERROR apply rc=%d", ret);
    while (mounts && !hddInstallRecoveryFault()) {
        int closed = fileXioUmount(pfs[--mounts]);
        hddInstallRecoveryObserve(closed);
        if (ret >= 0 && closed < 0) ret = closed;
    }
    if (ret < 0) snprintf(in->error, sizeof(in->error), "Software installation failed (error %d). The HDD is formatted, but installation is incomplete.", ret);
    else hddInstallOverall(100, 1000);
    return ret;
}
