//--------------------------------------------------------------
//File name:   dash_art.c
//Bounded artwork cache for the dashboard.
//
//* At most ART_SLOTS decoded thumbnails (<= 128x128 CT32, 64KB each) are
//  kept in EE RAM; their VRAM copies are managed by gsKit's texture manager
//  and only exist while they are being drawn.
//* Views request artwork every frame with a priority (0 = focused item).
//  dashArtStep() performs at most ONE file probe or ONE PNG decode per
//  frame, and only once the pad has been idle for a moment, so fast
//  scrolling never stalls on IO; the fallback tile is shown meanwhile.
//* Missing artwork is remembered (art_state = NONE) and cached on disk.
//
//Artwork resolution order for <dir>/<NAME>.ELF (first hit wins):
//  1. cover= from the metadata sidecar
//  2. <dir>/<NAME>.ELF_COV.png   (OPL naming, e.g. OPNPS2LD.ELF_COV.png)
//  3. <dir>/<NAME>_COV.png, <dir>/<NAME>.png
//  4. <dir>/cover.png, <dir>/icon.png (only for per-app folders)
//  5. <dir>/<NAME>_ICO.png       (OPL disc icon)
//  6. <device>/ART/<NAME>.ELF_COV.png, <device>/ART/<NAME>_COV.png
//     (OPL ART folder; +OPL partition on the internal HDD)
//  7. <dashboard dir>/ART/<NAME>.ELF_COV.png, <NAME>_COV.png, <NAME>.png
//  8. procedural tile (always available)
//--------------------------------------------------------------
#include "dash_internal.h"
#include "init.h"

#define ART_SLOTS 40
#define ART_MAX_DIM 128
#define ART_WANTS 48
#define ART_MAX_CANDIDATES 14

typedef struct
{
	char key[DASH_PATH_MAX];
	GSTEXTURE tex;
	u32 last_frame;
	u8 used;
} ArtSlot;

typedef struct
{
	DashApp *app;
	int prio;
} ArtWant;

static ArtSlot slots[ART_SLOTS];
static ArtWant wants[ART_WANTS];
static int nwants;
static u32 art_frame;

static int art_dirty;

//--------------------------------------------------------------
static void slotFree(ArtSlot *s)
{
	if (s->used) {
		dashTexRelease(&s->tex);
		free(s->tex.Mem);
	}
	memset(s, 0, sizeof(*s));
}

void dashArtInit(void)
{
	memset(slots, 0, sizeof(slots));
	nwants = 0;
	art_frame = 1;
}

void dashArtShutdown(void)
{
	dashArtFlushAll();
}

void dashArtFlushAll(void)
{
	int i;

	for (i = 0; i < ART_SLOTS; i++)
		slotFree(&slots[i]);
	nwants = 0;
}

void dashArtInvalidateVram(void)
{
	int i;

	for (i = 0; i < ART_SLOTS; i++)
		if (slots[i].used) {
			slots[i].tex.Vram = 0;
			slots[i].tex.VramClut = 0;
		}
}

void dashArtForget(const DashApp *app)
{
	int i;

	for (i = 0; i < ART_SLOTS; i++)
		if (slots[i].used && !stricmp(slots[i].key, app->path))
			slotFree(&slots[i]);
}

void dashArtBeginFrame(void)
{
	art_frame++;
	nwants = 0;
}

static ArtSlot *findSlot(const char *key)
{
	int i;

	for (i = 0; i < ART_SLOTS; i++)
		if (slots[i].used && !stricmp(slots[i].key, key))
			return &slots[i];
	return NULL;
}

GSTEXTURE *dashArtGet(DashApp *app, int prio)
{
	ArtSlot *s;
	int i;

	if (app == NULL || !dashCfg.artwork || app->art_state == DASH_ART_NONE)
		return NULL;
	s = findSlot(app->path);
	if (s != NULL) {
		s->last_frame = art_frame;
		return &s->tex;
	}
	for (i = 0; i < nwants; i++) {
		if (wants[i].app == app) {
			if (prio < wants[i].prio)
				wants[i].prio = prio;
			return NULL;
		}
	}
	if (nwants < ART_WANTS) {
		wants[nwants].app = app;
		wants[nwants].prio = prio;
		nwants++;
	}
	return NULL;
}

//--------------------------------------------------------------
// Probing
//--------------------------------------------------------------
static int fileExists(const char *path)
{
	char fixed[MAX_PATH];
	int fd;

	if (genFixPath(path, fixed) < 0)
		return 0;
	fd = genOpen(fixed, FIO_O_RDONLY);
	if (fd < 0)
		return 0;
	genClose(fd);
	return 1;
}

static int isScanRoot(const char *dir)
{
	int i;

	for (i = 0; i < dashCfg.nscan; i++) {
		const char *s = dashCfg.scan[i];
		if (!strncmp(s, "mc:", 3) && !strncmp(dir, "mc", 2) && dir[3] == ':') {
			if (!stricmp(s + 3, dir + 4))
				return 1;
		} else if (!stricmp(s, dir))
			return 1;
	}
	return 0;
}

static void deviceRoot(const char *path, char *root, int size)
{
	const char *colon = strchr(path, ':');
	int n;

	root[0] = '\0';
	if (colon == NULL)
		return;
	if (!strncmp(path, "hdd", 3)) {
		/* OPL keeps HDD artwork in the +OPL partition */
		snprintf(root, size, "%.*s:/+OPL/", (int)(colon - path), path);
		return;
	}
	n = (int)(colon - path) + 1;
	if (n >= size - 2)
		return;
	memcpy(root, path, n);
	root[n] = '\0';
	strcat(root, "/");
}

/* Builds candidate #idx; returns 0 when the list is exhausted. */
static int buildCandidate(const DashApp *app, int idx, char *out, int size)
{
	char dir[DASH_PATH_MAX], file[DASH_PATH_MAX], base[DASH_PATH_MAX], root[DASH_PATH_MAX];
	const char *slash = strrchr(app->path, '/');
	const char *name = slash ? slash + 1 : app->path;
	char *dot;
	int n = (int)(name - app->path);

	if (n >= (int)sizeof(dir))
		return 0;
	memcpy(dir, app->path, n);
	dir[n] = '\0';
	dashStrCopy(file, name, sizeof(file));
	dashStrCopy(base, name, sizeof(base));
	dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';

	out[0] = '\0';
	switch (idx) {
		case 0:
			if (app->art[0])
				dashStrCopy(out, app->art, size);
			break;
		case 1:
			snprintf(out, size, "%s%s_COV.png", dir, file);
			break;
		case 2:
			snprintf(out, size, "%s%s_COV.png", dir, base);
			break;
		case 3:
			snprintf(out, size, "%s%s.png", dir, base);
			break;
		case 4:
			if (!isScanRoot(dir))
				snprintf(out, size, "%scover.png", dir);
			break;
		case 5:
			if (!isScanRoot(dir))
				snprintf(out, size, "%sicon.png", dir);
			break;
		case 6:
			snprintf(out, size, "%s%s_ICO.png", dir, base);
			break;
		case 7:
			deviceRoot(app->path, root, sizeof(root));
			if (root[0])
				snprintf(out, size, "%sART/%s_COV.png", root, file);
			break;
		case 8:
			deviceRoot(app->path, root, sizeof(root));
			if (root[0])
				snprintf(out, size, "%sART/%s_COV.png", root, base);
			break;
		case 9:
			if (dashStoreDir()[0])
				snprintf(out, size, "%sART/%s_COV.png", dashStoreDir(), file);
			break;
		case 10:
			if (dashStoreDir()[0])
				snprintf(out, size, "%sART/%s_COV.png", dashStoreDir(), base);
			break;
		case 11:
			if (dashStoreDir()[0])
				snprintf(out, size, "%sART/%s.png", dashStoreDir(), base);
			break;
		default:
			return 0;
	}
	return 1;
}

static ArtSlot *allocSlot(void)
{
	ArtSlot *best = NULL;
	int i;

	for (i = 0; i < ART_SLOTS; i++) {
		if (!slots[i].used)
			return &slots[i];
		/* never evict something drawn this frame or the previous one */
		if (slots[i].last_frame + 1 >= art_frame)
			continue;
		if (best == NULL || slots[i].last_frame < best->last_frame)
			best = &slots[i];
	}
	if (best != NULL)
		slotFree(best);
	return best;
}

/* Artwork must never be the reason a device driver gets loaded (or an IOP
 * driver stack gets switched). Paths on devices whose stack is not running
 * are skipped until the user opens that device. */
static int artPathAllowed(const char *path)
{
	return path != NULL && path[0] != '\0' && isPathDeviceStackLoaded(path);
}

/* An app whose own device is not running can only use the dashboard ART
 * folder (candidates 9..11, on an already-mounted device). */
static int appDeviceReady(const DashApp *app)
{
	return isPathDeviceStackLoaded(app->path);
}

static int wantUsable(const DashApp *app)
{
	if (app->art_state == DASH_ART_FOUND)
		return artPathAllowed(app->art);
	if (app->art_state == DASH_ART_UNKNOWN)
		return appDeviceReady(app) || !(app->flags & DASH_APP_ART_DEFERRED);
	return 0;
}

//--------------------------------------------------------------
// Background loading. The UI thread picks the most wanted application;
// the worker probes the candidate paths and decodes the PNG; the result
// is applied on the UI thread (the app is matched again by path, since
// the library may have been rebuilt in the meantime).
//--------------------------------------------------------------
enum {
	ART_R_DECODED = 0, /* art[] + pixels */
	ART_R_NONE,        /* no usable artwork anywhere */
	ART_R_DEFERRED     /* the app's own device is not running yet */
};

typedef struct
{
	DashApp *app;
	DashApp copy;
	int result;
	char art[DASH_PATH_MAX];
	u32 *pixels;
	int w, h;
} ArtJob;

static ArtJob *art_job; /* at most one in flight */

static int tryDecode(ArtJob *j, const char *path)
{
	if (dashPngLoadFile(path, ART_MAX_DIM, ART_MAX_DIM, &j->pixels, &j->w, &j->h) != 0) {
		j->pixels = NULL;
		return 0;
	}
	dashStrCopy(j->art, path, sizeof(j->art));
	j->result = ART_R_DECODED;
	return 1;
}

static void artRun(void *arg)
{
	ArtJob *j = (ArtJob *)arg;
	const char *known = (j->copy.art_state == DASH_ART_FOUND) ? j->copy.art : NULL;
	int idx;

	dashWorkerStatus(j->copy.title, 0, 0);
	/* a previously found file first; if it became unreadable, probe the rest */
	if (known != NULL && known[0] && tryDecode(j, known))
		return;
	for (idx = (known != NULL) ? 1 : 0;; idx++) {
		char cand[MAX_PATH];

		if (!buildCandidate(&j->copy, idx, cand, sizeof(cand))) {
			/* only the dashboard ART folder could be checked; try the app's
			 * own device once its driver is running */
			j->result = appDeviceReady(&j->copy) ? ART_R_NONE : ART_R_DEFERRED;
			return;
		}
		if (!cand[0] || strlen(cand) >= DASH_PATH_MAX || !artPathAllowed(cand))
			continue;
		if (known != NULL && !stricmp(cand, known))
			continue; /* already failed to decode */
		if (fileExists(cand) && tryDecode(j, cand))
			return;
	}
}

static void artDone(void *arg)
{
	ArtJob *j = (ArtJob *)arg;
	DashApp *app = j->app;

	art_job = NULL;
	/* the entry may have been replaced by a rescan: match by path */
	if (app == NULL || stricmp(app->path, j->copy.path) || !dashCfg.artwork) {
		free(j->pixels);
		free(j);
		return;
	}
	if (j->result == ART_R_DECODED) {
		ArtSlot *s;
		if (app->art_state != DASH_ART_FOUND || stricmp(app->art, j->art)) {
			dashStrCopy(app->art, j->art, sizeof(app->art));
			app->art_state = DASH_ART_FOUND;
			art_dirty = 1;
		}
		s = findSlot(app->path);
		if (s == NULL)
			s = allocSlot();
		if (s == NULL) {
			free(j->pixels); /* cache full of visible items: try again later */
		} else {
			if (s->used)
				slotFree(s);
			s->used = 1;
			dashStrCopy(s->key, app->path, sizeof(s->key));
			s->tex.Width = j->w;
			s->tex.Height = j->h;
			s->tex.PSM = GS_PSM_CT32;
			s->tex.Mem = j->pixels;
			s->tex.Filter = GS_FILTER_LINEAR;
			s->tex.Delayed = 1;
			gsKit_setup_tbw(&s->tex);
			s->last_frame = art_frame;
		}
	} else if (j->result == ART_R_DEFERRED) {
		app->flags |= DASH_APP_ART_DEFERRED;
	} else {
		if (app->art_state != DASH_ART_NONE)
			art_dirty = 1;
		app->art_state = DASH_ART_NONE;
		app->art[0] = '\0';
	}
	free(j);
}

int dashArtStep(int input_idle_ms)
{
	ArtWant *best = NULL;
	ArtJob *j;
	int i;

	if (!dashCfg.artwork || nwants == 0 || art_job != NULL)
		return 0;
	/* artwork never delays saves, scans or device work */
	if (dashWorkerBusy())
		return 0;
	for (i = 0; i < nwants; i++) {
		if (!wantUsable(wants[i].app))
			continue;
		if (best == NULL || wants[i].prio < best->prio)
			best = &wants[i];
	}
	if (best == NULL)
		return 0;
	/* Focused item: load as soon as navigation pauses briefly.
	 * Everything else: only while the user is idle. */
	if (input_idle_ms < (best->prio == 0 ? 110 : 260))
		return 0;

	j = (ArtJob *)malloc(sizeof(ArtJob));
	if (j == NULL)
		return 0;
	memset(j, 0, sizeof(*j));
	j->app = best->app;
	j->copy = *best->app;
	j->result = ART_R_NONE;
	art_job = j;
	if (dashWorkerSubmit("Loading artwork", DASH_JOB_ART | DASH_JOB_VISIBLE, artRun, artDone, j) < 0) {
		art_job = NULL;
		free(j);
		return 0;
	}
	return 1;
}

int dashArtTakeDirty(void)
{
	int d = art_dirty;

	art_dirty = 0;
	return d;
}

//--------------------------------------------------------------
// Theme textures: optional PNGs in <dashboard dir>/THEME/
//--------------------------------------------------------------
#define THEME_SLOTS 8
enum {
	TS_EMPTY = 0,
	TS_LOADED,
	TS_MISSING,
	TS_LOADING
};

typedef struct
{
	char path[DASH_PATH_MAX];
	GSTEXTURE tex;
	int max_w, max_h; /* decode bounds are part of the cache identity */
	u8 state;
	u16 gen; /* bumped when the slot is freed, so a stale load is dropped */
} ThemeSlot;

typedef struct
{
	int slot;
	u16 gen;
	char path[DASH_PATH_MAX];
	int max_w, max_h;
	u32 *pixels;
	int w, h, ok;
} ThemeTexJob;

static ThemeSlot theme_slots[THEME_SLOTS];

static void themeTexRun(void *arg)
{
	ThemeTexJob *j = (ThemeTexJob *)arg;

	j->ok = (dashPngLoadFile(j->path, j->max_w, j->max_h, &j->pixels, &j->w, &j->h) == 0);
}

static void themeTexDone(void *arg)
{
	ThemeTexJob *j = (ThemeTexJob *)arg;
	ThemeSlot *s = &theme_slots[j->slot];

	if (s->gen != j->gen || s->state != TS_LOADING || strcmp(s->path, j->path)) {
		if (j->ok)
			free(j->pixels);
		free(j);
		return;
	}
	if (!j->ok) {
		s->state = TS_MISSING;
		free(j);
		return;
	}
	memset(&s->tex, 0, sizeof(GSTEXTURE));
	s->tex.Width = j->w;
	s->tex.Height = j->h;
	s->tex.PSM = GS_PSM_CT32;
	s->tex.Mem = j->pixels;
	s->tex.Filter = GS_FILTER_LINEAR;
	s->tex.Delayed = 1;
	gsKit_setup_tbw(&s->tex);
	s->state = TS_LOADED;
	free(j);
}

/* Theme PNGs (wallpaper, default app icon, THEME/ layers), cached by full
 * path and decoded in the background: NULL is returned (and the caller
 * falls back) until the image is ready. A file on a device whose driver is
 * not running is not looked at (and not remembered as missing), so a
 * theme never loads a driver. A missing or broken file is remembered. */
GSTEXTURE *dashThemeTextureFile(const char *path, int max_w, int max_h)
{
	int i, free_i = -1;
	ThemeTexJob *j;

	if (path == NULL || path[0] == '\0' || strlen(path) >= DASH_PATH_MAX)
		return NULL;
	for (i = 0; i < THEME_SLOTS; i++) {
		if (theme_slots[i].state != TS_EMPTY && !strcmp(theme_slots[i].path, path) &&
		    theme_slots[i].max_w == max_w && theme_slots[i].max_h == max_h)
			return (theme_slots[i].state == TS_LOADED) ? &theme_slots[i].tex : NULL;
		if (theme_slots[i].state == TS_EMPTY && free_i < 0)
			free_i = i;
	}
	/* A loading scene may surround driver startup after the worker drained.
	 * Drawing it must not start fresh file IO against that changing stack. */
	if (free_i < 0 || dashLoadingActive() || !isPathDeviceStackLoaded(path))
		return NULL;

	j = (ThemeTexJob *)malloc(sizeof(ThemeTexJob));
	if (j == NULL)
		return NULL;
	memset(j, 0, sizeof(*j));
	dashStrCopy(theme_slots[free_i].path, path, sizeof(theme_slots[free_i].path));
	theme_slots[free_i].max_w = max_w;
	theme_slots[free_i].max_h = max_h;
	theme_slots[free_i].state = TS_LOADING;
	j->slot = free_i;
	j->gen = theme_slots[free_i].gen;
	dashStrCopy(j->path, path, sizeof(j->path));
	j->max_w = max_w;
	j->max_h = max_h;
	if (dashWorkerSubmit("Loading theme image", DASH_JOB_THEME | DASH_JOB_VISIBLE, themeTexRun, themeTexDone, j) < 0) {
		theme_slots[free_i].state = TS_EMPTY;
		free(j);
	}
	return NULL;
}

GSTEXTURE *dashThemeTexture(const char *name, int max_w, int max_h)
{
	char path[MAX_PATH];

	if (dashStoreDir()[0] == '\0')
		return NULL;
	if (snprintf(path, sizeof(path), "%sTHEME/%s", dashStoreDir(), name) >= DASH_PATH_MAX)
		return NULL;
	return dashThemeTextureFile(path, max_w, max_h);
}

void dashThemeTexturesFree(void)
{
	int i;

	for (i = 0; i < THEME_SLOTS; i++) {
		u16 gen = theme_slots[i].gen;
		if (theme_slots[i].state == TS_LOADED) {
			dashTexRelease(&theme_slots[i].tex);
			free(theme_slots[i].tex.Mem);
		}
		memset(&theme_slots[i], 0, sizeof(ThemeSlot));
		theme_slots[i].gen = gen + 1;
	}
}

//--------------------------------------------------------------
// Drawing
//--------------------------------------------------------------
static void monogram(const char *title, char *out)
{
	int n = 0;
	const char *p = title;

	while (*p && n < 2) {
		if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) {
			char c = *p;
			if (c >= 'a' && c <= 'z')
				c -= 32;
			out[n++] = c;
			/* second letter from the next word, if any */
			while (*p && *p != ' ')
				p++;
		} else
			p++;
	}
	if (n == 0)
		out[n++] = '?';
	out[n] = '\0';
}

/* Convex silhouettes, in tile-local coordinates. Fills and focus outlines
 * use the same vertices; artwork stays inside a rectangular label well. */
static int tileVertices(float w, float h, float *vx, float *vy)
{
	static const float card[][2] = {{.14f, .02f}, {.70f, .02f}, {.86f, .18f}, {.86f, .98f}, {.14f, .98f}};
	static const float chamfer[][2] = {{.16f, .02f}, {.84f, .02f}, {.98f, .16f}, {.98f, .84f}, {.84f, .98f}, {.16f, .98f}, {.02f, .84f}, {.02f, .16f}};
	static const float crystal[][2] = {{.02f, .18f}, {.18f, .02f}, {.98f, .02f}, {.98f, .82f}, {.82f, .98f}, {.02f, .98f}};
	const float(*v)[2] = dashTheme.tile_style == DASH_TILE_MEMORY_CARD ? card : dashTheme.tile_style == DASH_TILE_CRYSTAL ? crystal :
                                                                                                                            chamfer;
	int i, n = dashTheme.tile_style == DASH_TILE_MEMORY_CARD ? 5 : dashTheme.tile_style == DASH_TILE_CRYSTAL ? 6 :
                                                                                                               8;
	for (i = 0; i < n; i++) {
		vx[i] = floorf(v[i][0] * w + .5f);
		vy[i] = floorf(v[i][1] * h + .5f);
	}
	return n;
}

static void tilePolygon(float x, float y, float w, float h, DashColor top, DashColor bot, int outline)
{
	float vx[8], vy[8];
	int i, n = tileVertices(w, h, vx, vy);
	DashColor mid = dashColorLerp(top, bot, .5f);
	for (i = 0; i < n; i++) {
		int j = (i + 1) % n;
		if (outline)
			dashLine(x + vx[i], y + vy[i], x + vx[j], y + vy[j], 2.0f, top);
		else
			dashTri(x + w * .5f, y + h * .5f, x + vx[i], y + vy[i], x + vx[j], y + vy[j], mid,
			        dashColorLerp(top, bot, vy[i] / h), dashColorLerp(top, bot, vy[j] / h));
	}
}

void dashDrawTileFocus(float x, float y, float w, float h, float r, float strength)
{
	DashColor edge = dashColorA(dashColorLighten(DASH_C_ACCENT, .45f), strength);
	if (strength <= .01f)
		return;
	/* Zoomed Continue tiles land on fractional coordinates; an outline
	 * straddling two scanlines flickers between interlaced fields (#140). */
	x = floorf(x + .5f);
	y = floorf(y + .5f);
	w = floorf(w + .5f);
	h = floorf(h + .5f);
	if (dashTheme.tile_style == DASH_TILE_FLOATING) {
		/* A solid pedestal remains visible even with theme glow disabled. */
		dashLine(x + w * .20f, y + h * .92f, x + w * .80f, y + h * .92f, 2.0f, edge);
	} else if (dashTheme.tile_style == DASH_TILE_ROUNDED)
		dashRoundedBorder(x - 2, y - 2, w + 4, h + 4, r + 2, edge);
	else
		tilePolygon(x - 2, y - 2, w + 4, h + 4, edge, edge, 1);
}

static void tileBody(float x, float y, float w, float h, float focus, DashColor top, DashColor bot)
{
	int style = dashTheme.tile_style;
	DashColor edge = dashColorA(DASH_C_ACCENT, .35f + .35f * focus);
	if (style == DASH_TILE_FLOATING) {
		if (dashTheme.glow)
			dashGlow(x + w * .5f, y + h * .88f, w * .45f, h * .10f, edge);
		return;
	}
	if (style == DASH_TILE_ROUNDED) {
		dashRoundedGradV(x, y, w, h, w * .10f, top, bot);
		if (DASH_C_BORDER.a)
			dashRoundedBorder(x, y, w, h, w * .10f, DASH_C_BORDER);
		return;
	}
	tilePolygon(x, y, w, h, top, bot, 0);
	if (style == DASH_TILE_MEMORY_CARD) {
		int i;
		for (i = 0; i < 3; i++)
			dashRect(x + w * .22f, y + h * (.09f + i * .045f), w * .35f, h * .014f, edge);
		/* Recessed label strip, kept clear of the artwork above it. */
		dashRect(x + w * .21f, y + h * .79f, w * .58f, h * .12f, dashColorA(DASH_C_ACCENT, .18f));
	} else if (style == DASH_TILE_CRYSTAL) {
		DashColor light = dashColorLighten(top, .25f), side = dashColorScale(bot, .65f);
		dashQuadG(x + w * .02f, y + h * .18f, x + w * .82f, y + h * .18f,
		          x + w * .18f, y + h * .02f, x + w * .98f, y + h * .02f, light, light, top, top);
		dashQuadG(x + w * .82f, y + h * .18f, x + w * .98f, y + h * .02f,
		          x + w * .82f, y + h * .98f, x + w * .98f, y + h * .82f, side, bot, side, bot);
	}
	tilePolygon(x, y, w, h, edge, edge, 1);
}

/* Both real artwork and fallback icons share the same shell and content well. */
static void appTile(const DashApp *app, GSTEXTURE *tex, float x, float y, float w, float h, float focus)
{
	int style = dashTheme.tile_style, icon = dashTheme.app_icon_style;
	float ax = .12f, ay = .12f, aw = .76f, ah = .66f;
	float badge_x = .77f, badge_y = .79f, badge_size = .15f;
	DashColor top = dashColorMix(dashColorLighten(DASH_C_PANEL_HI, .16f), DASH_C_SELECT, .22f + .30f * focus);
	DashColor bot = dashColorScale(DASH_C_PANEL_HI, .55f);
	DashColor glyph = dashColorLerp(DASH_C_TEXT2, DASH_C_TEXT, .4f + .6f * focus);
	GSTEXTURE *custom = NULL;
	if (icon == DASH_APPICON_MONOGRAM && tex == NULL) {
		top = dashColorHue(app->hue, 150, 96 + (int)(40 * focus), 255);
		bot = dashColorHue(app->hue + 28, 190, 34 + (int)(20 * focus), 255);
	}
	top.a = bot.a = 255;
	if (style == DASH_TILE_MEMORY_CARD) {
		ax = .22f;
		ay = .28f;
		aw = .56f;
		ah = .46f;
		badge_x = .64f;
		badge_y = .80f;
		badge_size = .10f;
	} else if (style == DASH_TILE_CRYSTAL) {
		ax = .10f;
		ay = .26f;
		aw = .64f;
		ah = .53f;
		badge_x = .62f;
		badge_y = .82f;
		badge_size = .12f;
	} else if (style == DASH_TILE_FLOATING) {
		ay = .16f - .06f * focus;
		if (dashCfg.anim > 0) {
			/* Whole-pixel bob: sub-pixel motion shimmers on interlaced TVs (#140). */
			float bob = h * .015f * focus * dashClampf(dashCfg.anim / 100.0f, 0.1f, 2.0f) * dashSinf(dashTime() * 2.0f);
			ay += floorf(bob + .5f) / h;
		}
		badge_y = .73f;
	}
	tileBody(x, y, w, h, focus, top, bot);
	if (tex != NULL)
		dashTexDrawFit(tex, x + w * ax, y + h * ay, w * aw, h * ah, DC(255, 255, 255, 255));
	else {
		if (icon == DASH_APPICON_SYSTEM && dashTheme.app_icon[0])
			custom = dashThemeTextureFile(dashTheme.app_icon, 128, 128);
		if (custom != NULL)
			dashTexDrawFit(custom, x + w * ax, y + h * ay, w * aw, h * ah, DC(255, 255, 255, 255));
		else if (icon == DASH_APPICON_MONOGRAM) {
			char mono[4];
			float ms = w * aw / 36.0f;
			monogram(app->title, mono);
			dashTextCenter(x + w * (ax + aw * .5f), y + h * (ay + ah * .5f) - dashTextLineH(ms) * .5f, mono, ms, glyph);
		} else {
			float sz = fminf(w * aw, h * ah) * .82f;
			dashIcon(icon == DASH_APPICON_DEVICE ? dashLibDeviceIcon(app->path) : DI_EXEC,
			         x + w * (ax + aw * .5f) - sz * .5f, y + h * (ay + ah * .5f) - sz * .5f, sz, glyph);
		}
	}
	if (tex != NULL || icon != DASH_APPICON_DEVICE)
		dashIcon(dashLibDeviceIcon(app->path), x + w * badge_x, y + h * badge_y, w * badge_size, dashColorA(DASH_C_ACCENT, .65f + .25f * focus));
}

void dashDrawAppTile(const DashApp *app, float x, float y, float w, float h, float focus)
{
	appTile(app, NULL, x, y, w, h, focus);
}

void dashDrawAppArt(DashApp *app, float x, float y, float w, float h, float focus, int prio)
{
	GSTEXTURE *tex = dashArtGet(app, prio);
	if (focus > .5f) {
		float k = dashUiFocusPop();
		x -= w * (k - 1.0f) * .5f;
		y -= h * (k - 1.0f) * .5f;
		w *= k;
		h *= k;
	}
	/* Snap tile coordinates to whole pixels so outlines and bevels
	 * remain stable on 4:3 interlaced CRT displays (#140). */
	x = floorf(x + .5f);
	y = floorf(y + .5f);
	w = floorf(w + .5f);
	h = floorf(h + .5f);
	/* Dim the actual silhouette/content, avoiding a rectangular missing-app overlay. */
	dashGfxPushAlpha(app->flags & DASH_APP_MISSING ? .5f : 1.0f);
	appTile(app, tex, x, y, w, h, focus);
	dashGfxPopAlpha();
}
//--------------------------------------------------------------
//End of file: dash_art.c
//--------------------------------------------------------------
