//--------------------------------------------------------------
//File name:   smb_cnf.c
//SMB2.CNF parser for the smb: and smb1: devices. Text only, no device IO,
//so tests/test_smb_shares.py compiles it on the host.
//
//The syntax is the one of wLaunchELF_R3Z's smb: device (idea and format by
//Ronnie Sahlberg, wLaunchELF_R3Z PR #63): KEY = value lines, and the URL
//key closes a share with the keys above it. Launchstation adds VERSION
//(1 = SMB1 through smbman.irx) and PLAINTEXT (SMB1 only: allow a clear-text
//password), takes keys in any case, skips lines it
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

/* 1 for 1/yes/on/true, 0 for 0/no/off/false, -1 for anything else */
static int parseFlag(const char *value)
{
	if (sameText(value, "1") || sameText(value, "yes") || sameText(value, "on") || sameText(value, "true"))
		return 1;
	if (sameText(value, "0") || sameText(value, "no") || sameText(value, "off") || sameText(value, "false"))
		return 0;
	return -1;
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
	if (problem == NULL && sh->plaintext && sh->version != 1)
		problem = "PLAINTEXT works only with VERSION = 1";
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
	cur.config_version = 2;
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
			/* The IOP driver negotiates SMB2/3; retain the declared marker
			 * for the GUI to avoid silently rewriting VERSION = 3 as 2. */
			cur.config_version = cur.version == 2 &&
			                     (sameText(value, "3") || sameText(value, "SMB3"))
			                         ? 3 : cur.version;
			if (cur.version == 0) {
				cur.version = 2;
				cur.config_version = 2;
				if (problem == NULL)
					problem = "VERSION must be 1 (SMB1) or 2 (SMB2/SMB3)";
			}
		} else if (sameText(key, "PLAINTEXT")) {
			cur.plaintext = parseFlag(value);
			if (cur.plaintext < 0) {
				cur.plaintext = 0;
				if (problem == NULL)
					problem = "PLAINTEXT must be 1 (allow) or 0";
			}
		} else if (sameText(key, "URL")) {
			if (!copyValue(cur.url, sizeof(cur.url), value) && problem == NULL)
				problem = "URL is longer than 255 characters";
			cur.line = line_no;
			closeShare(cfg, &cur, problem);
			memset(&cur, 0, sizeof(cur));
			cur.version = 2;
			cur.config_version = 2;
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

/* ---- editing: compose one block, splice it into the file's text ---------- */

/* 1-based line offsets of the given 1-based line in nul-terminated text:
 * returns the offset of its first byte. */
static int lineOffset(const char *text, int line_no)
{
	const char *p = text;
	int n = 1;

	while (n < line_no && *p) {
		/* Match smbConfigParse: CR, LF and CRLF each end one line. */
		if (*p == '\r') {
			n++;
			if (p[1] == '\n')
				p++;
		} else if (*p == '\n')
			n++;
		p++;
	}
	return (int)(p - text);
}

/* Does this raw line hold KEY = value (not a comment, not blank)? */
static int lineIsKey(const char *line)
{
	const char *eq;

	while (*line == ' ' || *line == '\t')
		line++;
	if (*line == '\0' || *line == '#' || *line == ';' || *line == '\r' || *line == '\n')
		return 0;
	eq = strchr(line, '=');
	return eq != NULL;
}

/* The GUI replaces only canonical share fields. Keep user annotations and
 * unknown extension keys when modifying a share; do not reinterpret them. */
static int lineIsManagedKey(const char *line, int len)
{
	const char *start = line, *eq;
	char key[16];
	int key_len;

	while (start < line + len && (*start == ' ' || *start == '\t'))
		start++;
	if (start >= line + len || *start == '#' || *start == ';')
		return 0;
	eq = memchr(start, '=', line + len - start);
	if (eq == NULL)
		return 0;
	while (eq > start && (eq[-1] == ' ' || eq[-1] == '\t'))
		eq--;
	key_len = (int)(eq - start);
	if (key_len <= 0 || key_len >= (int)sizeof(key))
		return 0;
	memcpy(key, start, key_len);
	key[key_len] = '\0';
	return sameText(key, "NAME") || sameText(key, "USERNAME") ||
	       sameText(key, "PASSWORD") || sameText(key, "VERSION") ||
	       sameText(key, "PLAINTEXT") || sameText(key, "URL");
}

int smbCnfBuildBlock(char *out, int out_size, const char *name, int kind,
                     const char *username, const char *password,
                     const char *ip, int port, const char *share, int plaintext)
{
	int n = 0, url_len;

	if (out_size < 1 || name == NULL || name[0] == '\0' || strlen(name) >= SMB_NAME_LEN ||
	    strpbrk(name, "\r\n") != NULL)
		return -1;
	if (kind != 1 && kind != 2 && kind != 3)
		return -1;
	if (ip == NULL || !isIpv4(ip))
		return -1;
	if (port < 1 || port > 65535)
		return -1;
	if (share == NULL || share[0] == '\0' || strpbrk(share, "\r\n") != NULL)
		return -1;
	if (username != NULL && (strlen(username) >= SMB_CRED_LEN || strpbrk(username, "\r\n")))
		return -1;
	if (password != NULL && (strlen(password) >= SMB_CRED_LEN || strpbrk(password, "\r\n")))
		return -1;

	/* Every snprintf is bounds-checked before the next one: a truncated
	 * write must not leave n past out_size for the following call. */
	url_len = snprintf(out, out_size, "NAME = %s\n", name);
	if (url_len < 0 || url_len >= out_size)
		return -1;
	n = url_len;
	if (kind != 2) {
		url_len = snprintf(out + n, out_size - n, "VERSION = %d\n", kind);
		if (url_len < 0 || url_len >= out_size - n)
			return -1;
		n += url_len;
	}
	if (username != NULL && username[0] != '\0') {
		url_len = snprintf(out + n, out_size - n, "USERNAME = %s\n", username);
		if (url_len < 0 || url_len >= out_size - n)
			return -1;
		n += url_len;
		url_len = snprintf(out + n, out_size - n, "PASSWORD = %s\n",
		                   password != NULL ? password : "");
		if (url_len < 0 || url_len >= out_size - n)
			return -1;
		n += url_len;
	}
	if (kind == 1 && plaintext) {
		url_len = snprintf(out + n, out_size - n, "PLAINTEXT = 1\n");
		if (url_len < 0 || url_len >= out_size - n)
			return -1;
		n += url_len;
	}
	url_len = snprintf(out + n, out_size - n, "URL = smb://%s", ip);
	if (url_len < 0 || url_len >= out_size - n)
		return -1;
	n += url_len;
	if (port != 445) {
		url_len = snprintf(out + n, out_size - n, ":%d", port);
		if (url_len < 0 || url_len >= out_size - n)
			return -1;
		n += url_len;
	}
	url_len = snprintf(out + n, out_size - n, "/%s\n", share);
	if (url_len < 0 || url_len >= out_size - n || n + url_len >= SMB_URL_LEN)
		return -1;
	return n + url_len;
}

int smbCnfUpsertText(const char *text, const char *orig_name,
                     const char *block_text, char *out, int out_size, char *work, int work_size)
{
	SmbConfig cfg;
	int text_len, target = -1, prev_end = 0, start_line, end_line;
	int start_off, end_off, block_len, kept_len = 0, url_prefix_len = 0, i, off, at;

	if (out_size < 1 || work_size < 1)
		return -1;
	out[0] = '\0';
	if (text == NULL)
		text = "";
	text_len = strlen(text);
	if (text_len >= work_size)
		return -1;
	memcpy(work, text, text_len + 1);
	smbConfigParse(work, &cfg);
	/* The parser cannot identify and safely splice shares beyond its cap. */
	if (cfg.dropped)
		return -1;

	if (orig_name != NULL && orig_name[0] != '\0') {
		for (i = 0; i < cfg.count; i++)
			if (sameText(cfg.share[i].name, orig_name)) {
				target = i;
				break;
			}
	}

	if (target < 0) {
		/* Trailing unfinished fields could become part of an appended share.
		 * Refuse implicit merging rather than quietly changing credentials. */
		if (cfg.unfinished)
			return -1;
		/* Append: block_text is required. Separate from what is there. */
		if (block_text == NULL || block_text[0] == '\0')
			return -1;
		block_len = strlen(block_text);
		if (text_len + (text_len > 0 && text[text_len - 1] != '\n' ? 1 : 0) + block_len >= out_size)
			return -1;
		i = 0;
		memcpy(out, text, text_len);
		i = text_len;
		if (i > 0 && out[i - 1] != '\n')
			out[i++] = '\n';
		memcpy(out + i, block_text, block_len + 1);
		return i + block_len;
	}

	/* Replace or remove the target's own lines: from the first key line after
	 * the previous block's URL to this block's URL line. Comments above the
	 * first key line stay. */
	end_line = cfg.share[target].line;
	if (end_line <= 0)
		return -1;
	if (target > 0)
		prev_end = cfg.share[target - 1].line;
	start_line = end_line;
	{
		int line = prev_end + 1, off = lineOffset(text, line);
		while (line < end_line) {
			int len = strcspn(text + off, "\r\n");
			char probe[128];
			int is_key, copy = len < (int)sizeof(probe) - 1 ? len : (int)sizeof(probe) - 1;

			memcpy(probe, text + off, copy);
			probe[copy] = '\0';
			is_key = lineIsKey(probe);
			if (is_key) {
				start_line = line;
				break;
			}
			line++;
			off += len + (text[off + len] == '\r' && text[off + len + 1] == '\n' ? 2 : 1);
		}
	}
	start_off = lineOffset(text, start_line);
	/* The parser skips a file BOM; preserve it as a prefix, not as a
	 * stale unknown field spliced into the edited first share. */
	if (start_off == 0 && (unsigned char)text[0] == 0xEF &&
	    (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		start_off = 3;
	end_off = lineOffset(text, end_line);
	while (text[end_off] && text[end_off] != '\n' && text[end_off] != '\r')
		end_off++;
	if (text[end_off] == '\r') {
		end_off++;
		if (text[end_off] == '\n')
			end_off++;
	} else if (text[end_off] == '\n')
		end_off++;

	block_len = block_text != NULL ? strlen(block_text) : 0;
	if (block_text != NULL) {
		/* smbCnfBuildBlock ends in a URL line. Place retained notes before
		 * that URL, so the parser still associates them with this share. */
		const char *url_line = block_text, *p;
		for (p = block_text; *p; p++)
			if (*p == '\n' && p[1] != '\0')
				url_line = p + 1;
		if (!startsWithText(url_line, "URL = "))
			return -1;
		url_prefix_len = (int)(url_line - block_text);
	}
	/* Preserve in-block comments and blanks even for a deletion. Keep
	 * unknown extension fields on edits, but remove them with their share. */
	for (off = start_off; off < end_off; ) {
		int len = strcspn(text + off, "\r\n"), end = off + len;
		int keep;
		char probe[128];
		int copy = len < (int)sizeof(probe) - 1 ? len : (int)sizeof(probe) - 1;
		memcpy(probe, text + off, copy);
		probe[copy] = '\0';
		keep = !lineIsManagedKey(text + off, len) &&
		       (block_text != NULL || !lineIsKey(probe));
		if (text[end] == '\r' && text[end + 1] == '\n')
			end += 2;
		else if (text[end] == '\r' || text[end] == '\n')
			end++;
		if (keep)
			kept_len += end - off;
		off = end;
	}
	if (start_off + block_len + kept_len + (text_len - end_off) >= out_size)
		return -1;
	memcpy(out, text, start_off);
	at = start_off;
	if (block_text != NULL) {
		memcpy(out + at, block_text, url_prefix_len);
		at += url_prefix_len;
	}
	for (off = start_off; off < end_off; ) {
		int len = strcspn(text + off, "\r\n"), end = off + len;
		int keep;
		char probe[128];
		int copy = len < (int)sizeof(probe) - 1 ? len : (int)sizeof(probe) - 1;
		memcpy(probe, text + off, copy);
		probe[copy] = '\0';
		keep = !lineIsManagedKey(text + off, len) &&
		       (block_text != NULL || !lineIsKey(probe));
		if (text[end] == '\r' && text[end + 1] == '\n')
			end += 2;
		else if (text[end] == '\r' || text[end] == '\n')
			end++;
		if (keep) {
			memcpy(out + at, text + off, end - off);
			at += end - off;
		}
		off = end;
	}
	if (block_text != NULL) {
		memcpy(out + at, block_text + url_prefix_len, block_len - url_prefix_len);
		at += block_len - url_prefix_len;
	}
	memcpy(out + at, text + end_off, text_len - end_off + 1);
	return at + text_len - end_off;
}
//--------------------------------------------------------------
//End of file: smb_cnf.c
//--------------------------------------------------------------
