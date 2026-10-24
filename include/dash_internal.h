//--------------------------------------------------------------
//File name:   dash_internal.h
//Shared declarations between the dashboard modules (dash_*.c).
//
//Coordinate system: every dashboard drawing call uses a virtual 640x448
//canvas. dash_gfx.c maps the Y axis to the real framebuffer height, so PAL
//(640x512) keeps the same proportions as NTSC/480p without separate layouts.
//--------------------------------------------------------------
#ifndef DASH_INTERNAL_H
#define DASH_INTERNAL_H

#include "launchelf.h"
#include "main_actions.h"
#include "dash.h"

#define DASH_VW 640
#define DASH_VH 448

/* Conservative CRT safe area (virtual coordinates). */
#define DASH_SAFE_L 40
#define DASH_SAFE_R 600
#define DASH_SAFE_T 26
#define DASH_SAFE_B 424

/* Bounded storage sizes. Paths longer than DASH_PATH_MAX are skipped by the
 * library (the FileBrowser still handles them). */
#define DASH_PATH_MAX 256
#define DASH_TITLE_MAX 64
#define DASH_DESC_MAX 160
#define DASH_SHORT_MAX 40

#define DASH_MAX_APPS 512
#define DASH_MAX_SCAN 12
#define DASH_MAX_FAV 48
#define DASH_MAX_RECENT 16

//--------------------------------------------------------------
// Colors
//--------------------------------------------------------------
typedef struct
{
	u8 r, g, b, a; /* a: 0..255 (converted to the GS 0..128 range on draw) */
} DashColor;

static inline DashColor DC(int r, int g, int b, int a)
{
	DashColor c;
	c.r = (u8)r;
	c.g = (u8)g;
	c.b = (u8)b;
	c.a = (u8)a;
	return c;
}

static inline DashColor dashColorA(DashColor c, float mul)
{
	int a = (int)(c.a * mul);
	if (a < 0)
		a = 0;
	if (a > 255)
		a = 255;
	c.a = (u8)a;
	return c;
}

DashColor dashColorLerp(DashColor a, DashColor b, float t);
DashColor dashColorHue(int hue, int sat, int val, int alpha); /* hue 0..359 */

/* Palette (dash_gfx.c). Filled from the active theme by dashThemeApply();
 * the initial values are the built-in fallback theme. */
extern DashColor DASH_C_TEXT;
extern DashColor DASH_C_TEXT2;
extern DashColor DASH_C_TEXT3;
extern DashColor DASH_C_ACCENT;
extern DashColor DASH_C_ACCENT2;
extern DashColor DASH_C_WARM;
extern DashColor DASH_C_DANGER;
extern DashColor DASH_C_OK;
extern DashColor DASH_C_PANEL;
extern DashColor DASH_C_PANEL_HI;
extern DashColor DASH_C_SHADOW;
extern DashColor DASH_C_SELECT;    /* selection fill */
extern DashColor DASH_C_SELEDGE;   /* selection outline */
extern DashColor DASH_C_SELTEXT;   /* text of the selected row */
extern DashColor DASH_C_BORDER;    /* panel borders (alpha 0 = none) */

//--------------------------------------------------------------
// Theme engine (dash_themes.c)
//
// One DashTheme holds every visual value the dashboard uses. Built-in
// presets fill it, the user edits it in Settings > Appearance, and it is saved
// as Theme_* keys in DASHBOARD.CNF. Theme folders
// (<dashboard dir>/THEMES/<name>/theme.cfg) use the same keys without the
// prefix. Every value is validated/clamped; unknown keys are ignored.
//--------------------------------------------------------------
enum DashBgKind {
	DASH_BG_SOLID = 0,
	DASH_BG_GRADIENT,
	DASH_BG_TUNNEL,    /* OSDSYS menu backdrop: indigo tunnel */
	DASH_BG_MIST,      /* drifting translucent blue forms */
	DASH_BG_TOWERS,    /* opening tower field */
	DASH_BG_DISC,      /* red disc-error atmosphere */
	DASH_BG_ORBIT,     /* near black, ring of eight lights */
	DASH_BG_CLOCKRING, /* System Configuration twelve-rod clock */
	DASH_BG_GRID,      /* memory card grid */
	DASH_BG_LINES,     /* boot lines */
	DASH_BG_CUBES,     /* wireframe cubes */
	DASH_BG_STREAM,    /* data stream */
	DASH_BG_TILES,     /* memory card blue tiles */
	DASH_BG_BRUSHED,   /* brushed console finish */
	DASH_BG_WAVE,      /* PSX-style ribbons */
	DASH_BG_CRYSTAL,   /* the previous dashboard atmosphere */
	DASH_BG_STREAKS,   /* light pillars sweeping across the screen */
	DASH_BG_CLOUDS,    /* layered drifting fog */
	DASH_BG_IMAGE,     /* wallpaper PNG from the theme */
	DASH_BG_COUNT
};

enum DashHighlight {
	DASH_HL_FILL = 0, /* solid selection bar with outline */
	DASH_HL_BAR,      /* accent edge bar and a faint fill */
	DASH_HL_OUTLINE,  /* outline only */
	DASH_HL_TEXT,     /* colour change only (OSDSYS style) */
	DASH_HL_COUNT
};

enum DashCorner {
	DASH_CORNER_SQUARE = 0,
	DASH_CORNER_SOFT,
	DASH_CORNER_ROUND,
	DASH_CORNER_COUNT
};

enum DashFontFace {
	DASH_FONT_ROBOTO = 0, /* smooth proportional (bundled) */
	DASH_FONT_SYSTEM,     /* wLaunchELF bitmap font / user .fnt (localized glyphs) */
	DASH_FONT_COUNT
};

enum DashTileStyle {
	DASH_TILE_MEMORY_CARD = 0,
	DASH_TILE_ROUNDED,
	DASH_TILE_CHAMFERED,
	DASH_TILE_FLOATING,
	DASH_TILE_CRYSTAL,
	DASH_TILE_COUNT
};

enum DashIconTint {
	DASH_ICONS_TEXT = 0, /* icons follow the text colours */
	DASH_ICONS_ACCENT,   /* icons lean towards the accent colour */
	DASH_ICONS_MONO,     /* greyscale */
	DASH_ICONS_COUNT
};

enum DashAppIcon {
	DASH_APPICON_SYSTEM = 0, /* default application icon */
	DASH_APPICON_DEVICE,     /* icon of the device the app lives on */
	DASH_APPICON_MONOGRAM,   /* initials on a coloured tile (old style) */
	DASH_APPICON_COUNT
};

enum DashClockMode {
	DASH_CLOCK_OFF = 0,
	DASH_CLOCK_STANDARD, /* time over date */
	DASH_CLOCK_SYSTEM,   /* OSDSYS style: date and time on one line */
	DASH_CLOCK_COUNT
};

enum DashClockPos {
	DASH_CLOCKPOS_HEADER = 0,
	DASH_CLOCKPOS_FOOTER,
	DASH_CLOCKPOS_COUNT
};

enum DashClockHours {
	DASH_HOURS_SYSTEM = 0, /* console setting */
	DASH_HOURS_24,
	DASH_HOURS_12,
	DASH_HOURS_COUNT
};

enum DashClockDate {
	DASH_DATE_SYSTEM = 0,
	DASH_DATE_YMD,
	DASH_DATE_MDY,
	DASH_DATE_DMY,
	DASH_DATE_OFF,
	DASH_DATE_COUNT
};

enum DashWallMode {
	DASH_WALL_OFF = 0,
	DASH_WALL_FOCUS, /* artwork of the focused application */
	DASH_WALL_COUNT
};

#define DASH_THEME_NAME_MAX 32

typedef struct
{
	char name[DASH_THEME_NAME_MAX];
	char author[DASH_THEME_NAME_MAX];
	char image[DASH_PATH_MAX];    /* wallpaper PNG (DASH_BG_IMAGE) */
	char app_icon[DASH_PATH_MAX]; /* optional PNG default application icon */
	/* background */
	u8 bg;
	u8 bg_bright; /* 10..200 % */
	u8 scanlines; /* CRT line overlay */
	DashColor bg1, bg2;
	/* colours */
	DashColor text, text2, text3, sel_text;
	DashColor accent, accent2, select, warn, danger, ok;
	DashColor panel, border;
	/* shapes */
	u8 corner, highlight, glow, tile_style;
	/* typography */
	u8 font, font_scale, bold, text_shadow; /* font_scale: 90..115 % */
	/* icons */
	u8 icon_tint, app_icon_style;
	/* clock */
	u8 clock, clock_pos, clock_hours, clock_seconds, clock_date;
	/* app-aware wallpaper */
	u8 wallpaper, wall_dim; /* wall_dim: 0..90 % */
} DashTheme;

typedef struct
{
	const char *id;   /* key used in configuration files */
	const char *name; /* shown in SETTINGS */
	const char *desc;
} DashThemePresetInfo;

extern DashTheme dashTheme;

void dashThemeInit(void);
int dashThemePresetCount(void);
const DashThemePresetInfo *dashThemePresetInfo(int i);
int dashThemePresetIndex(void); /* active preset, -1 = customised */
int dashThemeFindPreset(const char *id);
void dashThemeLoadPreset(int i, DashTheme *out);
void dashThemeSetPreset(int i);  /* load + apply, marks as not customised */
void dashThemeCommit(void);      /* validate + apply after an edit */
void dashThemeMarkCustom(void);
int dashThemeIsCustom(void);
void dashThemeApply(void);       /* push dashTheme into palette/renderer */
void dashThemeValidate(DashTheme *t);
/* key/value interface shared by DASHBOARD.CNF (Theme_ prefix) and theme.cfg */
int dashThemeParseKey(DashTheme *t, const char *key, char *value);
int dashThemeSerialize(const DashTheme *t, char *buf, int cap, const char *prefix);
/* DASHBOARD.CNF hooks (dash_store.c) */
void dashThemeStoreBegin(void);
int dashThemeStoreKey(const char *key, char *value);
void dashThemeStoreEnd(void);
int dashThemeStoreWrite(char *buf, int cap);
/* theme folders */
int dashThemeFolderList(char names[][DASH_THEME_NAME_MAX], int max);
int dashThemeFolderLoad(const char *folder);
int dashThemeFolderExport(const char *folder);
/* split versions for the background worker: read/write do the IO, the
 * caller applies (dashTheme = t; dashThemeCommit()) on the UI thread */
int dashThemeFolderRead(const char *folder, DashTheme *out);
char *dashThemeFolderText(int *out_len);
int dashThemeFolderWrite(const char *folder, const char *text, int len);
const char *dashThemeBgName(int kind);
const char *dashThemeBgId(int kind);
/* focused application for the app-aware wallpaper (set while drawing) */
enum DashThemeGroup {
	DASH_TG_BACKGROUND = 0,
	DASH_TG_COLORS,
	DASH_TG_STYLE,
	DASH_TG_TYPE,
	DASH_TG_ICONS,
	DASH_TG_CLOCK,
	DASH_TG_WALLPAPER
};
int dashThemeBasePreset(void);
void dashThemeRestore(const DashTheme *t, int preset, int custom);
void dashThemeResetGroup(int group);
void dashThemeMarkDirty(void);
void dashThemeSavePending(int idle_ms); /* saves after ~1.2 s of pad idle */
void dashThemeSetFocusApp(void *app);
void *dashThemeFocusApp(void);
/* derived shape helpers */
float dashThemeRadius(float r);
void dashThemeBaseColors(const DashTheme *t, DashColor *top, DashColor *bottom);
DashColor dashColorMix(DashColor a, DashColor b, float t); /* keeps a's alpha */
DashColor dashColorLighten(DashColor c, float t);
DashColor dashColorScale(DashColor c, float k);

//--------------------------------------------------------------
// Math / animation helpers
//--------------------------------------------------------------
static inline float dashClampf(float v, float lo, float hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}
static inline float dashLerpf(float a, float b, float t)
{
	return a + (b - a) * t;
}
float dashApproach(float cur, float target, float speed, float dt); /* exp smoothing */
float dashEaseOutCubic(float t);
float dashSinf(float x);
float dashCosf(float x);

//--------------------------------------------------------------
// Renderer (dash_gfx.c)
//--------------------------------------------------------------
enum DashBlend {
	DASH_BLEND_NORMAL = 0,
	DASH_BLEND_ADD
};

void dashGfxInit(void);
void dashGfxShutdown(void);
int dashGfxCheckScreenChange(void); /* re-inits VRAM state after a mode switch */
void dashGfxBeginFrame(void);
void dashGfxPresent(void);
void dashGfxRestoreLegacyState(void);
float dashGfxScaleY(void);
float dashGfxTextY(float y);

void dashGfxPushAlpha(float mul);
void dashGfxPopAlpha(void);
void dashGfxPushOffset(float dx, float dy);
void dashGfxPopOffset(void);
void dashGfxBlend(int mode);
void dashGfxScissor(float x, float y, float w, float h);
void dashGfxScissorReset(void);
/* Nested child clipping intersects the parent's GS scissor and restores it. */
void dashGfxScissorPush(float x, float y, float w, float h);
void dashGfxScissorPop(void);

int dashTexBind(GSTEXTURE *tex);
void dashTexInvalidate(GSTEXTURE *tex);
void dashTexRelease(GSTEXTURE *tex);

void dashRect(float x, float y, float w, float h, DashColor c);
void dashRectGradV(float x, float y, float w, float h, DashColor top, DashColor bottom);
void dashRectGradH(float x, float y, float w, float h, DashColor left, DashColor right);
void dashRectGrad4(float x, float y, float w, float h, DashColor tl, DashColor tr, DashColor bl, DashColor br);
void dashQuadG(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
               DashColor c1, DashColor c2, DashColor c3, DashColor c4);
void dashLine(float x1, float y1, float x2, float y2, float w, DashColor c);
void dashTri(float x1, float y1, float x2, float y2, float x3, float y3, DashColor c1, DashColor c2, DashColor c3);
void dashRoundedGradV(float x, float y, float w, float h, float r, DashColor top, DashColor bottom);
void dashRoundedBorder(float x, float y, float w, float h, float r, DashColor c);
void dashPanel(float x, float y, float w, float h, float r, DashColor fill);
void dashPanelGradV(float x, float y, float w, float h, float r, DashColor top, DashColor bottom);
void dashPanelBorder(float x, float y, float w, float h, float r, DashColor c);
void dashGlow(float cx, float cy, float rx, float ry, DashColor c);
void dashGlowRect(float x, float y, float w, float h, float spread, DashColor c);
void dashShadow(float x, float y, float w, float h, float spread, float alpha);
void dashSparkle(float cx, float cy, float size, DashColor c);
void dashTexDraw(GSTEXTURE *tex, float x, float y, float w, float h, DashColor tint);
void dashTexDrawUV(GSTEXTURE *tex, float x, float y, float w, float h,
                   float u1, float v1, float u2, float v2, DashColor tint);
void dashTexDrawQuadUV(GSTEXTURE *tex,
                       float x1, float y1, float u1, float v1,
                       float x2, float y2, float u2, float v2,
                       float x3, float y3, float u3, float v3,
                       float x4, float y4, float u4, float v4,
                       DashColor tint);
void dashTexDrawFit(GSTEXTURE *tex, float x, float y, float w, float h, DashColor tint);

/* Batched textured sprites (used by text and icons). */
void dashSpriteBatchBegin(GSTEXTURE *tex, DashColor c);
void dashSpriteBatchAdd(float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2);
void dashSpriteBatchEnd(void);

//--------------------------------------------------------------
// Font (dash_font.c)
//--------------------------------------------------------------
/* Type scale: size in px = value * 20 (Roboto, see dash_font.c) */
#define DASH_FS_CAPTION 0.60f /* 12 px */
#define DASH_FS_SMALL 0.66f   /* 13 px */
#define DASH_FS_BODY 0.76f    /* 15 px */
#define DASH_FS_LABEL 0.86f   /* 17 px */
#define DASH_FS_TITLE 1.10f   /* 22 px */
#define DASH_FS_HERO 1.40f    /* 28 px */

void dashFontInit(void);
void dashFontRebuild(void);
void dashFontShutdown(void);
float dashTextWidth(const char *s, float scale);
float dashTextWidthN(const char *s, int n, float scale);
float dashTextLineH(float scale);
float dashText(float x, float y, const char *s, float scale, DashColor c);
float dashTextS(float x, float y, const char *s, float scale, DashColor c); /* with drop shadow */
void dashTextCenter(float cx, float y, const char *s, float scale, DashColor c);
void dashTextRight(float rx, float y, const char *s, float scale, DashColor c);
float dashTextFit(float x, float y, float maxw, const char *s, float scale, DashColor c);
void dashTextScroll(float x, float y, float maxw, const char *s, float scale, DashColor c);
void dashTextScrollTimed(float x, float y, float maxw, const char *s, float scale, DashColor c, float age);
void dashTextScrollCenter(float cx, float y, float maxw, const char *s, float scale, DashColor c);
int dashTextWrap(float x, float y, float w, int max_lines, const char *s, float scale, DashColor c, float line_h);
/* Legacy 8px monospace glyph (for the themed classic screens). */
void dashFontLegacyChar(unsigned int c, int x, int y, u64 gs_color);
GSTEXTURE *dashFontTexture(void);

//--------------------------------------------------------------
// Icons (dash_icons.c)
//--------------------------------------------------------------
enum DashIcon {
	DI_HOME = 0,
	DI_APPS,
	DI_FOLDER,
	DI_DEVICES,
	DI_TOOLS,
	DI_SETTINGS,
	DI_MC,
	DI_USB,
	DI_HDD,
	DI_DISC,
	DI_NETWORK,
	DI_STAR,
	DI_CLOCK,
	DI_APP,
	DI_PLAY,
	DI_INFO,
	DI_WARNING,
	DI_POWER,
	DI_TEXT,
	DI_EDIT,
	DI_REFRESH,
	DI_PIN,
	DI_PLUS,
	DI_CHECK,
	DI_CLOSE,
	DI_SDCARD,
	DI_CHIP,
	DI_VIEW_GRID,
	DI_VIEW_LIST,
	DI_VIEW_CAROUSEL,
	DI_FILE,
	DI_BACK,
	DI_PAD_BADGE,   /* shared dark rounded-square tile drawn behind pad glyphs */
	DI_BTN_CROSS,
	DI_BTN_CIRCLE,
	DI_BTN_SQUARE,
	DI_BTN_TRIANGLE,
	DI_BTN_UP,
	DI_BTN_DOWN,
	DI_BTN_LEFT,
	DI_BTN_RIGHT,
	DI_BTN_START,
	DI_BTN_SELECT,
	DI_BTN_L1,
	DI_BTN_L2,
	DI_BTN_L3,
	DI_BTN_R1,
	DI_BTN_R2,
	DI_BTN_R3,
	DI_KEY,
	DI_GAMEPAD,
	DI_EXEC,    /* default application icon */
	DI_PALETTE, /* theme settings */
	DI_RECENT,  /* recently used */
	DI_IMAGE,   /* wallpaper / picture */
	DI_DPAD_UD, /* D-pad with up/down highlighted ("scroll") */
	DI_COUNT    /* the SDF atlas holds 56 icons (7 x 8) and is now full, pad glyphs included */
};

void dashIconsInit(void);
void dashIconsShutdown(void);
void dashIcon(int id, float x, float y, float size, DashColor c);
DashColor dashIconTint(DashColor c);
GSTEXTURE *dashIconsTexture(void);

//--------------------------------------------------------------
// PNG decoding (dash_png.c)
//--------------------------------------------------------------
#define DASH_PNG_MAX_FILE (1536 * 1024)
/* Decodes a PNG file into a newly memalign'd CT32 buffer (GS alpha range),
 * box-downscaled to fit max_w x max_h. Returns 0 on success. */
int dashPngLoadFile(const char *path, int max_w, int max_h, u32 **out, int *out_w, int *out_h);
int dashPngDecode(const u8 *data, int size, int max_w, int max_h, u32 **out, int *out_w, int *out_h);
int dashJpegLoadFile(const char *path, int max_w, int max_h, u32 **out, int *out_w, int *out_h);
int dashJpegDecode(const u8 *data, int size, int max_w, int max_h, u32 **out, int *out_w, int *out_h);

//--------------------------------------------------------------
// Settings / persistence (dash_store.c)
//--------------------------------------------------------------
enum DashSection {
	DASH_SEC_HOME = 0,
	DASH_SEC_APPS,
	DASH_SEC_FILES,
	DASH_SEC_DEVICES,
	DASH_SEC_TOOLS,
	DASH_SEC_SETTINGS,
	DASH_SEC_COUNT
};

enum DashView {
	DASH_VIEW_GRID = 0,
	DASH_VIEW_LIST,
	DASH_VIEW_CAROUSEL,
	DASH_VIEW_COUNT
};

enum DashFx {
	DASH_FX_OFF = 0,
	DASH_FX_SUBTLE,
	DASH_FX_FULL,
	DASH_FX_COUNT
};

enum DashAnim {
	DASH_ANIM_OFF = 0,
	DASH_ANIM_REDUCED,
	DASH_ANIM_FULL,
	DASH_ANIM_COUNT
};

enum DashIgrAfter {
	DASH_IGR_AFTER_DASHBOARD = 0,
	DASH_IGR_AFTER_OSDSYS,
	DASH_IGR_AFTER_APP,
	DASH_IGR_AFTER_COUNT
};

typedef struct
{
	int enabled;
	int start_section;
	int view;
	int effects; /* background motion, 0..200 % */
	int anim;    /* menu animation, 0..200 % */
	int autoscan;
	int artwork;
	int recent_count;
	int legacy_theme;
	int intro_done;
	int sound;
	int sound_vol;
	char software_clock[24]; /* optional local seed; resumes saved time without RTC */
	int bgm;
	int bgm_vol;
	int app_filter;
	int files_mode;              /* 0 = Files list, 1 = Devices cards */
	int home_shortcuts;          /* HOME quick-launch row */
	int icon_backing;            /* control/quick-action backing shapes */
	int show_unavailable_devices;/* include disconnected/probe-only device cards */
	int dev_auto; /* DASH_DEVA_* bitmask: drivers to load at startup */
	int mmce_backup_keep;   /* 0 = all, otherwise 1..99 */
	int mmce_backup_format; /* MMCE_BACKUP_FORMAT_* */
	int igr_auto_backup;    /* run MMCE backup when entered through WLE-IGR */
	int igr_cloud_sync;     /* upload WLE-MMCE-Backups to Google Drive after IGR */
	int igr_after;          /* DashIgrAfter */
	char igr_backup_path[DASH_PATH_MAX]; /* local root selected by IGR setup */
	char igr_after_path[DASH_PATH_MAX];
	int nscan;
	char scan[DASH_MAX_SCAN][DASH_PATH_MAX];
} DashSettings;

/* per-device "auto-start at dashboard startup" bits (dev_auto) */
#define DASH_DEVA_USB    (1 << 0)
#define DASH_DEVA_HDD    (1 << 1)
#define DASH_DEVA_ATA    (1 << 2) /* exFAT */
#define DASH_DEVA_MMCE   (1 << 3)
#define DASH_DEVA_MX4SIO (1 << 4)
#define DASH_DEVA_UDPFS  (1 << 5)
#define DASH_DEVA_ILINK  (1 << 6)
#define DASH_DEVA_ALL    0x7F

extern DashSettings dashCfg;

/* Favorites / recents / pins are stored as raw executable paths. */
typedef struct
{
	int nfav;
	char fav[DASH_MAX_FAV][DASH_PATH_MAX];
	int npin;
	char pin[DASH_MAX_FAV][DASH_PATH_MAX];
	int nrecent;
	char recent[DASH_MAX_RECENT][DASH_PATH_MAX];
} DashUserLists;

extern DashUserLists dashLists;

void dashStoreInit(void);
int dashStoreLoaded(void);
void dashStoreDefaults(void);
/* Saves are deferred: these only mark files dirty. dashStoreFlush() hands
 * dirty files to the background worker (after ~0.7 s of pad idle unless
 * forced); dashStoreFlushWait() also waits for the writes (0 = cancelled). */
int dashStoreSaveSettings(void);    /* commits dashCfg and schedules DASHBOARD.CNF */
void dashStoreSaveTheme(void);
int dashStoreSettingsPending(void); /* DASHBOARD.CNF write queued or running */
int dashStoreSettingsFailed(void);  /* last DASHBOARD.CNF write failed */      /* DASHBOARD.CNF with the committed settings */
const DashSettings *dashStorePersisted(void);
char *dashStoreSettingsText(const DashSettings *c, int *out_len); /* malloc'd DASHBOARD.CNF text */
int dashStoreSaveLists(void);
void dashStoreSaveCache(void);
void dashStoreFlush(int force);
int dashStoreFlushWait(const char *title);
const char *dashStoreDir(void);
int dashStoreBuildPath(char *out, int out_size, const char *file);
int dashStoreIsFavorite(const char *path);
int dashStoreIsPinned(const char *path);
int dashStoreToggleFavorite(const char *path);
int dashStoreTogglePinned(const char *path);
void dashStoreRemoveEverywhere(const char *path);
void dashStorePushRecent(const char *path);
void dashStoreClearRecent(void);

/* Tiny lenient "key = value" line reader shared by the dash modules.
 * Unlike get_CNF_string it skips malformed lines instead of stopping. */
typedef int (*DashKvCallback)(const char *key, char *value, void *user);
int dashKvParse(char *text, DashKvCallback cb, void *user);
char *dashLoadTextFile(const char *path, int max_size, int *out_len);
int dashWriteFile(const char *path, const void *data, int size);
void dashStrCopy(char *dst, const char *src, int dst_size);
char *dashTrim(char *s);

//--------------------------------------------------------------
// ELF library (dash_lib.c)
//--------------------------------------------------------------
enum DashAppFlags {
	DASH_APP_HAS_META = 1 << 0,
	DASH_APP_HAS_ARGS = 1 << 1,
	DASH_APP_MISSING = 1 << 2,
	DASH_APP_META_PIN = 1 << 3,
	DASH_APP_TRANSIENT = 1 << 4, /* recent/fav entry not found in library */
	DASH_APP_ART_DEFERRED = 1 << 5 /* art lookup waits for the device driver */
};

enum DashArtState {
	DASH_ART_UNKNOWN = 0,
	DASH_ART_FOUND,
	DASH_ART_NONE
};

typedef struct
{
	char path[DASH_PATH_MAX];
	char title[DASH_TITLE_MAX];
	char desc[DASH_DESC_MAX];
	char author[DASH_SHORT_MAX];
	char version[24];
	char category[24];
	char art[DASH_PATH_MAX];
	u8 art_state;
	u8 flags;
	u16 hue;
} DashApp;

extern DashApp *dashApps;
extern int dashAppCount;

typedef void (*DashProgressFn)(const char *status, int done, int total);

void dashLibInit(void);
void dashLibShutdown(void);
int dashLibLoadCache(void);
int dashLibSaveCache(void);                /* schedules DASHLIB.CNF */
char *dashLibBuildCache(int *out_len);     /* malloc'd DASHLIB.CNF text */
/* Background scan: dashLibScanStart() queues it (0, or -1 when out of
 * memory); the main loop calls dashLibApplyScan() each frame, which returns
 * the new application count once a scan finished (-2: nothing to apply,
 * -1: the scan failed). */
int dashLibScanStart(int interactive);
int dashLibScanRunning(void);
int dashLibApplyScan(int *interactive);
DashApp *dashLibFind(const char *path);
int dashLibIndexOf(const DashApp *app);
/* Resolves a path from favorites/recents to a library entry, or fills a
 * transient entry when the app is not (or no longer) in the library. */
DashApp *dashLibResolve(const char *path, DashApp *transient);
void dashLibMakeTransient(DashApp *app, const char *path);
int dashLibLoadArgs(const DashApp *app, char *buf, int buf_size);
void dashLibDeviceLabel(const char *path, char *out, int out_size);
int dashLibDeviceIcon(const char *path);
void dashLibPrettyName(const char *path, char *out, int out_size);
int dashLibRemove(int index);
void dashLibRefreshApp(DashApp *app);
void dashHomeRebuild(void);
int dashLibMetadataPath(const DashApp *app, char *out, int out_size);

//--------------------------------------------------------------
// Artwork cache (dash_art.c)
//--------------------------------------------------------------
void dashArtInit(void);
void dashArtShutdown(void);
void dashArtBeginFrame(void);
/* Returns a ready texture for the app or NULL (fallback tile is drawn by the
 * caller). prio: 0 = focused item, larger = farther from focus. */
GSTEXTURE *dashArtGet(DashApp *app, int prio);
/* Performs at most one probe/decode step; returns non-zero if it did IO. */
int dashArtStep(int input_idle_ms);
void dashArtForget(const DashApp *app);
int dashArtTakeDirty(void);
void dashArtFlushAll(void);
void dashArtInvalidateVram(void);
/* Themed PNG loaded from the dashboard THEME folder (NULL if absent). */
GSTEXTURE *dashThemeTexture(const char *name, int max_w, int max_h);
GSTEXTURE *dashThemeTextureFile(const char *path, int max_w, int max_h);
void dashThemeTexturesFree(void);

/* Procedural fallback tile for an app without artwork. */
void dashDrawAppTile(const DashApp *app, float x, float y, float w, float h, float focus);
void dashDrawTileFocus(float x, float y, float w, float h, float r, float strength);
/* Artwork or fallback, fitted into a rounded card. */
void dashDrawAppArt(DashApp *app, float x, float y, float w, float h, float focus, int prio);

//--------------------------------------------------------------
// Background (dash_bg.c)
//--------------------------------------------------------------
void dashBgInit(void);
void dashBgUpdate(float dt);
void dashBgDraw(void);
void dashBgDrawStatic(void); /* no animation (legacy screens / FX off) */
void dashBgFlash(float amount);

//--------------------------------------------------------------
// Devices (dash_devices.c)
//--------------------------------------------------------------
enum DashDevState {
	DASH_DEV_READY = 0,
	DASH_DEV_PROBE,  /* storage not checked yet: press to detect */
	DASH_DEV_ABSENT,
	DASH_DEV_BUSY
};

typedef struct
{
	int icon;
	int state;
	char name[32];
	char mount[24];
	char browse[24];
	char status[48];
	char detail[48];
	float fill; /* 0..1 used space, <0 unknown */
} DashDevice;

#define DASH_MAX_DEVICES 24
void dashDevicesInit(void);
void dashDevicesPoll(void);
void dashDevicesAutoProbe(void);
void dashDevicesResetBaseline(void);
int dashBootDeviceBit(void);
const char *dashDeviceDisplayMount(const DashDevice *d, char *buf, int size);
int dashDevicesList(DashDevice *out, int max);
/* Starts a device's drivers (UI thread, loading overlay); `cb` is called
 * with ok = device usable. -1 = the user backed out of the wait. */
typedef void (*DashProbeDone)(const char *mount, int ok, void *user);
int dashDevicesStart(const DashDevice *dev, DashProbeDone cb, void *user);
void dashDevicesAutoStart(void);                 /* auto-start devices that are off */
void dashDevicesStartForPath(const char *path);  /* driver for a path's device */
const char *dashDiscStatus(void);
int dashMcPresent(int port);
int dashDevicePathReady(const char *path);

//--------------------------------------------------------------
// Input
//--------------------------------------------------------------
enum {
	DASH_IN_UP = 1 << 0,
	DASH_IN_DOWN = 1 << 1,
	DASH_IN_LEFT = 1 << 2,
	DASH_IN_RIGHT = 1 << 3,
	DASH_IN_OK = 1 << 4,
	DASH_IN_BACK = 1 << 5,
	DASH_IN_MENU = 1 << 6,   /* Triangle */
	DASH_IN_FAV = 1 << 7,    /* Square */
	DASH_IN_L1 = 1 << 8,
	DASH_IN_R1 = 1 << 9,
	DASH_IN_L2 = 1 << 10,
	DASH_IN_R2 = 1 << 11,
	DASH_IN_START = 1 << 12,
	DASH_IN_SELECT = 1 << 13,
	DASH_IN_L3 = 1 << 14,
	DASH_IN_R3 = 1 << 15
};

typedef struct
{
	u32 pressed; /* includes auto-repeat */
	u32 raw_new;
	int any;
} DashInput;

void dashInputPoll(DashInput *in);
void dashInputPollEx(DashInput *in, int usb_kb_as_pad);
void dashInputFlush(void);
u64 dashInputIdleMs(void);

//--------------------------------------------------------------
// Widgets / overlays (dash_ui.c)
//--------------------------------------------------------------
typedef struct
{
	int button; /* DI_BTN_* icon, or -1 for text-only key labels */
	const char *key; /* "L1", "START"... used when button < 0 */
	const char *label;
} DashHint;

enum DashToastKind {
	DASH_TOAST_INFO = 0,
	DASH_TOAST_OK,
	DASH_TOAST_WARN,
	DASH_TOAST_ERROR
};

void dashUiInit(void);
void dashUiUpdate(float dt);
void dashUiFocusPoke(void); /* call when the selection moves */
float dashUiFocusPop(void); /* 0.9 -> 1.0 scale spring, 1.0 when animations are off */
int dashOkIcon(void);
int dashBackIcon(void);
void dashDrawHints(const DashHint *hints, int n);
void dashDrawHeader(int section, float section_anim);
void dashDrawClock(void);
void dashDrawSectionTitle(float x, float y, const char *title, const char *subtitle);
void dashDrawEmptyState(float cx, float cy, int icon, const char *title, const char *body);
void dashDrawScrollbar(float x, float y, float h, int first, int visible, int total);
void dashDrawFocusFrame(float x, float y, float w, float h, float r, float strength);
/* Themed building blocks: every list, card and selection goes through
 * these so the theme's panel, border and highlight style apply everywhere. */
void dashDrawPanelBox(float x, float y, float w, float h, float r);
void dashDrawCard(float x, float y, float w, float h, float r);
void dashDrawSelection(float x, float y, float w, float h, float r, float strength);
void dashAccentGlow(float cx, float cy, float rx, float ry, float strength);
DashColor dashItemColor(int selected); /* text colour of a list row */
void dashDrawSpinner(float cx, float cy, float r, DashColor c);
void dashToast(int kind, const char *msg);
void dashDrawToasts(void);

/* Blocking modal dialogs. They keep rendering the current dashboard scene
 * underneath through dashRenderScene(). */
typedef struct
{
	int icon;
	const char *label;
	const char *detail;
	int disabled;
} DashMenuItem;

int dashModalMenu(const char *title, const char *subtitle, const DashMenuItem *items, int n, int initial);
/* dialog building blocks for custom modals */
void dashModalBackdrop(float k);
void dashModalPanel(float x, float y, float w, float h, float k);
void dashModalHintRow(float x, float y, int ok_icon, const char *ok, int back_icon, const char *back);
float dashDrawButton(float x, float y, int icon, const char *key);

/* Native text entry (dash_widgets.c): the user's virtual keyboard layout,
 * USB keyboard typing. Edits `buf` in place; 1 = accepted, 0 = cancelled. */
int dashModalKeyboard(const char *title, char *buf, int max_len);
/* IPv4 editor for "a.b.c.d" strings; 1 = changed/accepted, 0 = cancelled. */
int dashModalIp(const char *title, const char *help, char *ip_str, int size);

/* Native file / folder picker (dash_widgets.c). Folders are read in the
 * background. `ext` filters files (e.g. "PNG", NULL = all). Returns 1 with
 * the chosen path in `out` (folders end in '/'), 0 when cancelled. */
enum {
	DASH_PICK_FILE = 0,
	DASH_PICK_FOLDER,
	DASH_PICK_LAUNCH /* ELF files plus the built-in MISC commands */
};
int dashPickPath(const char *title, const char *start_dir, int mode, const char *ext, char *out, int out_size);
void dashModalSetPreview(void (*fn)(int index)); /* applies to the next dashModalMenu() */
int dashModalKeepVideo(void);
int dashModalConfirm(const char *title, const char *body, const char *ok_label, int danger);
void dashModalMessage(int icon, const char *title, const char *body, const char *detail);
int dashModalChoice(const char *title, const char *const *options, int n, int current);

/* Synchronous work feedback: draws a full frame immediately. */
void dashLoadingBegin(const char *title);
/* Compact file-operation layout: filename, remaining MB and progress bar. */
void dashFileLoadingBegin(const char *title);
void dashLoadingStatus(const char *status, int done, int total);
void dashLoadingFrame(const char *status, int done, int total); /* always draws */
void dashLoadingSetCancel(int cancelable); /* shows "Back: keep working in background" */
void dashLoadingEnd(void);
int dashLoadingActive(void);

//--------------------------------------------------------------
// Background worker (dash_worker.c). One job at a time runs on a
// lower-priority EE thread; `done` runs later on the UI thread.
//--------------------------------------------------------------
typedef void (*DashJobFn)(void *arg);

enum DashJobFlags {
	DASH_JOB_VISIBLE = 1 << 0, /* show the activity indicator while it runs */
	DASH_JOB_ART = 1 << 1,
	DASH_JOB_SCAN = 1 << 2,
	DASH_JOB_SAVE = 1 << 3,
	DASH_JOB_DEVICE = 1 << 4,
	DASH_JOB_THEME = 1 << 5
};

void dashWorkerInit(void);
/* Positive job ID on acceptance; -1 leaves arg owned by the caller.
 * Never waits or invokes callbacks inline, even if the worker is unavailable. */
int dashWorkerSubmit(const char *label, int flags, DashJobFn run, DashJobFn done, void *arg);
void dashWorkerPump(void);
int dashWorkerBusy(void);     /* a job is queued, running or waiting for its completion */
int dashWorkerIoBusy(void);   /* the worker is doing IO right now: the UI thread must not */
int dashWorkerQueued(int flags_mask);
int dashWorkerDone(int id);
int dashWorkerLastId(void);
int dashWorkerAwait(int id, const char *title, int cancelable); /* 0 = cancelled */
int dashWorkerAwaitAll(const char *title);                       /* 0 = cancelled */
int dashWorkerOnThread(void);
void dashWorkerStatus(const char *text, int done, int total);   /* worker side */
void dashDrawActivity(void);
/* A background job rebooting the IOP parks the UI thread at its next
 * dashWorkerParkPoint(): no pad reads, no sound, until the IOP is back. */
void dashWorkerParkPoint(void);
int dashIopParked(void);

//--------------------------------------------------------------
// Sections (dash_home.c, dash_apps.c, dash_files.c, dash_tools.c,
// dash_settings.c) and shell (dash_main.c)
//--------------------------------------------------------------
typedef struct
{
	void (*enter)(void);
	void (*input)(const DashInput *in);
	void (*update)(float dt);
	void (*draw)(void);
	void (*hints)(void);
	int (*leave)(void); /* optional: 0 keeps the user on this section */
} DashScreen;

extern const DashScreen dashHomeScreen;
extern const DashScreen dashAppsScreen;
extern const DashScreen dashFilesScreen;
extern const DashScreen dashDevicesScreen;
extern const DashScreen dashToolsScreen;
extern const DashScreen dashSettingsScreen;

/* MMCE/cloud actions and cached Settings status shared by Tools/Settings. */
void dashToolsIgrBackupSetup(void);
void dashToolsSyncAllMmceCards(void);
void dashBrowserRecoverCardPath(const char *source); /* verified new USB/HDD copy only */
void dashToolsPrepareMmceStatus(void);
void dashToolsRefreshMmceStatus(void);
const char *dashToolsMmceStatus(void);
const char *dashToolsIgrStatus(void);
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
void dashToolsCloudBackup(void);
const char *dashToolsGoogleStatus(void);
#endif
void dashToolsCheckUpdates(void);

const char *dashSectionName(int section);
int dashSectionIcon(int section);
void dashSwitchSection(int section);
int dashConfirmLeave(void); /* asks the current section; 0 = stay */
int dashCurrentSection(void);
int dashVisibleSectionCount(void);
int dashVisibleSectionAt(int index);
int dashVisibleSectionIndex(int section);
const char *dashVisibleSectionName(int index);
int dashAdjacentSection(int section, int direction);
void dashRenderScene(void);
/* Full-screen tools push their scene on entry and pop it on exit, which
 * restores the scene of whatever opened them (dashboard or another tool). */
void dashPushSceneOverride(void (*fn)(void));
void dashPopSceneOverride(void);
/* Native Text Editor (dash_editor.c); returns TEXTEDITOR_RESULT_*.
 * dashTextEditorEx(path, 1) starts a new document named `path` when the
 * file does not exist yet (e.g. an application's .arg sidecar). */
int dashTextEditor(const char *path);
int dashTextEditorEx(const char *path, int create_if_missing);
float dashTime(void);
/* Visual-only tick, once before each dashboard/modal frame. No input or IO. */
float dashVisualUpdate(void);
float dashFrameDelta(void); /* measured delta of that tick, for draw-time easing */
float dashUiFocusAge(void); /* seconds since the focused item changed */
float dashAnimSpeed(void);
float dashSectionAnim(void); /* 0..1 section-transition clock */

/* Actions dispatched by the screens. All of them run through the proven
 * ExecuteMainAction() path. */
/* with_args_prompt: 0 = start, 1 = edit arguments first,
 * DASH_LAUNCH_PENDING_ARGS = start with the arguments already loaded */
#define DASH_LAUNCH_PENDING_ARGS 2
void dashLaunchApp(DashApp *app, int with_args_prompt);
void dashRunMisc(const char *misc_path);
/* Native HDD Manager (dash_hdd.c) */
void dashHddManager(void);
/* Native information screens (dash_info.c) */
void dashShowAbout(void);
void dashShowBuildInfo(void);
void dashShowSystemInfo(void);
/* Pure cached identity string; never starts hardware probes during drawing. */
void dashConsoleSummaryText(char *out, size_t out_size);
void dashShowFontViewer(void);
void dashShowTextLines(int icon, const char *title, const char *subtitle, const char *const *lines, int n);
typedef struct
{
	int fullscreen;
	int slideshow;
	int slide_seconds;
} DashImageViewerState;

enum {
	DASH_IMAGE_EXIT = 0,
	DASH_IMAGE_PREV = -1,
	DASH_IMAGE_NEXT = 1
};

int dashShowImageViewer(const char *path, const char *title, DashImageViewerState *state);
void dashExploitInstaller(void); /* dash_exploit.c */
void dashOpenBrowser(const char *start_dir);        /* native File Browser */
void dashNavigateFilesTab(const char *start_dir);  /* navigate dashboard Files tab */
void dashFileBrowser(const char *start_dir);        /* dash_browser.c */
void dashFileBrowserTabClose(void);                /* release inactive-tab state */
int dashBrowserPadInputReady(void);              /* global USB fileXio / pad guard */
void dashFileBrowserTabOpen(const char *start_dir);/* navigate within Files tab */
int dashSettingsSaveCnf(void);                      /* LAUNCHELF.CNF now */
/* classic dialogs (ynDialog/nonDialog/keyboard) as dashboard dialogs */
void dashClassicUiInstall(int on);
void dashClassicUiDone(void);
int dashOpenTextEditor(const char *file); /* 0 = the user backed out before it opened */
void dashAppActions(DashApp *app);
void dashRescanLibrary(int interactive);
int dashClockSetManual(int year, int month, int day, int hour, int minute, int second,
                       char *result, int result_size);
int dashClockSyncInternet(char *result, int result_size);
const MainExecuteContext *dashExecContext(void);
const char *dashOkLabelKey(void);

/* Main loop: finishes a device the user opened while its driver started. */
void dashFilesPoll(void);

/* Home/Apps shared helpers. */
int dashAppsVisibleCount(void);
void dashAppsRebuildView(void);
void dashAppsFocusPath(const char *path);

/* Legacy integration hooks (draw.c). */
extern void (*drawMsgOverride)(const char *msg);
extern int gsScreenGeneration;

/* filer.c accessors used by the device model. */
int getDir(const char *path, FILEINFO *info);
int filerRootMcPresent(int port, int *type, int *free_kb);
int filerUsbDriveMask(void);
int filerUsbRefreshDriveMask(void); /* force one cached USB root refresh */
int filerUsbDriversLoaded(void);
int filerShowHddUnit(int unit);
int filerShowAtaUnit(int unit);
void filerRootMcPollIdle(void);
int filerDirReadNeedsPrep(const char *path);
int filerPrepareDirRead(const char *path); /* UI thread, worker idle */
int pollRootMemoryCardDevices(void);
extern int uLE_cdmode;

/* main_title.c accessor for split clock text. */
void menuTitleGetClockText(char *time_out, int time_size, char *date_out, int date_size);
int menuTitleGetClockFields(int *year, int *month, int *day, int *hour, int *minute, int *second,
                            int *use_12h, int *date_format);
void menuTitleFormatClockTime(char *dst, size_t dst_size, int hour, int minute, int second, int use_12h);
void menuTitleFormatClockDate(char *dst, size_t dst_size, int year, int month, int day, int date_format);

//--------------------------------------------------------------
// Background scenes (dash_bg_ps2.c, dash_bg_geo.c). `t` is the background
// clock (frozen when animation or effects are off), `fx` the effects level.
// Colours come from dashTheme.bg1 / bg2.
//--------------------------------------------------------------
void dashBgSceneTunnel(float t, int fx);
void dashBgSceneMist(float t, int fx);
void dashBgSceneTowers(float t, int fx);
void dashBgSceneDisc(float t, int fx);
void dashBgSceneOrbit(float t, int fx);
void dashBgSceneClockRing(float t, int fx);
void dashBgSceneGradient(float t, int fx);
void dashBgSceneGrid(float t, int fx);
void dashBgSceneLines(float t, int fx);
void dashBgSceneCubes(float t, int fx);
void dashBgSceneStream(float t, int fx);
void dashBgSceneTiles(float t, int fx);
void dashBgSceneBrushed(float t, int fx);
void dashBgSceneWave(float t, int fx);
void dashBgSceneStreaks(float t, int fx);
void dashBgSceneClouds(float t, int fx);
/* shared helpers */
float dashBgHash(int a, int b); /* stable 0..1 */
void dashBgVignette(float strength);
void dashIconMany(int id, const float *xy, int n, float size, DashColor c);

#endif
