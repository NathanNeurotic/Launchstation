/*
  smb2man: the smb: device (SMB2/SMB3) of Launchstation's file browser.

  Up to SMB2MAN_SHARES shares are connected at once, each by a name: SMB2_DEVCTL_CONNECT (see
  include/smb2man.h) connects smb://<IPv4>[:port]/<share> as smb:/<NAME>/, the device root lists
  the connected names, and SMB2_DEVCTL_DISCONNECT_ALL ends every session. This is the interface of
  the libsmb2 driver it replaces, so the EE side is unchanged.

  The protocol work is Ronnie Sahlberg's usmb2, a tiny SMB2 client, vendored under usmb2/ (MIT;
  see usmb2/VENDORED.txt for the commit and RiptOPL's local patch). usmb2.c is compiled into this
  file so the driver can use its request builder for the requests usmb2 has no call for:
  NEGOTIATE offering 2.0.2, 2.1, 3.0 and 3.0.2, a 64-bit file size, and directory listings read
  straight into a per-handle buffer. The request code, signing, socket bounds and error mapping
  come from RiptOPL's modules/network/smb2man/smb2man.c (rebuild/main 9b8fea0), whose one session
  is made one per share here. RiptOPL's worker thread, keepalive alarm and async connect are left
  out: a lost session is connected again on its next use instead (at most every
  SMB2MAN_RETRY_SECONDS against a server that does not answer).

  Against the libsmb2 driver: SMB 2.x signing when the server requires it (requests signed, every
  reply verified, file data included); a 3.x server that requires signing is refused (no AES-CMAC),
  as is a server that speaks only 3.1.1; the server must be an IPv4 address; no encryption; a
  server that does not answer costs SMB2MAN_CONNECT_MS, not 30 s.
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

#include "../../include/smb2man.h"
#include "smb2auth.h" // MD4/MD5/SHA-256 and HMAC, from RiptOPL's in-game reader

#ifdef _IOP
#include <ps2ip.h>
#define SMB2MAN_IOCTL(fd, cmd, arg) lwip_ioctl(fd, cmd, arg)
#define SMB2MAN_CLOSE(fd)           lwip_close(fd)
#else
// The host build (tests/smb2man_live): the same code over POSIX sockets.
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define SMB2MAN_IOCTL(fd, cmd, arg) ioctl(fd, cmd, arg)
#define SMB2MAN_CLOSE(fd)           close(fd)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define MODNAME "smb2man"
IRX_ID(MODNAME, 3, 0);

#define SMB2MAN_SHARES     8 // as the EE's SMB_SHARE_MAX
#define MAX_OPEN_FILES     8
#define MAX_PATH_LEN       512
// Longest name on the share, in UTF-8 bytes: CREATE carries it as UTF-16 in usmb2's 1 KB buffer.
#define SMB2MAN_NAME_MAX   440
// How long the driver waits for the server: a connect, then any wait with no data moving.
#define SMB2MAN_CONNECT_MS 5000
#define SMB2MAN_REPLY_MS   15000
// A share whose reconnect failed is not tried again sooner than this.
#define SMB2MAN_RETRY_SECONDS 30
// One READ or WRITE moves at most this much (64 KB is the most a single credit covers).
#define SMB2MAN_IO_MAX     65536
// Directory listing bytes fetched per QUERY_DIRECTORY, kept with the open directory.
#define SMB2MAN_DIRBUF     4096

//-------------------------------------------------------------------------
// Socket calls for usmb2, each wait bounded. usmb2 reads and writes the socket with
// read()/write() and closes it with close() (mapped to these below, for usmb2.c only).
static int smb2man_wait(int fd, int for_write, int ms)
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

/*
  The socket stays non-blocking after the connect, so a recv() or send() never waits inside the
  stack: each call moves what it can, and the wait for more is the bounded select() above. A peer
  that stops sending, or stops taking data (no ACKs), fails the call after SMB2MAN_REPLY_MS of no
  progress. A recv()/send() that fails right after select() said ready is retried a few times
  (a spurious wake-up), then taken as a dead socket.
*/
#define SMB2MAN_SOCK_RETRIES 8

static int smb2man_sock_read(int fd, void *buf, int len)
{
    int n, tries;

    for (tries = 0; fd >= 0 && tries < SMB2MAN_SOCK_RETRIES; tries++) {
        if (smb2man_wait(fd, 0, SMB2MAN_REPLY_MS) <= 0)
            return -1;
        n = recv(fd, buf, len, 0);
        if (n >= 0)
            return n; // 0: the peer closed
    }
    return -1;
}

static int smb2man_sock_write(int fd, const void *buf, int len)
{
    int n, tries;

    for (tries = 0; fd >= 0 && tries < SMB2MAN_SOCK_RETRIES; tries++) {
        if (smb2man_wait(fd, 1, SMB2MAN_REPLY_MS) <= 0)
            return -1;
        n = send(fd, (void *)buf, len, 0);
        if (n > 0)
            return n;
    }
    return -1;
}

static int smb2man_sock_close(int fd)
{
    return SMB2MAN_CLOSE(fd);
}

// SMB 2.x signing (HMAC-SHA256 over the message, keyed by the session base key). Called from
// usmb2_build_request() under the driver lock: a request is signed whole before its reply is
// read, so every share can use the one context.
static smb2auth_hmac_t smb2man_mac;
static u8 smb2man_digest[32], smb2man_reply_sig[16];

static void smb2man_sign(const uint8_t *key, uint8_t *hdr, int hdrlen, const uint8_t *data, int datalen)
{
    hdr[16] |= 0x08; // SMB2_FLAGS_SIGNED
    memset(hdr + 48, 0, 16);
    smb2auth_hmac_init(&smb2man_mac, SMB2AUTH_SHA256, key, 16);
    smb2auth_hmac_update(&smb2man_mac, hdr, hdrlen);
    if (data != NULL && datalen > 0)
        smb2auth_hmac_update(&smb2man_mac, data, datalen);
    smb2auth_hmac_final(&smb2man_mac, smb2man_digest);
    memcpy(hdr + 48, smb2man_digest, 16);
}
#define USMB2_SIGN(u, hdr, hdrlen, data, datalen) smb2man_sign((u)->session_key, hdr, hdrlen, data, datalen)

// A reply on a signed session: refused unless it is signed (SMB2_FLAGS_SIGNED), then its 64-byte header,
// signature zeroed, starts the HMAC that usmb2_build_request() feeds every byte it reads after it.
static int smb2man_verify_begin(const uint8_t *key, uint8_t *hdr)
{
    if (!(hdr[16] & 0x08))
        return -1;
    memcpy(smb2man_reply_sig, hdr + 48, 16);
    memset(hdr + 48, 0, 16);
    smb2auth_hmac_init(&smb2man_mac, SMB2AUTH_SHA256, key, 16);
    smb2auth_hmac_update(&smb2man_mac, hdr, 64);
    return 0;
}

static int smb2man_verify_end(void)
{
    u8 diff = 0;
    int i;

    smb2auth_hmac_final(&smb2man_mac, smb2man_digest);
    for (i = 0; i < 16; i++)
        diff |= smb2man_digest[i] ^ smb2man_reply_sig[i];
    return diff ? -1 : 0;
}
#define USMB2_VERIFY_BEGIN(u, hdr) smb2man_verify_begin((u)->session_key, hdr)
#define USMB2_VERIFY_UPDATE(p, n)  smb2auth_hmac_update(&smb2man_mac, p, n)
#define USMB2_VERIFY_END()         smb2man_verify_end()

#undef read
#undef write
#undef close
#define read(a, b, c)  smb2man_sock_read(a, b, c)
#define write(a, b, c) smb2man_sock_write(a, b, c)
#define close(a)       smb2man_sock_close(a)
#include "usmb2/usmb2.c"
// The IOP faults on misaligned word and halfword access, which usmb2 makes all over buf.
_Static_assert(offsetof(struct usmb2_context, buf) % 8 == 0, "usmb2_context.buf must stay aligned");
#undef read
#undef write
#undef close

#define STATUS_NO_MORE_FILES          0x80000006
#define STATUS_NO_SUCH_FILE           0xC000000F
#define STATUS_OBJECT_NAME_INVALID    0xC0000033
#define STATUS_OBJECT_NAME_NOT_FOUND  0xC0000034
#define STATUS_OBJECT_NAME_COLLISION  0xC0000035
#define STATUS_OBJECT_PATH_NOT_FOUND  0xC000003A
#define STATUS_ACCESS_DENIED          0xC0000022
#define STATUS_SHARING_VIOLATION      0xC0000043
#define STATUS_DELETE_PENDING         0xC0000056
#define STATUS_DISK_FULL              0xC000007F
#define STATUS_FILE_IS_A_DIRECTORY    0xC00000BA
#define STATUS_NETWORK_NAME_DELETED   0xC00000C9
#define STATUS_DIRECTORY_NOT_EMPTY    0xC0000101
#define STATUS_NOT_A_DIRECTORY        0xC0000103
#define STATUS_USER_SESSION_DELETED   0xC0000203
#define STATUS_NETWORK_SESSION_EXPIRED 0xC000035C

//-------------------------------------------------------------------------
/*
  One connected share. name[0] == 0: a free slot. u is NULL while the session is down (never
  connected again since the server dropped it); the next call that uses the share connects it
  from the rest, which CONNECT kept.
*/
struct smb2man_share
{
    struct usmb2_context *u;
    unsigned gen;                // nonzero while u is up; open directories keep it
    uint32_t ip;                 // network order
    uint32_t io_read_max, io_write_max;
    uint32_t retry_at;           // smb2man_seconds() before which no reconnect is tried
    uint16_t port, dialect;
    char name[SMB2_MAX_NAME_LEN];
    char ipstr[16];
    char user[32], password[32]; // Latin-1, as usmb2's NTLM code takes them
    char share[112];             // TREE_CONNECT's \\ip\share fits in 127 bytes
};

static struct smb2man_share shares[SMB2MAN_SHARES];
static unsigned session_gen = 0;
static int smb2_sema = -1;

struct smb2man_file
{
    uint8_t fid[16];
    uint64_t pos, size;
    int share; // index in shares[], or -1 for a free slot
    int writable;
    unsigned gen; // bumped on every open of this slot; privdata carries it (see smb2man_slot)
};

// privdata = slot | gen << SMB2MAN_SLOT_BITS. MAX_OPEN_FILES must fit in the slot bits.
#define SMB2MAN_SLOT_BITS 4
#define SMB2MAN_GEN_MASK  (0x7FFFFFFFu >> SMB2MAN_SLOT_BITS)

static struct smb2man_file file_table[MAX_OPEN_FILES];

// An open directory: its handle on the server and the part of the listing not yet returned.
// The device root (share < 0) lists the connected names, off counting slots; it has no data.
struct smb2man_dir
{
    uint8_t fid[16];
    int share;
    unsigned gen; // the share's gen at dopen: a directory of an ended session gets -EBADF
    int len, off, eof;
    uint8_t data[SMB2MAN_DIRBUF];
};

#define LOCK()   WaitSema(smb2_sema)
#define UNLOCK() SignalSema(smb2_sema)

//-------------------------------------------------------------------------
// Allocator for usmb2 (the IOP has no libc heap).
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

// Seconds since the IOP started, for the reconnect backoff. tests/smb2man_live sets the clock.
#ifndef SMB2MAN_SECONDS
static uint32_t smb2man_seconds(void)
{
#ifdef _IOP
    iop_sys_clock_t clock;
    u32 sec, usec;

    GetSystemTime(&clock);
    SysClock2USec(&clock, &sec, &usec);
    return sec;
#else
    return (uint32_t)time(NULL);
#endif
}
#define SMB2MAN_SECONDS() smb2man_seconds()
#endif

//-------------------------------------------------------------------------
/*
  A user name or password from the EE (UTF-8) as usmb2's NTLM code takes it: one byte per UTF-16
  code unit, so Latin-1. Characters above U+00FF, malformed UTF-8 and anything that does not fit
  fail: the logon is refused rather than sent with credentials the server would not recognise.
*/
static int smb2man_latin1(char *dst, const char *src, unsigned size)
{
    const unsigned char *s = (const unsigned char *)src;
    unsigned n = 0;

    while (*s) {
        unsigned c = *s++;

        if (c >= 0x80) {
            if ((c & 0xe0) != 0xc0 || (*s & 0xc0) != 0x80)
                return -1;
            c = ((c & 0x1f) << 6) | (*s++ & 0x3f);
            if (c < 0x80 || c > 0xff)
                return -1;
        }
        if (n + 1 >= size)
            return -1;
        dst[n++] = (char)c;
    }
    dst[n] = '\0';
    return (int)n;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static uint16_t smb2man_utf16[SMB2MAN_NAME_MAX + 2]; // fixpath's check, used under the lock

/*
  A path within a share as SMB2 wants it: backslashes, relative to the share root, with no
  separator at either end. Returns -ENAMETOOLONG rather than truncating, so an over-long name can
  never open a different, shorter file.
*/
static int smb2man_fixpath(char *out, const char *in)
{
    int i = 0;

    while (*in == '\\' || *in == '/')
        in++;

    while (*in != '\0') {
        if (i >= SMB2MAN_NAME_MAX)
            return -ENAMETOOLONG;
        out[i++] = (*in == '/') ? '\\' : *in;
        in++;
    }
    while (i > 0 && out[i - 1] == '\\')
        i--;

    out[i] = '\0';
    // usmb2 writes the name as UTF-16 and trusts it to be UTF-8: a byte sequence that is not would
    // come back as a negative length. Refuse it here instead.
    if (utf8_to_utf16(out, smb2man_utf16) < 0)
        return -EINVAL;
    return 0;
}

// A negative errno for the last failed request, from the server's status.
static int smb2man_errno(const struct usmb2_context *u, int fallback)
{
    if (u == NULL || u->fd < 0)
        return -EIO;

    switch (u->status) {
        case STATUS_NO_SUCH_FILE:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
            return -ENOENT;
        case STATUS_OBJECT_NAME_COLLISION:
            return -EEXIST;
        case STATUS_ACCESS_DENIED:
            return -EACCES;
        case STATUS_SHARING_VIOLATION:
        case STATUS_DELETE_PENDING:
            return -EBUSY;
        case STATUS_DISK_FULL:
            return -ENOSPC;
        case STATUS_FILE_IS_A_DIRECTORY:
            return -EISDIR;
        case STATUS_NOT_A_DIRECTORY:
            return -ENOTDIR;
        case STATUS_DIRECTORY_NOT_EMPTY:
            return -ENOTEMPTY;
        case STATUS_OBJECT_NAME_INVALID:
            return -EINVAL;
    }
    return fallback;
}

//-------------------------------------------------------------------------
// End share s's session without waiting on the network: its open files and directories go with
// it. Called with the lock held.
static void smb2man_free_session(int s)
{
    struct smb2man_share *sh = &shares[s];
    int i;

    for (i = 0; i < MAX_OPEN_FILES; i++)
        if (file_table[i].share == s)
            file_table[i].share = -1;
    if (sh->u != NULL) {
        if (sh->u->fd >= 0)
            smb2man_sock_close(sh->u->fd);
        free(sh->u);
        sh->u = NULL;
    }
    sh->gen = 0;
}

/*
  After every failed request, with the lock held: when the socket died under it (usmb2 closes it
  on a failed read or write, and on a reply that never came), or the server says the session or
  share is gone (an idle Windows server logs sessions off after 15 minutes), the session ends. The
  share stays connected by name; its next use connects it again.
*/
static void smb2man_check_session(int s)
{
    struct usmb2_context *u = shares[s].u;

    if (u == NULL)
        return;
    if (u->fd >= 0 && u->status != STATUS_USER_SESSION_DELETED && u->status != STATUS_NETWORK_NAME_DELETED &&
        u->status != STATUS_NETWORK_SESSION_EXPIRED)
        return;
    smb2man_free_session(s);
}

// Close share s's open files on the server, then end its session. Called with the lock held.
static void smb2man_teardown(int s)
{
    struct usmb2_context *u = shares[s].u;
    int i;

    for (i = 0; u != NULL && u->fd >= 0 && i < MAX_OPEN_FILES; i++)
        if (file_table[i].share == s)
            usmb2_close(u, file_table[i].fid);
    smb2man_free_session(s);
}

//-------------------------------------------------------------------------
/*
  NEGOTIATE offering two dialects, as RiptOPL's in-game reader does: 2.0.2 and 2.1 first, then
  3.0 and 3.0.2 on a new connection when a server refuses 2.x. 2.x signs with HMAC-SHA256, which is
  all this driver implements, so a server that requires signing (Windows 11 24H2) stays on 2.1.
  Returns the dialect, -2 when the server supports neither, or -1. *sign_required: the server's ask.
*/
static int smb2man_negotiate(struct smb2man_share *sh, struct usmb2_context *u, uint16_t d0, uint16_t d1,
                             int *sign_required)
{
    uint8_t *p = &u->buf[4 + 64];
    int dialect;

    u->message_id = 0;
    u->session_id = 0;
    u->tree_id = 0;
    u->signing = 0;
    memset(u->buf, 0, sizeof(u->buf));
    // StructureSize 36, DialectCount 2, SecurityMode SIGNING_ENABLED, no capabilities
    *(uint32_t *)p = htole32(0x00020024);
    p[4] = 0x01;
    p[12] = 0xaa; // ClientGuid, as usmb2's
    p[27] = 0xbb;
    p[36] = (uint8_t)d0;
    p[37] = (uint8_t)(d0 >> 8);
    p[38] = (uint8_t)d1;
    p[39] = (uint8_t)(d1 >> 8);

    if (usmb2_build_request(u, CMD_NEGOTIATE_PROTOCOL, 40, 64, NULL, 0, NULL, 0)) {
        if (u->fd >= 0 && u->status == 0xC00000BB) // STATUS_NOT_SUPPORTED
            return -2;
        return -1;
    }

    dialect = u->buf[4] | (u->buf[5] << 8);
    if (dialect != d0 && dialect != d1)
        return -1;
    *sign_required = (u->buf[2] & 0x02) ? 1 : 0; // SMB2_NEGOTIATE_SIGNING_REQUIRED
    sh->io_read_max = rd32(&u->buf[32]);
    sh->io_write_max = rd32(&u->buf[36]);
    if (sh->io_read_max == 0 || sh->io_read_max > SMB2MAN_IO_MAX)
        sh->io_read_max = SMB2MAN_IO_MAX;
    if (sh->io_write_max == 0 || sh->io_write_max > SMB2MAN_IO_MAX)
        sh->io_write_max = SMB2MAN_IO_MAX;

    return dialect;
}

// TCP connect with a deadline: non-blocking connect, then wait until the socket is writable.
static int smb2man_tcp_connect(const struct smb2man_share *sh)
{
    struct sockaddr_in sin;
    int fd, err = 0, on = 1;
    socklen_t errlen = sizeof(err);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(sh->port);
    sin.sin_addr.s_addr = sh->ip;

    // Stays non-blocking: see smb2man_sock_read().
    SMB2MAN_IOCTL(fd, FIONBIO, &on);
    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0 &&
        (smb2man_wait(fd, 1, SMB2MAN_CONNECT_MS) <= 0 ||
         getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) != 0 || err != 0)) {
        smb2man_sock_close(fd);
        return -1;
    }

    return fd;
}

// Connect share s and log on. Returns 0 or -SMB2_CONNECT_ERR_*. Called with the lock held.
static int smb2man_connect(int s)
{
    struct smb2man_share *sh = &shares[s];
    struct usmb2_context *u;
    char unc[2 + sizeof(sh->ipstr) + 1 + sizeof(sh->share)];
    int rv, sign_required = 0;

    smb2man_free_session(s);
    sprintf(unc, "\\\\%s\\%s", sh->ipstr, sh->share);

    u = calloc(1, sizeof(*u));
    if (u == NULL)
        return -SMB2_CONNECT_ERR_MEMORY;

    rv = -SMB2_CONNECT_ERR_CONN;
    u->fd = smb2man_tcp_connect(sh);
    if (u->fd < 0)
        goto fail;
    // A blank user logs on as "Guest" with the password, as RiptOPL's readers do.
    strcpy(u->username, sh->user[0] ? sh->user : "Guest");
    strcpy(u->password, sh->password);

    rv = smb2man_negotiate(sh, u, 0x0202, 0x0210, &sign_required);
    if (rv == -2) {
        smb2man_sock_close(u->fd);
        u->fd = smb2man_tcp_connect(sh);
        rv = u->fd < 0 ? -1 : smb2man_negotiate(sh, u, 0x0300, 0x0302, &sign_required);
    }
    if (rv < 0) {
        rv = -SMB2_CONNECT_ERR_PROT;
        goto fail;
    }
    // 3.x signs with AES-CMAC, which this driver does not implement: refuse rather than send unsigned.
    if (rv >= 0x0300 && sign_required) {
        rv = -SMB2_CONNECT_ERR_SIGNING;
        goto fail;
    }
    if (usmb2_sessionsetup(u) != 0) {
        rv = -SMB2_CONNECT_ERR_LOGON;
        goto fail;
    }
    // A guest or null session has no key to sign with; servers do not require it of them.
    u->signing = (sign_required && !(rd16(&u->buf[2]) & 0x0003)) ? 1 : 0;
    if (usmb2_treeconnect(u, unc) != 0) {
        rv = -SMB2_CONNECT_ERR_SHARE;
        goto fail;
    }

    sh->u = u;
    sh->dialect = (uint16_t)rv;
    sh->gen = ++session_gen ? session_gen : ++session_gen;
    printf("smb2man: %s connected, SMB dialect 0x%04x\n", sh->name, rv);
    return 0;

fail:
    if (u->fd >= 0)
        smb2man_sock_close(u->fd);
    free(u);
    return rv;
}

/*
  Share s ready for a request, with the lock held: a session the server closed while it sat idle
  (its socket reads as ready with no request out) is ended, and a session that is down is
  connected again, unless the last try failed under SMB2MAN_RETRY_SECONDS ago. Returns 0 or -EIO.
*/
static int smb2man_ready(int s)
{
    struct smb2man_share *sh = &shares[s];

    if (sh->u != NULL && sh->u->fd >= 0 && smb2man_wait(sh->u->fd, 0, 0) == 0)
        return 0;
    smb2man_free_session(s);
    if ((int32_t)(SMB2MAN_SECONDS() - sh->retry_at) < 0)
        return -EIO;
    if (smb2man_connect(s) < 0) {
        sh->retry_at = SMB2MAN_SECONDS() + SMB2MAN_RETRY_SECONDS;
        return -EIO;
    }
    return 0;
}

/*
  The share a device path names ("/NAME/rest"), with the lock held and its session ready.
  Returns its index and points *rest past the name, or a negative errno without the lock.
  -ENOENT: no share by that name. root (when not NULL) is set when the path is the share itself.
*/
static int smb2man_lock_path(const char *path, const char **rest, int *root)
{
    const char *p, *end;
    size_t n;
    int s, rv;

    if (path == NULL)
        return -EINVAL;
    for (p = path; *p == '/' || *p == '\\'; p++)
        ;
    for (end = p; *end && *end != '/' && *end != '\\'; end++)
        ;
    n = (size_t)(end - p);

    LOCK();
    for (s = 0; s < SMB2MAN_SHARES; s++)
        if (n > 0 && shares[s].name[0] && strlen(shares[s].name) == n && !strncmp(shares[s].name, p, n))
            break;
    if (s == SMB2MAN_SHARES) {
        UNLOCK();
        return n ? -ENOENT : -EACCES; // the device root holds no files
    }
    for (p = end; *p == '/' || *p == '\\'; p++)
        ;
    if (root != NULL)
        *root = (*p == '\0');
    else if (*p == '\0') { // the share itself can only be listed, stat'ed or opened as a folder
        UNLOCK();
        return -EACCES;
    }
    rv = smb2man_ready(s);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }
    *rest = p;
    return s;
}

// The file's size from the server (QUERY_INFO FileStandardInformation), all 64 bits.
static int smb2man_query_size(struct usmb2_context *u, const uint8_t *fid, uint64_t *size)
{
    uint8_t *p = &u->buf[4 + 64];

    memset(u->buf, 0, sizeof(u->buf));
    *(uint32_t *)p = htole32(0x05010029);       // StructureSize 41, SMB2_0_INFO_FILE, FileStandardInformation
    *(uint32_t *)(p + 4) = htole32(0x00000018); // OutputBufferLength: the 24 bytes of the reply
    memcpy(p + 24, fid, 16);
    if (usmb2_build_request(u, CMD_QUERYINFO, 40, 8, NULL, 0, NULL, 0))
        return -1;

    *size = rd64(&u->buf[16]); // EndOfFile, after AllocationSize
    return 0;
}

//-------------------------------------------------------------------------
static int smb2man_init(iop_device_t *dev)
{
    iop_sema_t smp;
    int i;

    (void)dev;

    smp.initial = 1;
    smp.max = 1;
    smp.option = 0;
    smp.attr = 1;
    smb2_sema = CreateSema(&smp);

    for (i = 0; i < MAX_OPEN_FILES; i++)
        file_table[i].share = -1;

    return smb2_sema < 0 ? -1 : 0;
}

// iomanX calls this only when the "smb" device is deleted, which never happens while smb2man is
// resident (it goes only with an IOP reset).
static int smb2man_deinit(iop_device_t *dev)
{
    (void)dev;

    if (smb2_sema >= 0) {
        DeleteSema(smb2_sema);
        smb2_sema = -1;
    }

    return 0;
}

//-------------------------------------------------------------------------
/*
  A handle whose slot was cleared (a dropped session, DISCONNECT_ALL) and then reused by a later
  open must not reach the new file: the slot's generation no longer matches the one stored in the
  stale handle's privdata, so it gets -EBADF instead. Called with the lock held.
*/
static int smb2man_slot(iop_file_t *f)
{
    unsigned v = (unsigned)(uintptr_t)f->privdata;
    unsigned slot = v & ((1u << SMB2MAN_SLOT_BITS) - 1);

    if (slot >= MAX_OPEN_FILES || file_table[slot].share < 0 || file_table[slot].gen != (v >> SMB2MAN_SLOT_BITS))
        return -1;

    return (int)slot;
}

/*
  The CREATE disposition from the open flags: without O_CREAT no new file could be made, and
  without O_TRUNC a shorter rewrite would keep the old tail.
*/
static uint8_t smb2man_disposition(int flags)
{
    if (flags & FIO_O_CREAT) {
        if (flags & FIO_O_EXCL)
            return 0x02;                            // FILE_CREATE
        return (flags & FIO_O_TRUNC) ? 0x05 : 0x03; // FILE_OVERWRITE_IF : FILE_OPEN_IF
    }
    return (flags & FIO_O_TRUNC) ? 0x04 : 0x01; // FILE_OVERWRITE : FILE_OPEN
}

static int smb2man_open(iop_file_t *f, const char *filename, int flags, int mode)
{
    char path[MAX_PATH_LEN];
    struct smb2man_file *file;
    struct usmb2_context *u;
    const char *rest;
    uint8_t *fid;
    int s, slot, rv, writable;

    (void)mode;

    s = smb2man_lock_path(filename, &rest, NULL);
    if (s < 0)
        return s;
    u = shares[s].u;

    for (slot = 0; slot < MAX_OPEN_FILES && file_table[slot].share >= 0; slot++)
        ;
    rv = (slot == MAX_OPEN_FILES) ? -EMFILE : smb2man_fixpath(path, rest);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }

    // FIO_O_WRONLY's bit is in FIO_O_RDWR too. A read-only handle refuses writes itself (see write).
    writable = (flags & FIO_O_WRONLY) != 0;
    // Desired access: READ_DATA, READ_EA, READ_ATTRIBUTES, and WRITE_DATA for a writable handle.
    fid = usmb2_create(u, path, writable ? 0x8b : 0x89,
                       writable ? smb2man_disposition(flags) : 0x01, 0x40 /* non-directory */, 0x00, 0, 16);
    if (fid == NULL) {
        rv = smb2man_errno(u, -ENOENT);
        smb2man_check_session(s);
        UNLOCK();
        return rv;
    }

    file = &file_table[slot];
    memcpy(file->fid, fid, 16);
    free(fid);
    file->size = rd64(&u->buf[48]); // EndOfFile in the CREATE reply
    file->pos = (flags & FIO_O_APPEND) ? file->size : 0;
    file->writable = writable;
    file->share = s;
    file->gen = (file->gen + 1) & SMB2MAN_GEN_MASK;
    f->privdata = (void *)(uintptr_t)((unsigned)slot | (file->gen << SMB2MAN_SLOT_BITS));

    UNLOCK();

    return slot;
}

// The open file f names, with the lock held, or a negative errno without it.
static int smb2man_lock_file(iop_file_t *f)
{
    int slot;

    LOCK();
    slot = smb2man_slot(f);
    if (slot < 0) {
        UNLOCK();
        return -EBADF;
    }
    return slot;
}

static int smb2man_close(iop_file_t *f)
{
    struct usmb2_context *u;
    int slot, s;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;

    s = file_table[slot].share;
    u = shares[s].u;
    if (u->fd >= 0) {
        usmb2_close(u, file_table[slot].fid);
        smb2man_check_session(s);
    }
    file_table[slot].share = -1;

    UNLOCK();

    return 0;
}

/*
  One READ or WRITE moves at most io_read_max/io_write_max (64 KB), so loop until the whole
  request is done. A read stops early only at the end of the file.
*/
static int smb2man_read(iop_file_t *f, void *buffer, int size)
{
    struct smb2man_file *file;
    struct smb2man_share *sh;
    uint8_t *p = (uint8_t *)buffer;
    int slot, rv, n, done = 0;

    if (size < 0)
        return -EINVAL;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];
    sh = &shares[file->share];

    while (done < size) {
        n = size - done;
        if ((uint32_t)n > sh->io_read_max)
            n = (int)sh->io_read_max;
        rv = usmb2_pread(sh->u, file->fid, p + done, n, file->pos);
        if (rv < 0 && sh->u->status == STATUS_END_OF_FILE && sh->u->fd >= 0)
            break;
        if (rv < 0) {
            smb2man_check_session(file->share);
            UNLOCK();
            return (done > 0) ? done : -EIO;
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

static int smb2man_write(iop_file_t *f, void *buffer, int size)
{
    struct smb2man_file *file;
    struct smb2man_share *sh;
    uint8_t *p = (uint8_t *)buffer;
    int slot, rv, n, done = 0;

    if (size < 0)
        return -EINVAL;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];
    sh = &shares[file->share];
    if (!file->writable) {
        UNLOCK();
        return -EBADF;
    }

    while (done < size) {
        n = size - done;
        if ((uint32_t)n > sh->io_write_max)
            n = (int)sh->io_write_max;
        rv = usmb2_pwrite(sh->u, file->fid, p + done, n, file->pos);
        if (rv <= 0) {
            // A server that stores nothing (a full disk) must not spin this loop forever.
            smb2man_check_session(file->share);
            UNLOCK();
            return (done > 0) ? done : -EIO;
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

// The new position for lseek/lseek64, or a negative errno. Called with the lock held.
static s64 smb2man_seek(struct smb2man_file *file, s64 offset, int whence)
{
    uint64_t size;
    s64 base;

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (s64)file->pos;
            break;
        case SEEK_END:
            // Another client may have changed the file: ask the server, else use what is known.
            if (smb2man_query_size(shares[file->share].u, file->fid, &size) == 0)
                file->size = size;
            else
                smb2man_check_session(file->share);
            base = (s64)file->size;
            break;
        default:
            return -EINVAL;
    }
    if (base + offset < 0)
        return -EINVAL;

    file->pos = (uint64_t)(base + offset);
    return (s64)file->pos;
}

static s64 smb2man_lseek64(iop_file_t *f, s64 offset, int whence)
{
    s64 pos;
    int slot;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    pos = smb2man_seek(&file_table[slot], offset, whence);
    UNLOCK();

    return pos;
}

static int smb2man_lseek(iop_file_t *f, int offset, int whence)
{
    s64 pos = smb2man_lseek64(f, offset, whence);

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
static void smb2man_pack_time(uint64_t filetime, unsigned char *out)
{
    // FILETIME counts 100 ns from 1601; Unix seconds from 1970.
    u32 t = (filetime > 116444736000000000ULL) ? (u32)((filetime - 116444736000000000ULL) / 10000000u) : 0;
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

static void smb2man_dir_stat(iox_stat_t *stat)
{
    memset(stat, 0, sizeof(iox_stat_t));
    stat->mode = FIO_S_IFDIR | FIO_S_IRUSR | FIO_S_IWUSR | FIO_S_IXUSR;
}

/*
  The stat from SMB2's four FILETIMEs (creation, last access, last write, change, in that order at
  times), the size and the attributes. The PS2's ctime is the creation time, or the change time
  when the server does not report one.
*/
static void smb2man_fill_stat(iox_stat_t *stat, const uint8_t *times, uint64_t size, uint32_t attributes)
{
    uint64_t btime = rd64(times);

    smb2man_dir_stat(stat);
    if (!(attributes & 0x10))
        stat->mode = FIO_S_IFREG | FIO_S_IRUSR | FIO_S_IWUSR;
    stat->size = (unsigned int)size;
    stat->hisize = (unsigned int)(size >> 32);
    smb2man_pack_time(btime ? btime : rd64(times + 24), stat->ctime);
    smb2man_pack_time(rd64(times + 8), stat->atime);
    smb2man_pack_time(rd64(times + 16), stat->mtime);
}

// A name from the listing, UTF-16LE, to UTF-8 in out (outmax bytes with the NUL); never splits a
// character. An unpaired surrogate becomes U+FFFD.
static void smb2man_name_utf8(char *out, int outmax, const uint8_t *in, int units)
{
    int i = 0, o = 0, n;
    uint32_t c, c2;

    while (i < units) {
        c = in[2 * i] | (in[2 * i + 1] << 8);
        i++;
        if (c >= 0xd800 && c < 0xdc00 && i < units) {
            c2 = in[2 * i] | (in[2 * i + 1] << 8);
            if (c2 >= 0xdc00 && c2 < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
                i++;
            }
        }
        if (c >= 0xd800 && c < 0xe000)
            c = 0xfffd;
        n = (c < 0x80) ? 1 : (c < 0x800) ? 2 :
                         (c < 0x10000)   ? 3 :
                                           4;
        if (o + n >= outmax)
            break;
        if (n == 1) {
            out[o++] = (char)c;
        } else if (n == 2) {
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else if (n == 3) {
            out[o++] = (char)(0xe0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[o++] = (char)(0xf0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 0x3f));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[o] = '\0';
}

// True for a path that names the device root ("", "/").
static int smb2man_is_root(const char *path)
{
    while (*path == '/' || *path == '\\')
        path++;
    return *path == '\0';
}

//-------------------------------------------------------------------------
static int smb2man_dopen(iop_file_t *f, const char *dirname)
{
    char path[MAX_PATH_LEN];
    struct smb2man_dir *d;
    const char *rest;
    uint8_t *fid;
    int s, rv;

    if (dirname == NULL)
        return -EINVAL;

    if (smb2man_is_root(dirname)) {
        // The connected names: no server handle and no listing buffer, so only the head of the
        // struct is allocated (through a void *, which GCC's -Walloc-size accepts).
        void *head = malloc(offsetof(struct smb2man_dir, data));
        if (head == NULL)
            return -ENOMEM;
        d = head;
        d->share = -1;
        d->off = 0;
        f->privdata = d;
        return 0;
    }

    s = smb2man_lock_path(dirname, &rest, &rv);
    if (s < 0)
        return s;

    rv = smb2man_fixpath(path, rest);
    d = (rv < 0) ? NULL : malloc(sizeof(*d));
    if (d == NULL) {
        UNLOCK();
        return rv < 0 ? rv : -ENOMEM;
    }
    // LIST_DIRECTORY and READ_ATTRIBUTES, open an existing directory.
    fid = usmb2_create(shares[s].u, path, 0x81, 0x01, 0x01 /* directory */, 0x10, 0, 16);
    if (fid == NULL) {
        rv = smb2man_errno(shares[s].u, -ENOENT);
        free(d);
        smb2man_check_session(s);
        UNLOCK();
        return rv;
    }
    memcpy(d->fid, fid, 16);
    free(fid);
    d->share = s;
    d->gen = shares[s].gen;
    d->len = d->off = d->eof = 0;

    f->privdata = (void *)d;

    UNLOCK();

    return 0;
}

static int smb2man_dclose(iop_file_t *f)
{
    struct smb2man_dir *d;

    LOCK();

    d = (struct smb2man_dir *)f->privdata;
    if (d == NULL) {
        UNLOCK();
        return -EBADF;
    }

    // A directory of an ended session has nothing left on the server to close.
    if (d->share >= 0 && shares[d->share].u != NULL && shares[d->share].gen == d->gen) {
        usmb2_close(shares[d->share].u, d->fid);
        smb2man_check_session(d->share);
    }
    free(d);
    f->privdata = NULL;

    UNLOCK();

    return 0;
}

// Fetch the next part of the listing (QUERY_DIRECTORY, FileDirectoryInformation, "*").
static int smb2man_querydir(struct smb2man_dir *d)
{
    struct usmb2_context *u = shares[d->share].u;
    uint8_t *p = &u->buf[4 + 64];
    uint32_t status, len;

    memset(u->buf, 0, sizeof(u->buf));
    *(uint32_t *)p = htole32(0x00010021); // StructureSize 33, FileDirectoryInformation, no flags
    memcpy(p + 8, d->fid, 16);
    p[24] = 0x60; // FileNameOffset
    p[26] = 0x02; // FileNameLength: "*" in UTF-16
    *(uint32_t *)(p + 28) = htole32(SMB2MAN_DIRBUF);
    p[32] = '*';

    status = usmb2_build_request(u, CMD_QUERYDIR, 40, 8, NULL, 0, d->data, SMB2MAN_DIRBUF);
    if (status == STATUS_NO_MORE_FILES && u->fd >= 0) {
        d->eof = 1;
        return 0;
    }
    if (status != 0) {
        smb2man_check_session(d->share);
        return -EIO;
    }

    len = rd32(&u->buf[4]); // OutputBufferLength
    d->len = (len > SMB2MAN_DIRBUF) ? SMB2MAN_DIRBUF : (int)len;
    d->off = 0;
    if (d->len == 0)
        d->eof = 1;
    return 0;
}

static int smb2man_dread(iop_file_t *f, iox_dirent_t *dirent)
{
    struct smb2man_dir *d;
    const uint8_t *e;
    uint32_t next, namelen;
    int rv;

    LOCK();

    d = (struct smb2man_dir *)f->privdata;
    if (d != NULL && d->share < 0) {
        // The device root: one folder per connected share.
        while (d->off < SMB2MAN_SHARES && !shares[d->off].name[0])
            d->off++;
        rv = 0;
        if (d->off < SMB2MAN_SHARES) {
            smb2man_dir_stat(&dirent->stat);
            strcpy(dirent->name, shares[d->off++].name);
            dirent->privdata = NULL;
            rv = 1;
        }
        UNLOCK();
        return rv;
    }
    if (d == NULL || shares[d->share].u == NULL || d->gen != shares[d->share].gen) {
        UNLOCK();
        return -EBADF;
    }

    while (d->off + 64 > d->len) {
        if (d->eof) {
            UNLOCK();
            return 0; // end of the directory
        }
        rv = smb2man_querydir(d);
        if (rv < 0) {
            UNLOCK();
            return rv;
        }
    }

    // [MS-FSCC] 2.4.10 FileDirectoryInformation
    e = &d->data[d->off];
    next = rd32(e);
    namelen = rd32(e + 60);
    if (namelen > (uint32_t)(d->len - d->off - 64))
        namelen = (uint32_t)(d->len - d->off - 64);
    d->off = (next >= 64 && next <= (uint32_t)(d->len - d->off)) ? d->off + (int)next : d->len;

    smb2man_fill_stat(&dirent->stat, e + 8, rd64(e + 40), rd32(e + 56));
    smb2man_name_utf8(dirent->name, sizeof(dirent->name), e + 64, (int)(namelen / 2));
    dirent->privdata = NULL;

    UNLOCK();

    return 1;
}

static int smb2man_getstat(iop_file_t *f, const char *filename, iox_stat_t *stat)
{
    char path[MAX_PATH_LEN];
    uint8_t st[56]; // FileNetworkOpenInformation
    const char *rest;
    int s, rv, root;

    (void)f;

    if (filename == NULL || stat == NULL)
        return -EINVAL;
    if (smb2man_is_root(filename)) {
        smb2man_dir_stat(stat);
        return 0;
    }

    s = smb2man_lock_path(filename, &rest, &root);
    if (s < 0)
        return s;

    rv = smb2man_fixpath(path, rest);
    if (rv == 0 && root) {
        smb2man_dir_stat(stat); // the share itself, already known to answer
    } else if (rv == 0 && usmb2_stat(shares[s].u, path, st) != 0) {
        rv = smb2man_errno(shares[s].u, -ENOENT);
        smb2man_check_session(s);
    } else if (rv == 0) {
        smb2man_fill_stat(stat, st, rd64(st + 40), rd32(st + 48));
    }

    UNLOCK();

    return rv;
}

//-------------------------------------------------------------------------
// Directory and file management. The device root and the shares themselves cannot be changed.
static int smb2man_mkdir(iop_file_t *f, const char *dirname, int mode)
{
    char path[MAX_PATH_LEN];
    struct usmb2_context *u;
    const char *rest;
    uint8_t st[56];
    int s, rv;

    (void)f;
    (void)mode;

    s = smb2man_lock_path(dirname, &rest, NULL);
    if (s < 0)
        return s;
    u = shares[s].u;

    rv = smb2man_fixpath(path, rest);
    if (rv == 0 && usmb2_mkdir(u, path) != 0) {
        rv = smb2man_errno(u, -EIO);
        // An existing folder is not a failure.
        if (u->fd >= 0 && usmb2_stat(u, path, st) == 0 && (rd32(st + 48) & 0x10))
            rv = 0;
        smb2man_check_session(s);
    }

    UNLOCK();

    return rv;
}

/*
  rmdir and remove: DELETE_ON_CLOSE on a directory or a file. A server takes DELETE_ON_CLOSE on a
  folder that is not empty and then keeps the folder at CLOSE without an error (Samba does), so
  an rmdir is checked: a folder still there is -ENOTEMPTY, never a success.
*/
static int smb2man_delete(const char *name, int dir)
{
    char path[MAX_PATH_LEN];
    const char *rest;
    uint8_t st[56];
    int s, rv;

    s = smb2man_lock_path(name, &rest, NULL);
    if (s < 0)
        return s;

    rv = smb2man_fixpath(path, rest);
    if (rv == 0 && (dir ? usmb2_rmdir(shares[s].u, path) : usmb2_unlink(shares[s].u, path)) != 0) {
        rv = smb2man_errno(shares[s].u, -EIO);
        smb2man_check_session(s);
    } else if (rv == 0 && dir && usmb2_stat(shares[s].u, path, st) == 0) {
        rv = -ENOTEMPTY;
    }

    UNLOCK();

    return rv;
}

static int smb2man_rmdir(iop_file_t *f, const char *dirname)
{
    (void)f;
    return smb2man_delete(dirname, 1);
}

static int smb2man_remove(iop_file_t *f, const char *filename)
{
    (void)f;
    return smb2man_delete(filename, 0);
}

static int smb2man_rename(iop_file_t *f, const char *oldname, const char *newname)
{
    char oldpath[MAX_PATH_LEN];
    char newpath[MAX_PATH_LEN];
    const char *oldrest, *newrest;
    int s, rv;

    (void)f;

    if (newname == NULL)
        return -EINVAL;
    s = smb2man_lock_path(oldname, &oldrest, NULL);
    if (s < 0)
        return s;
    // Within one share only, as SMB2 renames: newname must start with the same "/NAME/".
    for (newrest = newname; *newrest == '/' || *newrest == '\\'; newrest++)
        ;
    rv = strlen(shares[s].name);
    if (strncmp(newrest, shares[s].name, rv) || (newrest[rv] != '/' && newrest[rv] != '\\')) {
        UNLOCK();
        return -EINVAL;
    }
    newrest += rv;

    rv = smb2man_fixpath(oldpath, oldrest);
    if (rv == 0)
        rv = smb2man_fixpath(newpath, newrest);
    if (rv == 0 && newpath[0] == '\0')
        rv = -EINVAL;
    if (rv == 0 && usmb2_rename(shares[s].u, oldpath, newpath) != 0) {
        rv = smb2man_errno(shares[s].u, -EIO);
        smb2man_check_session(s);
    }

    UNLOCK();

    return rv;
}

//-------------------------------------------------------------------------
/*
  SMB2_DEVCTL_CONNECT's URL: smb://<IPv4>[:port]/<share>[/anything], anything after the share
  ignored, as the libsmb2 driver did. Fills ip, ipstr, port and share. Returns 0 or -1.
*/
static int smb2man_parse_url(struct smb2man_share *sh, const char *url)
{
    const char *host, *p;
    size_t n;
    unsigned port = 445;

    if ((url[0] | 0x20) != 's' || (url[1] | 0x20) != 'm' || (url[2] | 0x20) != 'b' || strncmp(url + 3, "://", 3))
        return -1;
    host = url + 6;
    for (p = host; *p && *p != ':' && *p != '/'; p++)
        ;
    n = (size_t)(p - host);
    if (n == 0 || n >= sizeof(sh->ipstr))
        return -1;
    memcpy(sh->ipstr, host, n);
    sh->ipstr[n] = '\0';
    sh->ip = inet_addr(sh->ipstr);
    if (sh->ip == 0xffffffffu || sh->ip == 0) // an IP address only: the driver has no DNS
        return -1;
    if (*p == ':') {
        for (port = 0, p++; *p >= '0' && *p <= '9' && port <= 65535; p++)
            port = port * 10 + (unsigned)(*p - '0');
        if (port == 0 || port > 65535)
            return -1;
    }
    if (*p++ != '/')
        return -1;
    for (host = p; *p && *p != '/'; p++)
        ;
    n = (size_t)(p - host);
    // TREE_CONNECT's \\ip\share is at most 127 bytes.
    if (n == 0 || n >= sizeof(sh->share) || 3 + strlen(sh->ipstr) + n > 127)
        return -1;
    memcpy(sh->share, host, n);
    sh->share[n] = '\0';
    sh->port = (uint16_t)port;
    return 0;
}

// SMB2_DEVCTL_CONNECT: 0, -EEXIST for a name already connected, or -SMB2_CONNECT_ERR_*.
static int smb2man_connect_devctl(const smb2Connect_in_t *in)
{
    struct smb2man_share *sh;
    char name[SMB2_MAX_NAME_LEN], user[sizeof(in->username)], password[sizeof(in->password)];
    int s, free_slot = -1, rv;

    // Every field NUL-terminated within its size: the EE's buffers are copied in whole.
    if (memchr(in->name, 0, sizeof(in->name)) == NULL || memchr(in->url, 0, sizeof(in->url)) == NULL ||
        memchr(in->username, 0, sizeof(in->username)) == NULL || memchr(in->password, 0, sizeof(in->password)) == NULL)
        return -SMB2_CONNECT_ERR_URL;
    strcpy(name, in->name);
    if (name[0] == '\0' || strchr(name, '/') || strchr(name, '\\'))
        return -SMB2_CONNECT_ERR_URL;
    if (smb2man_latin1(user, in->username, sizeof(user)) < 0 ||
        smb2man_latin1(password, in->password, sizeof(password)) < 0)
        return -SMB2_CONNECT_ERR_LOGON;

    LOCK();
    for (s = 0; s < SMB2MAN_SHARES; s++) {
        if (!shares[s].name[0]) {
            if (free_slot < 0)
                free_slot = s;
        } else if (!strcmp(shares[s].name, name)) {
            UNLOCK();
            return -EEXIST;
        }
    }
    if (free_slot < 0) {
        UNLOCK();
        return -SMB2_CONNECT_ERR_FULL;
    }
    sh = &shares[free_slot];
    memset(sh, 0, sizeof(*sh));
    rv = smb2man_parse_url(sh, in->url) ? -SMB2_CONNECT_ERR_URL : 0;
    if (rv == 0) {
        strcpy(sh->user, user);
        strcpy(sh->password, password);
        strcpy(sh->name, name);
        rv = smb2man_connect(free_slot);
    }
    if (rv < 0)
        memset(sh, 0, sizeof(*sh)); // a share that never connected is not kept, password included
    UNLOCK();
    memset(password, 0, sizeof(password));
    return rv;
}

static int smb2man_devctl(iop_file_t *f, const char *name, int cmd, void *arg, unsigned int arglen, void *buf,
                          unsigned int buflen)
{
    int s;

    (void)f;
    (void)name;
    (void)buf;
    (void)buflen;

    switch (cmd) {
        case SMB2_DEVCTL_CONNECT:
            if (arg == NULL || arglen < sizeof(smb2Connect_in_t))
                return -EINVAL;
            return smb2man_connect_devctl((const smb2Connect_in_t *)arg);

        case SMB2_DEVCTL_DISCONNECT_ALL:
            LOCK();
            for (s = 0; s < SMB2MAN_SHARES; s++) {
                smb2man_teardown(s);
                memset(&shares[s], 0, sizeof(shares[s]));
            }
            UNLOCK();
            return 0;
    }

    return -EINVAL;
}

static int smb2man_nulldev(void)
{
    return -EIO;
}

static s64 smb2man_nulldev64(void)
{
    return -EIO;
}

static iop_device_ops_t smb2man_ops = {
    &smb2man_init,
    &smb2man_deinit,
    (void *)&smb2man_nulldev, // format
    &smb2man_open,
    &smb2man_close,
    &smb2man_read,
    &smb2man_write,
    &smb2man_lseek,
    (void *)&smb2man_nulldev, // ioctl
    &smb2man_remove,
    &smb2man_mkdir,
    &smb2man_rmdir,
    &smb2man_dopen,
    &smb2man_dclose,
    &smb2man_dread,
    &smb2man_getstat,
    (void *)&smb2man_nulldev, // chstat (the EE skips it for smb:)
    &smb2man_rename,
    (void *)&smb2man_nulldev, // chdir
    (void *)&smb2man_nulldev, // sync
    (void *)&smb2man_nulldev, // mount
    (void *)&smb2man_nulldev, // umount
    &smb2man_lseek64,
    &smb2man_devctl,
    (void *)&smb2man_nulldev,   // symlink
    (void *)&smb2man_nulldev,   // readlink
    (void *)&smb2man_nulldev64, // ioctl2
};

static iop_device_t smb2man_dev = {
    "smb",
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "SMB2",
    &smb2man_ops,
};

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    DelDrv(smb2man_dev.name);
    if (AddDrv(&smb2man_dev) != 0)
        return MODULE_NO_RESIDENT_END;

    return MODULE_RESIDENT_END;
}
