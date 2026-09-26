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

static char probe_key[DASH_PATH_MAX];
static int probe_idx;
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
	probe_key[0] = '\0';
	probe_idx = 0;
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

static int decodeInto(DashApp *app)
{
	ArtSlot *s;
	u32 *pixels = NULL;
	int w, h;

	if (dashPngLoadFile(app->art, ART_MAX_DIM, ART_MAX_DIM, &pixels, &w, &h) != 0)
		return -1;
	s = allocSlot();
	if (s == NULL) {
		free(pixels);
		return 1; /* cache full of visible items: try again later */
	}
	s->used = 1;
	dashStrCopy(s->key, app->path, sizeof(s->key));
	s->tex.Width = w;
	s->tex.Height = h;
	s->tex.PSM = GS_PSM_CT32;
	s->tex.Mem = pixels;
	s->tex.Filter = GS_FILTER_LINEAR;
	s->tex.Delayed = 1;
	gsKit_setup_tbw(&s->tex);
	s->last_frame = art_frame;
	return 0;
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

int dashArtStep(int input_idle_ms)
{
	ArtWant *best = NULL;
	DashApp *app;
	int i, tries;

	if (!dashCfg.artwork || nwants == 0)
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

	app = best->app;
	/* never overlap file IO with a pending asynchronous MC status poll */
	filerRootMcPollIdle();
	if (app->art_state == DASH_ART_FOUND) {
		int r = decodeInto(app);
		if (r < 0) {
			/* unreadable or corrupt: fall back to probing the rest */
			app->art_state = DASH_ART_UNKNOWN;
			app->art[0] = '\0';
			dashStrCopy(probe_key, app->path, sizeof(probe_key));
			probe_idx = 1;
			art_dirty = 1;
		}
		return 1;
	}

	/* DASH_ART_UNKNOWN: test up to two candidate paths this frame */
	if (stricmp(probe_key, app->path)) {
		dashStrCopy(probe_key, app->path, sizeof(probe_key));
		probe_idx = 0;
	}
	for (tries = 0; tries < 2; tries++) {
		char cand[MAX_PATH];

		if (!buildCandidate(app, probe_idx, cand, sizeof(cand))) {
			probe_key[0] = '\0';
			if (!appDeviceReady(app)) {
				/* only the dashboard ART folder could be checked; try the
				 * app's own device once its driver is running */
				app->flags |= DASH_APP_ART_DEFERRED;
				return 1;
			}
			app->art_state = DASH_ART_NONE;
			app->art[0] = '\0';
			art_dirty = 1;
			return 1;
		}
		probe_idx++;
		if (!artPathAllowed(cand))
			continue;
		if (cand[0] && strlen(cand) < DASH_PATH_MAX && fileExists(cand)) {
			dashStrCopy(app->art, cand, sizeof(app->art));
			app->art_state = DASH_ART_FOUND;
			probe_key[0] = '\0';
			art_dirty = 1;
			return 1;
		}
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
typedef struct
{
	char path[DASH_PATH_MAX];
	GSTEXTURE tex;
	u8 state; /* 0 unknown, 1 loaded, 2 missing */
} ThemeSlot;

static ThemeSlot theme_slots[THEME_SLOTS];

/* Theme PNGs (wallpaper, default app icon, THEME/ layers), cached by full
 * path. A file on a device whose driver is not running is not looked at
 * (and not remembered as missing), so a theme never loads a driver. A
 * missing or broken file is remembered and the caller falls back. */
GSTEXTURE *dashThemeTextureFile(const char *path, int max_w, int max_h)
{
	int i, free_i = -1;
	u32 *pixels;
	int w, h;

	if (path == NULL || path[0] == '\0' || strlen(path) >= DASH_PATH_MAX)
		return NULL;
	for (i = 0; i < THEME_SLOTS; i++) {
		if (theme_slots[i].state && !strcmp(theme_slots[i].path, path))
			return (theme_slots[i].state == 1) ? &theme_slots[i].tex : NULL;
		if (!theme_slots[i].state && free_i < 0)
			free_i = i;
	}
	if (free_i < 0 || !isPathDeviceStackLoaded(path))
		return NULL;

	dashStrCopy(theme_slots[free_i].path, path, sizeof(theme_slots[free_i].path));
	theme_slots[free_i].state = 2;
	filerRootMcPollIdle();
	if (dashPngLoadFile(path, max_w, max_h, &pixels, &w, &h) != 0)
		return NULL;
	memset(&theme_slots[free_i].tex, 0, sizeof(GSTEXTURE));
	theme_slots[free_i].tex.Width = w;
	theme_slots[free_i].tex.Height = h;
	theme_slots[free_i].tex.PSM = GS_PSM_CT32;
	theme_slots[free_i].tex.Mem = pixels;
	theme_slots[free_i].tex.Filter = GS_FILTER_LINEAR;
	theme_slots[free_i].tex.Delayed = 1;
	gsKit_setup_tbw(&theme_slots[free_i].tex);
	theme_slots[free_i].state = 1;
	return &theme_slots[free_i].tex;
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
		if (theme_slots[i].state == 1) {
			dashTexRelease(&theme_slots[i].tex);
			free(theme_slots[i].tex.Mem);
		}
		memset(&theme_slots[i], 0, sizeof(ThemeSlot));
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

/* Tile used for applications without artwork. The default is a real
 * application icon on a panel in the theme's colours (optionally a PNG from
 * the theme), with the device as a small badge; the old initials tile and
 * a device-icon tile remain selectable (Settings > Theme > Icons). */
static void monogramTile(const DashApp *app, float x, float y, float w, float h, float focus);

void dashDrawAppTile(const DashApp *app, float x, float y, float w, float h, float focus)
{
	float r = w * 0.10f;
	float isz = w * 0.18f;
	DashColor top, bot, glyph;
	int style = dashTheme.app_icon_style;
	GSTEXTURE *custom = NULL;

	if (style == DASH_APPICON_MONOGRAM) {
		monogramTile(app, x, y, w, h, focus);
		return;
	}
	if (r < 6.0f)
		r = 6.0f;
	/* lifted panel colour with a hint of the selection colour when focused */
	top = dashColorLighten(DASH_C_PANEL_HI, 0.10f + 0.06f * focus);
	top = dashColorMix(top, DASH_C_SELECT, 0.22f + 0.30f * focus);
	bot = dashColorScale(DASH_C_PANEL_HI, 0.55f);
	top.a = bot.a = 255;
	dashPanelGradV(x, y, w, h, r, top, bot);
	/* a thin top edge catches the light, like a moulded plastic tile */
	dashRect(x + r, y + 1, w - 2 * r, 1.0f, dashColorA(DASH_C_TEXT, 0.16f + 0.1f * focus));
	if (DASH_C_BORDER.a)
		dashPanelBorder(x, y, w, h, r, DASH_C_BORDER);

	glyph = dashColorLerp(DASH_C_TEXT2, DASH_C_TEXT, 0.4f + 0.6f * focus);
	if (style == DASH_APPICON_SYSTEM && dashTheme.app_icon[0])
		custom = dashThemeTextureFile(dashTheme.app_icon, 128, 128);
	if (custom != NULL)
		dashTexDrawFit(custom, x + w * 0.16f, y + h * 0.16f, w * 0.68f, h * 0.68f, DC(255, 255, 255, 255));
	else if (style == DASH_APPICON_DEVICE)
		dashIcon(dashLibDeviceIcon(app->path), x + w * 0.25f, y + h * 0.22f, w * 0.5f, glyph);
	else
		dashIcon(DI_EXEC, x + w * 0.24f, y + h * 0.2f, w * 0.52f, glyph);
	/* device badge, bottom right */
	if (style != DASH_APPICON_DEVICE)
		dashIcon(dashLibDeviceIcon(app->path), x + w - isz - w * 0.08f, y + h - isz - h * 0.08f, isz,
		         dashColorA(DASH_C_ACCENT, 0.55f + 0.25f * focus));
}

static void monogramTile(const DashApp *app, float x, float y, float w, float h, float focus)
{
	DashColor top = dashColorHue(app->hue, 150, 96 + (int)(40 * focus), 255);
	DashColor bottom = dashColorHue(app->hue + 28, 190, 34 + (int)(20 * focus), 255);
	float r = w * 0.10f;
	char mono[4];
	float ms = w / 46.0f;
	float isz = w * 0.18f;

	if (r < 6.0f)
		r = 6.0f;
	dashPanelGradV(x, y, w, h, r, top, bottom);
	/* diagonal sheen */
	dashRectGrad4(x + r * 0.5f, y + 2, w - r, h * 0.45f,
	              DC(255, 255, 255, 44), DC(255, 255, 255, 10), DC(255, 255, 255, 8), DC(255, 255, 255, 0));
	/* crystal motif */
	dashGlow(x + w * 0.78f, y + h * 0.22f, w * 0.34f, h * 0.34f, dashColorHue(app->hue + 180, 60, 255, 50));
	monogram(app->title, mono);
	dashTextCenter(x + w * 0.5f, y + h * 0.5f - dashTextLineH(ms) * 0.62f, mono, ms, DC(255, 255, 255, 235));
	dashIcon(dashLibDeviceIcon(app->path), x + w - isz - w * 0.08f, y + h - isz - h * 0.08f, isz, DC(255, 255, 255, 80));
}

void dashDrawAppArt(DashApp *app, float x, float y, float w, float h, float focus, int prio)
{
	GSTEXTURE *tex = dashArtGet(app, prio);
	float r = w * 0.10f;

	if (r < 6.0f)
		r = 6.0f;
	dashShadow(x, y, w, h, 10.0f + 10.0f * focus, 0.6f + 0.3f * focus);
	if (tex != NULL) {
		DashColor frame_top = DC(58 + (int)(40 * focus), 66 + (int)(40 * focus), 96 + (int)(50 * focus), 255);
		float inset = w * 0.05f;

		dashPanelGradV(x, y, w, h, r, frame_top, DC(18, 22, 36, 255));
		dashRect(x + inset, y + inset, w - inset * 2, h - inset * 2, DC(6, 8, 14, 255));
		dashTexDrawFit(tex, x + inset, y + inset, w - inset * 2, h - inset * 2, DC(255, 255, 255, 255));
		/* glassy highlight */
		dashRectGrad4(x + inset, y + inset, w - inset * 2, (h - inset * 2) * 0.4f,
		              DC(255, 255, 255, 34), DC(255, 255, 255, 12), DC(255, 255, 255, 0), DC(255, 255, 255, 0));
	} else
		dashDrawAppTile(app, x, y, w, h, focus);
	if (app->flags & DASH_APP_MISSING)
		dashRect(x, y, w, h, DC(0, 0, 0, 120));
}
//--------------------------------------------------------------
//End of file: dash_art.c
//--------------------------------------------------------------
