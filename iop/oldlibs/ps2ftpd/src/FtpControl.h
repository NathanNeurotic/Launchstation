#ifndef LS_FTP_CONTROL_H
#define LS_FTP_CONTROL_H
#include "ftp_protocol.h"
struct FtpServer;
#ifdef LINUX
static inline int FtpControl_Init(struct FtpServer *s)
{
	(void)s;
	return 0;
}
static inline int FtpControl_Stopping(void)
{
	return 0;
}
static inline void FtpControl_Operation(const char *s)
{
	(void)s;
}
static inline void FtpControl_Stopped(void) {}
static inline void FtpControl_Error(int error)
{
	(void)error;
}
#else
int FtpControl_Init(struct FtpServer *server);
int FtpControl_Stopping(void);
void FtpControl_Operation(const char *operation);
void FtpControl_Stopped(void);
void FtpControl_Error(int error);
/* Host fixture exercises the same handler without a real SIF transport. */
void *FtpControl_Exchange(int command, void *data, int size);
#endif
#endif
