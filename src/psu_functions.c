#include "psu_functions.h"
#include "init.h"
#include <string.h>

#define PSU_HEADER_SIZE ((int)sizeof(psu_header))

void clear_psu_header(psu_header *psu)
{
	memset((void *)psu, 0, sizeof(psu_header));
}

void pad_psu_header(psu_header *psu)
{
	memset((void *)psu, 0xFF, sizeof(psu_header));
}

/* A PSU backup is written to a temporary file beside the .psu it makes: the
 * same name with ".ps~" for ".psu", so it fits wherever the .psu fits. An
 * older .psu of that name is replaced only once the new one is complete and
 * closed. A backup that stops before then removes the temporary file and
 * leaves the older .psu as it was, and one left by a backup that never
 * finished (power lost) is removed by the next backup to that name.
 *
 * Replacing: where genCanRenamePath() allows a rename (mass, mx4sio, ata,
 * PC Card CF, SMB, MMCE, and the HDD and DVR partitions), the older .psu is
 * removed and the new file renamed to its name; genRenameNoReplace() checks
 * that the rename really happened. UDPFS has no rename, host files are
 * renamed only through a handle that genRenameNoReplace() does not use, and
 * any rename can still be refused by its driver: in those cases the older
 * .psu is removed and the finished file copied to its name instead. Once
 * the older .psu is removed, the finished file is the only complete backup
 * of that name, so a failure in that last rename or copy leaves it as it is,
 * under the temporary name (and no partial .psu), and says so; the next
 * backup to that name replaces it like an unfinished one. The save that was
 * backed up is never changed. cdfs is read-only, so a backup there fails
 * when it creates the temporary file. */
#define PSU_COPY_BUFFER 0x20000  //128 KB: a multiple of every device's sector size

static void psu_backup_temp_path(const char *path, char *temp)
{
	size_t length = strlen(path);

	memcpy(temp, path, length + 1);
	if (length)
		temp[length - 1] = '~';  //"NAME.psu" -> "NAME.ps~"
}

void psu_backup_discard_root_image(int out_fd, const char *path)
{
	char temp[MAX_PATH];

	genClose(out_fd);
	psu_backup_temp_path(path, temp);
	genRemove(temp);
}

int psu_backup_create_root_image(const char *path, const mcT_header *folder_stats, int *out_fd, int *psu_content)
{
	char temp[MAX_PATH];
	psu_header psu_head;
	int fd, ret;

	psu_backup_temp_path(path, temp);
	genRemove(temp);  //left by a backup that never finished
	fd = genOpen(temp, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (fd < 0)
		return fd;

	clear_psu_header(&psu_head);
	*psu_content = 2;
	psu_head.attr = folder_stats->attr;
	psu_head.size = *psu_content;
	psu_head.cTime = folder_stats->cTime;
	psu_head.mTime = folder_stats->mTime;
	memcpy(psu_head.name, folder_stats->name, sizeof(psu_head.name));
	psu_head.unknown_1_u16 = folder_stats->unknown_1_u16;
	psu_head.unknown_2_u64 = folder_stats->unknown_2_u64;
	if ((ret = genWrite(fd, (void *)&psu_head, sizeof(psu_head))) != PSU_HEADER_SIZE)
		goto fail;

	clear_psu_header(&psu_head);
	psu_head.attr = 0x8427;  //Standard folder attr, for pseudo "." and ".."
	psu_head.cTime = folder_stats->cTime;
	psu_head.mTime = folder_stats->mTime;
	psu_head.name[0] = '.';
	if ((ret = genWrite(fd, (void *)&psu_head, sizeof(psu_head))) != PSU_HEADER_SIZE)
		goto fail;
	psu_head.name[1] = '.';
	if ((ret = genWrite(fd, (void *)&psu_head, sizeof(psu_head))) != PSU_HEADER_SIZE)
		goto fail;

	*out_fd = fd;
	return 0;

fail:
	psu_backup_discard_root_image(fd, path);
	return ret < 0 ? ret : -EIO;
}

/* Copies the finished file 'from' to the .psu 'to', replacing an older one.
 * *kept is set once that is removed: then a failure must keep 'from'. */
static int psu_backup_copy_file(const char *from, const char *to, int *kept)
{
	char *buf;
	int in_fd, out_fd, n, ret = 0;

	buf = (char *)memalign(64, PSU_COPY_BUFFER);
	if (buf == NULL)
		return -ENOMEM;
	in_fd = genOpen(from, FIO_O_RDONLY);
	if (in_fd < 0) {
		free(buf);
		return in_fd;
	}
	*kept = 1;
	genRemove((char *)to);  //the older .psu
	out_fd = genOpen(to, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (out_fd < 0)
		ret = out_fd;
	while (ret == 0) {
		n = genRead(in_fd, buf, PSU_COPY_BUFFER);
		if (n <= 0) {
			ret = n;  //0 at the end of the file
			break;
		}
		ret = genWrite(out_fd, buf, n);
		ret = (ret == n) ? 0 : (ret < 0 ? ret : -EIO);
	}
	if (out_fd >= 0) {
		n = genClose(out_fd);
		if (ret == 0 && n < 0)
			ret = n;
		if (ret < 0)
			genRemove((char *)to);  //a partial .psu is not left as a backup
	}
	genClose(in_fd);
	free(buf);
	return ret;
}

/* Replaces the .psu 'path' with the finished file 'temp' (see above), and
 * sets *kept once a failure must keep 'temp'. */
static int psu_backup_publish(char *temp, const char *path, int *kept)
{
	int ret;

	if (genCanRenamePath(path) && strncmp(path, "host", 4)) {
		*kept = 1;
		genRemove((char *)path);  //the older .psu
		if (genRenameNoReplace(temp, path) >= 0)
			return 0;
	}
	ret = psu_backup_copy_file(temp, path, kept);
	if (ret == 0)
		genRemove(temp);
	return ret;
}

int psu_backup_finalize_root_image(int out_fd, const char *path, const mcT_header *folder_stats, int psu_content, int *kept)
{
	char temp[MAX_PATH];
	psu_header psu_head;
	int result, closed;

	genLseek(out_fd, 0, SEEK_SET);
	clear_psu_header(&psu_head);
	psu_head.attr = folder_stats->attr;
	psu_head.size = psu_content;
	psu_head.cTime = folder_stats->cTime;
	psu_head.mTime = folder_stats->mTime;
	memcpy(psu_head.name, folder_stats->name, sizeof(psu_head.name));
	psu_head.unknown_1_u16 = folder_stats->unknown_1_u16;
	psu_head.unknown_2_u64 = folder_stats->unknown_2_u64;
	result = genWrite(out_fd, (void *)&psu_head, sizeof(psu_head));
	result = (result == PSU_HEADER_SIZE) ? 0 : (result < 0 ? result : -EIO);
	genLseek(out_fd, 0, SEEK_END);
	closed = genClose(out_fd);
	if (result == 0 && closed < 0)
		result = closed;
	psu_backup_temp_path(path, temp);
	*kept = 0;
	if (result == 0)
		result = psu_backup_publish(temp, path, kept);
	if (result == 0)
		*kept = 0;
	else if (!*kept)
		genRemove(temp);
	return result;
}

int psu_backup_begin_file_entry(int psu_fd, const mcT_header *file_stats, int *psu_pad_size, int *psu_content)
{
	psu_header psu_head;

	clear_psu_header(&psu_head);
	psu_head.attr = file_stats->attr;
	psu_head.size = file_stats->size;
	psu_head.cTime = file_stats->cTime;
	psu_head.mTime = file_stats->mTime;
	memcpy(psu_head.name, file_stats->name, sizeof(psu_head.name));
	if (genWrite(psu_fd, (void *)&psu_head, sizeof(psu_head)) != PSU_HEADER_SIZE)
		return -1;

	if (psu_head.size & 0x3FF)
		*psu_pad_size = 0x400 - (psu_head.size & 0x3FF);
	else
		*psu_pad_size = 0;
	(*psu_content)++;
	return 0;
}

int psu_backup_write_padding(int psu_fd, int psu_pad_size)
{
	psu_header psu_head;
	int bytes;

	pad_psu_header(&psu_head);
	while (psu_pad_size > 0) {
		bytes = (psu_pad_size >= PSU_HEADER_SIZE) ? PSU_HEADER_SIZE : psu_pad_size;
		if (genWrite(psu_fd, (void *)&psu_head, bytes) != bytes)
			return -1;
		psu_pad_size -= bytes;
	}
	return 0;
}

int psu_restore_open_root_image(const char *path, FILEINFO *folder_file, int *in_fd, int *psu_content)
{
	psu_header psu_head;
	mcT_header *mc_head = (mcT_header *)&folder_file->stats;
	int fd;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return -1;
	if (genRead(fd, (void *)&psu_head, sizeof(psu_head)) != PSU_HEADER_SIZE) {
		genClose(fd);
		return -1;
	}
	/* A PSU starts with its save folder. Anything else (another format renamed
	 * to .psu, a blank file) would make copy() treat the header as a file to
	 * open, with this PSU still open. */
	if (!(psu_head.attr & sceMcFileAttrSubdir)) {
		genClose(fd);
		return -1;
	}

	mc_head->attr = psu_head.attr;
	*psu_content = psu_head.size;
	mc_head->size = 0;
	mc_head->cTime = psu_head.cTime;
	mc_head->mTime = psu_head.mTime;
	memcpy(mc_head->name, psu_head.name, sizeof(psu_head.name));
	mc_head->unknown_1_u16 = psu_head.unknown_1_u16;
	mc_head->unknown_2_u64 = psu_head.unknown_2_u64;

	memcpy(folder_file->name, psu_head.name, sizeof(psu_head.name));
	folder_file->name[sizeof(psu_head.name)] = 0;

	*in_fd = fd;
	return 0;
}

int psu_restore_read_file_entry(int psu_fd, FILEINFO *file, int *psu_pad_size)
{
	psu_header psu_head;
	mcT_header *mc_head = (mcT_header *)&file->stats;

	if (genRead(psu_fd, (void *)&psu_head, sizeof(psu_head)) != PSU_HEADER_SIZE)
		return -1;
	if ((psu_head.size == 0) && (psu_head.attr & sceMcFileAttrSubdir))
		return 1;  //Dummy/Pseudo folder entry
	if (psu_head.attr & sceMcFileAttrSubdir)
		return -1;  //Unexpected subfolder in PSU payload

	if (psu_head.size & 0x3FF)
		*psu_pad_size = 0x400 - (psu_head.size & 0x3FF);
	else
		*psu_pad_size = 0;

	mc_head->attr = psu_head.attr;
	mc_head->size = psu_head.size;
	mc_head->cTime = psu_head.cTime;
	mc_head->mTime = psu_head.mTime;
	memcpy(mc_head->name, psu_head.name, sizeof(psu_head.name));
	mc_head->unknown_1_u16 = psu_head.unknown_1_u16;
	mc_head->unknown_2_u64 = psu_head.unknown_2_u64;

	memcpy(file->name, psu_head.name, sizeof(psu_head.name));
	file->name[sizeof(psu_head.name)] = 0;

	return 0;
}

int psu_restore_apply_entry_stats_to_mc(const char *out_dir, const sceMcTblGetDir *stats, int set_mode)
{
	char path[MAX_PATH];
	int dummy;

	if (strncmp(out_dir, "mc", 2))
		return -1;
	if (snprintf(path, sizeof(path), "%s%.32s", out_dir, (const char *)stats->EntryName) >= (int)sizeof(path))
		return -1;

	ensureMemoryCardPortAccessible(path[2] - '0');
	mcGetInfo(path[2] - '0', 0, &dummy, &dummy, &dummy);
	mcSync(0, NULL, &dummy);
	mcSetFileInfo(path[2] - '0', 0, &path[4], (sceMcTblGetDir *)stats, set_mode);
	mcSync(0, NULL, &dummy);
	return 0;
}
