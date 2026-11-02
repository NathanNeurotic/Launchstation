/*
 * FileSystem.c - FTP server filesystem layer
 *
 * Copyright (C) 2004 Jesper Svennevid
 *
 * Device scan is based on the one from ps2netfs.
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "FileSystem.h"
#include <errno.h>

#ifndef LINUX
#include "irx_imports.h"
#define assert(x)
#define isnum(c) ((c) >= '0' && (c) <= '9')
#else
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#endif

// present in FtpClient.c
extern char *itoa(char *in, int val);

#ifndef LINUX
/* Mounting a different partition invalidates a previously accepted PFS rename. */
static unsigned int pfs_mount_generation[FS_PFS_UNITS];

static const char *FileSystem_DeviceName(const iop_device_t *device)
{
	const char *name;
	if (!device || !device->name || !(device->type & (IOP_DT_FS | IOP_DT_BLOCK)))
		return NULL;
	name = device->name;
	/* Non-extended USBMASS is USB-only. BDM massN can also refer to ATA. */
	if (!strcmp(name, "mass") && !(device->type & IOP_DT_FSEXT))
		return "usb";
	if (!strcmp(name, "usb") || !strcmp(name, "mc") || !strcmp(name, "mmce") || !strcmp(name, "ata") ||
		!strcmp(name, "mx4sio") || !strcmp(name, "ilink") || !strcmp(name, "hdd") || !strcmp(name, "pfs"))
		return name;
	return NULL;
}
#endif

#define DEVICE_UNITS 10

// buffer used for concating filenames internally
static char buffer[FS_PATH_MAX];

void FileSystem_Create(FSContext *pContext)
{
	strcpy(pContext->m_Path, "/");

#ifndef LINUX
	pContext->m_eType = FS_INVALID;
	memset(&(pContext->m_kFile), 0, sizeof(pContext->m_kFile));
	pContext->m_iOpen = 0;
#else
	pContext->m_iFile = -1;
	pContext->m_pDir = NULL;
#endif
}

void FileSystem_Destroy(FSContext *pContext)
{
	FileSystem_Close(pContext);
}

int FileSystem_OpenFile(FSContext *pContext, const char *pFile, FileMode eMode, s64 iContinue)
{
	int flags;
#ifndef LINUX
	int result;
#endif
	FileSystem_Close(pContext);
	if (FileSystem_BuildPath(buffer, pContext->m_Path, pFile) < 0)
		return -ENAMETOOLONG;
	if (eMode != FM_READ && eMode != FM_WRITE)
		return -EINVAL;
	flags = eMode == FM_READ ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
	/* APPE creates without truncating; REST must open the existing file. */
	if (iContinue && eMode == FM_WRITE)
		flags = O_WRONLY | (iContinue < 0 ? O_CREAT : 0);
#ifdef LINUX
	char host[FS_PATH_MAX + 1];
	snprintf(host, sizeof(host), ".%s", buffer);
	pContext->m_iFile = open(host, flags, S_IRWXU | S_IRGRP);
	if (pContext->m_iFile < 0)
		return -errno;
	if (iContinue &&
		lseek(pContext->m_iFile, iContinue < 0 ? 0 : iContinue, iContinue < 0 ? SEEK_END : SEEK_SET) < 0) {
		FileSystem_Close(pContext);
		return -EIO;
	}
	return 0;
#else
	pFile = FileSystem_ClassifyPath(pContext, buffer);
	if (!pFile || pContext->m_eType != FS_IODEVICE)
		return -ENODEV;
	pContext->m_kFile.mode = flags;
	result = pContext->m_kFile.device->ops->open(&pContext->m_kFile, pFile, flags,
												 !strcmp(pContext->m_kFile.device->name, "pfs") ? 0777 : 0);
	if (result < 0) {
		FileSystem_Close(pContext);
		return result;
	}
	pContext->m_iOpen = 1;
	if (iContinue) {
		s64 position;
		int whence = iContinue < 0 ? SEEK_END : SEEK_SET;
		s64 offset = iContinue < 0 ? 0 : iContinue;
		iop_device_t *device = pContext->m_kFile.device;
		if ((device->type & IOP_DT_FSEXT) && device->ops->lseek64)
			position = device->ops->lseek64(&pContext->m_kFile, offset, whence);
		else if (offset <= 0x7fffffff)
			position = device->ops->lseek(&pContext->m_kFile, (int)offset, whence);
		else
			position = -EOVERFLOW;
		if (position < 0 || (iContinue > 0 && position != iContinue)) {
			FileSystem_Close(pContext);
			return position < 0 ? (int)position : -EIO;
		}
	}
	return 0;
#endif
}


int FileSystem_OpenDir(FSContext *pContext, const char *pDir)
{
	FileSystem_Close(pContext);
	if (FileSystem_BuildPath(buffer, pContext->m_Path, pDir) < 0)
		return -ENAMETOOLONG;

#ifdef LINUX

	if (strlen(buffer) + 2 > sizeof(pContext->m_List)) return -ENAMETOOLONG;
	strcpy(pContext->m_List, ".");
	strcat(pContext->m_List, buffer);

	// unsafe, sure
	pContext->m_pDir = opendir(pContext->m_List);
	return pContext->m_pDir ? 0 : -1;

#else

	if (NULL == (pDir = FileSystem_ClassifyPath(pContext, buffer)))
		return -1;

	switch (pContext->m_eType) {
		// directory listing from I/O device
		case FS_IODEVICE: {
			if (!pContext->m_kFile.device)
				break;

			// attempt to open device directory

			pContext->m_kFile.mode = O_DIROPEN;
			if (pContext->m_kFile.device->ops->dopen(&(pContext->m_kFile), pDir) >= 0) {
				pContext->m_iOpen = 1;
				return 0;
			}
		} break;

		// either device-list or unit-list (depending on if pContext->m_kFile.device was set)
		case FS_DEVLIST: {
			pContext->m_kFile.unit = 0;  // we use this for enumeration
			return 0;
		} break;
		default:
			break;
	}

	pContext->m_eType = FS_INVALID;
	return -1;
#endif
}

int FileSystem_ReadFile(FSContext *pContext, char *pBuffer, int iSize)
{
#ifdef LINUX
	if (pContext->m_iFile < 0)
		return -1;

	return read(pContext->m_iFile, pBuffer, iSize);
#else

	switch (pContext->m_eType) {
	case FS_IODEVICE: {
		if (!pContext->m_kFile.device || !(pContext->m_kFile.mode & O_RDONLY))
			break;

		// read data from I/O device

		{
			int iRead = pContext->m_kFile.device->ops->read(&(pContext->m_kFile), pBuffer, iSize);

			// mmce answers a read that starts at end of file with an error instead of 0 bytes
			if ((iRead < 0) && !strcmp(pContext->m_kFile.device->name, "mmce")) {
				iop_device_t *device = pContext->m_kFile.device;
				s64 position, end;
				int wide = (device->type & IOP_DT_FSEXT) && device->ops->lseek64;
				position = wide ? device->ops->lseek64(&pContext->m_kFile, 0, SEEK_CUR)
								: device->ops->lseek(&pContext->m_kFile, 0, SEEK_CUR);
				end = wide ? device->ops->lseek64(&pContext->m_kFile, 0, SEEK_END)
						   : device->ops->lseek(&pContext->m_kFile, 0, SEEK_END);
				if (position >= 0 && position == end)
					return 0;
				if (position >= 0) {
					if (wide)
						device->ops->lseek64(&pContext->m_kFile, position, SEEK_SET);
					else
						device->ops->lseek(&pContext->m_kFile, (int)position, SEEK_SET);
				}
			}
			return iRead;
		}
	} break;

	default:
		break;
	}

	return -1;
#endif
}

int FileSystem_WriteFile(FSContext *pContext, const char *pBuffer, int iSize)
{
#ifdef LINUX
	if (pContext->m_iFile < 0)
		return -1;

	return write(pContext->m_iFile, pBuffer, iSize);
#else

	switch (pContext->m_eType) {
		case FS_IODEVICE: {
			if (!pContext->m_kFile.device || !(pContext->m_kFile.mode & O_WRONLY))
				break;

			// write data to device

			return pContext->m_kFile.device->ops->write(&(pContext->m_kFile), (char *)pBuffer, iSize);
		} break;

		default:
			break;
	}

	return -1;
#endif
}

int FileSystem_ReadDir(FSContext *pContext, FSFileInfo *pInfo)
{
#ifdef LINUX
	struct dirent *ent;
	struct stat stat;
	struct tm *time;
	char path[FS_PATH_MAX];
	if (!pContext->m_pDir)
		return -EBADF;
	do {
		errno = 0;
		ent = readdir(pContext->m_pDir);
		if (!ent)
			return errno ? -errno : 0;
	} while (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."));
	if (snprintf(path, sizeof(path), "%s/%s", pContext->m_List, ent->d_name) >= (int)sizeof(path))
		return -ENAMETOOLONG;
	if (lstat(path, &stat) < 0)
		return -errno;
	memset(pInfo, 0, sizeof(*pInfo));
	strncpy(pInfo->m_Name, ent->d_name, sizeof(pInfo->m_Name) - 1);
	pInfo->m_eType = S_ISDIR(stat.st_mode) ? FT_DIRECTORY : S_ISLNK(stat.st_mode) ? FT_LINK : FT_FILE;
	pInfo->m_iSize = stat.st_size;
	pInfo->m_iProtection = stat.st_mode & 0777;
	time = localtime(&stat.st_mtime);
	pInfo->m_TS.m_iYear = time->tm_year + 1900;
	pInfo->m_TS.m_iMonth = time->tm_mon + 1;
	pInfo->m_TS.m_iDay = time->tm_mday;
	pInfo->m_TS.m_iHour = time->tm_hour;
	pInfo->m_TS.m_iMinute = time->tm_min;
	return 1;
#else
	ps2time time;
	iop_device_t *device = pContext->m_kFile.device;
	getPs2Time(&time);
	memset(pInfo, 0, sizeof(*pInfo));
	pInfo->m_TS.m_iYear = time.year;
	pInfo->m_TS.m_iMonth = time.month;
	pInfo->m_TS.m_iDay = time.day;
	pInfo->m_TS.m_iHour = time.hour;
	pInfo->m_TS.m_iMinute = time.min;
	pInfo->m_TS.m_iSecond = time.sec;
	if (pContext->m_eType == FS_DEVLIST) {
		pInfo->m_iProtection = 0777;
		if (!device) {
			iop_device_t **devices = iomanX_GetDeviceList();
			if (!devices)
				return -ENODEV;
			while (pContext->m_kFile.unit < FS_IOMANX_DEVICES) {
				int index = pContext->m_kFile.unit++, previous;
				const char *name = FileSystem_DeviceName(devices[index]);
				if (!name || FileSystem_ScanDevice(name) != devices[index])
					continue;
				for (previous = 0; previous < index; previous++)
					if (devices[previous] == devices[index])
						break;
				if (previous != index)
					continue;
				strcpy(pInfo->m_Name, name);
				pInfo->m_eType = FT_DIRECTORY;
				return 1;
			}
			return 0;
		} else {
			int limit = DEVICE_UNITS;
			if (!strcmp(device->name, "mc") || !strcmp(device->name, "mmce"))
				limit = 2;
			else if (!strcmp(device->name, "pfs"))
				limit = FS_PFS_UNITS;
			else if (!strcmp(device->name, "hdd"))
				limit = 1;
			while (pContext->m_kFile.unit < limit) {
				iop_file_t probe = pContext->m_kFile;
				int unit = pContext->m_kFile.unit++;
				if (strcmp(device->name, "hdd")) {
					probe.privdata = NULL;
					probe.mode = O_DIROPEN;
					if (device->ops->dopen(&probe, "/") < 0)
						continue;
					int closed = device->ops->dclose(&probe);
					if (closed < 0)
						return closed;
				}
				itoa(pInfo->m_Name, unit);
				pInfo->m_eType = FT_DIRECTORY;
				return 1;
			}
			return 0;
		}
	}
	if (pContext->m_eType != FS_IODEVICE || !device || !pContext->m_iOpen ||
		!(pContext->m_kFile.mode & O_DIROPEN))
		return -EBADF;
	for (;;) {
		iox_stat_t stat;
		char name[256];
		int result, legacy = !(device->type & IOP_DT_FSEXT);
		memset(&stat, 0, sizeof(stat));
		if (legacy) {
			io_dirent_t entry;
			memset(&entry, 0, sizeof(entry));
			result = device->ops->dread(&pContext->m_kFile, (iox_dirent_t *)&entry);
			entry.name[sizeof(entry.name) - 1] = '\0';
			strcpy(name, entry.name);
			stat.mode = entry.stat.mode;
			stat.size = entry.stat.size;
			stat.hisize = entry.stat.hisize;
			memcpy(stat.mtime, entry.stat.mtime, sizeof(stat.mtime));
		} else {
			iox_dirent_t entry;
			memset(&entry, 0, sizeof(entry));
			result = device->ops->dread(&pContext->m_kFile, &entry);
			entry.name[sizeof(entry.name) - 1] = '\0';
			strcpy(name, entry.name);
			stat = entry.stat;
		}
		if (result <= 0)
			return result;
		if (!name[0] || !strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		if (!strcmp(device->name, "hdd") &&
			(stat.attr || stat.mode != 0x0100 ||
			 (!strncmp(name, "PP.", 3) && strlen(name) >= 4 && !strcmp(name + strlen(name) - 4, ".PCB"))))
			continue;
		strcpy(pInfo->m_Name, name);
		pInfo->m_eType = (legacy ? FIO_SO_ISDIR(stat.mode) || stat.mode == 0x002f
								 : FIO_S_ISDIR(stat.mode) || stat.mode == 0x0100)
							 ? FT_DIRECTORY
						 : (legacy ? FIO_SO_ISLNK(stat.mode) : FIO_S_ISLNK(stat.mode)) ? FT_LINK
																					   : FT_FILE;
		pInfo->m_iSize = pInfo->m_eType == FT_DIRECTORY ? 0 : ((u64)stat.hisize << 32) | (u32)stat.size;
		if (stat.mtime[6] || stat.mtime[7]) {
			pInfo->m_TS.m_iYear = (stat.mtime[7] << 8) | stat.mtime[6];
			pInfo->m_TS.m_iMonth = stat.mtime[5];
			pInfo->m_TS.m_iDay = stat.mtime[4];
			pInfo->m_TS.m_iHour = stat.mtime[3];
			pInfo->m_TS.m_iMinute = stat.mtime[2];
			pInfo->m_TS.m_iSecond = stat.mtime[1];
		}
		pInfo->m_iDaysBetween = getDaysBetween(pInfo->m_TS.m_iMonth, pInfo->m_TS.m_iDay, pInfo->m_TS.m_iYear,
											   time.month, time.day, time.year);
		if (legacy) {
			int permissions = stat.mode & 7;
			pInfo->m_iProtection = permissions | (permissions << 3) | (permissions << 6);
			if (stat.mode == 0x0010 || stat.mode == 0x0020)
				pInfo->m_iProtection = 0777;
		} else
			pInfo->m_iProtection = stat.mode & 0777;
		return 1;
	}
#endif
}



int FileSystem_DeleteFile(FSContext *pContext, const char *pFile)
{
	if (FileSystem_BuildPath(buffer, pContext->m_Path, pFile) < 0)
		return -ENAMETOOLONG;

#ifdef LINUX
	return -1;
#else

	if (NULL == (pFile = FileSystem_ClassifyPath(pContext, buffer)))
		return -1;

	switch (pContext->m_eType) {
		case FS_IODEVICE: {
			if (!pContext->m_kFile.device)
				break;

			return pContext->m_kFile.device->ops->remove((&pContext->m_kFile), pFile);
		} break;

		default:
			return -1;
	}

	return -1;
#endif
}

int FileSystem_CreateDir(FSContext *pContext, const char *pDir)
{
	#ifndef LINUX
	int fileMode = 0;
#endif
	if (FileSystem_BuildPath(buffer, pContext->m_Path, pDir) < 0)
		return -ENAMETOOLONG;

#ifdef LINUX
	return -1;
#else
	if (NULL == (pDir = FileSystem_ClassifyPath(pContext, buffer)))
		return -1;

	switch (pContext->m_eType) {
		case FS_IODEVICE: {
			if (!pContext->m_kFile.device)
				break;

			// check if hdd, then set permissions to 511
			if (!strcmp(pContext->m_kFile.device->name, "pfs"))
				fileMode = 511;

			return pContext->m_kFile.device->ops->mkdir(&(pContext->m_kFile), pDir, fileMode);
		} break;

		default:
			return -1;
	}

	return -1;
#endif
}

int FileSystem_DeleteDir(FSContext *pContext, const char *pDir)
{
	if (FileSystem_BuildPath(buffer, pContext->m_Path, pDir) < 0)
		return -ENAMETOOLONG;

#ifdef LINUX
	return -1;
#else
	if (NULL == (pDir = FileSystem_ClassifyPath(pContext, buffer)))
		return -1;

	switch (pContext->m_eType) {
		case FS_IODEVICE: {
			if (!pContext->m_kFile.device)
				break;
			return pContext->m_kFile.device->ops->rmdir(&(pContext->m_kFile), pDir);
		} break;

		default:
			return -1;
	}

	return -1;
#endif
}

int FileSystem_PrepareRename(FSContext *pContext, const char *name, FSRename *rename)
{
	int result;
	memset(rename, 0, sizeof(*rename));
	result = FileSystem_BuildPath(rename->path, pContext->m_Path, name);
	if (result < 0)
		return result;
	result = FileSystem_OpenFile(pContext, rename->path, FM_READ, 0);
	if (result < 0)
		result = FileSystem_OpenDir(pContext, rename->path);
#ifndef LINUX
	if (result >= 0 &&
		(pContext->m_eType != FS_IODEVICE || !(pContext->m_kFile.device->type & IOP_DT_FSEXT) ||
		 !pContext->m_kFile.device->ops->rename))
		result = -ENOSYS;
	if (result >= 0) {
		rename->device = pContext->m_kFile.device;
		rename->unit = pContext->m_kFile.unit;
		if (!strcmp(rename->device->name, "pfs") && rename->unit < FS_PFS_UNITS)
			rename->mount_generation = pfs_mount_generation[rename->unit];
	}
#endif
	FileSystem_Close(pContext);
	if (result < 0)
		rename->path[0] = '\0';
	return result;
}

int FileSystem_Rename(FSContext *pContext, const FSRename *rename, const char *newName)
{
	char destination[FS_PATH_MAX];
	int result = FileSystem_BuildPath(destination, pContext->m_Path, newName);
	if (result < 0 || !rename->path[0])
		return result < 0 ? result : -EINVAL;
#ifdef LINUX
	char source[FS_PATH_MAX];
	if (FileSystem_BuildPath(source, "/", rename->path) < 0)
		return -ENAMETOOLONG;
	char old_host[FS_PATH_MAX + 1], new_host[FS_PATH_MAX + 1];
	snprintf(old_host, sizeof(old_host), ".%s", source);
	snprintf(new_host, sizeof(new_host), ".%s", destination);
	return renameat(AT_FDCWD, old_host, AT_FDCWD, new_host) < 0 ? -errno : 0;
#else
	char source[FS_PATH_MAX];
	const char *relative;
	if (!rename->device)
		return -EINVAL;
	if (!strcmp(rename->device->name, "pfs") && rename->unit < FS_PFS_UNITS &&
		rename->mount_generation != pfs_mount_generation[rename->unit])
		return -EXDEV;
	relative = FileSystem_ClassifyPath(pContext, rename->path);
	if (!relative || pContext->m_eType != FS_IODEVICE || pContext->m_kFile.device != rename->device ||
		pContext->m_kFile.unit != rename->unit)
		return -EXDEV;
	strcpy(source, relative);
	relative = FileSystem_ClassifyPath(pContext, destination);
	if (!relative || pContext->m_eType != FS_IODEVICE)
		return -ENODEV;
	if (pContext->m_kFile.device != rename->device || pContext->m_kFile.unit != rename->unit)
		return -EXDEV;
	if (!strcmp(source, "/") || !strcmp(relative, "/"))
		return -EINVAL;
	if (!(rename->device->type & IOP_DT_FSEXT) || !rename->device->ops->rename)
		return -ENOSYS;
	return rename->device->ops->rename(&pContext->m_kFile, source, relative);
#endif
}



int FileSystem_Close(FSContext *pContext)
{
	int result = 0;
#ifdef LINUX
	if (pContext->m_iFile >= 0) {
		if (close(pContext->m_iFile) < 0)
			result = -errno;
		pContext->m_iFile = -1;
	}
	if (pContext->m_pDir) {
		if (closedir(pContext->m_pDir) < 0)
			result = -errno;
		pContext->m_pDir = NULL;
	}
#else
	if (pContext->m_eType == FS_IODEVICE && pContext->m_kFile.device && pContext->m_iOpen) {
		if (pContext->m_kFile.mode & O_DIROPEN)
			result = pContext->m_kFile.device->ops->dclose(&pContext->m_kFile);
		else
			result = pContext->m_kFile.device->ops->close(&pContext->m_kFile);
	}
	pContext->m_eType = FS_INVALID;
	memset(&pContext->m_kFile, 0, sizeof(pContext->m_kFile));
	pContext->m_iOpen = 0;
#endif
	return result < 0 ? result : 0;
}


int FileSystem_BuildPath(char *pResult, const char *pOriginal, const char *pAdd)
{
	char combined[FS_PATH_MAX];
	const char *cursor;
	size_t used = 1;
	int count;
	if (!pAdd || !pOriginal)
		return -EINVAL;
	if (pAdd[0] == '/') {
		count = strlen(pAdd);
		if (count >= (int)sizeof(combined))
			return -ENAMETOOLONG;
		memcpy(combined, pAdd, count + 1);
	} else {
		size_t original = strlen(pOriginal), added = strlen(pAdd);
		if (original + added + 2 > sizeof(combined))
			return -ENAMETOOLONG;
		memcpy(combined, pOriginal, original);
		combined[original] = '/';
		memcpy(combined + original + 1, pAdd, added + 1);
	}
	pResult[0] = '/';
	pResult[1] = '\0';
	cursor = combined;
	while (*cursor) {
		const char *component;
		size_t length;
		while (*cursor == '/')
			cursor++;
		component = cursor;
		while (*cursor && *cursor != '/')
			cursor++;
		length = cursor - component;
		if (!length || (length == 1 && component[0] == '.'))
			continue;
		if (length == 2 && component[0] == '.' && component[1] == '.') {
			while (used > 1 && pResult[used - 1] != '/')
				used--;
			if (used > 1)
				used--;
			pResult[used] = '\0';
			continue;
		}
		if (used + (used > 1) + length >= FS_PATH_MAX)
			return -ENAMETOOLONG;
		if (used > 1)
			pResult[used++] = '/';
		memcpy(pResult + used, component, length);
		used += length;
		pResult[used] = '\0';
	}

	return 0;
}


int FileSystem_ChangeDir(FSContext *pContext, const char *pPath)
{
	FSContext candidate;
	char path[FS_PATH_MAX];
	int result;
	FileSystem_Create(&candidate);
	result = FileSystem_BuildPath(path, pContext->m_Path, pPath);
	if (result < 0)
		return result;
#ifndef LINUX
	/* Auto-mount APA partitions at the reserved FTP PFS0 mount point. */
	if (!strncmp(path, "/hdd/0/", 7)) {
		const char *end = strchr(path + 7, '/');
		char mount[40]; // hdd: plus a 32-byte APA partition identifier
		size_t length = end ? (size_t)(end - (path + 7)) : strlen(path + 7);
		if (!length || length + 6 >= sizeof(mount))
			return -EINVAL;
		memcpy(mount, "hdd:", 4);
		memcpy(mount + 4, path + 7, length);
		mount[4 + length] = '\0';
		FileSystem_UnmountDevice(&candidate, "/pfs/0/");
		if (FileSystem_MountDevice(&candidate, "/pfs/0/", mount) < 0)
			return -EIO;
		if (FileSystem_BuildPath(path, "/pfs/0/", end ? end + 1 : "") < 0)
			return -ENAMETOOLONG;
	}
#endif
	result = FileSystem_OpenDir(&candidate, path);
	FileSystem_Close(&candidate);
	if (result < 0)
		return result;
	if (strlen(path) + 2 > sizeof(pContext->m_Path))
		return -ENAMETOOLONG;
	FileSystem_Close(pContext);
	strcpy(pContext->m_Path, path);
	if (strcmp(path, "/"))
		strcat(pContext->m_Path, "/");
	return 0;
}


s64 FileSystem_GetFileSize(FSContext *pContext, const char *pFile)
{
	FileSystem_Close(pContext);
	if (FileSystem_BuildPath(buffer, pContext->m_Path, pFile) < 0)
		return -ENAMETOOLONG;
#ifdef LINUX
	struct stat info;
	char host[FS_PATH_MAX + 1];
	snprintf(host, sizeof(host), ".%s", buffer);
	if (stat(host, &info) < 0 || !S_ISREG(info.st_mode))
		return -ENOENT;
	return info.st_size;
#else
	iox_stat_t stat;
	s64 size = -ENOENT;
	pFile = FileSystem_ClassifyPath(pContext, buffer);
	memset(&stat, 0, sizeof(stat));
	if (pFile && pContext->m_eType == FS_IODEVICE) {
		int result = pContext->m_kFile.device->ops->getstat(&pContext->m_kFile, pFile, &stat);
		int legacy = !(pContext->m_kFile.device->type & IOP_DT_FSEXT);
		if (result >= 0 && (legacy ? FIO_SO_ISREG(stat.mode) : FIO_S_ISREG(stat.mode)))
			size = ((u64)stat.hisize << 32) | (u32)stat.size;
	}
	FileSystem_Close(pContext);
	return size;
#endif
}


#ifndef LINUX
const char *FileSystem_ClassifyPath(FSContext *pContext, const char *pPath)
{
	char path[FS_PATH_MAX], name[32];
	const char *cursor, *end;
	size_t length;
	int unit = 0;
	FileSystem_Close(pContext);
	if (!pPath || pPath[0] != '/' || FileSystem_BuildPath(path, "/", pPath) < 0)
		return NULL;
	pContext->m_eType = FS_DEVLIST;
	if (!strcmp(path, "/"))
		return "/";
	cursor = path + 1;
	end = strchr(cursor, '/');
	length = end ? (size_t)(end - cursor) : strlen(cursor);
	if (length >= sizeof(name))
		return NULL;
	memcpy(name, cursor, length);
	name[length] = '\0';
	pContext->m_kFile.device = FileSystem_ScanDevice(name);
	if (!pContext->m_kFile.device)
		return NULL;
	if (!end)
		return "/";
	cursor = end + 1;
	if (!isnum(*cursor))
		return NULL;
	while (isnum(*cursor)) {
		int digit = *cursor++ - '0';
		if (unit > (0x7fffffff - digit) / 10)
			return NULL;
		unit = unit * 10 + digit;
	}
	if (*cursor && *cursor != '/')
		return NULL;
	pContext->m_kFile.unit = unit;
	pContext->m_eType = FS_IODEVICE;
	strcpy(buffer, *cursor ? cursor : "/");
	return buffer;
}



iop_device_t *FileSystem_ScanDevice(const char *name)
{
	iop_device_t **devices = iomanX_GetDeviceList();
	iop_device_t *legacy_usb = NULL;
	int index;
	if (!devices)
		return NULL;
	/* mass is left out of the root listing (it repeats usb/ata), but existing
	 * /mass/N/ bookmarks and the Cloud Relay's massN: mapping still open it. */
	if (!strcmp(name, "mass")) {
		for (index = 0; index < FS_IOMANX_DEVICES; index++)
			if (devices[index] && devices[index]->name && !strcmp(devices[index]->name, "mass") &&
				(devices[index]->type & (IOP_DT_FS | IOP_DT_BLOCK)))
				return devices[index];
		return NULL;
	}
	for (index = 0; index < FS_IOMANX_DEVICES; index++) {
		const char *visible = FileSystem_DeviceName(devices[index]);
		if (!visible || strcmp(visible, name))
			continue;
		if (!strcmp(devices[index]->name, name))
			return devices[index];
		legacy_usb = devices[index];
	}
	return legacy_usb;
}

//! Mount device
int FileSystem_MountDevice(FSContext *pContext, const char *mount_point, const char *mount_file)
{
	if (!mount_point || !mount_file)
		return -1;

	if (NULL == (mount_point = FileSystem_ClassifyPath(pContext, mount_point)))
		return -1;

	if ((FS_IODEVICE != pContext->m_eType) || (NULL == pContext->m_kFile.device))
		return -1;

	if ((pContext->m_kFile.device->type & 0xf0000000) != IOP_DT_FSEXT) {
#ifdef DEBUG
		printf("ps2ftpd: device fs is not extended.\n");
#endif
		pContext->m_eType = FS_INVALID;
		return -1;
	}

	if (pContext->m_kFile.device->ops->mount(&(pContext->m_kFile), mount_point, mount_file, 0, NULL, 0) < 0) {
#ifdef DEBUG
		printf("ps2ftpd: device-mount failed.\n");
#endif
		pContext->m_eType = FS_INVALID;
		return -1;
	}

	DelayThread(100);

	if (!strcmp(pContext->m_kFile.device->name, "pfs") && pContext->m_kFile.unit < FS_PFS_UNITS)
		pfs_mount_generation[pContext->m_kFile.unit]++;
	pContext->m_eType = FS_INVALID;
	return 0;
}

//! Unmount device
int FileSystem_UnmountDevice(FSContext *pContext, const char *mount_point)
{
	if (!mount_point)
		return -1;

	if (NULL == (mount_point = FileSystem_ClassifyPath(pContext, mount_point)))
		return -1;

	if ((FS_IODEVICE != pContext->m_eType) || (NULL == pContext->m_kFile.device))
		return -1;

	if ((pContext->m_kFile.device->type & 0xf0000000) != IOP_DT_FSEXT) {
#ifdef DEBUG
		printf("ps2ftpd: device fs is not extended.\n");
#endif
		pContext->m_eType = FS_INVALID;
		return -1;
	}

	if (pContext->m_kFile.device->ops->umount(&(pContext->m_kFile), mount_point) < 0) {
#ifdef DEBUG
		printf("ps2ftpd: device-unmount failed.\n");
#endif
		pContext->m_eType = FS_INVALID;
		return -1;
	}

	if (!strcmp(pContext->m_kFile.device->name, "pfs") && pContext->m_kFile.unit < FS_PFS_UNITS)
		pfs_mount_generation[pContext->m_kFile.unit]++;
	pContext->m_eType = FS_INVALID;
	return 0;
}

//! Synchronize device
int FileSystem_SyncDevice(FSContext *pContext, const char *devname)
{
	if (!devname)
		return -1;

	if (NULL == (devname = FileSystem_ClassifyPath(pContext, devname)))
		return -1;

	if ((FS_IODEVICE != pContext->m_eType) || (NULL == pContext->m_kFile.device))
		return -1;

	if ((pContext->m_kFile.device->type & 0xf0000000) != IOP_DT_FSEXT) {
#ifdef DEBUG
		printf("ps2ftpd: device fs is not extended.\n");
#endif
		pContext->m_eType = FS_INVALID;
		return -1;
	}

	if (pContext->m_kFile.device->ops->sync(&(pContext->m_kFile), devname, 0) < 0) {
#ifdef DEBUG
		printf("ps2ftpd: device-sync failed.\n");
#endif
		pContext->m_eType = FS_INVALID;
		return -1;
	}

	pContext->m_eType = FS_INVALID;
	return 0;
}

int getPs2Time(ps2time *tm)
{
	sceCdCLOCK cdtime;
	s32 tmp;
	// used if can not get time...
	ps2time timeBuf = {0, 0, 0, 0, 1, 1, 1970};

	if (sceCdReadClock(&cdtime) != 0 && cdtime.stat == 0) {
		tmp = cdtime.second >> 4;
		timeBuf.sec = (int)(((tmp << 2) + tmp) << 1) + (cdtime.second & 0x0F);
		tmp = cdtime.minute >> 4;
		timeBuf.min = (((tmp << 2) + tmp) << 1) + (cdtime.minute & 0x0F);
		tmp = cdtime.hour >> 4;
		timeBuf.hour = (((tmp << 2) + tmp) << 1) + (cdtime.hour & 0x0F);
		tmp = cdtime.day >> 4;
		timeBuf.day = (((tmp << 2) + tmp) << 1) + (cdtime.day & 0x0F);
		tmp = (cdtime.month >> 4) & 0x7F;
		timeBuf.month = (((tmp << 2) + tmp) << 1) + (cdtime.month & 0x0F);
		tmp = cdtime.year >> 4;
		timeBuf.year = (((tmp << 2) + tmp) << 1) + (cdtime.year & 0xF) + 2000;
	}
	memcpy(tm, &timeBuf, sizeof(ps2time));
	return 0;
}

int getDaysBetween(int month1, int day1, int year1, int month2, int day2, int year2)
{
	int num_days = 0;
	int reverse;
	int i;

	if ((reverse = year1 > year2) || ((year1 == year2) && ((month1 > month2) || ((month1 == month2) && (day1 > day2))))) {
		i = year1;
		year1 = year2;
		year2 = i;
		i = month1;
		month1 = month2;
		month2 = i;
		i = day1;
		day1 = day2;
		day2 = i;
	}

	// do a gross total of the span of years, then adjust
	for (i = year1; i <= year2; i++)
		num_days += (365 + ((i) % 4 == 0 && ((i) % 100 != 0 || (i) % 400 == 0)));

	// adjust first year
	for (i = 1; i < month1; i++)
		num_days -= ((i) == 2 ? 28 + ((year1) % 4 == 0 && ((year1) % 100 != 0 || (year1) % 400 == 0)) : 30 + ((i) + ((i) >= 8)) % 2);

	// adjust last year
	for (i = month2; i <= 12; i++)
		num_days -= ((i) == 2 ? 28 + ((year2) % 4 == 0 && ((year2) % 100 != 0 || (year2) % 400 == 0)) : 30 + ((i) + ((i) >= 8)) % 2);

	num_days += day2 - day1;

	return (reverse ? -num_days : num_days);
}

#endif
