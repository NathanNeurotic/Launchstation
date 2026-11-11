/* smbd.irx: Launchstation's on-demand SMB2/3 file server for the memory cards.
 *
 *   SifExecModuleBuffer(smbd_irx, size, len, "USER\0PASSWORD", &ret)
 *
 * Serves \\<ip>\mc0 and \\<ip>\mc1 on port 445 from a thread of its own. The "smbd:" device
 * takes the EE's devctls (SMBD_DEVCTL_* in include/smbd_devctl.h): status, stop and start.
 * The module stays resident after a stop, so nothing else on the IOP has to be reset.
 *
 * This file is the platform half of smbd.h: iomanX for files, MCMAN for rename and free
 * space, AllocSysMemory for libsmb2's heap. smbd_fs.c answers the requests. */
#include <irx.h>
#include <types.h>
#include <intrman.h>
#include <iomanX.h>
#include <loadcore.h>
#include <mcman.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>

#include "smbd.h"
#include "../../include/smbd_devctl.h"

IRX_ID("smbd", 1, 0);

static char user[32] = "ps2", password[32] = "ps2";
const char *smbd_user = user, *smbd_password = password;
static volatile int running;
static int last_error;

/* libsmb2's heap. A size word in front of each block makes realloc() exact. */
void *malloc(int size)
{
	int state;
	u32 *p;

	CpuSuspendIntr(&state);
	p = AllocSysMemory(ALLOC_FIRST, size + 8, NULL);
	CpuResumeIntr(state);
	if (!p)
		return NULL;
	p[0] = size;
	return p + 2;
}

void free(void *ptr)
{
	int state;

	if (!ptr)
		return;
	CpuSuspendIntr(&state);
	FreeSysMemory((u32 *)ptr - 2);
	CpuResumeIntr(state);
}

void *calloc(size_t n, size_t size)
{
	void *p = malloc(n * size);
	if (p)
		memset(p, 0, n * size);
	return p;
}

void *realloc(void *ptr, size_t size)
{
	void *n = malloc(size);
	if (n && ptr) {
		u32 old = ((u32 *)ptr)[-2];
		memcpy(n, ptr, old < size ? old : size);
		free(ptr);
	}
	return n;
}

/* --- files ------------------------------------------------------------------------------ */

/* MCMAN's raw results, as mcman_ioerrcode() maps them for its own iomanX calls. */
static int mc_errno(int r)
{
	static const u8 map[] = {EIO, EMFILE, EEXIST, EACCES, ENOENT, ENOSPC, EIO};
	return r >= 0 ? r : r >= -8 && r <= -2 ? -map[r + 8] : -ENXIO;
}

/* The card's clock is JST (UTC+9): {_, sec, min, hour, day, month, year lo, year hi}. */
static uint64_t unix_time(const unsigned char *t)
{
	int y = t[6] | t[7] << 8, m = t[5], era, yoe, doy;

	if (m < 1 || m > 12)
		return 0;
	y -= m <= 2;
	era = y / 400;
	yoe = y - era * 400;
	doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + t[4] - 1;
	return (uint64_t)(era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468) * 86400 + t[3] * 3600 +
	       t[2] * 60 + t[1] - 9 * 3600;
}

static void fill_stat(const iox_stat_t *s, struct smbd_stat *st)
{
	/* iomanX modes, or the legacy ones a bridged ioman driver reports */
	st->dir = FIO_S_ISDIR(s->mode) || FIO_SO_ISDIR(s->mode);
	st->size = s->size;
	st->mtime = unix_time(s->mtime);
	st->ctime = unix_time(s->ctime);
}

int smbd_share_root(int share, char *out, int max)
{
	(void)max;
	strcpy(out, share ? "mc1:" : "mc0:");
	return 4;
}

int smbd_stat(const char *path, struct smbd_stat *st)
{
	iox_stat_t s;
	int r;

	memset(st, 0, sizeof(*st));
	if (!path[5]) { /* "mc0:/" has no entry of its own: present if it opens */
		if ((r = iomanX_dopen(path)) < 0)
			return r;
		iomanX_dclose(r);
		st->dir = 1;
		return 0;
	}
	memset(&s, 0, sizeof(s));
	if ((r = iomanX_getstat(path, &s)) < 0)
		return r;
	fill_stat(&s, st);
	return 0;
}

int smbd_open(const char *path, int write, int create, int trunc)
{
	/* Truncate by recreating: MCMAN's open ignores FIO_O_TRUNC (mcman_open2 in iop/vmcman). */
	if (trunc && !create) {
		int r = iomanX_remove(path);
		if (r < 0)
			return r;
		create = 1;
	}
	return iomanX_open(path, (write ? FIO_O_RDWR : FIO_O_RDONLY) | (create ? FIO_O_CREAT : 0), 0666);
}

int smbd_pread(int fd, void *buf, int len, uint64_t off)
{
	int r = iomanX_lseek(fd, (int)off, FIO_SEEK_SET);
	return r < 0 ? r : iomanX_read(fd, buf, len);
}

int smbd_pwrite(int fd, const void *buf, int len, uint64_t off)
{
	int r = iomanX_lseek(fd, (int)off, FIO_SEEK_SET);
	return r < 0 ? r : iomanX_write(fd, (void *)buf, len);
}

int smbd_close(int fd)
{
	return iomanX_close(fd);
}

int smbd_opendir(const char *path)
{
	return iomanX_dopen(path);
}

int smbd_readdir(int fd, char *name, int max, struct smbd_stat *st)
{
	iox_dirent_t e;
	int r;

	memset(&e, 0, sizeof(e));
	if ((r = iomanX_dread(fd, &e)) <= 0)
		return r;
	strncpy(name, e.name, max - 1);
	name[max - 1] = 0;
	fill_stat(&e.stat, st);
	return 1;
}

int smbd_closedir(int fd)
{
	return iomanX_dclose(fd);
}

int smbd_mkdir(const char *path)
{
	return iomanX_mkdir(path, 0777);
}

int smbd_remove(const char *path)
{
	return iomanX_remove(path);
}

int smbd_rmdir(const char *path)
{
	int r = iomanX_rmdir(path);
	return r == -EEXIST ? -ENOTEMPTY : r; /* MCMAN's "not empty" */
}

/* The card renames an entry in place (as mc_rename does for VMCMAN). smbd_fs.c has already
 * refused another folder and a name longer than SMBD_LEAF_MAX. */
int smbd_rename(const char *from, const char *to)
{
	sceMcTblGetDir info;
	int port = from[2] - '0', r;
	if ((r = McDetectCard2(port, 0)) < -1)
		return mc_errno(r);
	memset(&info, 0, sizeof(info));
	strcpy(info.EntryName, strrchr(to, '/') + 1);
	return mc_errno(McSetFileInfo(port, 0, from + 4, &info, sceMcFileAttrFile));
}

int smbd_space(int share, uint64_t *total, uint64_t *avail)
{
	s16 pagesize;
	u16 blocksize;
	int cardsize, r;
	u8 flags;

	if ((r = McDetectCard2(share, 0)) < -1 || (r = McGetCardSpec(share, 0, &pagesize, &blocksize, &cardsize, &flags)) < 0)
		return mc_errno(r);
	if ((r = McGetFreeClusters(share, 0)) < 0)
		return mc_errno(r);
	*total = (uint64_t)cardsize * pagesize;
	*avail = (uint64_t)r * pagesize * 2; /* two pages to a cluster */
	return 0;
}

/* --- the server thread and the "smbd:" device ------------------------------------------- */

static void serve(void *arg)
{
	(void)arg;
	last_error = smbd_serve(445);
	running = 0;
	ExitDeleteThread();
}

static int start(void)
{
	iop_thread_t t;
	int id;

	if (running)
		return 0;
	memset(&t, 0, sizeof(t));
	t.attr = TH_C;
	t.thread = serve;
	t.stacksize = 0x4000;
	t.priority = 0x43; /* as ps2ftpd */
	if ((id = CreateThread(&t)) < 0)
		return id;
	last_error = 0;
	running = 1;
	if ((id = StartThread(id, NULL)) < 0) {
		running = 0;
		return id;
	}
	return 0;
}

static int dev_devctl(iomanX_iop_file_t *f, const char *name, int cmd, void *arg, unsigned int arglen, void *buf,
                      unsigned int buflen)
{
	int i;

	(void)f, (void)name, (void)arg, (void)arglen, (void)buf, (void)buflen;
	switch (cmd) {
	case SMBD_DEVCTL_STATUS:
		return running ? 1 | !!smbd_listening() << 1 | smbd_clients << 2 : last_error;
	case SMBD_DEVCTL_STOP: /* files close and clients drop within a select round */
		smbd_stop();
		for (i = 0; running && i < 30; i++)
			DelayThread(100000);
		return running ? -EBUSY : 0;
	case SMBD_DEVCTL_START:
		return start();
	}
	return -EINVAL;
}

IOMANX_RETURN_VALUE_IMPL(0);
IOMANX_RETURN_VALUE_IMPL(EPERM);

static iomanX_iop_device_ops_t ops = {
    IOMANX_RETURN_VALUE(0),     /* init */
    IOMANX_RETURN_VALUE(0),     /* deinit */
    IOMANX_RETURN_VALUE(EPERM), /* format */
    IOMANX_RETURN_VALUE(EPERM), /* open */
    IOMANX_RETURN_VALUE(EPERM), /* close */
    IOMANX_RETURN_VALUE(EPERM), /* read */
    IOMANX_RETURN_VALUE(EPERM), /* write */
    IOMANX_RETURN_VALUE(EPERM), /* lseek */
    IOMANX_RETURN_VALUE(EPERM), /* ioctl */
    IOMANX_RETURN_VALUE(EPERM), /* remove */
    IOMANX_RETURN_VALUE(EPERM), /* mkdir */
    IOMANX_RETURN_VALUE(EPERM), /* rmdir */
    IOMANX_RETURN_VALUE(EPERM), /* dopen */
    IOMANX_RETURN_VALUE(EPERM), /* dclose */
    IOMANX_RETURN_VALUE(EPERM), /* dread */
    IOMANX_RETURN_VALUE(EPERM), /* getstat */
    IOMANX_RETURN_VALUE(EPERM), /* chstat */
    IOMANX_RETURN_VALUE(EPERM), /* rename */
    IOMANX_RETURN_VALUE(EPERM), /* chdir */
    IOMANX_RETURN_VALUE(EPERM), /* sync */
    IOMANX_RETURN_VALUE(EPERM), /* mount */
    IOMANX_RETURN_VALUE(EPERM), /* umount */
    IOMANX_RETURN_VALUE_S64(EPERM), /* lseek64 */
    dev_devctl,
    IOMANX_RETURN_VALUE(EPERM), /* symlink */
    IOMANX_RETURN_VALUE(EPERM), /* readlink */
    IOMANX_RETURN_VALUE(EPERM), /* ioctl2 */
};

static iomanX_iop_device_t device = {"smbd", IOP_DT_FS | IOP_DT_FSEXT, 1, "SMB server", &ops};

int _start(int argc, char *argv[])
{
	if (argc > 2) {
		strncpy(user, argv[1], sizeof(user) - 1);
		strncpy(password, argv[2], sizeof(password) - 1);
	}
	iomanX_DelDrv(device.name);
	if (iomanX_AddDrv(&device) || start() < 0) {
		iomanX_DelDrv(device.name);
		return MODULE_NO_RESIDENT_END;
	}
	return MODULE_RESIDENT_END;
}
