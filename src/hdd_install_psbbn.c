#include "launchelf.h"
#include "dash_internal.h"
#include "hdd_install.h"
#include "install_ext2.h"

/* Match DP's creation order, using the standard driver's 128 MiB minimum.
 * All volumes are single APA extents, so Linux's 4 MiB main-header mapping
 * is unambiguous. The UI checks HDIOC_MAXSECTOR before any download/erase. */
static const char *const linux_names[HDD_INSTALL_LINUX_PARTITIONS] = {
    "__linux.1", "__linux.2", "__linux.4", "__linux.5",
    "__linux.6", "__linux.7", "__linux.9", "__linux.8", "__linux.3", "__linux.10"
};
static const char *const pfs_names[3] = {"__system", "__sysconf", "__contents"};
static uint32_t le32(const unsigned char *p)
{ return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int hddInstallPsbbnPartitions(HddInstall *in)
{
    char path[96];
    int ret;
    if (!in->jail) return -EINVAL;
    for (unsigned i = 0; i < HDD_INSTALL_LINUX_PARTITIONS; i++) {
        unsigned mb = hddInstallPartitionMb(in, i);
        iox_stat_t st;
        if (!mb) continue;
        hddInstallLog("Create %s: %u MiB type=%s", linux_names[i], mb, i == 1 ? "SWAP" : "EXT2");
        hddInstallLogFlush(in);
        ret = hddCheckAllocation(mb * 2048u);
        hddInstallLog("Allocation check %s: rc=%d", linux_names[i], ret);
        if (ret < 0) return ret;
        hddInstallStatus("Creating PSBBN partitions", i, in->flags & HDD_INSTALL_PS2LINUX ? 11 : 9);
        /* The stock driver's size lookup accepts 1G/2G, not 1024M/2048M. */
        snprintf(path, sizeof(path), "hdd0:%s,,,%u%c,%s", linux_names[i],
                 mb >= 1024 ? mb / 1024 : mb,
                 mb >= 1024 ? 'G' : 'M', i == 1 ? "EXT2SWAP" : "EXT2");
        hddInstallLog("OPEN %s", path);
        hddInstallLogFlush(in);
        ret = fileXioOpen(path, FIO_O_CREAT | FIO_O_EXCL | FIO_O_RDWR, 0);
        hddInstallLog("OPEN %s: rc=%d", linux_names[i], ret);
        if (ret < 0) return ret;
        ret = fileXioClose(ret);
        hddInstallLog("CLOSE %s: rc=%d", linux_names[i], ret);
        if (ret < 0) return ret;
        snprintf(path, sizeof(path), "hdd0:%s", linux_names[i]);
        ret = fileXioGetStat(path, &st);
        hddInstallLog("STAT %s: rc=%d", linux_names[i], ret);
        if (ret < 0) return ret;
        hddInstallLog("Partition %s: LBA=%u sectors=%u type=0x%x flags=0x%x subs=%u", linux_names[i],
                      (unsigned)st.private_5, (unsigned)st.size, (unsigned)st.mode, (unsigned)st.attr, (unsigned)st.private_0);
        if (st.private_0 || st.attr || st.size != mb * 2048u || st.mode != (i == 1 ? 0x82 : 0x83) ||
            st.private_5 < 2048 || st.private_5 > 131072u * 2048 - st.size) return -EINVAL;
        in->linux[i].start = st.private_5;
        in->linux[i].sectors = st.size;
    }
    ret = hddMakeFilesystem(in->contents_mb ? in->contents_mb : 1024, "__contents", FS_GROUP_APPLICATION);
    if (ret != (int)(in->contents_mb ? in->contents_mb : 1024)) return ret < 0 ? ret : -ENOSPC;
    ret = fileXioMount("pfs0:", "hdd0:__common", FIO_MT_RDWR);
    if (ret < 0) return ret;
    ret = genMkdir("pfs0:/Your Saves", 0777);
    { int closed = fileXioUmount("pfs0:"); if (ret >= 0) ret = closed; }
    return ret < 0 ? ret : fileXioDevctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0);
}

typedef struct {
    char *name;
    unsigned char type;
} PreflightPath;

typedef struct {
    HddInstall *in;
    InstallExt2 *fs[HDD_INSTALL_LINUX_PARTITIONS];
    int input, output, route, preflight;
    unsigned entries, required, archive, linux_required;
    uint64_t archive_done, archive_total;
    uint32_t file_done, file_size;
    uint32_t blocks[HDD_INSTALL_LINUX_PARTITIONS + 3], inodes[HDD_INSTALL_LINUX_PARTITIONS + 3];
    char current[INSTALL_TAR_PATH];
    PreflightPath *paths;
    unsigned path_count, path_capacity;
} Payload;
static int routePath(const char *name, const char **relative)
{
    const char *slash = strchr(name, '/');
    unsigned n = slash ? (unsigned)(slash - name) : strlen(name);
    *relative = slash ? slash + 1 : name + n;
    if (!n) return -1;
    for (unsigned i = 0; i < HDD_INSTALL_LINUX_PARTITIONS; i++) if (strlen(linux_names[i]) == n && !memcmp(name, linux_names[i], n)) return i;
    for (unsigned i = 0; i < 3; i++) if (strlen(pfs_names[i]) == n && !memcmp(name, pfs_names[i], n)) return i + HDD_INSTALL_LINUX_PARTITIONS;
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
static int preflightPathType(Payload *p, const char *name)
{
    for (unsigned i = 0; i < 8; i++) if (!strcmp(name, linux_names[i])) return '5';
    for (unsigned i = 0; i < p->path_count; i++) if (!strcmp(name, p->paths[i].name)) return p->paths[i].type;
    return -1;
}

static int preflightPathAdd(Payload *p, const char *name, unsigned char type)
{
    if (p->path_count == p->path_capacity) {
        unsigned capacity = p->path_capacity ? p->path_capacity * 2 : 128;
        PreflightPath *grown = realloc(p->paths, capacity * sizeof(*grown));
        if (!grown) return -ENOMEM;
        p->paths = grown; p->path_capacity = capacity;
    }
    p->paths[p->path_count].name = malloc(strlen(name) + 1);
    if (!p->paths[p->path_count].name) return -ENOMEM;
    strcpy(p->paths[p->path_count].name, name);
    p->paths[p->path_count++].type = type;
    return 0;
}

static int preflightPath(Payload *p, const InstallTarEntry *entry)
{
    const char *slash;
    char parent[INSTALL_TAR_PATH];
    unsigned i;
    for (slash = strchr(entry->name, '/'); slash; slash = strchr(slash + 1, '/')) {
        size_t n = (size_t)(slash - entry->name);
        int type;
        if (n >= sizeof(parent)) return -ENAMETOOLONG;
        memcpy(parent, entry->name, n); parent[n] = 0;
        type = preflightPathType(p, parent);
        if (type < 0) {
            int ret = preflightPathAdd(p, parent, '5');
            if (ret < 0) return ret;
        } else if (type != '5') return -ENOTDIR;
    }
    for (i = 0; i < p->path_count; i++) if (!strcmp(p->paths[i].name, entry->name)) {
        if (entry->type != '5' && p->paths[i].type == '5') {
            size_t n = strlen(entry->name);
            for (unsigned j = 0; j < p->path_count; j++)
                if (!strncmp(p->paths[j].name, entry->name, n) && p->paths[j].name[n] == '/') return -EINVAL;
        }
        p->paths[i].type = entry->type;
        return 0;
    }
    return preflightPathAdd(p, entry->name, entry->type);
}

static int begin(void *context, const InstallTarEntry *entry)
{
    Payload *p = context;
    InstallTarEntry e = *entry;
    const char *relative, *link;
    int r, ret;
    if (p->archive == HI_PS2LINUX) { r = 8; relative = entry->name; }
    else r = routePath(entry->name, &relative);
    p->route = r;
    if (r == -1) return entry->type == '5' ? 0 : -EINVAL;
    if (r < 0 || r == 1 || r == 7 || (r < HDD_INSTALL_LINUX_PARTITIONS && !hddInstallPartitionMb(p->in, r))) return -EINVAL;
    if (p->archive != HI_PS2LINUX && (r == 8 || r == 9)) return -EINVAL;
    p->entries++;
    if (entry->type == '1') {
        if (p->archive == HI_PS2LINUX) link = entry->link;
        else if (routePath(entry->link, &link) != r) return -EINVAL;
        if (!*link) return -EINVAL;
        memmove(e.link, link, strlen(link) + 1);
    }
    if (r >= HDD_INSTALL_LINUX_PARTITIONS && ((entry->type != '0' && entry->type != '5') || strchr(relative, ':') || strchr(relative, '\\'))) return -EINVAL;
    if (p->preflight) {
        static const char *const required[] = {
            "__system/p2lboot/osdboot.elf", "__system/p2lboot/vmlinux",
            "__linux.1/sbin/init", "__linux.1/bin/sh", "__linux.1/etc/fstab",
            "__linux.4/bn/script/utility/sysconf.xml"
        };
        if (p->archive == HI_PS2LINUX) {
            if (!strcmp(entry->name, "sbin/init")) {
                if (entry->type == '0' && entry->size) p->linux_required |= 1;
                else p->linux_required &= ~1u;
            }
            if (!strcmp(entry->name, "bin/sh")) {
                if ((entry->type == '0' && entry->size) || (entry->type == '2' && *entry->link)) p->linux_required |= 2;
                else p->linux_required &= ~2u;
            }
        }
        for (unsigned i = 0; i < 6; i++) if (p->archive != HI_PS2LINUX && !strcmp(entry->name, required[i])) {
            int valid = (entry->type == '0' && entry->size) || (i == 3 && entry->type == '2' && *entry->link);
            if (valid) p->required |= 1u << i;
            else p->required &= ~(1u << i);
        }
        for (unsigned i = 0; i < 7; i++) if (r == HDD_INSTALL_LINUX_PARTITIONS && !strncmp(relative, "osd100/", 7) &&
            !strcmp(relative + 7, hddInstallOsdFiles[i])) {
            if (entry->type == '0' && entry->size) p->required |= 1u << (i + 6);
            else p->required &= ~(1u << (i + 6));
        }
        /* These are installed separately from the commit-pinned repository,
         * and configs are generated from this install's actual paths. */
        if (entry->type != '5' && (r == HDD_INSTALL_LINUX_PARTITIONS || r == HDD_INSTALL_LINUX_PARTITIONS + 1) && !strncmp(relative, "osdmenu/", 8)) return -EINVAL;
        if (r < HDD_INSTALL_LINUX_PARTITIONS) {
            if (entry->type == '1') {
                ret = preflightPathType(p, entry->link);
                if (ret < 0) return -ENOENT;
                if (ret == '5') return -EINVAL;
            }
            ret = preflightPath(p, entry);
            if (ret < 0) return ret;
        }
        /* Conservative accounting: count language replacements a second time,
         * and charge one inode plus two indirect blocks for every entry. */
        p->inodes[r]++;
        p->blocks[r] += (entry->size + 4095) / 4096 + (entry->type == '5') + 2;
        if (r < HDD_INSTALL_LINUX_PARTITIONS && (p->inodes[r] > (hddInstallPartitionMb(p->in, r) / 128) * 8192 - 10 ||
            p->blocks[r] > (hddInstallPartitionMb(p->in, r) - 4) * 256 - (hddInstallPartitionMb(p->in, r) / 128) * 260)) return -ENOSPC;
        if (r >= HDD_INSTALL_LINUX_PARTITIONS && p->blocks[r] > 32u * 256) return -EFBIG;
        return 0;
    }
    snprintf(p->current, sizeof(p->current), "%s", entry->name);
    p->file_done = 0; p->file_size = entry->size;
    hddInstallProgress(p->current, 0, p->file_size);
    if (r < HDD_INSTALL_LINUX_PARTITIONS) {
        memmove(e.name, relative, strlen(relative) + 1);
        return installExt2Begin(p->fs[r], &e);
    }
    if (!*relative) return entry->type == '5' ? 0 : -EINVAL;
    if (snprintf(e.name, sizeof(e.name), "pfs%u:/%s", r - HDD_INSTALL_LINUX_PARTITIONS, relative) >= (int)sizeof(e.name)) return -ENAMETOOLONG;
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
    if (p->route >= 0 && p->route < HDD_INSTALL_LINUX_PARTITIONS) {
        int ret = installExt2Data(p->fs[p->route], bytes, size);
        if (ret >= 0) {
            p->file_done += size;
            hddInstallProgress(p->current, p->file_done, p->file_size);
        }
        return ret;
    }
    if (p->output < 0) return -EINVAL;
    while (done < size) {
        int n = genWrite(p->output, (void *)((const unsigned char *)bytes + done), size - done);
        if (n <= 0 || (unsigned)n > size - done) return n < 0 ? n : -EIO;
        done += n;
        p->file_done += n;
        hddInstallProgress(p->current, p->file_done, p->file_size);
    }
    return 0;
}
static int end(void *context)
{
    Payload *p = context;
    if (p->preflight) return 0;
    if (p->route >= 0 && p->route < HDD_INSTALL_LINUX_PARTITIONS) return installExt2End(p->fs[p->route]);
    if (p->output >= 0) { int ret = genClose(p->output); p->output = -1; return ret < 0 ? ret : 0; }
    return 0;
}
static int readArchive(void *context, uint32_t offset, void *bytes, unsigned size)
{
    Payload *p = context;
    unsigned done = 0;
    if (genLseek(p->input, offset, SEEK_SET) != (s64)offset) return -EIO;
    while (done < size) {
        int n = genRead(p->input, (unsigned char *)bytes + done, size - done);
        if (n <= 0 || (unsigned)n > size - done) return n < 0 ? n : -EIO;
        done += n;
    }
    if (p->preflight) hddInstallProgress("Checking PSBBN archive", offset + size, p->in->sizes[p->archive]);
    if (p->archive_total) {
        uint64_t completed = p->archive_done + offset + size;
        if (p->preflight) hddInstallOverall(completed, p->archive_total);
        else hddInstallOverall(100 + 800 * completed / p->archive_total, 1000);
    }
    return 0;
}
static int archives(Payload *p)
{
    InstallArchiveIo io = {p, readArchive, NULL};
    InstallTarSink sink = {p, begin, data, end};
    char path[MAX_PATH];
    int ret = 0;
    unsigned assets[4] = {HI_SYSTEM, HI_LANGUAGE}, count = 2;
    if (hddInstallSelectedAsset(p->in, HI_CHANNELS)) assets[count++] = HI_CHANNELS;
    if (p->in->flags & HDD_INSTALL_PS2LINUX) assets[count++] = HI_PS2LINUX;
    p->archive_done = p->archive_total = 0;
    for (unsigned a = 0; a < count; a++) p->archive_total += p->in->sizes[assets[a]];
    for (unsigned a = 0; a < count; a++) {
        unsigned i = assets[a];
        if (snprintf(path, sizeof(path), "%s/%s", p->in->stage, hddInstallAssetName(i)) >= (int)sizeof(path)) return -ENAMETOOLONG;
        p->archive = i;
        p->input = genOpen(path, FIO_O_RDONLY); if (p->input < 0) return p->input;
        hddInstallStatus(p->preflight ? "Checking PSBBN archives" : "Installing PSBBN archives", a, count);
        hddInstallLog("%s %s (%u compressed bytes)", p->preflight ? "PREFLIGHT" : "EXTRACT", path, (unsigned)p->in->sizes[i]);
        hddInstallLogFlush(p->in);
        ret = installTarGzip(&io, p->in->sizes[i], &sink);
        hddInstallLog("ARCHIVE completed rc=%d entries=%u last=%s", ret, p->entries, p->current);
        hddInstallLogFlush(p->in);
        { int closed = genClose(p->input); if (ret >= 0) ret = closed; }
        p->input = -1;
        if (p->output >= 0) { int closed = genClose(p->output); p->output = -1; if (ret >= 0) ret = closed; }
        if (ret < 0) break;
        p->archive_done += p->in->sizes[i];
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
    if (ret >= 0 && (p->required != (1u << 13) - 1 || ((in->flags & HDD_INSTALL_PS2LINUX) && p->linux_required != 3))) ret = -EINVAL;
    for (unsigned i = 0; i < p->path_count; i++) free(p->paths[i].name);
    free(p->paths);
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
    for (unsigned i = 0; i < HDD_INSTALL_LINUX_PARTITIONS; i++) {
        if (!hddInstallPartitionMb(in, i)) continue;
        uint32_t sum = 0, start = in->linux[i].start, sectors = in->linux[i].sectors;
        /* Revalidate each exact extent through raw I/O before writing it. */
        if (sectors != hddInstallPartitionMb(in, i) * 2048u || start < 2048 || start > 131072u * 2048 - sectors) { ret = -EINVAL; goto done; }
        ret = io->read(io->context, start, 2, header); if (ret < 0) goto done;
        for (unsigned w = 1; w < 256; w++) sum += le32(header + 4 * w);
        hddInstallLog("APA header %s: checksum=0x%08x calculated=0x%08x start=%u length=%u type/flags=0x%x subs=%u",
                      linux_names[i], (unsigned)le32(header), (unsigned)sum, (unsigned)le32(header + 64),
                      (unsigned)le32(header + 68), (unsigned)le32(header + 72), (unsigned)le32(header + 76));
        if (sum != le32(header) || le32(header + 4) != 0x00415041 ||
            memcmp(header + 16, linux_names[i], strlen(linux_names[i]) + 1) ||
            le32(header + 64) != start || le32(header + 68) != sectors ||
            le32(header + 72) != (i == 1 ? 0x82u : 0x83u) || le32(header + 76)) { ret = -EINVAL; goto done; }
        hddInstallStatus("Preparing PSBBN filesystems", i, in->flags & HDD_INSTALL_PS2LINUX ? 10 : 8);
        hddInstallLog("MKFS %s: data LBA=%u sectors=%u", linux_names[i], (unsigned)(start + 8192), (unsigned)(sectors - 8192));
        hddInstallLogFlush(in);
        if (i == 1) ret = installPsbbnSwap(io, start + 8192, sectors - 8192);
        else if (i == 7) ret = installPsbbnMusic(io, start + 8192, sectors - 8192, (uint32_t)time(NULL));
        else ret = installExt2Create(&p->fs[i], io, start + 8192, sectors - 8192, (uint32_t)time(NULL));
        hddInstallLog("MKFS %s rc=%d", linux_names[i], ret);
        if (ret < 0) goto done;
    }
    for (unsigned i = 0; i < 3; i++) {
        char mount[8], partition[32];
        snprintf(mount, sizeof(mount), "pfs%u:", i); snprintf(partition, sizeof(partition), "hdd0:%s", pfs_names[i]);
        ret = fileXioMount(mount, partition, FIO_MT_RDWR); if (ret < 0) goto done;
        mounts++;
    }
    ret = hddInstallCheckAsset(in, HI_SYSTEM);
    if (ret >= 0) ret = hddInstallCheckAsset(in, HI_LANGUAGE);
    if (ret < 0) goto done;
    ret = archives(p); if (ret < 0) goto done;
    {
        InstallTarEntry entry = {0}; char shortcut[1024];
        ret = hddInstallGameShortcut(in, shortcut, sizeof(shortcut));
        if (ret < 0) goto done;
        strcpy(entry.name, "bn/sysconf/shortcut_0"); entry.type = '0';
        entry.size = ret; entry.mode = 0644; entry.mtime = (uint32_t)time(NULL);
        ret = installExt2Begin(p->fs[5], &entry);
        if (ret >= 0) ret = installExt2Data(p->fs[5], shortcut, entry.size);
        if (ret >= 0) ret = installExt2End(p->fs[5]);
        if (ret < 0) goto done;
    }
    /* Tar overlays never supply our generated component-specific settings. */
    ret = hddInstallWriteConfigs(in); if (ret < 0) goto done;
    for (unsigned i = 0; i < HDD_INSTALL_LINUX_PARTITIONS; i++) if (p->fs[i]) { ret = installExt2Finish(p->fs[i]); if (ret < 0) goto done; }
    ret = io->flush(io->context);
done:
    while (mounts) {
        char mount[8]; int closed;
        snprintf(mount, sizeof(mount), "pfs%u:", --mounts); closed = fileXioUmount(mount);
        if (ret >= 0 && closed < 0) ret = closed;
    }
    for (unsigned i = 0; i < HDD_INSTALL_LINUX_PARTITIONS; i++) installExt2Free(p->fs[i]);
    hddInstallLog("PSBBN population finished rc=%d entries=%u last=%s", ret, p->entries, p->current);
    hddInstallLogFlush(in);
    if (ret < 0) snprintf(in->error, sizeof(in->error), "PSBBN installation failed (error %d): %.160s. Boot MBR was not activated.", ret, p->current);
    free(p); return ret;
}
