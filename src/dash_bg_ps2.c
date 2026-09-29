//--------------------------------------------------------------
//File name:   dash_bg_ps2.c
//Background scenes modelled on the PS2 system software (OSDSYS).
//
//These are original, procedural re-creations informed by the osdbits
//OSDSYS reverse-engineering notes (docs/menu-backdrop.md,
//docs/towers-analysis.md, docs/menu-config.md, osdbits/opening.c). No
//BIOS resources are used or required: every surface is flat or gouraud
//shaded geometry and the shared soft glow sprite.
//
//  Tunnel     The menu backdrop is a cylinder around the camera whose far
//             end is black, drawn bright and tinted indigo afterwards. Here:
//             16 ribbons x 13 rings, per-panel brightness noise standing in
//             for the noise texture, slow travel and roll, tinted by bg1.
//  Mist       Large, very dim translucent forms on navy (no particles).
//  Towers     The opening's tower field: a grid of square prisms seen from
//             above, heights from a radial heightfield with a per-cell
//             tweak, lighter tops, sides fogged to black at the floor, a
//             blue fog pool in the middle, three small coloured point
//             lights and a slow camera roll. Drawn outermost first, which
//             is a correct painter's order for a look-down camera.
//  Disc       The illegal-disc screen: dark maroon, red cloud blobs
//             streaming past the camera (additive, low peak alpha), a
//             pulsing red flare and one slowly turning glass cube.
//  Orbit      Near black with the main menu's ring of eight lights.
//  ClockRing  System Configuration's twelve-rod clock: the ring turns with
//             the seconds, the rod of the current hour is lit and fills
//             with the minutes.
//--------------------------------------------------------------
#include "dash_internal.h"

#define PI_F 3.14159265f
#define TAU_F 6.28318531f

static DashColor colA(DashColor c, int a)
{
	c.a = (u8)((a < 0) ? 0 : (a > 255) ? 255 : a);
	return c;
}

//--------------------------------------------------------------
// Tunnel (OSD Classic)
//--------------------------------------------------------------
#define TUN_SEGS 16
#define TUN_RINGS 13

void dashBgSceneTunnel(float t, int fx)
{
	DashColor wall = dashTheme.bg1, far_c = dashTheme.bg2;
	float travel = t * 0.095f; /* rings per second */
	float s = travel - (float)(int)travel;
	int base_ring = (int)travel;
	float roll = t * 0.052f;
	float cx = 320.0f + dashSinf(t * 0.070f) * 22.0f;
	float cy = 214.0f + dashCosf(t * 0.058f) * 14.0f;
	float px[TUN_RINGS + 1][TUN_SEGS + 1], py[TUN_RINGS + 1][TUN_SEGS + 1];
	float bright[TUN_RINGS + 1];
	int k, j;

	(void)fx;
	dashRect(0, 0, DASH_VW, DASH_VH, far_c);
	/* ring k sits at depth 38 * 1.32^(k - s); screen radius = 230*110/z */
	for (k = 0; k <= TUN_RINGS; k++) {
		float z = 38.0f * powf(1.32f, (float)k - s);
		float r = 230.0f * 110.0f / z;
		float fog = dashClampf(1.0f - (z - 38.0f) / 1100.0f, 0.0f, 1.0f);

		bright[k] = fog * fog;
		for (j = 0; j <= TUN_SEGS; j++) {
			float a = roll + j * (TAU_F / TUN_SEGS);
			px[k][j] = cx + dashCosf(a) * r * 1.18f;
			py[k][j] = cy + dashSinf(a) * r;
		}
	}
	for (k = TUN_RINGS - 1; k >= 0; k--) {
		for (j = 0; j < TUN_SEGS; j++) {
			/* the panel pattern travels with its ring */
			float n = 0.5f + 0.5f * dashBgHash(k + base_ring, j);
			float n2 = 0.5f + 0.5f * dashBgHash(k + base_ring + 1, j);
			DashColor cn = colA(dashColorScale(wall, bright[k] * n), 255);
			DashColor cf = colA(dashColorScale(wall, bright[k + 1] * n2), 255);

			cn = dashColorLerp(far_c, cn, bright[k]);
			cf = dashColorLerp(far_c, cf, bright[k + 1]);
			dashQuadG(px[k][j], py[k][j], px[k][j + 1], py[k][j + 1],
			          px[k + 1][j], py[k + 1][j], px[k + 1][j + 1], py[k + 1][j + 1], cn, cn, cf, cf);
		}
	}
	/* the black far end, softened */
	dashGlow(cx, cy, 170, 130, colA(far_c, 200));
	dashBgVignette(0.8f);
}

//--------------------------------------------------------------
// Blue mist
//--------------------------------------------------------------
void dashBgSceneMist(float t, int fx)
{
	DashColor b = dashTheme.bg2, m = dashTheme.bg1;
	int i, n = (fx == DASH_FX_SUBTLE) ? 4 : 7;

	/* Midnight's blue-to-black base, with the Blue Mist motion layered over it. */
	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLerp(b, m, 0.34f), b);
	dashGfxBlend(DASH_BLEND_ADD);
	for (i = 0; i < n; i++) {
		float sp = 0.075f + i * 0.013f;
		float x = 320.0f + dashSinf(t * sp + i * 1.9f) * (275.0f - i * 14.0f);
		float y = 205.0f + dashCosf(t * sp * 0.74f + i * 2.7f) * (118.0f + i * 9.0f);
		float rx = 220.0f + 70.0f * dashSinf(t * 0.10f + i);
		float ry = 92.0f + 34.0f * dashCosf(t * 0.12f + i * 1.3f);
		dashGlow(x, y, rx, ry, colA(dashColorLighten(m, 0.14f), 25 + (i & 1) * 10));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashBgVignette(0.72f);
}

//--------------------------------------------------------------
// PS2 towers
//--------------------------------------------------------------
#define TW_NX 15
#define TW_NY 11
#define TW_N (TW_NX * TW_NY)
#define TW_PITCH 10.0f
#define TW_HALF 3.7f

typedef struct
{
	float x, y, h;
} TowerCell;

static TowerCell tw_cells[TW_N];
static short tw_order[TW_N];
static int tw_count;

static void towersInit(void)
{
	int i, j, n = 0;

	if (tw_count)
		return;
	for (j = 0; j < TW_NY; j++) {
		for (i = 0; i < TW_NX; i++) {
			float x = (i - TW_NX / 2) * TW_PITCH, y = (j - TW_NY / 2) * TW_PITCH;
			/* two radial terms like the original heightfield, plus the
			 * integer per-cell tweak that breaks the symmetry */
			float d1 = sqrtf((x + 12.0f) * (x + 12.0f) + (y + 8.0f) * (y + 8.0f));
			float d2 = sqrtf((x - 14.0f) * (x - 14.0f) + (y - 10.0f) * (y - 10.0f));
			float h = 16.0f + d1 * 0.55f + d2 * 0.35f;
			int tweak = ((i + 3) * (i + j + 6) / (j + 1)) % 11 - 5;

			h += tweak * 3.2f + dashBgHash(i, j) * 10.0f;
			if (h > 96.0f)
				h = 96.0f - (h - 96.0f) * 0.3f;
			if (h < 8.0f)
				h = 8.0f;
			/* a few missing cells keep the field from looking tiled */
			if (dashBgHash(j + 7, i + 3) < 0.07f)
				continue;
			tw_cells[n].x = x;
			tw_cells[n].y = y;
			tw_cells[n].h = h;
			n++;
		}
	}
	tw_count = n;
	/* outermost first */
	for (i = 0; i < n; i++)
		tw_order[i] = (short)i;
	for (i = 1; i < n; i++) {
		short v = tw_order[i];
		float dv = tw_cells[v].x * tw_cells[v].x + tw_cells[v].y * tw_cells[v].y;
		j = i - 1;
		while (j >= 0) {
			float dj = tw_cells[tw_order[j]].x * tw_cells[tw_order[j]].x + tw_cells[tw_order[j]].y * tw_cells[tw_order[j]].y;
			if (dj >= dv)
				break;
			tw_order[j + 1] = tw_order[j];
			j--;
		}
		tw_order[j + 1] = v;
	}
}

typedef struct
{
	float ca, sa, cx, cy, cam_h, f;
} TowerCam;

static void twProject(const TowerCam *c, float x, float y, float h, float *sx, float *sy)
{
	float k = c->f / (c->cam_h - h);
	float rx = x * c->ca - y * c->sa, ry = x * c->sa + y * c->ca;

	*sx = c->cx + rx * k * 1.12f;
	*sy = c->cy + ry * k;
}

void dashBgSceneTowers(float t, int fx)
{
	DashColor wall = dashTheme.bg1, fog = dashTheme.bg2;
	DashColor black = DC(0, 0, 0, 255);
	TowerCam cam;
	int n;

	towersInit();
	cam.ca = dashCosf(0.22f * dashSinf(t * 0.14f) + t * 0.024f);
	cam.sa = dashSinf(0.22f * dashSinf(t * 0.14f) + t * 0.024f);
	cam.cx = 320.0f + dashSinf(t * 0.11f) * 24.0f;
	cam.cy = 224.0f + dashCosf(t * 0.085f) * 17.0f;
	cam.cam_h = 150.0f + 15.0f * dashSinf(t * 0.19f);
	cam.f = 300.0f;

	dashRect(0, 0, DASH_VW, DASH_VH, black);
	/* the fog pool the field falls into */
	dashGlow(cam.cx, cam.cy, 300, 230, colA(dashColorScale(fog, 0.7f), 150));

	for (n = 0; n < tw_count; n++) {
		const TowerCell *c = &tw_cells[tw_order[n]];
		float x0 = c->x - TW_HALF, x1 = c->x + TW_HALF, y0 = c->y - TW_HALF, y1 = c->y + TW_HALF;
		float tx[4], ty[4], bx[4], by[4];
		float lum = 0.18f + 0.82f * powf(c->h / 96.0f, 1.3f);
		DashColor top = colA(dashColorScale(wall, lum * 0.62f), 255);
		DashColor side_hi = colA(dashColorScale(wall, lum * 0.36f), 255);
		DashColor side_lo = colA(dashColorLerp(black, fog, 0.18f), 255);
		/* ground level: well below the camera's view of the floor */
		const float gh = -40.0f;

		twProject(&cam, x0, y0, c->h, &tx[0], &ty[0]);
		twProject(&cam, x1, y0, c->h, &tx[1], &ty[1]);
		twProject(&cam, x0, y1, c->h, &tx[2], &ty[2]);
		twProject(&cam, x1, y1, c->h, &tx[3], &ty[3]);
		twProject(&cam, x0, y0, gh, &bx[0], &by[0]);
		twProject(&cam, x1, y0, gh, &bx[1], &by[1]);
		twProject(&cam, x0, y1, gh, &bx[2], &by[2]);
		twProject(&cam, x1, y1, gh, &bx[3], &by[3]);

		/* the faces that look towards the camera axis */
		if (x0 > 0.0f) /* -X face: corners 0,2 */
			dashQuadG(tx[0], ty[0], tx[2], ty[2], bx[0], by[0], bx[2], by[2],
			          dashColorScale(side_hi, 1.15f), dashColorScale(side_hi, 1.15f), side_lo, side_lo);
		else if (x1 < 0.0f) /* +X face: corners 1,3 */
			dashQuadG(tx[1], ty[1], tx[3], ty[3], bx[1], by[1], bx[3], by[3],
			          dashColorScale(side_hi, 0.8f), dashColorScale(side_hi, 0.8f), side_lo, side_lo);
		if (y0 > 0.0f) /* -Y face: corners 0,1 */
			dashQuadG(tx[0], ty[0], tx[1], ty[1], bx[0], by[0], bx[1], by[1],
			          side_hi, side_hi, side_lo, side_lo);
		else if (y1 < 0.0f) /* +Y face: corners 2,3 */
			dashQuadG(tx[2], ty[2], tx[3], ty[3], bx[2], by[2], bx[3], by[3],
			          dashColorScale(side_hi, 0.7f), dashColorScale(side_hi, 0.7f), side_lo, side_lo);

		/* top face, a touch lighter towards the upper left */
		dashQuadG(tx[0], ty[0], tx[1], ty[1], tx[2], ty[2], tx[3], ty[3],
		          dashColorScale(top, 1.1f), top, top, dashColorScale(top, 0.86f));
		if (fx == DASH_FX_FULL && c->h > 40.0f) {
			/* the faint cross of the wall texture */
			DashColor cr = colA(dashColorScale(wall, lum * 0.8f), 36);
			dashLine((tx[0] + tx[1]) * 0.5f, (ty[0] + ty[1]) * 0.5f, (tx[2] + tx[3]) * 0.5f, (ty[2] + ty[3]) * 0.5f, 1.0f, cr);
			dashLine((tx[0] + tx[2]) * 0.5f, (ty[0] + ty[2]) * 0.5f, (tx[1] + tx[3]) * 0.5f, (ty[1] + ty[3]) * 0.5f, 1.0f, cr);
		}
	}

	dashGfxBlend(DASH_BLEND_ADD);
	/* blue haze over the pit */
	dashGlow(cam.cx, cam.cy, 170, 130, colA(fog, 70));
	if (fx != DASH_FX_OFF) {
		/* three small drifting lights (red, green, violet) */
		static const u8 lc[3][3] = {{255, 60, 60}, {70, 255, 110}, {170, 90, 255}};
		int i;
		for (i = 0; i < 3; i++) {
			float a = t * (0.16f + i * 0.041f) + i * 2.1f;
			float lx = cam.cx + dashCosf(a) * (120.0f + i * 50.0f);
			float ly = cam.cy + dashSinf(a * 1.3f) * (70.0f + i * 30.0f);
			dashGlow(lx, ly, 3.0f, 3.0f, DC(lc[i][0], lc[i][1], lc[i][2], 220));
			dashGlow(lx, ly, 12.0f, 12.0f, DC(lc[i][0], lc[i][1], lc[i][2], 70));
		}
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	/* the field is bright: keep the UI readable */
	dashRect(0, 0, DASH_VW, DASH_VH, DC(0, 0, 0, 96));
	dashBgVignette(1.0f);
}

//--------------------------------------------------------------
// Disc error red
//--------------------------------------------------------------
#define DISC_CLOUDS 22

typedef struct
{
	float x, y, z;
} V3;

static void rotV(V3 *v, float ax, float ay)
{
	float c = dashCosf(ax), s = dashSinf(ax), y, z, x;

	y = v->y * c - v->z * s;
	z = v->y * s + v->z * c;
	v->y = y;
	v->z = z;
	c = dashCosf(ay);
	s = dashSinf(ay);
	x = v->x * c + v->z * s;
	z = -v->x * s + v->z * c;
	v->x = x;
	v->z = z;
}

/* Translucent cube with shaded faces (backface culled). */
static void glassCube(float cx, float cy, float size, float ax, float ay, DashColor col, float alpha)
{
	static const signed char vert[8][3] = {
	    {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};
	/* faces as strip-order quads: a, b, c, d with a-b and c-d opposite edges */
	static const u8 face[6][4] = {
	    {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 6, 2}, {1, 5, 3, 7}, {4, 5, 0, 1}, {2, 3, 6, 7}};
	float sx[8], sy[8], sz[8];
	int i;

	for (i = 0; i < 8; i++) {
		V3 v = {vert[i][0] * 1.0f, vert[i][1] * 1.0f, vert[i][2] * 1.0f};
		float k;
		rotV(&v, ax, ay);
		k = 4.0f / (4.0f + v.z * 0.35f);
		sx[i] = cx + v.x * size * k;
		sy[i] = cy + v.y * size * k;
		sz[i] = v.z;
	}
	for (i = 0; i < 6; i++) {
		const u8 *f = face[i];
		/* screen-space winding of a->b->d */
		float cross = (sx[f[1]] - sx[f[0]]) * (sy[f[3]] - sy[f[0]]) - (sy[f[1]] - sy[f[0]]) * (sx[f[3]] - sx[f[0]]);
		float depth = (sz[f[0]] + sz[f[1]] + sz[f[2]] + sz[f[3]]) * 0.25f;
		float light = dashClampf(0.55f - depth * 0.35f, 0.15f, 1.0f);
		DashColor c1 = colA(dashColorScale(col, light * 1.2f), (int)(alpha * 120));
		DashColor c2 = colA(dashColorScale(col, light * 0.6f), (int)(alpha * 90));

		if (cross <= 0.0f)
			continue;
		dashQuadG(sx[f[0]], sy[f[0]], sx[f[1]], sy[f[1]], sx[f[2]], sy[f[2]], sx[f[3]], sy[f[3]], c1, c2, c2, c1);
		/* lit rim */
		dashLine(sx[f[0]], sy[f[0]], sx[f[1]], sy[f[1]], 1.2f, colA(dashColorLighten(col, 0.4f), (int)(alpha * 70)));
	}
}

void dashBgSceneDisc(float t, int fx)
{
	DashColor red = dashTheme.bg1, base_c = dashTheme.bg2;
	float cx = 320.0f, cy = 206.0f;
	int i, n = (fx == DASH_FX_FULL) ? DISC_CLOUDS : DISC_CLOUDS / 2;
	float pulse = 0.82f + 0.18f * dashSinf(t * 1.35f);

	dashRect(0, 0, DASH_VW, DASH_VH, base_c);
	/* the dim red body of the cloud bank */
	dashGlow(cx, cy, 380, 300, colA(dashColorScale(red, 0.55f), 160));

	dashGfxBlend(DASH_BLEND_ADD);
	/* cloud blobs streaming towards the camera */
	for (i = 0; i < n; i++) {
		float life = t * (0.050f + (i % 5) * 0.008f) + dashBgHash(i, 3);
		float z = 1.0f - (life - (float)(int)life); /* 1 far .. 0 near */
		float ang = dashBgHash(i, 11) * TAU_F + t * 0.01f * ((i & 1) ? 1 : -1);
		float rad = 30.0f + dashBgHash(i, 17) * 120.0f;
		float k = 1.0f / (0.25f + z * 1.4f);
		float x = cx + dashCosf(ang) * rad * k * 1.2f;
		float y = cy + dashSinf(ang) * rad * k * 0.9f;
		float size = (70.0f + dashBgHash(i, 5) * 80.0f) * k * 0.6f;
		/* fade in far away, out as it passes the camera (peak ~ 1/4) */
		float a = dashClampf((1.0f - z) * 3.0f, 0.0f, 1.0f) * dashClampf(z * 4.0f, 0.0f, 1.0f);
		DashColor cc = dashColorLerp(red, DC(255, 120, 110, 255), dashBgHash(i, 23) * 0.4f);

		dashGlow(x, y, size, size * 0.8f, colA(cc, (int)(34 * a)));
		dashGlow(x + size * 0.35f, y - size * 0.2f, size * 0.7f, size * 0.55f, colA(cc, (int)(22 * a)));
	}
	/* the flare above the centre */
	dashGlow(cx + 8, cy - 26, 60 * pulse, 52 * pulse, colA(DC(255, 110, 110, 255), 60));
	dashGlow(cx + 8, cy - 26, 230, 190, colA(dashColorScale(red, 0.8f), 40));
	dashGfxBlend(DASH_BLEND_NORMAL);

	/* glass cubes: one large, one small and far */
	glassCube(cx + 110 + dashSinf(t * 0.05f) * 10.0f, cy - 70 + dashCosf(t * 0.04f) * 8.0f, 58.0f,
	          0.4f + t * 0.20f, 0.7f + t * 0.13f, red, 1.0f);
	if (fx != DASH_FX_OFF)
		glassCube(cx - 30 + dashSinf(t * 0.03f + 1.0f) * 16.0f, cy + 70, 14.0f, 1.1f + t * 0.17f, t * 0.13f, red, 0.8f);

	/* dark edges */
	dashRectGradH(0, 0, 120, DASH_VH, colA(base_c, 220), colA(base_c, 0));
	dashRectGradH(DASH_VW - 120, 0, 120, DASH_VH, colA(base_c, 0), colA(base_c, 220));
	dashBgVignette(1.0f);
}

//--------------------------------------------------------------
// Orbit (OSD Minimal)
//--------------------------------------------------------------
void dashBgSceneOrbit(float t, int fx)
{
	DashColor lc = dashTheme.bg1;
	float cx = 508.0f, cy = 244.0f;
	int i, k;

	dashRect(0, 0, DASH_VW, DASH_VH, dashTheme.bg2);
	/* A faint changing path field instead of one rigid ring. */
	for (i = 0; i < 40; i++) {
		float a0 = i * (TAU_F / 40), a1 = (i + 1) * (TAU_F / 40);
		float r0 = 57.0f + 5.0f * dashSinf(a0 * 3.0f + t * 0.18f);
		float r1 = 57.0f + 5.0f * dashSinf(a1 * 3.0f + t * 0.18f);
		dashLine(cx + dashCosf(a0) * r0, cy + dashSinf(a0) * r0 * 0.78f,
		         cx + dashCosf(a1) * r1, cy + dashSinf(a1) * r1 * 0.78f, 1.0f, colA(lc, 20));
	}
	dashGfxBlend(DASH_BLEND_ADD);
	for (i = 0; i < 8; i++) {
		float speed = 0.20f + dashBgHash(i, 1) * 0.22f;
		float phase = i * (TAU_F / 8) + dashBgHash(i, 2) * 0.55f;
		float rx = 48.0f + dashBgHash(i, 3) * 25.0f;
		float ry = 31.0f + dashBgHash(i, 4) * 21.0f;
		float a = t * speed + phase;
		float depth = 0.5f + 0.5f * dashSinf(a + i * 0.71f);
		float size = 2.6f + depth * 2.8f;
		int alpha = 125 + (int)(depth * 100.0f);
		int trails = (fx == DASH_FX_SUBTLE) ? 5 : 10;

		for (k = trails; k >= 1; k--) {
			float ta = a - k * 0.055f;
			float fade = (trails - k + 1) / (float)(trails + 1);
			dashGlow(cx + dashCosf(ta) * rx, cy + dashSinf(ta) * ry,
			         size * (0.55f + fade * 0.35f), size * (0.55f + fade * 0.35f), colA(lc, (int)(42 * fade)));
		}
		dashGlow(cx + dashCosf(a) * rx, cy + dashSinf(a) * ry, size * 3.2f, size * 3.2f, colA(lc, 44 + (int)(depth * 34.0f)));
		dashGlow(cx + dashCosf(a) * rx, cy + dashSinf(a) * ry, size, size, colA(dashColorLighten(lc, 0.72f), alpha));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
}

//--------------------------------------------------------------
// Clock ring (System Configuration)
//--------------------------------------------------------------
void dashBgSceneClockRing(float t, int fx)
{
	DashColor rod = dashTheme.bg1, b = dashTheme.bg2;
	float cx = 430.0f, cy = 226.0f;
	int hour = 0, minute = 0, second = 0, i;
	static int clock_ready, held_hour, held_minute, rtc_second;
	static float held_second, last_anim_t;
	float now = dashTime(), spin, fill;

	menuTitleGetClockFields(NULL, NULL, NULL, &hour, &minute, &second, NULL, NULL);
	if (!clock_ready) {
		held_hour = hour;
		held_minute = minute;
		rtc_second = second;
		held_second = (float)second;
		last_anim_t = now;
		clock_ready = 1;
	}
	if (fx != DASH_FX_OFF) {
		/* Re-anchor on each RTC tick, then interpolate within the second. */
		if (hour != held_hour || minute != held_minute || second != rtc_second) {
			held_hour = hour;
			held_minute = minute;
			rtc_second = second;
			last_anim_t = now;
		}
		held_second = rtc_second + dashClampf(now - last_anim_t, 0.0f, 0.999f);
	}
	spin = -(held_second / 60.0f) * TAU_F;
	fill = (held_minute + held_second / 60.0f) / 60.0f;
	(void)t;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(b, 0.05f), dashColorScale(b, 0.55f));
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlow(cx, cy, 260, 220, colA(dashColorScale(b, 1.4f), 90));
	for (i = 0; i < 12; i++) {
		float a = spin + i * (TAU_F / 12) - PI_F * 0.5f;
		float ca = dashCosf(a), sa = dashSinf(a);
		float r0 = 70.0f, r1 = 190.0f, w = 7.0f;
		float nx = -sa * w, ny = ca * w;
		int lit = (i == held_hour % 12);
		DashColor ci = colA(rod, lit ? 110 : 34), co = colA(rod, 0);

		dashQuadG(cx + ca * r0 + nx, cy + sa * r0 * 0.9f + ny, cx + ca * r0 - nx, cy + sa * r0 * 0.9f - ny,
		          cx + ca * r1 + nx, cy + sa * r1 * 0.9f + ny, cx + ca * r1 - nx, cy + sa * r1 * 0.9f - ny, ci, ci, co, co);
		if (lit && fx != DASH_FX_OFF) {
			float rf = r0 + (r1 - r0) * fill;
			DashColor cb = colA(dashColorLighten(rod, 0.4f), 150);
			dashQuadG(cx + ca * r0 + nx * 0.5f, cy + sa * r0 * 0.9f + ny * 0.5f, cx + ca * r0 - nx * 0.5f, cy + sa * r0 * 0.9f - ny * 0.5f,
			          cx + ca * rf + nx * 0.5f, cy + sa * rf * 0.9f + ny * 0.5f, cx + ca * rf - nx * 0.5f, cy + sa * rf * 0.9f - ny * 0.5f, cb, cb, cb, cb);
		}
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashBgVignette(0.9f);
}

//--------------------------------------------------------------
// Light streaks (the opening's light pillars)
//--------------------------------------------------------------
static void streak(float x, float yc, float len, float w, float tilt, DashColor c)
{
	float dx = dashSinf(tilt) * len * 0.5f, dy = dashCosf(tilt) * len * 0.5f;
	float px = dashCosf(tilt) * w * 0.5f, py = -dashSinf(tilt) * w * 0.5f;

	dashQuadG(x - dx - px, yc - dy - py, x - dx + px, yc - dy + py,
	          x + dx - px, yc + dy - py, x + dx + px, yc + dy + py, c, c, c, c);
}

void dashBgSceneStreaks(float t, int fx)
{
	DashColor base = dashTheme.bg2, glow = dashTheme.bg1;
	int i, n = (fx == DASH_FX_SUBTLE) ? 7 : 12;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(base, 0.06f), base);
	dashGfxBlend(DASH_BLEND_ADD);
	for (i = 0; i < n; i++) {
		float sp = 26.0f + (i % 4) * 14.0f;              /* px/s horizontal drift */
		float x = fmodf(t * sp + i * 173.0f, (float)(DASH_VW + 240)) - 120.0f;
		float w = 3.0f + (i % 3) * 2.5f;
		float len = 190.0f + 60.0f * dashBgHash(i, 7);
		float yc = DASH_VH * 0.5f + (dashBgHash(i, 3) - 0.5f) * DASH_VH * 0.3f;
		float tilt = 0.22f;
		DashColor c = colA(glow, 24 + (i % 3) * 10);

		streak(x, yc, len, w, tilt, c);
		streak(x, yc, len, w * 0.45f, tilt, colA(dashColorLighten(glow, 0.5f), c.a + 14));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashBgVignette(0.75f);
}

//--------------------------------------------------------------
// Drift clouds (the illegal-disc cloud field, theme-tinted)
//--------------------------------------------------------------
void dashBgSceneClouds(float t, int fx)
{
	int i, n = (fx == DASH_FX_SUBTLE) ? 5 : 9;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.03f), dashTheme.bg2);
	for (i = 0; i < n; i++) {
		float depth = (i % 3) / 2.0f;                    /* 0 near .. 1 far */
		float sp = 6.0f + 26.0f * depth;                 /* px/s */
		float dir = (i & 1) ? 1.0f : -1.0f;
		float span = (float)(DASH_VW + 360);
		float x = fmodf(dir * t * sp + i * 211.0f, span);

		if (dir < 0.0f)
			x = span - x;
		x -= 180.0f;
		{
			float y = DASH_VH * 0.2f + depth * DASH_VH * 0.6f + dashSinf(t * 0.05f + i * 1.7f) * 14.0f;
			float rx = 150.0f + 90.0f * depth + 30.0f * dashBgHash(i, 11);
			float ry = 44.0f + 26.0f * depth;
			DashColor c = dashColorLerp(dashTheme.bg1, DC(255, 255, 255, 255), 0.25f + 0.2f * depth);

			dashGfxBlend(DASH_BLEND_ADD);
			dashGlow(x, y, rx, ry, colA(c, (int)(20 + 26 * (1.0f - depth))));
			dashGfxBlend(DASH_BLEND_NORMAL);
		}
	}
	dashBgVignette(0.8f);
}
//--------------------------------------------------------------
//End of file: dash_bg_ps2.c
//--------------------------------------------------------------
