/* libsmb2 build configuration for smbd.irx: what the IOP has, and what compat.c supplies. */
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define HAVE_STDIO_H 1
#define HAVE_SYS_TYPES_H 1
#define _U_ __attribute__((unused))
#include <sys/fcntl.h>
#define HAVE_ERRNO_H 1
#define NEED_RANDOM 1
#define NEED_SRANDOM 1
#define NEED_GETPID 1
#define NEED_GETLOGIN_R 1
#define NEED_WRITEV 1
#define NEED_READV 1
#define NEED_POLL 1
#define NEED_STRDUP 1
