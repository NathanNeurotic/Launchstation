/* Host boundary for the real IOP FTP units. ABI types come from PS2BUILD headers. */
#ifndef FTP_TEST_PLATFORM_H
#define FTP_TEST_PLATFORM_H
#define IOP_IRX_IMPORTS_H
#include "../pccf-stubs/iomanX.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
typedef iomanX_iop_file_t iop_file_t;
typedef iomanX_iop_device_t iop_device_t;
typedef iomanX_iop_device_ops_t iop_device_ops_t;
typedef int32_t s32;
#undef O_RDONLY
#undef O_WRONLY
#undef O_CREAT
#undef O_TRUNC
#define O_RDONLY FIO_O_RDONLY
#define O_WRONLY FIO_O_WRONLY
#define O_CREAT FIO_O_CREAT
#define O_TRUNC FIO_O_TRUNC
#define O_DIROPEN FIO_O_DIROPEN
typedef struct {
	unsigned char stat, second, minute, hour, pad, day, month, year;
} sceCdCLOCK;
int sceCdReadClock(sceCdCLOCK *clock);
int DelayThread(unsigned int delay);
/* Match the IOP ps2ip.h lwip_send signature, including its mutable buffer. */
int ftp_test_send(int socket, void *data, int length, unsigned int flags);
int ftp_test_recv(int socket, void *data, int length, int flags);
int ftp_test_disconnect(int socket);
int ftp_test_select(int count, fd_set *read, fd_set *write, fd_set *except, struct timeval *timeout);
int ftp_test_socket(int family, int type, int protocol);
int ftp_test_connect(int socket, const struct sockaddr *address, socklen_t length);
int ftp_test_accept(int socket, struct sockaddr *address, socklen_t *length);
int ftp_test_bind(int socket, const struct sockaddr *address, socklen_t length);
int ftp_test_listen(int socket, int backlog);
int ftp_test_getsockname(int socket, struct sockaddr *address, socklen_t *length);
int ftp_test_getsockopt(int socket, int level, int opt, void *value, socklen_t *length);
int ftp_test_setsockopt(int socket, int level, int opt, const void *value, socklen_t length);
#define send ftp_test_send
#define recv ftp_test_recv
#define disconnect ftp_test_disconnect
#define select ftp_test_select
#define socket ftp_test_socket
#define connect ftp_test_connect
#define accept ftp_test_accept
#define bind ftp_test_bind
#define listen ftp_test_listen
#define getsockname ftp_test_getsockname
#define getsockopt ftp_test_getsockopt
#define setsockopt ftp_test_setsockopt
#endif
