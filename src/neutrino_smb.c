/* Neutrino SMB handoff, targeting ps2max32/neutrino #236 (1bff25b),
 * by Ronnie Sahlberg, using his usmb2 client. The browser's libsmb2 session
 * is not inherited: Neutrino reconnects after its own IOP reset. */
#include <stdio.h>
#include <string.h>
#include "neutrino_smb.h"

static int ipv4(const char *s)
{
	int part;
	for (part = 0; part < 4; part++) {
		int value = 0, digits = 0;
		while (*s >= '0' && *s <= '9') {
			value = value * 10 + *s++ - '0';
			if (++digits > 3) return 0;
		}
		if (!digits || value > 255) return 0;
		if (part < 3 && *s++ != '.') return 0;
	}
	return !*s;
}

static int ascii(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return 0;
	return 1;
}

int neutrinoSmbResolve(const SmbConfig *cfg, const char *path, SmbShare *share,
                      char *dvd, size_t dvd_size, const char **problem)
{
	const char *name, *relative, *host, *slash, *colon, *sub;
	size_t length;
	int i, kind = smbPathKind(path);
	*problem = "SMB share is not configured or connected";
	if (!cfg || !share || !dvd || !kind) return 0;
	name = path + (kind == SMB_PATH_V1 ? 5 : 4);
	while (*name == '/') name++;
	relative = strchr(name, '/');
	for (i = 0; i < cfg->count; i++) {
		const SmbShare *s = &cfg->share[i];
		if (s->state != SMB_STATE_CONNECTED || s->version != kind) continue;
		if (kind == SMB_PATH_V2 && (!relative || strlen(s->name) != (size_t)(relative-name) ||
		    strncmp(s->name, name, relative-name))) continue;
		*share = *s;
		break;
	}
	if (i == cfg->count) return 0;
	if (kind == SMB_PATH_V1) relative = name;
	else relative++;
	*problem = "Neutrino SMB needs an IPv4 server and TCP port 445";
	/* Parse independently of libsmb2: its URLs can contain DNS names, ports
	 * and a subdirectory, whereas usmb2 takes a dotted IP and share separately. */
	if (strncmp(share->url, "smb://", 6)) return 0;
	host = share->url + 6;
	slash = strchr(host, '/');
	if (!slash) return 0;
	colon = memchr(host, ':', slash-host);
	length = (size_t)((colon ? colon : slash)-host);
	if (!length || length >= sizeof(share->server)) return 0;
	memcpy(share->server, host, length); share->server[length] = 0;
	if (!ipv4(share->server)) return 0;
	if (colon && ((size_t)(slash-colon) != 4 || strncmp(colon, ":445", 4))) return 0;
	share->port = 445;
	sub = strchr(slash+1, '/');
	length = sub ? (size_t)(sub-slash-1) : strlen(slash+1);
	*problem = "Neutrino SMB share or ISO path exceeds the driver limits";
	if (!length || length > 63) return 0;
	memcpy(share->share, slash+1, length); share->share[length] = 0;
	/* libsmb2 URLs may carry a subdirectory below the share. */
	if (sub && sub[1]) i = snprintf(dvd, dvd_size, "smb:%s/%s", sub+1, relative);
	else i = snprintf(dvd, dvd_size, "smb:%s", relative);
	if (i < 5 || i >= (int)dvd_size || i-4 > 127 || !ascii(dvd)) return 0;
	*problem = "Neutrino SMB needs ASCII credentials of at most 31 characters";
	if (strlen(share->username) > 31 || strlen(share->password) > 31 ||
	    !ascii(share->username) || !ascii(share->password)) return 0;
	*problem = NULL;
	return 1;
}

/* Only accept the stock single-line basic-string args layout for the three
 * SMB modules. Preserve every other line, including comments and env values.
 * Unknown, duplicate, missing or multiline args are refused before any write. */
static int argsShape(const char *p, size_t length, int module, const char **tail)
{
	static const char *keys[] = {"ip=", "mask=", "gw=", "srv=", "user=", "pass=", "share="};
	const char *end = p + length;
	unsigned seen = 0;
	int k;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (end-p < 4 || strncmp(p, "args", 4)) return 0;
	p += 4;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p == end || *p++ != '=') return 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p == end || *p++ != '[') return 0;
	for (;;) {
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p == end) return 0;
		if (*p == ']') { p++; break; }
		if (*p++ != '"') return 0;
		for (k = 0; k < 7; k++)
			if ((size_t)(end-p) >= strlen(keys[k]) && !strncmp(p, keys[k], strlen(keys[k]))) break;
		if (k == 7 || (module == 1 && k != 0) || (seen & (1u<<k))) return 0;
		seen |= 1u<<k;
		while (p < end && *p != '"') {
			if (*p == '\\') { if (++p == end) return 0; }
			p++;
		}
		if (p == end) return 0;
		p++;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p < end && *p == ',') p++;
		else if (p == end || *p != ']') return 0;
	}
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
	*tail = p;
	return (p == end || *p == '#') && seen == (module == 1 ? 1u : 127u);
}

static int append(char *out, size_t size, size_t *used, const char *text, size_t length)
{
	if (*used >= size || length >= size-*used) return 0;
	memcpy(out+*used, text, length); *used += length; out[*used] = 0;
	return 1;
}

static int argument(char *out, size_t size, size_t *used, const char *key, const char *value)
{
	if (!append(out,size,used,"\"",1) || !append(out,size,used,key,strlen(key))) return 0;
	for (; *value; value++) {
		if ((unsigned char)*value < 32 || (unsigned char)*value > 126) return 0;
		if ((*value == '"' || *value == '\\') && !append(out,size,used,"\\",1)) return 0;
		if (!append(out,size,used,value,1)) return 0;
	}
	return append(out,size,used,"\"",1);
}

int neutrinoSmbConfig(const char *original, const SmbShare *share,
                     const char *ip, const char *mask, const char *gw,
                     char *out, size_t size)
{
	static const char *files[] = {"ministack.irx", "smb_ioman.irx", "smb_fhi.irx"};
	static const char *keys[] = {"ip=", "mask=", "gw=", "srv=", "user=", "pass=", "share="};
	const char *values[] = {ip,mask,gw,share->server,share->username,share->password,share->share};
	const char *line = original;
	size_t used = 0;
	unsigned seen = 0;
	int module = 0, k;
	if (!size || !ipv4(ip) || !strcmp(ip,"0.0.0.0") || !ipv4(mask) || !ipv4(gw) ||
	    !ipv4(share->server) || strlen(share->username) > 31 || strlen(share->password) > 31 ||
	    !share->share[0] || strlen(share->share) > 63) return -1;
	out[0] = 0;
	while (*line) {
		const char *end = strchr(line,'\n'), *p = line;
		size_t len = end ? (size_t)(end-line) : strlen(line);
		while (p < line+len && (*p == ' ' || *p == '\t')) p++;
		if (p < line+len && *p == '[') {
			if (strncmp(p,"[[module]]",10)) return -1;
			module = 0;
		}
		if (!strncmp(p,"file = \"",8)) {
			module = 0;
			for (k = 0; k < 3; k++) {
				size_t n = strlen(files[k]);
				if (!strncmp(p+8,files[k],n) && p[8+n] == '"') module = k+1;
			}
		}
		if (module && !strncmp(p,"args",4)) {
			const char *tail;
			if ((seen & (1u<<module)) || !argsShape(p,(size_t)(line+len-p),module,&tail)) return -1;
			seen |= 1u<<module;
			if (!append(out,size,&used,"args = [",8)) return -1;
			for (k = 0; k < (module == 1 ? 1 : 7); k++) {
				if (k && !append(out,size,&used,", ",2)) return -1;
				if (!argument(out,size,&used,keys[k],values[k])) return -1;
			}
			if (!append(out,size,&used,"]",1)) return -1;
			if (tail < line+len && (!append(out,size,&used," ",1) ||
			    !append(out,size,&used,tail,(size_t)(line+len-tail)))) return -1;
		} else if (!append(out,size,&used,line,len)) return -1;
		if (end && !append(out,size,&used,"\n",1)) return -1;
		line += len + (end != NULL);
	}
	return seen == 14 ? (int)used : -1;
}
