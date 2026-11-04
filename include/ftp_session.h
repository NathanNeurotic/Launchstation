#ifndef LS_FTP_SESSION_H
#define LS_FTP_SESSION_H
#include "ftp_protocol.h"
#ifdef ETH
int ftpSessionQuery(LsFtpStatus *status);
int ftpSessionShutdown(void);
int ftpSessionStopped(void);
#endif
#endif
