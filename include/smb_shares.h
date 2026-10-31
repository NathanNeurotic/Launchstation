#ifndef SMB_SHARES_H
#define SMB_SHARES_H

//--------------------------------------------------------------
// SMB network shares: SMB2.CNF and the smb: / smb1: devices.
//
//   smb:/<NAME>/...  SMB2/SMB3 shares, through libsmb2's smb2man.irx
//   smb1:/...        one SMB1 (NT1) share, through smbman.irx
//
// smb_cnf.c only parses text and is compiled on the host by
// tests/test_smb_shares.py; smb_shares.c is the PS2 side.
//--------------------------------------------------------------

#define SMB_SHARE_MAX  8
#define SMB_NAME_LEN   32  /* as smb2man's SMB2_MAX_NAME_LEN, terminator included */
#define SMB_CRED_LEN   32  /* smb2Connect_in_t username/password */
#define SMB_URL_LEN    256 /* smb2Connect_in_t url */
#define SMB_SERVER_LEN 16  /* smbLogOn_in_t serverIP: dotted IPv4 */

#define SMB_DEVICE_V2 "smb:"
#define SMB_DEVICE_V1 "smb1:"

enum {
	SMB_PATH_NONE = 0,
	SMB_PATH_V1 = 1, /* smb1: */
	SMB_PATH_V2 = 2  /* smb: */
};

enum {
	SMB_STATE_INVALID = 0, /* rejected while parsing, never sent to a driver */
	SMB_STATE_UNTRIED,
	SMB_STATE_CONNECTED,
	SMB_STATE_FAILED
};

typedef struct
{
	char name[SMB_NAME_LEN];
	char username[SMB_CRED_LEN];
	char password[SMB_CRED_LEN];
	char url[SMB_URL_LEN];
	int version; /* 1 or 2 */
	/* SMB1 only, split out of url: smbman takes them separately. */
	char server[SMB_SERVER_LEN];
	int port;
	char share[SMB_URL_LEN];
	/* Where it stands; problem says why when INVALID or FAILED. */
	int state;
	int error;
	int line; /* line of its URL key in SMB2.CNF */
	const char *problem;
} SmbShare;

typedef struct
{
	SmbShare share[SMB_SHARE_MAX];
	int count;      /* shares recorded, valid or not */
	int dropped;    /* complete shares past SMB_SHARE_MAX */
	int unfinished; /* keys after the last URL, which never form a share */
} SmbConfig;

/* Parses SMB2.CNF text in place (the buffer is modified). Returns the
 * number of valid shares; every share, valid or not, is in cfg. */
int smbConfigParse(char *text, SmbConfig *cfg);
int smbConfigValidCount(const SmbConfig *cfg, int version);

/* SMB_PATH_V1 for "smb1:...", SMB_PATH_V2 for "smb:...", else SMB_PATH_NONE.
 * Plain prefix tests on "smb" would take smb1: for smb:. */
int smbPathKind(const char *path);
/* "smb:", "smb:/", "smb1:" or "smb1:/": the device itself, where smb: lists
 * its connected shares and nothing can be written. */
int smbIsRootPath(const char *path);

#ifdef SMB
/* PS2 side (smb_shares.c) */
int smbSharesConfigured(int version);   /* reads SMB2.CNF once; no network */
int smbSharesKnown(int version);        /* the same, but no IO: 0 until read */
const char *smbSharesConfigPath(void);  /* "" when no SMB2.CNF was found */
void smbSharesConnect(int version, int retry_failed);
int smbSharesConnectedCount(int version);
void smbSharesStatus(int version, char *out, int out_size); /* no IO */
void smbSharesForget(void); /* the drivers are gone (IOP reset) */
#endif

#endif
