//--------------------------------------------------------------
//File name:   dash_themes.c
//Theme engine: the theme model, built-in presets, validation,
//persistence and user theme folders.
//
//* dashTheme holds every visual value of the dashboard. dashThemeApply()
//  pushes it into the palette (DASH_C_*) and the renderer; the font, icon,
//  background and widget code read the remaining fields directly.
//* Presets are plain functions of a base theme, so a preset only states
//  what makes it different.
//* The active theme is stored in DASHBOARD.CNF as Theme_* keys (written
//  by dash_store.c). An unmodified preset is stored by name only, so it
//  follows preset improvements in later builds; a customised theme is
//  stored as a complete snapshot.
//* Theme folders: <dashboard dir>/THEMES/<name>/theme.cfg with the same
//  keys (no prefix). Relative image names are resolved in that folder.
//  Folders are only read when the user loads one from SETTINGS, so a
//  missing or broken theme can never block startup.
//* Every value is validated: out-of-range numbers are clamped, unknown
//  keys and malformed values are ignored and unreadable text colours are
//  replaced, so no theme can make the dashboard unusable.
//
//Visual direction: the PS2 presets follow the design language documented
//by the osdbits OSDSYS reverse-engineering notes (restrained colour swaps
//for selection, the {30,110,156} highlight blue, near-black backdrops,
//indigo menu tunnel, red disc-error atmosphere). No Sony resource data is
//used; every background is procedural.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "init.h"

extern int fileMode;

DashTheme dashTheme;

static int theme_preset = 0; /* index into presets[] */
static int theme_custom;     /* 1: edited, saved as a snapshot */
static int theme_ready;
static void *focus_app;

/* DASHBOARD.CNF load state */
static int store_preset;
static int store_custom;
static int store_seen;

//--------------------------------------------------------------
// Colour helpers
//--------------------------------------------------------------
DashColor dashColorMix(DashColor a, DashColor b, float t)
{
	DashColor c = dashColorLerp(a, b, t);

	c.a = a.a;
	return c;
}

DashColor dashColorLighten(DashColor c, float t)
{
	return dashColorMix(c, DC(255, 255, 255, 255), t);
}

DashColor dashColorScale(DashColor c, float k)
{
	int r = (int)(c.r * k), g = (int)(c.g * k), b = (int)(c.b * k);

	return DC(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b, c.a);
}

static int luma(DashColor c)
{
	return (c.r * 77 + c.g * 150 + c.b * 29) >> 8;
}

/* What the screen behind the menus mostly looks like, top and bottom. For
 * scenes, bg1 is only a tint (tower walls, clock rods, lights...) and bg2
 * is the base that fills most of the screen. */
void dashThemeBaseColors(const DashTheme *t, DashColor *top, DashColor *bottom)
{
	switch (t->bg) {
		case DASH_BG_SOLID:
			*top = *bottom = t->bg1;
			break;
		case DASH_BG_GRADIENT:
		case DASH_BG_CRYSTAL:
		case DASH_BG_IMAGE:
			*top = t->bg1;
			*bottom = t->bg2;
			break;
		default:
			*top = dashColorLerp(t->bg2, t->bg1, 0.18f);
			*bottom = t->bg2;
			break;
	}
	top->a = bottom->a = 255;
}

float dashThemeRadius(float r)
{
	switch (dashTheme.corner) {
		case DASH_CORNER_SQUARE:
			return 0.0f;
		case DASH_CORNER_SOFT:
			return (r > 4.0f) ? 4.0f : r;
		default:
			return r;
	}
}

//--------------------------------------------------------------
// Names
//--------------------------------------------------------------
static const char *const bg_ids[DASH_BG_COUNT] = {
    "solid", "gradient", "tunnel", "mist", "towers", "disc", "orbit", "clockring", "grid",
    "lines", "cubes", "stream", "tiles", "brushed", "wave", "crystal", "streaks", "clouds", "image"};
static const char *const bg_names[DASH_BG_COUNT] = {
    "Solid colour", "Gradient", "System tunnel", "Blue mist", "PS2 towers", "Disc error red",
    "Orbit lights", "Clock ring", "Memory card grid", "Boot lines", "Wireframe cubes",
    "Data stream", "Memory card tiles", "Brushed console", "Ribbons", "Crystal",
    "Light streaks", "Drift clouds", "Wallpaper image"};
static const char *const hl_ids[DASH_HL_COUNT] = {"fill", "bar", "outline", "text"};
static const char *const corner_ids[DASH_CORNER_COUNT] = {"square", "soft", "round"};
static const char *const font_ids[DASH_FONT_COUNT] = {"roboto", "system"};
static const char *const tint_ids[DASH_ICONS_COUNT] = {"text", "accent", "mono"};
static const char *const appicon_ids[DASH_APPICON_COUNT] = {"system", "device", "monogram"};
static const char *const clock_ids[DASH_CLOCK_COUNT] = {"off", "standard", "system"};
static const char *const clockpos_ids[DASH_CLOCKPOS_COUNT] = {"header", "footer"};
static const char *const hours_ids[DASH_HOURS_COUNT] = {"system", "24", "12"};
static const char *const date_ids[DASH_DATE_COUNT] = {"system", "ymd", "mdy", "dmy", "off"};
static const char *const wall_ids[DASH_WALL_COUNT] = {"off", "focused"};

const char *dashThemeBgName(int kind)
{
	return (kind >= 0 && kind < DASH_BG_COUNT) ? bg_names[kind] : "";
}

const char *dashThemeBgId(int kind)
{
	return (kind >= 0 && kind < DASH_BG_COUNT) ? bg_ids[kind] : "solid";
}

//--------------------------------------------------------------
// Presets
//--------------------------------------------------------------
enum {
	P_OSD = 0,
	P_BLACK,
	P_TOWERS,
	P_DISC,
	P_MIST,
	P_MIDNIGHT,
	P_MINIMAL,
	P_CLOCKRING,
	P_MCGRID,
	P_BOOTLINES,
	P_CUBES,
	P_STREAM,
	P_MCBLUE,
	P_BRUSHED,
	P_BIOSBLUE,
	P_MONO,
	P_CRT,
	P_PSX,
	P_CRYSTAL,
	P_LIGHTSHOW,
	P_COUNT
};

static const DashThemePresetInfo presets[P_COUNT] = {
    {"osd", "OSD Classic", "Indigo system tunnel and the OSDSYS highlight blue. The default."},
    {"black", "Classic Black", "Plain black. The lightest background, adjustable to any colour."},
    {"towers", "PS2 Towers", "The opening tower field, seen from above in fog."},
    {"discred", "Disc Error Red", "Deep red clouds and a slow glass cube."},
    {"mist", "PS2 Blue Mist", "Dark navy with slow translucent blue forms."},
    {"midnight", "Midnight Gradient", "A quiet blue-to-black gradient."},
    {"minimal", "OSD Minimal", "Near black, a ring of eight lights, text-only selection."},
    {"clockring", "System Clock", "The System Configuration clock ring: hours, minutes and seconds."},
    {"mcgrid", "Memory Card Grid", "Sparse block grid in thin lines."},
    {"bootlines", "Boot Lines", "Soft light traces crossing a dark screen."},
    {"cubes", "Wireframe Cubes", "A few blue wireframe cubes drifting in depth."},
    {"stream", "Data Stream", "Abstract device activity in thin columns."},
    {"mcblue", "Memory Card Blue", "Muted memory card blue with a tiled motif."},
    {"brushed", "Brushed Console", "Charcoal with a fine matte finish."},
    {"biosblue", "BIOS Blue", "Clean blue system screen, white text, cyan focus."},
    {"mono", "Monochrome", "Black, white and grey. Colour only marks the selection."},
    {"crt", "CRT", "Dark gradient with an understated scanline overlay."},
    {"psx", "PSX", "Charcoal and silver ribbons."},
    {"crystal", "Crystal", "The previous dashboard look: towers, stars and glow."},
    {"lightshow", "Light Show", "Light pillars sweeping across an indigo night."}};

static void base(DashTheme *t)
{
	memset(t, 0, sizeof(*t));
	strcpy(t->author, "wLaunchELF");
	t->bg = DASH_BG_TUNNEL;
	t->bg_bright = 100;
	t->bg1 = DC(46, 36, 78, 255);
	t->bg2 = DC(4, 3, 10, 255);
	t->text = DC(236, 238, 244, 255);
	t->text2 = DC(156, 162, 178, 255);
	t->text3 = DC(100, 106, 124, 255);
	t->sel_text = DC(118, 214, 255, 255);
	t->accent = DC(84, 196, 250, 255);
	t->accent2 = DC(150, 140, 255, 255);
	t->select = DC(30, 110, 156, 150);
	t->warn = DC(255, 204, 92, 255);
	t->danger = DC(255, 98, 110, 255);
	t->ok = DC(92, 220, 150, 255);
	t->panel = DC(6, 6, 16, 150);
	t->border = DC(255, 255, 255, 16);
	t->corner = DASH_CORNER_SOFT;
	t->highlight = DASH_HL_BAR;
	t->glow = 0;
	t->font = DASH_FONT_ROBOTO;
	t->font_scale = 100;
	t->bold = 0;
	t->text_shadow = 1;
	t->icon_tint = DASH_ICONS_TEXT;
	t->app_icon_style = DASH_APPICON_SYSTEM;
	t->clock = DASH_CLOCK_SYSTEM;
	t->clock_pos = DASH_CLOCKPOS_HEADER;
	t->clock_hours = DASH_HOURS_SYSTEM;
	t->clock_seconds = 1;
	t->clock_date = DASH_DATE_SYSTEM;
	t->wallpaper = DASH_WALL_OFF;
	t->wall_dim = 65;
}

void dashThemeLoadPreset(int i, DashTheme *t)
{
	if (i < 0 || i >= P_COUNT)
		i = P_OSD;
	base(t);
	dashStrCopy(t->name, presets[i].name, sizeof(t->name));

	switch (i) {
		case P_OSD:
			break;
		case P_BLACK:
			t->bg = DASH_BG_SOLID;
			t->bg1 = DC(0, 0, 0, 255);
			t->bg2 = DC(0, 0, 0, 255);
			t->select = DC(48, 96, 160, 170);
			t->sel_text = t->text;
			t->highlight = DASH_HL_FILL;
			t->clock = DASH_CLOCK_STANDARD;
			break;
		case P_TOWERS:
			t->bg = DASH_BG_TOWERS;
			t->bg1 = DC(150, 148, 190, 255); /* tower walls */
			t->bg2 = DC(40, 76, 170, 255);   /* fog */
			t->panel = DC(2, 3, 10, 170);
			break;
		case P_DISC:
			t->bg = DASH_BG_DISC;
			t->bg1 = DC(168, 34, 44, 255);
			t->bg2 = DC(10, 2, 3, 255);
			t->text = DC(244, 236, 234, 255);
			t->text2 = DC(196, 166, 164, 255);
			t->text3 = DC(136, 104, 106, 255);
			t->sel_text = DC(255, 206, 196, 255);
			t->accent = DC(255, 128, 116, 255);
			t->accent2 = DC(255, 176, 140, 255);
			t->select = DC(150, 32, 44, 170);
			t->warn = DC(255, 204, 128, 255);
			t->panel = DC(14, 3, 4, 160);
			t->border = DC(255, 200, 200, 18);
			t->highlight = DASH_HL_FILL;
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_MIST:
			t->bg = DASH_BG_MIST;
			t->bg1 = DC(40, 96, 200, 255);
			t->bg2 = DC(3, 6, 18, 255);
			t->select = DC(34, 84, 164, 170);
			t->sel_text = t->text;
			t->highlight = DASH_HL_FILL;
			break;
		case P_MIDNIGHT:
			t->bg = DASH_BG_GRADIENT;
			t->bg1 = DC(18, 30, 68, 255);
			t->bg2 = DC(0, 0, 4, 255);
			t->select = DC(40, 88, 168, 170);
			t->sel_text = t->text;
			t->highlight = DASH_HL_FILL;
			break;
		case P_MINIMAL:
			t->bg = DASH_BG_ORBIT;
			t->bg1 = DC(80, 160, 255, 255);
			t->bg2 = DC(0, 0, 0, 255);
			t->text = DC(226, 226, 226, 255);
			t->text2 = DC(140, 140, 140, 255);
			t->text3 = DC(92, 92, 92, 255);
			t->sel_text = DC(60, 200, 255, 255);
			t->panel = DC(0, 0, 0, 0);
			t->border = DC(255, 255, 255, 0);
			t->highlight = DASH_HL_TEXT;
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_CLOCKRING:
			t->bg = DASH_BG_CLOCKRING;
			t->bg1 = DC(167, 217, 255, 255); /* rod colour */
			t->bg2 = DC(34, 24, 60, 255);
			t->warn = DC(236, 226, 80, 255);
			t->panel = DC(4, 2, 12, 150);
			break;
		case P_MCGRID:
			t->bg = DASH_BG_GRID;
			t->bg1 = DC(96, 128, 178, 255);
			t->bg2 = DC(6, 8, 14, 255);
			t->select = DC(40, 84, 150, 180);
			t->sel_text = t->text;
			t->highlight = DASH_HL_FILL;
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_BOOTLINES:
			t->bg = DASH_BG_LINES;
			t->bg1 = DC(120, 172, 255, 255);
			t->bg2 = DC(2, 3, 8, 255);
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_CUBES:
			t->bg = DASH_BG_CUBES;
			t->bg1 = DC(70, 140, 255, 255);
			t->bg2 = DC(2, 4, 12, 255);
			t->select = DC(36, 82, 160, 170);
			t->sel_text = t->text;
			t->highlight = DASH_HL_FILL;
			break;
		case P_STREAM:
			t->bg = DASH_BG_STREAM;
			t->bg1 = DC(64, 134, 224, 255);
			t->bg2 = DC(2, 4, 10, 255);
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_MCBLUE:
			t->bg = DASH_BG_TILES;
			t->bg1 = DC(118, 138, 170, 255);
			t->bg2 = DC(38, 50, 72, 255);
			t->text = DC(246, 248, 252, 255);
			t->text2 = DC(204, 212, 226, 255);
			t->text3 = DC(150, 162, 184, 255);
			t->sel_text = t->text;
			t->accent = DC(160, 214, 255, 255);
			t->select = DC(92, 124, 176, 210);
			t->panel = DC(18, 26, 42, 180);
			t->border = DC(255, 255, 255, 24);
			t->highlight = DASH_HL_FILL;
			break;
		case P_BRUSHED:
			t->bg = DASH_BG_BRUSHED;
			t->bg1 = DC(64, 66, 72, 255);
			t->bg2 = DC(16, 17, 20, 255);
			t->text2 = DC(170, 172, 180, 255);
			t->text3 = DC(112, 114, 122, 255);
			t->sel_text = DC(255, 255, 255, 255);
			t->accent = DC(110, 160, 250, 255);
			t->select = DC(70, 74, 88, 220);
			t->panel = DC(8, 8, 10, 150);
			t->border = DC(255, 255, 255, 22);
			t->highlight = DASH_HL_FILL;
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_BIOSBLUE:
			t->bg = DASH_BG_GRADIENT;
			t->bg1 = DC(0, 42, 124, 255);
			t->bg2 = DC(0, 12, 52, 255);
			t->text = DC(255, 255, 255, 255);
			t->text2 = DC(204, 218, 242, 255);
			t->text3 = DC(142, 162, 204, 255);
			t->sel_text = DC(255, 255, 255, 255);
			t->accent = DC(0, 230, 255, 255);
			t->select = DC(0, 70, 150, 200);
			t->panel = DC(0, 8, 36, 130);
			t->border = DC(0, 230, 255, 40);
			t->corner = DASH_CORNER_SQUARE;
			t->text_shadow = 0;
			break;
		case P_MONO:
			t->bg = DASH_BG_SOLID;
			t->bg1 = DC(0, 0, 0, 255);
			t->bg2 = DC(0, 0, 0, 255);
			t->text = DC(240, 240, 240, 255);
			t->text2 = DC(170, 170, 170, 255);
			t->text3 = DC(110, 110, 110, 255);
			t->sel_text = DC(255, 255, 255, 255);
			t->accent = DC(228, 228, 228, 255);
			t->accent2 = DC(190, 190, 190, 255);
			t->select = DC(40, 120, 230, 190);
			t->warn = DC(236, 236, 236, 255);
			t->danger = DC(255, 255, 255, 255);
			t->ok = DC(210, 210, 210, 255);
			t->panel = DC(10, 10, 10, 160);
			t->border = DC(255, 255, 255, 30);
			t->icon_tint = DASH_ICONS_MONO;
			t->highlight = DASH_HL_FILL;
			t->corner = DASH_CORNER_SQUARE;
			break;
		case P_CRT:
			t->bg = DASH_BG_GRADIENT;
			t->bg1 = DC(12, 26, 44, 255);
			t->bg2 = DC(0, 2, 6, 255);
			t->scanlines = 1;
			t->select = DC(36, 92, 150, 180);
			t->sel_text = t->text;
			t->highlight = DASH_HL_FILL;
			t->bold = 1;
			break;
		case P_PSX:
			t->bg = DASH_BG_WAVE;
			t->bg1 = DC(186, 192, 204, 255);
			t->bg2 = DC(22, 24, 30, 255);
			t->text2 = DC(178, 182, 192, 255);
			t->text3 = DC(118, 122, 134, 255);
			t->sel_text = DC(255, 255, 255, 255);
			t->accent = DC(140, 190, 255, 255);
			t->select = DC(104, 114, 136, 180);
			t->panel = DC(12, 13, 16, 150);
			t->highlight = DASH_HL_FILL;
			break;
		case P_CRYSTAL:
			t->bg = DASH_BG_CRYSTAL;
			t->bg1 = DC(18, 22, 50, 255);
			t->bg2 = DC(3, 4, 9, 255);
			t->text = DC(0xF2, 0xF4, 0xFA, 255);
			t->text2 = DC(0xAE, 0xB6, 0xCC, 255);
			t->text3 = DC(0x70, 0x7A, 0x96, 255);
			t->sel_text = t->text;
			t->accent = DC(0x7F, 0xD8, 0xFF, 255);
			t->accent2 = DC(0x9C, 0x8C, 0xFF, 255);
			t->select = DC(60, 120, 210, 170);
			t->warn = DC(0xFF, 0xBE, 0x5C, 255);
			t->danger = DC(0xFF, 0x62, 0x70, 255);
			t->ok = DC(0x4E, 0xE0, 0xA8, 255);
			t->panel = DC(12, 15, 30, 150);
			t->border = DC(255, 255, 255, 0);
			t->corner = DASH_CORNER_ROUND;
			t->highlight = DASH_HL_FILL;
			t->glow = 1;
			t->app_icon_style = DASH_APPICON_MONOGRAM;
			t->clock = DASH_CLOCK_STANDARD;
			t->clock_seconds = 0;
			break;
		case P_LIGHTSHOW:
			t->bg = DASH_BG_STREAKS;
			t->bg1 = DC(96, 140, 255, 255);  /* streak colour */
			t->bg2 = DC(4, 4, 14, 255);      /* night base */
			t->select = DC(40, 80, 170, 160);
			t->sel_text = t->text;
			t->highlight = DASH_HL_BAR;
			break;
	}
}

int dashThemePresetCount(void)
{
	return P_COUNT;
}

const DashThemePresetInfo *dashThemePresetInfo(int i)
{
	return (i >= 0 && i < P_COUNT) ? &presets[i] : NULL;
}

int dashThemeFindPreset(const char *id)
{
	int i;

	if (id == NULL)
		return -1;
	for (i = 0; i < P_COUNT; i++)
		if (!stricmp(presets[i].id, id) || !stricmp(presets[i].name, id))
			return i;
	return -1;
}

int dashThemePresetIndex(void)
{
	return theme_custom ? -1 : theme_preset;
}

int dashThemeIsCustom(void)
{
	return theme_custom;
}

//--------------------------------------------------------------
// Validation
//--------------------------------------------------------------
static u8 clampU8(int v, int lo, int hi)
{
	return (u8)((v < lo) ? lo : (v > hi) ? hi : v);
}

/* Text must stay readable on the background and on panels. */
static DashColor readable(DashColor c, DashColor bg, int min_diff, int min_alpha)
{
	int d = luma(c) - luma(bg);

	if (d < 0)
		d = -d;
	if (d < min_diff)
		c = (luma(bg) > 128) ? DC(0, 0, 0, c.a) : DC(240, 240, 240, c.a);
	if (c.a < min_alpha)
		c.a = (u8)min_alpha;
	return c;
}

void dashThemeValidate(DashTheme *t)
{
	DashColor ref;

	if (t->bg >= DASH_BG_COUNT)
		t->bg = DASH_BG_SOLID;
	t->bg_bright = clampU8(t->bg_bright, 10, 100);
	t->scanlines = t->scanlines ? 1 : 0;
	t->bg1.a = t->bg2.a = 255;
	if (t->corner >= DASH_CORNER_COUNT)
		t->corner = DASH_CORNER_SOFT;
	if (t->highlight >= DASH_HL_COUNT)
		t->highlight = DASH_HL_FILL;
	t->glow = t->glow ? 1 : 0;
	if (t->font >= DASH_FONT_COUNT)
		t->font = DASH_FONT_ROBOTO;
	if (t->font_scale == 0)
		t->font_scale = 100;
	t->font_scale = clampU8(t->font_scale, 90, 115);
	t->bold = t->bold ? 1 : 0;
	t->text_shadow = t->text_shadow ? 1 : 0;
	if (t->icon_tint >= DASH_ICONS_COUNT)
		t->icon_tint = DASH_ICONS_TEXT;
	if (t->app_icon_style >= DASH_APPICON_COUNT)
		t->app_icon_style = DASH_APPICON_SYSTEM;
	if (t->clock >= DASH_CLOCK_COUNT)
		t->clock = DASH_CLOCK_STANDARD;
	if (t->clock_pos >= DASH_CLOCKPOS_COUNT)
		t->clock_pos = DASH_CLOCKPOS_HEADER;
	if (t->clock_hours >= DASH_HOURS_COUNT)
		t->clock_hours = DASH_HOURS_SYSTEM;
	t->clock_seconds = t->clock_seconds ? 1 : 0;
	if (t->clock_date >= DASH_DATE_COUNT)
		t->clock_date = DASH_DATE_SYSTEM;
	if (t->wallpaper >= DASH_WALL_COUNT)
		t->wallpaper = DASH_WALL_OFF;
	t->wall_dim = clampU8(t->wall_dim, 0, 90);
	if (t->name[0] == '\0')
		strcpy(t->name, "Custom");

	/* Contrast guard: judge against the panel when panels are mostly
	 * opaque, otherwise against the average of the visible background. */
	{
		DashColor top, bot;
		dashThemeBaseColors(t, &top, &bot);
		ref = dashColorLerp(top, bot, 0.5f);
	}
	ref = dashColorScale(ref, t->bg_bright / 100.0f);
	if (t->panel.a > 150)
		ref = dashColorLerp(ref, t->panel, t->panel.a / 255.0f);
	t->text = readable(t->text, ref, 96, 200);
	t->sel_text = readable(t->sel_text, ref, 80, 200);
	t->text2 = readable(t->text2, ref, 56, 150);
	t->text3 = readable(t->text3, ref, 32, 120);
	t->accent.a = t->accent2.a = t->warn.a = t->danger.a = t->ok.a = 255;
	/* the selection must stay visible in every highlight style */
	if (t->highlight != DASH_HL_TEXT && t->select.a < 40)
		t->select.a = 40;
	if (t->highlight == DASH_HL_TEXT && abs(luma(t->sel_text) - luma(t->text2)) < 24 &&
	    abs(t->sel_text.b - t->text2.b) < 24 && abs(t->sel_text.r - t->text2.r) < 24)
		t->sel_text = t->accent;
}

//--------------------------------------------------------------
// Apply
//--------------------------------------------------------------
static char applied_image[DASH_PATH_MAX];
static char applied_icon[DASH_PATH_MAX];

void dashThemeApply(void)
{
	DashTheme *t = &dashTheme;

	DASH_C_TEXT = t->text;
	DASH_C_TEXT2 = t->text2;
	DASH_C_TEXT3 = t->text3;
	DASH_C_ACCENT = t->accent;
	DASH_C_ACCENT2 = t->accent2;
	DASH_C_WARM = t->warn;
	DASH_C_DANGER = t->danger;
	DASH_C_OK = t->ok;
	DASH_C_PANEL = t->panel;
	DASH_C_PANEL_HI = dashColorLighten(t->panel, 0.10f);
	DASH_C_PANEL_HI.a = (u8)((t->panel.a + 255) / 2 > 216 ? 216 : (t->panel.a + 255) / 2);
	DASH_C_SELECT = t->select;
	DASH_C_SELEDGE = dashColorLighten(t->select, 0.55f);
	DASH_C_SELEDGE.a = 140;
	DASH_C_SELTEXT = t->sel_text;
	DASH_C_BORDER = t->border;

	/* Wallpaper / icon PNGs are cached by path; drop the old ones when the
	 * theme points somewhere else. */
	if (strcmp(applied_image, t->image) || strcmp(applied_icon, t->app_icon)) {
		dashThemeTexturesFree();
		dashStrCopy(applied_image, t->image, sizeof(applied_image));
		dashStrCopy(applied_icon, t->app_icon, sizeof(applied_icon));
	}
}

void dashThemeInit(void)
{
	if (theme_ready)
		return;
	theme_ready = 1;
	theme_preset = P_OSD;
	theme_custom = 0;
	dashThemeLoadPreset(theme_preset, &dashTheme);
	dashThemeValidate(&dashTheme);
	dashThemeApply();
}

void dashThemeSetPreset(int i)
{
	if (i < 0 || i >= P_COUNT)
		i = P_OSD;
	theme_preset = i;
	theme_custom = 0;
	dashThemeLoadPreset(i, &dashTheme);
	dashThemeValidate(&dashTheme);
	dashThemeApply();
}

void dashThemeMarkCustom(void)
{
	theme_custom = 1;
}

void dashThemeCommit(void)
{
	theme_custom = 1;
	dashThemeValidate(&dashTheme);
	dashThemeApply();
}

//--------------------------------------------------------------
// Key / value
//--------------------------------------------------------------
static int hexNibble(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* "#RRGGBB", "RRGGBB", "RRGGBBAA" or "r,g,b[,a]" (decimal). */
static int parseColor(const char *v, DashColor *out)
{
	int n[4], i, cnt = 0, len;
	const char *p = v;

	while (*p == ' ')
		p++;
	if (*p == '#')
		p++;
	len = strlen(p);
	while (len > 0 && p[len - 1] == ' ')
		len--;
	if ((len == 6 || len == 8) && strchr(p, ',') == NULL) {
		int ok = 1, vals[4] = {0, 0, 0, 255};
		for (i = 0; i < len / 2; i++) {
			int hi = hexNibble(p[i * 2]), lo = hexNibble(p[i * 2 + 1]);
			if (hi < 0 || lo < 0) {
				ok = 0;
				break;
			}
			vals[i] = hi * 16 + lo;
		}
		if (ok) {
			*out = DC(vals[0], vals[1], vals[2], vals[3]);
			return 0;
		}
	}
	n[3] = 255;
	p = v;
	while (*p && cnt < 4) {
		while (*p == ' ' || *p == '#')
			p++;
		if (*p < '0' || *p > '9')
			return -1;
		n[cnt++] = atoi(p);
		while (*p >= '0' && *p <= '9')
			p++;
		while (*p == ' ')
			p++;
		if (*p == ',')
			p++;
		else
			break;
	}
	if (cnt < 3)
		return -1;
	for (i = 0; i < 4; i++)
		if (n[i] < 0)
			n[i] = 0;
		else if (n[i] > 255)
			n[i] = 255;
	*out = DC(n[0], n[1], n[2], n[3]);
	return 0;
}

static int parseEnum(const char *v, const char *const *ids, int n, int cur)
{
	int i;

	for (i = 0; i < n; i++)
		if (!stricmp(v, ids[i]))
			return i;
	if (v[0] >= '0' && v[0] <= '9') {
		i = atoi(v);
		if (i >= 0 && i < n)
			return i;
	}
	return cur;
}

static int parseBool(const char *v, int cur)
{
	if (!stricmp(v, "1") || !stricmp(v, "on") || !stricmp(v, "yes") || !stricmp(v, "true"))
		return 1;
	if (!stricmp(v, "0") || !stricmp(v, "off") || !stricmp(v, "no") || !stricmp(v, "false"))
		return 0;
	return cur;
}

static int parseInt(const char *v, int cur)
{
	if ((v[0] >= '0' && v[0] <= '9') || v[0] == '-')
		return atoi(v);
	return cur;
}

typedef enum {
	K_COLOR = 0,
	K_ENUM,
	K_BOOL,
	K_INT,
	K_STR
} KeyType;

typedef struct
{
	const char *key;
	KeyType type;
	int offset;
	const char *const *ids;
	int nids;
	int size; /* K_STR buffer size */
} ThemeKey;

#define OFS(f) ((int)offsetof(DashTheme, f))
static const ThemeKey keys[] = {
    {"name", K_STR, OFS(name), NULL, 0, DASH_THEME_NAME_MAX},
    {"author", K_STR, OFS(author), NULL, 0, DASH_THEME_NAME_MAX},
    {"background", K_ENUM, OFS(bg), bg_ids, DASH_BG_COUNT, 0},
    {"image", K_STR, OFS(image), NULL, 0, DASH_PATH_MAX},
    {"background_color", K_COLOR, OFS(bg1), NULL, 0, 0},
    {"background_color2", K_COLOR, OFS(bg2), NULL, 0, 0},
    {"brightness", K_INT, OFS(bg_bright), NULL, 0, 0},
    {"scanlines", K_BOOL, OFS(scanlines), NULL, 0, 0},
    {"text", K_COLOR, OFS(text), NULL, 0, 0},
    {"text_secondary", K_COLOR, OFS(text2), NULL, 0, 0},
    {"text_muted", K_COLOR, OFS(text3), NULL, 0, 0},
    {"text_selected", K_COLOR, OFS(sel_text), NULL, 0, 0},
    {"accent", K_COLOR, OFS(accent), NULL, 0, 0},
    {"accent2", K_COLOR, OFS(accent2), NULL, 0, 0},
    {"selection", K_COLOR, OFS(select), NULL, 0, 0},
    {"warning", K_COLOR, OFS(warn), NULL, 0, 0},
    {"error", K_COLOR, OFS(danger), NULL, 0, 0},
    {"success", K_COLOR, OFS(ok), NULL, 0, 0},
    {"panel", K_COLOR, OFS(panel), NULL, 0, 0},
    {"border", K_COLOR, OFS(border), NULL, 0, 0},
    {"corners", K_ENUM, OFS(corner), corner_ids, DASH_CORNER_COUNT, 0},
    {"highlight", K_ENUM, OFS(highlight), hl_ids, DASH_HL_COUNT, 0},
    {"glow", K_BOOL, OFS(glow), NULL, 0, 0},
    {"font", K_ENUM, OFS(font), font_ids, DASH_FONT_COUNT, 0},
    {"font_scale", K_INT, OFS(font_scale), NULL, 0, 0},
    {"bold", K_BOOL, OFS(bold), NULL, 0, 0},
    {"text_shadow", K_BOOL, OFS(text_shadow), NULL, 0, 0},
    {"icon_tint", K_ENUM, OFS(icon_tint), tint_ids, DASH_ICONS_COUNT, 0},
    {"app_icon_style", K_ENUM, OFS(app_icon_style), appicon_ids, DASH_APPICON_COUNT, 0},
    {"app_icon", K_STR, OFS(app_icon), NULL, 0, DASH_PATH_MAX},
    {"clock", K_ENUM, OFS(clock), clock_ids, DASH_CLOCK_COUNT, 0},
    {"clock_position", K_ENUM, OFS(clock_pos), clockpos_ids, DASH_CLOCKPOS_COUNT, 0},
    {"clock_hours", K_ENUM, OFS(clock_hours), hours_ids, DASH_HOURS_COUNT, 0},
    {"clock_seconds", K_BOOL, OFS(clock_seconds), NULL, 0, 0},
    {"clock_date", K_ENUM, OFS(clock_date), date_ids, DASH_DATE_COUNT, 0},
    {"wallpaper", K_ENUM, OFS(wallpaper), wall_ids, DASH_WALL_COUNT, 0},
    {"wallpaper_dim", K_INT, OFS(wall_dim), NULL, 0, 0},
};
#define NKEYS ((int)(sizeof(keys) / sizeof(keys[0])))

int dashThemeParseKey(DashTheme *t, const char *key, char *value)
{
	int i;
	u8 *base_p = (u8 *)t;

	if (!stricmp(key, "preset")) {
		int p = dashThemeFindPreset(value);
		if (p >= 0) {
			char name[DASH_THEME_NAME_MAX], author[DASH_THEME_NAME_MAX];
			/* keep an already parsed name/author */
			dashStrCopy(name, t->name, sizeof(name));
			dashStrCopy(author, t->author, sizeof(author));
			dashThemeLoadPreset(p, t);
			if (name[0])
				dashStrCopy(t->name, name, sizeof(t->name));
			if (author[0])
				dashStrCopy(t->author, author, sizeof(t->author));
		}
		return 1;
	}
	for (i = 0; i < NKEYS; i++) {
		const ThemeKey *k = &keys[i];
		u8 *f = base_p + k->offset;

		if (stricmp(key, k->key))
			continue;
		switch (k->type) {
			case K_COLOR:
				parseColor(value, (DashColor *)f);
				break;
			case K_ENUM:
				*f = (u8)parseEnum(value, k->ids, k->nids, *f);
				break;
			case K_BOOL:
				*f = (u8)parseBool(value, *f);
				break;
			case K_INT: {
				int v = parseInt(value, *f);
				*f = (u8)((v < 0) ? 0 : (v > 255) ? 255 : v);
				break;
			}
			case K_STR:
				dashStrCopy((char *)f, value, k->size);
				break;
		}
		return 1;
	}
	return 0;
}

int dashThemeSerialize(const DashTheme *t, char *buf, int cap, const char *prefix)
{
	int i, len = 0;
	const u8 *base_p = (const u8 *)t;

	for (i = 0; i < NKEYS && len < cap - DASH_PATH_MAX - 64; i++) {
		const ThemeKey *k = &keys[i];
		const u8 *f = base_p + k->offset;

		len += snprintf(buf + len, cap - len, "%s%s = ", prefix, k->key);
		switch (k->type) {
			case K_COLOR: {
				const DashColor *c = (const DashColor *)f;
				if (c->a == 255)
					len += snprintf(buf + len, cap - len, "#%02X%02X%02X", c->r, c->g, c->b);
				else
					len += snprintf(buf + len, cap - len, "#%02X%02X%02X%02X", c->r, c->g, c->b, c->a);
				break;
			}
			case K_ENUM:
				len += snprintf(buf + len, cap - len, "%s", k->ids[(*f < k->nids) ? *f : 0]);
				break;
			case K_BOOL:
			case K_INT:
				len += snprintf(buf + len, cap - len, "%d", *f);
				break;
			case K_STR:
				len += snprintf(buf + len, cap - len, "%s", (const char *)f);
				break;
		}
		len += snprintf(buf + len, cap - len, "\r\n");
	}
	return len;
}

//--------------------------------------------------------------
// DASHBOARD.CNF
//--------------------------------------------------------------
void dashThemeStoreBegin(void)
{
	dashThemeInit();
	store_preset = P_OSD;
	store_custom = 0;
	store_seen = 0;
	dashThemeLoadPreset(P_OSD, &dashTheme);
}

int dashThemeStoreKey(const char *key, char *value)
{
	if (strncasecmp(key, "Theme_", 6))
		return 0;
	key += 6;
	store_seen = 1;
	if (!stricmp(key, "preset")) {
		int p = dashThemeFindPreset(value);
		store_preset = (p >= 0) ? p : P_OSD;
		dashThemeLoadPreset(store_preset, &dashTheme);
	} else if (!stricmp(key, "custom"))
		store_custom = parseBool(value, 0);
	else
		dashThemeParseKey(&dashTheme, key, value);
	return 1;
}

void dashThemeStoreEnd(void)
{
	theme_preset = store_preset;
	theme_custom = store_custom;
	/* an unmodified preset follows the built-in definition */
	if (!theme_custom)
		dashThemeLoadPreset(theme_preset, &dashTheme);
	dashThemeValidate(&dashTheme);
	dashThemeApply();
}

int dashThemeStoreWrite(char *buf, int cap)
{
	int len;

	len = snprintf(buf, cap, "Theme_preset = %s\r\nTheme_custom = %d\r\n", presets[theme_preset].id, theme_custom);
	if (theme_custom && len < cap)
		len += dashThemeSerialize(&dashTheme, buf + len, cap - len, "Theme_");
	return len;
}

//--------------------------------------------------------------
// Theme folders
//--------------------------------------------------------------
static int isWritablePath(const char *path)
{
	return (!strncmp(path, "mc", 2) || !strncmp(path, "mass", 4) || !strncmp(path, "usb", 3) ||
	        !strncmp(path, "hdd", 3) || !strncmp(path, "mmce", 4) || !strncmp(path, "mx4sio", 6) ||
	        !strncmp(path, "ata", 3));
}

/* Ordered candidate roots for theme folders: the ELF's own folder first
 * (so themes ride along with the ELF on any device), then the dashboard
 * folder. A root is only offered when its device stack is already loaded;
 * looking at themes never starts a driver. */
static int themeRoots(char roots[2][MAX_PATH])
{
	int n = 0;
	const char *store = dashStoreDir();

	if (LaunchElfDir[0] && isWritablePath(LaunchElfDir) && isPathDeviceStackLoaded(LaunchElfDir)) {
		/* themes ride along with the ELF, in <ELF folder>/THEMES/ */
		if (snprintf(roots[0], MAX_PATH, "%sTHEMES/", LaunchElfDir) < MAX_PATH)
			n++;
	}
	if (store != NULL && store[0]) {
		char root[MAX_PATH];

		if (snprintf(root, sizeof(root), "%sTHEMES/", store) < (int)sizeof(root) &&
		    isPathDeviceStackLoaded(root) &&
		    (n == 0 || strcmp(roots[0], root) != 0))
			dashStrCopy(roots[n++], root, MAX_PATH);
	}
	return n;
}

int dashThemeFolderList(char names[][DASH_THEME_NAME_MAX], int max)
{
	char roots[2][MAX_PATH];
	FILEINFO *files;
	int nr, ri, n, i, count = 0;

	nr = themeRoots(roots);
	if (nr <= 0)
		return 0;
	files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	if (files == NULL)
		return 0;
	for (ri = 0; ri < nr; ri++) {
		filerRootMcPollIdle();
		n = getDir(roots[ri], files);
		for (i = 0; i < n && i < MAX_ENTRY - 2 && count < max; i++) {
			FILEINFO *f = &files[i];
			int j, dup = 0;

			if (f->name[0] == '\0' || f->name[0] == '.')
				continue;
			if (!(f->stats.AttrFile & sceMcFileAttrSubdir))
				continue;
			if (strlen(f->name) >= DASH_THEME_NAME_MAX)
				continue;
			for (j = 0; j < count; j++)
				if (!stricmp(names[j], f->name))
					dup = 1;
			if (!dup)
				dashStrCopy(names[count++], f->name, DASH_THEME_NAME_MAX);
		}
	}
	free(files);
	return count;
}

typedef struct
{
	DashTheme *t;
	const char *folder_path;
} FolderCtx;

static void resolveInFolder(char *field, int size, const char *folder_path)
{
	char tmp[DASH_PATH_MAX];

	if (field[0] == '\0' || strchr(field, ':') != NULL)
		return;
	if (snprintf(tmp, sizeof(tmp), "%s%s", folder_path, field) >= (int)sizeof(tmp)) {
		field[0] = '\0';
		return;
	}
	dashStrCopy(field, tmp, size);
}

static int folderKv(const char *key, char *value, void *user)
{
	FolderCtx *ctx = (FolderCtx *)user;

	dashThemeParseKey(ctx->t, key, value);
	return 0;
}

int dashThemeFolderLoad(const char *folder)
{
	char roots[2][MAX_PATH], dir[MAX_PATH], path[MAX_PATH];
	char *text;
	int nr, ri, len;
	DashTheme t;
	FolderCtx ctx;

	nr = themeRoots(roots);
	for (ri = 0; ri < nr; ri++) {
		if (snprintf(dir, sizeof(dir), "%s%s/", roots[ri], folder) >= DASH_PATH_MAX - 32)
			continue;
		if (snprintf(path, sizeof(path), "%stheme.cfg", dir) >= (int)sizeof(path))
			continue;
		filerRootMcPollIdle();
		text = dashLoadTextFile(path, 16 * 1024, &len);
		if (text == NULL)
			continue; /* not in this root: try the next */
		/* start from the default so a sparse theme.cfg still makes sense */
		dashThemeLoadPreset(P_OSD, &t);
		t.name[0] = '\0';
		ctx.t = &t;
		ctx.folder_path = dir;
		dashKvParse(text, folderKv, &ctx);
		free(text);
		if (t.name[0] == '\0')
			dashStrCopy(t.name, folder, sizeof(t.name));
		resolveInFolder(t.image, sizeof(t.image), dir);
		resolveInFolder(t.app_icon, sizeof(t.app_icon), dir);
		/* an image background without an image falls back to the gradient */
		if (t.bg == DASH_BG_IMAGE && t.image[0] == '\0')
			t.bg = DASH_BG_GRADIENT;
		dashTheme = t;
		dashThemeCommit();
		return 0;
	}
	return -1;
}

int dashThemeFolderExport(const char *folder)
{
	char roots[2][MAX_PATH], dir[MAX_PATH], fixed[MAX_PATH], path[MAX_PATH];
	char *buf;
	int nr, len, ret;
	const int cap = 8192;

	nr = themeRoots(roots);
	if (nr <= 0)
		return -1;
	filerRootMcPollIdle();
	/* create THEMES/ and THEMES/<folder>/ under the first root (errors = already there) */
	dashStrCopy(dir, roots[0], sizeof(dir));
	dir[strlen(dir) - 1] = '\0';
	if (genFixPath(dir, fixed) >= 0)
		genMkdir(fixed, fileMode);
	if (snprintf(dir, sizeof(dir), "%s%s", roots[0], folder) >= DASH_PATH_MAX - 32)
		return -1;
	if (genFixPath(dir, fixed) >= 0)
		genMkdir(fixed, fileMode);
	if (snprintf(path, sizeof(path), "%s/theme.cfg", dir) >= (int)sizeof(path))
		return -1;

	buf = (char *)malloc(cap);
	if (buf == NULL)
		return -1;
	len = snprintf(buf, cap,
	               "# wLaunchELF dashboard theme\r\n"
	               "# Colours: #RRGGBB or #RRGGBBAA (AA = opacity). Image paths are relative\r\n"
	               "# to this folder unless they contain a device (mc0:/...).\r\n");
	len += dashThemeSerialize(&dashTheme, buf + len, cap - len, "");
	ret = dashWriteFile(path, buf, len);
	free(buf);
	return ret;
}

//--------------------------------------------------------------
// Editing support (SETTINGS > Theme)
//--------------------------------------------------------------
static int theme_dirty;

int dashThemeBasePreset(void)
{
	return theme_preset;
}

void dashThemeRestore(const DashTheme *t, int preset, int custom)
{
	dashTheme = *t;
	theme_preset = (preset >= 0 && preset < P_COUNT) ? preset : P_OSD;
	theme_custom = custom ? 1 : 0;
	dashThemeValidate(&dashTheme);
	dashThemeApply();
}

/* Put one settings group back to the base preset's values. */
void dashThemeResetGroup(int group)
{
	DashTheme p, *t = &dashTheme;

	dashThemeLoadPreset(theme_preset, &p);
	switch (group) {
		case DASH_TG_BACKGROUND:
			t->bg = p.bg;
			t->bg1 = p.bg1;
			t->bg2 = p.bg2;
			t->bg_bright = p.bg_bright;
			t->scanlines = p.scanlines;
			dashStrCopy(t->image, p.image, sizeof(t->image));
			break;
		case DASH_TG_COLORS:
			t->text = p.text;
			t->text2 = p.text2;
			t->text3 = p.text3;
			t->sel_text = p.sel_text;
			t->accent = p.accent;
			t->accent2 = p.accent2;
			t->select = p.select;
			t->warn = p.warn;
			t->danger = p.danger;
			t->ok = p.ok;
			t->panel = p.panel;
			t->border = p.border;
			break;
		case DASH_TG_STYLE:
			t->corner = p.corner;
			t->highlight = p.highlight;
			t->glow = p.glow;
			t->panel.a = p.panel.a;
			break;
		case DASH_TG_TYPE:
			t->font = p.font;
			t->font_scale = p.font_scale;
			t->bold = p.bold;
			t->text_shadow = p.text_shadow;
			break;
		case DASH_TG_ICONS:
			t->icon_tint = p.icon_tint;
			t->app_icon_style = p.app_icon_style;
			dashStrCopy(t->app_icon, p.app_icon, sizeof(t->app_icon));
			break;
		case DASH_TG_CLOCK:
			t->clock = p.clock;
			t->clock_pos = p.clock_pos;
			t->clock_hours = p.clock_hours;
			t->clock_seconds = p.clock_seconds;
			t->clock_date = p.clock_date;
			break;
		case DASH_TG_WALLPAPER:
			t->wallpaper = p.wallpaper;
			t->wall_dim = p.wall_dim;
			break;
	}
	dashThemeCommit();
}

/* Theme edits are saved once the pad has been idle for a moment, so
 * adjusting a colour does not write the memory card on every step. */
void dashThemeMarkDirty(void)
{
	theme_dirty = 1;
}

void dashThemeSavePending(int idle_ms)
{
	if (!theme_dirty || idle_ms < 1200)
		return;
	theme_dirty = 0;
	filerRootMcPollIdle();
	dashStoreSaveSettings();
}

//--------------------------------------------------------------
// App-aware wallpaper focus
//--------------------------------------------------------------
void dashThemeSetFocusApp(void *app)
{
	focus_app = app;
}

void *dashThemeFocusApp(void)
{
	return focus_app;
}
//--------------------------------------------------------------
//End of file: dash_themes.c
//--------------------------------------------------------------
