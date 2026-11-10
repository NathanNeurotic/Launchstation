#ifndef SMB2MAN_H
#define SMB2MAN_H

// devctl interface of the smb: driver, iop/smb2man/smb2man.c (included by it and by the EE).
// The same interface as the libsmb2 smb2man.irx it replaced (lib/ps2/ps2smb2.h, d61b781).

// Connect url, smb://<IPv4>[:port]/<share>, as the folder smb:/<name>/. Returns 0, -EEXIST
// when name is already connected (nothing changes), or -SMB2_CONNECT_ERR_*.
#define SMB2_DEVCTL_CONNECT        0xC0DE0001
// End every share's session and forget the shares. Returns 0.
#define SMB2_DEVCTL_DISCONNECT_ALL 0xC0DE0002

#define SMB2_MAX_NAME_LEN 32
typedef struct
{
	char name[SMB2_MAX_NAME_LEN];
	char username[32]; // UTF-8; Latin-1 characters only (NTLM as usmb2 does it)
	char password[32];
	char url[256];
} smb2Connect_in_t;

typedef struct
{
	void *ctx; // not written: kept so the EE's call is unchanged
} smb2Connect_out_t;

// Why a CONNECT failed (returned negative). Above every errno, so neither is taken for the other.
#define SMB2_CONNECT_ERR_URL     1001 // not smb://<IPv4>[:port]/<share>, or a name that does not fit
#define SMB2_CONNECT_ERR_CONN    1002 // no TCP connection within 5 s
#define SMB2_CONNECT_ERR_PROT    1003 // no SMB 2.0.2, 2.1, 3.0 or 3.0.2 (a 3.1.1-only server)
#define SMB2_CONNECT_ERR_SIGNING 1004 // SMB 3.x with signing required (AES-CMAC is not implemented)
#define SMB2_CONNECT_ERR_LOGON   1005 // user name or password refused, or not Latin-1
#define SMB2_CONNECT_ERR_SHARE   1006 // share refused or not found
#define SMB2_CONNECT_ERR_FULL    1007 // 8 shares already connected
#define SMB2_CONNECT_ERR_MEMORY  1008 // no IOP memory for the session

#endif
