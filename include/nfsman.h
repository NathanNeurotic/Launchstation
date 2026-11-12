#ifndef NFSMAN_H
#define NFSMAN_H

// devctl interface of the nfs: driver, iop/nfsman/nfsman.c (included by it and by the EE). The
// same shape as smb2man.h.

// Connect url, nfs://<IPv4>[:port]/<export>[?uid=N&gid=N], as the folder nfs:/<name>/. Returns 0,
// -EEXIST when name is already connected (nothing changes), or -NFS_CONNECT_ERR_*.
#define NFS_DEVCTL_CONNECT        0xC0DF0001
// Forget every export and close its connection. Returns 0.
#define NFS_DEVCTL_DISCONNECT_ALL 0xC0DF0002

#define NFS_MAX_NAME_LEN 32
typedef struct
{
	char name[NFS_MAX_NAME_LEN];
	char url[256];
	// Seconds from the EE's clock: the driver starts its RPC xids from it, so that a session after
	// a reboot does not reuse the open owners of the last one (the server keeps them a while).
	unsigned int seed;
} nfsConnect_in_t;

// Why a CONNECT failed (returned negative). Above every errno, so neither is taken for the other.
#define NFS_CONNECT_ERR_URL    1001 // not nfs://<IPv4>[:port]/<export>, or a name that does not fit
#define NFS_CONNECT_ERR_CONN   1002 // no TCP connection to port 2049 within 5 s
#define NFS_CONNECT_ERR_PROT   1003 // the server does not answer NFSv4.0
#define NFS_CONNECT_ERR_ACCESS 1004 // the export refused this client
#define NFS_CONNECT_ERR_EXPORT 1005 // no such export
#define NFS_CONNECT_ERR_FULL   1007 // 8 exports already connected
#define NFS_CONNECT_ERR_MEMORY 1008 // no IOP memory for the connection

#endif
