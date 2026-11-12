/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
 * Copyright (c) 2026, Ronnie Sahlberg <ronniesahlberg@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _UNFS4_H_
#define _UNFS4_H_

#include <stdint.h>

#if defined(Z80)
#define O_CREAT 0x0100
#ifndef UNFS4_FEATURE_TINY_TCP
#define UNFS4_FEATURE_TINY_TCP
#endif
#endif

/*
 * A minimal NFSv4.0 client, the sibling of usmb2. It speaks only the NFS
 * program on TCP port 2049: no portmapper, no MOUNT protocol, no NLM.
 * Credentials are AUTH_SYS with a fixed uid and gid.
 *
 * Reads and writes use the anonymous stateid so that the client does not
 * need to keep any state on the server: no SETCLIENTID, no OPEN, no lease
 * to renew. Only creating a regular file (UNFS4_FEATURE_CREATE) needs an
 * OPEN, which is closed again at once.
 *
 * Defines to enable features in unfs4:
 * UNFS4_FEATURE_WRITE : Adds unfs4_pwrite()
 * UNFS4_FEATURE_CREATE : unfs4_open() with O_CREAT creates the file if it
 *                        does not exist. Needs UNFS4_FEATURE_WRITE.
 * UNFS4_FEATURE_OPENDIR : Adds unfs4_opendir() and unfs4_readdir()
 * UNFS4_FEATURE_STAT  : Adds unfs4_stat()
 * UNFS4_FEATURE_MKDIR : Adds unfs4_mkdir()
 * UNFS4_FEATURE_UNLINK : Adds unfs4_unlink() and unfs4_rmdir()
 * UNFS4_FEATURE_RENAME : Adds unfs4_rename()
 * UNFS4_FEATURE_TRUNCATE : Adds unfs4_truncate()
 * UNFS4_FEATURE_TINY_TCP : Talks to the server through the tiny TCP/IP stack
 *                          in usmb2/z80 instead of the sockets API. Always on
 *                          for Z80.
 * UNFS4_FEATURE_REASSEMBLY : With UNFS4_FEATURE_TINY_TCP, collects replies
 *                          that take several TCP segments, up to
 *                          UNFS4_REASSEMBLY_SIZE bytes (a 2 kbyte READ).
 */
#if defined(UNFS4_FEATURE_CREATE) && !defined(UNFS4_FEATURE_WRITE)
#define UNFS4_FEATURE_WRITE
#endif

/* The largest filehandle we accept. RFC 7530 allows 128 bytes but Linux
 * uses 28 to 40. A multiple of 4.
 */
#ifndef UNFS4_MAX_FH
#define UNFS4_MAX_FH 64
#endif

/* Requests and replies, other than READ data and READDIR entries, go
 * through this buffer. A request is 76 bytes of RPC and COMPOUND headers,
 * PUTFH and the filehandle, then a LOOKUP per path component. 1024, as
 * usmb2 in Launchstation, takes a 440 byte path. A path that does not fit
 * fails the call. A rename carries both paths.
 */
#ifndef UNFS4_SIZE
#define UNFS4_SIZE 1024
#endif

/* Bytes of READDIR entries to store in the directory handle. An entry is
 * about 60 bytes and the name, so this holds one with a 440 byte name.
 */
#ifndef UNFS4_DIR_SIZE
#define UNFS4_DIR_SIZE 1024
#endif

/* unfs4_context status when the connection failed, after which every call fails */
#define UNFS4_ERR_CONN 0xffffffff

/* NFSv4 file types, struct unfs4_attr type */
#define NF4REG 1
#define NF4DIR 2

struct unfs4_context {
        int fd;
        uint32_t xid;
        uint32_t uid;
        uint32_t gid;
        int nops;
        /* Of the last call: 0, an NFS4ERR_*, or UNFS4_ERR_CONN */
        uint32_t status;
        /* Of the last call: how much of the reply went to buf and indata */
        int len;
        int inlen;
        /* Names this client to the server when it creates a file: the
         * first xid then, so two clients never share a name */
        uint32_t id;
        /* The filehandle of the export as XDR opaque: length, then data */
        uint8_t root[4 + UNFS4_MAX_FH];
        uint8_t buf[UNFS4_SIZE];
};

struct unfs4_attr {
        uint64_t size;
        int64_t mtime;          /* seconds since 1970 */
        uint32_t type;          /* NF4REG, NF4DIR, ... */
};

struct unfs4_dirent {
        char *name;             /* nul-terminated UTF-8 */
        struct unfs4_attr attr;
};

/* A directory handle. Allocated by unfs4_opendir(), free() it when done. */
struct unfs4_dir {
        uint8_t fh[4 + UNFS4_MAX_FH];
        uint8_t cookie[16];     /* where to continue, and the cookie verifier */
        uint16_t pos;           /* the next entry in buf */
        uint16_t len;           /* the end of the entries in buf */
        uint8_t have;           /* buf holds entries we have not returned */
        uint8_t eof;            /* the server has no more entries */
        struct unfs4_dirent de;
        uint8_t buf[UNFS4_DIR_SIZE];
};

#ifdef UNFS4_FEATURE_TINY_TCP
/* The TCP connection to the server, see usmb2/z80/README.tcpip. Set it
 * after tcp_connect() and before unfs4_init_context(), which then does not
 * use ip. The server must be told to accept connections from ports above
 * 1023, the "insecure" export option on Linux, unless the tiny TCP stack
 * uses a port below 1024.
 */
extern struct tcp_context *unfs4_tcp;
#endif

/* Connects to the server. ip is in network byte order. uid and gid are sent
 * as AUTH_SYS credentials with every request.
 *
 * Then set xid from a clock in microseconds. Every request takes the next
 * xid, and creating a file uses it as the open owner, which the server
 * remembers for a lease time: a session that reuses the xids of the last
 * one fails to create files with BAD_SEQID.
 */
struct unfs4_context *unfs4_init_context(uint32_t ip, uint32_t uid, uint32_t gid);

/* Looks up the export, e.g. "/home/nfsshare". All other paths are relative
 * to it. Components are separated by '/'.
 */
int unfs4_mount(struct unfs4_context *unfs4, const char *path);

/* Opens a file, and with UNFS4_FEATURE_CREATE and O_CREAT in mode creates it
 * if it does not exist. Returns the filehandle, or NULL. There is nothing
 * to close on the server, free() it when done.
 */
uint8_t *unfs4_open(struct unfs4_context *unfs4, const char *name, int mode);
#define unfs4_close(u, fh) free(fh)

/* Returns the number of bytes read or written, 0 at end of file, or -1 */
int32_t unfs4_prw(struct unfs4_context *unfs4, int op, uint8_t *fh, uint8_t *rbuf, uint8_t *wbuf, int count, uint64_t offset);
#define unfs4_pread(u, fh, buf, count, offset) unfs4_prw(u, 25, fh, buf, NULL, count, offset)
#ifdef UNFS4_FEATURE_WRITE
#define unfs4_pwrite(u, fh, buf, count, offset) unfs4_prw(u, 38, fh, NULL, buf, count, offset)
#endif

/* Size in bytes, or -1. 64 bits: a DVD image can be over 4 GB. */
int64_t unfs4_size(struct unfs4_context *unfs4, uint8_t *fh);

#ifdef UNFS4_FEATURE_OPENDIR
struct unfs4_dir *unfs4_opendir(struct unfs4_context *unfs4, const char *name);
/* Returns the next entry, or NULL at the end. "." and ".." are not
 * returned. The entry is valid until the next call.
 */
struct unfs4_dirent *unfs4_readdir(struct unfs4_context *unfs4, struct unfs4_dir *dir);
#define unfs4_closedir(u, dir) free(dir)
#endif /* UNFS4_FEATURE_OPENDIR */

#ifdef UNFS4_FEATURE_STAT
/* Works for files and directories */
int unfs4_stat(struct unfs4_context *unfs4, const char *name, struct unfs4_attr *st);
#endif /* UNFS4_FEATURE_STAT */

#ifdef UNFS4_FEATURE_MKDIR
/* Creates a directory, mode 0755. Fails if it already exists. */
int unfs4_mkdir(struct unfs4_context *unfs4, const char *name);
#endif /* UNFS4_FEATURE_MKDIR */

#ifdef UNFS4_FEATURE_UNLINK
/* Removes a file or an empty directory */
int unfs4_remove(struct unfs4_context *unfs4, const char *name);
#define unfs4_unlink(u, n) unfs4_remove(u, n)
#define unfs4_rmdir(u, n) unfs4_remove(u, n)
#endif /* UNFS4_FEATURE_UNLINK */

#ifdef UNFS4_FEATURE_RENAME
/* Renames a file or directory. newname is a path from the export. Unlike
 * usmb2_rename() an existing newname is replaced, as rename(2) does.
 */
int unfs4_rename(struct unfs4_context *unfs4, const char *oldname, const char *newname);
#endif /* UNFS4_FEATURE_RENAME */

#ifdef UNFS4_FEATURE_TRUNCATE
/* Sets the size of an open file, e.g. 0 to write it anew */
int unfs4_truncate(struct unfs4_context *unfs4, uint8_t *fh, uint64_t size);
#endif /* UNFS4_FEATURE_TRUNCATE */

#endif /* !_UNFS4_H_ */
