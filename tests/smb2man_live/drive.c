/* Host harness for iop/smb2man/smb2man.c: compiles the driver into itself (so it can reach the
   share table, cut a session's socket and set the backoff clock) and drives its iomanX ops and
   devctls the way the EE does, against a live Samba server. Built and run by run.sh.

   drive <mode> <port> <user> <password> <dir-of-share-1> <dir-of-share-2>
   mode: full   every case below
         dialect=<hex>  connect, check the dialect, one read and one write
         refuse=<code>  connect must fail with -<code> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdint.h>
#include <unistd.h>

int host_lock_depth;
static uint32_t host_now = 1000;
#define SMB2MAN_SECONDS() host_now
static int host_connects;
// Counts TCP connects, so a backoff that skips the network can be told from one that tried.
#define socket(a, b, c) (host_connects++, socket(a, b, c))
#include "../../iop/smb2man/smb2man.c"
#undef socket

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

static int port;
static const char *user, *password;

static int connect_share(const char *name, const char *url, const char *u, const char *p)
{
    smb2Connect_in_t in;
    smb2Connect_out_t out;

    memset(&in, 0, sizeof(in));
    snprintf(in.name, sizeof(in.name), "%s", name);
    snprintf(in.url, sizeof(in.url), "%s", url);
    snprintf(in.username, sizeof(in.username), "%s", u);
    snprintf(in.password, sizeof(in.password), "%s", p);
    return OPS->devctl(NULL, "", SMB2_DEVCTL_CONNECT, &in, sizeof(in), &out, sizeof(out));
}

static int connect_url(const char *name, const char *share)
{
    char url[128];
    snprintf(url, sizeof(url), "smb://127.0.0.1:%d/%s/ignored/sub", port, share);
    return connect_share(name, url, user, password);
}

static int list(const char *path, char names[][64], int max)
{
    iop_file_t f;
    iox_dirent_t de;
    int n = 0, r;

    memset(&f, 0, sizeof(f));
    if ((r = OPS->dopen(&f, path)) < 0)
        return r;
    while ((r = OPS->dread(&f, &de)) > 0) {
        if (n < max)
            snprintf(names[n], 64, "%.63s", de.name);
        n++;
    }
    OPS->dclose(&f);
    return r < 0 ? r : n;
}

static int listed(const char *path, const char *name)
{
    char names[64][64];
    int i, n = list(path, names, 64);

    for (i = 0; i < n && i < 64; i++)
        if (!strcmp(names[i], name))
            return 1;
    return 0;
}

static int put(const char *path, const unsigned char *data, int len)
{
    iop_file_t f;
    int r;

    memset(&f, 0, sizeof(f));
    if ((r = OPS->open(&f, path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0644)) < 0)
        return r;
    r = OPS->write(&f, (void *)data, len);
    OPS->close(&f);
    return r;
}

static int get(const char *path, unsigned char *data, int len)
{
    iop_file_t f;
    int r;

    memset(&f, 0, sizeof(f));
    if ((r = OPS->open(&f, path, FIO_O_RDONLY, 0)) < 0)
        return r;
    r = OPS->read(&f, data, len);
    OPS->close(&f);
    return r;
}

static double now_s(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static unsigned char big[300 * 1024], back[300 * 1024 + 16];

static int full(const char *dir1, const char *dir2)
{
    char names[16][64], path[700], url[128], longname[300];
    iox_stat_t st;
    iop_file_t f, g, d;
    struct stat hs;
    int i, r;
    s64 pos;
    double t;

    for (i = 0; i < (int)sizeof(big); i++)
        big[i] = (unsigned char)(i * 7 + 3);

    // ---- connect: two shares, a name taken twice, the device root -------------------------
    CHECK(connect_url("A", "PS2SMB") == 0, "CONNECT A (PS2SMB, a path after the share ignored)");
    CHECK(connect_url("A", "PS2SMB") == -EEXIST, "CONNECT A again: -EEXIST, the session kept");
    CHECK(connect_url("Second", "OTHER") == 0, "CONNECT Second (OTHER) beside it");
    snprintf(url, sizeof(url), "SMB://127.0.0.1:%d/PS2SMB", port);
    CHECK(connect_share("G", url, "", "") == 0, "CONNECT G as guest (blank user), upper-case scheme");
    r = list("/", names, 16);
    CHECK(r == 3 && !strcmp(names[0], "A") && !strcmp(names[1], "Second") && !strcmp(names[2], "G"),
          "device root lists A, Second, G (%d)", r);
    CHECK(OPS->getstat(NULL, "/", &st) == 0 && (st.mode & FIO_S_IFDIR), "getstat / is a folder");
    CHECK(OPS->getstat(NULL, "/A", &st) == 0 && (st.mode & FIO_S_IFDIR), "getstat /A is a folder");
    CHECK(OPS->getstat(NULL, "/Nope/x", &st) == -ENOENT, "a share not connected: -ENOENT");
    CHECK(listed("/A/", "DVD") && listed("/A", "DVD"), "share root lists its folders (with or without /)");
    CHECK(listed("/Second", "only-in-other.txt"), "Second lists its own files");

    // ---- files -----------------------------------------------------------------------------
    CHECK(OPS->mkdir(NULL, "/A/new", 0777) == 0 && OPS->mkdir(NULL, "/A/new", 0777) == 0,
          "mkdir, and mkdir of an existing folder is not a failure");
    CHECK(put("/A/new/big.bin", big, sizeof(big)) == (int)sizeof(big), "write 300 KB (several 64 KB WRITEs)");
    snprintf(path, sizeof(path), "%s/new/big.bin", dir1);
    CHECK(stat(path, &hs) == 0 && hs.st_size == (off_t)sizeof(big), "the server has all of it");
    memset(back, 0, sizeof(back));
    CHECK(get("/G/new/big.bin", back, sizeof(back)) == (int)sizeof(big) && !memcmp(back, big, sizeof(big)),
          "read it back through the guest share, short count at the end of the file");
    CHECK(OPS->getstat(NULL, "/A/new/big.bin", &st) == 0 && st.size == sizeof(big) && !(st.mode & FIO_S_IFDIR) &&
              st.mtime[6] + st.mtime[7] * 256 >= 2024,
          "getstat: size, a file, this decade's mtime");
    memset(&f, 0, sizeof(f));
    CHECK(OPS->open(&f, "/A/new/big.bin", FIO_O_RDONLY, 0) >= 0, "open for reading");
    CHECK(OPS->lseek(&f, 0, SEEK_END) == (int)sizeof(big), "lseek END is the size");
    pos = OPS->lseek64(&f, 100000, SEEK_SET);
    CHECK(pos == 100000 && OPS->read(&f, back, 10) == 10 && !memcmp(back, big + 100000, 10), "lseek64 + read");
    CHECK(OPS->write(&f, big, 10) == -EBADF, "a read-only handle refuses writes");
    // a second share's file open at the same time
    memset(&g, 0, sizeof(g));
    CHECK(OPS->open(&g, "/Second/only-in-other.txt", FIO_O_RDONLY, 0) >= 0 && OPS->read(&g, back, 64) == 6 &&
              !memcmp(back, "other\n", 6),
          "a file on Second read while one on A is open");
    OPS->close(&g);
    OPS->close(&f);
    CHECK(OPS->close(&f) == -EBADF, "a closed handle is -EBADF");

    CHECK(OPS->rename(NULL, "/A/new/big.bin", "/A/new/moved.bin") == 0 && listed("/A/new", "moved.bin") &&
              !listed("/A/new", "big.bin"),
          "rename within a share");
    CHECK(OPS->rename(NULL, "/A/new/moved.bin", "/Second/moved.bin") == -EINVAL, "rename across shares: -EINVAL");
    r = OPS->rmdir(NULL, "/A/new");
    CHECK(r == -ENOTEMPTY && listed("/A/new", "moved.bin"), "rmdir of a full folder: -ENOTEMPTY (%d), nothing removed", r);
    CHECK(OPS->remove(NULL, "/A/new/moved.bin") == 0 && OPS->rmdir(NULL, "/A/new") == 0, "remove, rmdir");
    CHECK(OPS->getstat(NULL, "/A/new", &st) == -ENOENT, "gone: -ENOENT");

    // the device root and the shares themselves stay as they are
    memset(&f, 0, sizeof(f));
    CHECK(OPS->open(&f, "/A", FIO_O_RDONLY, 0) == -EACCES, "open of a share itself: -EACCES");
    CHECK(OPS->open(&f, "/x.txt", FIO_O_WRONLY | FIO_O_CREAT, 0) == -ENOENT, "a file in the device root: -ENOENT");
    CHECK(OPS->mkdir(NULL, "/", 0777) == -EACCES && OPS->rmdir(NULL, "/A") == -EACCES &&
              OPS->remove(NULL, "/Second") == -EACCES,
          "mkdir /, rmdir /A, remove /Second: -EACCES");

    // a name past 127 characters
    memset(longname, 'n', 200);
    longname[200] = '\0';
    snprintf(path, sizeof(path), "/A/%s.bin", longname);
    CHECK(put(path, big, 1000) == 1000 && get(path, back, 2000) == 1000 && OPS->remove(NULL, path) == 0,
          "a 204-character name: write, read, remove");
    memset(longname, 'n', 299);
    longname[299] = '\0';
    snprintf(path, sizeof(path), "/A/%s/%s", longname, longname);
    CHECK(OPS->getstat(NULL, path, &st) == -ENAMETOOLONG, "a 599-character path: -ENAMETOOLONG, not cut short");

    // ---- a session the server closed, and one that died under a request ----------------------
    memset(&d, 0, sizeof(d));
    memset(&f, 0, sizeof(f));
    CHECK(OPS->open(&f, "/A/DVD/TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->dopen(&d, "/A/DVD") == 0,
          "a file and a folder open on A");
    shutdown(shares[0].u->fd, SHUT_RDWR); // the socket now reads as closed, as after the server's FIN
    i = host_connects;
    CHECK(listed("/A/DVD", "TEST.iso") && host_connects == i + 1, "next use of A finds it closed and connects again");
    CHECK(OPS->read(&f, back, 16) == -EBADF && OPS->dread(&d, (iox_dirent_t *)back) == -EBADF,
          "handles of the ended session: -EBADF");
    OPS->close(&f);
    OPS->dclose(&d);
    CHECK(listed("/Second", "only-in-other.txt"), "Second untouched");

    memset(&f, 0, sizeof(f));
    CHECK(OPS->open(&f, "/A/DVD/TEST.iso", FIO_O_RDONLY, 0) >= 0, "open a file on A");
    close(shares[0].u->fd); // the socket is gone under usmb2: the READ fails, the session ends
    CHECK(OPS->read(&f, back, 16) == -EIO && shares[0].u == NULL, "a READ on a dead socket: -EIO, session ended");
    CHECK(OPS->close(&f) == -EBADF, "its handle went with it");
    CHECK(OPS->getstat(NULL, "/A/DVD", &st) == 0, "the next call connects A again");

    // the server stops answering: one try, then -EIO without the network until the backoff ends
    shutdown(shares[0].u->fd, SHUT_RDWR);
    shares[0].port = 1; // nothing listens there
    i = host_connects;
    CHECK(OPS->getstat(NULL, "/A/DVD", &st) == -EIO && host_connects == i + 1, "a reconnect that fails: -EIO");
    host_now += SMB2MAN_RETRY_SECONDS - 1;
    CHECK(OPS->getstat(NULL, "/A/DVD", &st) == -EIO && host_connects == i + 1, "within the backoff: no try");
    CHECK(listed("/", "A"), "the share stays listed while down");
    shares[0].port = (uint16_t)port;
    host_now += 1;
    CHECK(OPS->getstat(NULL, "/A/DVD", &st) == 0 && host_connects == i + 2, "after %d s: tried, and back",
          SMB2MAN_RETRY_SECONDS);

    // ---- refusals -----------------------------------------------------------------------------
    snprintf(url, sizeof(url), "smb://127.0.0.1:%d/PS2SMB", port);
    CHECK(connect_share("W", url, user, "wrong") == -SMB2_CONNECT_ERR_LOGON, "wrong password: LOGON");
    CHECK(connect_share("W", url, "Caf\xc3\xa9\xe2\x82\xac", "") == -SMB2_CONNECT_ERR_LOGON,
          "a user name outside Latin-1: LOGON, not sent");
    snprintf(url, sizeof(url), "smb://127.0.0.1:%d/NOSUCH", port);
    CHECK(connect_share("W", url, user, password) == -SMB2_CONNECT_ERR_SHARE, "no such share: SHARE");
    CHECK(connect_share("W", "smb://nas.local/PS2SMB", user, password) == -SMB2_CONNECT_ERR_URL, "a host name: URL");
    CHECK(connect_share("W", "smb://127.0.0.1:0/PS2SMB", user, password) == -SMB2_CONNECT_ERR_URL &&
              connect_share("W", "smb://127.0.0.1:70000/PS2SMB", user, password) == -SMB2_CONNECT_ERR_URL &&
              connect_share("W", "smb://127.0.0.1/", user, password) == -SMB2_CONNECT_ERR_URL &&
              connect_share("W", "http://127.0.0.1/PS2SMB", user, password) == -SMB2_CONNECT_ERR_URL &&
              connect_share("W/X", url, user, password) == -SMB2_CONNECT_ERR_URL,
          "bad port, no share, other scheme, a / in the name: URL");
    t = now_s();
    CHECK(connect_share("W", "smb://127.0.0.1:1/PS2SMB", user, password) == -SMB2_CONNECT_ERR_CONN,
          "nothing listening: CONN (%.1f s)", now_s() - t);
    t = now_s();
    r = connect_share("W", "smb://10.255.255.1/PS2SMB", user, password);
    t = now_s() - t;
    CHECK(r == -SMB2_CONNECT_ERR_CONN && t < SMB2MAN_CONNECT_MS / 1000.0 + 1, "an address that never answers: CONN in %.1f s",
          t);
    CHECK(list("/", names, 16) == 3, "refused shares are not kept");
    for (i = 3; i < SMB2MAN_SHARES; i++) {
        snprintf(path, sizeof(path), "S%d", i);
        connect_url(path, "PS2SMB");
    }
    CHECK(list("/", names, 16) == SMB2MAN_SHARES && connect_url("Ninth", "PS2SMB") == -SMB2_CONNECT_ERR_FULL,
          "%d shares at once, a ninth: FULL", SMB2MAN_SHARES);

    // ---- disconnect -----------------------------------------------------------------------------
    memset(&f, 0, sizeof(f));
    memset(&d, 0, sizeof(d));
    OPS->open(&f, "/S5/DVD/TEST.iso", FIO_O_RDONLY, 0);
    OPS->dopen(&d, "/S5/DVD");
    CHECK(OPS->devctl(NULL, "", SMB2_DEVCTL_DISCONNECT_ALL, NULL, 0, NULL, 0) == 0 && list("/", names, 16) == 0,
          "DISCONNECT_ALL: the root is empty");
    CHECK(OPS->read(&f, back, 1) == -EBADF && OPS->dread(&d, (iox_dirent_t *)back) == -EBADF && OPS->close(&f) == -EBADF,
          "old handles: -EBADF");
    OPS->dclose(&d);
    CHECK(connect_url("A", "PS2SMB") == 0 && listed("/A", "DVD"), "the same name connects again afterwards");
    OPS->devctl(NULL, "", SMB2_DEVCTL_DISCONNECT_ALL, NULL, 0, NULL, 0);
    (void)dir2;
    return 0;
}

int main(int argc, char **argv)
{
    char names[1][64];
    unsigned char buf[64];
    int r;

    if (argc < 7) {
        fprintf(stderr, "usage: drive mode port user password dir1 dir2\n");
        return 2;
    }
    port = atoi(argv[2]);
    user = argv[3];
    password = argv[4];
    CHECK(_start(0, NULL) == 0, "module start");

    if (!strcmp(argv[1], "full")) {
        full(argv[5], argv[6]);
    } else if (!strncmp(argv[1], "dialect=", 8)) {
        unsigned want = (unsigned)strtoul(argv[1] + 8, NULL, 16);
        r = connect_url("A", "PS2SMB");
        CHECK(r == 0, "connect (%d)", r);
        if (r == 0) {
            CHECK(shares[0].dialect == want, "dialect 0x%04x (want 0x%04x), signing %s", shares[0].dialect, want,
                  shares[0].u->signing ? "on" : "off");
            CHECK(put("/A/dialect.txt", (const unsigned char *)"signed?", 7) == 7 &&
                      get("/A/dialect.txt", buf, sizeof(buf)) == 7 && !memcmp(buf, "signed?", 7) &&
                      OPS->remove(NULL, "/A/dialect.txt") == 0,
                  "write, read back, remove");
            CHECK(listed("/A/DVD", "TEST.iso"), "list");
        }
    } else if (!strncmp(argv[1], "refuse=", 7)) {
        r = connect_url("A", "PS2SMB");
        CHECK(r == -atoi(argv[1] + 7), "connect refused with %d (want -%s)", r, argv[1] + 7);
        CHECK(list("/", names, 1) == 0, "nothing kept");
    }
    printf("%s: %d failure(s)\n", argv[1], fails);
    return fails ? 1 : 0;
}
