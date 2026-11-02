/* Fault-injected driver/socket contracts for the production FTP server. */
#include "FtpClient.h"
#include "FtpMessages.h"
#include "FtpServer.h"
#include <assert.h>
#define MAX_NODES 64
#define PAYLOAD_SIZE 100003
#define MAX_SOCKETS 48

typedef struct {
	iop_device_t *device;
	int unit, dir;
	char path[256];
	unsigned char *data;
	size_t size;
	u64 reported_size;
} Node;
typedef struct {
	int used, index;
	Node *node;
	size_t position;
} Handle;
typedef struct {
	unsigned char input[PAYLOAD_SIZE + 1024], output[PAYLOAD_SIZE + 4096];
	int size, pos, out, closed, read_cap, write_cap, read_error, write_error;
} Socket;
static Node nodes[MAX_NODES];
static int node_count;
static Handle handles[32];
static int open_count, close_count, rename_calls, truncations;
static int write_cap, read_cap, write_error, read_error, dir_error, close_error, seek_error;
static int dir_calls, dir_fail_on;
static Socket sockets[MAX_SOCKETS];
static int next_socket = 30, connect_error;
static iop_device_t *devices[FS_IOMANX_DEVICES];
static iop_device_t usb, mass, mc, mmce, pfs, duplicate_usb, ata;
static unsigned char payload[PAYLOAD_SIZE];
static Node *find(iop_device_t *device, int unit, const char *path)
{
	for (int i = 0; i < node_count; i++)
		if (nodes[i].device == device && nodes[i].unit == unit && !strcmp(nodes[i].path, path))
			return &nodes[i];
	return NULL;
}
static Node *add(iop_device_t *device, int unit, const char *path, int dir, const void *data, size_t size)
{
	assert(node_count < MAX_NODES);
	Node *n = &nodes[node_count++];
	memset(n, 0, sizeof(*n));
	n->device = device;
	n->unit = unit;
	n->dir = dir;
	strcpy(n->path, path);
	n->data = calloc(PAYLOAD_SIZE + 1024, 1);
	assert(n->data);
	if (size)
		memcpy(n->data, data, size);
	n->size = n->reported_size = size;
	return n;
}
static Handle *allocate(Node *node)
{
	for (unsigned i = 0; i < sizeof(handles) / sizeof(handles[0]); i++)
		if (!handles[i].used) {
			handles[i] = (Handle){.used = 1, .node = node};
			open_count++;
			return &handles[i];
		}
	assert(0);
	return NULL;
}
static int drv_open(iop_file_t *f, const char *name, int flags, int mode)
{
	(void)mode;
	Node *n = find(f->device, f->unit, name);
	if (!n && (flags & O_CREAT))
		n = add(f->device, f->unit, name, 0, NULL, 0);
	if (!n)
		return -ENOENT;
	if (n->dir)
		return -EISDIR;
	if (flags & O_TRUNC) {
		n->size = n->reported_size = 0;
		truncations++;
	}
	f->privdata = allocate(n);
	return 0;
}
static int drv_close(iop_file_t *f)
{
	Handle *h = f->privdata;
	assert(h && h->used);
	h->used = 0;
	f->privdata = NULL;
	close_count++;
	return close_error ? -EIO : 0;
}
static int drv_read(iop_file_t *f, void *data, int length)
{
	Handle *h = f->privdata;
	assert(h && h->used);
	if (read_error)
		return read_error;
	if (length > read_cap)
		length = read_cap;
	if ((size_t)length > h->node->size - h->position)
		length = h->node->size - h->position;
	memcpy(data, h->node->data + h->position, length);
	h->position += length;
	return length;
}
static int drv_write(iop_file_t *f, void *data, int length)
{
	Handle *h = f->privdata;
	assert(h && h->used);
	if (write_error)
		return write_error;
	if (length > write_cap)
		length = write_cap;
	assert(h->position + length <= PAYLOAD_SIZE + 1024);
	memcpy(h->node->data + h->position, data, length);
	h->position += length;
	if (h->position > h->node->size)
		h->node->size = h->node->reported_size = h->position;
	return length;
}
static s64 drv_seek64(iop_file_t *f, s64 offset, int whence)
{
	Handle *h = f->privdata;
	assert(h && h->used);
	if (seek_error)
		return -EIO;
	s64 position = offset + (whence == SEEK_END	  ? (s64)h->node->reported_size
							 : whence == SEEK_CUR ? (s64)h->position
												  : 0);
	if (position < 0)
		return -EINVAL;
	h->position = position;
	return position;
}
static int drv_seek(iop_file_t *f, int offset, int whence)
{
	return drv_seek64(f, offset, whence);
}
static int drv_dopen(iop_file_t *f, const char *name)
{
	Node *n = find(f->device, f->unit, name);
	if (!n || !n->dir)
		return -ENOENT;
	f->privdata = allocate(n);
	return 0;
}
static void metadata(iox_stat_t *s, Node *n)
{
	memset(s, 0, sizeof(*s));
	s->mode = (n->device->type & IOP_DT_FSEXT) ? (n->dir ? FIO_S_IFDIR : FIO_S_IFREG) | (0777)
											   : (n->dir ? FIO_SO_IFDIR : FIO_SO_IFREG) | 7;
	s->size = n->reported_size;
	s->hisize = n->reported_size >> 32;
	if (!strcmp(n->device->name, "hdd")) {
		s->mode = n->dir ? 0x0100 : 0x0040;
		s->attr = 0;
	}
	s->mtime[4] = 31;
	s->mtime[5] = 10;
	s->mtime[6] = 0xea;
	s->mtime[7] = 7;
}
static int drv_stat(iop_file_t *f, const char *name, iox_stat_t *s)
{
	Node *n = find(f->device, f->unit, name);
	if (!n)
		return -ENOENT;
	metadata(s, n);
	return 0;
}
static int drv_dread(iop_file_t *f, iox_dirent_t *entry)
{
	Handle *h = f->privdata;
	assert(h && h->used);
	if (dir_error)
		return dir_error;
	if (++dir_calls == dir_fail_on)
		return -EIO;
	while (h->index < node_count) {
		Node *n = &nodes[h->index++];
		const char *leaf;
		size_t parent = strlen(h->node->path);
		if (n == h->node || n->device != f->device || n->unit != f->unit)
			continue;
		if (parent == 1) {
			leaf = n->path + 1;
		} else {
			if (strncmp(n->path, h->node->path, parent) || n->path[parent] != '/')
				continue;
			leaf = n->path + parent + 1;
		}
		if (strchr(leaf, '/'))
			continue;
		if (f->device->type & IOP_DT_FSEXT) {
			memset(entry, 0, sizeof(*entry));
			strcpy(entry->name, leaf);
			metadata(&entry->stat, n);
		} else {
			io_dirent_t *old = (io_dirent_t *)entry;
			iox_stat_t s;
			memset(old, 0, sizeof(*old));
			strcpy(old->name, leaf);
			metadata(&s, n);
			old->stat.mode = s.mode;
			old->stat.size = s.size;
			old->stat.hisize = s.hisize;
			memcpy(old->stat.mtime, s.mtime, 8);
		}
		return 1;
	}
	return 0;
}
static int drv_rename(iop_file_t *f, const char *old, const char *newname)
{
	rename_calls++;
	Node *n = find(f->device, f->unit, old);
	if (!n)
		return -ENOENT;
	if (find(f->device, f->unit, newname))
		return -EEXIST;
	strcpy(n->path, newname);
	return 0;
}
static int drv_mount(iop_file_t *f, const char *fs, const char *dev, int flags, void *arg, int len)
{
	(void)f;
	(void)fs;
	(void)dev;
	(void)flags;
	(void)arg;
	(void)len;
	return 0;
}
static int drv_umount(iop_file_t *f, const char *name)
{
	(void)f;
	(void)name;
	return 0;
}
static iop_device_ops_t ops = {.open = drv_open,
							   .close = drv_close,
							   .read = drv_read,
							   .write = drv_write,
							   .lseek = drv_seek,
							   .dopen = drv_dopen,
							   .dclose = drv_close,
							   .dread = drv_dread,
							   .getstat = drv_stat,
							   .rename = drv_rename,
							   .lseek64 = drv_seek64,
							   .mount = drv_mount,
							   .umount = drv_umount};
iomanX_iop_device_t **iomanX_GetDeviceList(void)
{
	return devices;
}
int sceCdReadClock(sceCdCLOCK *clock)
{
	memset(clock, 0, sizeof(*clock));
	clock->day = 0x31;
	clock->month = 0x10;
	clock->year = 0x26;
	return 1;
}
int DelayThread(unsigned int delay)
{
	(void)delay;
	return 0;
}
int ftp_test_send(int fd, const void *data, int length, int flags)
{
	(void)flags;
	Socket *s = &sockets[fd];
	assert(!s->closed);
	if (s->write_error) {
		errno = s->write_error;
		s->write_error = 0;
		return -1;
	}
	if (length > s->write_cap)
		length = s->write_cap;
	assert(s->out + length < (int)sizeof(s->output));
	memcpy(s->output + s->out, data, length);
	s->out += length;
	s->output[s->out] = 0;
	return length;
}
int ftp_test_recv(int fd, void *data, int length, int flags)
{
	(void)flags;
	Socket *s = &sockets[fd];
	if (s->read_error) {
		errno = s->read_error;
		s->read_error = 0;
		return -1;
	}
	if (length > s->read_cap)
		length = s->read_cap;
	if (length > s->size - s->pos)
		length = s->size - s->pos;
	memcpy(data, s->input + s->pos, length);
	s->pos += length;
	return length;
}
int ftp_test_disconnect(int fd)
{
	assert(fd >= 0 && fd < MAX_SOCKETS);
	sockets[fd].closed++;
	return 0;
}
int ftp_test_select(int count, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
	(void)e;
	(void)t;
	int ready = 0;
	for (int fd = 0; fd < count; fd++) {
		if (FD_ISSET(fd, r)) {
			if (sockets[fd].pos < sockets[fd].size || sockets[fd].read_error)
				ready++;
			else
				FD_CLR(fd, r);
		}
		if (FD_ISSET(fd, w))
			ready++;
	}
	return ready;
}
int ftp_test_socket(int family, int type, int protocol)
{
	(void)family;
	(void)type;
	(void)protocol;
	assert(next_socket < MAX_SOCKETS);
	return next_socket++;
}
int ftp_test_connect(int fd, const struct sockaddr *address, socklen_t length)
{
	(void)fd;
	(void)address;
	(void)length;
	if (connect_error) {
		errno = connect_error;
		return -1;
	}
	return 0;
}
int ftp_test_accept(int fd, struct sockaddr *address, socklen_t *length)
{
	(void)fd;
	(void)address;
	(void)length;
	errno = EAGAIN;
	return -1;
}
int ftp_test_bind(int fd, const struct sockaddr *address, socklen_t length)
{
	(void)fd;
	(void)address;
	(void)length;
	return 0;
}
int ftp_test_listen(int fd, int backlog)
{
	(void)fd;
	(void)backlog;
	return 0;
}
int ftp_test_getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
	(void)fd;
	struct sockaddr_in a = {
		.sin_family = AF_INET, .sin_port = htons(12345), .sin_addr.s_addr = htonl(0x7f000001)};
	memcpy(address, &a, sizeof(a));
	*length = sizeof(a);
	return 0;
}
int ftp_test_getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
	(void)fd;
	(void)level;
	(void)option;
	*(int *)value = connect_error;
	*length = sizeof(int);
	return 0;
}
int ftp_test_setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
	(void)fd;
	(void)level;
	(void)option;
	(void)value;
	(void)length;
	return 0;
}
static void clear_output(int fd)
{
	sockets[fd].out = 0;
	sockets[fd].output[0] = 0;
}
static int reply(int fd)
{
	return atoi((char *)sockets[fd].output);
}
static void command(FtpClient *c, const char *text)
{
	clear_output(c->m_iControlSocket);
	FtpClient_OnCommand(c, text);
}
static void setup(void)
{
	for (int i = 0; i < node_count; i++) {
		free(nodes[i].data);
	}
	memset(nodes, 0, sizeof(nodes));
	node_count = 0;
	memset(handles, 0, sizeof(handles));
	open_count = close_count = rename_calls = truncations = 0;
	write_cap = read_cap = 32768;
	write_error = read_error = dir_error = close_error = seek_error = 0;
	dir_calls = dir_fail_on = 0;
	memset(sockets, 0, sizeof(sockets));
	for (int i = 0; i < MAX_SOCKETS; i++)
		sockets[i].read_cap = sockets[i].write_cap = 32768;
	memset(devices, 0, sizeof(devices));
	usb = (iop_device_t){.name = "usb", .type = IOP_DT_FS | IOP_DT_FSEXT, .ops = &ops};
	mass = (iop_device_t){.name = "mass", .type = IOP_DT_FS | IOP_DT_FSEXT, .ops = &ops};
	mc = (iop_device_t){.name = "mc", .type = IOP_DT_FS, .ops = &ops};
	mmce = (iop_device_t){.name = "mmce", .type = IOP_DT_FS | IOP_DT_FSEXT, .ops = &ops};
	pfs = (iop_device_t){.name = "pfs", .type = IOP_DT_FS | IOP_DT_FSEXT, .ops = &ops};
	ata = (iop_device_t){.name = "ata", .type = IOP_DT_FS | IOP_DT_FSEXT, .ops = &ops};
	duplicate_usb = usb;
	devices[0] = &mass;
	devices[2] = &usb;
	devices[3] = &usb;
	devices[4] = &duplicate_usb;
	devices[6] = &mc;
	devices[9] = &mmce;
	devices[14] = &ata;
	devices[31] = &pfs;
	add(&usb, 0, "/", 1, NULL, 0);
	add(&usb, 1, "/", 1, NULL, 0);
	add(&usb, 0, "/folder", 1, NULL, 0);
	add(&usb, 0, "/file.elf", 0, payload, sizeof(payload));
	add(&usb, 0, "/other.elf", 0, payload, 7);
	add(&usb, 1, "/file.elf", 0, payload, 13);
	add(&mc, 0, "/", 1, NULL, 0);
	add(&mc, 0, "/file.elf", 0, payload, 7);
	add(&mmce, 0, "/", 1, NULL, 0);
	add(&mmce, 1, "/", 1, NULL, 0);
	add(&pfs, 0, "/", 1, NULL, 0);
	add(&pfs, 1, "/", 1, NULL, 0);
	add(&pfs, 0, "/file.elf", 0, payload, 7);
	next_socket = 30;
	connect_error = 0;
}
static FtpClient *client(FtpServer *server, int fd, const char *cwd)
{
	FtpClient *c = FtpServer_OnClientConnect(server, fd);
	assert(c);
	c->m_eAuthState = AUTHSTATE_VALID;
	assert(FileSystem_ChangeDir(&c->m_kContext, cwd) == 0);
	clear_output(fd);
	return c;
}
static void test_paths_and_listing(void)
{
	setup();
	FSContext ctx;
	FSFileInfo info;
	char path[FS_PATH_MAX];
	FileSystem_Create(&ctx);
	assert(!FileSystem_BuildPath(path, "/usb/0/folder/", ".././file.elf") &&
		   !strcmp(path, "/usb/0/file.elf"));
	assert(!FileSystem_BuildPath(path, "/", "../../") && !strcmp(path, "/"));
	strcpy(path, "/usb/0/folder");
	assert(!FileSystem_BuildPath(path, path, "../other.elf") && !strcmp(path, "/usb/0/other.elf"));
	char oversized[FS_PATH_MAX + 1];
	memset(oversized, 'a', sizeof(oversized) - 1);
	oversized[sizeof(oversized) - 1] = 0;
	assert(FileSystem_BuildPath(path, "/", oversized) == -ENAMETOOLONG);
	assert(FileSystem_ScanDevice("usb") == &usb);
	assert(!FileSystem_ScanDevice("usb-extra"));
	assert(!FileSystem_ScanDevice("mass"));
	assert(!FileSystem_OpenDir(&ctx, "/"));
	int seen_usb = 0, seen_pfs = 0, count = 0;
	while (FileSystem_ReadDir(&ctx, &info) > 0) {
		assert(strcmp(info.m_Name, "mass"));
		seen_usb += !strcmp(info.m_Name, "usb");
		seen_pfs += !strcmp(info.m_Name, "pfs");
		count++;
	}
	assert(count == 5 && seen_usb == 1 && seen_pfs == 1);
	assert(!FileSystem_OpenDir(&ctx, "/usb"));
	count = 0;
	while (FileSystem_ReadDir(&ctx, &info) > 0) {
		assert(atoi(info.m_Name) == count++);
	}
	assert(count == 2);
	assert(!FileSystem_OpenDir(&ctx, "/mc"));
	count = 0;
	while (FileSystem_ReadDir(&ctx, &info) > 0)
		count++;
	assert(count == 1);
	assert(!FileSystem_ChangeDir(&ctx, "/usb/0"));
	assert(!FileSystem_ChangeDir(&ctx, "."));
	assert(!strcmp(ctx.m_Path, "/usb/0/"));
	assert(FileSystem_ChangeDir(&ctx, "missing") < 0);
	assert(!strcmp(ctx.m_Path, "/usb/0/"));
	assert(!FileSystem_ChangeDir(&ctx, ".."));
	assert(!strcmp(ctx.m_Path, "/usb/"));
	assert(!FileSystem_OpenDir(&ctx, "/usb/0"));
	count = 0;
	while (FileSystem_ReadDir(&ctx, &info) > 0)
		count++;
	assert(count == 3);
	dir_error = -EIO;
	assert(FileSystem_ReadDir(&ctx, &info) == -EIO);
	dir_error = 0;
	FileSystem_Close(&ctx);
	assert(open_count == close_count);
	/* Legacy USB-only driver is still presented as usb, never mass. */
	devices[2] = devices[3] = devices[4] = NULL;
	mass.type = IOP_DT_FS;
	assert(FileSystem_ScanDevice("usb") == &mass);
	assert(!FileSystem_OpenDir(&ctx, "/"));
	seen_usb = 0;
	while (FileSystem_ReadDir(&ctx, &info) > 0) {
		assert(strcmp(info.m_Name, "mass"));
		seen_usb += !strcmp(info.m_Name, "usb");
	}
	assert(seen_usb == 1);
	FileSystem_Close(&ctx);
	puts("PASS: sparse device discovery, duplicate drivers, usb-only namespace, unit probing, paths and "
		 "directory errors");
}
static void test_hdd_listing(void)
{
	setup();
	iop_device_t hdd = {.name = "hdd", .type = IOP_DT_FS | IOP_DT_FSEXT, .ops = &ops};
	devices[16] = &hdd;
	add(&hdd, 0, "/", 1, NULL, 0);
	add(&hdd, 0, "/not-pfs", 0, NULL, 0);
	add(&hdd, 0, "/PP.GAME.PCB", 1, NULL, 0);
	add(&hdd, 0, "/__common", 1, NULL, 0);
	FSContext ctx;
	FSFileInfo info;
	FileSystem_Create(&ctx);
	assert(!FileSystem_OpenDir(&ctx, "/hdd/0"));
	dir_calls = 0;
	dir_fail_on = 2;
	assert(FileSystem_ReadDir(&ctx, &info) == -EIO);
	dir_fail_on = 0;
	assert(!FileSystem_OpenDir(&ctx, "/hdd/0"));
	assert(FileSystem_ReadDir(&ctx, &info) == 1 && !strcmp(info.m_Name, "__common"));
	assert(FileSystem_ReadDir(&ctx, &info) == 0);
	assert(!FileSystem_ChangeDir(&ctx, "/hdd/0/__common/"));
	assert(!strcmp(ctx.m_Path, "/pfs/0/"));
	FileSystem_Close(&ctx);
	assert(open_count == close_count);
	puts("PASS: HDD partition filtering terminates on errors and CWD auto-mounts PFS0");
}
static void test_renames(void)
{
	setup();
	FtpServer s;
	FtpServer_Create(&s);
	FtpClient *a = client(&s, 10, "/usb/0"), *b = client(&s, 11, "/usb/0");
	command(a, "RNFR file.elf");
	assert(reply(10) == 350);
	assert(!a->m_kContext.m_iOpen);
	command(b, "PWD");
	command(b, "PASV");
	command(b, "SIZE other.elf");
	command(b, "RNFR other.elf");
	assert(reply(11) == 350);
	command(a, "RNTO moved.elf");
	assert(reply(10) == 250);
	assert(find(&usb, 0, "/moved.elf") && !find(&usb, 0, "/file.elf"));
	command(b, "RNTO renamed.elf");
	assert(reply(11) == 250 && find(&usb, 0, "/renamed.elf"));
	command(a, "RNTO stale.elf");
	assert(reply(10) == 503);
	command(a, "RNFR moved.elf");
	command(a, "RNTO /usb/1/wrong.elf");
	assert(reply(10) == 550);
	assert(find(&usb, 0, "/moved.elf") && !find(&usb, 1, "/wrong.elf"));
	assert(find(&usb, 1, "/file.elf")->size == 13);
	command(a, "RNFR moved.elf");
	command(a, "RNTO /mmce/0/wrong.elf");
	assert(reply(10) == 550 && !find(&mmce, 0, "/wrong.elf"));
	command(a, "RNFR moved.elf");
	command(a, "NOOP");
	command(a, "RNTO cancelled.elf");
	assert(reply(10) == 503);
	command(a, "RNFR missing.elf");
	assert(reply(10) == 550 && !a->m_Rename.path[0]);
	command(a, "CWD /mc/0");
	command(a, "RNFR file.elf");
	assert(reply(10) == 502 && !a->m_Rename.path[0]);
	command(a, "CWD /usb/0");
	command(a, "RNFR folder");
	assert(reply(10) == 350);
	command(a, "RNTO new-folder");
	assert(reply(10) == 250 && find(&usb, 0, "/new-folder"));
	command(a, "CWD /pfs/0");
	command(a, "RNFR file.elf");
	assert(reply(10) == 350);
	assert(!FileSystem_UnmountDevice(&b->m_kContext, "/pfs/0"));
	command(a, "RNTO stale.elf");
	assert(reply(10) == 550 && find(&pfs, 0, "/file.elf"));
	command(a, "RNFR file.elf");
	assert(!FileSystem_UnmountDevice(&b->m_kContext, "/pfs/1"));
	command(a, "RNTO valid.elf");
	assert(reply(10) == 250 && find(&pfs, 0, "/valid.elf"));
	FtpServer_Destroy(&s);
	assert(open_count == close_count);
	puts("PASS: interleaved rename sources, sequencing, folders, cross-device rejection and PFS mount "
		 "generations");
}
static void start_data(FtpClient *c, int fd, DataAction action)
{
	c->m_iDataSocket = fd;
	c->m_eDataAction = action;
	c->m_uiDataBufferSize = 32768;
	c->m_eConnState = CONNSTATE_RUNNING;
	c->m_eDataMode = action == DATAACTION_STOR ? DATAMODE_READ : DATAMODE_WRITE;
	clear_output(c->m_iControlSocket);
}
static void test_transfers(void)
{
	const int caps[][3] = {{32768, 32768, 32768}, {17, 23, 37}, {1, 1, 1}, {4096, 7, 19}};
	for (unsigned k = 0; k < sizeof(caps) / sizeof(caps[0]); k++) {
		setup();
		FtpServer s;
		FtpServer_Create(&s);
		FtpClient *c = client(&s, 10, "/usb/0");
		assert(!FileSystem_OpenFile(&c->m_kContext, "upload.bin", FM_WRITE, 0));
		start_data(c, 20, DATAACTION_STOR);
		memcpy(sockets[20].input, payload, sizeof(payload));
		sockets[20].size = sizeof(payload);
		sockets[20].read_cap = caps[k][0];
		write_cap = caps[k][1];
		while (c->m_iDataSocket >= 0) {
			FtpClient_OnDataRead(c);
		}
		Node *n = find(&usb, 0, "/upload.bin");
		assert(n->size == sizeof(payload) && !memcmp(n->data, payload, sizeof(payload)));
		assert(reply(10) == 226 && c->m_uiDataOffset == sizeof(payload));
		assert(!FileSystem_OpenFile(&c->m_kContext, "upload.bin", FM_READ, 0));
		start_data(c, 21, DATAACTION_RETR);
		read_cap = caps[k][1];
		sockets[21].write_cap = caps[k][2];
		while (c->m_iDataSocket >= 0)
			FtpClient_OnDataWrite(c);
		assert(sockets[21].out == sizeof(payload) && !memcmp(sockets[21].output, payload, sizeof(payload)) &&
			   reply(10) == 226);
		FtpServer_Destroy(&s);
		assert(open_count == close_count);
	}
	setup();
	FtpServer s;
	FtpServer_Create(&s);
	FtpClient *c = client(&s, 10, "/usb/0");
	assert(!FileSystem_OpenFile(&c->m_kContext, "file.elf", FM_READ, 0));
	start_data(c, 20, DATAACTION_RETR);
	read_error = -ENOENT;
	FtpClient_OnDataWrite(c);
	assert(reply(10) == 500 && !sockets[20].out);
	read_error = 0;
	assert(!FileSystem_OpenFile(&c->m_kContext, "file.elf", FM_READ, 0));
	start_data(c, 21, DATAACTION_RETR);
	FtpClient_OnDataWrite(c);
	assert(sockets[21].out > 0);
	sockets[21].write_error = EPIPE;
	FtpClient_OnDataWrite(c);
	assert(reply(10) == 500 && c->m_iDataSocket < 0);
	assert(!FileSystem_OpenFile(&c->m_kContext, "bad.bin", FM_WRITE, 0));
	start_data(c, 22, DATAACTION_STOR);
	sockets[22].read_error = ECONNRESET;
	FtpClient_OnDataRead(c);
	assert(reply(10) == 500 && c->m_iDataSocket < 0);
	assert(!FileSystem_OpenFile(&c->m_kContext, "bad.bin", FM_WRITE, 0));
	start_data(c, 23, DATAACTION_STOR);
	sockets[23].size = 100;
	write_error = -EIO;
	FtpClient_OnDataRead(c);
	assert(reply(10) == 500 && !find(&usb, 0, "/bad.bin")->size);
	write_error = 0;
	assert(!FileSystem_OpenDir(&c->m_kContext, "/usb/0"));
	start_data(c, 24, DATAACTION_LIST);
	dir_error = -EIO;
	FtpClient_OnDataWrite(c);
	assert(reply(10) == 500 && !sockets[24].out);
	dir_error = 0;
	assert(!FileSystem_OpenDir(&c->m_kContext, "/"));
	start_data(c, 25, DATAACTION_LIST);
	sockets[25].write_cap = 1;
	while (c->m_iDataSocket >= 0)
		FtpClient_OnDataWrite(c);
	assert(reply(10) == 226 && strstr((char *)sockets[25].output, " usb\r\n") &&
		   !strstr((char *)sockets[25].output, " mass\r\n"));
	assert(!FileSystem_OpenDir(&c->m_kContext, "/"));
	start_data(c, 26, DATAACTION_NLST);
	sockets[26].write_error = EPIPE;
	FtpClient_OnDataWrite(c);
	assert(reply(10) == 500);
	assert(!FileSystem_OpenFile(&c->m_kContext, "close.bin", FM_WRITE, 0));
	start_data(c, 27, DATAACTION_STOR);
	close_error = 1;
	FtpClient_OnDataRead(c);
	assert(reply(10) == 451 && !strstr((char *)sockets[10].output, "226 "));
	close_error = 0;
	assert(!FileSystem_OpenFile(&c->m_kContext, "empty.bin", FM_WRITE, 0));
	start_data(c, 28, DATAACTION_STOR);
	FtpClient_OnDataRead(c);
	assert(reply(10) == 226 && !find(&usb, 0, "/empty.bin")->size);
	FtpServer_Destroy(&s);
	assert(open_count == close_count);
	puts("PASS: exact upload/download bytes with short I/O, directory sends and network/filesystem failures");
}
static void test_resume_and_size(void)
{
	setup();
	FtpServer s;
	FtpServer_Create(&s);
	FtpClient *c = client(&s, 10, "/usb/0");
	int before = truncations;
	seek_error = 1;
	assert(FileSystem_OpenFile(&c->m_kContext, "file.elf", FM_WRITE, 500) < 0);
	assert(truncations == before && find(&usb, 0, "/file.elf")->size == sizeof(payload));
	seek_error = 0;
	assert(FileSystem_OpenFile(&c->m_kContext, "missing.bin", FM_WRITE, 500) < 0 &&
		   !find(&usb, 0, "/missing.bin"));
	assert(!FileSystem_OpenFile(&c->m_kContext, "append.bin", FM_WRITE, -1));
	FileSystem_Close(&c->m_kContext);
	Node *n = find(&usb, 0, "/file.elf");
	n->reported_size = ((u64)5 << 30) + 123;
	command(c, "SIZE file.elf");
	assert(!strcmp((char *)sockets[10].output, "213 5368709243\r\n"));
	Node *large = add(&mmce, 0, "/large.bin", 0, payload, 7);
	large->reported_size = ((u64)5 << 30) + 123;
	assert(!FileSystem_OpenFile(&c->m_kContext, "/mmce/0/large.bin", FM_READ, large->reported_size));
	read_error = -EIO;
	char byte;
	assert(FileSystem_ReadFile(&c->m_kContext, &byte, 1) == 0);
	assert(drv_seek64(&c->m_kContext.m_kFile, large->reported_size - 1, SEEK_SET) >= 0);
	assert(FileSystem_ReadFile(&c->m_kContext, &byte, 1) == -EIO);
	assert(drv_seek64(&c->m_kContext.m_kFile, 0, SEEK_CUR) == (s64)large->reported_size - 1);
	FileSystem_Close(&c->m_kContext);
	read_error = 0;
	char text[21];
	extern char *uitoa(char *, u64);
	uitoa(text, ~(u64)0);
	assert(!strcmp(text, "18446744073709551615"));
	FtpServer_Destroy(&s);
	assert(open_count == close_count);
	puts("PASS: failed resumes preserve files, append creates and 64-bit file sizes are exact");
}
static void feed(Socket *s, const char *text)
{
	int n = strlen(text);
	assert(s->size + n < (int)sizeof(s->input));
	memcpy(s->input + s->size, text, n);
	s->size += n;
}
static void test_command_stream(void)
{
	setup();
	FtpServer s;
	FtpServer_Create(&s);
	FtpClient *c = client(&s, 10, "/usb/0");
	feed(&sockets[10], "RNFR file.elf\r\nRNTO pipelined.elf\r\nPWD\r\n");
	assert(FtpServer_HandleEvents(&s) > 0);
	assert(find(&usb, 0, "/pipelined.elf") && strstr((char *)sockets[10].output, "350 ") &&
		   strstr((char *)sockets[10].output, "250 ") && strstr((char *)sockets[10].output, "257 "));
	clear_output(10);
	sockets[10].read_cap = 1;
	feed(&sockets[10], "NOOP\r\n");
	while (sockets[10].pos < sockets[10].size)
		FtpServer_HandleEvents(&s);
	assert(reply(10) == 200 && !c->m_iCommandOffset);
	c->m_eDataMode = DATAMODE_WRITE;
	command(c, "CWD /mc/0");
	assert(reply(10) == 450 && !strcmp(c->m_kContext.m_Path, "/usb/0/"));
	command(c, "ABOR");
	assert(reply(10) == 226 && c->m_eDataMode == DATAMODE_IDLE);
	memset(c->m_kContext.m_Path, 'a', sizeof(c->m_kContext.m_Path) - 1);
	c->m_kContext.m_Path[0] = '/';
	c->m_kContext.m_Path[sizeof(c->m_kContext.m_Path) - 1] = 0;
	sockets[10].write_cap = 1;
	command(c, "PWD");
	assert(reply(10) == 257 && !strcmp((char *)sockets[10].output + sockets[10].out - 3, "\"\r\n"));
	sockets[10].read_cap = 32768;
	char longline[512];
	memset(longline, 'x', 511);
	longline[511] = 0;
	feed(&sockets[10], longline);
	FtpServer_HandleEvents(&s);
	assert(!c->m_pServer && sockets[10].closed);
	FtpServer_Destroy(&s);
	assert(open_count == close_count);
	setup();
	FtpServer_Create(&s);
	c = client(&s, 10, "/usb/0");
	sockets[10].write_error = EPIPE;
	command(c, "PWD");
	assert(!c->m_pServer && sockets[10].closed);
	FtpServer_Destroy(&s);
	puts("PASS: combined/split command streams, transfer guards, oversized commands and control failures");
}
int main(void)
{
	for (int i = 0; i < PAYLOAD_SIZE; i++)
		payload[i] = (unsigned char)(i * 17 + i / 251);
	test_paths_and_listing();
	test_hdd_listing();
	test_renames();
	test_transfers();
	test_resume_and_size();
	test_command_stream();
	setup();
	for (int i = 0; i < node_count; i++) {
		free(nodes[i].data);
	}
	return 0;
}
