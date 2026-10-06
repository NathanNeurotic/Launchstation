//--------------------------------------------------------------
// wLaunchELF OPL IGR setup / BootCard helper installer.
//--------------------------------------------------------------
#include "launchelf.h"
#include "mmce_igr.h"
#include "init.h"
#include "wle_igr_config.h"

extern int fileMode;

#define WLE_IGR_HELPER_NAME "WLE-IGR.ELF"
#define WLE_IGR_MAX_BOOT_IMAGES 32
#define WLE_IGR_SCAN_DEPTH 10
#define WLE_IGR_VERIFY_CHUNK 32768

#ifdef WLE_PS2BUILD
extern u8 wle_igr_elf_irx[];
extern int size_wle_igr_elf_irx;
#else
extern u8 wle_igr_elf[];
extern int size_wle_igr_elf;
#define wle_igr_elf_irx wle_igr_elf
#define size_wle_igr_elf_irx size_wle_igr_elf
#endif

typedef struct {
    char path[MAX_PATH];
} BootImage;

static BootImage boot_images[WLE_IGR_MAX_BOOT_IMAGES];
static int boot_image_count;
static u8 verify_buf[WLE_IGR_VERIFY_CHUNK] __attribute__((aligned(64)));

static int join_path(char *out, size_t out_size, const char *a, const char *b)
{
    size_t n;

    if (out == NULL || out_size == 0 || a == NULL || b == NULL)
        return 0;
    n = strlen(a);
    if (n > 0 && a[n - 1] != '/' && a[n - 1] != ':')
        return snprintf(out, out_size, "%s/%s", a, b) < (int)out_size;
    return snprintf(out, out_size, "%s%s", a, b) < (int)out_size;
}

static int prefix_case_equal(const char *text, const char *prefix)
{
    int i;

    if (text == NULL || prefix == NULL)
        return 0;
    for (i = 0; prefix[i]; i++) {
        int a = text[i], b = prefix[i];
        if (a == 0)
            return 0;
        if (a >= 'a' && a <= 'z')
            a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z')
            b -= 'a' - 'A';
        if (a != b)
            return 0;
    }
    return 1;
}

static int is_bootcard_image(const char *parent, const char *name)
{
    const char *leaf;

    if (parent == NULL || name == NULL || !genCmpFileExt(name, "MCD"))
        return 0;
    leaf = strrchr(parent, '/');
    leaf = leaf != NULL ? leaf + 1 : parent;
    if (stricmp(leaf, "BOOT"))
        return 0;
    return prefix_case_equal(name, "BootCard");
}

static void collect_boot_images(const char *dir, int depth)
{
    iox_dirent_t ent;
    char scan[MAX_PATH], child[MAX_PATH];
    int fd;

    if (depth > WLE_IGR_SCAN_DEPTH || boot_image_count >= WLE_IGR_MAX_BOOT_IMAGES)
        return;
    snprintf(scan, sizeof(scan), "%s", dir);
    fd = genDopen(scan);
    if (fd < 0)
        return;

    memset(&ent, 0, sizeof(ent));
    while (fileXioDread(fd, &ent) > 0) {
        if (!strcmp(ent.name, ".") || !strcmp(ent.name, "..")) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }
        if (!join_path(child, sizeof(child), dir, ent.name)) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }

        if (FIO_S_ISDIR(ent.stat.mode)) {
            collect_boot_images(child, depth + 1);
        } else if (is_bootcard_image(dir, ent.name) &&
                   boot_image_count < WLE_IGR_MAX_BOOT_IMAGES) {
            snprintf(boot_images[boot_image_count++].path,
                     sizeof(boot_images[0].path), "%s", child);
        }
        memset(&ent, 0, sizeof(ent));
    }
    genDclose(fd);
}

static void portable_mmce_path(const char *in, char *out, size_t out_size)
{
    if (in != NULL &&
        (!strncmp(in, "mmce0:", 6) || !strncmp(in, "mmce1:", 6)))
        snprintf(out, out_size, "mmce?:%s", in + 6);
    else
        snprintf(out, out_size, "%s", in != NULL ? in : "");
}

static u8 *patched_helper(const char *app, const char *backup, int *size_out)
{
    u8 *copy;
    int size = size_wle_igr_elf_irx;
    int i;
    size_t magic_len = strlen(WLE_IGR_MAGIC);

    if (size <= (int)sizeof(WleIgrEmbeddedConfig) ||
        app == NULL || backup == NULL ||
        strlen(app) >= WLE_IGR_PATH || strlen(backup) >= WLE_IGR_PATH)
        return NULL;

    copy = memalign(64, size);
    if (copy == NULL)
        return NULL;
    memcpy(copy, wle_igr_elf_irx, size);

    for (i = 0; i <= size - (int)sizeof(WleIgrEmbeddedConfig); i++) {
        WleIgrEmbeddedConfig *cfg = (WleIgrEmbeddedConfig *)(copy + i);
        if (memcmp(cfg->magic, WLE_IGR_MAGIC, magic_len) != 0)
            continue;
        memset(cfg->app, 0, sizeof(cfg->app));
        memset(cfg->backup, 0, sizeof(cfg->backup));
        snprintf(cfg->app, sizeof(cfg->app), "%s", app);
        snprintf(cfg->backup, sizeof(cfg->backup), "%s", backup);
        *size_out = size;
        return copy;
    }

    free(copy);
    return NULL;
}

static int split_image_path(const char *full, char *dir, size_t dir_size,
                            FILEINFO *file)
{
    const char *slash;
    size_t n;

    if (full == NULL || dir == NULL || file == NULL)
        return 0;
    slash = strrchr(full, '/');
    if (slash == NULL)
        return 0;
    n = slash - full + 1;
    if (n >= dir_size || strlen(slash + 1) >= sizeof(file->name))
        return 0;

    memcpy(dir, full, n);
    dir[n] = '\0';
    memset(file, 0, sizeof(*file));
    snprintf(file->name, sizeof(file->name), "%s", slash + 1);
    return 1;
}

static int write_verified(const char *path, const u8 *data, int size)
{
    int fd, done, r;
    char remove_path[MAX_PATH];

    fd = genOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    if (fd < 0)
        return -1;
    for (done = 0; done < size;) {
        int want = size - done > 32768 ? 32768 : size - done;
        r = genWrite(fd, (void *)(data + done), want);
        if (r != want) {
            genClose(fd);
            snprintf(remove_path, sizeof(remove_path), "%s", path);
            genRemove(remove_path);
            return -1;
        }
        done += r;
    }
    if (genClose(fd) < 0)
        return -1;

    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0)
        return -1;
    for (done = 0; done < size;) {
        int want = size - done > (int)sizeof(verify_buf) ? (int)sizeof(verify_buf) : size - done;
        r = genRead(fd, verify_buf, want);
        if (r != want || memcmp(verify_buf, data + done, want) != 0) {
            genClose(fd);
            snprintf(remove_path, sizeof(remove_path), "%s", path);
            genRemove(remove_path);
            return -1;
        }
        done += r;
    }
    r = genClose(fd);
    if (r < 0)
        return -1;
    return 0;
}

enum {
    WLE_IGR_HELPER_ERROR = -1,
    WLE_IGR_HELPER_MISSING = 0,
    WLE_IGR_HELPER_CURRENT = 1,
    WLE_IGR_HELPER_OUTDATED = 2
};

static int inspect_helper_path(const char *path, const u8 *helper, int helper_size)
{
    int fd, done = 0;
    s64 size;

    if (path == NULL || helper == NULL || helper_size <= 0)
        return WLE_IGR_HELPER_ERROR;
    if (!wleExists(path))
        return WLE_IGR_HELPER_MISSING;

    fd = genOpen(path, FIO_O_RDONLY);
    if (fd < 0)
        return WLE_IGR_HELPER_ERROR;
    size = genLseek(fd, 0, SEEK_END);
    if (size != helper_size || genLseek(fd, 0, SEEK_SET) != 0) {
        genClose(fd);
        return WLE_IGR_HELPER_OUTDATED;
    }

    while (done < helper_size) {
        int want = helper_size - done > (int)sizeof(verify_buf)
                       ? (int)sizeof(verify_buf)
                       : helper_size - done;
        int r = genRead(fd, verify_buf, want);
        if (r != want) {
            genClose(fd);
            return WLE_IGR_HELPER_ERROR;
        }
        if (memcmp(verify_buf, helper + done, want) != 0) {
            genClose(fd);
            return WLE_IGR_HELPER_OUTDATED;
        }
        done += r;
    }
    if (genClose(fd) < 0)
        return WLE_IGR_HELPER_ERROR;
    return WLE_IGR_HELPER_CURRENT;
}

static int inspect_boot_image_helper(const char *image, const u8 *helper, int helper_size)
{
    FILEINFO file;
    char dir[MAX_PATH], target[MAX_PATH], msg[128];
    int slot, status;

    if (!split_image_path(image, dir, sizeof(dir), &file))
        return WLE_IGR_HELPER_ERROR;
    slot = !vmcMounted[0] ? 0 : (!vmcMounted[1] ? 1 : -1);
    if (slot < 0)
        return WLE_IGR_HELPER_ERROR;
    if (filerMountVmc(dir, &file, slot, msg, sizeof(msg)) < 0)
        return WLE_IGR_HELPER_ERROR;

    snprintf(target, sizeof(target), "vmc%d:/BOOT/%s", slot, WLE_IGR_HELPER_NAME);
    status = inspect_helper_path(target, helper, helper_size);
    filerUnmountVmc(slot);
    return status;
}

static int install_into_boot_image(const char *image, const u8 *helper, int helper_size)
{
    FILEINFO file;
    char dir[MAX_PATH], vmc_root[8], boot_dir[MAX_PATH], target[MAX_PATH], msg[128];
    int slot;

    if (!split_image_path(image, dir, sizeof(dir), &file))
        return -1;

    slot = !vmcMounted[0] ? 0 : (!vmcMounted[1] ? 1 : -1);
    if (slot < 0)
        return -2;

    if (filerMountVmc(dir, &file, slot, msg, sizeof(msg)) < 0)
        return -1;

    snprintf(vmc_root, sizeof(vmc_root), "vmc%d:", slot);
    snprintf(boot_dir, sizeof(boot_dir), "%s/BOOT", vmc_root);
    if (genMkdir(boot_dir, fileMode) < 0) {
        char probe[MAX_PATH];
        int d;
        snprintf(probe, sizeof(probe), "%s", boot_dir);
        d = genDopen(probe);
        if (d < 0) {
            filerUnmountVmc(slot);
            return -1;
        }
        genDclose(d);
    }

    snprintf(target, sizeof(target), "%s/%s", boot_dir, WLE_IGR_HELPER_NAME);
    if (write_verified(target, helper, helper_size) < 0) {
        filerUnmountVmc(slot);
        return -1;
    }

    filerUnmountVmc(slot);
    return 0;
}

static int remove_from_boot_image(const char *image)
{
    FILEINFO file;
    char dir[MAX_PATH], target[MAX_PATH], msg[128];
    int slot, r;

    if (!split_image_path(image, dir, sizeof(dir), &file))
        return -1;

    slot = !vmcMounted[0] ? 0 : (!vmcMounted[1] ? 1 : -1);
    if (slot < 0)
        return -2;
    if (filerMountVmc(dir, &file, slot, msg, sizeof(msg)) < 0)
        return -1;

    snprintf(target, sizeof(target), "vmc%d:/BOOT/%s", slot, WLE_IGR_HELPER_NAME);
    if (!wleExists(target)) {
        filerUnmountVmc(slot);
        return 0;
    }

    r = genRemove(target);
    if (r >= 0 && wleExists(target))
        r = -1;
    filerUnmountVmc(slot);
    return r < 0 ? -1 : 1;
}


static int is_mc_path(const char *path)
{
    return path != NULL &&
           ((!strncmp(path, "mc0:", 4)) || (!strncmp(path, "mc1:", 4)));
}

static int is_usb_path(const char *path)
{
    if (path == NULL)
        return 0;
    return !strncmp(path, "mass:", 5) ||
           (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':') ||
           !strncmp(path, "usb:", 4) ||
           (!strncmp(path, "usb", 3) && path[3] >= '0' && path[3] <= '9' && path[4] == ':');
}

static int parent_dir(const char *path, char *out, size_t out_size)
{
    const char *slash, *colon;
    size_t n;

    if (path == NULL || out == NULL || out_size == 0)
        return 0;
    slash = strrchr(path, '/');
    if (slash != NULL)
        n = slash - path + 1;
    else {
        colon = strrchr(path, ':');
        if (colon == NULL)
            return 0;
        n = colon - path + 1;
    }
    if (n >= out_size)
        return 0;
    memcpy(out, path, n);
    out[n] = '\0';
    return 1;
}

static int install_direct_helper(const char *boot, const u8 *helper, int helper_size,
                                 char *target, size_t target_size)
{
    char dir[MAX_PATH];

    if (is_mc_path(boot)) {
        snprintf(dir, sizeof(dir), "%c%c%c%c/BOOT",
                 boot[0], boot[1], boot[2], boot[3]);
        if (genMkdir(dir, fileMode) < 0) {
            char probe[MAX_PATH];
            int d;
            snprintf(probe, sizeof(probe), "%s", dir);
            d = genDopen(probe);
            if (d < 0)
                return -1;
            genDclose(d);
        }
        return snprintf(target, target_size, "%s/%s", dir, WLE_IGR_HELPER_NAME) < (int)target_size &&
               write_verified(target, helper, helper_size) == 0 ? 0 : -1;
    }

    if (is_usb_path(boot) && parent_dir(boot, dir, sizeof(dir)))
        return snprintf(target, target_size, "%s%s", dir, WLE_IGR_HELPER_NAME) < (int)target_size &&
               write_verified(target, helper, helper_size) == 0 ? 0 : -1;

    return -1;
}

static int install_helper_on_mc_slot(int slot, const u8 *helper, int helper_size,
                                     char *target, size_t target_size)
{
    char root[8], boot_dir[MAX_PATH], probe[MAX_PATH];
    int d;

    if (slot < 0 || slot > 1)
        return -1;
    snprintf(root, sizeof(root), "mc%d:", slot);
    snprintf(probe, sizeof(probe), "%s", root);
    d = genDopen(probe);
    if (d < 0)
        return -1;
    genDclose(d);

    snprintf(boot_dir, sizeof(boot_dir), "%s/BOOT", root);
    if (genMkdir(boot_dir, fileMode) < 0) {
        snprintf(probe, sizeof(probe), "%s", boot_dir);
        d = genDopen(probe);
        if (d < 0)
            return -1;
        genDclose(d);
    }

    if (snprintf(target, target_size, "%s/%s", boot_dir, WLE_IGR_HELPER_NAME) >= (int)target_size)
        return -1;
    return write_verified(target, helper, helper_size);
}

static int install_helper_on_any_mc(const u8 *helper, int helper_size,
                                    char *target, size_t target_size)
{
    int slot;

    for (slot = 0; slot < 2; slot++)
        if (install_helper_on_mc_slot(slot, helper, helper_size, target, target_size) == 0)
            return slot;
    return -1;
}

static int is_bdm_relaunch_path(const char *boot)
{
    return boot != NULL &&
           (!strncmp(boot, "mx4sio", 6) ||
            !strncmp(boot, "ilink", 5) ||
            !strncmp(boot, "ata", 3) ||
            !strncmp(boot, "hdd", 3));
}



static int direct_helper_target(const char *boot, char *target, size_t target_size)
{
    char dir[MAX_PATH];

    if (is_mc_path(boot))
        return snprintf(target, target_size, "%c%c%c%c/BOOT/%s",
                        boot[0], boot[1], boot[2], boot[3], WLE_IGR_HELPER_NAME) < (int)target_size;
    if (is_usb_path(boot) && parent_dir(boot, dir, sizeof(dir)))
        return snprintf(target, target_size, "%s%s", dir, WLE_IGR_HELPER_NAME) < (int)target_size;
    return 0;
}


int wleIgrConfigure(const char *backup_root, WleIgrInstallStats *stats,
                    char *result, size_t result_size)
{
    const char *boot = wleBootElfPath();
    char app[WLE_IGR_PATH], root[MAX_PATH];
    u8 *helper;
    int helper_size = 0;
    int unit, i;

    if (stats == NULL || result == NULL || result_size == 0 ||
        backup_root == NULL || backup_root[0] == '\0')
        return -1;
    {
        static const char *const card_roots[] = {
            "mmce0:/MemoryCards",
            "mmce1:/MemoryCards"
        };
        int root_i;

        for (root_i = 0; root_i < 2; root_i++) {
            size_t n = strlen(card_roots[root_i]);
            if (!strncmp(backup_root, card_roots[root_i], n) &&
                (backup_root[n] == '\0' || backup_root[n] == '/')) {
                snprintf(result, result_size,
                         "Choose a backup destination outside the MMCE MemoryCards tree.");
                return -4;
            }
        }
    }
    memset(stats, 0, sizeof(*stats));
    result[0] = '\0';

    if (boot == NULL || boot[0] == '\0')
        return -1;

    portable_mmce_path(boot, app, sizeof(app));
    helper = patched_helper(app, backup_root, &helper_size);
    if (helper == NULL)
        return -1;

    if (is_mc_path(boot) || is_usb_path(boot)) {
        char target[MAX_PATH];
        int r = install_direct_helper(boot, helper, helper_size, target, sizeof(target));
        free(helper);
        if (r < 0) {
            snprintf(result, result_size, "The verified IGR helper could not be installed.");
            return -1;
        }
        stats->boot_images = 1;
        stats->installed = 1;
        snprintf(result, result_size,
                 "Installed and verified the IGR helper. Set OPL Exit to: %s", target);
        return 0;
    }

    if (is_bdm_relaunch_path(boot)) {
        char target[MAX_PATH];
        int slot = install_helper_on_any_mc(helper, helper_size, target, sizeof(target));
        free(helper);
        if (slot < 0) {
            snprintf(result, result_size,
                     "MX4SIO/iLink/ATA/HDD IGR needs a writable physical memory card in either slot for OPL's Exit-to helper.");
            return -2;
        }
        stats->boot_images = 1;
        stats->installed = 1;
        snprintf(result, result_size,
                 "Installed and verified the IGR trampoline on memory-card slot %d. Set OPL Exit to: %s",
                 slot + 1, target);
        return 0;
    }

    if (strncmp(boot, "mmce0:", 6) && strncmp(boot, "mmce1:", 6)) {
        free(helper);
        snprintf(result, result_size,
                 "This boot device still needs a trampoline storage driver before OPL IGR can relaunch it.");
        return -3;
    }

    boot_image_count = 0;
    if (!loadMmceModules()) {
        free(helper);
        return -1;
    }
    for (unit = 0; unit < 2; unit++) {
        snprintf(root, sizeof(root), "mmce%d:/MemoryCards", unit);
        collect_boot_images(root, 0);
    }

    stats->boot_images = boot_image_count;
    for (i = 0; i < boot_image_count; i++) {
        int r = install_into_boot_image(boot_images[i].path, helper, helper_size);
        if (r == 0)
            stats->installed++;
        else
            stats->failed++;
    }
    free(helper);

    if (stats->boot_images == 0) {
        snprintf(result, result_size,
                 "No BootCard .mcd images were found. Create/enable an MMCE BootCard first.");
        return -2;
    }
    if (stats->failed) {
        snprintf(result, result_size,
                 "Installed verified helper into %d/%d BootCard images; %d failed.",
                 stats->installed, stats->boot_images, stats->failed);
        return -1;
    }

    snprintf(result, result_size,
             "Installed verified helper into %d BootCard image%s. Set OPL Exit to mc0:/BOOT/%s for MMCE slot 1, or mc1:/BOOT/%s for slot 2.",
             stats->installed, stats->installed == 1 ? "" : "s",
             WLE_IGR_HELPER_NAME, WLE_IGR_HELPER_NAME);
    return 0;
}

int wleIgrRemove(WleIgrInstallStats *stats, char *result, size_t result_size)
{
    const char *boot = wleBootElfPath();
    char target[MAX_PATH], root[MAX_PATH];
    int unit, i;

    if (stats == NULL || result == NULL || result_size == 0 || boot == NULL)
        return -1;
    memset(stats, 0, sizeof(*stats));
    result[0] = '\0';

    if (is_mc_path(boot) || is_usb_path(boot)) {
        if (!direct_helper_target(boot, target, sizeof(target))) {
            snprintf(result, result_size, "Could not resolve the installed helper path.");
            return -1;
        }
        stats->boot_images = 1;
        if (!wleExists(target)) {
            snprintf(result, result_size, "No WLE-IGR helper is installed at %s", target);
            return 0;
        }
        if (genRemove(target) < 0 || wleExists(target)) {
            stats->failed = 1;
            snprintf(result, result_size, "The IGR helper could not be removed from %s", target);
            return -1;
        }
        stats->removed = 1;
        snprintf(result, result_size, "Removed WLE-IGR helper from %s", target);
        return 0;
    }

    if (is_bdm_relaunch_path(boot)) {
        int slot;
        stats->boot_images = 2;
        for (slot = 0; slot < 2; slot++) {
            snprintf(target, sizeof(target), "mc%d:/BOOT/%s", slot, WLE_IGR_HELPER_NAME);
            if (!wleExists(target))
                continue;
            if (genRemove(target) < 0 || wleExists(target))
                stats->failed++;
            else
                stats->removed++;
        }
        if (stats->failed) {
            snprintf(result, result_size,
                     "Removed %d helper copy/copies; %d memory-card removal failed.",
                     stats->removed, stats->failed);
            return -1;
        }
        snprintf(result, result_size,
                 "Removed %d WLE-IGR helper copy/copies from physical memory cards.",
                 stats->removed);
        return 0;
    }

    if (strncmp(boot, "mmce0:", 6) && strncmp(boot, "mmce1:", 6)) {
        snprintf(result, result_size, "This boot device does not use a managed WLE-IGR helper yet.");
        return -3;
    }

    if (!loadMmceModules())
        return -1;
    boot_image_count = 0;
    for (unit = 0; unit < 2; unit++) {
        snprintf(root, sizeof(root), "mmce%d:/MemoryCards", unit);
        collect_boot_images(root, 0);
    }
    stats->boot_images = boot_image_count;

    for (i = 0; i < boot_image_count; i++) {
        int r = remove_from_boot_image(boot_images[i].path);
        if (r > 0)
            stats->removed++;
        else if (r < 0)
            stats->failed++;
    }

    if (stats->failed) {
        snprintf(result, result_size,
                 "Removed helper from %d/%d BootCard images; %d failed.",
                 stats->removed, stats->boot_images, stats->failed);
        return -1;
    }
    snprintf(result, result_size,
             "Removed WLE-IGR helper from %d BootCard image%s.",
             stats->removed, stats->removed == 1 ? "" : "s");
    return 0;
}



int wleIgrStatus(const char *backup_root, WleIgrStatusStats *stats)
{
    const char *boot = wleBootElfPath();
    char app[WLE_IGR_PATH], target[MAX_PATH], root[MAX_PATH];
    u8 *helper;
    int helper_size = 0;
    int status, unit, i;

    if (stats == NULL)
        return WLE_IGR_STATUS_UNKNOWN;
    memset(stats, 0, sizeof(*stats));

    /* The helper embeds both the application path and backup destination.
     * Without the latter, this build cannot decide whether an installed
     * helper is current without guessing. */
    if (boot == NULL || boot[0] == '\0' ||
        backup_root == NULL || backup_root[0] == '\0')
        return WLE_IGR_STATUS_UNKNOWN;

    portable_mmce_path(boot, app, sizeof(app));
    helper = patched_helper(app, backup_root, &helper_size);
    if (helper == NULL)
        return WLE_IGR_STATUS_UNKNOWN;

    if (is_mc_path(boot) || is_usb_path(boot)) {
        if (!direct_helper_target(boot, target, sizeof(target))) {
            free(helper);
            return WLE_IGR_STATUS_UNKNOWN;
        }
        stats->copies = 1;
        status = inspect_helper_path(target, helper, helper_size);
        if (status == WLE_IGR_HELPER_CURRENT) stats->current++;
        else if (status == WLE_IGR_HELPER_OUTDATED) stats->outdated++;
        else if (status == WLE_IGR_HELPER_MISSING) stats->missing++;
        else stats->failed++;
        free(helper);

        if (stats->current) return WLE_IGR_STATUS_CURRENT;
        if (stats->outdated) return WLE_IGR_STATUS_OUTDATED;
        if (stats->missing) return WLE_IGR_STATUS_NOT_INSTALLED;
        return WLE_IGR_STATUS_UNKNOWN;
    }

    if (is_bdm_relaunch_path(boot)) {
        /* Setup installs one trampoline on the first writable physical MC.
         * A missing second slot is therefore normal, not "partial". */
        for (i = 0; i < 2; i++) {
            snprintf(target, sizeof(target), "mc%d:/BOOT/%s", i, WLE_IGR_HELPER_NAME);
            status = inspect_helper_path(target, helper, helper_size);
            stats->copies++;
            if (status == WLE_IGR_HELPER_CURRENT) stats->current++;
            else if (status == WLE_IGR_HELPER_OUTDATED) stats->outdated++;
            else if (status == WLE_IGR_HELPER_MISSING) stats->missing++;
            else stats->failed++;
        }
        free(helper);

        if (stats->current && stats->outdated) return WLE_IGR_STATUS_PARTIAL;
        if (stats->current) return WLE_IGR_STATUS_CURRENT;
        if (stats->outdated) return WLE_IGR_STATUS_OUTDATED;
        if (stats->failed && stats->missing < stats->copies) return WLE_IGR_STATUS_UNKNOWN;
        return WLE_IGR_STATUS_NOT_INSTALLED;
    }

    if (strncmp(boot, "mmce0:", 6) && strncmp(boot, "mmce1:", 6)) {
        free(helper);
        return WLE_IGR_STATUS_UNKNOWN;
    }

    /* Read-only by design: never start MMCE merely to populate a Settings
     * status row. If the stack is not already live, report Unknown. */
    if (!isPathDeviceStackLoaded("mmce0:/") &&
        !isPathDeviceStackLoaded("mmce1:/")) {
        free(helper);
        return WLE_IGR_STATUS_UNKNOWN;
    }

    boot_image_count = 0;
    for (unit = 0; unit < 2; unit++) {
        snprintf(root, sizeof(root), "mmce%d:/MemoryCards", unit);
        collect_boot_images(root, 0);
    }
    if (boot_image_count == 0) {
        free(helper);
        return WLE_IGR_STATUS_NOT_INSTALLED;
    }

    stats->copies = boot_image_count;
    for (i = 0; i < boot_image_count; i++) {
        status = inspect_boot_image_helper(boot_images[i].path, helper, helper_size);
        if (status == WLE_IGR_HELPER_CURRENT) stats->current++;
        else if (status == WLE_IGR_HELPER_OUTDATED) stats->outdated++;
        else if (status == WLE_IGR_HELPER_MISSING) stats->missing++;
        else stats->failed++;
    }
    free(helper);

    if (stats->failed)
        return (stats->current || stats->outdated || stats->missing)
                   ? WLE_IGR_STATUS_PARTIAL
                   : WLE_IGR_STATUS_UNKNOWN;
    if (stats->current == stats->copies)
        return WLE_IGR_STATUS_CURRENT;
    if (stats->outdated == stats->copies)
        return WLE_IGR_STATUS_OUTDATED;
    if (stats->missing == stats->copies)
        return WLE_IGR_STATUS_NOT_INSTALLED;
    return WLE_IGR_STATUS_PARTIAL;
}
