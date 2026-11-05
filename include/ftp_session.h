#ifndef LS_FTP_SESSION_H
#define LS_FTP_SESSION_H
#include "ftp_protocol.h"
#ifdef ETH
int ftpSessionQuery(LsFtpStatus *status);
int ftpSessionShutdown(void);
int ftpSessionStopped(void);
/* Give up on an unanswered control RPC so the IOP may be reset over it.
 * Only after the user confirmed that open FTP files may be left incomplete. */
void ftpSessionAbandon(void);
#endif
#endif
