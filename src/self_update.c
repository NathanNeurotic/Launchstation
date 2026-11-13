//--------------------------------------------------------------
// Native self-update for Launchstation: download into RAM, verify, overwrite the running ELF in place.
//--------------------------------------------------------------
#include "launchelf.h"
#include "cloud_http.h"
#include "filer_shared.h"
#include "filer_actions.h"
#include "self_update.h"
#include "dash.h"

#include <malloc.h>
#include <ctype.h>

#define UPDATE_ELF_URL "https://github.com/NathanNeurotic/Launchstation/releases/download/latest/" SELF_UPDATE_ASSET
#define UPDATE_MAX_ELF (16 * 1024 * 1024)
#define UPDATE_SPACE_MARGIN (32 * 1024) /* card cluster rounding and directory entries */
/* Heap that must stay free beside a RAM download, for TLS, drawing and
 * whatever else is loaded. Without it the package goes to the card. */
#define UPDATE_RAM_RESERVE (4 * 1024 * 1024)

static int device_path(const char *path, const char *name, int allow_bare)
{
    const char *p;
    size_t n;

    if (path == NULL || name == NULL)
        return 0;
    n = strlen(name);
    if (strncmp(path, name, n))
        return 0;
    p = path + n;
    if (allow_bare && *p == ':')
        return 1;
    if (!isdigit((unsigned char)*p))
        return 0;
    while (isdigit((unsigned char)*p))
        p++;
    return *p == ':';
}

static int supported_boot_path(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return 0;

    /* Do not bake in mass0/mass1-style limits: BDM unit numbering depends on
     * attach order and the number of live devices. */
    return device_path(path, "mc", 0) ||
           device_path(path, "mass", 1) ||
           device_path(path, "usb", 1) ||
           device_path(path, "mmce", 0) ||
           device_path(path, "mx4sio", 0) ||
           device_path(path, "ilink", 0) ||
           device_path(path, "ata", 0) ||
           device_path(path, "hdd", 0);
}

static int split_parent_leaf(const char *path, char *parent, int parent_size,
                             char *leaf, int leaf_size)
{
    const char *slash, *colon;
    size_t n;

    if (path == NULL || parent == NULL || leaf == NULL ||
        parent_size <= 0 || leaf_size <= 0)
        return 0;

    slash = strrchr(path, '/');
    if (slash != NULL) {
        n = (size_t)(slash - path + 1);
        if (n >= (size_t)parent_size || strlen(slash + 1) >= (size_t)leaf_size)
            return 0;
        memcpy(parent, path, n);
        parent[n] = '\0';
        snprintf(leaf, leaf_size, "%s", slash + 1);
        return leaf[0] != '\0';
    }

    colon = strrchr(path, ':');
    if (colon == NULL)
        return 0;
    n = (size_t)(colon - path + 1);
    if (n >= (size_t)parent_size || strlen(colon + 1) >= (size_t)leaf_size)
        return 0;
    memcpy(parent, path, n);
    parent[n] = '\0';
    snprintf(leaf, leaf_size, "%s", colon + 1);
    return leaf[0] != '\0';
}

static int join_leaf(char *out, int out_size, const char *parent, const char *leaf)
{
    return snprintf(out, out_size, "%s%s", parent, leaf) < out_size;
}

static int find_file_info(const char *parent, const char *leaf, FILEINFO *out)
{
    FILEINFO *files;
    int n, i, found = 0;

    if (parent == NULL || leaf == NULL || out == NULL)
        return 0;
    if (!filerPrepareDirRead(parent))
        return 0;

    files = memalign(64, sizeof(FILEINFO) * MAX_ENTRY);
    if (files == NULL)
        return 0;
    n = getDir(parent, files);
    for (i = 0; i < n; i++) {
        if (!strcmp(files[i].name, leaf)) {
            memcpy(out, &files[i], sizeof(*out));
            found = 1;
            break;
        }
    }
    free(files);
    return found;
}

static int hex_nibble(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int parse_expected_sha(const char *text, unsigned char expected[32])
{
    int i;

    if (text == NULL)
        return 0;
    if (!strncmp(text, "sha256:", 7))
        text += 7;
    if (strlen(text) != 64)
        return 0;

    for (i = 0; i < 32; i++) {
        int hi = hex_nibble((unsigned char)text[i * 2]);
        int lo = hex_nibble((unsigned char)text[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        expected[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

/* The bar under the step text: bytes done of the expected total. */
static int download_expected;

static void update_progress(const char *step, int done, int total)
{
    char line[96];

    if (total > 0)
        snprintf(line, sizeof(line), "%s: %d / %d KB", step, done / 1024, (total + 1023) / 1024);
    else
        snprintf(line, sizeof(line), "%s: %d KB", step, done / 1024);
    DashStorageProbeProgress(line, total > 0 ? done : 0, total > 0 ? total : 0);
}

static void download_progress(int done, int total)
{
    update_progress("Downloading the update", done, total > 0 ? total : download_expected);
}

static void write_progress(int done, int total)
{
    update_progress("Writing the new ELF", done, total);
}

/* The package is downloaded into RAM only: it fits when the heap holds it plus
 * the reserve, otherwise NULL and the update is refused before anything is touched. */
static unsigned char *update_ram_buffer(int package_bytes)
{
    unsigned char *buffer;
    void *spare;

    if (package_bytes <= 0 || package_bytes > UPDATE_MAX_ELF)
        return NULL;
    buffer = malloc(package_bytes);
    if (buffer == NULL)
        return NULL;
    spare = malloc(UPDATE_RAM_RESERVE);
    if (spare == NULL) {
        free(buffer);
        return NULL;
    }
    free(spare);
    return buffer;
}

static int download_to_ram(const char *url, unsigned char *buffer, int cap,
                           const char *ca_path, unsigned char digest[32],
                           char *error, int error_size)
{
    int size = 0, r;

    download_expected = cap;
    cloudHttpProgress = download_progress;
    selfUpdateWriteProgress = write_progress;
    r = cloudHttpDownloadMem(url, buffer, cap, ca_path, &size, digest,
                             error, error_size);
    cloudHttpProgress = NULL;
    return r < 0 ? -1 : size;
}

static int looks_like_elf(const unsigned char *h, int n)
{
    /* 32-bit little-endian MIPS executable */
    return n >= 20 && h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' &&
           h[4] == 1 && h[5] == 1 && h[16] == 2 && h[17] == 0 && h[18] == 8 && h[19] == 0;
}

/* Write the verified ELF over the running one: the same path, opened with
 * O_TRUNC, no second file, rename, delete or backup. Returns 0, or the step
 * (1 open, 2 write, 3 close) with *code set to the error. */
static int overwrite_elf(const char *path, const unsigned char *data, int size, int *code)
{
    int fd, done = 0;

    fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    if (fd < 0) {
        *code = fd;
        return 1;
    }
    while (done < size) {
        int count = size - done > 16384 ? 16384 : size - done;
        int n = genWrite(fd, (void *)(data + done), count);
        if (n <= 0 || n > count) {
            *code = n < 0 ? n : -ENOSPC;
            genClose(fd);
            return 2;
        }
        done += n;
        write_progress(done, size);
    }
    if (genClose(fd) < 0) {
        *code = -EIO;
        return 3;
    }
    return 0;
}

static int install_update_files(const char *parent, const char *leaf,
                                const char *ca_path, const char *expected_sha256,
                                int package_bytes, unsigned char *ram,
                                char *result, int result_size)
{
    char elf[MAX_PATH], backup[MAX_PATH];
    char update_ca[MAX_PATH], bgm[MAX_PATH];
    char error[256], remove_path[MAX_PATH];
    unsigned char expected[32], actual[32];
    int size, package_size, sidecar_size, elf_pos = 0, step, code = 0;
    int need_ca, need_bgm, added_ca = 0, added_bgm = 0;
    u64 free_bytes;
    FILEINFO old_info;

    if (snprintf(backup, sizeof(backup), "%s%s.BAK", parent, leaf) >= (int)sizeof(backup) ||
        !join_leaf(elf, sizeof(elf), parent, leaf) ||
        !join_leaf(update_ca, sizeof(update_ca), parent, "WLE-UPDATE-CA.PEM") ||
        !join_leaf(bgm, sizeof(bgm), parent, "BGM.ADP")) {
        snprintf(result, result_size, "The app folder path is too long for a safe update.");
        return -1;
    }
    if (ram == NULL) {
        snprintf(result, result_size,
                 "Not enough free memory to hold the update (it needs the package plus 4 MB). The installed ELF was not changed.");
        return -1;
    }
    if (!parse_expected_sha(expected_sha256, expected)) {
        snprintf(result, result_size,
                 "GitHub did not provide a valid SHA-256 digest for the app package.");
        return -1;
    }

    /* Remove a .BAK left by an older build (none is made any more). */
    if (wleExists(backup)) {
        snprintf(remove_path, sizeof(remove_path), "%s", backup);
        if (genRemove(remove_path) < 0 && wleExists(backup)) {
            snprintf(result, result_size,
                     "Could not remove the old .BAK file. The installed ELF was not changed.");
            return -1;
        }
    }

    error[0] = '\0';
    DashStorageProbeStatus("Downloading the update from GitHub");
    size = download_to_ram(UPDATE_ELF_URL, ram, package_bytes, ca_path,
                           actual, error, sizeof(error));
    if (size <= 0) {
        snprintf(result, result_size, "Update download failed: %s",
                 error[0] ? error : "no update data was received");
        return -1;
    }
    DashStorageProbeStatus("Checking the download (SHA-256)");
    if (memcmp(expected, actual, sizeof(expected)) != 0) {
        snprintf(result, result_size,
                 "Downloaded update failed SHA-256 verification. The running ELF was not changed.");
        return -1;
    }
    package_size = size;
    DashStorageProbeStatus("Checking the new ELF");
    size = selfUpdateLocatePsuElf(ram, package_size, &elf_pos);
    if (size <= 0) {
        snprintf(result, result_size,
                 "The verified package does not contain the expected app ELF. The running ELF was not changed.");
        return -1;
    }
    if (!looks_like_elf(ram + elf_pos, size)) {
        snprintf(result, result_size,
                 "Downloaded update is not a valid PlayStation 2 ELF. The running ELF was not changed.");
        return -1;
    }

    need_ca = !wleExists(update_ca);
    need_bgm = !wleExists(bgm);

    /* Verified four-, eight- and nine-file PSUs can add missing runtime sidecars.
     * Existing CA/BGM files may be customized and are never touched. The extractor
     * deletes a sidecar it could not finish; the running ELF is still untouched. */
    if (need_ca || need_bgm)
        DashStorageProbeStatus("Adding missing bundled files");
    if (need_ca) {
        sidecar_size = selfUpdateExtractPsuSidecarMem(ram, package_size, "WLE-UPDATE-CA.PEM", update_ca);
        if (sidecar_size < 0)
            goto sidecar_failed;
        added_ca = sidecar_size > 0;
    }
    if (need_bgm) {
        sidecar_size = selfUpdateExtractPsuSidecarMem(ram, package_size, "BGM.ADP", bgm);
        if (sidecar_size < 0)
            goto sidecar_failed;
        added_bgm = sidecar_size > 0;
    }

    /* Overwriting in place needs room only for the growth over the old ELF. Checked after the sidecars
     * are written, so what they used is already gone, and before the running ELF is touched. */
    if (find_file_info(parent, leaf, &old_info) && filerFreeSpace(parent, &free_bytes) &&
        free_bytes + old_info.stats.FileSizeByte < (u64)size + UPDATE_SPACE_MARGIN) {
        snprintf(result, result_size,
                 "Not enough free space: the new ELF is %d KB and the device has %d KB free beside the old one. Free some space and try again. The running ELF was not changed.",
                 size / 1024, (int)((free_bytes + old_info.stats.FileSizeByte) / 1024));
        return -1;
    }

    /* Only now, after a complete verified download, touch the running ELF. */
    DashStorageProbeStatus("Replacing the ELF");
    step = overwrite_elf(elf, ram + elf_pos, size, &code);
    if (step) {
        snprintf(result, result_size,
                 "Could not finish replacing the ELF (step %d of 3: 1 open, 2 write, 3 close; error %d). %s may be damaged: copy the Launchstation ELF to this device again.",
                 step, code, leaf);
        return -1;
    }

    snprintf(result, result_size,
             "Update installed and verified (%d bytes). %s%s%sRestart Launchstation to run the new build.",
             size,
             added_ca ? "Installed updater CA. " : "",
             added_bgm ? "Installed BGM. " : "",
             (!added_ca && !added_bgm) ? "Existing sidecars preserved. " : "");
    return 0;

sidecar_failed:
    snprintf(result, result_size,
             "Failed to add the bundled updater CA or BGM. The running ELF and existing sidecars were not changed.");
    return -1;
}

int selfUpdateInstall(const char *ca_path, const char *expected_sha256, int package_bytes,
                      char *result, int result_size)
{
    const char *boot = wleBootElfPath();
    char parent[MAX_PATH], leaf[MAX_NAME];
    sceMcStDateTime sas_timestamp;
    char warning[128];
    int preserve_sas, installed, timestamp_result;
    unsigned char *ram;
    size_t prefix, kept;

    if (result == NULL || result_size <= 0)
        return -1;
    result[0] = '\0';
    if (!supported_boot_path(boot)) {
        snprintf(result, result_size,
                 "This boot location cannot be updated safely in place: %.160s",
                 boot != NULL ? boot : "(unknown)");
        return -1;
    }
    if (!split_parent_leaf(boot, parent, sizeof(parent), leaf, sizeof(leaf))) {
        snprintf(result, result_size, "Could not resolve the running ELF path safely.");
        return -1;
    }
    if (!ensurePathDeviceStackReady(parent)) {
        snprintf(result, result_size, "The running ELF device is not writable right now.");
        return -1;
    }
    /* Read before the first download/staging write can change the folder date.
     * Restore after every transaction outcome, including all cleanup/rollback. */
    preserve_sas = filerCaptureSasTimestamp(parent, &sas_timestamp);
    if (preserve_sas < 0) {
        snprintf(result, result_size,
                 "Could not read the app folder timestamp (error %d). No update files were changed.",
                 preserve_sas);
        return -1;
    }
    ram = update_ram_buffer(package_bytes);
    installed = install_update_files(parent, leaf, ca_path, expected_sha256,
                                     package_bytes, ram, result, result_size);
    free(ram);
    if (preserve_sas > 0) {
        timestamp_result = filerRestoreSasTimestamp(parent, &sas_timestamp);
        if (timestamp_result != 0) {
            snprintf(warning, sizeof(warning),
                     "Could not restore the app folder SAS date (error %d); run SAS timestamps again.\n",
                     timestamp_result);
            /* Keep this warning visible even if the transaction message filled
             * the caller's buffer; retain as much recovery detail as fits. */
            prefix = strlen(warning);
            if (prefix > (size_t)result_size - 1)
                prefix = result_size - 1;
            kept = strlen(result);
            if (kept > (size_t)result_size - prefix - 1)
                kept = result_size - prefix - 1;
            memmove(result + prefix, result, kept);
            memcpy(result, warning, prefix);
            result[prefix + kept] = '\0';
        }
    }
    return installed;
}
