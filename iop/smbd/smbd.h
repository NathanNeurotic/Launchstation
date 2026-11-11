/* smbd: the file interface smbd_fs.c answers SMB requests with.
 * smbd.c implements it on the IOP (iomanX and MCMAN); tests/smbd_live/host.c with POSIX. */
#ifndef SMBD_H
#define SMBD_H

#include <stdint.h>
#include <string.h>
#include <errno.h>
#ifdef _IOP
#include "compat.h"
#else
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#endif
#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-raw.h"

#ifndef ENOTSUP
#define ENOTSUP 134
#endif

#define SMBD_SHARES 2      /* mc0, mc1 */
#define SMBD_PATH_MAX 128  /* "mc0:/" + folder + "/" + file, with room to spare */
#define SMBD_NAME_MAX 64   /* card names are at most 31 bytes */
#define SMBD_LEAF_MAX 31   /* longest name a card entry holds (sceMcTblGetDir.EntryName) */
#define SMBD_MAX_IO 65536  /* largest READ/WRITE/transact the server offers */
#define SMBD_MAX_CLIENTS 2

struct smbd_stat {
	int dir;
	uint64_t size;
	uint64_t mtime, ctime; /* Unix seconds */
};

/* The platform (smbd.c on the IOP). Every call returns a negative errno on failure. */
int smbd_share_root(int share, char *out, int max); /* writes "mc0:", returns its length */
int smbd_stat(const char *path, struct smbd_stat *st);
int smbd_open(const char *path, int write, int create, int trunc);
int smbd_pread(int fd, void *buf, int len, uint64_t off);
int smbd_pwrite(int fd, const void *buf, int len, uint64_t off);
int smbd_close(int fd);
int smbd_opendir(const char *path);
int smbd_readdir(int fd, char *name, int max, struct smbd_stat *st); /* 1 entry, 0 end */
int smbd_closedir(int fd);
int smbd_mkdir(const char *path);
int smbd_remove(const char *path);
int smbd_rmdir(const char *path);
int smbd_rename(const char *from, const char *to); /* same folder; smbd_fs.c checks first */
int smbd_space(int share, uint64_t *total, uint64_t *avail);

extern const char *smbd_user, *smbd_password;
extern int smbd_clients;
extern struct smb2_server_request_handlers smbd_handlers;
void smbd_release_all(void);
int smbd_serve(uint16_t port);
void smbd_stop(void);
int smbd_listening(void);

#endif
