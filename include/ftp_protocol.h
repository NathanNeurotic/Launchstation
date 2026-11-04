#ifndef LAUNCHSTATION_FTP_PROTOCOL_H
#define LAUNCHSTATION_FTP_PROTOCOL_H
/* Fixed-width EE/IOP wire layout. No pointers or SDK-specific structures. */
#define LS_FTP_RPC_ID 0x4c534654
#define LS_FTP_PROTOCOL_VERSION 1
#define LS_FTP_STATUS 1
#define LS_FTP_STOP 2
#define LS_FTP_RUNNING 1
#define LS_FTP_STOPPING 2
#define LS_FTP_STOPPED 3
typedef struct {
	unsigned int version;
	int state, error, clients, transfers;
	unsigned int bytes_low, bytes_high;
	char operation[128];
	unsigned int reserved[9]; /* Whole 64-byte cache lines for SIF DMA. */
} LsFtpStatus;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(LsFtpStatus) == 192, "FTP status wire layout must be 192 bytes");
#endif
#endif
