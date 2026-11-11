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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

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

/* Refused because another handle holds what the rename would move or replace. */
static int rename_refused(struct smb2_context *smb2, const char *from, const char *to)
{
	return smb2_rename(smb2, from, to) < 0 && (uint32_t)smb2_get_nterror(smb2) == SMB2_STATUS_SHARING_VIOLATION;
}

int main(int argc, char **argv)
{
	struct smb2_context *smb2;
	struct smb2fh *a, *b, *a2, *c, *f;
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
