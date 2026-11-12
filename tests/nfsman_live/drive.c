/*
  Host driver for tests/nfsman_live: compiles the nfs: driver into itself (with the host stubs of
  the IOP headers in tests/smb2man_live/inc, a clock it sets, and a count of the sockets the
  driver connects) and drives its iomanX ops and devctls the way the EE does.

  drive <server-ip> <export-path> <dir-in-export>
  <dir-in-export> must exist and be empty: the test works in it.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <time.h>

int host_lock_depth;
static uint32_t host_now = 1000;
#define NFSMAN_SECONDS() host_now
static uint32_t nfsman_clock(uint32_t *usec)
{
    *usec = (uint32_t)getpid() * 7919u % 1000000u;
    return host_now;
}
static int host_connects;
#define connect(a, b, c) (host_connects++, connect(a, b, c))
#include "../../iop/nfsman/nfsman.c"
#undef connect

static iop_device_t *dev;
int AddDrv(iop_device_t *d)
{
    dev = d;
    return d->ops->init(d);
}
int DelDrv(const char *n)
{
    (void)n;
    return 0;
}

#define OPS dev->ops
static int fails;
#define CHECK(c, ...)                                                  \
    do {                                                               \
        int ok_ = (c) && host_lock_depth == 0;                         \
        fails += !ok_;                                                 \
        printf("%s", ok_ ? "ok: " : "FAIL: ");                         \
        printf(__VA_ARGS__);                                           \
        printf(host_lock_depth ? " (driver lock left held)\n" : "\n"); \
        host_lock_depth = 0;                                           \
    } while (0)

static const char *server, *export_path, *subdir;

static int connect_export(const char *name, const char *url)
{
    nfsConnect_in_t in;

    memset(&in, 0, sizeof(in));
    snprintf(in.name, sizeof(in.name), "%s", name);
    snprintf(in.url, sizeof(in.url), "%s", url);
    in.seed = (unsigned int)time(NULL); /* as the EE does: a run soon after the last must not reuse its xids */
    return OPS->devctl(NULL, "", NFS_DEVCTL_CONNECT, &in, sizeof(in), NULL, 0);
}

static int list(const char *path, char names[][256], int max)
{
    iop_file_t d = {0};
    iox_dirent_t de;
    int n = 0, rv;

    if ((rv = OPS->dopen(&d, path)) < 0)
        return rv;
    while ((rv = OPS->dread(&d, &de)) > 0)
        if (n < max)
            strcpy(names[n++], de.name);
    OPS->dclose(&d);
    return rv < 0 ? rv : n;
}

static int listed(const char *path, const char *name)
{
    static char names[64][256];
    int i, n = list(path, names, 64);

    for (i = 0; i < n; i++)
        if (!strcmp(names[i], name))
            return 1;
    return 0;
}

static int put(const char *path, int flags, const unsigned char *data, int len)
{
    iop_file_t f = {0};
    int rv = OPS->open(&f, path, flags, 0644);

    if (rv < 0)
        return rv;
    rv = OPS->write(&f, (void *)data, len);
    OPS->close(&f);
    return rv;
}

static int get(const char *path, unsigned char *data, int len)
{
    iop_file_t f = {0};
    int rv = OPS->open(&f, path, FIO_O_RDONLY, 0);

    if (rv < 0)
        return rv;
    rv = OPS->read(&f, data, len);
    OPS->close(&f);
    return rv;
}

static unsigned char big[300 * 1024], back[300 * 1024 + 16];

int main(int argc, char **argv)
{
    char url[512], p[1024], q[1024], longname[256];
    iop_file_t f = {0}, f2 = {0};
    iox_stat_t st;
    int i, rv, c0;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <server-ip> <export-path> <dir-in-export>\n", argv[0]);
        return 2;
    }
    server = argv[1];
    export_path = argv[2];
    subdir = argv[3];
    for (i = 0; i < (int)sizeof(big); i++)
        big[i] = (unsigned char)(i * 131 + (i >> 9));

    CHECK(nfsman_start(0, NULL) == MODULE_RESIDENT_END && dev != NULL, "driver registered as nfs:");

    // CONNECT and its refusals
    snprintf(url, sizeof(url), "nfs://%s%s/%s", server, export_path, subdir);
    CHECK(connect_export("A", url) == 0, "CONNECT %s as A", url);
    snprintf(url, sizeof(url), "nfs://%s:2049%s?uid=0&gid=0", server, export_path);
    CHECK(connect_export("B", url) == 0, "CONNECT %s as B (port and uid/gid)", url);
    CHECK(connect_export("A", url) == -EEXIST, "the same name again: -EEXIST");
    CHECK(connect_export("C", "smb://127.0.0.1/x") == -NFS_CONNECT_ERR_URL, "not nfs://: URL");
    CHECK(connect_export("C", "nfs://server/x") == -NFS_CONNECT_ERR_URL, "a host name: URL");
    CHECK(connect_export("C", "nfs://127.0.0.1/x?user=1") == -NFS_CONNECT_ERR_URL, "an unknown option: URL");
    CHECK(connect_export("C", "nfs://127.0.0.1:1/x") == -NFS_CONNECT_ERR_CONN, "nothing listening: CONN");
    // uid and gid take 9 digits, as smb_cnf.c and the Settings editor do; nothing wraps
    CHECK(connect_export("C", "nfs://127.0.0.1:1/x?uid=999999999&gid=500000000") == -NFS_CONNECT_ERR_CONN,
          "the largest uid and gid parse: CONN");
    CHECK(connect_export("C", "nfs://127.0.0.1:1/x?uid=1000000000") == -NFS_CONNECT_ERR_URL, "a 10 digit uid: URL");
    CHECK(connect_export("C", "nfs://127.0.0.1:1/x?uid=4294967296") == -NFS_CONNECT_ERR_URL, "uid 2^32: URL");
    CHECK(connect_export("C", "nfs://127.0.0.1:1/x?gid=4294967296") == -NFS_CONNECT_ERR_URL, "gid 2^32: URL");
    CHECK(connect_export("C", "nfs://127.0.0.1:65536/x") == -NFS_CONNECT_ERR_URL, "port 65536: URL");
    snprintf(url, sizeof(url), "nfs://%s%s/no-such-dir-here", server, export_path);
    CHECK(connect_export("C", url) == -NFS_CONNECT_ERR_EXPORT, "no such export: EXPORT");
    CHECK(listed("/", "A") && listed("/", "B") && !listed("/", "C"), "the device root lists A and B only");

    // Files and folders
    CHECK(OPS->mkdir(NULL, "/A/sub", 0777) == 0, "mkdir");
    CHECK(OPS->mkdir(NULL, "/A/sub", 0777) == 0, "mkdir of an existing folder is not a failure");
    CHECK(put("/A/sub/big.bin", FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, big, sizeof(big)) == sizeof(big),
          "write 300 kB");
    memset(back, 0, sizeof(back));
    CHECK(get("/A/sub/big.bin", back, sizeof(back)) == sizeof(big) && !memcmp(big, back, sizeof(big)),
          "read it back: same bytes, stops at the end of the file");
    CHECK(OPS->getstat(NULL, "/A/sub/big.bin", &st) == 0 && st.size == sizeof(big) && (st.mode & FIO_S_IFREG),
          "getstat: a file of 300 kB");
    snprintf(p, sizeof(p), "/B/%s/sub/big.bin", subdir);
    CHECK(OPS->getstat(NULL, p, &st) == 0 && st.size == sizeof(big), "the same file through B");
    CHECK(OPS->getstat(NULL, "/A/sub", &st) == 0 && (st.mode & FIO_S_IFDIR), "getstat: a folder");
    CHECK(listed("/A/sub", "big.bin") && listed("/A", "sub"), "listings");

    CHECK(OPS->open(&f, "/A/sub/big.bin", FIO_O_RDONLY, 0) >= 0, "open read-only");
    CHECK(OPS->lseek(&f, 0, SEEK_END) == sizeof(big), "lseek SEEK_END: the size");
    CHECK(OPS->lseek(&f, 1000, SEEK_SET) == 1000 && OPS->read(&f, back, 16) == 16 && !memcmp(back, big + 1000, 16),
          "lseek and read 16 bytes");
    CHECK(OPS->write(&f, big, 16) == -EBADF, "a read-only handle refuses writes");
    OPS->close(&f);

    CHECK(put("/A/sub/big.bin", FIO_O_WRONLY | FIO_O_TRUNC, big, 100) == 100 &&
              OPS->getstat(NULL, "/A/sub/big.bin", &st) == 0 && st.size == 100,
          "O_TRUNC: a shorter rewrite does not keep the old tail");
    CHECK(put("/A/sub/big.bin", FIO_O_WRONLY | FIO_O_APPEND, big + 100, 50) == 50 &&
              OPS->getstat(NULL, "/A/sub/big.bin", &st) == 0 && st.size == 150 &&
              get("/A/sub/big.bin", back, sizeof(back)) == 150 && !memcmp(back, big, 150),
          "O_APPEND writes at the end");
    CHECK(OPS->open(&f, "/A/sub/big.bin", FIO_O_RDWR | FIO_O_APPEND, 0) >= 0 &&
              OPS->open(&f2, "/A/sub/big.bin", FIO_O_RDWR | FIO_O_APPEND, 0) >= 0 &&
              OPS->write(&f, big + 150, 10) == 10 && OPS->write(&f2, big + 160, 10) == 10 &&
              OPS->close(&f) == 0 && OPS->close(&f2) == 0 &&
              get("/A/sub/big.bin", back, sizeof(back)) == 170 && !memcmp(back, big, 170),
          "O_APPEND through two handles: each write goes to the end the other left");
    CHECK(put("/A/sub/big.bin", FIO_O_WRONLY | FIO_O_TRUNC, big, 150) == 150, "back to 150 bytes");
    CHECK(OPS->open(&f, "/A/sub/big.bin", FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL, 0644) == -EEXIST,
          "O_EXCL on an existing file: -EEXIST");
    CHECK(put("/A/sub/excl.bin", FIO_O_WRONLY | FIO_O_CREAT | FIO_O_EXCL, big, 10) == 10 &&
              OPS->remove(NULL, "/A/sub/excl.bin") == 0,
          "O_EXCL creates a new file");
    CHECK(OPS->open(&f, "/A/sub/missing", FIO_O_RDONLY, 0) == -ENOENT, "open of a missing file: -ENOENT");
    CHECK(OPS->open(&f, "/A/sub", FIO_O_RDONLY, 0) == -EISDIR, "open of a folder as a file: -EISDIR");
    CHECK(OPS->open(&f, "/Z/x", FIO_O_RDONLY, 0) == -ENOENT, "an unknown export name: -ENOENT");
    CHECK(OPS->open(&f, "/x", FIO_O_RDONLY, 0) == -ENOENT && OPS->open(&f, "/A", FIO_O_RDONLY, 0) == -EACCES,
          "the root and the export itself hold no file");

    CHECK(OPS->rename(NULL, "/A/sub/big.bin", "/A/sub/moved.bin") == 0 && listed("/A/sub", "moved.bin") &&
              !listed("/A/sub", "big.bin"),
          "rename");
    snprintf(p, sizeof(p), "/B/%s/sub/x.bin", subdir);
    CHECK(OPS->rename(NULL, "/A/sub/moved.bin", p) == -EINVAL, "rename across exports: -EINVAL");
    CHECK(OPS->remove(NULL, "/A/sub") == -EISDIR, "remove of a folder: -EISDIR");
    CHECK(OPS->rmdir(NULL, "/A/sub/moved.bin") == -ENOTDIR, "rmdir of a file: -ENOTDIR");
    CHECK(OPS->rmdir(NULL, "/A/sub") == -ENOTEMPTY, "rmdir of a folder that is not empty: -ENOTEMPTY");

    // A 255 byte name, the most Linux takes, in a 440 byte path as smb: takes
    memset(longname, 'n', 255);
    longname[255] = '\0';
    snprintf(p, sizeof(p), "/A/sub/%s", longname);
    CHECK(put(p, FIO_O_WRONLY | FIO_O_CREAT, big, 10) == 10 && listed("/A/sub", longname) &&
              OPS->remove(NULL, p) == 0,
          "a 255 byte name: create, list, remove");
    memset(q, 'd', 900);
    q[900] = '\0';
    snprintf(p, sizeof(p), "/A/%.180s/%.180s/%.180s/%.180s/%.180s", q, q, q, q, q);
    CHECK(OPS->getstat(NULL, p, &st) == -ENAMETOOLONG, "a path that does not fit: -ENAMETOOLONG");

    // A connection the server closed: the next call connects again and open files carry on
    CHECK(OPS->open(&f, "/A/sub/moved.bin", FIO_O_RDONLY, 0) >= 0, "open a file to keep across a reconnect");
    c0 = host_connects;
    shutdown(exports[0].u->fd, SHUT_RDWR);
    CHECK(listed("/A", "sub") && host_connects == c0 + 1, "a closed connection is connected again on the next use");
    CHECK(OPS->lseek(&f, 0, SEEK_SET) == 0 && OPS->read(&f, back, 150) == 150 && !memcmp(back, big, 150),
          "the open file reads after the reconnect");
    c0 = host_connects;
    close(exports[0].u->fd); // the socket is gone: found before the next call, which connects again
    rv = OPS->lseek(&f, 0, SEEK_SET) == 0 ? OPS->read(&f, back, 16) : -1;
    CHECK(rv == 16 && host_connects == c0 + 1, "a read after the socket died connects again (%d, %d connects)", rv, host_connects - c0);
    exports[0].u->fd = -1; // as unfs4 leaves it when a call fails under it
    CHECK(OPS->lseek(&f, 0, SEEK_SET) == 0 && OPS->read(&f, back, 16) == 16, "and after a call failed under it");

    // The reconnect backoff
    exports[0].port = 1; // nothing listens there now
    nfsman_drop(0);
    rv = OPS->getstat(NULL, "/A/sub", &st);
    c0 = host_connects;
    CHECK(rv == -EIO && OPS->getstat(NULL, "/A/sub", &st) == -EIO && host_connects == c0,
          "a failed reconnect is not tried again at once");
    exports[0].port = 2049;
    host_now += NFSMAN_RETRY_SECONDS;
    CHECK(OPS->getstat(NULL, "/A/sub", &st) == 0, "it is tried again %d s later", NFSMAN_RETRY_SECONDS);

    // Clean up, then DISCONNECT_ALL
    CHECK(OPS->remove(NULL, "/A/sub/moved.bin") == 0 && OPS->rmdir(NULL, "/A/sub") == 0 && !listed("/A", "sub"),
          "remove and rmdir");
    CHECK(OPS->open(&f2, "/B/", FIO_O_RDONLY, 0) == -EACCES, "the export root is not a file");
    {
        iop_file_t d = {0};
        iox_dirent_t de;

        CHECK(OPS->dopen(&d, "/A/") >= 0, "open a folder to keep across DISCONNECT_ALL");
        CHECK(OPS->devctl(NULL, "", NFS_DEVCTL_DISCONNECT_ALL, NULL, 0, NULL, 0) == 0 && !listed("/", "A") &&
                  !listed("/", "B"),
              "DISCONNECT_ALL: the root is empty");
        CHECK(OPS->read(&f, back, 16) == -EBADF, "a handle from before DISCONNECT_ALL: -EBADF");
        snprintf(url, sizeof(url), "nfs://%s%s", server, export_path);
        CHECK(connect_export("A", url) == 0 && OPS->dread(&d, &de) == -EBADF,
              "a folder from before DISCONNECT_ALL, with A connected again in its slot: -EBADF");
        CHECK(OPS->dclose(&d) == 0 && OPS->devctl(NULL, "", NFS_DEVCTL_DISCONNECT_ALL, NULL, 0, NULL, 0) == 0,
              "close it, DISCONNECT_ALL");
    }

    printf("%s: %d failures\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
