//--------------------------------------------------------------
//File name:   smb_cnf.c
//SMB2.CNF parser for the smb: and smb1: devices. Text only, no device IO,
//so tests/test_smb_shares.py compiles it on the host.
//
//The syntax is the one of wLaunchELF_R3Z's smb: device (idea and format by
//Ronnie Sahlberg, wLaunchELF_R3Z PR #63): KEY = value lines, and the URL
//key closes a share with the keys above it. Launchstation adds VERSION
//(1 = SMB1 through smbman.irx), takes keys in any case, skips lines it
//cannot read instead of stopping, and rejects a share whose values would
//not fit the driver instead of cutting them short.
//--------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "smb_shares.h"

static int lowerChar(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int sameText(const char *a, const char *b)
{
	while (*a && lowerChar((unsigned char)*a) == lowerChar((unsigned char)*b)) {
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static int startsWithText(const char *s, const char *prefix)
{
	while (*prefix) {
		if (lowerChar((unsigned char)*s) != lowerChar((unsigned char)*prefix))
			return 0;
		s++;
		prefix++;
	}
	return 1;
}

static char *trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	return s;
}

/* Copies only when it fits: a cut password or URL fails later for a reason
 * the user cannot see. */
static int copyValue(char *dst, int dst_size, const char *src)
{
	size_t len = strlen(src);

	if (len >= (size_t)dst_size)
		return 0;
	memcpy(dst, src, len + 1);
	return 1;
}

int smbPathKind(const char *path)
{
	if (path == NULL)
		return SMB_PATH_NONE;
	if (!strncmp(path, SMB_DEVICE_V1, 5))
		return SMB_PATH_V1;
	if (!strncmp(path, SMB_DEVICE_V2, 4))
		return SMB_PATH_V2;
	return SMB_PATH_NONE;
}

int smbIsRootPath(const char *path)
{
	int kind = smbPathKind(path);

	if (kind == SMB_PATH_NONE)
		return 0;
	path += kind == SMB_PATH_V1 ? 5 : 4;
	while (*path == '/')
		path++;
	return *path == '\0';
}

static int parseVersion(const char *value)
{
	if (sameText(value, "1") || sameText(value, "SMB1") || sameText(value, "NT1"))
		return 1;
	if (sameText(value, "2") || sameText(value, "3") || sameText(value, "SMB2") || sameText(value, "SMB3"))
		return 2;
	return 0;
}

/* a.b.c.d with each part 0-255; smbman's logon wants the address itself */
static int isIpv4(const char *s)
{
	int parts = 0;

	while (parts < 4) {
		int value = 0, digits = 0;

		while (*s >= '0' && *s <= '9') {
			value = value * 10 + (*s++ - '0');
			if (++digits > 3)
				return 0;
		}
		if (digits == 0 || value > 255)
			return 0;
		parts++;
		if (parts < 4) {
			if (*s++ != '.')
				return 0;
		}
	}
	return *s == '\0';
}

/* smb://<IPv4>[:port]/<share>[/ignored] into server, port and share */
static const char *splitSmb1Url(SmbShare *sh)
{
	const char *host = sh->url + 6; /* past "smb://", checked by the caller */
	const char *slash = strchr(host, '/');
	const char *colon;
	const char *share;
	size_t host_len, share_len;

	if (slash == NULL || slash == host)
		return "URL needs smb://<server>/<share>";
	host_len = (size_t)(slash - host);
	if (memchr(host, '@', host_len) || memchr(host, ';', host_len))
		return "Put the user name in USERNAME, not in the URL";
	colon = memchr(host, ':', host_len);
	sh->port = 445;
	if (colon != NULL) {
		const char *p = colon + 1;
		long port = 0;

		if (p == slash)
			return "URL has an empty port";
		for (; p < slash; p++) {
			if (*p < '0' || *p > '9')
				return "URL port is not a number";
			port = port * 10 + (*p - '0');
			if (port > 65535)
				return "URL port is over 65535";
		}
		if (port == 0)
			return "URL port is 0";
		sh->port = (int)port;
		host_len = (size_t)(colon - host);
	}
	if (host_len == 0 || host_len >= sizeof(sh->server))
		return "SMB1 needs the server's IP address in the URL";
	memcpy(sh->server, host, host_len);
	sh->server[host_len] = '\0';
	if (!isIpv4(sh->server))
		return "SMB1 needs the server's IP address in the URL";

	share = slash;
	while (*share == '/')
		share++;
	share_len = strcspn(share, "/");
	if (share_len == 0)
		return "URL needs smb://<server>/<share>";
	if (share_len >= sizeof(sh->share))
		return "URL share name is too long";
	memcpy(sh->share, share, share_len);
	sh->share[share_len] = '\0';
	return NULL;
}

static const char *checkName(const SmbConfig *cfg, const SmbShare *sh)
{
	int i;

	if (sh->name[0] == '\0')
		return "NAME is missing";
	if (strpbrk(sh->name, "/\\:*?\"<>|") != NULL)
		return "NAME has a character a folder name cannot have";
	if (!strcmp(sh->name, ".") || !strcmp(sh->name, ".."))
		return "NAME cannot be . or ..";
	for (i = 0; i < cfg->count; i++)
		if (cfg->share[i].state != SMB_STATE_INVALID && sameText(cfg->share[i].name, sh->name))
			return "Another share already has this NAME";
	return NULL;
}

/* A share with its URL read: validate it and record it. */
static void closeShare(SmbConfig *cfg, SmbShare *sh, const char *pending_problem)
{
	const char *problem = pending_problem;
	int i;

	if (problem == NULL)
		problem = checkName(cfg, sh);
	if (problem == NULL && !startsWithText(sh->url, "smb://"))
		problem = "URL must start with smb://";
	if (problem == NULL && sh->version == 1) {
		for (i = 0; i < cfg->count; i++)
			if (cfg->share[i].state != SMB_STATE_INVALID && cfg->share[i].version == 1)
				problem = "Only one SMB1 share can be connected";
		if (problem == NULL)
			problem = splitSmb1Url(sh);
	}
	if (problem == NULL && sh->version == 2) {
		/* libsmb2 parses it; only refuse what Launchstation can see is wrong */
		const char *host = sh->url + 6;
		if (*host == '\0' || *host == '/')
			problem = "URL needs smb://<server>/<share>";
		else if (strchr(host, '/') == NULL || strchr(host, '/')[1] == '\0')
			problem = "URL needs smb://<server>/<share>";
	}

	if (cfg->count >= SMB_SHARE_MAX) {
		cfg->dropped++;
		return;
	}
	sh->state = problem == NULL ? SMB_STATE_UNTRIED : SMB_STATE_INVALID;
	sh->problem = problem;
	cfg->share[cfg->count++] = *sh;
}

int smbConfigParse(char *text, SmbConfig *cfg)
{
	SmbShare cur;
	const char *problem = NULL; /* first problem of the share being read */
	int has_keys = 0;
	int line_no = 0;
	char *line = text;
	int i, valid = 0;

	memset(cfg, 0, sizeof(*cfg));
	memset(&cur, 0, sizeof(cur));
	cur.version = 2;
	if (text == NULL)
		return 0;
	/* a UTF-8 BOM, as Windows Notepad may write */
	if ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
		line += 3;

	while (*line) {
		char *next = line, *key, *value, *eq;

		while (*next && *next != '\n' && *next != '\r')
			next++;
		if (*next == '\r' && next[1] == '\n')
			*next++ = '\0';
		if (*next)
			*next++ = '\0';
		line_no++;

		key = trim(line);
		line = next;
		if (key[0] == '\0' || key[0] == '#' || key[0] == ';')
			continue;
		eq = strchr(key, '=');
		if (eq == NULL)
			continue; /* not KEY = value: skipped, the rest still counts */
		*eq = '\0';
		value = trim(eq + 1);
		key = trim(key);

		if (sameText(key, "NAME")) {
			if (!copyValue(cur.name, sizeof(cur.name), value) && problem == NULL)
				problem = "NAME is longer than 31 characters";
		} else if (sameText(key, "USERNAME")) {
			if (!copyValue(cur.username, sizeof(cur.username), value) && problem == NULL)
				problem = "USERNAME is longer than 31 characters";
		} else if (sameText(key, "PASSWORD")) {
			if (!copyValue(cur.password, sizeof(cur.password), value) && problem == NULL)
				problem = "PASSWORD is longer than 31 characters";
		} else if (sameText(key, "VERSION")) {
			cur.version = parseVersion(value);
			if (cur.version == 0) {
				cur.version = 2;
				if (problem == NULL)
					problem = "VERSION must be 1 (SMB1) or 2 (SMB2/SMB3)";
			}
		} else if (sameText(key, "URL")) {
			if (!copyValue(cur.url, sizeof(cur.url), value) && problem == NULL)
				problem = "URL is longer than 255 characters";
			cur.line = line_no;
			closeShare(cfg, &cur, problem);
			memset(&cur, 0, sizeof(cur));
			cur.version = 2;
			problem = NULL;
			has_keys = 0;
			continue;
		} else {
			continue; /* unknown key: kept for later versions, ignored */
		}
		has_keys = 1;
	}
	if (has_keys)
		cfg->unfinished = 1;

	for (i = 0; i < cfg->count; i++)
		if (cfg->share[i].state != SMB_STATE_INVALID)
			valid++;
	return valid;
}

int smbConfigValidCount(const SmbConfig *cfg, int version)
{
	int i, n = 0;

	for (i = 0; i < cfg->count; i++)
		if (cfg->share[i].state != SMB_STATE_INVALID && cfg->share[i].version == version)
			n++;
	return n;
}
//--------------------------------------------------------------
//End of file: smb_cnf.c
//--------------------------------------------------------------
