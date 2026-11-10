/* Host stand-in: smb2man.c reads the IOP clock only for its reconnect backoff, which the
   harness drives itself (SMB2MAN_SECONDS). */
#ifndef HOST_THBASE
#define HOST_THBASE
typedef struct
{
    unsigned int lo, hi;
} iop_sys_clock_t;
#endif
