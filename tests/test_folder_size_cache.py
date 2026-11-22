"""Run production folder measurements/cache/display against filesystem adapters."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

receiver = (root / "src/pc_game_receiver.c").read_text()
assert '#include "filer_size_cache.h"' in receiver
for signature, paths in (
    ("static void writeResult(", ("PC_RECV_RESULT",)),
    ("static int copyWithResume(", ("dest",)),
    ("static void receiveGames(", ("PC_RECV_STAGE_DIR", "dest")),
    ("int pcGameReceiverPoll(", ("PC_RECV_REQUEST",)),
):
    start = receiver.index(signature)
    end = receiver.index("\n}", start)
    body = receiver[start:end]
    for path in paths:
        assert f"filerSizeCacheInvalidate({path});" in body, (signature, path)


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


platform = r"""
#ifndef SIZE_TEST_PLATFORM
#define SIZE_TEST_PLATFORM
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef _WIN32
#define memalign(a, s) malloc(s)
#else
#include <malloc.h>
#endif
typedef uint32_t u32;
typedef uint64_t u64;
#define MAX_PATH 1025
#define MAX_NAME 256
#define MAX_ENTRY 2048
#define MOUNT_LIMIT 8
#define sceMcFileAttrSubdir 0x20
#define FIO_O_RDONLY 1
#define FIO_O_WRONLY 2
#define FIO_O_RDWR 3
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define DVRP 1
#define EXFAT 1
typedef struct { unsigned AttrFile, FileSizeByte, Reserve2; } McStat;
typedef struct { char name[MAX_NAME]; McStat stats; } FILEINFO;
typedef struct { unsigned size, hisize; } iox_stat_t;
typedef struct { int init_count, max_count; } ee_sema_t;
extern char mountedParty[MOUNT_LIMIT][MAX_NAME], mountedDVRPParty[MOUNT_LIMIT][MAX_NAME];
int CreateSema(ee_sema_t *);
int WaitSema(int);
int SignalSema(int);
unsigned getIopResetGeneration(void);
int getDirComplete(const char *, FILEINFO *, int *);
int ensurePathDeviceStackReady(const char *);
int getHddParty(const char *, const FILEINFO *, char *, char *);
int getHddDVRPParty(const char *, const FILEINFO *, char *, char *);
int mountParty(const char *);
int mountDVRPParty(const char *);
int genGetStat(const char *, iox_stat_t *);
#endif
"""

code = r"""
#include "platform.h"
#include "filer_size_cache.h"
static int held, creates, dir_reads, stat_reads, failed;
static unsigned iop_generation;
static u64 content_bytes = 5000;
char mountedParty[MOUNT_LIMIT][MAX_NAME], mountedDVRPParty[MOUNT_LIMIT][MAX_NAME];
int CreateSema(ee_sema_t *s) { assert(s->init_count == 1 && s->max_count == 1); creates++; return 7; }
int WaitSema(int s) { assert(s == 7 && !held); held = 1; return 0; }
int SignalSema(int s) { assert(s == 7 && held); held = 0; return 0; }
unsigned getIopResetGeneration(void) { return iop_generation; }
int ensurePathDeviceStackReady(const char *path) { assert(path && !held); return 1; }
static int sizeFromListingOnStatFailure(const char *path) { (void)path; return 0; }
static FILEINFO folder(const char *name) {
    FILEINFO f = {0};
    strcpy(f.name, name);
    f.stats.AttrFile = sceMcFileAttrSubdir;
    return f;
}
int getDirComplete(const char *path, FILEINFO *out, int *more) {
    if (more) *more = 0;
    assert(!held);
    dir_reads++;
    if (failed) return -EIO;
    if (strstr(path, "empty/")) return 0;
    if (strstr(path, "nested/")) {
        memset(out, 0, sizeof(*out));
        strcpy(out[0].name, "data");
        return 1;
    }
    out[0] = folder("nested");
    return 1;
}
int genGetStat(const char *path, iox_stat_t *stat) {
    assert(path && !held);
    stat_reads++;
    stat->size = (unsigned)content_bytes;
    stat->hisize = (unsigned)(content_bytes >> 32);
    return 0;
}
int getHddParty(const char *p, const FILEINFO *f, char *party, char *dir) {
    (void)p; (void)f;
    strcpy(party, "hdd0:PART");
    strcpy(dir, "pfs0:/data");
    return 0;
}
int getHddDVRPParty(const char *p, const FILEINFO *f, char *party, char *dir) {
    return getHddParty(p, f, party, dir);
}
int mountParty(const char *p) { (void)p; return 0; }
int mountDVRPParty(const char *p) { (void)p; return 0; }
static struct { char dir[MAX_PATH]; } fb = {"usb0:/"};
static int size_valid = 1;
static int isDir(const FILEINFO *f) { return !!(f->stats.AttrFile & sceMcFileAttrSubdir); }
"""

actions = (root / "src/filer_actions.c").read_text()
code += actions[actions.index("#define TREE_DEPTH_LIMIT"):actions.index("//endfunc getFileSize")]
code += function((root / "src/dash_browser.c").read_text(), "static void fmtSize(")
code += r"""
#define FILEOP_TRACE 0
#define DPRINTF(...) ((void)0)
static int backend_result, fileMode;
static int ensurePathPcCardCfOnline(const char *p) { (void)p; return 1; }
static void ensurePathMemoryCardAccessible(const char *p) { (void)p; }
static void normalizePcCardCfPath(char *p, size_t n) { (void)p; (void)n; }
static void genLimObjName(char *p, int n) { (void)p; (void)n; }
static int fileXioOpen(const char *p, int flags, int mode) {
    (void)p; (void)flags; (void)mode;
    return backend_result < 0 ? backend_result : 9;
}
static int fileXioClose(int fd) { assert(fd == 9); return backend_result; }
static int fileXioMkdir(const char *p, int mode) { (void)p; (void)mode; return backend_result; }
static int fileXioRemove(const char *p) { (void)p; return backend_result; }
static int fileXioRmdir(const char *p) { (void)p; return backend_result; }
"""
fileops = (root / "src/filer_fileops.c").read_text()
for signature in ("int genOpen(", "int genClose(", "int genMkdir(", "int genRemove(", "int genRmdir("):
    code += function(fileops, signature)
code += r"""
static void put(const char *parent, const char *name, u64 bytes) {
    FILEINFO f = folder(name);
    filerSizeCachePut(parent, &f, bytes, filerSizeCacheRevision());
}
static int cached(const char *parent, const char *name, u64 expected) {
    FILEINFO f = folder(name);
    u64 bytes = 999;
    int found = filerSizeCacheGet(parent, &f, &bytes);
    if (found) assert(bytes == expected);
    return found;
}
int main(void) {
    FILEINFO a = folder("A"), empty = folder("empty");
    u64 bytes = 0;
    char text[24];
    filerSizeCacheInit();
    filerSizeCacheInit();
    assert(creates == 1);
    fmtSize(text, sizeof(text), &a);
    assert(!text[0]);
    assert(!getFileSize("usb0:/", &a, &bytes) && bytes == 5000);
    int reads = dir_reads, stats = stat_reads;
    FILEINFO refreshed = folder("A");
    bytes = 0;
    assert(!getFileSize("usb0:/", &refreshed, &bytes) && bytes == 5000 && dir_reads == reads && stat_reads == stats);
    bytes = 100;
    assert(!getFileSize("usb0:/", &refreshed, &bytes) && bytes == 5100 && dir_reads == reads && stat_reads == stats);
    fmtSize(text, sizeof(text), &refreshed);
    assert(!strcmp(text, "4 KB"));
    assert(cached("usb0:", "A", 5000) && cached("mass:/", "a", 5000));
    size_valid = 0;
    fmtSize(text, sizeof(text), &a);
    assert(!strcmp(text, "4 KB"));
    size_valid = 1;
    bytes = 0;
    assert(!getFileSize("usb0:/", &empty, &bytes) && bytes == 0);
    fmtSize(text, sizeof(text), &empty);
    assert(!strcmp(text, "0 B"));
    put("usb0:/A/", "nested", 5000);
    put("usb0:/", "Another", 77);
    put("usb0:/", "A2", 88);
    filerSizeCacheInvalidate("usb0:/A/nested/deep/data");
    assert(!cached("usb0:/", "A", 5000) && !cached("usb0:/A/", "nested", 5000));
    assert(cached("usb0:/", "Another", 77) && cached("usb0:/", "A2", 88) && cached("usb0:/", "empty", 0));
    fmtSize(text, sizeof(text), &a);
    assert(!text[0]);
    content_bytes = ((u64)3 << 32) + 7;
    bytes = 0;
    assert(!getFileSize("usb0:/", &a, &bytes) && bytes == content_bytes && dir_reads > reads);
    fmtSize(text, sizeof(text), &a);
    assert(!strcmp(text, "12.00 GB"));
    filerSizeCacheInvalidate("usb0:/A/nested/data");
    failed = 1;
    bytes = 0;
    assert(getFileSize("usb0:/", &a, &bytes) == -EIO && !cached("usb0:/", "A", 0));
    failed = 0;
    /* Removing or renaming a containing folder invalidates descendant keys too. */
    put("usb0:/A/", "nested", 5);
    filerSizeCacheInvalidate("usb0:/A/");
    assert(!cached("usb0:/A/", "nested", 5) && cached("usb0:/", "A2", 88));
    /* A write invalidates ancestors at open and again at close; unrelated
     * folders remain cached, and measurements during the write are not stored. */
    put("usb0:/", "A", 10);
    filerSizeCacheOpened(-1, "usb0:/A/data", FIO_O_WRONLY);
    assert(cached("usb0:/", "A", 10));
    filerSizeCacheOpened(3, "usb0:/A/data", FIO_O_WRONLY | FIO_O_TRUNC);
    assert(!cached("usb0:/", "A", 10));
    put("usb0:/", "A", 11);
    assert(!cached("usb0:/", "A", 11));
    assert(cached("usb0:/", "A2", 88));
    filerSizeCacheClosed(3);
    put("usb0:/", "A", 12);
    assert(cached("usb0:/", "A", 12));
    filerSizeCacheOpened(4, "usb0:/A/data", FIO_O_RDONLY);
    assert(cached("usb0:/", "A", 12));
    filerSizeCacheClosed(4);
    unsigned revision = filerSizeCacheRevision();
    filerSizeCacheInvalidate("usb0:/A/nested/data");
    filerSizeCachePut("usb0:/", &a, 999, revision);
    assert(!cached("usb0:/", "A", 999));
    /* HDD identities survive remounts and isolate physical unit/partition names. */
    strcpy(mountedParty[0], "hdd1:PART");
    put("hdd1:/PART/", "A", 42);
    put("hdd0:/PART/", "A", 43);
    assert(cached("pfs0:/", "A", 42));
    filerSizeCacheInvalidate("pfs0:/A/deep/data");
    assert(!cached("hdd1:/PART/", "A", 42) && cached("hdd0:/PART/", "A", 43));
    strcpy(mountedParty[0], "hdd1:OTHER");
    assert(!cached("pfs0:/", "A", 42));
    strcpy(mountedDVRPParty[1], "dvr_hdd0:__xdata");
    put("dvr_hdd0:/__xdata/", "A", 44);
    assert(cached("dvr_pfs1:/", "A", 44));
    filerSizeCacheInvalidate("dvr_pfs1:/A/deep/data");
    assert(!cached("dvr_hdd0:/__xdata/", "A", 44));
    put("ata0:/", "A", 100);
    filerSizeCacheInvalidate("mass3:/A/file");
    assert(!cached("ata0:/", "A", 100));
    /* Exercise real write/create/delete wrappers, including failed calls. */
    put("usb0:/", "A", 1);
    backend_result = -EIO;
    assert(genMkdir("usb0:/A/nested/new", 0) == -EIO && cached("usb0:/", "A", 1));
    assert(genOpen("usb0:/A/data", FIO_O_WRONLY) == -EIO && cached("usb0:/", "A", 1));
    backend_result = 0;
    assert(!genMkdir("usb0:/A/nested/new", 0) && !cached("usb0:/", "A", 1));
    put("usb0:/", "A", 2);
    assert(!genRemove("usb0:/A/nested/file") && !cached("usb0:/", "A", 2));
    put("usb0:/", "A", 3);
    assert(!genRmdir("usb0:/A/nested/empty") && !cached("usb0:/", "A", 3));
    put("usb0:/", "A", 4);
    int fd = genOpen("usb0:/A/data", FIO_O_RDONLY);
    assert(fd == 9 && cached("usb0:/", "A", 4) && !genClose(fd));
    fd = genOpen("usb0:/A/data", FIO_O_WRONLY);
    assert(fd == 9 && !cached("usb0:/", "A", 4));
    put("usb0:/", "A", 5);
    assert(!cached("usb0:/", "A", 5));
    backend_result = -EIO;
    assert(genClose(fd) == -EIO);
    put("usb0:/", "A", 5);
    assert(!cached("usb0:/", "A", 5));
    backend_result = 0;
    assert(!genClose(fd));
    put("usb0:/", "A", 6);
    assert(cached("usb0:/", "A", 6));
    /* IOP replacement retires measurements and writer descriptors together. */
    put("usb0:/", "A", 55);
    filerSizeCacheOpened(5, "usb0:/Another/file", FIO_O_WRONLY);
    iop_generation++;
    assert(!cached("usb0:/", "A", 55));
    put("usb0:/", "Another", 66);
    assert(cached("usb0:/", "Another", 66));
    FILEINFO file = {0};
    strcpy(file.name, "data");
    file.stats.FileSizeByte = 17;
    fmtSize(text, sizeof(text), &file);
    assert(!strcmp(text, "17 B"));
    size_valid = 0;
    fmtSize(text, sizeof(text), &file);
    assert(!text[0]);
    assert(!held);
    puts("PASS: cached recursive 64-bit measurements survive refreshed rows, show zero/large sizes, avoid rescans, and reject failed or concurrent measurements");
    puts("PASS: deep mutations invalidate ancestors/subtrees while retaining siblings; HDD/DVR mounts, FAT aliases, writers and IOP resets cannot reuse stale totals");
    return 0;
}
"""

compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="folder-size-cache-") as folder:
    folder = Path(folder)
    (folder / "platform.h").write_text(platform)
    for name in ("filer_internal.h", "init.h"):
        (folder / name).write_text('#include "platform.h"\n')
    (folder / "filer_size_cache.h").write_text(
        (root / "include/filer_size_cache.h").read_text().replace('"launchelf.h"', '"platform.h"')
    )
    (folder / "test.c").write_text(code)
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("SANITIZE") else []
    subprocess.run(
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags, "-I", str(folder),
         str(folder / "test.c"), str(root / "src/filer_size_cache.c"), "-o", str(folder / "test")],
        check=True,
    )
    subprocess.run([str(folder / "test")], check=True)
