#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

/*
 * FileSystem.h - FTP server filesystem declarations
 *
 * Copyright (C) 2004 Jesper Svennevid
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

/* PS2BUILD's legacy-hook iomanx package exposes 32 sparse device slots. */
#define FS_IOMANX_DEVICES 32
#define FS_PATH_MAX 512
#define FS_PFS_UNITS 4

#ifdef LINUX
#include <dirent.h>
#include <stdint.h>
typedef uint64_t u64;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
#else

#include <iomanX.h>

typedef enum {
	FS_INVALID,
	FS_DEVLIST,
	FS_IODEVICE,  // ioman or iomanX
} FSType;

#endif

typedef enum {
	FM_READ,
	FM_WRITE,
} FileMode;

typedef struct FSContext
{
	char m_Path[FS_PATH_MAX];

#ifndef LINUX
	FSType m_eType;
	iop_file_t m_kFile;
	int m_iOpen;  // m_kFile holds a file/dir that was really opened, so it may be closed
#else
	int m_iFile;
	DIR *m_pDir;
	char m_List[FS_PATH_MAX];
#endif
} FSContext;

/* A pending rename belongs to one FTP connection, including its source device. */
typedef struct FSRename
{
	char path[FS_PATH_MAX];
#ifndef LINUX
	iop_device_t *device;
	int unit;
	unsigned int mount_generation;
#endif
} FSRename;

typedef enum {
	FT_FILE,
	FT_DIRECTORY,
	FT_LINK,
} FileType;

typedef struct FSFileInfo
{
	struct
	{
		int m_iYear;
		int m_iMonth;
		int m_iDay;
		int m_iHour;
		int m_iMinute;
		int m_iSecond;
	} m_TS;  // timestamp

	FileType m_eType;
	int m_iProtection;  // 3 bits protection per section: UrwxGrwxOrwx

	u64 m_iSize;
	char m_Name[256];
	int m_iDaysBetween;
} FSFileInfo;

typedef struct
{
	u8 unused;
	u8 sec;
	u8 min;
	u8 hour;
	u8 day;
	u8 month;
	u16 year;
} ps2time;

//! Initialize context for use
void FileSystem_Create(FSContext *pContext);

//! Cleanup context
void FileSystem_Destroy(FSContext *pContext);

//! Open file for readin or writing
int FileSystem_OpenFile(FSContext *pContext, const char *pFile, FileMode eMode, s64 iContinue);

//! Open directory for listing
int FileSystem_OpenDir(FSContext *pContext, const char *pDir);

//! Read data from open file
int FileSystem_ReadFile(FSContext *pContext, char *pBuffer, int iSize);

//! Write data to open file
int FileSystem_WriteFile(FSContext *pContext, const char *pBuffer, int iSize);

//! Read directory: 1 = entry, 0 = end, negative = error.
int FileSystem_ReadDir(FSContext *pContext, FSFileInfo *pInfo);

//! Close and clear ownership; return any driver close error.
int FileSystem_Close(FSContext *pContext);

//! Normalize an absolute/CWD-relative virtual path into an FS_PATH_MAX buffer.
int FileSystem_BuildPath(char *pResult, const char *pOriginal, const char *pAdd);

//! Change directory
int FileSystem_ChangeDir(FSContext *pContext, const char *pPath);

//! Delete file
int FileSystem_DeleteFile(FSContext *pContext, const char *pFile);

//! Create new directory
int FileSystem_CreateDir(FSContext *pContext, const char *pDir);

//! Get file-size
s64 FileSystem_GetFileSize(FSContext *pContext, const char *pFile);

//! Delete directory
int FileSystem_DeleteDir(FSContext *pContext, const char *pDir);

//! Rename file or directory
int FileSystem_PrepareRename(FSContext *pContext, const char *name, FSRename *rename);
int FileSystem_Rename(FSContext *pContext, const FSRename *rename, const char *newName);

#ifndef LINUX
//! Determine device needed for accessing device & fill context with info
const char *FileSystem_ClassifyPath(FSContext *pContext, const char *pPath);

//! Find an exact, supported device name through the exported IOMANX table.
iop_device_t *FileSystem_ScanDevice(const char *name);

//! Mount device
int FileSystem_MountDevice(FSContext *pContext, const char *mount_point, const char *mount_file);

//! Unmount device
int FileSystem_UnmountDevice(FSContext *pContext, const char *mount_point);

//! Sync device
int FileSystem_SyncDevice(FSContext *pContext, const char *devname);

//! Get current ps2 system time
int getPs2Time(ps2time *tm);

//! Calculate the difference in days between ps2 system time and file or directory time
int getDaysBetween(int month1, int day1, int year1, int month2, int day2, int year2);
#endif

#endif
