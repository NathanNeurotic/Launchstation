//--------------------------------------------------------------
//File name:   dash_bg.c
//Animated dashboard atmosphere.
//
//Layers (back to front), all cheap GS primitives:
//  * deep indigo gradient with a very slow colour drift
//  * optional user backdrop (THEME/background.png) and star layers
//    (THEME/stars1.png, THEME/stars2.png) scrolled with texture wrap
//  * procedural parallax star field with twinkle
//  * large soft orbs drifting on Lissajous paths (additive)
//  * translucent rotating crystal towers, a nod to the OSDSYS towers
//    (additive gouraud prisms, fogged towards the floor)
//  * rising sparkles and an occasional horizontal light streak
//  * floor fog and top/bottom vignette to keep text readable
//Effects level FULL draws everything, SUBTLE drops the towers/streaks and
//halves the particles, OFF draws only the static gradient.
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

static float frand(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return (rng & 0xFFFFFF) / (float)0x1000000;
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

void dashBgUpdate(float dt)
{
	int i;
	float speed = (dashCfg.anim == DASH_ANIM_OFF) ? 0.0f : 1.0f;

	bg_time += dt * speed;
	flash = dashApproach(flash, 0.0f, 3.0f, dt);
	if (dashCfg.effects == DASH_FX_OFF || speed == 0.0f)
		return;

	for (i = 0; i < BG_STARS; i++) {
		stars[i].x -= stars[i].speed * dt;
		if (stars[i].x < -4.0f) {
			stars[i].x = DASH_VW + 4.0f;
			stars[i].y = frand() * DASH_VH;
		}
	}
	for (i = 0; i < BG_SPARKS; i++) {
		Spark *s = &sparks[i];
		s->life += dt;
		s->y -= s->vy * dt;
		s->x += dashSinf(bg_time * 0.7f + i) * 3.0f * dt;
		if (s->life >= s->max_life || s->y < 40.0f)
			spawnSpark(s, 0);
	}
	for (i = 0; i < BG_TOWERS; i++)
		towers[i].rot += towers[i].rot_speed * dt;

	if (dashCfg.effects == DASH_FX_FULL) {
		if (streak_t < 0.0f) {
			streak_next -= dt;
			if (streak_next <= 0.0f) {
				streak_t = 0.0f;
				streak_y = 90.0f + frand() * 260.0f;
				streak_next = 9.0f + frand() * 9.0f;
			}
		} else {
			streak_t += dt;
			if (streak_t > 2.6f)
				streak_t = -1.0f;
		}
	}
}

static void drawBase(void)
{
	float d = dashSinf(bg_time * 0.045f) * 0.5f + 0.5f;
	DashColor top = DC(9 + (int)(4 * d), 12, 30 + (int)(8 * d), 255);
	DashColor mid = DC(18 + (int)(10 * d), 22 + (int)(6 * (1 - d)), 50 + (int)(8 * d), 255);
	DashColor low = DC(8, 10, 24, 255);
	DashColor bot = DC(3, 4, 9, 255);

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

void dashBgDrawStatic(void)
{
	drawBase();
	dashThemeTexture("background.png", 256, 256) ? drawThemeLayers() : (void)0;
	dashRectGradV(0, DASH_VH - 90, DASH_VW, 90, DC(0, 0, 0, 0), DC(0, 0, 6, 150));
}

void dashBgDraw(void)
{
	int i;
	int fx = dashCfg.effects;

	drawBase();
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
		/* horizontal light streak */
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

	if (flash > 0.01f)
		dashRect(0, 0, DASH_VW, DASH_VH, DC(200, 225, 255, (int)(90 * flash)));
}
//--------------------------------------------------------------
//End of file: dash_bg.c
//--------------------------------------------------------------
