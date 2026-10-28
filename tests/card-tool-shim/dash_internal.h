/* Host stand-in for include/dash_internal.h, used by tests/dash_card_tool_host.c only. */
#ifndef DASH_INTERNAL_H
#define DASH_INTERNAL_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define MAX_PATH 1025
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_RDWR O_RDWR
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define FIO_O_EXCL O_EXCL
typedef struct { struct { unsigned int mode; } stat; char name[256]; } iox_dirent_t;
#define FIO_S_ISDIR(m) (((m) & 0x1000) != 0)
enum { MAX_NAME = 256 };
enum { DI_WARNING = 1, DI_FILE, DI_CLOSE, DI_MC, DI_PLUS };
enum { DASH_TOAST_OK = 1, DASH_TOAST_WARN, DASH_TOAST_INFO };
enum { DASH_PICK_FILE = 0, DASH_PICK_FOLDER, DASH_PICK_LAUNCH };
typedef struct { int icon; const char *label; const char *detail; int disabled; } DashMenuItem;
int genOpen(const char *path, int mode);
s64 genLseek(int fd, s64 where, int how);
int genRead(int fd, void *buf, int size);
int genWrite(int fd, void *buf, int size);
int genClose(int fd);
int genDopen(char *path);
int genDclose(int fd);
int genMkdir(const char *path, int mode);
int genRemove(char *path);
int fileXioDevctl(const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen);
int DelayThread(int usec);
int genCmpFileExt(const char *filename, const char *extension);
int fileXioDread(int fd, iox_dirent_t *ent);
int wleExists(const char *path);
void dashStrCopy(char *dst, const char *src, int dst_size);
void dashSjisTitle(char *out, size_t size, const unsigned char *title);
void dashToast(int kind, const char *msg);
int dashModalMenu(const char *title, const char *subtitle, const DashMenuItem *items, int n, int initial);
int dashPickPath(const char *title, const char *start_dir, int mode, const char *ext, char *out, int out_size);
int dashModalConfirm(const char *title, const char *body, const char *ok_label, int danger);
void dashModalMessage(int icon, const char *title, const char *body, const char *detail);
int dashModalChoice(const char *title, const char *const *options, int n, int current);
void dashLoadingBegin(const char *title);
void dashLoadingStatus(const char *status, int done, int total);
void dashLoadingEnd(void);
#endif
