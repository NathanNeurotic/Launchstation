/* Host build of the SMB file server (iop/smbd/smbd_fs.c): the same request handlers, with the
 * console's file interface (smbd.h) done in POSIX under ROOT/mc0 and ROOT/mc1.
 *
 *   host PORT ROOT USER PASSWORD [FREE_KB]
 *
 * FREE_KB caps the space a share reports and accepts (a write past it fails as a full card
 * does). SIGTERM stops the server through smbd_stop(), as the Stop button does on the PS2. */
#include "smbd.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

const char *smbd_user, *smbd_password;
static const char *root;
static long long free_limit = -1;

#define MAX_DIRS 32
static DIR *dirs[MAX_DIRS];
static char dir_paths[MAX_DIRS][4096];

/* "mc0:/a/b" -> "ROOT/mc0/a/b" */
static const char *host_path(const char *path, char *buf)
{
	snprintf(buf, 4096, "%s/%.3s%s", root, path, path + 4);
	return buf;
}

int smbd_share_root(int share, char *out, int max)
{
	return snprintf(out, max, "mc%d:", share);
}

int smbd_stat(const char *path, struct smbd_stat *st)
{
	char b[4096];
	struct stat s;

	if (stat(host_path(path, b), &s) < 0)
		return -errno;
	st->dir = S_ISDIR(s.st_mode);
	st->size = s.st_size;
	st->mtime = s.st_mtime;
	st->ctime = s.st_ctime;
	return 0;
}

int smbd_open(const char *path, int write, int create, int trunc)
{
	char b[4096];
	int fd = open(host_path(path, b), (write ? O_RDWR : O_RDONLY) | (create ? O_CREAT | O_EXCL : 0) |
	                                      (trunc ? O_TRUNC : 0), 0644);
	return fd < 0 ? -errno : fd;
}

static long long used_bytes(const char *dir)
{
	char sub[4096];
	struct dirent *e;
	struct stat s;
	long long n = 0;
	DIR *d = opendir(dir);

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name);
		if (lstat(sub, &s) == 0)
			n += S_ISDIR(s.st_mode) ? used_bytes(sub) : s.st_size;
	}
	closedir(d);
	return n;
}

int smbd_pread(int fd, void *buf, int len, uint64_t off)
{
	ssize_t n = pread(fd, buf, len, (off_t)off);
	return n < 0 ? -errno : (int)n;
}

int smbd_pwrite(int fd, const void *buf, int len, uint64_t off)
{
	ssize_t n;
	struct stat s;

	if (free_limit >= 0 && fstat(fd, &s) == 0 && (long long)(off + len) > (long long)s.st_size) {
		char b[4096];
		long long room = free_limit * 1024 - used_bytes(host_path("mc0:", b)) - used_bytes(host_path("mc1:", b));
		long long grow = (long long)(off + len) - s.st_size;
		if (room <= 0)
			return 0;
		if (grow > room)
			len -= (int)(grow - room);
	}
	n = pwrite(fd, buf, len, (off_t)off);
	return n < 0 ? -errno : (int)n;
}

int smbd_close(int fd)
{
	return close(fd) < 0 ? -errno : 0;
}

int smbd_opendir(const char *path)
{
	char b[4096];
	int i;

	for (i = 0; i < MAX_DIRS && dirs[i]; i++)
		;
	if (i == MAX_DIRS)
		return -EMFILE;
	if (!(dirs[i] = opendir(host_path(path, b))))
		return -errno;
	snprintf(dir_paths[i], sizeof(dir_paths[i]), "%s", b);
	return i;
}

int smbd_readdir(int fd, char *name, int max, struct smbd_stat *st)
{
	char sub[8192];
	struct dirent *e;
	struct stat s;

	if (!(e = readdir(dirs[fd])))
		return 0;
	snprintf(name, max, "%s", e->d_name);
	snprintf(sub, sizeof(sub), "%s/%s", dir_paths[fd], e->d_name);
	memset(st, 0, sizeof(*st));
	if (stat(sub, &s) == 0) {
		st->dir = S_ISDIR(s.st_mode);
		st->size = s.st_size;
		st->mtime = s.st_mtime;
		st->ctime = s.st_ctime;
	}
	return 1;
}

int smbd_closedir(int fd)
{
	closedir(dirs[fd]);
	dirs[fd] = NULL;
	return 0;
}

int smbd_mkdir(const char *path)
{
	char b[4096];
	return mkdir(host_path(path, b), 0755) < 0 ? -errno : 0;
}

int smbd_remove(const char *path)
{
	char b[4096];
	return unlink(host_path(path, b)) < 0 ? -errno : 0;
}

int smbd_rmdir(const char *path)
{
	char b[4096];
	return rmdir(host_path(path, b)) < 0 ? -errno : 0;
}

int smbd_rename(const char *from, const char *to)
{
	char a[4096], b[4096];
	return rename(host_path(from, a), host_path(to, b)) < 0 ? -errno : 0;
}

int smbd_space(int share, uint64_t *total, uint64_t *avail)
{
	char b[4096];
	long long used;

	(void)share;
	used = used_bytes(host_path("mc0:", b)) + used_bytes(host_path("mc1:", b));
	*total = 8 * 1024 * 1024;
	*avail = free_limit >= 0 ? (uint64_t)(free_limit * 1024 > used ? free_limit * 1024 - used : 0)
	                         : 8 * 1024 * 1024;
	return 0;
}

static void on_term(int sig)
{
	(void)sig;
	smbd_stop();
}

int main(int argc, char **argv)
{
	int ret;

	if (argc < 5) {
		fprintf(stderr, "usage: host PORT ROOT USER PASSWORD [FREE_KB]\n");
		return 2;
	}
	root = argv[2];
	smbd_user = argv[3];
	smbd_password = argv[4];
	if (argc > 5)
		free_limit = atoll(argv[5]);
	signal(SIGTERM, on_term);
	signal(SIGPIPE, SIG_IGN);
	ret = smbd_serve((uint16_t)atoi(argv[1]));
	printf("smbd_serve returned %d\n", ret);
	fflush(stdout);
	return ret == 0 ? 0 : 1;
}
