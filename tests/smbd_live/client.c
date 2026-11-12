/* A client that keeps several files open at once, as Explorer and most copy tools do, where
 * smbclient opens one at a time. Built on libsmb2's client.
 *
 *   client HOST:PORT USER PASSWORD
 *
 * On share mc0, which holds A.TXT ("aaaa"), B.TXT ("bbbb"), E.TXT and DIR/F.TXT ("ffff"):
 *   - open A, open B, open A again, and create C, all held open at once;
 *   - list the share while they are open;
 *   - read A through both of its handles and B, and write C;
 *   - refuse to rename A while it is open, E onto B while B is open, and folder DIR while
 *     DIR/F.TXT is open, reading each held handle after;
 *   - close them all, rename C to D, then read D back.
 * Prints one line per failure. Exits 0 only if everything held. */
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>

static int failed;

static void check(int ok, const char *what, struct smb2_context *smb2)
{
	if (!ok) {
		printf("client: %s: %s\n", what, smb2_get_error(smb2));
		failed = 1;
	}
}

static int read_is(struct smb2_context *smb2, struct smb2fh *fh, const char *want)
{
	uint8_t buf[16] = {0};
	int n = smb2_pread(smb2, fh, buf, sizeof(buf) - 1, 0);
	return n == (int)strlen(want) && !memcmp(buf, want, (size_t)n);
}

struct raw_reply {
	int done, status;
	uint8_t type, cls;
	uint32_t len;
	uint8_t buf[128];
	char stream_name[16];
};

static void raw_cb(struct smb2_context *smb2, int status, void *data, void *cb_data)
{
	struct raw_reply *r = cb_data;
	struct smb2_query_info_reply *rep = data;

	r->status = status;
	if (status == 0 && rep) {
		r->len = rep->output_buffer_length;
		memcpy(r->buf, rep->output_buffer, r->len < sizeof(r->buf) ? r->len : sizeof(r->buf));
		/* libsmb2's client decodes FileStreamInformation even in passthrough: keep the name
		 * before the reply, which holds it, is freed. */
		if (r->type == SMB2_0_INFO_FILE && r->cls == SMB2_FILE_STREAM_INFORMATION) {
			const struct smb2_file_stream_info *si = rep->output_buffer;
			snprintf(r->stream_name, sizeof(r->stream_name), "%s", si->stream_name ? si->stream_name : "");
		}
		smb2_free_data(smb2, rep->output_buffer);
	}
	r->done = 1;
}

/* An info class as raw bytes, for classes libsmb2's client has no decoder for (Windows' stat asks for 6 and 59). */
static int query_raw_type(struct smb2_context *smb2, struct smb2fh *fh, uint8_t type, uint8_t cls,
                          struct raw_reply *r)
{
	struct smb2_query_info_request req;
	struct smb2_pdu *pdu;
	struct pollfd pfd;

	memset(&req, 0, sizeof(req));
	memset(r, 0, sizeof(*r));
	r->type = type;
	r->cls = cls;
	req.info_type = type;
	req.file_info_class = cls;
	req.output_buffer_length = 4096;
	memcpy(req.file_id, smb2_get_file_id(fh), SMB2_FD_SIZE);
	smb2_set_passthrough(smb2, 1);
	if (!(pdu = smb2_cmd_query_info_async(smb2, &req, raw_cb, r))) {
		smb2_set_passthrough(smb2, 0);
		return -1;
	}
	smb2_queue_pdu(smb2, pdu);
	while (!r->done) {
		pfd.fd = smb2_get_fd(smb2);
		pfd.events = smb2_which_events(smb2);
		if (poll(&pfd, 1, 5000) <= 0 || smb2_service(smb2, pfd.revents) < 0)
			break;
	}
	smb2_set_passthrough(smb2, 0);
	return r->done && r->status == 0 ? 0 : -1;
}

static int query_raw(struct smb2_context *smb2, struct smb2fh *fh, uint8_t cls, struct raw_reply *r)
{
	return query_raw_type(smb2, fh, SMB2_0_INFO_FILE, cls, r);
}

static uint32_t le32_at(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Refused because another handle holds what the rename would move or replace. */
static int rename_refused(struct smb2_context *smb2, const char *from, const char *to)
{
	return smb2_rename(smb2, from, to) < 0 && (uint32_t)smb2_get_nterror(smb2) == SMB2_STATUS_SHARING_VIOLATION;
}

int main(int argc, char **argv)
{
	struct smb2_context *smb2;
	struct smb2fh *a, *b, *a2, *c, *f;
	struct raw_reply ra, ra2, rb;
	struct smb2dir *dir;
	struct smb2dirent *ent;
	int seen = 0;

	if (argc < 4) {
		fprintf(stderr, "usage: client HOST:PORT USER PASSWORD\n");
		return 2;
	}
	smb2 = smb2_init_context();
	smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_REQUIRED);
	smb2_set_password(smb2, argv[3]);
	if (smb2_connect_share(smb2, argv[1], "mc0", argv[2]) < 0) {
		printf("client: connect: %s\n", smb2_get_error(smb2));
		smb2_destroy_context(smb2);
		return 1;
	}

	a = smb2_open(smb2, "A.TXT", O_RDONLY);
	check(a != NULL, "open A", smb2);
	b = smb2_open(smb2, "B.TXT", O_RDONLY);
	check(b != NULL, "open B while A is open", smb2);
	a2 = smb2_open(smb2, "A.TXT", O_RDONLY);
	check(a2 != NULL, "open A a second time", smb2);
	c = smb2_open(smb2, "C.TXT", O_WRONLY | O_CREAT);
	check(c != NULL, "create C while A, B and A again are open", smb2);

	dir = smb2_opendir(smb2, "");
	check(dir != NULL, "list the share while four files are open", smb2);
	if (dir) {
		while ((ent = smb2_readdir(smb2, dir)))
			seen += !strcmp(ent->name, "A.TXT") || !strcmp(ent->name, "B.TXT");
		smb2_closedir(smb2, dir);
		check(seen == 2, "the listing shows A and B", smb2);
	}

	if (a)
		check(read_is(smb2, a, "aaaa"), "read A", smb2);
	if (b)
		check(read_is(smb2, b, "bbbb"), "read B", smb2);
	if (a2)
		check(read_is(smb2, a2, "aaaa"), "read A through its second handle", smb2);
	if (c)
		check(smb2_pwrite(smb2, c, (const uint8_t *)"cccc", 4, 0) == 4, "write C", smb2);
	if (a)
		check(read_is(smb2, a, "aaaa"), "read A again after the others", smb2);
	/* What Windows' stat needs: a file number, no EAs, and a file id; one file, one number. */
	if (a && a2 && b) {
		check(query_raw(smb2, a, SMB2_FILE_INTERNAL_INFORMATION, &ra) == 0 && ra.len == 8,
		      "A's file number (FileInternalInformation)", smb2);
		check(query_raw(smb2, a2, SMB2_FILE_INTERNAL_INFORMATION, &ra2) == 0 && !memcmp(ra.buf, ra2.buf, 8),
		      "the same number through A's second handle", smb2);
		check(query_raw(smb2, b, SMB2_FILE_INTERNAL_INFORMATION, &rb) == 0 && memcmp(ra.buf, rb.buf, 8),
		      "B's number differs from A's", smb2);
		check(query_raw(smb2, a, SMB2_FILE_EA_INFORMATION, &rb) == 0 && rb.len == 4 && !memcmp(rb.buf, "\0\0\0\0", 4),
		      "A has no extended attributes (FileEaInformation)", smb2);
		check(query_raw(smb2, a, SMB2_FILE_ID_INFORMATION, &rb) == 0 && rb.len == 24 && !memcmp(rb.buf + 8, ra.buf, 8),
		      "A's file id carries its number (FileIdInformation)", smb2);
		/* What Explorer, robocopy and WSL ask for beyond that. */
		/* Decoded by libsmb2's client, so this is its struct, not the wire bytes. */
		check(query_raw(smb2, a, SMB2_FILE_STREAM_INFORMATION, &rb) == 0 &&
		          ((struct smb2_file_stream_info *)rb.buf)->stream_size == 4 &&
		          ((struct smb2_file_stream_info *)rb.buf)->next_entry_offset == 0 &&
		          !strcmp(rb.stream_name, "::$DATA"),
		      "A has one stream, ::$DATA, 4 bytes (FileStreamInformation)", smb2);
		check(query_raw(smb2, a, 68, &rb) == 0 && rb.len == 72 && !memcmp(rb.buf, ra.buf, 8) &&
		          le32_at(rb.buf + 48) == 4 && le32_at(rb.buf + 64) == 1 && le32_at(rb.buf + 8) != 0,
		      "A's FileStatInformation: its number, a time, 4 bytes, one link", smb2);
		check(query_raw(smb2, a, 70, &rb) == 0 && rb.len == 96 && !memcmp(rb.buf, ra.buf, 8) &&
		          !memcmp(rb.buf + 72, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 24),
		      "A's FileStatLxInformation, with no Lx fields marked valid", smb2);
		check(query_raw(smb2, a, SMB2_FILE_ALTERNATE_NAME_INFORMATION, &rb) < 0 && rb.done &&
		          (uint32_t)rb.status == SMB2_STATUS_OBJECT_NAME_NOT_FOUND,
		      "A has no 8.3 name (FileAlternateNameInformation: OBJECT_NAME_NOT_FOUND)", smb2);
		check(query_raw_type(smb2, a, SMB2_0_INFO_FILESYSTEM, SMB2_FILE_FS_SECTOR_SIZE_INFORMATION, &rb) == 0 &&
		          rb.len == 28 && le32_at(rb.buf) == 512 && le32_at(rb.buf + 4) == 512 && le32_at(rb.buf + 16) == 3,
		      "512-byte sectors (FileFsSectorSizeInformation, decoded: the struct is the wire order)", smb2);
	}

	/* A rename would leave a held handle naming nothing, or naming another file: refused. */
	check(rename_refused(smb2, "A.TXT", "D.TXT"), "refuse to rename A while it is open", smb2);
	if (a)
		check(read_is(smb2, a, "aaaa"), "read A after the refused rename", smb2);
	if (a2)
		check(read_is(smb2, a2, "aaaa"), "read A's second handle after the refused rename", smb2);
	check(rename_refused(smb2, "E.TXT", "B.TXT"), "refuse to rename E onto B while B is open", smb2);
	if (b)
		check(read_is(smb2, b, "bbbb"), "read B after the refused rename onto it", smb2);
	f = smb2_open(smb2, "DIR/F.TXT", O_RDONLY);
	check(f != NULL, "open F inside DIR", smb2);
	check(rename_refused(smb2, "DIR", "DIR2"), "refuse to rename DIR while F inside it is open", smb2);
	if (f) {
		check(read_is(smb2, f, "ffff"), "read F after the refused folder rename", smb2);
		smb2_close(smb2, f);
	}

	if (a)
		smb2_close(smb2, a);
	if (b)
		smb2_close(smb2, b);
	if (a2)
		smb2_close(smb2, a2);
	if (c)
		smb2_close(smb2, c);

	check(smb2_rename(smb2, "C.TXT", "D.TXT") == 0, "rename C once nothing holds it", smb2);
	c = smb2_open(smb2, "D.TXT", O_RDONLY);
	check(c != NULL && read_is(smb2, c, "cccc"), "C reads back as D", smb2);
	if (c)
		smb2_close(smb2, c);

	smb2_disconnect_share(smb2);
	smb2_destroy_context(smb2);
	if (!failed)
		printf("client: all held\n");
	return failed;
}
