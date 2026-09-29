//--------------------------------------------------------------
//File name:   dash_bg.c
//Dashboard background renderer.
//
//dashBgDraw() draws, back to front:
//  1. the theme's background scene (dashTheme.bg): a solid colour, a
//     gradient, a wallpaper PNG or one of the procedural scenes in
//     dash_bg_ps2.c / dash_bg_geo.c, or the original "Crystal" atmosphere
//     kept below
//  2. the background brightness overlay
//  3. the app-aware wallpaper: artwork of the focused application,
//     cover-fitted, cross-faded on focus changes and dimmed
//  4. the optional CRT scanline overlay
//  5. the launch flash
//
//Every scene is built from cheap GS primitives. The background clock stops
//when animations or background effects are off, so each scene doubles as
//its own static variant; SUBTLE effects run at a slower pace with fewer
//elements.
//--------------------------------------------------------------
#include "dash_internal.h"

#define BG_STARS 72
#define BG_SPARKS 18
#define BG_TOWERS 7

typedef struct
{
	float x, y, speed, size, phase, freq;
	u8 layer;
} Star;

typedef struct
{
	float x, y, vy, life, max_life, size;
} Spark;

typedef struct
{
	float x, base_y, height, radius, rot, rot_speed, phase;
	int hue;
} Tower;

static Star stars[BG_STARS];
static Spark sparks[BG_SPARKS];
static Tower towers[BG_TOWERS];
static float bg_time;
static float flash;
static float streak_t = -1.0f, streak_y, streak_next = 6.0f;
static u32 rng = 0x2545F491u;
static int bg_ready;

/* app-aware wallpaper cross-fade */
static DashApp *wall_cur, *wall_prev;
static float wall_mix = 1.0f, wall_vis;

static float frand(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return (rng & 0xFFFFFF) / (float)0x1000000;
}

float dashBgHash(int a, int b)
{
	u32 h = (u32)a * 374761393u + (u32)b * 668265263u;

	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (h & 0xFFFF) / 65535.0f;
}

void dashBgVignette(float strength)
{
	DashColor e = dashColorScale(dashTheme.bg2, 0.5f);
	DashColor z = e;

	e.a = (u8)dashClampf(170.0f * strength, 0.0f, 255.0f);
	z.a = 0;
	dashRectGradV(0, DASH_VH - 110, DASH_VW, 110, z, e);
	e.a = (u8)dashClampf(120.0f * strength, 0.0f, 255.0f);
	dashRectGradV(0, 0, DASH_VW, 70, e, z);
}

static void spawnSpark(Spark *s, int initial)
{
	s->x = 330.0f + frand() * 300.0f;
	s->y = initial ? 200.0f + frand() * 260.0f : 440.0f + frand() * 30.0f;
	s->vy = 8.0f + frand() * 16.0f;
	s->max_life = 6.0f + frand() * 7.0f;
	s->life = initial ? frand() * s->max_life : 0.0f;
	s->size = 3.0f + frand() * 4.0f;
}

void dashBgInit(void)
{
	int i;
	static const float tx[BG_TOWERS] = {372, 418, 450, 488, 522, 560, 604};
	static const float th[BG_TOWERS] = {150, 230, 175, 290, 205, 250, 160};
	static const float tr[BG_TOWERS] = {11, 15, 12, 18, 13, 16, 11};

	if (bg_ready)
		return;
	for (i = 0; i < BG_STARS; i++) {
		stars[i].layer = (u8)(i % 3);
		stars[i].x = frand() * DASH_VW;
		stars[i].y = frand() * DASH_VH;
		stars[i].speed = 1.5f + stars[i].layer * 3.0f + frand() * 1.5f;
		stars[i].size = 1.1f + stars[i].layer * 0.7f + frand() * 0.6f;
		stars[i].phase = frand() * 6.283f;
		stars[i].freq = 0.6f + frand() * 1.8f;
	}
	for (i = 0; i < BG_SPARKS; i++)
		spawnSpark(&sparks[i], 1);
	for (i = 0; i < BG_TOWERS; i++) {
		towers[i].x = tx[i];
		towers[i].base_y = 470.0f;
		towers[i].height = th[i];
		towers[i].radius = tr[i];
		towers[i].rot = frand() * 6.283f;
		towers[i].rot_speed = 0.10f + frand() * 0.18f;
		towers[i].phase = frand() * 6.283f;
		towers[i].hue = 195 + (int)(frand() * 60.0f);
	}
	bg_ready = 1;
}

void dashBgFlash(float amount)
{
	if (amount > flash)
		flash = amount;
}

static int bgAnimated(void)
{
	return dashCfg.effects > 0;
}

void dashBgUpdate(float dt)
{
	int i;
	int animated = bgAnimated();
	float motion = animated ? dashCfg.effects / 100.0f : 0.0f;
	float scene_dt = dt * motion;

	bg_time += scene_dt;
	flash = dashApproach(flash, 0.0f, 3.0f, dt);

	/* app-aware wallpaper focus tracking (the sections publish their
	 * focused app while drawing) */
	{
		DashApp *f = (dashTheme.wallpaper == DASH_WALL_FOCUS) ? (DashApp *)dashThemeFocusApp() : NULL;
		if (f != wall_cur) {
			wall_prev = wall_cur;
			wall_cur = f;
			wall_mix = 0.0f;
		}
		wall_mix = dashApproach(wall_mix, 1.0f, 5.0f * dashAnimSpeed(), dt);
		dashThemeSetFocusApp(NULL);
	}

	if (dashTheme.bg != DASH_BG_CRYSTAL || !animated)
		return;

	for (i = 0; i < BG_STARS; i++) {
		stars[i].x -= stars[i].speed * scene_dt;
		if (stars[i].x < -4.0f) {
			stars[i].x = DASH_VW + 4.0f;
			stars[i].y = frand() * DASH_VH;
		}
	}
	for (i = 0; i < BG_SPARKS; i++) {
		Spark *sp = &sparks[i];
		sp->life += scene_dt;
		sp->y -= sp->vy * scene_dt;
		sp->x += dashSinf(bg_time * 0.7f + i) * 3.0f * scene_dt;
		if (sp->life >= sp->max_life || sp->y < 40.0f)
			spawnSpark(sp, 0);
	}
	for (i = 0; i < BG_TOWERS; i++)
		towers[i].rot += towers[i].rot_speed * scene_dt;

	if (dashCfg.effects >= 100) {
		if (streak_t < 0.0f) {
			streak_next -= scene_dt;
			if (streak_next <= 0.0f) {
				streak_t = 0.0f;
				streak_y = 90.0f + frand() * 260.0f;
				streak_next = 7.0f + frand() * 7.0f;
			}
		} else {
			streak_t += scene_dt;
			if (streak_t > 2.2f)
				streak_t = -1.0f;
		}
	}
}

//--------------------------------------------------------------
// Crystal: the original dashboard atmosphere (drifting orbs, stars,
// translucent crystal towers, sparkles, light streak). Colours follow
// bg1 (upper gradient) and bg2 (floor).
//--------------------------------------------------------------
static void crystalBase(void)
{
	float d = dashSinf(bg_time * 0.045f) * 0.5f + 0.5f;
	DashColor b1 = dashTheme.bg1, b2 = dashTheme.bg2;
	DashColor top = DC(b1.r / 2 + (int)(4 * d), b1.g / 2, b1.b / 2 + (int)(8 * d), 255);
	DashColor mid = DC(b1.r + (int)(10 * d), b1.g + (int)(6 * (1 - d)), b1.b + (int)(8 * d), 255);
	DashColor low = dashColorLerp(b1, b2, 0.55f);
	DashColor bot = b2;

	dashRectGradV(0, 0, DASH_VW, 200, top, mid);
	dashRectGradV(0, 200, DASH_VW, 130, mid, low);
	dashRectGradV(0, 330, DASH_VW, DASH_VH - 330, low, bot);
}

static void drawThemeLayers(void)
{
	GSTEXTURE *bg = dashThemeTexture("background.png", 256, 256);
	GSTEXTURE *s1, *s2;

	if (bg != NULL)
		dashTexDraw(bg, 0, 0, DASH_VW, DASH_VH, DC(255, 255, 255, 255));
	if (dashCfg.effects == DASH_FX_OFF)
		return;
	s1 = dashThemeTexture("stars1.png", 256, 128);
	s2 = dashThemeTexture("stars2.png", 256, 128);
	if (s1 == NULL && s2 == NULL)
		return;
	dashGfxBlend(DASH_BLEND_ADD);
	gsKit_set_clamp(gsGlobal, GS_CMODE_REPEAT);
	if (s1 != NULL) {
		float u = bg_time * 3.0f;
		dashTexDrawUV(s1, 0, 0, DASH_VW, DASH_VH, u, 0, u + s1->Width * 2.5f, s1->Height * 3.5f, DC(255, 255, 255, 200));
	}
	if (s2 != NULL) {
		float u = bg_time * 7.0f;
		dashTexDrawUV(s2, 0, 0, DASH_VW, DASH_VH, u, 0, u + s2->Width * 2.0f, s2->Height * 2.8f, DC(255, 255, 255, 170));
	}
	gsKit_set_clamp(gsGlobal, GS_CMODE_CLAMP);
	dashGfxBlend(DASH_BLEND_NORMAL);
}

static void drawOrbs(float strength)
{
	static const int hues[4] = {232, 188, 268, 32};
	static const float radius[4] = {250, 210, 190, 150};
	int i;

	for (i = 0; i < 4; i++) {
		float t = bg_time * (0.035f + i * 0.011f);
		float cx = 320.0f + dashSinf(t * 1.3f + i * 1.7f) * (220.0f - i * 20.0f);
		float cy = 210.0f + dashCosf(t + i * 2.3f) * (110.0f + i * 10.0f);
		int a = (int)((i == 3 ? 16 : 34) * strength);
		dashGlow(cx, cy, radius[i], radius[i] * 0.8f, dashColorHue(hues[i], 170, 200, a));
	}
}

static void drawStars(int count)
{
	int i;

	for (i = 0; i < count; i++) {
		Star *s = &stars[i];
		float tw = 0.55f + 0.45f * dashSinf(bg_time * s->freq + s->phase);
		int a = (int)((70 + s->layer * 60) * tw);
		dashGlow(s->x, s->y, s->size * 2.2f, s->size * 2.2f, DC(200, 220, 255, a));
	}
}

static void drawTower(const Tower *t, float strength)
{
	const int sides = 6;
	float bob = dashSinf(bg_time * 0.35f + t->phase) * 6.0f;
	float top_y = t->base_y - t->height + bob;
	float tip_y = top_y - t->radius * 2.4f;
	float fade_y = t->base_y - 30.0f;
	int k;

	for (k = 0; k < sides; k++) {
		float a0 = t->rot + k * (6.2831853f / sides);
		float a1 = a0 + 6.2831853f / sides;
		float x0 = t->x + dashCosf(a0) * t->radius;
		float x1 = t->x + dashCosf(a1) * t->radius;
		float z0 = dashSinf(a0), z1 = dashSinf(a1);
		float facing = (z0 + z1) * 0.5f; /* >0: front half */
		float nx = dashCosf(a0 + 3.14159f / sides);
		float light, alpha;
		DashColor ctop, cbot, ctip;

		if (facing < -0.05f || x1 <= x0)
			continue;
		light = 0.35f + 0.65f * dashClampf(0.6f * nx + 0.8f * facing, 0.0f, 1.0f);
		alpha = strength * light;
		ctop = dashColorHue(t->hue + (int)(20 * nx), 120, 255, (int)(90 * alpha));
		cbot = dashColorHue(t->hue + 20, 160, 160, 0);
		ctip = dashColorHue(t->hue - 10, 90, 255, (int)(120 * alpha));
		/* side face, fogged towards the floor */
		dashTri(x0, top_y, x1, top_y, x0, fade_y, ctop, ctop, cbot);
		dashTri(x1, top_y, x1, fade_y, x0, fade_y, ctop, cbot, cbot);
		/* pointed cap */
		dashTri(x0, top_y, x1, top_y, t->x, tip_y, ctop, ctop, ctip);
	}
	/* bright edge highlight and a soft halo at the tip */
	dashGlow(t->x, tip_y + t->radius, t->radius * 2.6f, t->radius * 2.6f, DC(170, 220, 255, (int)(60 * strength)));
}

static void sceneCrystal(int fx)
{
	int i;

	crystalBase();
	drawThemeLayers();
	if (fx == DASH_FX_OFF) {
		dashRectGradV(0, DASH_VH - 90, DASH_VW, 90, DC(0, 0, 0, 0), DC(0, 0, 6, 150));
		return;
	}

	dashGfxBlend(DASH_BLEND_ADD);
	drawOrbs(fx == DASH_FX_FULL ? 1.0f : 0.7f);
	drawStars(fx == DASH_FX_FULL ? BG_STARS : BG_STARS / 2);

	if (fx == DASH_FX_FULL) {
		float strength = 0.55f;

		for (i = 0; i < BG_TOWERS; i++)
			drawTower(&towers[i], strength);
		if (streak_t >= 0.0f) {
			float p = streak_t / 2.6f;
			float sx = -200.0f + p * (DASH_VW + 400.0f);
			float a = dashSinf(p * 3.14159f);
			dashGlow(sx, streak_y, 240.0f, 3.0f, DC(160, 210, 255, (int)(110 * a)));
			dashGlow(sx, streak_y, 90.0f, 10.0f, DC(160, 210, 255, (int)(50 * a)));
		}
	}
	for (i = 0; i < (fx == DASH_FX_FULL ? BG_SPARKS : BG_SPARKS / 2); i++) {
		Spark *s = &sparks[i];
		float lp = s->life / s->max_life;
		float a = dashSinf(lp * 3.14159f);
		dashSparkle(s->x, s->y, s->size, DC(190, 225, 255, (int)(120 * a)));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);

	/* floor fog and vignette */
	dashRectGradV(0, DASH_VH - 110, DASH_VW, 110, DC(2, 3, 8, 0), DC(2, 3, 8, 170));
	dashRectGradV(0, 0, DASH_VW, 70, DC(0, 0, 4, 120), DC(0, 0, 4, 0));
}

//--------------------------------------------------------------
// Image / artwork layers
//--------------------------------------------------------------
/* Draw a texture so it covers the whole screen, preserving its aspect
 * ratio (the 640x448 canvas is shown as 4:3) and cropping the excess. */
static void drawCover(GSTEXTURE *tex, DashColor tint)
{
	float tw = (float)tex->Width, th = (float)tex->Height;
	float screen_aspect = 4.0f / 3.0f, tex_aspect;
	float u0 = 0, v0 = 0, u1 = tw, v1 = th;

	if (tw < 1 || th < 1)
		return;
	tex_aspect = tw / th;
	if (tex_aspect > screen_aspect) {
		float cw = th * screen_aspect;
		u0 = (tw - cw) * 0.5f;
		u1 = u0 + cw;
	} else {
		float ch = tw / screen_aspect;
		v0 = (th - ch) * 0.5f;
		v1 = v0 + ch;
	}
	dashTexDrawUV(tex, 0, 0, DASH_VW, DASH_VH, u0, v0, u1, v1, tint);
}

static void sceneImage(float t, int fx)
{
	GSTEXTURE *tex = dashThemeTextureFile(dashTheme.image, 320, 256);

	if (tex == NULL) {
		dashBgSceneGradient(t, fx);
		return;
	}
	dashRect(0, 0, DASH_VW, DASH_VH, dashTheme.bg2);
	drawCover(tex, DC(255, 255, 255, 255));
}

static void drawAppWallpaper(void)
{
	GSTEXTURE *cur = NULL, *prev = NULL;
	float vis_target;

	if (dashTheme.wallpaper != DASH_WALL_FOCUS)
		return;
	/* the artwork cache keeps textures drawn in the current or previous
	 * frame, so requesting them here keeps them resident */
	if (wall_cur != NULL)
		cur = dashArtGet(wall_cur, 0);
	if (wall_prev != NULL && wall_mix < 1.0f)
		prev = dashArtGet(wall_prev, 1);
	vis_target = (cur != NULL) ? 1.0f : 0.0f;
	wall_vis = dashApproach(wall_vis, vis_target, 4.0f * dashAnimSpeed(), 1.0f / 60.0f);
	if (wall_vis < 0.01f && prev == NULL)
		return;
	/* the artwork is laid over the scene at 60 % (a softer, more muted
	 * picture than the cover itself), then dimmed towards a near-black
	 * version of the background colour so every text colour stays
	 * readable over bright covers */
	if (prev != NULL)
		drawCover(prev, DC(255, 255, 255, (int)(153 * (1.0f - wall_mix) * wall_vis)));
	if (cur != NULL)
		drawCover(cur, DC(255, 255, 255, (int)(153 * wall_mix * wall_vis)));
	{
		DashColor d = dashColorScale(dashTheme.bg2, 0.3f);
		d.a = (u8)(dashTheme.wall_dim * 2.55f * wall_vis);
		dashRect(0, 0, DASH_VW, DASH_VH, d);
	}
}

static void drawScanlines(void)
{
	float y;
	float step = 2.0f / dashGfxScaleY(); /* every second framebuffer line */

	if (step < 1.5f)
		step = 1.5f;
	for (y = 0; y < DASH_VH; y += step * 1.0f)
		dashRect(0, y, DASH_VW, step * 0.5f, DC(0, 0, 0, 60));
}

//--------------------------------------------------------------
// Entry points
//--------------------------------------------------------------
static void drawScene(int fx)
{
	float t = bg_time;
	DashColor saved_bg1 = dashTheme.bg1, saved_bg2 = dashTheme.bg2;

	/* Brightness above 100% boosts only the background colours. Interface
	 * colours are drawn after this function and are deliberately untouched. */
	if (dashTheme.bg_bright > 100) {
		float k = dashTheme.bg_bright / 100.0f;
		dashTheme.bg1 = dashColorScale(dashTheme.bg1, k);
		dashTheme.bg2 = dashColorScale(dashTheme.bg2, k);
	}

	switch (dashTheme.bg) {
		case DASH_BG_SOLID:
			dashRect(0, 0, DASH_VW, DASH_VH, dashTheme.bg1);
			if (fx != DASH_FX_OFF) {
				float x = -180.0f + fmodf(t * 54.0f, DASH_VW + 360.0f);
				dashGfxBlend(DASH_BLEND_ADD);
				dashGlow(x, DASH_VH * 0.46f, 190, 300, colA(dashColorScale(dashTheme.accent, 0.35f), 18));
				dashGfxBlend(DASH_BLEND_NORMAL);
			}
			break;
		case DASH_BG_GRADIENT:
			dashBgSceneGradient(t, fx);
			break;
		case DASH_BG_TUNNEL:
			dashBgSceneTunnel(t, fx);
			break;
		case DASH_BG_MIST:
			dashBgSceneMist(t, fx);
			break;
		case DASH_BG_TOWERS:
			dashBgSceneTowers(t, fx);
			break;
		case DASH_BG_DISC:
			dashBgSceneDisc(t, fx);
			break;
		case DASH_BG_ORBIT:
			dashBgSceneOrbit(t, fx);
			break;
		case DASH_BG_CLOCKRING:
			dashBgSceneClockRing(t, fx);
			break;
		case DASH_BG_GRID:
			dashBgSceneGrid(t, fx);
			break;
		case DASH_BG_LINES:
			dashBgSceneLines(t, fx);
			break;
		case DASH_BG_CUBES:
			dashBgSceneCubes(t, fx);
			break;
		case DASH_BG_STREAM:
			dashBgSceneStream(t, fx);
			break;
		case DASH_BG_TILES:
			dashBgSceneTiles(t, fx);
			break;
		case DASH_BG_BRUSHED:
			dashBgSceneBrushed(t, fx);
			break;
		case DASH_BG_WAVE:
			dashBgSceneWave(t, fx);
			break;
		case DASH_BG_CRYSTAL:
			sceneCrystal(fx);
			break;
		case DASH_BG_STREAKS:
			dashBgSceneStreaks(t, fx);
			break;
		case DASH_BG_CLOUDS:
			dashBgSceneClouds(t, fx);
			break;
		case DASH_BG_IMAGE:
			sceneImage(t, fx);
			break;
		default:
			dashRect(0, 0, DASH_VW, DASH_VH, DC(0, 0, 0, 255));
			break;
	}
	dashTheme.bg1 = saved_bg1;
	dashTheme.bg2 = saved_bg2;
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashGfxScissorReset();

	if (dashTheme.bg_bright < 100)
		dashRect(0, 0, DASH_VW, DASH_VH, DC(0, 0, 0, (int)((100 - dashTheme.bg_bright) * 2.55f)));
}

void dashBgDrawStatic(void)
{
	/* used before the main loop runs (first library scan) */
	drawScene(DASH_FX_OFF);
}

void dashBgDraw(void)
{
	int fx = dashCfg.effects <= 0 ? DASH_FX_OFF : (dashCfg.effects < 100 ? DASH_FX_SUBTLE : DASH_FX_FULL);

	drawScene(fx);
	drawAppWallpaper();
	if (dashTheme.scanlines)
		drawScanlines();
	if (flash > 0.01f) {
		DashColor f = dashColorLighten(dashTheme.accent, 0.6f);
		f.a = (u8)(90 * flash);
		dashRect(0, 0, DASH_VW, DASH_VH, f);
	}
}
//--------------------------------------------------------------
//End of file: dash_bg.c
//--------------------------------------------------------------
