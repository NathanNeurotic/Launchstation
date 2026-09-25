//--------------------------------------------------------------
//File name:   dash_lib.c
//Dashboard application (ELF) library.
//
//Discovery never walks whole filesystems. Only the configured scan
//locations are listed (default mc:/APPS, mc:/BOOT, mass:/APPS, mass:/BOOT),
//each to a depth of one sub-folder (APPS/<folder>/*.ELF). Directory listing
//goes through the FileBrowser's getDir(), so every device, partition mount
//and driver quirk handled there is handled here too. Results are cached in
//DASHLIB.CNF and shown instantly on the next boot.
//
//Optional metadata sidecars (plain "key = value" text, all keys optional):
//  <dir>/<ELFNAME>.cfg   e.g. OPNPS2LD.cfg next to OPNPS2LD.ELF
//  <dir>/title.cfg       OPL application format (title=..., boot=...)
//Keys: title (or name), description, author, version, category,
//      cover/art, arg (repeatable, one argument per line), pinned.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "main_startup.h"
#include "init.h"

DashApp *dashApps;
int dashAppCount;

static int lib_ready;

typedef struct
{
	const char *key;
	const char *name;
} KnownName;

/* Friendly names for well-known PS2 homebrew executables. */
static const KnownName known_names[] = {
    {"OPNPS2LD", "Open PS2 Loader"},
    {"OPL", "Open PS2 Loader"},
    {"ULE", "uLaunchELF"},
    {"WLE", "wLaunchELF"},
    {"WLAUNCHELF", "wLaunchELF"},
    {"ULAUNCHELF", "uLaunchELF"},
    {"ESR", "ESR"},
    {"SMS", "Simple Media System"},
    {"NHDDL", "NHDDL"},
    {"NEUTRINO", "Neutrino"},
    {"HDLDUMP", "HDL Dump"},
    {"HDLGAMEINSTALLER", "HDL Game Installer"},
    {"GSM", "GS Mode Selector"},
    {"PS2ESDL", "PS2ESDL"},
    {"FMCB", "Free McBoot"},
    {"FREEMCBOOT", "Free McBoot"},
    {"FMCBCFG", "FMCB Configurator"},
    {"OSDMENU", "OSDMenu"},
    {"PS2LINK", "PS2Link"},
    {"RETROARCH", "RetroArch"},
    {"PICODRIVE", "PicoDrive"},
    {"INFOGB", "InfoGB"},
    {"FCEUMM", "FCEUmm"},
    {"SNES9X", "Snes9x"},
    {"SNESSTATION", "SNES Station"},
    {"PS2DOOM", "PS2 Doom"},
    {"LUNA", "LUNA"},
    {"XEB", "XEB+"},
    {"CHEATDEVICE", "Cheat Device"},
    {"POPSTARTER", "POPStarter"},
    {"PS2BBL", "PS2BBL"},
    {"KELFBINDER", "KELFBinder"},
    {"MCTOOLS", "Memory Card Tools"},
    {"HDDCHECKER", "HDD Checker"},
    {"PS2IDENT", "PS2Ident"},
    {"SYSTEMINFO", "System Info"},
    {NULL, NULL}};

/* File names too generic to be shown; the parent folder name is used. */
static const char *const generic_names[] = {
    "BOOT", "MAIN", "EXECUTE", "RUN", "START", "APP", "LAUNCHER", "ELF", "PROGRAM", "GAME", NULL};

//--------------------------------------------------------------
// Naming
//--------------------------------------------------------------
static void upperCopy(char *dst, const char *src, int n)
{
	int i;

	for (i = 0; i < n - 1 && src[i]; i++)
		dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
	dst[i] = '\0';
}

static void cleanName(char *s)
{
	char *p;
	int i;

	/* OPL application folders are often "APP_<name>" */
	if (!strncmp(s, "APP_", 4) || !strncmp(s, "app_", 4))
		memmove(s, s + 4, strlen(s + 4) + 1);
	for (p = s; *p; p++)
		if (*p == '_')
			*p = ' ';
	i = strlen(s);
	while (i > 0 && s[i - 1] == ' ')
		s[--i] = '\0';
}

void dashLibPrettyName(const char *path, char *out, int out_size)
{
	char base[DASH_PATH_MAX], folder[DASH_PATH_MAX], up[DASH_PATH_MAX];
	const char *slash, *name;
	char *dot;
	int i, generic = 0;

	slash = strrchr(path, '/');
	name = slash ? slash + 1 : path;
	if (!slash) {
		const char *colon = strchr(path, ':');
		name = colon ? colon + 1 : path;
	}
	dashStrCopy(base, name, sizeof(base));
	dot = strrchr(base, '.');
	if (dot != NULL)
		*dot = '\0';

	folder[0] = '\0';
	if (slash != NULL && slash > path) {
		const char *p = slash - 1;
		while (p > path && *p != '/' && *p != ':')
			p--;
		if (*p == '/' || *p == ':')
			p++;
		i = (int)(slash - p);
		if (i > 0 && i < (int)sizeof(folder)) {
			memcpy(folder, p, i);
			folder[i] = '\0';
		}
	}

	upperCopy(up, base, sizeof(up));
	for (i = 0; generic_names[i]; i++)
		if (!strcmp(up, generic_names[i]))
			generic = 1;
	if (generic && folder[0] && stricmp(folder, "APPS") && stricmp(folder, "BOOT"))
		dashStrCopy(base, folder, sizeof(base));

	upperCopy(up, base, sizeof(up));
	for (i = 0; known_names[i].key; i++) {
		if (!strcmp(up, known_names[i].key) ||
		    (!strncmp(up, known_names[i].key, strlen(known_names[i].key)) && up[strlen(known_names[i].key)] >= '0' &&
		     up[strlen(known_names[i].key)] <= '9' && strlen(known_names[i].key) > 3)) {
			dashStrCopy(out, known_names[i].name, out_size);
			return;
		}
	}
	cleanName(base);
	dashStrCopy(out, base[0] ? base : name, out_size);
}

static u16 hashHue(const char *s)
{
	u32 h = 5381;

	while (*s)
		h = ((h << 5) + h) ^ (u8)(*s++);
	return (u16)(h % 360);
}

void dashLibDeviceLabel(const char *path, char *out, int out_size)
{
	if (!strncmp(path, "mc0", 3))
		dashStrCopy(out, "Memory Card 1", out_size);
	else if (!strncmp(path, "mc1", 3))
		dashStrCopy(out, "Memory Card 2", out_size);
	else if (!strncmp(path, "mc:", 3))
		dashStrCopy(out, "Memory Card", out_size);
	else if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3))
		dashStrCopy(out, "USB Storage", out_size);
	else if (!strncmp(path, "hdd", 3)) {
		char part[40];
		const char *p = strchr(path, '/');
		int i = 0;

		if (p != NULL) {
			p++;
			while (*p && *p != '/' && i < (int)sizeof(part) - 1)
				part[i++] = *p++;
		}
		part[i] = '\0';
		if (i > 0)
			snprintf(out, out_size, "Internal HDD: %s", part);
		else
			dashStrCopy(out, "Internal HDD", out_size);
	} else if (!strncmp(path, "mmce", 4))
		snprintf(out, out_size, "MMCE Slot %c", (path[4] == '1') ? '2' : '1');
	else if (!strncmp(path, "mx4sio", 6))
		dashStrCopy(out, "MX4SIO SD Card", out_size);
	else if (!strncmp(path, "ata", 3))
		dashStrCopy(out, "exFAT HDD", out_size);
	else if (!strncmp(path, "cdfs", 4) || !strncmp(path, "cdrom", 5))
		dashStrCopy(out, "Disc", out_size);
	else if (!strncmp(path, "udpfs", 5))
		dashStrCopy(out, "Network (UDPFS)", out_size);
	else if (!strncmp(path, "host", 4))
		dashStrCopy(out, "Network Host", out_size);
	else if (!strncmp(path, "xfrom", 5))
		dashStrCopy(out, "PSX Flash", out_size);
	else if (!strncmp(path, "dvr_hdd", 7))
		dashStrCopy(out, "PSX DVR HDD", out_size);
	else if (!strncmp(path, "vmc", 3))
		dashStrCopy(out, "Virtual MC", out_size);
	else if (!strncmp(path, "rom", 3))
		dashStrCopy(out, "System ROM", out_size);
	else
		dashStrCopy(out, "Storage", out_size);
}

int dashLibDeviceIcon(const char *path)
{
	if (!strncmp(path, "mc", 2) || !strncmp(path, "vmc", 3))
		return DI_MC;
	if (!strncmp(path, "mass", 4) || !strncmp(path, "usb", 3))
		return DI_USB;
	if (!strncmp(path, "hdd", 3) || !strncmp(path, "ata", 3) || !strncmp(path, "dvr_hdd", 7))
		return DI_HDD;
	if (!strncmp(path, "mmce", 4) || !strncmp(path, "mx4sio", 6))
		return DI_SDCARD;
	if (!strncmp(path, "cdfs", 4) || !strncmp(path, "cdrom", 5))
		return DI_DISC;
	if (!strncmp(path, "udpfs", 5) || !strncmp(path, "host", 4))
		return DI_NETWORK;
	if (!strncmp(path, "xfrom", 5) || !strncmp(path, "rom", 3))
		return DI_CHIP;
	return DI_FOLDER;
}

//--------------------------------------------------------------
// Metadata
//--------------------------------------------------------------
typedef struct
{
	DashApp *app;
	const char *elf_name;
	int opl_style;
	int boot_mismatch;
	char *args;
	int args_size;
	int args_len;
	int nargs;
} MetaCtx;

static void sanitizeField(char *s)
{
	for (; *s; s++)
		if (*s == '|' || *s == '\t')
			*s = ' ';
}

static int metaKv(const char *key, char *value, void *user)
{
	MetaCtx *m = (MetaCtx *)user;
	DashApp *a = m->app;

	if (m->opl_style && !stricmp(key, "boot")) {
		if (m->elf_name && value[0] && stricmp(value, m->elf_name))
			m->boot_mismatch = 1;
		return 0;
	}
	if (a != NULL) {
		sanitizeField(value);
		if (!stricmp(key, "title") || !stricmp(key, "name")) {
			if (value[0])
				dashStrCopy(a->title, value, sizeof(a->title));
		} else if (!stricmp(key, "description") || !stricmp(key, "desc"))
			dashStrCopy(a->desc, value, sizeof(a->desc));
		else if (!stricmp(key, "author") || !stricmp(key, "developer"))
			dashStrCopy(a->author, value, sizeof(a->author));
		else if (!stricmp(key, "version"))
			dashStrCopy(a->version, value, sizeof(a->version));
		else if (!stricmp(key, "category") || !stricmp(key, "genre"))
			dashStrCopy(a->category, value, sizeof(a->category));
		else if (!stricmp(key, "cover") || !stricmp(key, "art") || !stricmp(key, "icon")) {
			if (value[0] && a->art_state != DASH_ART_FOUND) {
				/* relative paths resolve against the ELF folder */
				if (strchr(value, ':') == NULL) {
					char dir[DASH_PATH_MAX];
					const char *slash = strrchr(a->path, '/');
					int n = slash ? (int)(slash - a->path) + 1 : 0;
					if (n > 0 && n < (int)sizeof(dir)) {
						memcpy(dir, a->path, n);
						dir[n] = '\0';
						snprintf(a->art, sizeof(a->art), "%s%s", dir, value);
					}
				} else
					dashStrCopy(a->art, value, sizeof(a->art));
				a->art_state = DASH_ART_UNKNOWN; /* verified by the art loader */
			}
		} else if (!stricmp(key, "pinned") || !stricmp(key, "favorite")) {
			if (atoi(value))
				a->flags |= DASH_APP_META_PIN;
		} else if (!stricmp(key, "arg") || !stricmp(key, "args"))
			a->flags |= DASH_APP_HAS_ARGS;
	}
	if (m->args != NULL && (!stricmp(key, "arg") || !stricmp(key, "args")) && value[0]) {
		int len = strlen(value);
		if (m->args_len + len + 2 < m->args_size) {
			memcpy(m->args + m->args_len, value, len);
			m->args_len += len;
			m->args[m->args_len++] = '\n';
			m->args[m->args_len] = '\0';
			m->nargs++;
		}
	}
	return 0;
}

static void splitPath(const char *path, char *dir, int dir_size, char *file, int file_size, char *base, int base_size)
{
	const char *slash = strrchr(path, '/');
	const char *name = slash ? slash + 1 : path;
	char *dot;
	int n = (int)(name - path);

	if (n >= dir_size)
		n = dir_size - 1;
	memcpy(dir, path, n);
	dir[n] = '\0';
	dashStrCopy(file, name, file_size);
	dashStrCopy(base, name, base_size);
	dot = strrchr(base, '.');
	if (dot != NULL)
		*dot = '\0';
}

int dashLibMetadataPath(const DashApp *app, char *out, int out_size)
{
	char dir[DASH_PATH_MAX], file[DASH_PATH_MAX], base[DASH_PATH_MAX];

	splitPath(app->path, dir, sizeof(dir), file, sizeof(file), base, sizeof(base));
	return snprintf(out, out_size, "%s%s.cfg", dir, base) < out_size ? 0 : -1;
}

/* Reads sidecar metadata into app (and optionally args). Returns number of
 * metadata files found. */
static int readMetadata(DashApp *app, char *args, int args_size, int *nargs)
{
	char dir[DASH_PATH_MAX], file[DASH_PATH_MAX], base[DASH_PATH_MAX];
	char path[MAX_PATH];
	char *text;
	int len, found = 0;
	MetaCtx ctx;

	splitPath(app->path, dir, sizeof(dir), file, sizeof(file), base, sizeof(base));
	memset(&ctx, 0, sizeof(ctx));
	ctx.app = app;
	ctx.elf_name = file;
	ctx.args = args;
	ctx.args_size = args_size;
	if (args != NULL && args_size > 0)
		args[0] = '\0';

	/* OPL-style title.cfg first, so a dedicated sidecar can override it. */
	snprintf(path, sizeof(path), "%stitle.cfg", dir);
	text = dashLoadTextFile(path, 16 * 1024, &len);
	if (text != NULL) {
		MetaCtx pc;

		/* Check boot= first so a folder-level title.cfg for another ELF is
		 * not applied to this one. */
		memset(&pc, 0, sizeof(pc));
		pc.elf_name = file;
		pc.opl_style = 1;
		{
			char *copy = strdup(text);
			if (copy != NULL) {
				dashKvParse(copy, metaKv, &pc);
				free(copy);
			}
		}
		if (!pc.boot_mismatch) {
			ctx.opl_style = 1;
			dashKvParse(text, metaKv, &ctx);
			found++;
		}
		free(text);
	}

	snprintf(path, sizeof(path), "%s%s.cfg", dir, base);
	text = dashLoadTextFile(path, 16 * 1024, &len);
	if (text != NULL) {
		ctx.opl_style = 0;
		dashKvParse(text, metaKv, &ctx);
		free(text);
		found++;
	}
	if (found)
		app->flags |= DASH_APP_HAS_META;
	if (nargs != NULL)
		*nargs = ctx.nargs;
	return found;
}

int dashLibLoadArgs(const DashApp *app, char *buf, int buf_size)
{
	DashApp tmp;
	int nargs = 0;

	memcpy(&tmp, app, sizeof(tmp));
	readMetadata(&tmp, buf, buf_size, &nargs);
	return nargs;
}

//--------------------------------------------------------------
// Library management
//--------------------------------------------------------------
void dashLibMakeTransient(DashApp *app, const char *path)
{
	memset(app, 0, sizeof(*app));
	dashStrCopy(app->path, path, sizeof(app->path));
	dashLibPrettyName(path, app->title, sizeof(app->title));
	app->hue = hashHue(path);
	app->flags = DASH_APP_TRANSIENT;
	app->art_state = DASH_ART_UNKNOWN;
}

DashApp *dashLibFind(const char *path)
{
	int i;

	for (i = 0; i < dashAppCount; i++)
		if (!stricmp(dashApps[i].path, path))
			return &dashApps[i];
	return NULL;
}

int dashLibIndexOf(const DashApp *app)
{
	if (app == NULL || app < dashApps || app >= dashApps + dashAppCount)
		return -1;
	return (int)(app - dashApps);
}

DashApp *dashLibResolve(const char *path, DashApp *transient)
{
	DashApp *a = dashLibFind(path);

	if (a != NULL)
		return a;
	if (transient != NULL) {
		dashLibMakeTransient(transient, path);
		return transient;
	}
	return NULL;
}

static int titleCmp(const void *pa, const void *pb)
{
	const DashApp *a = (const DashApp *)pa, *b = (const DashApp *)pb;
	int c = stricmp(a->title, b->title);

	return c ? c : stricmp(a->path, b->path);
}

void dashLibInit(void)
{
	if (lib_ready)
		return;
	dashApps = (DashApp *)malloc(sizeof(DashApp) * DASH_MAX_APPS);
	dashAppCount = 0;
	lib_ready = (dashApps != NULL);
}

void dashLibShutdown(void)
{
}

void dashLibRefreshApp(DashApp *a)
{
	a->title[0] = a->desc[0] = a->author[0] = a->version[0] = a->category[0] = '\0';
	a->flags &= ~(DASH_APP_HAS_META | DASH_APP_HAS_ARGS | DASH_APP_META_PIN | DASH_APP_MISSING);
	a->art[0] = '\0';
	a->art_state = DASH_ART_UNKNOWN;
	dashLibPrettyName(a->path, a->title, sizeof(a->title));
	readMetadata(a, NULL, 0, NULL);
	dashArtForget(a);
	dashLibSaveCache();
}

int dashLibRemove(int index)
{
	if (index < 0 || index >= dashAppCount)
		return -1;
	dashArtForget(&dashApps[index]);
	memmove(&dashApps[index], &dashApps[index + 1], sizeof(DashApp) * (dashAppCount - index - 1));
	dashAppCount--;
	dashLibSaveCache();
	return 0;
}

//--------------------------------------------------------------
// Cache file
//--------------------------------------------------------------
static int nextField(char **p, char *out, int out_size)
{
	char *s = *p, *e;
	int n;

	if (s == NULL)
		return 0;
	e = strchr(s, '|');
	n = e ? (int)(e - s) : (int)strlen(s);
	if (n >= out_size)
		n = out_size - 1;
	memcpy(out, s, n);
	out[n] = '\0';
	*p = e ? e + 1 : NULL;
	return 1;
}

static int cacheKv(const char *key, char *value, void *user)
{
	char num[16];
	DashApp *a;
	char *p = value;

	(void)user;
	if (stricmp(key, "App") || dashAppCount >= DASH_MAX_APPS)
		return 0;
	a = &dashApps[dashAppCount];
	memset(a, 0, sizeof(*a));
	nextField(&p, a->path, sizeof(a->path));
	if (a->path[0] == '\0' || strchr(a->path, ':') == NULL)
		return 0;
	nextField(&p, a->title, sizeof(a->title));
	nextField(&p, a->author, sizeof(a->author));
	nextField(&p, a->version, sizeof(a->version));
	nextField(&p, a->category, sizeof(a->category));
	nextField(&p, num, sizeof(num));
	a->flags = (u8)(atoi(num) & (DASH_APP_HAS_META | DASH_APP_HAS_ARGS | DASH_APP_META_PIN));
	nextField(&p, num, sizeof(num));
	a->art_state = (u8)atoi(num);
	if (a->art_state > DASH_ART_NONE)
		a->art_state = DASH_ART_UNKNOWN;
	nextField(&p, a->art, sizeof(a->art));
	if (a->art_state == DASH_ART_FOUND && a->art[0] == '\0')
		a->art_state = DASH_ART_UNKNOWN;
	nextField(&p, a->desc, sizeof(a->desc));
	if (a->title[0] == '\0')
		dashLibPrettyName(a->path, a->title, sizeof(a->title));
	a->hue = hashHue(a->path);
	dashAppCount++;
	return 0;
}

int dashLibLoadCache(void)
{
	char path[MAX_PATH];
	char *text;
	int len;

	if (!lib_ready)
		return -1;
	dashAppCount = 0;
	if (dashStoreBuildPath(path, sizeof(path), "DASHLIB.CNF") < 0)
		return -1;
	text = dashLoadTextFile(path, 512 * 1024, &len);
	if (text == NULL)
		return -1;
	dashKvParse(text, cacheKv, NULL);
	free(text);
	qsort(dashApps, dashAppCount, sizeof(DashApp), titleCmp);
	return dashAppCount;
}

int dashLibSaveCache(void)
{
	char path[MAX_PATH];
	char *buf;
	int cap, len = 0, i, ret;

	if (!lib_ready || dashStoreBuildPath(path, sizeof(path), "DASHLIB.CNF") < 0)
		return -1;
	cap = 128 + dashAppCount * (int)(sizeof(DashApp) + 32);
	buf = (char *)malloc(cap);
	if (buf == NULL)
		return -1;
	len += snprintf(buf + len, cap - len, "# wLaunchELF dashboard application cache (safe to delete)\r\n");
	for (i = 0; i < dashAppCount; i++) {
		DashApp *a = &dashApps[i];
		if (a->flags & DASH_APP_TRANSIENT)
			continue;
		len += snprintf(buf + len, cap - len, "App = %s|%s|%s|%s|%s|%d|%d|%s|%s\r\n",
		                a->path, a->title, a->author, a->version, a->category,
		                a->flags & (DASH_APP_HAS_META | DASH_APP_HAS_ARGS | DASH_APP_META_PIN),
		                (a->art_state == DASH_ART_FOUND || a->art_state == DASH_ART_NONE) ? a->art_state : 0,
		                a->art, a->desc);
		if (len >= cap - 1)
			break;
	}
	ret = dashWriteFile(path, buf, len);
	free(buf);
	return ret;
}

//--------------------------------------------------------------
// Scanning
//--------------------------------------------------------------
typedef struct
{
	DashApp *old;
	int nold;
	DashApp *fresh;
	int nfresh;
	FILEINFO *files;
	DashProgressFn progress;
	int dirs_done;
	int dirs_total;
} ScanCtx;

#define SCAN_MAX_SUBDIRS 48

static int isElfName(const char *name)
{
	return genCmpFileExt(name, "ELF");
}

static void scanAddElf(ScanCtx *sc, const char *dir, const char *name)
{
	DashApp *a;
	int i;
	char full[MAX_PATH];

	if (sc->nfresh >= DASH_MAX_APPS)
		return;
	if (snprintf(full, sizeof(full), "%s%s", dir, name) >= DASH_PATH_MAX)
		return;
	for (i = 0; i < sc->nfresh; i++)
		if (!stricmp(sc->fresh[i].path, full))
			return;

	a = &sc->fresh[sc->nfresh];
	memset(a, 0, sizeof(*a));
	dashStrCopy(a->path, full, sizeof(a->path));
	/* keep previously resolved artwork across rescans */
	for (i = 0; i < sc->nold; i++) {
		if (!stricmp(sc->old[i].path, full)) {
			if (sc->old[i].art_state == DASH_ART_FOUND) {
				dashStrCopy(a->art, sc->old[i].art, sizeof(a->art));
				a->art_state = DASH_ART_FOUND;
			}
			break;
		}
	}
	dashLibPrettyName(full, a->title, sizeof(a->title));
	a->hue = hashHue(full);
	readMetadata(a, NULL, 0, NULL);
	sc->nfresh++;
}

static void scanDir(ScanCtx *sc, const char *dir, int depth)
{
	char subdirs[SCAN_MAX_SUBDIRS][MAX_NAME];
	int nsub = 0, n, i;
	char status[DASH_PATH_MAX + 32];

	if (sc->progress) {
		snprintf(status, sizeof(status), "%s", dir);
		sc->progress(status, sc->dirs_done, sc->dirs_total);
	}
	n = getDir(dir, sc->files);
	for (i = 0; i < n && i < MAX_ENTRY - 2; i++) {
		FILEINFO *f = &sc->files[i];

		if (f->name[0] == '\0' || f->name[0] == '.')
			continue;
		if (f->stats.AttrFile & sceMcFileAttrSubdir) {
			if (depth < 1 && nsub < SCAN_MAX_SUBDIRS && strlen(f->name) < MAX_NAME - 2)
				dashStrCopy(subdirs[nsub++], f->name, MAX_NAME);
		} else if (isElfName(f->name))
			scanAddElf(sc, dir, f->name);
	}
	for (i = 0; i < nsub; i++) {
		char sub[MAX_PATH];
		if (snprintf(sub, sizeof(sub), "%s%s/", dir, subdirs[i]) < DASH_PATH_MAX - 16)
			scanDir(sc, sub, depth + 1);
	}
}

/* Devices whose driver stack is already running can be listed at boot
 * without side effects. Anything else waits for an explicit scan. */
static int cheapToScan(const char *dir)
{
	if (!strncmp(dir, "mc", 2))
		return 1;
	return isPathDeviceStackLoaded(dir);
}

static int prefixMatchNoCase(const char *s, const char *prefix)
{
	while (*prefix) {
		if (wle_ascii_tolower((unsigned char)*s) != wle_ascii_tolower((unsigned char)*prefix))
			return 0;
		s++;
		prefix++;
	}
	return 1;
}

static int dirPrefixMatch(const char *path, char dirs[][DASH_PATH_MAX], int ndirs)
{
	int i;

	for (i = 0; i < ndirs; i++)
		if (prefixMatchNoCase(path, dirs[i]))
			return 1;
	return 0;
}

int dashLibScan(int interactive, DashProgressFn progress)
{
	char dirs[DASH_MAX_SCAN * 2][DASH_PATH_MAX];
	int ndirs = 0, i;
	ScanCtx sc;

	if (!lib_ready)
		return -1;

	for (i = 0; i < dashCfg.nscan; i++) {
		const char *s = dashCfg.scan[i];
		char d[DASH_PATH_MAX];
		int len;

		dashStrCopy(d, s, sizeof(d));
		len = strlen(d);
		if (len == 0 || len >= DASH_PATH_MAX - 2)
			continue;
		if (d[len - 1] != '/' && d[len - 1] != ':')
			strcat(d, "/");
		if (!strncmp(d, "mc:", 3)) {
			snprintf(dirs[ndirs++], DASH_PATH_MAX, "mc0:%.*s", DASH_PATH_MAX - 8, d + 3);
			snprintf(dirs[ndirs++], DASH_PATH_MAX, "mc1:%.*s", DASH_PATH_MAX - 8, d + 3);
		} else
			dashStrCopy(dirs[ndirs++], d, DASH_PATH_MAX);
	}

	memset(&sc, 0, sizeof(sc));
	sc.progress = progress;
	sc.old = dashApps;
	sc.nold = dashAppCount;
	sc.fresh = (DashApp *)malloc(sizeof(DashApp) * DASH_MAX_APPS);
	sc.files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	if (sc.fresh == NULL || sc.files == NULL) {
		free(sc.fresh);
		free(sc.files);
		return -1;
	}
	sc.dirs_total = ndirs;

	{
		char scanned[DASH_MAX_SCAN * 2][DASH_PATH_MAX];
		int nscanned = 0;

		for (i = 0; i < ndirs; i++) {
			sc.dirs_done = i;
			if (!interactive && !cheapToScan(dirs[i]))
				continue;
			if (!strncmp(dirs[i], "mc", 2) && !dashMcPresent(dirs[i][2] - '0'))
				continue;
			scanDir(&sc, dirs[i], 0);
			dashStrCopy(scanned[nscanned++], dirs[i], DASH_PATH_MAX);
		}

		/* Keep cached entries that live on locations we did not look at
		 * (e.g. USB not yet initialised during a boot-time scan). */
		for (i = 0; i < sc.nold && sc.nfresh < DASH_MAX_APPS; i++) {
			DashApp *o = &sc.old[i];
			int j, dup = 0;

			if (o->flags & DASH_APP_TRANSIENT)
				continue;
			if (dirPrefixMatch(o->path, scanned, nscanned))
				continue;
			for (j = 0; j < sc.nfresh; j++)
				if (!stricmp(sc.fresh[j].path, o->path))
					dup = 1;
			if (!dup)
				sc.fresh[sc.nfresh++] = *o;
		}
	}

	free(sc.files);
	dashArtFlushAll();
	memcpy(dashApps, sc.fresh, sizeof(DashApp) * sc.nfresh);
	dashAppCount = sc.nfresh;
	free(sc.fresh);
	qsort(dashApps, dashAppCount, sizeof(DashApp), titleCmp);
	if (progress)
		progress("Saving library", ndirs, ndirs);
	dashLibSaveCache();
	return dashAppCount;
}
//--------------------------------------------------------------
//End of file: dash_lib.c
//--------------------------------------------------------------
