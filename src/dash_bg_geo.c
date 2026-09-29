//--------------------------------------------------------------
//File name:   dash_bg_geo.c
//Animated geometric background scenes made from flat or gouraud
//primitives (or one small procedural texture). Colours: dashTheme.bg1 is
//the drawing colour, dashTheme.bg2 the base.
//--------------------------------------------------------------
#include "dash_internal.h"

#define TAU_F 6.28318531f

static DashColor colA(DashColor c, int a)
{
	c.a = (u8)((a < 0) ? 0 : (a > 255) ? 255 : a);
	return c;
}

static float fracf(float v)
{
	return v - floorf(v);
}

//--------------------------------------------------------------
// Gradient (BIOS Blue, CRT)
//--------------------------------------------------------------
void dashBgSceneGradient(float t, int fx)
{
	DashColor top = dashTheme.bg1, bot = dashTheme.bg2;
	DashColor mid = dashColorLerp(top, bot, 0.55f);

	float split = 210.0f + 65.0f * dashSinf(t * 0.18f);

	(void)fx;
	dashRectGradV(0, 0, DASH_VW, split, top, mid);
	dashRectGradV(0, split, DASH_VW, DASH_VH - split, mid, bot);
	dashGlow(320 + 210 * dashSinf(t * 0.14f), 200 + 100 * dashCosf(t * 0.19f), 260, 150, colA(top, 90));
}

//--------------------------------------------------------------
// Memory card grid
//--------------------------------------------------------------
void dashBgSceneGrid(float t, int fx)
{
	DashColor ln = dashTheme.bg1;
	const float cell = 32.0f;
	int i, j;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.03f), dashTheme.bg2);
	/* a few occupied blocks, like a memory card's block map */
	for (j = 0; j < 14; j++)
		for (i = 0; i < 20; i++) {
			float h = dashBgHash(i, j);
			if (h < 0.035f)
				dashRect(i * cell + 1, j * cell + 1, cell - 1, cell - 1, colA(ln, 10 + (int)(h * 300)));
		}
	/* one block being "written": it moves every few seconds */
	if (fx != DASH_FX_OFF) {
		int step = (int)(t / 3.5f);
		float ph = fracf(t / 3.5f);
		int bi = (int)(dashBgHash(step, 1) * 20), bj = 3 + (int)(dashBgHash(step, 2) * 9);
		float a = dashSinf(ph * 3.14159f);
		dashRect(bi * cell + 1, bj * cell + 1, cell - 1, cell - 1, colA(ln, (int)(80 * a)));
	}
	for (i = 1; i < 20; i++)
		dashRect(i * cell, 0, 1, DASH_VH, colA(ln, (i % 4) ? 9 : 20));
	for (j = 1; j < 14; j++)
		dashRect(0, j * cell, DASH_VW, 1, colA(ln, (j % 4) ? 9 : 20));
	/* registration marks on the major intersections */
	for (i = 4; i < 20; i += 4)
		for (j = 4; j < 14; j += 4) {
			dashRect(i * cell - 4, j * cell, 9, 1, colA(ln, 60));
			dashRect(i * cell, j * cell - 4, 1, 9, colA(ln, 60));
		}
	dashBgVignette(0.8f);
}

//--------------------------------------------------------------
// Boot lines
//--------------------------------------------------------------
static void trace(float x, float y, float w, float h, int vertical, DashColor c, int a)
{
	DashColor z = colA(c, 0), m = colA(c, a);

	if (vertical) {
		dashRectGradV(x, y, w, h * 0.7f, z, m);
		dashRectGradV(x, y + h * 0.7f, w, h * 0.3f, m, z);
	} else {
		dashRectGradH(x, y, w * 0.7f, h, z, m);
		dashRectGradH(x + w * 0.7f, y, w * 0.3f, h, m, z);
	}
}

void dashBgSceneLines(float t, int fx)
{
	DashColor c = dashTheme.bg1;
	static const float vx[5] = {92, 214, 351, 468, 571};
	static const float hy[3] = {118, 262, 371};
	int i;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.025f), dashTheme.bg2);
	for (i = 0; i < 5; i++) {
		float len = 180.0f + dashBgHash(i, 4) * 170.0f;
		float sp = 9.0f + dashBgHash(i, 9) * 12.0f;
		float y = fracf((t * sp + dashBgHash(i, 2) * 900.0f) / (DASH_VH + len)) * (DASH_VH + len) - len;
		trace(vx[i], y, 2.0f, len, 1, c, 100);
		if (fx == DASH_FX_FULL) {
			dashGfxBlend(DASH_BLEND_ADD);
			dashGlow(vx[i] + 1, y + len * 0.7f, 10, 18, colA(c, 46));
			dashGfxBlend(DASH_BLEND_NORMAL);
		}
	}
	for (i = 0; i < 3; i++) {
		float len = 220.0f + dashBgHash(i, 14) * 200.0f;
		float sp = 7.0f + dashBgHash(i, 19) * 8.0f;
		float x = fracf((t * sp + dashBgHash(i, 12) * 900.0f) / (DASH_VW + len)) * (DASH_VW + len) - len;
		trace(x, hy[i], len, 2.0f, 0, c, 76);
	}
	dashBgVignette(0.7f);
}

//--------------------------------------------------------------
// Wireframe cubes
//--------------------------------------------------------------
#define CUBES 8

void dashBgSceneCubes(float t, int fx)
{
	static const signed char v[8][3] = {
	    {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};
	static const u8 e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	DashColor c = dashTheme.bg1;
	int n, i;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashTheme.bg2, dashColorScale(dashTheme.bg2, 0.6f));
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlow(320, 224, 320, 220, colA(dashColorScale(c, 0.25f), 60));
	for (n = 0; n < CUBES; n++) {
		/* Separate home regions prevent cubes travelling through one another.
		 * Staggered centres and independent three-axis turns avoid a tiled look.
		 * Max projected radius is < 62; drift is bounded to 10 pixels in X. */
		static const float home_x[CUBES] = {76, 232, 398, 560, 94, 250, 416, 570};
		static const float home_y[CUBES] = {104, 132, 94, 118, 328, 304, 340, 316};
		float cx = home_x[n] + 10.0f * dashSinf(t * (0.17f + n * 0.013f) + n * 1.7f);
		float cy = home_y[n] + 22.0f * dashCosf(t * (0.13f + n * 0.009f) + n * 2.3f);
		float size = 18.0f + dashBgHash(n, 5) * 8.0f + 4.0f * dashSinf(t * 0.17f + n);
		float ax = t * (0.13f + dashBgHash(n, 6) * 0.12f) + n;
		float ay = t * (0.11f + dashBgHash(n, 7) * 0.10f) * ((n & 1) ? -1 : 1);
		float az = t * (0.07f + dashBgHash(n, 8) * 0.09f) + n * 0.7f;
		float px[8], py[8];
		int alpha = 95 + (int)(25 * dashSinf(t * 0.3f + n));

		if (fx == DASH_FX_SUBTLE && (n & 1))
			continue;
		for (i = 0; i < 8; i++) {
			float x = v[i][0], y = v[i][1], zz = v[i][2], q;
			float cx_ = dashCosf(ax), sx_ = dashSinf(ax), cy_ = dashCosf(ay), sy_ = dashSinf(ay);
			float y2 = y * cx_ - zz * sx_, z2 = y * sx_ + zz * cx_;
			float x3 = x * cy_ + z2 * sy_, z3 = -x * sy_ + z2 * cy_;
			q = 4.0f / (4.0f + z3 * 0.35f);
			px[i] = cx + (x3 * dashCosf(az) - y2 * dashSinf(az)) * size * q;
			py[i] = cy + (x3 * dashSinf(az) + y2 * dashCosf(az)) * size * q;
		}
		for (i = 0; i < 12; i++)
			dashLine(px[e[i][0]], py[e[i][0]], px[e[i][1]], py[e[i][1]], 1.3f, colA(c, alpha));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashBgVignette(0.7f);
}

//--------------------------------------------------------------
// Data stream
//--------------------------------------------------------------
void dashBgSceneStream(float t, int fx)
{
	DashColor c = dashTheme.bg1;
	int i, k, cols = (fx == DASH_FX_SUBTLE) ? 14 : 26;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashTheme.bg2, dashColorScale(dashTheme.bg2, 0.7f));
	for (i = 0; i < cols; i++) {
		float x = 14.0f + i * (612.0f / cols) + dashBgHash(i, 1) * 8.0f;
		float sp = 12.0f + dashBgHash(i, 2) * 34.0f;
		/* the column's faint rail */
		dashRect(x, 0, 1, DASH_VH, colA(c, 7));
		for (k = 0; k < 3; k++) {
			float len = 6.0f + dashBgHash(i, 10 + k) * 34.0f;
			float y = fracf((t * sp + dashBgHash(i, 20 + k) * 700.0f) / (DASH_VH + 60.0f)) * (DASH_VH + 60.0f) - 40.0f;
			int a = 16 + (int)(dashBgHash(i, 30 + k) * 34);
			dashRect(x - 0.5f, y, 2, len, colA(c, a));
		}
	}
	dashBgVignette(0.8f);
}

//--------------------------------------------------------------
// Memory card blue tiles
//--------------------------------------------------------------
void dashBgSceneTiles(float t, int fx)
{
	DashColor c = dashTheme.bg1;
	float xy[2 * 13 * 10];
	float ox = fracf(t * 3.0f / 64.0f) * 64.0f, oy = fracf(t * 1.5f / 64.0f) * 64.0f;
	int i, j, n = 0;

	(void)fx;
	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.08f), dashColorScale(dashTheme.bg2, 0.7f));
	for (j = 0; j < 10; j++)
		for (i = 0; i < 13; i++) {
			xy[n * 2] = i * 64.0f - 64.0f + ox + ((j & 1) ? 32.0f : 0.0f);
			xy[n * 2 + 1] = j * 64.0f - 64.0f + oy;
			n++;
		}
	dashIconMany(DI_MC, xy, n, 22, colA(c, 30));
	dashBgVignette(0.6f);
}

//--------------------------------------------------------------
// Brushed console
//--------------------------------------------------------------
#define BRUSH_W 128
#define BRUSH_H 64
static GSTEXTURE brush_tex;
static int brush_ready;

static void buildBrush(void)
{
	u32 *mem;
	int x, y;

	brush_ready = -1;
	mem = (u32 *)memalign(128, BRUSH_W * BRUSH_H * 4);
	if (mem == NULL)
		return;
	for (y = 0; y < BRUSH_H; y++) {
		float row = dashBgHash(y, 77);
		for (x = 0; x < BRUSH_W; x++) {
			/* long horizontal fibres: row value modulated slowly along x */
			float f = row * 0.7f + 0.3f * dashBgHash(y, x / 16) + 0.15f * dashBgHash(x, y) - 0.1f;
			int a = (int)(dashClampf(f, 0.0f, 1.0f) * 128.0f);
			mem[y * BRUSH_W + x] = 0x00808080 | ((u32)a << 24);
		}
	}
	memset(&brush_tex, 0, sizeof(brush_tex));
	brush_tex.Width = BRUSH_W;
	brush_tex.Height = BRUSH_H;
	brush_tex.PSM = GS_PSM_CT32;
	brush_tex.Mem = mem;
	brush_tex.Filter = GS_FILTER_LINEAR;
	brush_tex.Delayed = 1;
	gsKit_setup_tbw(&brush_tex);
	SyncDCache(mem, (u8 *)mem + BRUSH_W * BRUSH_H * 4);
	brush_ready = 1;
}

void dashBgSceneBrushed(float t, int fx)
{
	DashColor c = dashTheme.bg1;
	float sheen = fracf(t * 0.035f);

	(void)fx;
	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.04f), dashTheme.bg2);
	if (!brush_ready)
		buildBrush();
	if (brush_ready > 0) {
		gsKit_set_clamp(gsGlobal, GS_CMODE_REPEAT);
		/* stretched along the fibres, repeated across the screen */
		dashTexDrawUV(&brush_tex, 0, 0, DASH_VW, DASH_VH, 0, 0, BRUSH_W * 2.5f, BRUSH_H * 3.5f, colA(c, 44));
		gsKit_set_clamp(gsGlobal, GS_CMODE_CLAMP);
	}
	/* a soft light band slowly crossing the surface */
	{
		float x = -300.0f + sheen * (DASH_VW + 600.0f);
		DashColor z = colA(DC(255, 255, 255, 255), 0), m = colA(DC(255, 255, 255, 255), 24);
		dashQuadG(x, 0, x + 150, 0, x - 120, DASH_VH, x + 30, DASH_VH, z, m, z, m);
		dashQuadG(x + 150, 0, x + 300, 0, x + 30, DASH_VH, x + 180, DASH_VH, m, z, m, z);
	}
	dashBgVignette(0.6f);
}

//--------------------------------------------------------------
// Ribbons (PSX)
//--------------------------------------------------------------
#define WAVE_SEGS 32

void dashBgSceneWave(float t, int fx)
{
	DashColor c = dashTheme.bg1;
	int r, i, nr = (fx == DASH_FX_SUBTLE) ? 2 : 3;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.07f), dashColorScale(dashTheme.bg2, 0.8f));
	dashGfxBlend(DASH_BLEND_ADD);
	for (r = 0; r < nr; r++) {
		float y0 = 250.0f + r * 26.0f, amp = 34.0f - r * 8.0f;
		float k = 0.0085f + r * 0.0021f, w = 0.16f + r * 0.05f, ph = r * 1.7f;
		float th0 = 22.0f + r * 10.0f;
		int alpha = 30 - r * 6;
		float px = 0, py = 0, pt = 0;

		for (i = 0; i <= WAVE_SEGS; i++) {
			float x = i * (DASH_VW / (float)WAVE_SEGS);
			float y = y0 + amp * dashSinf(x * k + t * w + ph) + 10.0f * dashSinf(x * k * 2.3f - t * w * 0.7f);
			float th = th0 * (0.6f + 0.4f * dashSinf(x * 0.006f + t * 0.1f + r));

			if (i > 0) {
				DashColor z = colA(c, 0), m = colA(c, alpha);
				dashQuadG(px, py - pt, x, y - th, px, py, x, y, z, z, m, m);
				dashQuadG(px, py, x, y, px, py + pt, x, y + th, m, m, z, z);
			}
			px = x;
			py = y;
			pt = th;
		}
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashBgVignette(0.7f);
}
//--------------------------------------------------------------
//End of file: dash_bg_geo.c
//--------------------------------------------------------------
