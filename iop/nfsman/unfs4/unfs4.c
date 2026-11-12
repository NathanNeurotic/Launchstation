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
/* PS2IOP does not use read/write to access the socket but lwip calls.
   it will need this:

#define write(a,b,c) lwip_send(a,b,c,0)
#define read(a,b,c) lwip_recv(a,b,c,0)

*/
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#if defined(_IOP)
#include "ps2iop-compat.h"
#else

#if !defined(Z80)
#include <arpa/inet.h>
#include <sys/socket.h>
#else
#include <net/hton.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#endif
#include <string.h>
#include <sys/types.h>

#include "unfs4.h"
#ifdef UNFS4_FEATURE_TINY_TCP
#include "ip.h"
#include "tcp.h"
#endif

/* RFC 7530 operations */
#define OP_CLOSE                 4
#define OP_CREATE                6
#define OP_GETATTR               9
#define OP_GETFH                10
#define OP_LOOKUP               15
#define OP_OPEN                 18
#define OP_OPEN_CONFIRM         20
#define OP_PUTFH                22
#define OP_PUTROOTFH            24
#define OP_READ                 25
#define OP_READDIR              26
#define OP_REMOVE               28
#define OP_RENAME               29
#define OP_SAVEFH               32
#define OP_SETATTR              34
#define OP_SETCLIENTID          35
#define OP_SETCLIENTID_CONFIRM  36
#define OP_WRITE                38

/*
 * Where things are in a reply, counting the record mark at 0. The verifier
 * is AUTH_NONE, of length 0, and the tag is empty, as in our requests.
 *  0 record mark
 *  4 xid, message type, reply status, verifier flavor and length,
 *    accept status
 * 28 COMPOUND status, tag, number of results
 * 40 the results, operation and status, then its result
 */
#define R_STATUS        28
#define R_RES           40

/*
 * The results of PUTFH, PUTROOTFH, LOOKUP, SAVEFH and SETCLIENTID_CONFIRM
 * are just the operation and the status, 8 bytes, so the result of an
 * operation that only follows those is at R_RES + 8 * (number of the
 * operations before it) + 8.
 */
#define RES(u, n)       (&(u)->buf[R_RES + 8 * (n)])


#ifdef UNFS4_FEATURE_TINY_TCP
tcp_context_t *unfs4_tcp;

static int recv_pos = 0;

#ifdef UNFS4_FEATURE_REASSEMBLY
/* Big enough for the reply to a 2 kbyte READ: record mark and headers */
#ifndef UNFS4_REASSEMBLY_SIZE
#define UNFS4_REASSEMBLY_SIZE (64 + 2048)
#endif
/* Replies that take more than one TCP segment are collected here */
static uint8_t reasm[UNFS4_REASSEMBLY_SIZE];
#endif /* UNFS4_FEATURE_REASSEMBLY */

static int recv_reply_from_socket(uint32_t wanted_xid)
{
        int rc;
        uint8_t *ptr;
        uint32_t xid, rm;
#ifdef UNFS4_FEATURE_REASSEMBLY
        int len = 0;
#endif /* UNFS4_FEATURE_REASSEMBLY */

        recv_pos = 0;
 again:
        /* Open the window for the reply, tcp_recv() clamps it to the buffer */
        rc = tcp_recv(unfs4_tcp, unfs4_tcp->ip.pkt_size);
        if (rc < 0) {
                return -1;
        }
        if (rc == 0) {
                goto again;
        }
        ptr = tcp_rx_buffer(unfs4_tcp);
#ifdef UNFS4_FEATURE_REASSEMBLY
        if (len + rc > UNFS4_REASSEMBLY_SIZE) {
                return -1;
        }
        memcpy(&reasm[len], ptr, rc);
        len += rc;
        /* Wait for the rest of the record: the mark is its size, without
         * the mark, and the last-fragment bit
         */
        if (len < 4) {
                goto again;
        }
        memcpy(&rm, reasm, 4);
        rm = ntohl(rm) & 0x7fffffff;
        if (rm > UNFS4_REASSEMBLY_SIZE - 4) {
                return -1;
        }
        if (len < 4 + (int)rm) {
                goto again;
        }
        ptr = reasm;
        len = 0;
#else
        /* The whole record must be in this segment */
        memcpy(&rm, ptr, 4);
        if (rc < 8 || (ntohl(rm) & 0x7fffffff) > (uint32_t)rc - 4) {
                return -1;
        }
#endif /* UNFS4_FEATURE_REASSEMBLY */
        memcpy(&xid, &ptr[4], 4);
        if (ntohl(xid) != wanted_xid) {
                goto again;
        }

        return 0;
}


static int send_pos = 0;

static void send_rqst_to_socket(void)
{
#ifdef UNFS4_FEATURE_REASSEMBLY
        /* Open the window for the whole reply, which then comes at once */
        tcp_send(unfs4_tcp, send_pos, NULL, UNFS4_REASSEMBLY_SIZE);
#else
        tcp_send(unfs4_tcp, send_pos, NULL, 0);
#endif
        send_pos = 0;
}
#endif

#ifndef UNFS4_FEATURE_TINY_TCP
/* Closes the socket so that all further reads and writes fail at once */
static int socket_failed(struct unfs4_context *unfs4)
{
        if (unfs4->fd >= 0) {
                close(unfs4->fd);
                unfs4->fd = -1;
        }
        return -1;
}
#endif

static int write_to_socket(struct unfs4_context *unfs4, const uint8_t *buf, int len)
{
#ifdef UNFS4_FEATURE_TINY_TCP
        uint8_t *ptr;
        ptr = tcp_tx_buffer(unfs4_tcp);

        memcpy(&ptr[send_pos], buf, len);
        send_pos += len;
#else
        int count;

        while (len) {
                count = write(unfs4->fd, buf, len);
                if (count <= 0) {
                        return socket_failed(unfs4);
                }
                len -= count;
                buf += count;
        }
#endif
        return 0;
}

static int read_from_socket(struct unfs4_context *unfs4, uint8_t *buf, int len)
{
#ifdef UNFS4_FEATURE_TINY_TCP
        uint8_t *ptr;
#ifdef UNFS4_FEATURE_REASSEMBLY
        ptr = reasm;
#else
        ptr = tcp_rx_buffer(unfs4_tcp);
#endif

        memcpy(buf, &ptr[recv_pos], len);
        recv_pos += len;
#else
        int count;

        while (len) {
                count = read(unfs4->fd, buf, len);
                if (count <= 0) {
                        return socket_failed(unfs4);
                }
                len -= count;
                buf += count;
        }
#endif
        return 0;
}

/* Reads up to len bytes of the rest of the reply, *left bytes, to buf.
 * Returns how many it read.
 */
static int read_reply(struct unfs4_context *unfs4, uint8_t *buf, int len, uint32_t *left)
{
        if ((uint32_t)len > *left) {
                len = *left;
        }
        read_from_socket(unfs4, buf, len);
        *left -= len;
        return len;
}

/*
 * XDR is big-endian and everything is a multiple of 4 bytes
 */
static uint8_t *put32(uint8_t *p, uint32_t v)
{
        p[0] = v >> 24;
        p[1] = v >> 16;
        p[2] = v >> 8;
        p[3] = v;
        return p + 4;
}

static uint32_t get32(const uint8_t *p)
{
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | (p[2] << 8) | p[3];
}

static uint64_t get64(const uint8_t *p)
{
        return ((uint64_t)get32(p) << 32) | get32(p + 4);
}

/* Length, data and zero padding. buf starts out zeroed. */
static uint8_t *put_opaque(uint8_t *p, const void *data, int len)
{
        p = put32(p, len);
        memcpy(p, data, len);
        return p + ((len + 3) & ~3);
}

#if defined(UNFS4_FEATURE_CREATE) || defined(UNFS4_FEATURE_MKDIR) || \
    defined(UNFS4_FEATURE_UNLINK) || defined(UNFS4_FEATURE_RENAME)
static uint8_t *put_name(uint8_t *p, const char *name)
{
        return put_opaque(p, name, strlen(name));
}
#endif

static uint8_t *put_op(struct unfs4_context *unfs4, uint8_t *p, uint32_t op)
{
        unfs4->nops++;
        return put32(p, op);
}

/* Clears buf, writes the RPC and COMPOUND headers to it and returns where
 * the first operation goes.
 */
static uint8_t *unfs4_cmd_hdr(struct unfs4_context *unfs4)
{
        uint8_t *b = unfs4->buf;

        memset(b, 0, sizeof(unfs4->buf));
        unfs4->nops = 0;
        /*
         * RPC call: the record mark and xid are set when it is sent,
         * message type CALL is 0
         */
        b[15] = 2;                      /* RPC version 2 */
        put32(&b[16], 100003);          /* NFS */
        b[23] = 4;                      /* version 4 */
        b[27] = 1;                      /* COMPOUND */
        /* AUTH_SYS: stamp 0, no machine name, uid, gid, no other gids */
        b[31] = 1;
        b[35] = 20;
        put32(&b[44], unfs4->uid);
        put32(&b[48], unfs4->gid);
        /* verifier AUTH_NONE, then COMPOUND: no tag, minor version 0, the
         * number of operations is set when it is sent
         */
        return &b[76];
}

/* PUTFH, or PUTROOTFH before unfs4_mount() */
static uint8_t *put_fh(struct unfs4_context *unfs4, uint8_t *p, const uint8_t *fh)
{
        int len = 4 + get32(fh);

        if (len == 4) {
                return put_op(unfs4, p, OP_PUTROOTFH);
        }
        p = put_op(unfs4, p, OP_PUTFH);
        memcpy(p, fh, len);
        return p + len;
}

/* Bytes path takes in a request, a LOOKUP or a name for each component */
static int path_size(const char *path)
{
        int n = 0, len;

        while (*path) {
                while (*path == '/') {
                        path++;
                }
                for (len = 0; path[len] && path[len] != '/'; len++) {
                }
                if (len) {
                        n += 8 + ((len + 3) & ~3);
                }
                path += len;
        }
        return n;
}

/* Room for the paths: the headers, two PUTFH and the largest tail, OPEN */
#define PATH_ROOM (UNFS4_SIZE - 76 - 2 * (4 + 4 + UNFS4_MAX_FH) - 84)

#define NFS4ERR_NAMETOOLONG     63
#define NFS4ERR_SERVERFAULT     10006

static int too_long(struct unfs4_context *unfs4, int size)
{
        if (size > PATH_ROOM) {
                unfs4->status = NFS4ERR_NAMETOOLONG;
                return 1;
        }
        return 0;
}

/* PUTFH of the export, then a LOOKUP for each component of path. With last,
 * not for the last one: it is returned in *last.
 */
static uint8_t *put_path(struct unfs4_context *unfs4, uint8_t *p, const char *path, const char **last)
{
        const char *e;

        p = put_fh(unfs4, p, unfs4->root);
        for (;;) {
                while (*path == '/') {
                        path++;
                }
                for (e = path; *e && *e != '/'; e++) {
                }
                if (last && !*e) {
                        *last = path;
                        return p;
                }
                if (e == path) {
                        return p;
                }
                p = put_op(unfs4, p, OP_LOOKUP);
                p = put_opaque(p, path, e - path);
                path = e;
        }
}

/* The attributes we ask for: TYPE, SIZE and TIME_MODIFY */
static uint8_t *put_attr_request(uint8_t *p)
{
        p = put32(p, 2);
        p = put32(p, 0x00000012);
        return put32(p, 0x00200000);
}

/* Takes apart the attributes from put_attr_request(), which end before
 * end, returns where they end, or NULL if they do not fit
 */
static uint8_t *get_attrs(uint8_t *p, const uint8_t *end, struct unfs4_attr *a)
{
        uint32_t n;

        /* The bitmap, its words, the length of the attributes and them */
        if (end - p < 8 || (n = get32(p)) > (uint32_t)(end - p - 8) / 4) {
                return NULL;
        }
        p += 4 + 4 * n;
        n = get32(p);
        if (n < 20 || n > (uint32_t)(end - p - 4)) {
                return NULL;
        }
        a->type = get32(p + 4);
        a->size = get64(p + 8);
        a->mtime = get64(p + 16);
        return p + 4 + n;
}

#if defined(UNFS4_FEATURE_MKDIR) || defined(UNFS4_FEATURE_CREATE)
/* Attributes for a new file or directory: just the MODE */
static uint8_t *put_mode(uint8_t *p, uint32_t mode)
{
        p[3] = 2;
        p[11] = 2;
        p[15] = 4;
        return put32(p + 16, mode);
}
#endif

/* Copies the filehandle at fh in buf, from GETFH, to dst */
static int get_fh(const uint8_t *fh, uint8_t *dst)
{
        uint32_t len = get32(fh);

        if (len > UNFS4_MAX_FH) {
                return -1;
        }
        memcpy(dst, fh, 4 + ((len + 3) & ~3));
        return 0;
}

/* Sends the request in buf, which ends at p, and outdata, and reads the
 * reply. Without indata all of it goes to buf. With indata 64 bytes of it
 * go to buf, that is up to the data of a READ or the entries of a READDIR,
 * and then up to indatacount bytes to indata.
 * Returns 0 if the RPC and all operations succeeded.
 */
static uint32_t unfs4_rpc(struct unfs4_context *unfs4, uint8_t *p,
                          const uint8_t *outdata, int outdatacount,
                          uint8_t *indata, int indatacount)
{
        static const uint8_t zero[4];
        uint8_t *b = unfs4->buf;
        int len = p - b, pad = -outdatacount & 3;
        uint32_t left;

        /* Record mark: the last and only fragment */
        put32(b, 0x80000000 | (len - 4 + outdatacount + pad));
        put32(&b[4], ++unfs4->xid);
        put32(&b[72], unfs4->nops);

        write_to_socket(unfs4, b, len);
        write_to_socket(unfs4, outdata, outdatacount);
        write_to_socket(unfs4, zero, pad);

#ifdef UNFS4_FEATURE_TINY_TCP
        send_rqst_to_socket();
        if (recv_reply_from_socket(unfs4->xid)) {
                return -1;
        }
#endif

        read_from_socket(unfs4, b, 4);
        left = get32(b);
        /*
         * Our replies are one fragment, the last, and never bigger than buf
         * and indata. A server that sends something else is broken, and we
         * would only drain the record, maybe for minutes.
         */
        if (!(left & 0x80000000) ||
            (left &= 0x7fffffff) > sizeof(unfs4->buf) + indatacount) {
#ifndef UNFS4_FEATURE_TINY_TCP
                socket_failed(unfs4);
#endif
                unfs4->status = UNFS4_ERR_CONN;
                return -1;
        }
        /* Leave at least 64 bytes at the end of buf to read the rest to */
        len = read_reply(unfs4, &b[4], indata ? 60 : sizeof(unfs4->buf) - 4 - 64, &left);
        unfs4->len = 4 + len;
        unfs4->inlen = read_reply(unfs4, indata, indatacount, &left);
        while (left) {
                read_reply(unfs4, &b[4 + len], sizeof(unfs4->buf) - 4 - len, &left);
        }

#ifndef UNFS4_FEATURE_TINY_TCP
        if (unfs4->fd < 0) {
                unfs4->status = UNFS4_ERR_CONN;
                return -1;
        }
#endif
        /* Up to the COMPOUND status, or the request is still there */
        if (unfs4->len < R_STATUS + 4) {
                unfs4->status = NFS4ERR_SERVERFAULT;
                return -1;
        }
        /* reply status, accept status and the COMPOUND status */
        unfs4->status = (get32(&b[12]) | get32(&b[24])) ? NFS4ERR_SERVERFAULT : get32(&b[R_STATUS]);
        return get32(&b[12]) | get32(&b[24]) | get32(&b[R_STATUS]);
}

/* Looks up path and copies its filehandle to fh */
static int unfs4_lookup(struct unfs4_context *unfs4, const char *path, uint8_t *fh)
{
        uint8_t *p;

        if (too_long(unfs4, path_size(path))) {
                return -1;
        }
        p = put_path(unfs4, unfs4_cmd_hdr(unfs4), path, NULL);

        p = put_op(unfs4, p, OP_GETFH);
        if (unfs4_rpc(unfs4, p, NULL, 0, NULL, 0)) {
                return -1;
        }
        return get_fh(RES(unfs4, unfs4->nops), fh);
}

int unfs4_mount(struct unfs4_context *unfs4, const char *path)
{
        return unfs4_lookup(unfs4, path, unfs4->root);
}

#ifdef UNFS4_FEATURE_CREATE
/*
 * Creating a regular file takes an OPEN, which takes a client id. We get a
 * new one each time and close the file again at once, so that there is no
 * state on the server for a lease to keep alive.
 */
static uint8_t *unfs4_create(struct unfs4_context *unfs4, const char *name, uint8_t *fh)
{
        uint8_t *p, clientid[16], stateid[16];
        char id[14] = "unfs4-";
        const char *last;
        uint32_t x;
        int n, confirm;

        if (too_long(unfs4, path_size(name))) {
                return NULL;
        }
        /*
         * The server keeps a client's record for a lease time, and refuses
         * its name to another client, one with other credentials, until then
         */
        if (unfs4->id == 0) {
                unfs4->id = unfs4->xid | 1;
        }
        for (x = unfs4->id, n = 13; n > 5; n--, x >>= 4) {
                id[n] = "0123456789abcdef"[x & 15];
        }
        /*
         * SETCLIENTID: verifier 0, our name, a callback the server cannot
         * reach so that it does not hand out delegations
         */
        p = put_op(unfs4, unfs4_cmd_hdr(unfs4), OP_SETCLIENTID);
        p = put_opaque(p + 8, id, sizeof(id));
        p = put32(p, 0x40000000);
        p = put_name(p, "tcp");
        p = put_name(p, "0.0.0.0.0.0");
        if (unfs4_rpc(unfs4, p + 4, NULL, 0, NULL, 0)) {
                return NULL;
        }
        /* client id and confirm verifier */
        memcpy(clientid, RES(unfs4, 1), 16);

        /*
         * SETCLIENTID_CONFIRM, then OPEN the file in its directory: seqid 0,
         * access READ and WRITE, deny none, owner the client id and the xid,
         * CREATE UNCHECKED with mode 0644, CLAIM_NULL with the name
         */
        p = put_op(unfs4, unfs4_cmd_hdr(unfs4), OP_SETCLIENTID_CONFIRM);
        memcpy(p, clientid, 16);
        p = put_path(unfs4, p + 16, name, &last);
        p = put_op(unfs4, p, OP_OPEN);
        p[7] = 3;
        memcpy(p + 12, clientid, 8);
        p = put32(p + 20, 4);
        p = put32(p, unfs4->xid);
        p[3] = 1;
        p = put_mode(p + 8, 0644);
        p = put_name(p + 4, last);
        if (unfs4_rpc(unfs4, p, NULL, 0, NULL, 0)) {
                return NULL;
        }
        p = RES(unfs4, unfs4->nops);
        memcpy(stateid, p, 16);
        /* rflags OPEN4_RESULT_CONFIRM: a new owner must confirm the open */
        confirm = p[39] & 2;

        /*
         * Look the file up for its filehandle, then OPEN_CONFIRM and CLOSE.
         * Each bumps the seqid of the owner and OPEN_CONFIRM the seqid of
         * the stateid.
         */
        p = put_path(unfs4, unfs4_cmd_hdr(unfs4), name, NULL);
        p = put_op(unfs4, p, OP_GETFH);
        n = unfs4->nops;
        if (confirm) {
                p = put_op(unfs4, p, OP_OPEN_CONFIRM);
                memcpy(p, stateid, 16);
                p = put32(p + 16, 1);
                put32(stateid, get32(stateid) + 1);
        }
        p = put_op(unfs4, p, OP_CLOSE);
        p = put32(p, 1 + !!confirm);
        memcpy(p, stateid, 16);
        if (unfs4_rpc(unfs4, p + 16, NULL, 0, NULL, 0) ||
            get_fh(RES(unfs4, n), fh)) {
                return NULL;
        }
        return fh;
}
#endif /* UNFS4_FEATURE_CREATE */

uint8_t *unfs4_open(struct unfs4_context *unfs4, const char *name, int mode)
{
        uint8_t *fh = malloc(4 + UNFS4_MAX_FH);

        if (fh == NULL) {
                return NULL;
        }
#ifdef UNFS4_FEATURE_CREATE
        if (mode & O_CREAT) {
                if (unfs4_create(unfs4, name, fh)) {
                        return fh;
                }
        } else
#endif /* UNFS4_FEATURE_CREATE */
        if (!unfs4_lookup(unfs4, name, fh)) {
                return fh;
        }
        free(fh);
        return NULL;
}

#ifdef UNFS4_FEATURE_OPENDIR
struct unfs4_dir *unfs4_opendir(struct unfs4_context *unfs4, const char *name)
{
        struct unfs4_dir *dir = calloc(1, sizeof(struct unfs4_dir));

        if (dir && unfs4_lookup(unfs4, name, dir->fh)) {
                free(dir);
                dir = NULL;
        }
        return dir;
}

/*
 * The entries of a READDIR reply go to dir->buf. Each is: 1 (an entry
 * follows), the cookie, the name and the attributes. After the last one is
 * 0 (no entry follows) and eof.
 */
struct unfs4_dirent *unfs4_readdir(struct unfs4_context *unfs4, struct unfs4_dir *dir)
{
        uint8_t *p, *e, *end;
        uint32_t len;

 again:
        if (!dir->have) {
                if (dir->eof) {
                        return NULL;
                }
                /* READDIR: cookie and verifier, dircount, maxcount, attrs */
                p = put_fh(unfs4, unfs4_cmd_hdr(unfs4), dir->fh);
                p = put_op(unfs4, p, OP_READDIR);
                memcpy(p, dir->cookie, 16);
                p = put32(p + 16, UNFS4_DIR_SIZE);
                /* maxcount counts the verifier too */
                p = put32(p, UNFS4_DIR_SIZE + 8);
                p = put_attr_request(p);
                if (unfs4_rpc(unfs4, p, NULL, 0, dir->buf, UNFS4_DIR_SIZE)) {
                        return NULL;
                }
                memcpy(&dir->cookie[8], RES(unfs4, 2), 8);
                dir->pos = 0;
                dir->len = unfs4->inlen;
                dir->have = 1;
        }
        p = &dir->buf[dir->pos];
        end = &dir->buf[dir->len];
        /* 1 and the cookie, or 0 and eof */
        if (end - p < 8) {
                goto bad;
        }
        if (!get32(p)) {
                dir->eof = p[7];
                dir->have = 0;
                goto again;
        }
        if (end - p < 16 || (len = get32(p + 12)) > (uint32_t)(end - p - 16) ||
            !(e = get_attrs(p + 16 + ((len + 3) & ~3), end, &dir->de.attr))) {
                goto bad;
        }
        memcpy(dir->cookie, p + 4, 8);
        dir->de.name = (char *)p + 16;
        /* over the padding or the attributes, which we have read */
        dir->de.name[len] = 0;
        dir->pos = e - dir->buf;
        return &dir->de;

 bad:
        /* A reply that does not fit what it says: the server is broken */
        unfs4->status = NFS4ERR_SERVERFAULT;
        dir->have = 0;
        dir->eof = 1;
        return NULL;
}
#endif /* UNFS4_FEATURE_OPENDIR */

int32_t unfs4_prw(struct unfs4_context *unfs4, int op, uint8_t *fh, uint8_t *rbuf, uint8_t *wbuf, int count, uint64_t offset)
{
        uint8_t *p = put_fh(unfs4, unfs4_cmd_hdr(unfs4), fh);
        uint32_t n;

        /* READ or WRITE: the anonymous stateid, all zero, then offset */
        p = put_op(unfs4, p, op) + 16;
        p = put32(p, offset >> 32);
        p = put32(p, offset);
        if (op == OP_WRITE) {
                /* FILE_SYNC4, so that we never need COMMIT */
                p[3] = 2;
                p += 4;
        }
        /* count, then the data of a WRITE */
        p = put32(p, count);
        if (unfs4_rpc(unfs4, p,
                      wbuf, (op == OP_WRITE) ? count : 0,
                      rbuf, (op == OP_READ) ? count : 0)) {
                return -1;
        }
        /* WRITE: count, READ: eof, then count, at most the data we read */
        n = get32(RES(unfs4, 2) + ((op == OP_READ) ? 4 : 0));
        if (n > (uint32_t)count || (op == OP_READ && n > (uint32_t)unfs4->inlen)) {
                unfs4->status = NFS4ERR_SERVERFAULT;
                return -1;
        }
        return n;
}

/* GETATTR after the operations up to p */
static int unfs4_getattr(struct unfs4_context *unfs4, uint8_t *p, struct unfs4_attr *a)
{
        p = put_attr_request(put_op(unfs4, p, OP_GETATTR));
        if (unfs4_rpc(unfs4, p, NULL, 0, NULL, 0)) {
                return -1;
        }
        if (!get_attrs(RES(unfs4, unfs4->nops), unfs4->buf + unfs4->len, a)) {
                unfs4->status = NFS4ERR_SERVERFAULT;
                return -1;
        }
        return 0;
}

int64_t unfs4_size(struct unfs4_context *unfs4, uint8_t *fh)
{
        struct unfs4_attr a;

        if (unfs4_getattr(unfs4, put_fh(unfs4, unfs4_cmd_hdr(unfs4), fh), &a)) {
                return -1;
        }
        return a.size;
}

#ifdef UNFS4_FEATURE_STAT
int unfs4_stat(struct unfs4_context *unfs4, const char *name, struct unfs4_attr *st)
{
        if (too_long(unfs4, path_size(name))) {
                return -1;
        }
        return unfs4_getattr(unfs4, put_path(unfs4, unfs4_cmd_hdr(unfs4), name, NULL), st);
}
#endif /* UNFS4_FEATURE_STAT */

#ifdef UNFS4_FEATURE_MKDIR
int unfs4_mkdir(struct unfs4_context *unfs4, const char *name)
{
        const char *last;
        uint8_t *p;

        if (too_long(unfs4, path_size(name))) {
                return -1;
        }
        p = put_path(unfs4, unfs4_cmd_hdr(unfs4), name, &last);

        /* CREATE: type NF4DIR, the name, mode 0755 */
        p = put_op(unfs4, p, OP_CREATE);
        p = put32(p, NF4DIR);
        p = put_mode(put_name(p, last), 0755);
        return unfs4_rpc(unfs4, p, NULL, 0, NULL, 0) ? -1 : 0;
}
#endif /* UNFS4_FEATURE_MKDIR */

#ifdef UNFS4_FEATURE_UNLINK
int unfs4_remove(struct unfs4_context *unfs4, const char *name)
{
        const char *last;
        uint8_t *p;

        if (too_long(unfs4, path_size(name))) {
                return -1;
        }
        p = put_path(unfs4, unfs4_cmd_hdr(unfs4), name, &last);

        p = put_name(put_op(unfs4, p, OP_REMOVE), last);
        return unfs4_rpc(unfs4, p, NULL, 0, NULL, 0) ? -1 : 0;
}
#endif /* UNFS4_FEATURE_UNLINK */

#ifdef UNFS4_FEATURE_RENAME
int unfs4_rename(struct unfs4_context *unfs4, const char *oldname, const char *newname)
{
        const char *oldlast, *newlast;
        uint8_t *p;

        if (too_long(unfs4, path_size(oldname) + path_size(newname))) {
                return -1;
        }
        p = put_path(unfs4, unfs4_cmd_hdr(unfs4), oldname, &oldlast);

        /* The saved filehandle is the old directory, the current the new */
        p = put_op(unfs4, p, OP_SAVEFH);
        p = put_path(unfs4, p, newname, &newlast);
        p = put_op(unfs4, p, OP_RENAME);
        p = put_name(put_name(p, oldlast), newlast);
        return unfs4_rpc(unfs4, p, NULL, 0, NULL, 0) ? -1 : 0;
}
#endif /* UNFS4_FEATURE_RENAME */

#ifdef UNFS4_FEATURE_TRUNCATE
int unfs4_truncate(struct unfs4_context *unfs4, uint8_t *fh, uint64_t size)
{
        uint8_t *p = put_fh(unfs4, unfs4_cmd_hdr(unfs4), fh);

        /* SETATTR: the anonymous stateid, then the SIZE alone */
        p = put_op(unfs4, p, OP_SETATTR) + 16;
        p = put32(p, 1);
        p = put32(p, 0x10);
        p = put32(p, 8);
        p = put32(p, size >> 32);
        p = put32(p, size);
        return unfs4_rpc(unfs4, p, NULL, 0, NULL, 0) ? -1 : 0;
}
#endif /* UNFS4_FEATURE_TRUNCATE */

struct unfs4_context *unfs4_init_context(uint32_t ip, uint32_t uid, uint32_t gid)
{
        struct unfs4_context *unfs4;
#ifndef UNFS4_FEATURE_TINY_TCP
        struct sockaddr_in sin;
        int socksize = sizeof(struct sockaddr_in);
#endif

        unfs4 = calloc(1, sizeof(struct unfs4_context));
        if (unfs4 == NULL) {
                return NULL;
        }
        unfs4->uid = uid;
        unfs4->gid = gid;

#ifndef UNFS4_FEATURE_TINY_TCP
        unfs4->fd = socket(AF_INET, SOCK_STREAM, 0);

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(2049);
        memcpy(&sin.sin_addr, &ip, 4);
#ifdef HAVE_SOCK_SIN_LEN
        sin.sin_len = socksize;
#endif
        if (connect(unfs4->fd, (struct sockaddr *)&sin, socksize) != 0) {
                close(unfs4->fd);
                free(unfs4);
                return NULL;
        }
#endif
        return unfs4;
}
