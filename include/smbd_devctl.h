/* devctls of the "smbd:" device (iop/smbd/smbd.c), the memory card SMB server. */
#ifndef SMBD_DEVCTL_H
#define SMBD_DEVCTL_H

/* Running: bit 0 set, bit 1 listening, bits 2+ connected clients. Stopped: 0, or the negative
 * errno the server ended with (for example when port 445 could not be bound). */
#define SMBD_DEVCTL_STATUS 0x534d4201
/* Closes every file and connection; 0 once the server thread has ended (3 s at most). */
#define SMBD_DEVCTL_STOP 0x534d4202
#define SMBD_DEVCTL_START 0x534d4203

#endif
