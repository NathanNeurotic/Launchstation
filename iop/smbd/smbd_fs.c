/* smbd: the SMB requests, answered on the console's memory cards.
 *
 * libsmb2 (Ronnie Sahlberg, LGPL-2.1) does the protocol: negotiate, NTLM, signing and the
 * select loop. This file is Launchstation's own: it maps each request onto the small file
 * interface in smbd.h, which smbd.c implements with iomanX/MCMAN on the IOP and
 * tests/smbd_live/host.c with POSIX on a PC. Shares: \\<ip>\mc0 and \\<ip>\mc1.
 *
 * A handler with a real error to report queues its own error reply and returns 1:
 * libsmb2 itself only knows STATUS_NOT_IMPLEMENTED. */

#include "smbd.h"
#include "libsmb2-private.h"

#ifndef SMB2_FILE_SUPERSEDED
#define SMB2_FILE_SUPERSEDED 0
#define SMB2_FILE_OPENED 1
#define SMB2_FILE_CREATED 2
#define SMB2_FILE_OVERWRITTEN 3
#endif

#define MAX_HANDLES 32
#define PAD8(x) (((x) + 7) & ~7u)
/* Encoded size of one entry: the largest fixed part (FileIdBothDirectoryInformation, 104 bytes)
 * plus the UTF-16 name, which takes at most two bytes for each UTF-8 byte. */
#define DIR_ENTRY_ROOM(namelen) PAD8(104 + 2 * (namelen))
#define DIR_BATCH 32

struct handle {
	struct smb2_context *owner;
	uint16_t gen, dir_pos; /* dir_pos: entries a listing has read so far */
	uint8_t used, dir, share, write, delete_on_close, dir_done, pending;
	char path[SMBD_PATH_MAX]; /* full device path, e.g. mc0:/BESLES-12345/icon.sys */
	/* A directory entry read but not sent: it did not fit the last reply. */
	char pending_name[SMBD_NAME_MAX];
	struct smbd_stat pending_st;
};

static struct handle handles[MAX_HANDLES];
static uint16_t next_gen = 1;
static uint32_t tree_counter;
static int last_create = -1; /* for a compound request's "the previous create" file id */
static const char *const share_names[SMBD_SHARES] = {"mc0", "mc1"};

/* One answer at a time: the reply is encoded before the handler's next call. */
static union {
	struct smb2_file_basic_info basic;
	struct smb2_file_standard_info standard;
	struct smb2_file_all_info all;
	struct smb2_file_network_open_info open;
	struct smb2_file_attribute_tag_info tag;
	struct smb2_file_fs_size_info size;
	struct smb2_file_fs_full_size_info full;
	struct smb2_file_fs_attribute_info attr;
	struct smb2_file_fs_device_info device;
	struct smb2_file_fs_volume_info volume;
} info;

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int ieq(const char *a, const char *b)
{
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
		a++, b++;
	return *a == *b || lower((unsigned char)*a) == lower((unsigned char)*b);
}

static int reply(struct smb2_context *smb2, uint8_t command, uint32_t status)
{
	struct smb2_error_reply err;
	struct smb2_pdu *pdu;

	memset(&err, 0, sizeof(err));
	pdu = smb2_cmd_error_reply_async(smb2, &err, command, (int)status, NULL, NULL);
	if (pdu) {
		smb2_set_pdu_message_id(smb2, pdu, smb2_get_last_request_message_id(smb2));
		smb2_queue_pdu(smb2, pdu);
	}
	return 1;
}

static uint32_t status_of(int err)
{
	switch (-err) {
	case ENOENT: return SMB2_STATUS_OBJECT_NAME_NOT_FOUND;
	case ENOTDIR: return SMB2_STATUS_OBJECT_PATH_NOT_FOUND;
	case EACCES:
	case EPERM: return SMB2_STATUS_ACCESS_DENIED;
	case EEXIST: return SMB2_STATUS_OBJECT_NAME_COLLISION;
	case ENOTEMPTY: return SMB2_STATUS_DIRECTORY_NOT_EMPTY;
	case EISDIR: return SMB2_STATUS_FILE_IS_A_DIRECTORY;
	case ENOSPC: return SMB2_STATUS_DISK_FULL;
	case ENAMETOOLONG: return SMB2_STATUS_NAME_TOO_LONG;
	case EXDEV: return SMB2_STATUS_NOT_SAME_DEVICE;
	case EMFILE: return SMB2_STATUS_TOO_MANY_OPENED_FILES;
	case EINVAL: return SMB2_STATUS_OBJECT_NAME_INVALID;
	case ENODEV: return SMB2_STATUS_NO_SUCH_DEVICE;
	case ENOTSUP: return SMB2_STATUS_NOT_SUPPORTED;
	default: return SMB2_STATUS_IO_DEVICE_ERROR;
	}
}

/* --- shares and paths ----------------------------------------------------------------- */

static int share_of_tree(struct smb2_context *smb2)
{
	uint32_t id = smb2->hdr.sync.tree_id;
	if ((id >> 28) != 0x5 || (id & 0xf) >= SMBD_SHARES)
		return -1;
	return id & 0xf;
}

/* "dir\name" relative to the share root -> "mc0:/dir/name". Refuses "..", ":" and over-long
 * paths, so a request can never leave its share. */
static int make_path(int share, const char *rel, char *out)
{
	int n = smbd_share_root(share, out, SMBD_PATH_MAX);
	const char *p = rel ? rel : "";

	while (*p == '\\' || *p == '/')
		p++;
	while (*p) {
		const char *s = p;
		int len;
		while (*p && *p != '\\' && *p != '/')
			p++;
		len = (int)(p - s);
		while (*p == '\\' || *p == '/')
			p++;
		if (len == 0 || (len == 1 && s[0] == '.'))
			continue;
		if ((len == 2 && s[0] == '.' && s[1] == '.') || memchr(s, ':', len))
			return -EINVAL;
		if (n + 1 + len >= SMBD_PATH_MAX)
			return -ENAMETOOLONG;
		out[n++] = '/';
		memcpy(out + n, s, len);
		n += len;
	}
	if (out[n - 1] == ':') /* the share root */
		out[n++] = '/';
	out[n] = 0;
	return 0;
}

static int is_root(const char *path)
{
	size_t n = strlen(path);
	return n && path[n - 1] == '/';
}

/* --- handles ---------------------------------------------------------------------------- */

static int lookup(struct smb2_context *smb2, const smb2_file_id id)
{
	int i, all_ones = 1;

	for (i = 0; i < SMB2_FD_SIZE; i++)
		if (id[i] != 0xff)
			all_ones = 0;
	if (all_ones) /* compound request: the create earlier in this chain */
		i = last_create;
	else
		i = id[0] | (id[1] << 8);
	if (i < 0 || i >= MAX_HANDLES || !handles[i].used || handles[i].owner != smb2)
		return -1;
	if (!all_ones && handles[i].gen != (uint16_t)(id[2] | (id[3] << 8)))
		return -1;
	return i;
}

/* 1 if the folder holds anything, 0 if it is empty, or a negative errno. */
static int dir_used(const char *path)
{
	char name[SMBD_NAME_MAX];
	struct smbd_stat st;
	int fd = smbd_opendir(path), r;

	if (fd < 0)
		return fd;
	while ((r = smbd_readdir(fd, name, sizeof(name), &st)) > 0 && (!strcmp(name, ".") || !strcmp(name, "..")))
		;
	smbd_closedir(fd);
	return r;
}

/* MCMAN has three descriptors for every user of the cards together, so an SMB handle holds none
 * of its own: one card file stays open, for the handle that last read or wrote, and a listing
 * opens its folder only while it reads it. */
static int card_fd = -1, card_owner = -1;

static void card_close(void)
{
	if (card_fd >= 0)
		smbd_close(card_fd);
	card_fd = card_owner = -1;
}

static int card_fd_of(int i)
{
	int r;

	if (card_owner == i)
		return card_fd;
	card_close();
	if ((r = smbd_open(handles[i].path, handles[i].write, 0, 0)) < 0)
		return r;
	card_owner = i;
	return card_fd = r;
}

static void release(int i)
{
	struct handle *h = &handles[i];

	if (card_owner == i || h->delete_on_close) /* nothing open on what is removed */
		card_close();
	if (h->delete_on_close && !is_root(h->path)) {
		if (h->dir)
			smbd_rmdir(h->path);
		else
			smbd_remove(h->path);
	}
	h->used = 0;
	if (last_create == i)
		last_create = -1;
}

static void unix_to_tv(uint64_t t, struct smb2_timeval *tv)
{
	tv->tv_sec = (time_t)t;
	tv->tv_usec = 0;
}

static uint32_t attributes(const struct smbd_stat *st)
{
	return st->dir ? SMB2_FILE_ATTRIBUTE_DIRECTORY : SMB2_FILE_ATTRIBUTE_ARCHIVE;
}

static uint64_t allocation(const struct smbd_stat *st)
{
	return st->dir ? 0 : (st->size + 1023) & ~(uint64_t)1023;
}

/* --- requests ---------------------------------------------------------------------------- */

static int authorize_user(struct smb2_server *srvr, struct smb2_context *smb2, const char *user,
                          const char *domain, const char *workstation)
{
	(void)srvr;
	(void)domain;
	(void)workstation;
	if (!user || !ieq(user, smbd_user))
		return -1;
	smb2_set_user(smb2, smbd_user);
	smb2_set_password(smb2, smbd_password);
	return 0;
}

static int session_established(struct smb2_server *srvr, struct smb2_context *smb2)
{
	(void)srvr;
	(void)smb2;
	return 0;
}

static int destruction_event(struct smb2_server *srvr, struct smb2_context *smb2)
{
	int i;

	(void)srvr;
	for (i = 0; i < MAX_HANDLES; i++)
		if (handles[i].used && handles[i].owner == smb2)
			release(i);
	smbd_clients--;
	return 0;
}

static int logoff_cmd(struct smb2_server *srvr, struct smb2_context *smb2)
{
	(void)srvr;
	(void)smb2;
	return 0;
}

static int tree_connect_cmd(struct smb2_server *srvr, struct smb2_context *smb2,
                            struct smb2_tree_connect_request *req, struct smb2_tree_connect_reply *rep)
{
	struct smb2_pdu *pdu;
	char name[8];
	int n = req->path_length / 2, start = n, i, len;

	(void)srvr;
	while (start > 0 && req->path[start - 1] != '\\')
		start--;
	len = n - start;
	for (i = 0; i < len && i < (int)sizeof(name) - 1; i++)
		name[i] = req->path[start + i] < 0x80 ? (char)req->path[start + i] : '?';
	name[i] = 0;
	for (i = 0; i < SMBD_SHARES; i++)
		if (len == 3 && ieq(name, share_names[i]))
			break;
	if (i == SMBD_SHARES)
		return reply(smb2, SMB2_TREE_CONNECT, SMB2_STATUS_BAD_NETWORK_NAME);

	rep->share_type = SMB2_SHARE_TYPE_DISK;
	rep->maximal_access = 0x001f01ff; /* FILE_ALL_ACCESS */
	/* The tree id carries the share, so later requests know their root. */
	tree_counter = (tree_counter + 1) & 0xffffff;
	pdu = smb2_cmd_tree_connect_reply_async(smb2, rep, 0x50000000 | (tree_counter << 4) | i, NULL, NULL);
	if (!pdu)
		return reply(smb2, SMB2_TREE_CONNECT, SMB2_STATUS_INSUFFICIENT_RESOURCES);
	smb2_set_pdu_message_id(smb2, pdu, smb2_get_last_request_message_id(smb2));
	smb2_queue_pdu(smb2, pdu);
	return 1;
}

static int tree_disconnect_cmd(struct smb2_server *srvr, struct smb2_context *smb2, const uint32_t tree_id)
{
	(void)srvr;
	(void)smb2;
	(void)tree_id;
	return 0;
}

static int create_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_create_request *req,
                      struct smb2_create_reply *rep)
{
	struct smbd_stat st;
	struct handle *h;
	char path[SMBD_PATH_MAX];
	int share = share_of_tree(smb2), i, ret, exists, want_dir, want_file, disp = req->create_disposition;
	int write = (req->desired_access & (SMB2_FILE_WRITE_DATA | SMB2_FILE_APPEND_DATA | SMB2_GENERIC_WRITE |
	                                    SMB2_GENERIC_ALL | SMB2_MAXIMUM_ALLOWED)) != 0;

	(void)srvr;
	last_create = -1;
	if (share < 0)
		return reply(smb2, SMB2_CREATE, SMB2_STATUS_INVALID_PARAMETER);
	if ((ret = make_path(share, req->name, path)) < 0)
		return reply(smb2, SMB2_CREATE, status_of(ret));
	for (i = 0; i < MAX_HANDLES && handles[i].used; i++)
		;
	if (i == MAX_HANDLES)
		return reply(smb2, SMB2_CREATE, SMB2_STATUS_TOO_MANY_OPENED_FILES);

	ret = smbd_stat(path, &st);
	if (ret < 0 && ret != -ENOENT)
		return reply(smb2, SMB2_CREATE, status_of(ret));
	exists = ret == 0;
	want_dir = (req->create_options & SMB2_FILE_DIRECTORY_FILE) != 0;
	want_file = (req->create_options & SMB2_FILE_NON_DIRECTORY_FILE) != 0;

	if (exists && disp == SMB2_FILE_CREATE)
		return reply(smb2, SMB2_CREATE, SMB2_STATUS_OBJECT_NAME_COLLISION);
	if (!exists && (disp == SMB2_FILE_OPEN || disp == SMB2_FILE_OVERWRITE))
		return reply(smb2, SMB2_CREATE, SMB2_STATUS_OBJECT_NAME_NOT_FOUND);
	if (exists && st.dir && want_file)
		return reply(smb2, SMB2_CREATE, SMB2_STATUS_FILE_IS_A_DIRECTORY);
	if (exists && !st.dir && want_dir)
		return reply(smb2, SMB2_CREATE, SMB2_STATUS_NOT_A_DIRECTORY);
	/* A folder is deleted on close only when empty: say so now, not after a silent failure. */
	if (exists && st.dir && (req->create_options & SMB2_FILE_DELETE_ON_CLOSE) && (ret = dir_used(path)) != 0)
		return reply(smb2, SMB2_CREATE, ret > 0 ? SMB2_STATUS_DIRECTORY_NOT_EMPTY : status_of(ret));

	h = &handles[i];
	memset(h, 0, sizeof(*h));
	if (exists ? st.dir : want_dir) {
		if (!exists && (ret = smbd_mkdir(path)) < 0)
			return reply(smb2, SMB2_CREATE, status_of(ret));
		h->dir = 1;
		rep->create_action = exists ? SMB2_FILE_OPENED : SMB2_FILE_CREATED;
	} else {
		int trunc = exists && disp != SMB2_FILE_OPEN && disp != SMB2_FILE_OPEN_IF;
		h->write = write || !exists || trunc;
		if (!exists || trunc) { /* an existing file opens at its first read or write */
			card_close(); /* also when another handle has it open: truncating removes it */
			if ((ret = smbd_open(path, 1, !exists, trunc)) < 0)
				return reply(smb2, SMB2_CREATE, status_of(ret));
			card_fd = ret;
			card_owner = i;
		}
		rep->create_action = !exists ? SMB2_FILE_CREATED : trunc ? SMB2_FILE_OVERWRITTEN : SMB2_FILE_OPENED;
	}
	if (smbd_stat(path, &st) < 0) {
		memset(&st, 0, sizeof(st));
		st.dir = h->dir;
	}
	h->used = 1;
	h->owner = smb2;
	h->share = (uint8_t)share;
	h->gen = next_gen++;
	h->delete_on_close = (req->create_options & SMB2_FILE_DELETE_ON_CLOSE) != 0;
	strcpy(h->path, path);
	last_create = i;

	rep->oplock_level = 0;
	rep->creation_time = smb2_timeval_to_win(&(struct smb2_timeval){(time_t)st.ctime, 0});
	rep->last_write_time = rep->change_time = rep->last_access_time =
	    smb2_timeval_to_win(&(struct smb2_timeval){(time_t)st.mtime, 0});
	rep->allocation_size = allocation(&st);
	rep->end_of_file = st.dir ? 0 : st.size;
	rep->file_attributes = attributes(&st);
	memset(rep->file_id, 0, SMB2_FD_SIZE);
	rep->file_id[0] = (uint8_t)i;
	rep->file_id[1] = (uint8_t)(i >> 8);
	rep->file_id[2] = (uint8_t)h->gen;
	rep->file_id[3] = (uint8_t)(h->gen >> 8);
	return 0;
}

static int close_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_close_request *req,
                     struct smb2_close_reply *rep)
{
	int i = lookup(smb2, req->file_id);

	(void)srvr;
	(void)rep; /* flags 0: the attributes in the reply are not valid */
	if (i < 0)
		return reply(smb2, SMB2_CLOSE, SMB2_STATUS_FILE_CLOSED);
	release(i);
	return 0;
}

static int flush_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_flush_request *req)
{
	(void)srvr;
	if (lookup(smb2, req->file_id) < 0)
		return reply(smb2, SMB2_FLUSH, SMB2_STATUS_FILE_CLOSED);
	return 0; /* writes go straight to the device; close finishes them */
}

static int read_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_read_request *req,
                    struct smb2_read_reply *rep)
{
	int i = lookup(smb2, req->file_id), n;
	uint32_t len = req->length;

	(void)srvr;
	if (i < 0)
		return reply(smb2, SMB2_READ, SMB2_STATUS_FILE_CLOSED);
	if (handles[i].dir)
		return reply(smb2, SMB2_READ, SMB2_STATUS_INVALID_PARAMETER);
	if (len > SMBD_MAX_IO)
		len = SMBD_MAX_IO;
	if ((n = card_fd_of(i)) < 0)
		return reply(smb2, SMB2_READ, status_of(n));
	rep->data = malloc(len ? len : 1); /* libsmb2 frees it with the reply */
	if (!rep->data)
		return reply(smb2, SMB2_READ, SMB2_STATUS_INSUFFICIENT_RESOURCES);
	n = smbd_pread(card_fd, rep->data, (int)len, req->offset);
	if (n <= 0) {
		free(rep->data);
		rep->data = NULL;
		return reply(smb2, SMB2_READ, n == 0 ? SMB2_STATUS_END_OF_FILE : status_of(n));
	}
	rep->data_length = (uint32_t)n;
	return 0;
}

static int write_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_write_request *req,
                     struct smb2_write_reply *rep)
{
	int i = lookup(smb2, req->file_id), n;

	(void)srvr;
	if (i < 0)
		return reply(smb2, SMB2_WRITE, SMB2_STATUS_FILE_CLOSED);
	if (handles[i].dir)
		return reply(smb2, SMB2_WRITE, SMB2_STATUS_INVALID_PARAMETER);
	if (req->length == 0) {
		rep->count = 0;
		return 0;
	}
	if ((n = card_fd_of(i)) < 0)
		return reply(smb2, SMB2_WRITE, status_of(n));
	n = smbd_pwrite(card_fd, req->buf, (int)req->length, req->offset);
	if (n < 0)
		return reply(smb2, SMB2_WRITE, status_of(n));
	if (n == 0) /* nothing written: the card is full */
		return reply(smb2, SMB2_WRITE, SMB2_STATUS_DISK_FULL);
	rep->count = (uint32_t)n; /* a short count tells the client the rest did not fit */
	return 0;
}

static int lock_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_lock_request *req)
{
	(void)srvr;
	(void)smb2;
	(void)req;
	return 0; /* one console, no byte-range locks */
}

static int ioctl_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_ioctl_request *req,
                     struct smb2_ioctl_reply *rep)
{
	(void)srvr;
	(void)req;
	(void)rep;
	return reply(smb2, SMB2_IOCTL, SMB2_STATUS_NOT_SUPPORTED);
}

static int cancel_cmd(struct smb2_server *srvr, struct smb2_context *smb2)
{
	(void)srvr;
	(void)smb2;
	return 0;
}

static int echo_cmd(struct smb2_server *srvr, struct smb2_context *smb2)
{
	(void)srvr;
	(void)smb2;
	return 0;
}

/* '*' and '?' as Windows uses them, without case. */
static int match(const char *pat, const char *s)
{
	if (!pat || !pat[0] || !strcmp(pat, "*"))
		return 1;
	for (; *pat; pat++, s++) {
		if (*pat == '*') {
			for (pat++; ; s++) {
				if (match(pat, s))
					return 1;
				if (!*s)
					return 0;
			}
		}
		if (!*s || (*pat != '?' && lower((unsigned char)*pat) != lower((unsigned char)*s)))
			return 0;
	}
	return !*s;
}

static int query_directory_cmd(struct smb2_server *srvr, struct smb2_context *smb2,
                               struct smb2_query_directory_request *req, struct smb2_query_directory_reply *rep)
{
	static struct smb2_fileidbothdirectoryinformation ent[DIR_BATCH];
	static char names[DIR_BATCH][SMBD_NAME_MAX];
	struct handle *h;
	struct smbd_stat st;
	uint32_t room = req->output_buffer_length, used = 0;
	int i = lookup(smb2, req->file_id), n = 0, ret, fd;

	(void)srvr;
	if (i < 0)
		return reply(smb2, SMB2_QUERY_DIRECTORY, SMB2_STATUS_FILE_CLOSED);
	h = &handles[i];
	if (!h->dir)
		return reply(smb2, SMB2_QUERY_DIRECTORY, SMB2_STATUS_INVALID_PARAMETER);
	if (req->flags & (SMB2_RESTART_SCANS | SMB2_REOPEN))
		h->dir_pos = h->dir_done = h->pending = 0;
	if (h->dir_done)
		return 0; /* empty buffer: STATUS_NO_MORE_FILES */
	if ((fd = smbd_opendir(h->path)) < 0) /* reopened for each reply, and skipped to where it was */
		return reply(smb2, SMB2_QUERY_DIRECTORY, status_of(fd));
	for (ret = 0; ret < h->dir_pos && smbd_readdir(fd, names[0], SMBD_NAME_MAX, &st) > 0; ret++)
		;
	while (n < DIR_BATCH) {
		uint32_t need;
		if (h->pending) {
			strcpy(names[n], h->pending_name);
			st = h->pending_st;
			h->pending = 0;
		} else if (smbd_readdir(fd, names[n], SMBD_NAME_MAX, &st) <= 0) {
			h->dir_done = 1;
			break;
		} else {
			h->dir_pos++;
		}
		if (!match(req->name, names[n]))
			continue;
		need = DIR_ENTRY_ROOM(strlen(names[n]));
		if (used + need > room) {
			/* No room for this one: it starts the next reply. */
			strcpy(h->pending_name, names[n]);
			h->pending_st = st;
			h->pending = 1;
			break;
		}
		used += need;
		memset(&ent[n], 0, sizeof(ent[n]));
		ent[n].file_index = n;
		unix_to_tv(st.ctime, &ent[n].creation_time);
		unix_to_tv(st.mtime, &ent[n].last_access_time);
		unix_to_tv(st.mtime, &ent[n].last_write_time);
		unix_to_tv(st.mtime, &ent[n].change_time);
		ent[n].end_of_file = st.dir ? 0 : st.size;
		ent[n].allocation_size = allocation(&st);
		ent[n].file_attributes = attributes(&st);
		ent[n].name = names[n];
		n++;
		if (req->flags & SMB2_RETURN_SINGLE_ENTRY)
			break;
	}
	smbd_closedir(fd);
	if (n == 0 && h->pending)
		return reply(smb2, SMB2_QUERY_DIRECTORY, SMB2_STATUS_INFO_LENGTH_MISMATCH);
	rep->output_buffer = (uint8_t *)ent;
	rep->output_buffer_length = n * PAD8(sizeof(ent[0]));
	return 0;
}

static int change_notify_cmd(struct smb2_server *srvr, struct smb2_context *smb2,
                             struct smb2_change_notify_request *req, struct smb2_change_notify_reply *rep)
{
	(void)srvr;
	(void)req;
	(void)rep;
	return reply(smb2, SMB2_CHANGE_NOTIFY, SMB2_STATUS_NOT_SUPPORTED);
}

static int query_info_cmd(struct smb2_server *srvr, struct smb2_context *smb2,
                          struct smb2_query_info_request *req, struct smb2_query_info_reply *rep)
{
	struct smbd_stat st;
	struct smb2_timeval ct, mt;
	uint64_t total = 0, avail = 0;
	int i = lookup(smb2, req->file_id), ret, len = 0;

	(void)srvr;
	if (i < 0)
		return reply(smb2, SMB2_QUERY_INFO, SMB2_STATUS_FILE_CLOSED);
	memset(&info, 0, sizeof(info));
	if (req->info_type == SMB2_0_INFO_FILE) {
		if ((ret = smbd_stat(handles[i].path, &st)) < 0)
			return reply(smb2, SMB2_QUERY_INFO, status_of(ret));
		unix_to_tv(st.ctime, &ct);
		unix_to_tv(st.mtime, &mt);
		switch (req->file_info_class) {
		case SMB2_FILE_BASIC_INFORMATION:
			info.basic.creation_time = ct;
			info.basic.last_access_time = info.basic.last_write_time = info.basic.change_time = mt;
			info.basic.file_attributes = attributes(&st);
			len = sizeof(info.basic);
			break;
		case SMB2_FILE_STANDARD_INFORMATION:
			info.standard.allocation_size = allocation(&st);
			info.standard.end_of_file = st.dir ? 0 : st.size;
			info.standard.number_of_links = 1;
			info.standard.delete_pending = handles[i].delete_on_close;
			info.standard.directory = (uint8_t)st.dir;
			len = sizeof(info.standard);
			break;
		case SMB2_FILE_ALL_INFORMATION:
			info.all.basic.creation_time = ct;
			info.all.basic.last_access_time = info.all.basic.last_write_time = info.all.basic.change_time = mt;
			info.all.basic.file_attributes = attributes(&st);
			info.all.standard.allocation_size = allocation(&st);
			info.all.standard.end_of_file = st.dir ? 0 : st.size;
			info.all.standard.number_of_links = 1;
			info.all.standard.delete_pending = handles[i].delete_on_close;
			info.all.standard.directory = (uint8_t)st.dir;
			info.all.index_number = (uint64_t)(i + 1);
			info.all.access_flags = 0x001f01ff;
			info.all.name = (const uint8_t *)"";
			len = sizeof(info.all);
			break;
		case SMB2_FILE_NETWORK_OPEN_INFORMATION:
			info.open.creation_time = ct;
			info.open.last_access_time = info.open.last_write_time = info.open.change_time = mt;
			info.open.allocation_size = allocation(&st);
			info.open.end_of_file = st.dir ? 0 : st.size;
			info.open.file_attributes = attributes(&st);
			len = sizeof(info.open);
			break;
		case SMB2_FILE_ATTRIBUTE_TAG_INFORMATION:
			info.tag.file_attributes = attributes(&st);
			len = sizeof(info.tag);
			break;
		}
	} else if (req->info_type == SMB2_0_INFO_FILESYSTEM) {
		switch (req->file_info_class) {
		case SMB2_FILE_FS_SIZE_INFORMATION:
		case SMB2_FILE_FS_FULL_SIZE_INFORMATION:
			if ((ret = smbd_space(handles[i].share, &total, &avail)) < 0)
				return reply(smb2, SMB2_QUERY_INFO, status_of(ret));
			if (req->file_info_class == SMB2_FILE_FS_SIZE_INFORMATION) {
				info.size.total_allocation_units = total / 1024;
				info.size.available_allocation_units = avail / 1024;
				info.size.sectors_per_allocation_unit = 2;
				info.size.bytes_per_sector = 512;
				len = sizeof(info.size);
			} else {
				info.full.total_allocation_units = total / 1024;
				info.full.caller_available_allocation_units = info.full.actual_available_allocation_units =
				    avail / 1024;
				info.full.sectors_per_allocation_unit = 2;
				info.full.bytes_per_sector = 512;
				len = sizeof(info.full);
			}
			break;
		case SMB2_FILE_FS_ATTRIBUTE_INFORMATION:
			info.attr.filesystem_attributes = 0x2; /* case-preserved names */
			info.attr.maximum_component_name_length = SMBD_NAME_MAX - 1;
			info.attr.filesystem_name = (const uint8_t *)"PS2MC";
			info.attr.filesystem_name_length = 5;
			len = sizeof(info.attr);
			break;
		case SMB2_FILE_FS_DEVICE_INFORMATION:
			info.device.device_type = FILE_DEVICE_DISK;
			info.device.characteristics = FILE_REMOVABLE_MEDIA;
			len = sizeof(info.device);
			break;
		case SMB2_FILE_FS_VOLUME_INFORMATION:
			info.volume.volume_serial_number = 0x50320000u + handles[i].share;
			info.volume.volume_label = (const uint8_t *)"";
			len = sizeof(info.volume);
			break;
		}
	}
	if (!len)
		return reply(smb2, SMB2_QUERY_INFO, SMB2_STATUS_NOT_SUPPORTED);
	rep->output_buffer = &info;
	rep->output_buffer_length = len;
	return 0;
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int rename_handle(struct smb2_context *smb2, int i, const uint8_t *buf, uint32_t len)
{
	struct smbd_stat st;
	char to[SMBD_PATH_MAX], aside[SMBD_PATH_MAX];
	const char *name;
	uint16_t *wide;
	uint32_t n;
	const char *a, *b;
	int ret;

	if (len < 20 || (n = le32(buf + 16)) > len - 20 || (n & 1))
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_INVALID_PARAMETER);
	wide = malloc(n + 2);
	if (!wide)
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_INSUFFICIENT_RESOURCES);
	memcpy(wide, buf + 20, n); /* the name is not aligned in the request */
	name = smb2_utf16_to_utf8(wide, n / 2);
	free(wide);
	if (!name)
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_INSUFFICIENT_RESOURCES);
	ret = make_path(handles[i].share, name, to);
	free((void *)name);
	if (ret < 0)
		return reply(smb2, SMB2_SET_INFO, status_of(ret));
	if (is_root(handles[i].path) || is_root(to))
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_ACCESS_DENIED);
	/* The card renames in place: refuse what smbd_rename() would before a replaced target is gone. */
	a = strrchr(handles[i].path, '/');
	b = strrchr(to, '/');
	if (a - handles[i].path != b - to || memcmp(handles[i].path, to, a - handles[i].path))
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_NOT_SAME_DEVICE);
	if (strlen(b + 1) > SMBD_LEAF_MAX)
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_NAME_TOO_LONG);
	aside[0] = 0;
	if (!ieq(to, handles[i].path) && smbd_stat(to, &st) == 0) {
		if (!buf[0] || st.dir) /* ReplaceIfExists; a folder is never replaced */
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_OBJECT_NAME_COLLISION);
		/* Move the target aside, not away, so a failed rename can put it back. */
		if ((size_t)(b - to) + sizeof("/SMBD-OLD.TMP") > sizeof(aside))
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_NAME_TOO_LONG);
		memcpy(aside, to, b - to);
		strcpy(aside + (b - to), "/SMBD-OLD.TMP");
		if (smbd_stat(aside, &st) == 0)
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_OBJECT_NAME_COLLISION);
		if ((ret = smbd_rename(to, aside)) < 0)
			return reply(smb2, SMB2_SET_INFO, status_of(ret));
	}
	if ((ret = smbd_rename(handles[i].path, to)) < 0) {
		if (aside[0])
			smbd_rename(aside, to);
		return reply(smb2, SMB2_SET_INFO, status_of(ret));
	}
	if (aside[0])
		smbd_remove(aside);
	strcpy(handles[i].path, to);
	return 0;
}

static int set_info_cmd(struct smb2_server *srvr, struct smb2_context *smb2, struct smb2_set_info_request *req)
{
	const uint8_t *buf = req->input_data;
	struct smbd_stat st;
	int i = lookup(smb2, req->file_id), ret;

	(void)srvr;
	if (i < 0)
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_FILE_CLOSED);
	if (req->info_type != SMB2_0_INFO_FILE)
		return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_NOT_SUPPORTED);
	switch (req->file_info_class) {
	case SMB2_FILE_BASIC_INFORMATION: /* times and attributes: kept as the card has them */
	case SMB2_FILE_ALLOCATION_INFORMATION:
		return 0;
	case SMB2_FILE_DISPOSITION_INFORMATION:
		if (req->buffer_length < 1)
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_INVALID_PARAMETER);
		if (buf[0] && is_root(handles[i].path))
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_ACCESS_DENIED);
		if (buf[0] && handles[i].dir && (ret = dir_used(handles[i].path)) != 0)
			return reply(smb2, SMB2_SET_INFO, ret > 0 ? SMB2_STATUS_DIRECTORY_NOT_EMPTY : status_of(ret));
		handles[i].delete_on_close = buf[0] != 0;
		return 0;
	case SMB2_FILE_END_OF_FILE_INFORMATION:
		if (req->buffer_length < 8 || handles[i].dir)
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_INVALID_PARAMETER);
		if ((ret = smbd_stat(handles[i].path, &st)) < 0)
			return reply(smb2, SMB2_SET_INFO, status_of(ret));
		/* Growing happens as the data is written. Memory cards cannot cut a file shorter. */
		if (((uint64_t)le32(buf) | ((uint64_t)le32(buf + 4) << 32)) < st.size)
			return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_NOT_SUPPORTED);
		return 0;
	case SMB2_FILE_RENAME_INFORMATION:
		card_close(); /* nothing open on what moves */
		return rename_handle(smb2, i, buf, req->buffer_length);
	}
	return reply(smb2, SMB2_SET_INFO, SMB2_STATUS_NOT_SUPPORTED);
}

struct smb2_server_request_handlers smbd_handlers = {
    .destruction_event = destruction_event,
    .authorize_user = authorize_user,
    .session_established = session_established,
    .logoff_cmd = logoff_cmd,
    .tree_connect_cmd = tree_connect_cmd,
    .tree_disconnect_cmd = tree_disconnect_cmd,
    .create_cmd = create_cmd,
    .close_cmd = close_cmd,
    .flush_cmd = flush_cmd,
    .read_cmd = read_cmd,
    .write_cmd = write_cmd,
    .lock_cmd = lock_cmd,
    .ioctl_cmd = ioctl_cmd,
    .cancel_cmd = cancel_cmd,
    .echo_cmd = echo_cmd,
    .query_directory_cmd = query_directory_cmd,
    .change_notify_cmd = change_notify_cmd,
    .query_info_cmd = query_info_cmd,
    .set_info_cmd = set_info_cmd,
};

/* Close every handle, as when the server stops. */
void smbd_release_all(void)
{
	int i;

	for (i = 0; i < MAX_HANDLES; i++)
		if (handles[i].used)
			release(i);
	card_close();
}

/* --- the server ------------------------------------------------------------------------- */

static struct smb2_server server;
int smbd_clients;

static void on_new_client(struct smb2_context *smb2, void *cb_data)
{
	(void)cb_data;
	smb2_set_version(smb2, SMB2_VERSION_ANY); /* 2.0.2 to 3.1.1; libsmb2 picks the highest shared */
	/* Counted here and in destruction_event, which also runs for one refused below. */
	if (++smbd_clients > SMBD_MAX_CLIENTS)
		smb2_close_context(smb2);
}

/* Serves until smbd_stop(). Returns 0 after a stop, or a negative errno (the port is taken). */
int smbd_serve(uint16_t port)
{
	int ret;

	memset(&server, 0, sizeof(server));
	server.handlers = &smbd_handlers;
	server.port = port;
	server.signing_enabled = 1; /* signed sessions; no guest login */
	server.allow_anonymous = 0;
	server.max_transact_size = server.max_read_size = server.max_write_size = SMBD_MAX_IO;
	memcpy(server.guid, "Launchstation-PS", 16);
	strcpy(server.hostname, "PS2");
	smbd_clients = 0;
	ret = smb2_serve_port(&server, SMBD_MAX_CLIENTS, on_new_client, NULL);
	/* smb2_serve_port() has torn down the connections; close what they left open. */
	smbd_release_all();
	smbd_clients = 0;
	return ret;
}

void smbd_stop(void)
{
	server.stop_requested = 1; /* seen within one 100 ms select round */
}

int smbd_listening(void)
{
	return server.listener_ready;
}
