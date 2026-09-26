//--------------------------------------------------------------
//File name:   dash_store.c
//Dashboard persistence.
//
//The dashboard never modifies LAUNCHELF.CNF on its own. Its state lives in
//separate files next to the loaded configuration (or mc0:/SYS-CONF/):
//  DASHBOARD.CNF  settings and application scan locations
//  DASHAPPS.CNF   favorites, pinned apps and recently launched apps
//  DASHLIB.CNF    cached application library (dash_lib.c)
//Older wLaunchELF builds simply ignore these files, missing keys fall back
//to defaults, and unknown keys are skipped.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "config_private.h"

DashSettings dashCfg;
DashUserLists dashLists;

static char store_dir[MAX_PATH];
static int store_loaded;
static int store_initialized;

static const char *const default_scan[] = {
    "mc:/APPS/",
    "mc:/BOOT/",
    "mass:/APPS/",
    "mass:/BOOT/"};

//--------------------------------------------------------------
// Small string / file helpers
//--------------------------------------------------------------
void dashStrCopy(char *dst, const char *src, int dst_size)
{
	int i;

	if (dst == NULL || dst_size <= 0)
		return;
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	for (i = 0; i < dst_size - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

char *dashTrim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = '\0';
	return s;
}

int dashKvParse(char *text, DashKvCallback cb, void *user)
{
	char *line = text;
	int count = 0;

	if (text == NULL)
		return 0;
	/* Skip a UTF-8 BOM written by some PC editors. */
	if ((u8)line[0] == 0xEF && (u8)line[1] == 0xBB && (u8)line[2] == 0xBF)
		line += 3;
	while (*line) {
		char *next = line;
		char *eq, *key, *val;

		while (*next && *next != '\n' && *next != '\r')
			next++;
		if (*next) {
			*next++ = '\0';
			while (*next == '\n' || *next == '\r')
				next++;
		}
		key = dashTrim(line);
		if (key[0] != '\0' && key[0] != '#' && key[0] != ';' && key[0] != '[' && !(key[0] == '/' && key[1] == '/')) {
			eq = strchr(key, '=');
			if (eq != NULL) {
				*eq = '\0';
				val = dashTrim(eq + 1);
				key = dashTrim(key);
				if (key[0] != '\0') {
					count++;
					if (cb(key, val, user))
						break;
				}
			}
		}
		line = next;
	}
	return count;
}

char *dashLoadTextFile(const char *path, int max_size, int *out_len)
{
	char fixed[MAX_PATH];
	int fd, size, rd, total;
	char *buf;

	if (out_len != NULL)
		*out_len = 0;
	if (path == NULL || path[0] == '\0')
		return NULL;
	if (genFixPath(path, fixed) < 0)
		return NULL;
	fd = genOpen(fixed, FIO_O_RDONLY);
	if (fd < 0)
		return NULL;
	size = (int)genLseek(fd, 0, SEEK_END);
	if (size <= 0 || size > max_size || genLseek(fd, 0, SEEK_SET) < 0) {
		genClose(fd);
		return NULL;
	}
	buf = (char *)memalign(64, size + 1);
	if (buf == NULL) {
		genClose(fd);
		return NULL;
	}
	total = 0;
	while (total < size) {
		rd = genRead(fd, buf + total, size - total);
		if (rd <= 0)
			break;
		total += rd;
	}
	genClose(fd);
	if (total <= 0) {
		free(buf);
		return NULL;
	}
	buf[total] = '\0';
	if (out_len != NULL)
		*out_len = total;
	return buf;
}

int dashWriteFile(const char *path, const void *data, int size)
{
	char fixed[MAX_PATH];
	int fd, wr;

	if (genFixPath(path, fixed) < 0)
		return -1;
	configEnsureSysconfDir(fixed);
	fd = genOpen(fixed, FIO_O_CREAT | FIO_O_WRONLY | FIO_O_TRUNC);
	if (fd < 0)
		return -1;
	wr = genWrite(fd, (void *)data, size);
	genClose(fd);
	return (wr == size) ? 0 : -1;
}

//--------------------------------------------------------------
// Storage location
//--------------------------------------------------------------
static int isWritableDevice(const char *path)
{
	if (path == NULL || path[0] == '\0')
		return 0;
	return (!strncmp(path, "mc", 2) || !strncmp(path, "mass", 4) || !strncmp(path, "usb", 3) ||
	        !strncmp(path, "hdd", 3) || !strncmp(path, "mmce", 4) || !strncmp(path, "mx4sio", 6) ||
	        !strncmp(path, "ata", 3));
}

static void dirOf(const char *path, char *out, int out_size)
{
	const char *slash;

	dashStrCopy(out, path, out_size);
	slash = strrchr(out, '/');
	if (slash != NULL)
		out[slash - out + 1] = '\0';
	else {
		char *colon = strchr(out, ':');
		if (colon != NULL)
			colon[1] = '\0';
	}
}

static int candidateDirs(char dirs[3][MAX_PATH])
{
	int n = 0;

	if (LoadedConfigPath[0] && isWritableDevice(LoadedConfigPath))
		dirOf(LoadedConfigPath, dirs[n++], MAX_PATH);
	if (LaunchElfDir[0] && isWritableDevice(LaunchElfDir) && (n == 0 || strcmp(dirs[0], LaunchElfDir)))
		dashStrCopy(dirs[n++], LaunchElfDir, MAX_PATH);
	dashStrCopy(dirs[n++], "mc0:/SYS-CONF/", MAX_PATH);
	return n;
}

const char *dashStoreDir(void)
{
	return store_dir;
}

int dashStoreBuildPath(char *out, int out_size, const char *file)
{
	int len;

	if (store_dir[0] == '\0')
		return -1;
	len = snprintf(out, out_size, "%s%s", store_dir, file);
	return (len > 0 && len < out_size) ? 0 : -1;
}

//--------------------------------------------------------------
// Settings
//--------------------------------------------------------------
void dashStoreDefaults(void)
{
	int i;

	memset(&dashCfg, 0, sizeof(dashCfg));
	dashCfg.enabled = 1;
	dashCfg.start_section = DASH_SEC_HOME;
	dashCfg.view = DASH_VIEW_GRID;
	dashCfg.effects = DASH_FX_FULL;
	dashCfg.anim = DASH_ANIM_FULL;
	dashCfg.autoscan = 1;
	dashCfg.artwork = 1;
	dashCfg.recent_count = 8;
	dashCfg.legacy_theme = 1;
	dashCfg.intro_done = 0;
	dashCfg.sound = 1;
	dashCfg.sound_vol = 80;
	for (i = 0; i < (int)(sizeof(default_scan) / sizeof(default_scan[0])); i++)
		dashStrCopy(dashCfg.scan[dashCfg.nscan++], default_scan[i], DASH_PATH_MAX);
}

static int clampInt(const char *v, int lo, int hi, int def)
{
	int x;

	if (v == NULL || v[0] == '\0')
		return def;
	x = atoi(v);
	if (x < lo || x > hi)
		return def;
	return x;
}

static int settingsKv(const char *key, char *value, void *user)
{
	int *scan_reset = (int *)user;

	if (!stricmp(key, "Dash_Enabled"))
		dashCfg.enabled = clampInt(value, 0, 1, 1);
	else if (!stricmp(key, "Dash_StartSection"))
		dashCfg.start_section = clampInt(value, 0, DASH_SEC_COUNT - 1, DASH_SEC_HOME);
	else if (!stricmp(key, "Dash_View"))
		dashCfg.view = clampInt(value, 0, DASH_VIEW_COUNT - 1, DASH_VIEW_GRID);
	else if (!stricmp(key, "Dash_Effects"))
		dashCfg.effects = clampInt(value, 0, DASH_FX_COUNT - 1, DASH_FX_FULL);
	else if (!stricmp(key, "Dash_Animations"))
		dashCfg.anim = clampInt(value, 0, DASH_ANIM_COUNT - 1, DASH_ANIM_FULL);
	else if (!stricmp(key, "Dash_AutoScan"))
		dashCfg.autoscan = clampInt(value, 0, 1, 1);
	else if (!stricmp(key, "Dash_Artwork"))
		dashCfg.artwork = clampInt(value, 0, 1, 1);
	else if (!stricmp(key, "Dash_RecentCount"))
		dashCfg.recent_count = clampInt(value, 0, DASH_MAX_RECENT, 8);
	else if (!stricmp(key, "Dash_LegacyTheme"))
		dashCfg.legacy_theme = clampInt(value, 0, 1, 1);
	else if (!stricmp(key, "Dash_IntroDone"))
		dashCfg.intro_done = clampInt(value, 0, 1, 0);
	else if (!stricmp(key, "Dash_Sound"))
		dashCfg.sound = clampInt(value, 0, 1, 1);
	else if (!stricmp(key, "Dash_SoundVolume"))
		dashCfg.sound_vol = clampInt(value, 0, 100, 80);
	else if (dashThemeStoreKey(key, value))
		;
	else if (!stricmp(key, "Dash_Scan")) {
		if (!*scan_reset) {
			dashCfg.nscan = 0;
			*scan_reset = 1;
		}
		if (value[0] != '\0' && dashCfg.nscan < DASH_MAX_SCAN && strlen(value) < DASH_PATH_MAX)
			dashStrCopy(dashCfg.scan[dashCfg.nscan++], value, DASH_PATH_MAX);
	}
	return 0;
}

static int listsKv(const char *key, char *value, void *user)
{
	(void)user;
	if (value[0] == '\0' || strlen(value) >= DASH_PATH_MAX)
		return 0;
	if (!stricmp(key, "Favorite") && dashLists.nfav < DASH_MAX_FAV)
		dashStrCopy(dashLists.fav[dashLists.nfav++], value, DASH_PATH_MAX);
	else if (!stricmp(key, "Pinned") && dashLists.npin < DASH_MAX_FAV)
		dashStrCopy(dashLists.pin[dashLists.npin++], value, DASH_PATH_MAX);
	else if (!stricmp(key, "Recent") && dashLists.nrecent < DASH_MAX_RECENT)
		dashStrCopy(dashLists.recent[dashLists.nrecent++], value, DASH_PATH_MAX);
	return 0;
}

void dashStoreInit(void)
{
	char dirs[3][MAX_PATH];
	char path[MAX_PATH];
	int n, i, len;
	char *text;
	int scan_reset = 0;

	if (store_initialized)
		return;
	store_initialized = 1;
	dashStoreDefaults();
	memset(&dashLists, 0, sizeof(dashLists));
	store_dir[0] = '\0';
	dashThemeStoreBegin();

	n = candidateDirs(dirs);
	for (i = 0; i < n; i++) {
		if (snprintf(path, sizeof(path), "%.*sDASHBOARD.CNF", (int)sizeof(path) - 16, dirs[i]) >= (int)sizeof(path))
			continue;
		text = dashLoadTextFile(path, 64 * 1024, &len);
		if (text != NULL) {
			dashStrCopy(store_dir, dirs[i], sizeof(store_dir));
			dashKvParse(text, settingsKv, &scan_reset);
			free(text);
			store_loaded = 1;
			break;
		}
	}
	if (store_dir[0] == '\0')
		dashStrCopy(store_dir, dirs[0], sizeof(store_dir));
	dashThemeStoreEnd();

	if (dashStoreBuildPath(path, sizeof(path), "DASHAPPS.CNF") == 0) {
		text = dashLoadTextFile(path, 64 * 1024, &len);
		if (text != NULL) {
			dashKvParse(text, listsKv, NULL);
			free(text);
		}
	}
}

int dashStoreLoaded(void)
{
	return store_loaded;
}

int dashStoreSaveSettings(void)
{
	char path[MAX_PATH];
	char *buf;
	int len = 0, i, ret;
	const int cap = 2048 + DASH_MAX_SCAN * (DASH_PATH_MAX + 16) + 4096;

	if (dashStoreBuildPath(path, sizeof(path), "DASHBOARD.CNF") < 0)
		return -1;
	buf = (char *)malloc(cap);
	if (buf == NULL)
		return -1;
	len += snprintf(buf + len, cap - len,
	                "# wLaunchELF dashboard settings (LAUNCHELF.CNF is not modified)\r\n"
	                "Dash_Enabled = %d\r\n"
	                "Dash_StartSection = %d\r\n"
	                "Dash_View = %d\r\n"
	                "Dash_Effects = %d\r\n"
	                "Dash_Animations = %d\r\n"
	                "Dash_AutoScan = %d\r\n"
	                "Dash_Artwork = %d\r\n"
	                "Dash_RecentCount = %d\r\n"
	                "Dash_LegacyTheme = %d\r\n"
	                "Dash_IntroDone = %d\r\n"
	                "Dash_Sound = %d\r\n"
	                "Dash_SoundVolume = %d\r\n",
	                dashCfg.enabled, dashCfg.start_section, dashCfg.view, dashCfg.effects,
	                dashCfg.anim, dashCfg.autoscan, dashCfg.artwork, dashCfg.recent_count,
	                dashCfg.legacy_theme, dashCfg.intro_done, dashCfg.sound, dashCfg.sound_vol);
	for (i = 0; i < dashCfg.nscan && len < cap - DASH_PATH_MAX - 16; i++)
		len += snprintf(buf + len, cap - len, "Dash_Scan = %s\r\n", dashCfg.scan[i]);
	/* theme: preset name, plus a full snapshot once customised */
	len += dashThemeStoreWrite(buf + len, cap - len);
	ret = dashWriteFile(path, buf, len);
	free(buf);
	if (ret == 0)
		store_loaded = 1;
	return ret;
}

int dashStoreSaveLists(void)
{
	char path[MAX_PATH];
	char *buf;
	int len = 0, i, ret;
	const int cap = 256 + (DASH_MAX_FAV * 2 + DASH_MAX_RECENT) * (DASH_PATH_MAX + 16);

	if (dashStoreBuildPath(path, sizeof(path), "DASHAPPS.CNF") < 0)
		return -1;
	buf = (char *)malloc(cap);
	if (buf == NULL)
		return -1;
	len += snprintf(buf + len, cap - len, "# wLaunchELF dashboard: favorites, pinned and recent apps\r\n");
	for (i = 0; i < dashLists.npin; i++)
		len += snprintf(buf + len, cap - len, "Pinned = %s\r\n", dashLists.pin[i]);
	for (i = 0; i < dashLists.nfav; i++)
		len += snprintf(buf + len, cap - len, "Favorite = %s\r\n", dashLists.fav[i]);
	for (i = 0; i < dashLists.nrecent; i++)
		len += snprintf(buf + len, cap - len, "Recent = %s\r\n", dashLists.recent[i]);
	ret = dashWriteFile(path, buf, len);
	free(buf);
	return ret;
}

//--------------------------------------------------------------
// Favorites / pins / recents
//--------------------------------------------------------------
static int listFind(char list[][DASH_PATH_MAX], int n, const char *path)
{
	int i;

	for (i = 0; i < n; i++)
		if (!stricmp(list[i], path))
			return i;
	return -1;
}

static void listRemoveAt(char list[][DASH_PATH_MAX], int *n, int idx)
{
	int i;

	if (idx < 0 || idx >= *n)
		return;
	for (i = idx; i < *n - 1; i++)
		memcpy(list[i], list[i + 1], DASH_PATH_MAX);
	(*n)--;
}

int dashStoreIsFavorite(const char *path)
{
	return listFind(dashLists.fav, dashLists.nfav, path) >= 0;
}

int dashStoreIsPinned(const char *path)
{
	return listFind(dashLists.pin, dashLists.npin, path) >= 0;
}

int dashStoreToggleFavorite(const char *path)
{
	int idx = listFind(dashLists.fav, dashLists.nfav, path);

	if (idx >= 0)
		listRemoveAt(dashLists.fav, &dashLists.nfav, idx);
	else if (dashLists.nfav < DASH_MAX_FAV && strlen(path) < DASH_PATH_MAX)
		dashStrCopy(dashLists.fav[dashLists.nfav++], path, DASH_PATH_MAX);
	else
		return -1;
	dashStoreSaveLists();
	return idx < 0;
}

int dashStoreTogglePinned(const char *path)
{
	int idx = listFind(dashLists.pin, dashLists.npin, path);

	if (idx >= 0)
		listRemoveAt(dashLists.pin, &dashLists.npin, idx);
	else if (dashLists.npin < DASH_MAX_FAV && strlen(path) < DASH_PATH_MAX)
		dashStrCopy(dashLists.pin[dashLists.npin++], path, DASH_PATH_MAX);
	else
		return -1;
	dashStoreSaveLists();
	return idx < 0;
}

void dashStoreRemoveEverywhere(const char *path)
{
	listRemoveAt(dashLists.fav, &dashLists.nfav, listFind(dashLists.fav, dashLists.nfav, path));
	listRemoveAt(dashLists.pin, &dashLists.npin, listFind(dashLists.pin, dashLists.npin, path));
	listRemoveAt(dashLists.recent, &dashLists.nrecent, listFind(dashLists.recent, dashLists.nrecent, path));
	dashStoreSaveLists();
}

void dashStorePushRecent(const char *path)
{
	int idx, i;

	if (path == NULL || strlen(path) >= DASH_PATH_MAX)
		return;
	idx = listFind(dashLists.recent, dashLists.nrecent, path);
	if (idx < 0) {
		idx = (dashLists.nrecent < DASH_MAX_RECENT) ? dashLists.nrecent++ : DASH_MAX_RECENT - 1;
	}
	for (i = idx; i > 0; i--)
		memcpy(dashLists.recent[i], dashLists.recent[i - 1], DASH_PATH_MAX);
	dashStrCopy(dashLists.recent[0], path, DASH_PATH_MAX);
	dashStoreSaveLists();
}

void dashStoreClearRecent(void)
{
	dashLists.nrecent = 0;
	dashStoreSaveLists();
}
//--------------------------------------------------------------
//End of file: dash_store.c
//--------------------------------------------------------------
