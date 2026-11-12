/*
  nfsman: the nfs: device (NFSv4.0) of Launchstation's file browser.

  Up to NFSMAN_EXPORTS exports are connected at once, each by a name: NFS_DEVCTL_CONNECT (see
  include/nfsman.h) connects nfs://<IPv4>[:port]/<export>[?uid=N&gid=N] as nfs:/<NAME>/, the
  device root lists the connected names, and NFS_DEVCTL_DISCONNECT_ALL forgets them all. This is
  smb2man's interface, so the EE side follows the SMB code.

  The protocol work is Ronnie Sahlberg's unfs4, a tiny NFSv4.0 client, vendored under unfs4/
  (see unfs4/VENDORED.txt). unfs4.c is compiled into this file with read()/write()/close() mapped
  to the bounded socket calls below, as smb2man does with usmb2. unfs4 reads and writes with the
  anonymous stateid, so nothing but a file's creation keeps state on the server: a filehandle
  stays good across a reconnect, and open files and folders carry on after one.

  The connection comes from a port below 1024, so a Linux export does not need "insecure".
  Credentials are AUTH_SYS with the uid and gid from the URL, 0 if not given.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stddef.h>

#include <errno.h>
#include <fcntl.h>
#include <intrman.h>
#include <iomanX.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>
#include <thsemap.h>

#include "../../include/nfsman.h"

#ifdef _IOP
#include <ps2ip.h>
#define NFSMAN_IOCTL(fd, cmd, arg) lwip_ioctl(fd, cmd, arg)
#define NFSMAN_CLOSE(fd)           lwip_close(fd)
#else
// The host build (tests/nfsman_live): the same code over POSIX sockets.
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define NFSMAN_IOCTL(fd, cmd, arg) ioctl(fd, cmd, arg)
#define NFSMAN_CLOSE(fd)           close(fd)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define MODNAME "nfsman"
IRX_ID(MODNAME, 1, 0);

#define NFSMAN_EXPORTS     8 // as the EE's NFS_SHARE_MAX
#define MAX_OPEN_FILES     8
#define MAX_PATH_LEN       512
// How long the driver waits for the server: a connect, then any wait with no data moving.
#define NFSMAN_CONNECT_MS  5000
#define NFSMAN_REPLY_MS    15000
// An export whose reconnect failed is not tried again sooner than this.
#define NFSMAN_RETRY_SECONDS 30
// One READ or WRITE moves at most this much; a server may move less, and the loop goes on.
#define NFSMAN_IO_MAX      65536

//-------------------------------------------------------------------------
// Socket calls for unfs4, each wait bounded, as smb2man's for usmb2.
static int nfsman_wait(int fd, int for_write, int ms)
{
    fd_set set, err;
    struct timeval tv;

    FD_ZERO(&set);
    FD_ZERO(&err);
    FD_SET(fd, &set);
    FD_SET(fd, &err);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    return select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, &err, &tv);
}

// The socket stays non-blocking after the connect: a peer that stops sending, or stops taking
// data, fails the call after NFSMAN_REPLY_MS of no progress (see smb2man_sock_read()).
#define NFSMAN_SOCK_RETRIES 8

static int nfsman_sock_read(int fd, void *buf, int len)
{
    int n, tries;

    for (tries = 0; fd >= 0 && tries < NFSMAN_SOCK_RETRIES; tries++) {
        if (nfsman_wait(fd, 0, NFSMAN_REPLY_MS) <= 0)
            return -1;
        n = recv(fd, buf, len, 0);
        if (n >= 0)
            return n; // 0: the peer closed
    }
    return -1;
}

static int nfsman_sock_write(int fd, const void *buf, int len)
{
    int n, tries;

    for (tries = 0; fd >= 0 && tries < NFSMAN_SOCK_RETRIES; tries++) {
        if (nfsman_wait(fd, 1, NFSMAN_REPLY_MS) <= 0)
            return -1;
        n = send(fd, (void *)buf, len, 0);
        if (n > 0)
            return n;
    }
    return -1;
}

static int nfsman_sock_close(int fd)
{
    return NFSMAN_CLOSE(fd);
}

#define UNFS4_FEATURE_OPENDIR
#define UNFS4_FEATURE_WRITE
#define UNFS4_FEATURE_CREATE
#define UNFS4_FEATURE_STAT
#define UNFS4_FEATURE_MKDIR
#define UNFS4_FEATURE_UNLINK
#define UNFS4_FEATURE_RENAME
#define UNFS4_FEATURE_TRUNCATE
#undef read
#undef write
#undef close
#define read(a, b, c)  nfsman_sock_read(a, b, c)
#define write(a, b, c) nfsman_sock_write(a, b, c)
#define close(a)       nfsman_sock_close(a)
#include "unfs4/unfs4.c"
#undef read
#undef write
#undef close

// RFC 7530 status codes the driver tells apart
#define NFS4ERR_PERM        1
#define NFS4ERR_NOENT       2
#define NFS4ERR_ACCESS      13
#define NFS4ERR_EXIST       17
#define NFS4ERR_NOTDIR      20
#define NFS4ERR_ISDIR       21
#define NFS4ERR_INVAL       22
#define NFS4ERR_FBIG        27
#define NFS4ERR_NOSPC       28
#define NFS4ERR_ROFS        30
#define NFS4ERR_NOTEMPTY    66
#define NFS4ERR_DQUOT       69
#define NFS4ERR_STALE       70
#define NFS4ERR_WRONGSEC    10016
#define NFS4ERR_MINOR_VERS_MISMATCH 10021

//-------------------------------------------------------------------------
/*
  One connected export. name[0] == 0: a free slot. u is NULL while the connection is down; the
  next call that uses the export connects it again from the rest, which CONNECT kept.
*/
struct nfsman_export
{
    struct unfs4_context *u;
    uint32_t ip;       // network order
    uint32_t uid, gid;
    uint32_t retry_at; // nfsman_seconds() before which no reconnect is tried
    uint32_t seed;     // from the EE's clock, for the xids
    uint16_t port;
    char name[NFS_MAX_NAME_LEN];
    char path[200];    // the export, as unfs4_mount() takes it
};

static struct nfsman_export exports[NFSMAN_EXPORTS];
static int nfs_sema = -1;

struct nfsman_file
{
    uint8_t fh[4 + UNFS4_MAX_FH];
    uint64_t pos, size;
    int exp;      // index in exports[], or -1 for a free slot
    int writable; // 0 read-only, 1 writable, 3 writable in append mode
    unsigned gen; // bumped on every open of this slot; privdata carries it (see nfsman_slot)
};

// privdata = slot | gen << NFSMAN_SLOT_BITS. MAX_OPEN_FILES must fit in the slot bits.
#define NFSMAN_SLOT_BITS 4
#define NFSMAN_GEN_MASK  (0x7FFFFFFFu >> NFSMAN_SLOT_BITS)

static struct nfsman_file file_table[MAX_OPEN_FILES];

// An open directory. The device root (exp < 0) lists the connected names, off counting slots.
struct nfsman_dir
{
    int exp;
    int off;
    char name[NFS_MAX_NAME_LEN]; // the export it was opened on: a DISCONNECT_ALL ends it
    struct unfs4_dir *ud;
};

#define LOCK()   WaitSema(nfs_sema)
#define UNLOCK() SignalSema(nfs_sema)

//-------------------------------------------------------------------------
#ifdef _IOP
// Allocator for unfs4 (the IOP has no libc heap).
void *malloc(size_t size)
{
    void *result;
    int OldState;

    CpuSuspendIntr(&OldState);
    result = AllocSysMemory(ALLOC_FIRST, (int)size, NULL);
    CpuResumeIntr(OldState);

    return result;
}

void free(void *ptr)
{
    int OldState;

    if (ptr == NULL)
        return;

    CpuSuspendIntr(&OldState);
    FreeSysMemory(ptr);
    CpuResumeIntr(OldState);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t s = nmemb * size;
    void *ptr;

    if (size != 0 && s / size != nmemb)
        return NULL;

    ptr = malloc(s);
    if (ptr != NULL)
        memset(ptr, 0, s);

    return ptr;
}
#endif

// Seconds and microseconds since the IOP started. tests/nfsman_live sets the clock.
#ifndef NFSMAN_SECONDS
static uint32_t nfsman_clock(uint32_t *usec)
{
#ifdef _IOP
    iop_sys_clock_t clock;
    u32 sec, us;

    GetSystemTime(&clock);
    SysClock2USec(&clock, &sec, &us);
    *usec = us;
    return sec;
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    *usec = (uint32_t)(ts.tv_nsec / 1000);
    return (uint32_t)ts.tv_sec;
#endif
}

static uint32_t nfsman_seconds(void)
{
    uint32_t usec;

    return nfsman_clock(&usec);
}
#define NFSMAN_SECONDS() nfsman_seconds()
#endif

//-------------------------------------------------------------------------
// A path within an export: at most MAX_PATH_LEN - 1 bytes, with no separator at either end.
static int nfsman_fixpath(char *out, const char *in)
{
    int i = 0;

    while (*in == '\\' || *in == '/')
        in++;

    while (*in != '\0') {
        if (i >= MAX_PATH_LEN - 1)
            return -ENAMETOOLONG;
        out[i++] = (*in == '\\') ? '/' : *in;
        in++;
    }
    while (i > 0 && out[i - 1] == '/')
        i--;

    out[i] = '\0';
    return 0;
}

// A negative errno for the last failed call, from the server's status.
static int nfsman_errno(const struct unfs4_context *u, int fallback)
{
    if (u == NULL || u->fd < 0)
        return -EIO;

    switch (u->status) {
        case NFS4ERR_NOENT:
        case NFS4ERR_STALE:
            return -ENOENT;
        case NFS4ERR_PERM:
        case NFS4ERR_ACCESS:
        case NFS4ERR_WRONGSEC:
            return -EACCES;
        case NFS4ERR_EXIST:
            return -EEXIST;
        case NFS4ERR_NOTDIR:
            return -ENOTDIR;
        case NFS4ERR_ISDIR:
            return -EISDIR;
        case NFS4ERR_INVAL:
            return -EINVAL;
        case NFS4ERR_FBIG:
            return -EFBIG;
        case NFS4ERR_NOSPC:
        case NFS4ERR_DQUOT:
            return -ENOSPC;
        case NFS4ERR_ROFS:
            return -EROFS;
        case NFS4ERR_NAMETOOLONG:
            return -ENAMETOOLONG;
        case NFS4ERR_NOTEMPTY:
            return -ENOTEMPTY;
    }
    return fallback;
}

//-------------------------------------------------------------------------
// Close export e's connection without waiting on the network. Its open files keep their
// filehandles, which are good on the next connection. Called with the lock held.
static void nfsman_drop(int e)
{
    struct nfsman_export *ex = &exports[e];

    if (ex->u != NULL) {
        if (ex->u->fd >= 0)
            nfsman_sock_close(ex->u->fd);
        free(ex->u);
        ex->u = NULL;
    }
}

// After every failed call, with the lock held: a connection that died under it is closed, and
// the next use of the export connects again.
static void nfsman_check(int e)
{
    if (exports[e].u != NULL && exports[e].u->fd < 0)
        nfsman_drop(e);
}

/*
  TCP connect with a deadline, from a port below 1024: non-blocking connect, then wait until the
  socket is writable. A port that is taken (the last connection's, in TIME_WAIT) is passed over.
*/
static int nfsman_tcp_connect(const struct nfsman_export *ex)
{
    struct sockaddr_in sin;
    uint32_t usec, port;
    int fd, err = 0, on = 1, tries;
    socklen_t errlen = sizeof(err);

    nfsman_clock(&usec);
    port = 512 + usec % 512;
    for (tries = 0; tries < 16; tries++, port = 512 + (port - 512 + 37) % 512) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == 0)
            break;
        nfsman_sock_close(fd);
        fd = -1;
    }
    if (fd < 0) { // no port below 1024: an "insecure" export takes any
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(ex->port);
    sin.sin_addr.s_addr = ex->ip;

    // Stays non-blocking: see nfsman_sock_read().
    NFSMAN_IOCTL(fd, FIONBIO, &on);
    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0 &&
        (nfsman_wait(fd, 1, NFSMAN_CONNECT_MS) <= 0 ||
         getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) != 0 || err != 0)) {
        nfsman_sock_close(fd);
        return -1;
    }

    return fd;
}

// Connect export e and look it up. Returns 0 or -NFS_CONNECT_ERR_*. Called with the lock held.
static int nfsman_connect(int e)
{
    struct nfsman_export *ex = &exports[e];
    struct unfs4_context *u;
    uint32_t sec, usec;
    int rv;

    nfsman_drop(e);

    u = calloc(1, sizeof(*u));
    if (u == NULL)
        return -NFS_CONNECT_ERR_MEMORY;

    u->fd = nfsman_tcp_connect(ex);
    if (u->fd < 0) {
        free(u);
        return -NFS_CONNECT_ERR_CONN;
    }
    u->uid = ex->uid;
    u->gid = ex->gid;
    // unfs4.h asks for xids from a clock: the EE's seconds and the IOP's microseconds.
    sec = nfsman_clock(&usec);
    u->xid = (ex->seed + sec) * 1000000u + usec;

    if (unfs4_mount(u, ex->path) == 0) {
        ex->u = u;
        printf("nfsman: %s connected\n", ex->name);
        return 0;
    }

    switch (u->fd < 0 ? UNFS4_ERR_CONN : u->status) {
        case NFS4ERR_NOENT:
        case NFS4ERR_NOTDIR:
            rv = -NFS_CONNECT_ERR_EXPORT;
            break;
        case NFS4ERR_PERM:
        case NFS4ERR_ACCESS:
        case NFS4ERR_WRONGSEC:
            rv = -NFS_CONNECT_ERR_ACCESS;
            break;
        default: // no answer, an RPC error, or no 4.0 (NFS4ERR_MINOR_VERS_MISMATCH)
            rv = -NFS_CONNECT_ERR_PROT;
            break;
    }
    if (u->fd >= 0)
        nfsman_sock_close(u->fd);
    free(u);
    return rv;
}

/*
  Export e ready for a call, with the lock held: a connection the server closed while it sat idle
  (its socket reads as ready with no call out) is closed, and one that is down is connected again,
  unless the last try failed under NFSMAN_RETRY_SECONDS ago. Returns 0 or -EIO.
*/
static int nfsman_ready(int e)
{
    struct nfsman_export *ex = &exports[e];

    if (ex->u != NULL && ex->u->fd >= 0 && nfsman_wait(ex->u->fd, 0, 0) == 0)
        return 0;
    nfsman_drop(e);
    if ((int32_t)(NFSMAN_SECONDS() - ex->retry_at) < 0)
        return -EIO;
    if (nfsman_connect(e) < 0) {
        ex->retry_at = NFSMAN_SECONDS() + NFSMAN_RETRY_SECONDS;
        return -EIO;
    }
    return 0;
}

/*
  The export a device path names ("/NAME/rest"), with the lock held and its connection ready.
  Returns its index and points *rest past the name, or a negative errno without the lock.
  -ENOENT: no export by that name. root (when not NULL) is set when the path is the export itself.
*/
static int nfsman_lock_path(const char *path, const char **rest, int *root)
{
    const char *p, *end;
    size_t n;
    int e, rv;

    if (path == NULL)
        return -EINVAL;
    for (p = path; *p == '/' || *p == '\\'; p++)
        ;
    for (end = p; *end && *end != '/' && *end != '\\'; end++)
        ;
    n = (size_t)(end - p);

    LOCK();
    for (e = 0; e < NFSMAN_EXPORTS; e++)
        if (n > 0 && exports[e].name[0] && strlen(exports[e].name) == n && !strncmp(exports[e].name, p, n))
            break;
    if (e == NFSMAN_EXPORTS) {
        UNLOCK();
        return n ? -ENOENT : -EACCES; // the device root holds no files
    }
    for (p = end; *p == '/' || *p == '\\'; p++)
        ;
    if (root != NULL)
        *root = (*p == '\0');
    else if (*p == '\0') { // the export itself can only be listed, stat'ed or opened as a folder
        UNLOCK();
        return -EACCES;
    }
    rv = nfsman_ready(e);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }
    *rest = p;
    return e;
}

//-------------------------------------------------------------------------
static int nfsman_init(iop_device_t *dev)
{
    iop_sema_t smp;
    int i;

    (void)dev;

    smp.initial = 1;
    smp.max = 1;
    smp.option = 0;
    smp.attr = 1;
    nfs_sema = CreateSema(&smp);

    for (i = 0; i < MAX_OPEN_FILES; i++)
        file_table[i].exp = -1;

    return nfs_sema < 0 ? -1 : 0;
}

static int nfsman_deinit(iop_device_t *dev)
{
    (void)dev;

    if (nfs_sema >= 0) {
        DeleteSema(nfs_sema);
        nfs_sema = -1;
    }

    return 0;
}

//-------------------------------------------------------------------------
// The slot of an open file, or -1 for a handle whose slot was freed and maybe reused since
// (DISCONNECT_ALL), as smb2man_slot(). Called with the lock held.
static int nfsman_slot(iop_file_t *f)
{
    unsigned v = (unsigned)(uintptr_t)f->privdata;
    unsigned slot = v & ((1u << NFSMAN_SLOT_BITS) - 1);

    if (slot >= MAX_OPEN_FILES || file_table[slot].exp < 0 || file_table[slot].gen != (v >> NFSMAN_SLOT_BITS))
        return -1;

    return (int)slot;
}

static int nfsman_open(iop_file_t *f, const char *filename, int flags, int mode)
{
    char path[MAX_PATH_LEN];
    struct nfsman_file *file;
    struct unfs4_context *u;
    struct unfs4_attr st;
    const char *rest;
    uint8_t *fh = NULL;
    int e, slot, rv, writable, exists;
    int64_t size;

    (void)mode;

    e = nfsman_lock_path(filename, &rest, NULL);
    if (e < 0)
        return e;
    u = exports[e].u;

    for (slot = 0; slot < MAX_OPEN_FILES && file_table[slot].exp >= 0; slot++)
        ;
    rv = (slot == MAX_OPEN_FILES) ? -EMFILE : nfsman_fixpath(path, rest);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }

    // FIO_O_WRONLY's bit is in FIO_O_RDWR too. A read-only handle refuses writes itself (see write).
    writable = (flags & FIO_O_WRONLY) != 0;
    exists = unfs4_stat(u, path, &st) == 0;
    if (!exists && (u->status != NFS4ERR_NOENT || !writable || !(flags & FIO_O_CREAT))) {
        rv = nfsman_errno(u, -ENOENT);
        goto fail;
    }
    if (exists && st.type == NF4DIR) {
        rv = -EISDIR;
        goto fail;
    }
    if (exists && writable && (flags & FIO_O_CREAT) && (flags & FIO_O_EXCL)) {
        rv = -EEXIST;
        goto fail;
    }
    fh = unfs4_open(u, path, exists ? 0 : O_CREAT);
    if (fh == NULL) {
        rv = nfsman_errno(u, -EIO);
        goto fail;
    }
    size = exists ? (int64_t)st.size : 0;
    // Without O_TRUNC a shorter rewrite would keep the old tail.
    if (writable && (flags & FIO_O_TRUNC) && size > 0) {
        if (unfs4_truncate(u, fh, 0) != 0) {
            rv = nfsman_errno(u, -EIO);
            goto fail;
        }
        size = 0;
    }

    file = &file_table[slot];
    memcpy(file->fh, fh, 4 + ((get32(fh) + 3) & ~3u));
    free(fh);
    file->size = (uint64_t)size;
    file->pos = (flags & FIO_O_APPEND) ? file->size : 0;
    file->writable = (writable && (flags & FIO_O_APPEND)) ? 3 : writable;
    file->exp = e;
    file->gen = (file->gen + 1) & NFSMAN_GEN_MASK;
    f->privdata = (void *)(uintptr_t)((unsigned)slot | (file->gen << NFSMAN_SLOT_BITS));

    UNLOCK();

    return slot;

fail:
    free(fh);
    nfsman_check(e);
    UNLOCK();
    return rv;
}

// The open file f names, with the lock held and its export ready, or a negative errno without
// the lock.
static int nfsman_lock_file(iop_file_t *f)
{
    int slot, rv;

    LOCK();
    slot = nfsman_slot(f);
    if (slot < 0) {
        UNLOCK();
        return -EBADF;
    }
    rv = nfsman_ready(file_table[slot].exp);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }
    return slot;
}

static int nfsman_close(iop_file_t *f)
{
    int slot;

    // Nothing to close on the server: unfs4 keeps no state for an open file.
    LOCK();
    slot = nfsman_slot(f);
    if (slot >= 0)
        file_table[slot].exp = -1;
    UNLOCK();

    return slot < 0 ? -EBADF : 0;
}

static int nfsman_read(iop_file_t *f, void *buffer, int size)
{
    struct nfsman_file *file;
    struct unfs4_context *u;
    uint8_t *p = (uint8_t *)buffer;
    int slot, rv, n, done = 0;

    if (size < 0)
        return -EINVAL;

    slot = nfsman_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];
    u = exports[file->exp].u;

    while (done < size) {
        n = size - done;
        if (n > NFSMAN_IO_MAX)
            n = NFSMAN_IO_MAX;
        rv = unfs4_pread(u, file->fh, p + done, n, file->pos);
        if (rv < 0) {
            rv = nfsman_errno(u, -EIO);
            nfsman_check(file->exp);
            UNLOCK();
            return (done > 0) ? done : rv;
        }
        if (rv == 0)
            break; // end of file
        if (rv > n)
            rv = n;
        done += rv;
        file->pos += (uint64_t)rv;
    }

    UNLOCK();

    return done;
}

static int nfsman_write(iop_file_t *f, void *buffer, int size)
{
    struct nfsman_file *file;
    struct unfs4_context *u;
    uint8_t *p = (uint8_t *)buffer;
    int slot, rv, n, done = 0;

    if (size < 0)
        return -EINVAL;

    slot = nfsman_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];
    u = exports[file->exp].u;
    if (!file->writable) {
        UNLOCK();
        return -EBADF;
    }
    if ((file->writable & 2) && size > 0) // append: every write goes to the end, whatever lseek did
        file->pos = file->size;

    while (done < size) {
        n = size - done;
        if (n > NFSMAN_IO_MAX)
            n = NFSMAN_IO_MAX;
        rv = unfs4_pwrite(u, file->fh, p + done, n, file->pos);
        if (rv <= 0) {
            // A server that stores nothing (a full disk) must not spin this loop forever.
            rv = rv < 0 ? nfsman_errno(u, -EIO) : -EIO;
            nfsman_check(file->exp);
            UNLOCK();
            return (done > 0) ? done : rv;
        }
        if (rv > n)
            rv = n;
        done += rv;
        file->pos += (uint64_t)rv;
        if (file->pos > file->size)
            file->size = file->pos;
    }

    UNLOCK();

    return done;
}

static s64 nfsman_lseek64(iop_file_t *f, s64 offset, int whence)
{
    struct nfsman_file *file;
    int64_t size;
    s64 base;
    int slot;

    slot = nfsman_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (s64)file->pos;
            break;
        case SEEK_END:
            // Another client may have changed the file: ask the server, else use what is known.
            size = unfs4_size(exports[file->exp].u, file->fh);
            if (size >= 0)
                file->size = (uint64_t)size;
            else
                nfsman_check(file->exp);
            base = (s64)file->size;
            break;
        default:
            UNLOCK();
            return -EINVAL;
    }
    if (base + offset < 0) {
        UNLOCK();
        return -EINVAL;
    }
    file->pos = (uint64_t)(base + offset);
    base = (s64)file->pos;
    UNLOCK();

    return base;
}

static int nfsman_lseek(iop_file_t *f, int offset, int whence)
{
    s64 pos = nfsman_lseek64(f, offset, whence);

    // The 32-bit call cannot report a position past 2 GiB; lseek64 serves those.
    if (pos > 0x7fffffffLL)
        return -EOVERFLOW;

    return (int)pos;
}

//-------------------------------------------------------------------------
/*
  Unix seconds to the packed time in iox_stat_t: [1] second, [2] minute, [3] hour, [4] day,
  [5] month, [6..7] year (little-endian).
*/
static void nfsman_pack_time(int64_t secs, unsigned char *out)
{
    u32 t = secs > 0 ? (u32)secs : 0;
    u32 days = t / 86400u;
    u32 rem = t % 86400u;
    u32 y, m, d;

    out[0] = 0;
    out[1] = (unsigned char)(rem % 60u);
    out[2] = (unsigned char)((rem / 60u) % 60u);
    out[3] = (unsigned char)(rem / 3600u);

    // Civil date from days since 1970-01-01 (Howard Hinnant's civil_from_days).
    {
        u32 z = days + 719468u;
        u32 era = z / 146097u;
        u32 doe = z - era * 146097u;
        u32 yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
        u32 doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
        u32 mp = (5u * doy + 2u) / 153u;

        d = doy - (153u * mp + 2u) / 5u + 1u;
        m = mp + (mp < 10u ? 3u : (u32)-9);
        y = yoe + era * 400u + (m <= 2u ? 1u : 0u);
    }

    out[4] = (unsigned char)d;
    out[5] = (unsigned char)m;
    out[6] = (unsigned char)(y & 0xff);
    out[7] = (unsigned char)((y >> 8) & 0xff);
}

static void nfsman_dir_stat(iox_stat_t *stat)
{
    memset(stat, 0, sizeof(iox_stat_t));
    stat->mode = FIO_S_IFDIR | FIO_S_IRUSR | FIO_S_IWUSR | FIO_S_IXUSR;
}

static void nfsman_fill_stat(iox_stat_t *stat, const struct unfs4_attr *a)
{
    memset(stat, 0, sizeof(iox_stat_t));
    if (a->type == NF4DIR) {
        stat->mode = FIO_S_IFDIR | FIO_S_IRUSR | FIO_S_IWUSR | FIO_S_IXUSR;
    } else {
        stat->mode = FIO_S_IFREG | FIO_S_IRUSR | FIO_S_IWUSR;
        stat->size = (unsigned int)(a->size & 0xffffffffu);
        stat->hisize = (unsigned int)(a->size >> 32);
    }
    nfsman_pack_time(a->mtime, stat->mtime);
    memcpy(stat->ctime, stat->mtime, sizeof(stat->ctime));
    memcpy(stat->atime, stat->mtime, sizeof(stat->atime));
}

static int nfsman_is_root(const char *path)
{
    while (*path == '/' || *path == '\\')
        path++;
    return *path == '\0';
}

//-------------------------------------------------------------------------
static int nfsman_dopen(iop_file_t *f, const char *dirname)
{
    char path[MAX_PATH_LEN];
    struct nfsman_dir *d;
    const char *rest;
    int e, rv;

    if (dirname == NULL)
        return -EINVAL;

    d = calloc(1, sizeof(*d));
    if (d == NULL)
        return -ENOMEM;

    if (nfsman_is_root(dirname)) {
        d->exp = -1;
        f->privdata = d;
        return 0;
    }

    e = nfsman_lock_path(dirname, &rest, &rv);
    if (e < 0) {
        free(d);
        return e;
    }

    rv = nfsman_fixpath(path, rest);
    if (rv == 0) {
        d->ud = unfs4_opendir(exports[e].u, path);
        if (d->ud == NULL) {
            rv = nfsman_errno(exports[e].u, -ENOENT);
            nfsman_check(e);
        }
    }
    if (rv < 0) {
        free(d);
        UNLOCK();
        return rv;
    }
    d->exp = e;
    strcpy(d->name, exports[e].name);
    f->privdata = d;

    UNLOCK();

    return 0;
}

static int nfsman_dclose(iop_file_t *f)
{
    struct nfsman_dir *d;

    LOCK();
    d = (struct nfsman_dir *)f->privdata;
    if (d == NULL) {
        UNLOCK();
        return -EBADF;
    }
    free(d->ud);
    free(d);
    f->privdata = NULL;
    UNLOCK();

    return 0;
}

static int nfsman_dread(iop_file_t *f, iox_dirent_t *dirent)
{
    struct unfs4_dirent *de;
    struct nfsman_dir *d;
    int rv;

    LOCK();

    d = (struct nfsman_dir *)f->privdata;
    if (d != NULL && d->exp < 0) {
        // The device root: one folder per connected export.
        while (d->off < NFSMAN_EXPORTS && !exports[d->off].name[0])
            d->off++;
        rv = 0;
        if (d->off < NFSMAN_EXPORTS) {
            nfsman_dir_stat(&dirent->stat);
            strcpy(dirent->name, exports[d->off++].name);
            dirent->privdata = NULL;
            rv = 1;
        }
        UNLOCK();
        return rv;
    }
    // A folder of an export that was disconnected, or whose slot now holds another export.
    if (d == NULL || strcmp(exports[d->exp].name, d->name)) {
        UNLOCK();
        return -EBADF;
    }
    rv = nfsman_ready(d->exp);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }

    // NULL is the end or a failure, and the end may make no call: only a call sets status.
    exports[d->exp].u->status = 0;
    de = unfs4_readdir(exports[d->exp].u, d->ud);
    if (de == NULL) {
        rv = (exports[d->exp].u->status != 0) ? nfsman_errno(exports[d->exp].u, -EIO) : 0;
        nfsman_check(d->exp);
        UNLOCK();
        return rv; // 0: the end of the directory
    }
    nfsman_fill_stat(&dirent->stat, &de->attr);
    strncpy(dirent->name, de->name, sizeof(dirent->name) - 1);
    dirent->name[sizeof(dirent->name) - 1] = '\0';
    dirent->privdata = NULL;

    UNLOCK();

    return 1;
}

static int nfsman_getstat(iop_file_t *f, const char *filename, iox_stat_t *stat)
{
    char path[MAX_PATH_LEN];
    struct unfs4_attr st;
    const char *rest;
    int e, rv, root;

    (void)f;

    if (filename == NULL || stat == NULL)
        return -EINVAL;
    if (nfsman_is_root(filename)) {
        nfsman_dir_stat(stat);
        return 0;
    }

    e = nfsman_lock_path(filename, &rest, &root);
    if (e < 0)
        return e;

    rv = nfsman_fixpath(path, rest);
    if (rv == 0 && root) {
        nfsman_dir_stat(stat); // the export itself, already known to answer
    } else if (rv == 0 && unfs4_stat(exports[e].u, path, &st) != 0) {
        rv = nfsman_errno(exports[e].u, -ENOENT);
        nfsman_check(e);
    } else if (rv == 0) {
        nfsman_fill_stat(stat, &st);
    }

    UNLOCK();

    return rv;
}

//-------------------------------------------------------------------------
// Directory and file management. The device root and the exports themselves cannot be changed.
static int nfsman_mkdir(iop_file_t *f, const char *dirname, int mode)
{
    char path[MAX_PATH_LEN];
    struct unfs4_context *u;
    struct unfs4_attr st;
    const char *rest;
    int e, rv;

    (void)f;
    (void)mode;

    e = nfsman_lock_path(dirname, &rest, NULL);
    if (e < 0)
        return e;
    u = exports[e].u;

    rv = nfsman_fixpath(path, rest);
    if (rv == 0 && unfs4_mkdir(u, path) != 0) {
        rv = nfsman_errno(u, -EIO);
        // An existing folder is not a failure.
        if (rv == -EEXIST && unfs4_stat(u, path, &st) == 0 && st.type == NF4DIR)
            rv = 0;
        nfsman_check(e);
    }

    UNLOCK();

    return rv;
}

// rmdir and remove. NFS has one REMOVE for both, so the type is checked first, as rmdir(2) and
// unlink(2) would.
static int nfsman_delete(const char *name, int dir)
{
    char path[MAX_PATH_LEN];
    struct unfs4_context *u;
    struct unfs4_attr st;
    const char *rest;
    int e, rv;

    e = nfsman_lock_path(name, &rest, NULL);
    if (e < 0)
        return e;
    u = exports[e].u;

    rv = nfsman_fixpath(path, rest);
    if (rv == 0 && unfs4_stat(u, path, &st) != 0)
        rv = nfsman_errno(u, -ENOENT);
    else if (rv == 0 && dir && st.type != NF4DIR)
        rv = -ENOTDIR;
    else if (rv == 0 && !dir && st.type == NF4DIR)
        rv = -EISDIR;
    else if (rv == 0 && unfs4_remove(u, path) != 0)
        rv = nfsman_errno(u, -EIO);
    if (rv < 0)
        nfsman_check(e);

    UNLOCK();

    return rv;
}

static int nfsman_rmdir(iop_file_t *f, const char *dirname)
{
    (void)f;
    return nfsman_delete(dirname, 1);
}

static int nfsman_remove(iop_file_t *f, const char *filename)
{
    (void)f;
    return nfsman_delete(filename, 0);
}

static int nfsman_rename(iop_file_t *f, const char *oldname, const char *newname)
{
    char oldpath[MAX_PATH_LEN];
    char newpath[MAX_PATH_LEN];
    const char *oldrest, *newrest;
    int e, rv;

    (void)f;

    if (newname == NULL)
        return -EINVAL;
    e = nfsman_lock_path(oldname, &oldrest, NULL);
    if (e < 0)
        return e;
    // Within one export only: newname must start with the same "/NAME/".
    for (newrest = newname; *newrest == '/' || *newrest == '\\'; newrest++)
        ;
    rv = strlen(exports[e].name);
    if (strncmp(newrest, exports[e].name, rv) || (newrest[rv] != '/' && newrest[rv] != '\\')) {
        UNLOCK();
        return -EINVAL;
    }
    newrest += rv;

    rv = nfsman_fixpath(oldpath, oldrest);
    if (rv == 0)
        rv = nfsman_fixpath(newpath, newrest);
    if (rv == 0 && newpath[0] == '\0')
        rv = -EINVAL;
    if (rv == 0 && unfs4_rename(exports[e].u, oldpath, newpath) != 0) {
        rv = nfsman_errno(exports[e].u, -EIO);
        nfsman_check(e);
    }

    UNLOCK();

    return rv;
}

//-------------------------------------------------------------------------
static int nfsman_number(const char **p, uint32_t max, uint32_t *out)
{
    uint32_t v = 0;
    const char *s = *p;

    if (*s < '0' || *s > '9')
        return -1;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (uint32_t)(*s - '0');
        if (v > max)
            return -1;
    }
    *p = s;
    *out = v;
    return 0;
}

/*
  NFS_DEVCTL_CONNECT's URL: nfs://<IPv4>[:port]/<export>[?uid=N&gid=N]. Fills ip, port, path, uid
  and gid. Returns 0 or -1.
*/
static int nfsman_parse_url(struct nfsman_export *ex, const char *url)
{
    char ipstr[16];
    const char *host, *p;
    uint32_t v;
    size_t n;

    if ((url[0] | 0x20) != 'n' || (url[1] | 0x20) != 'f' || (url[2] | 0x20) != 's' || strncmp(url + 3, "://", 3))
        return -1;
    host = url + 6;
    for (p = host; *p && *p != ':' && *p != '/'; p++)
        ;
    n = (size_t)(p - host);
    if (n == 0 || n >= sizeof(ipstr))
        return -1;
    memcpy(ipstr, host, n);
    ipstr[n] = '\0';
    ex->ip = inet_addr(ipstr);
    if (ex->ip == 0xffffffffu || ex->ip == 0) // an IP address only: the driver has no DNS
        return -1;
    ex->port = 2049;
    if (*p == ':') {
        p++;
        if (nfsman_number(&p, 65535, &v) || v == 0)
            return -1;
        ex->port = (uint16_t)v;
    }
    if (*p != '/')
        return -1;
    for (host = p; *p && *p != '?'; p++)
        ;
    n = (size_t)(p - host);
    if (n >= sizeof(ex->path))
        return -1;
    memcpy(ex->path, host, n);
    ex->path[n] = '\0';
    while (*p == '?' || *p == '&') {
        p++;
        if (!strncmp(p, "uid=", 4)) {
            p += 4;
            if (nfsman_number(&p, 0xffffffffu / 10, &ex->uid))
                return -1;
        } else if (!strncmp(p, "gid=", 4)) {
            p += 4;
            if (nfsman_number(&p, 0xffffffffu / 10, &ex->gid))
                return -1;
        } else {
            return -1;
        }
    }
    return *p ? -1 : 0;
}

// NFS_DEVCTL_CONNECT: 0, -EEXIST for a name already connected, or -NFS_CONNECT_ERR_*.
static int nfsman_connect_devctl(const nfsConnect_in_t *in)
{
    struct nfsman_export *ex;
    int e, free_slot = -1, rv;

    // Every field NUL-terminated within its size: the EE's buffers are copied in whole.
    if (memchr(in->name, 0, sizeof(in->name)) == NULL || memchr(in->url, 0, sizeof(in->url)) == NULL)
        return -NFS_CONNECT_ERR_URL;
    if (in->name[0] == '\0' || strchr(in->name, '/') || strchr(in->name, '\\'))
        return -NFS_CONNECT_ERR_URL;

    LOCK();
    for (e = 0; e < NFSMAN_EXPORTS; e++) {
        if (!exports[e].name[0]) {
            if (free_slot < 0)
                free_slot = e;
        } else if (!strcmp(exports[e].name, in->name)) {
            UNLOCK();
            return -EEXIST;
        }
    }
    if (free_slot < 0) {
        UNLOCK();
        return -NFS_CONNECT_ERR_FULL;
    }
    ex = &exports[free_slot];
    memset(ex, 0, sizeof(*ex));
    rv = nfsman_parse_url(ex, in->url) ? -NFS_CONNECT_ERR_URL : 0;
    if (rv == 0) {
        strcpy(ex->name, in->name);
        ex->seed = in->seed;
        rv = nfsman_connect(free_slot);
    }
    if (rv < 0)
        memset(ex, 0, sizeof(*ex)); // an export that never connected is not kept
    UNLOCK();
    return rv;
}

static int nfsman_devctl(iop_file_t *f, const char *name, int cmd, void *arg, unsigned int arglen, void *buf,
                         unsigned int buflen)
{
    int e;

    (void)f;
    (void)name;
    (void)buf;
    (void)buflen;

    switch (cmd) {
        case NFS_DEVCTL_CONNECT:
            if (arg == NULL || arglen < sizeof(nfsConnect_in_t))
                return -EINVAL;
            return nfsman_connect_devctl((const nfsConnect_in_t *)arg);

        case NFS_DEVCTL_DISCONNECT_ALL:
            LOCK();
            for (e = 0; e < MAX_OPEN_FILES; e++)
                file_table[e].exp = -1;
            for (e = 0; e < NFSMAN_EXPORTS; e++) {
                nfsman_drop(e);
                memset(&exports[e], 0, sizeof(exports[e]));
            }
            UNLOCK();
            return 0;
    }

    return -EINVAL;
}

static int nfsman_nulldev(void)
{
    return -EIO;
}

static s64 nfsman_nulldev64(void)
{
    return -EIO;
}

static iop_device_ops_t nfsman_ops = {
    &nfsman_init,
    &nfsman_deinit,
    (void *)&nfsman_nulldev, // format
    &nfsman_open,
    &nfsman_close,
    &nfsman_read,
    &nfsman_write,
    &nfsman_lseek,
    (void *)&nfsman_nulldev, // ioctl
    &nfsman_remove,
    &nfsman_mkdir,
    &nfsman_rmdir,
    &nfsman_dopen,
    &nfsman_dclose,
    &nfsman_dread,
    &nfsman_getstat,
    (void *)&nfsman_nulldev, // chstat (the EE skips it for nfs:)
    &nfsman_rename,
    (void *)&nfsman_nulldev, // chdir
    (void *)&nfsman_nulldev, // sync
    (void *)&nfsman_nulldev, // mount
    (void *)&nfsman_nulldev, // umount
    &nfsman_lseek64,
    &nfsman_devctl,
    (void *)&nfsman_nulldev,   // symlink
    (void *)&nfsman_nulldev,   // readlink
    (void *)&nfsman_nulldev64, // ioctl2
};

static iop_device_t nfsman_dev = {
    "nfs",
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "NFS",
    &nfsman_ops,
};

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    DelDrv(nfsman_dev.name);
    if (AddDrv(&nfsman_dev) != 0)
        return MODULE_NO_RESIDENT_END;

    return MODULE_RESIDENT_END;
}
