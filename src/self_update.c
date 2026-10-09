//--------------------------------------------------------------
// Native staged self-update for Launchstation.
//--------------------------------------------------------------
#include "launchelf.h"
#include "cloud_http.h"
#include "filer_shared.h"
#include "filer_actions.h"
#include "self_update.h"

#include <malloc.h>
#include <ctype.h>

#define UPDATE_ELF_URL "https://github.com/NathanNeurotic/Launchstation/releases/download/latest/" SELF_UPDATE_ASSET
#define UPDATE_MAX_ELF (16 * 1024 * 1024)

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

static int rename_leaf(const char *parent, const char *old_leaf, const char *new_leaf)
{
    FILEINFO file;

    if (!find_file_info(parent, old_leaf, &file))
        return -1;
    return Rename(parent, &file, new_leaf);
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

static int download_file(const char *url, const char *path, const char *ca_path,
                         int max_bytes, unsigned char digest[32],
                         char *error, int error_size)
{
    int fd, size = 0, r;

    fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    if (fd < 0) {
        snprintf(error, error_size, "Could not create %.160s", path);
        return -1;
    }
    r = cloudHttpDownloadFd(url, fd, ca_path, max_bytes, &size,
                            digest, error, error_size);
    if (genClose(fd) < 0 && r == 0) {
        snprintf(error, error_size, "Could not finalize %.160s", path);
        r = -1;
    }
    if (r < 0) {
        char remove_path[MAX_PATH];
        snprintf(remove_path, sizeof(remove_path), "%s", path);
        genRemove(remove_path);
        return -1;
    }
    return size;
}

int selfUpdateInstall(const char *ca_path, const char *expected_sha256, char *result, int result_size)
{
    const char *boot = wleBootElfPath();
    char parent[MAX_PATH], leaf[MAX_NAME];
    char stage_leaf[MAX_NAME], backup_leaf[MAX_NAME];
    char stage[MAX_PATH], backup[MAX_PATH];
    char package_leaf[MAX_NAME], package[MAX_PATH];
    char error[256], remove_path[MAX_PATH];
    unsigned char expected[32], actual[32];
    int size, rollback_ok;

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
        snprintf(result, result_size, "Could not resolve the running ELF path.");
        return -1;
    }
    if (snprintf(stage_leaf, sizeof(stage_leaf), "%s.UPDATE", leaf) >= (int)sizeof(stage_leaf) ||
        snprintf(backup_leaf, sizeof(backup_leaf), "%s.BAK", leaf) >= (int)sizeof(backup_leaf) ||
        !join_leaf(stage, sizeof(stage), parent, stage_leaf) ||
        !join_leaf(backup, sizeof(backup), parent, backup_leaf) ||
        snprintf(package_leaf, sizeof(package_leaf), "%s.PACKAGE", leaf) >= (int)sizeof(package_leaf) ||
        !join_leaf(package, sizeof(package), parent, package_leaf)) {
        snprintf(result, result_size, "The running ELF path is too long for a safe staged update.");
        return -1;
    }

    if (!ensurePathDeviceStackReady(parent)) {
        snprintf(result, result_size, "The running ELF device is not writable right now.");
        return -1;
    }

    if (!parse_expected_sha(expected_sha256, expected)) {
        snprintf(result, result_size,
                 "GitHub did not provide a valid SHA-256 digest for the app package.");
        return -1;
    }

    error[0] = '\0';
    size = download_file(UPDATE_ELF_URL, package, ca_path, UPDATE_MAX_ELF,
                         actual, error, sizeof(error));
    if (size <= 0) {
        snprintf(result, result_size, "Update download failed: %s",
                 error[0] ? error : "no update data was received");
        return -1;
    }
    if (memcmp(expected, actual, sizeof(expected)) != 0) {
        snprintf(remove_path, sizeof(remove_path), "%s", package);
        genRemove(remove_path);
        snprintf(result, result_size,
                 "Downloaded update failed SHA-256 verification. The running ELF was not changed.");
        return -1;
    }
    size = selfUpdateExtractPsu(package, stage, size);
    snprintf(remove_path, sizeof(remove_path), "%s", package);
    genRemove(remove_path);
    if (size <= 0) {
        snprintf(result, result_size,
                 "The verified package does not contain the expected app ELF. The running ELF was not changed.");
        return -1;
    }
    if (checkELFheader(stage) < 0) {
        snprintf(remove_path, sizeof(remove_path), "%s", stage);
        genRemove(remove_path);
        snprintf(result, result_size,
                 "Downloaded update is not a valid PlayStation 2 ELF. The running ELF was not changed.");
        return -1;
    }

    /* Only now, after a complete verified download, touch the current ELF.
     * Keep one recovery copy beside it. */
    if (wleExists(backup)) {
        snprintf(remove_path, sizeof(remove_path), "%s", backup);
        if (genRemove(remove_path) < 0 && wleExists(backup)) {
            snprintf(remove_path, sizeof(remove_path), "%s", stage);
            genRemove(remove_path);
            snprintf(result, result_size,
                     "Could not replace the previous .BAK recovery copy. The running ELF was not changed.");
            return -1;
        }
    }

    if (rename_leaf(parent, leaf, backup_leaf) < 0) {
        snprintf(remove_path, sizeof(remove_path), "%s", stage);
        genRemove(remove_path);
        snprintf(result, result_size,
                 "Could not create the .BAK recovery copy. The running ELF was not changed.");
        return -1;
    }

    if (rename_leaf(parent, stage_leaf, leaf) < 0) {
        rollback_ok = rename_leaf(parent, backup_leaf, leaf) >= 0;
        snprintf(result, result_size,
                 rollback_ok
                     ? "Installing the verified update failed; the original ELF was restored."
                     : "Installing the verified update failed and automatic rollback also failed. Keep the .BAK file and restore it manually.");
        return -1;
    }

    snprintf(result, result_size,
             "Update installed and verified (%d bytes). Recovery copy: %s%s. Restart Launchstation to run the new build.",
             size, parent, backup_leaf);
    return 0;
}
