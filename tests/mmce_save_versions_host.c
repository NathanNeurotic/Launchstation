/* Host harness for src/mmce_save_versions.c: the real module, the real card
 * viewer and .psu writer, and POSIX stand-ins for the PS2 file calls. Paths
 * are host paths. The harness aborts if two files are ever open at once.
 *
 *   run <history> <saves_dir> <keep>   -> RUN|r|saves|versioned|unchanged|skipped|failed
 *   validate <path>                    -> VALID|r
 *   folder <save name>                 -> FOLDER|name
 *
 * Faults (environment): FAIL_WRITE=1 short writes, CORRUPT=1 flips a byte
 * of every write (the read-back must catch it), NORENAME=1 rename fails. */
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include "dash_internal.h"
#include "mmce_save_versions.h"

int fileMode = 0777;
static int open_files, max_open;

int genOpen(const char *path, int mode)
{
	int fd = open(path, mode, 0644);

	if (fd >= 0) {
		if (++open_files > 1) {
			fprintf(stderr, "two files open at once: %s\n", path);
			abort();
		}
		if (open_files > max_open)
			max_open = open_files;
	}
	return fd;
}
s64 genLseek(int fd, s64 where, int how) { return lseek(fd, where, how); }
int genRead(int fd, void *buf, int size) { return (int)read(fd, buf, (size_t)size); }
int genWrite(int fd, void *buf, int size)
{
	if (getenv("FAIL_WRITE"))
		return size > 1 ? (int)write(fd, buf, (size_t)size - 1) : -1;
	if (getenv("CORRUPT") && size > 0) {
		unsigned char *copy = malloc((size_t)size);
		int r;
		memcpy(copy, buf, (size_t)size);
		copy[size / 2] ^= 0x5a;
		r = (int)write(fd, copy, (size_t)size);
		free(copy);
		return r;
	}
	return (int)write(fd, buf, (size_t)size);
}
int genClose(int fd)
{
	open_files--;
	return close(fd);
}

static DIR *dirs[8];
int genDopen(char *path)
{
	int i;

	for (i = 0; i < 8 && dirs[i]; i++)
		;
	if (i == 8 || !(dirs[i] = opendir(path)))
		return -1;
	return i;
}
int genDclose(int fd)
{
	closedir(dirs[fd]);
	dirs[fd] = NULL;
	return 0;
}
int fileXioDread(int fd, iox_dirent_t *ent)
{
	struct dirent *d = readdir(dirs[fd]);
	struct stat st;

	if (!d)
		return 0;
	snprintf(ent->name, sizeof(ent->name), "%s", d->d_name);
	if (fstatat(dirfd(dirs[fd]), d->d_name, &st, 0) != 0)
		memset(&st, 0, sizeof(st));
	ent->stat.mode = S_ISDIR(st.st_mode) ? 0x1000 : 0x2000;
	ent->stat.size = (unsigned int)st.st_size;
	ent->stat.hisize = 0;
	return 1;
}
int genMkdir(const char *path, int mode) { return mkdir(path, (mode_t)mode); }
int genRemove(char *path) { return unlink(path); }
int genRenameNoReplace(const char *from, const char *to)
{
	if (getenv("NORENAME"))
		return -38;
	if (access(to, F_OK) == 0)
		return -17;
	return rename(from, to) == 0 ? 0 : -5;
}
int genCmpFileExt(const char *name, const char *ext)
{
	const char *dot = strrchr(name, '.');

	return dot && !strcasecmp(dot + 1, ext);
}
int wleExists(const char *path) { return access(path, F_OK) == 0; }

int main(int argc, char **argv)
{
	if (argc >= 5 && !strcmp(argv[1], "run")) {
		MmceSaveVersionStats st;
		int r = mmceSaveVersionsFromHistory(argv[2], argv[3], atoi(argv[4]), &st);

		printf("RUN|%d|%d|%d|%d|%d|%d\n", r, st.saves, st.versioned, st.unchanged, st.skipped, st.failed);
	} else if (argc >= 3 && !strcmp(argv[1], "validate")) {
		printf("VALID|%d\n", mmceSaveVersionValidate(argv[2]));
	} else if (argc >= 3 && !strcmp(argv[1], "folder")) {
		char out[48];

		mmceSaveVersionFolder(argv[2], out, sizeof(out));
		printf("FOLDER|%s\n", out);
	} else
		return 2;
	printf("END|open=%d|max_open=%d\n", open_files, max_open);
	return 0;
}
