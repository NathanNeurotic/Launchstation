//--------------------------------------------------------------
//File name:   smb_shares.c
//SMB network shares on the PS2 side: finds and reads SMB2.CNF, connects its
//shares through smb2man.irx (smb:, SMB2/SMB3) and smbman.irx (smb1:, SMB1),
//and keeps what happened to each share for the browser to show.
//
//init.c loads the drivers (load_smb()); this file only talks to them.
//--------------------------------------------------------------
#include "launchelf.h"
#include "dash_internal.h"
#include "smb_shares.h"
#include "neutrino_smb.h"
#include "smb2man.h"
#include <ps2smb.h>

#define SMB_CNF_NAME     "SMB2.CNF"
#define SMB_CNF_MAX_SIZE 16384
/* A failed share is tried again when its device is opened, but not sooner
 * than this: each try can wait for libsmb2's 30 s timeout. */
#define SMB_RETRY_MS 30000

/* smbman's logon errors are positive codes; this one is ours, for a
 * logon that worked and a share that did not open. */
#define SMB1_ERR_OPENSHARE (-(SMB_DEVCTL_LOGON_ERR_LOGON + 1))
#define SMB1_ERR_HASH (-(SMB_DEVCTL_LOGON_ERR_LOGON + 2))

static SmbConfig smb_cfg;
static int smb_cfg_loaded = 0;
static char smb_cfg_path[MAX_PATH];
static unsigned int smb_retry_ms[3]; /* by version: last retry of failed shares */
static void smbReadConfig(void);

int smbSharesNeutrino(const char *path, SmbShare *share, char *dvd,
                     size_t dvd_size, const char **problem)
{
	if (!smb_cfg_loaded)
		smbReadConfig();
	return neutrinoSmbResolve(&smb_cfg, path, share, dvd, dvd_size, problem);
}

/* Same places as IPCONFIG.DAT: beside the launched ELF, then SYS-CONF on
 * the card it was launched from, then on the other card. */
static char *smbReadConfigText(void)
{
	char path[MAX_PATH];
	char *text;
	int port, i;

	smb_cfg_path[0] = '\0';
	if (strlen(LaunchElfDir) + sizeof(SMB_CNF_NAME) <= sizeof(path)) {
		snprintf(path, sizeof(path), "%s%s", LaunchElfDir, SMB_CNF_NAME);
		if ((text = dashLoadTextFile(path, SMB_CNF_MAX_SIZE, NULL)) != NULL) {
			snprintf(smb_cfg_path, sizeof(smb_cfg_path), "%s", path);
			return text;
		}
	}
	port = !strncmp(LaunchElfDir, "mc1", 3);
	for (i = 0; i < 2; i++, port ^= 1) {
		snprintf(path, sizeof(path), "mc%d:/SYS-CONF/%s", port, SMB_CNF_NAME);
		if ((text = dashLoadTextFile(path, SMB_CNF_MAX_SIZE, NULL)) != NULL) {
			snprintf(smb_cfg_path, sizeof(smb_cfg_path), "%s", path);
			return text;
		}
	}
	return NULL;
}

static void smbReadConfig(void)
{
	char *text = smbReadConfigText();
	int i;

	smbConfigParse(text, &smb_cfg);
	free(text);
	smb_cfg_loaded = 1;
	DPRINTF(" [SMB]: %s: %d share(s), dropped=%d unfinished=%d\n",
	        smb_cfg_path[0] ? smb_cfg_path : "no " SMB_CNF_NAME,
	        smb_cfg.count, smb_cfg.dropped, smb_cfg.unfinished);
	for (i = 0; i < smb_cfg.count; i++)
		if (smb_cfg.share[i].state == SMB_STATE_INVALID)
			DPRINTF(" [SMB]: line %d, %s: %s\n", smb_cfg.share[i].line,
			        smb_cfg.share[i].name, smb_cfg.share[i].problem);
}

int smbSharesConfigured(int version)
{
	if (!smb_cfg_loaded)
		smbReadConfig();
	return smbConfigValidCount(&smb_cfg, version);
}

int smbSharesKnown(int version)
{
	return smb_cfg_loaded ? smbConfigValidCount(&smb_cfg, version) : 0;
}

const char *smbSharesConfigPath(void)
{
	return smb_cfg_path;
}

static int connectSmb2(SmbShare *sh)
{
	smb2Connect_in_t in;
	smb2Connect_out_t out;
	int ret;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	memcpy(in.name, sh->name, sizeof(in.name));
	memcpy(in.username, sh->username, sizeof(in.username));
	memcpy(in.password, sh->password, sizeof(in.password));
	memcpy(in.url, sh->url, sizeof(in.url));
	ret = fileXioDevctl(SMB_DEVICE_V2, SMB2_DEVCTL_CONNECT, &in, sizeof(in), &out, sizeof(out));
	memset(&in, 0, sizeof(in)); /* no password left on the stack */
	/* -EEXIST: the driver already has it, from before a reset of our state */
	return ret == -EEXIST ? 0 : ret;
}

/* smbman serves one share: log on to the server, then open the share. */
static int connectSmb1(SmbShare *sh)
{
	smbLogOn_in_t logon;
	smbOpenShare_in_t openshare;
	smbGetPasswordHashes_in_t passwd;
	smbGetPasswordHashes_out_t hashes;
	int ret;

	memset(&logon, 0, sizeof(logon));
	memset(&openshare, 0, sizeof(openshare));
	snprintf(logon.serverIP, sizeof(logon.serverIP), "%s", sh->server);
	logon.serverPort = sh->port;
	snprintf(logon.User, sizeof(logon.User), "%s", sh->username);
	if (sh->password[0]) {
		/* Hashed (LM/NTLM) unless the share opts in with PLAINTEXT = 1:
		 * smbman sends a password in clear text only when it is given the
		 * clear text (PLAINTEXT_PASSWORD), and still hashes it for a server
		 * that takes challenge/response. */
		memset(&passwd, 0, sizeof(passwd));
		snprintf(passwd.password, sizeof(passwd.password), "%s", sh->password);
		if (sh->plaintext) {
			snprintf(logon.Password, sizeof(logon.Password), "%s", sh->password);
			logon.PasswordType = PLAINTEXT_PASSWORD;
			snprintf(openshare.Password, sizeof(openshare.Password), "%s", sh->password);
			openshare.PasswordType = PLAINTEXT_PASSWORD;
		} else if (fileXioDevctl(SMB_DEVICE_V1, SMB_DEVCTL_GETPASSWORDHASHES, &passwd, sizeof(passwd), &hashes, sizeof(hashes)) == 0) {
			memcpy(logon.Password, &hashes, sizeof(hashes));
			logon.PasswordType = HASHED_PASSWORD;
			memcpy(openshare.Password, &hashes, sizeof(hashes));
			openshare.PasswordType = HASHED_PASSWORD;
		} else {
			memset(&passwd, 0, sizeof(passwd));
			return SMB1_ERR_HASH;
		}
		memset(&passwd, 0, sizeof(passwd));
		memset(&hashes, 0, sizeof(hashes));
	} else {
		logon.PasswordType = NO_PASSWORD;
		openshare.PasswordType = NO_PASSWORD;
	}

	ret = fileXioDevctl(SMB_DEVICE_V1, SMB_DEVCTL_LOGON, &logon, sizeof(logon), NULL, 0);
	if (ret >= 0) {
		snprintf(openshare.ShareName, sizeof(openshare.ShareName), "%s", sh->share);
		ret = fileXioDevctl(SMB_DEVICE_V1, SMB_DEVCTL_OPENSHARE, &openshare, sizeof(openshare), NULL, 0);
		if (ret < 0) {
			/* leave no half session behind for the next try */
			fileXioDevctl(SMB_DEVICE_V1, SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
			ret = SMB1_ERR_OPENSHARE;
		}
	}
	memset(&logon, 0, sizeof(logon));
	memset(&openshare, 0, sizeof(openshare));
	return ret;
}

static const char *smb1Problem(int ret)
{
	switch (ret) {
		case -SMB_DEVCTL_LOGON_ERR_CONN: return "Server did not answer";
		case -SMB_DEVCTL_LOGON_ERR_PROT: return "Server does not speak SMB1";
		case -SMB_DEVCTL_LOGON_ERR_LOGON: return "User name or password refused (or the server wants a clear-text password: PLAINTEXT = 1)";
		case SMB1_ERR_OPENSHARE: return "Share refused or not found";
		case SMB1_ERR_HASH: return "Could not prepare the password";
		default: return "Could not connect";
	}
}

/* Connects the valid shares of one version that are not connected yet.
 * retry_failed also tries the failed ones again, at most every
 * SMB_RETRY_MS. With no share connected, SMB2.CNF is read again first, so
 * a corrected file is used without a restart. */
void smbSharesConnect(int version, int retry_failed)
{
	unsigned int now = (unsigned int)Timer();
	int i, ret;

	if (retry_failed) {
		if (smb_cfg_loaded && now - smb_retry_ms[version] < SMB_RETRY_MS)
			retry_failed = 0;
		else
			smb_retry_ms[version] = now;
	}
	if (!smb_cfg_loaded || (retry_failed && smbSharesConnectedCount(1) == 0 && smbSharesConnectedCount(2) == 0))
		smbReadConfig();
	for (i = 0; i < smb_cfg.count; i++) {
		SmbShare *sh = &smb_cfg.share[i];

		if (sh->version != version || sh->state == SMB_STATE_INVALID || sh->state == SMB_STATE_CONNECTED)
			continue;
		if (sh->state == SMB_STATE_FAILED && !retry_failed)
			continue;
		ret = version == 1 ? connectSmb1(sh) : connectSmb2(sh);
		sh->error = ret;
		if (ret >= 0) {
			sh->state = SMB_STATE_CONNECTED;
			sh->problem = NULL;
		} else {
			sh->state = SMB_STATE_FAILED;
			sh->problem = version == 1 ? smb1Problem(ret)
			                           : "Could not connect: check the address, share, user name and password";
		}
		DPRINTF(" [SMB%d]: %s as %s%s, ret=%d\n", version, sh->url,
		        version == 1 ? SMB_DEVICE_V1 "/" : SMB_DEVICE_V2 "/", version == 1 ? "" : sh->name, ret);
	}
}

int smbSharesConnectedCount(int version)
{
	int i, n = 0;

	for (i = 0; i < smb_cfg.count; i++)
		if (smb_cfg.share[i].version == version && smb_cfg.share[i].state == SMB_STATE_CONNECTED)
			n++;
	return n;
}

/* One line for the browser: what is connected, or why nothing is. */
void smbSharesStatus(int version, char *out, int out_size)
{
	int i, valid = 0, connected = 0;
	const SmbShare *first_bad = NULL;

	if (!smb_cfg_loaded) {
		snprintf(out, out_size, "Open to read %s and connect", SMB_CNF_NAME);
		return;
	}
	if (!smb_cfg_path[0]) {
		snprintf(out, out_size, "No %s found (beside Launchstation or in mc?:/SYS-CONF)", SMB_CNF_NAME);
		return;
	}
	for (i = 0; i < smb_cfg.count; i++) {
		const SmbShare *sh = &smb_cfg.share[i];

		if (sh->version != version)
			continue;
		if (sh->state != SMB_STATE_INVALID)
			valid++;
		if (sh->state == SMB_STATE_CONNECTED)
			connected++;
		else if (first_bad == NULL && (sh->state == SMB_STATE_INVALID || sh->state == SMB_STATE_FAILED))
			first_bad = sh;
	}
	if (first_bad != NULL)
		snprintf(out, out_size, "%s (line %d): %s", first_bad->name[0] ? first_bad->name : "share",
		         first_bad->line, first_bad->problem ? first_bad->problem : "Could not connect");
	else if (valid == 0)
		snprintf(out, out_size, "%s has no %s share", smb_cfg_path, version == 1 ? "VERSION = 1" : "SMB2/SMB3");
	else
		snprintf(out, out_size, "%d of %d share(s) connected", connected, valid);
}

/* The drivers went away with an IOP reset: every share must be connected
 * again. The parsed SMB2.CNF stays, so the device list still knows smb1:. */
void smbSharesForget(void)
{
	int i;

	for (i = 0; i < smb_cfg.count; i++) {
		SmbShare *sh = &smb_cfg.share[i];

		if (sh->state == SMB_STATE_INVALID)
			continue;
		sh->state = SMB_STATE_UNTRIED;
		sh->error = 0;
		sh->problem = NULL;
	}
	memset(smb_retry_ms, 0, sizeof(smb_retry_ms));
}
//--------------------------------------------------------------
//End of file: smb_shares.c
//--------------------------------------------------------------
