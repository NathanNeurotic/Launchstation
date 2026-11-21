#include "launchelf.h"
#include "dash_internal.h"
#include "hdd_install.h"
#include "install_ext2.h"

/* Match DP's creation order, using the standard driver's 128 MiB minimum.
 * All volumes are single APA extents, so Linux's 4 MiB main-header mapping
 * is unambiguous. The UI checks HDIOC_MAXSECTOR before any download/erase. */
static const char *const linux_names[8] = {
    "__linux.1", "__linux.2", "__linux.4", "__linux.5",
    "__linux.6", "__linux.7", "__linux.9", "__linux.8"
};
static const unsigned linux_mb[8] = {512, 128, 512, 512, 128, 256, 2048, 1024};
static const char *const pfs_names[3] = {"__system", "__sysconf", "__contents"};
static uint32_t le32(const unsigned char *p)
{ return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int hddInstallPsbbnPartitions(HddInstall *in)
{
    char path[96];
    int ret;
    if (!in->jail) return -EINVAL;
    for (unsigned i = 0; i < 8; i++) {
        iox_stat_t st;
        ret = hddCheckAllocation(linux_mb[i] * 2048u);
        if (ret < 0) return ret;
        dashWorkerStatus("Creating PSBBN partitions", i, 9);
        snprintf(path, sizeof(path), "hdd0:%s,,,%uM,%s", linux_names[i], linux_mb[i], i == 1 ? "EXT2SWAP" : "EXT2");
        ret = fileXioOpen(path, FIO_O_CREAT | FIO_O_EXCL | FIO_O_RDWR, 0);
        if (ret < 0) return ret;
        ret = fileXioClose(ret); if (ret < 0) return ret;
        snprintf(path, sizeof(path), "hdd0:%s", linux_names[i]);
        ret = fileXioGetStat(path, &st); if (ret < 0) return ret;
        if (st.private_0 || st.attr || st.size != linux_mb[i] * 2048u || st.mode != (i == 1 ? 0x82 : 0x83) ||
            st.private_5 < 2048 || st.private_5 > 131072u * 2048 - st.size) return -EINVAL;
        in->linux[i].start = st.private_5;
        in->linux[i].sectors = st.size;
    }
    ret = hddMakeFilesystem(1024, "__contents", FS_GROUP_APPLICATION);
    if (ret != 1024) return ret < 0 ? ret : -ENOSPC;
    ret = fileXioMount("pfs0:", "hdd0:__common", FIO_MT_RDWR);
    if (ret < 0) return ret;
    ret = genMkdir("pfs0:/Your Saves", 0777);
    { int closed = fileXioUmount("pfs0:"); if (ret >= 0) ret = closed; }
    return ret < 0 ? ret : fileXioDevctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0);
}

typedef struct {
    HddInstall *in;
    InstallExt2 *fs[8];
    int input, output, route, preflight;
    unsigned entries, required;
    uint32_t blocks[11], inodes[11];
    char current[INSTALL_TAR_PATH];
} Payload;
static int routePath(const char *name, const char **relative)
{
    const char *slash = strchr(name, '/');
    unsigned n = slash ? (unsigned)(slash - name) : strlen(name);
    *relative = slash ? slash + 1 : name + n;
    if (!n) return -1;
    for (unsigned i = 0; i < 8; i++) if (strlen(linux_names[i]) == n && !memcmp(name, linux_names[i], n)) return i;
    for (unsigned i = 0; i < 3; i++) if (strlen(pfs_names[i]) == n && !memcmp(name, pfs_names[i], n)) return i + 8;
    return -2;
}
static int mkdirParents(const char *path)
{
    char buf[MAX_PATH];
    iox_stat_t st;
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return -ENAMETOOLONG;
    strcpy(buf, path);
    for (char *p = strchr(buf, ':'); p && *p; p++) if (*p == '/' && p[1]) {
        *p = 0;
        if (p > buf && p[-1] != ':') {
            int ret = genMkdir(buf, 0777);
            if (ret < 0 && (fileXioGetStat(buf, &st) < 0 || !FIO_S_ISDIR(st.mode))) return ret;
        }
        *p = '/';
    }
    return 0;
}
static int begin(void *context, const InstallTarEntry *entry)
{
    Payload *p = context;
    InstallTarEntry e = *entry;
    const char *relative, *link;
    int r = routePath(entry->name, &relative), ret;
    p->route = r;
    if (r == -1) return entry->type == '5' ? 0 : -EINVAL;
    if (r < 0 || r == 1 || r == 7) return -EINVAL;
    if (entry->type == '1') {
        if (routePath(entry->link, &link) != r || !*link) return -EINVAL;
        memmove(e.link, link, strlen(link) + 1);
    }
    if (r >= 8 && ((entry->type != '0' && entry->type != '5') || strchr(relative, ':') || strchr(relative, '\\'))) return -EINVAL;
    if (p->preflight) {
        static const char *const required[] = {
            "__system/p2lboot/osdboot.elf", "__system/p2lboot/vmlinux",
            "__linux.1/sbin/init", "__linux.1/bin/sh", "__linux.1/etc/fstab",
            "__linux.4/bn/script/utility/sysconf.xml"
        };
        for (unsigned i = 0; i < 6; i++) if (!strcmp(entry->name, required[i]) &&
            ((entry->type == '0' && entry->size) || (i == 3 && entry->type == '2' && *entry->link))) p->required |= 1u << i;
        for (unsigned i = 0; i < 7; i++) if (r == 8 && !strncmp(relative, "osd100/", 7) &&
            !strcmp(relative + 7, hddInstallOsdFiles[i]) && entry->type == '0' && entry->size) p->required |= 1u << (i + 6);
        /* These are installed separately from the commit-pinned repository,
         * and configs are generated from this install's actual paths. */
        if (entry->type != '5' && (r == 8 || r == 9) && !strncmp(relative, "osdmenu/", 8)) return -EINVAL;
        /* Conservative accounting: count language replacements a second time,
         * and charge one inode plus two indirect blocks for every entry. */
        p->inodes[r]++;
        p->blocks[r] += (entry->size + 4095) / 4096 + (entry->type == '5') + 2;
        if (r < 8 && (p->inodes[r] > (linux_mb[r] / 128) * 8192 - 10 ||
            p->blocks[r] > (linux_mb[r] - 4) * 256 - (linux_mb[r] / 128) * 260)) return -ENOSPC;
        if (r >= 8 && p->blocks[r] > 32u * 256) return -EFBIG;
        return 0;
    }
    if (!(p->entries++ % 64)) dashWorkerStatus("Installing PSBBN DP", p->entries, 0);
    snprintf(p->current, sizeof(p->current), "%s", entry->name);
    if (r < 8) {
        memmove(e.name, relative, strlen(relative) + 1);
        return installExt2Begin(p->fs[r], &e);
    }
    if (!*relative) return entry->type == '5' ? 0 : -EINVAL;
    if (snprintf(e.name, sizeof(e.name), "pfs%u:/%s", r - 8, relative) >= (int)sizeof(e.name)) return -ENAMETOOLONG;
    ret = mkdirParents(e.name); if (ret < 0) return ret;
    if (entry->type == '5') {
        iox_stat_t st;
        ret = genMkdir(e.name, 0777);
        if (ret < 0 && fileXioGetStat(e.name, &st) >= 0 && FIO_S_ISDIR(st.mode)) ret = 0;
        return ret;
    }
    p->output = genOpen(e.name, FIO_O_CREAT | FIO_O_TRUNC | FIO_O_WRONLY);
    return p->output < 0 ? p->output : 0;
}
static int data(void *context, const void *bytes, unsigned size)
{
    Payload *p = context;
    unsigned done = 0;
    if (p->preflight) return 0;
    if (p->route >= 0 && p->route < 8) return installExt2Data(p->fs[p->route], bytes, size);
    if (p->output < 0) return -EINVAL;
    while (done < size) {
        int n = genWrite(p->output, (void *)((const unsigned char *)bytes + done), size - done);
        if (n <= 0 || (unsigned)n > size - done) return n < 0 ? n : -EIO;
        done += n;
    }
    return 0;
}
static int end(void *context)
{
    Payload *p = context;
    if (p->preflight) return 0;
    if (p->route >= 0 && p->route < 8) return installExt2End(p->fs[p->route]);
    if (p->output >= 0) { int ret = genClose(p->output); p->output = -1; return ret < 0 ? ret : 0; }
    return 0;
}
static int readArchive(void *context, uint32_t offset, void *bytes, unsigned size)
{
    Payload *p = context;
    unsigned done = 0;
    if (genLseek(p->input, offset, SEEK_SET) != offset) return -EIO;
    while (done < size) {
        int n = genRead(p->input, (unsigned char *)bytes + done, size - done);
        if (n <= 0 || (unsigned)n > size - done) return n < 0 ? n : -EIO;
        done += n;
    }
    return 0;
}
static int archives(Payload *p)
{
    InstallArchiveIo io = {p, readArchive, NULL};
    InstallTarSink sink = {p, begin, data, end};
    char path[MAX_PATH];
    int ret = 0;
    for (unsigned i = HI_SYSTEM; i <= HI_LANGUAGE; i++) {
        if (snprintf(path, sizeof(path), "%s/%s", p->in->stage, hddInstallAssetName(i)) >= (int)sizeof(path)) return -ENAMETOOLONG;
        p->input = genOpen(path, FIO_O_RDONLY); if (p->input < 0) return p->input;
        dashWorkerStatus(p->preflight ? "Checking PSBBN archives" : "Installing PSBBN archives", i - HI_SYSTEM, 2);
        ret = installTarGzip(&io, p->in->sizes[i], &sink);
        { int closed = genClose(p->input); if (ret >= 0) ret = closed; }
        p->input = -1;
        if (p->output >= 0) { int closed = genClose(p->output); p->output = -1; if (ret >= 0) ret = closed; }
        if (ret < 0) break;
    }
    return ret;
}
int hddInstallPsbbnPreflight(HddInstall *in)
{
    Payload *p = calloc(1, sizeof(*p));
    int ret;
    if (!p) return -ENOMEM;
    p->in = in; p->preflight = 1; p->input = p->output = -1;
    ret = archives(p);
    if (ret >= 0 && p->required != (1u << 13) - 1) ret = -EINVAL;
    free(p); return ret;
}
int hddInstallPsbbnPayload(void *context, const HddFormatIo *io)
{
    HddInstall *in = context;
    Payload *p;
    int ret = -EINVAL, mounts = 0;
    unsigned char header[1024];
    if (!in || !in->jail || !(in->flags & HDD_INSTALL_PSBBN)) return -EINVAL;
    p = calloc(1, sizeof(*p)); if (!p) return -ENOMEM;
    p->in = in; p->input = p->output = -1;
    for (unsigned i = 0; i < 8; i++) {
        uint32_t sum = 0, start = in->linux[i].start, sectors = in->linux[i].sectors;
        /* Revalidate each exact extent through raw I/O before writing it. */
        if (sectors != linux_mb[i] * 2048u || start < 2048 || start > 131072u * 2048 - sectors) { ret = -EINVAL; goto done; }
        ret = io->read(io->context, start, 2, header); if (ret < 0) goto done;
        for (unsigned w = 1; w < 256; w++) sum += le32(header + 4 * w);
        if (sum != le32(header) || le32(header + 4) != 0x00415041 ||
            memcmp(header + 16, linux_names[i], strlen(linux_names[i]) + 1) ||
            le32(header + 64) != start || le32(header + 68) != sectors ||
            le32(header + 72) != (i == 1 ? 0x82u : 0x83u) || le32(header + 76)) { ret = -EINVAL; goto done; }
        dashWorkerStatus("Preparing PSBBN filesystems", i, 8);
        if (i == 1) ret = installPsbbnSwap(io, start + 8192, sectors - 8192);
        else if (i == 7) ret = installPsbbnMusic(io, start + 8192, sectors - 8192, (uint32_t)time(NULL));
        else ret = installExt2Create(&p->fs[i], io, start + 8192, sectors - 8192, (uint32_t)time(NULL));
        if (ret < 0) goto done;
    }
    for (unsigned i = 0; i < 3; i++) {
        char mount[8], partition[32];
        snprintf(mount, sizeof(mount), "pfs%u:", i); snprintf(partition, sizeof(partition), "hdd0:%s", pfs_names[i]);
        ret = fileXioMount(mount, partition, FIO_MT_RDWR); if (ret < 0) goto done;
        mounts++;
    }
    ret = archives(p); if (ret < 0) goto done;
    /* Tar overlays never supply our generated component-specific settings. */
    ret = hddInstallWriteConfigs(in); if (ret < 0) goto done;
    for (unsigned i = 0; i < 8; i++) if (p->fs[i]) { ret = installExt2Finish(p->fs[i]); if (ret < 0) goto done; }
    ret = io->flush(io->context);
done:
    while (mounts) {
        char mount[8]; int closed;
        snprintf(mount, sizeof(mount), "pfs%u:", --mounts); closed = fileXioUmount(mount);
        if (ret >= 0 && closed < 0) ret = closed;
    }
    for (unsigned i = 0; i < 8; i++) installExt2Free(p->fs[i]);
    if (ret < 0) snprintf(in->error, sizeof(in->error), "PSBBN installation failed (error %d): %.160s. Boot MBR was not activated.", ret, p->current);
    free(p); return ret;
}
