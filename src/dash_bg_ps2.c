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
//  Orbit      The main menu's orbs on near black (see "OSD orbs" below).
//  ClockRing  System Configuration's twelve-rod clock: the ring turns with
//             the seconds, the rod of the current hour is lit and fills
//             with the minutes; the orbs fly inside it.
//
//  OSD orbs   The main menu / System Configuration "light spots" (Sony
//             patent US 6,693,606: light spots moving around a single
//             simulative sphere, their tracks drawn by after-image
//             processing so the sphere emerges, the sphere shrinking in a
//             constant period). Seven orbs ride ONE circle through the
//             sphere, orb k at phase k * 2pi * s/60 of the real-time clock,
//             which gives the known groupings: all seven meet at :00, two
//             groups at :30, three at :20/:40, four at :15/:45, five at
//             :12/:24/:36/:48, six at :10/:50. The circle's plane tumbles
//             and each trail sample uses the plane of its own moment, so
//             the trails of past orientations paint the globe. The sphere
//             is small at the top of the hour and grows through it. Better
//             than the original: smooth sub-second motion, two-layer glow
//             trails with depth fade, and merged orbs bloom into one.
//--------------------------------------------------------------
#include "dash_internal.h"

#define PI_F 3.14159265f
#define TAU_F 6.28318531f

static DashColor colA(DashColor c, int a)
{
	c.a = (u8)((a < 0) ? 0 : (a > 255) ? 255 : a);
	return c;
}

/* OSD orbs (defined below, also drawn by the tunnel and the clock ring) */
typedef struct
{
	float cx, cy;     /* sphere centre on screen */
	float rmin, rmax; /* sphere radius at the top / end of the hour */
	DashColor c;      /* orb blue */
} OrbStyle;
static void osdOrbs(const OrbStyle *st, float t, int fx);
static OrbStyle menuOrbStyle(DashColor c);

//--------------------------------------------------------------
// Tunnel (OSD Classic)
//--------------------------------------------------------------
#define TUN_SEGS 16
#define TUN_RINGS 13

void dashBgSceneTunnel(float t, int fx)
{
	DashColor wall = dashTheme.bg1, far_c = dashTheme.bg2;
	float travel = t * 0.095f;
	float s = travel - (float)(int)travel;
	int base_ring = (int)travel;
	float roll = t * 0.052f;
	float cx = 320.0f + dashSinf(t * 0.070f) * 22.0f;
	float cy = 214.0f + dashCosf(t * 0.058f) * 14.0f;
	float px[TUN_RINGS + 1][TUN_SEGS + 1], py[TUN_RINGS + 1][TUN_SEGS + 1];
	float ca[TUN_SEGS + 1], sa[TUN_SEGS + 1];
	float bright[TUN_RINGS + 1];
	float z;
	int seg_step = (fx == DASH_FX_FULL) ? 1 : 2;
	int k, j;

	dashRect(0, 0, DASH_VW, DASH_VH, far_c);
	/* Angles are shared by every depth ring. Depth itself is geometric, so
	 * one powf plus multiplication replaces repeated trig/powf inner work. */
	for (j = 0; j <= TUN_SEGS; j += seg_step) {
		float a = roll + j * (TAU_F / TUN_SEGS);
		ca[j] = dashCosf(a);
		sa[j] = dashSinf(a);
	}
	z = 38.0f * powf(1.32f, -s);
	for (k = 0; k <= TUN_RINGS; k++) {
		float radius = 230.0f * 110.0f / z;
		float fog = dashClampf(1.0f - (z - 38.0f) / 1100.0f, 0.0f, 1.0f);

		bright[k] = fog * fog;
		for (j = 0; j <= TUN_SEGS; j += seg_step) {
			px[k][j] = cx + ca[j] * radius * 1.18f;
			py[k][j] = cy + sa[j] * radius;
		}
		z *= 1.32f;
	}
	for (k = TUN_RINGS - 1; k >= 0; k--) {
		for (j = 0; j < TUN_SEGS; j += seg_step) {
			int j2 = j + seg_step;
			float n = 0.5f + 0.5f * dashBgHash(k + base_ring, j);
			float n2 = 0.5f + 0.5f * dashBgHash(k + base_ring + 1, j);
			DashColor cn = colA(dashColorScale(wall, bright[k] * n), 255);
			DashColor cf = colA(dashColorScale(wall, bright[k + 1] * n2), 255);

			cn = dashColorLerp(far_c, cn, bright[k]);
			cf = dashColorLerp(far_c, cf, bright[k + 1]);
			dashQuadG(px[k][j], py[k][j], px[k][j2], py[k][j2],
			          px[k + 1][j], py[k + 1][j], px[k + 1][j2], py[k + 1][j2],
			          cn, cn, cf, cf);
		}
	}
	dashGlow(cx, cy, 170, 130, colA(far_c, 200));
	{
		OrbStyle st = menuOrbStyle(DC(80, 160, 255, 255));
		osdOrbs(&st, t, fx);
	}
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
	float x, y, h, lum;
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
			tw_cells[n].lum = 0.18f + 0.82f * powf(h / 96.0f, 1.3f);
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

static inline void twProject(const TowerCam *c, float x, float y, float h, float *sx, float *sy)
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
	DashColor side_lo = colA(dashColorLerp(black, fog, 0.18f), 255);
	float cam_angle;
	int n;

	towersInit();
	cam_angle = 0.22f * dashSinf(t * 0.14f) + t * 0.024f;
	cam.ca = dashCosf(cam_angle);
	cam.sa = dashSinf(cam_angle);
	cam.cx = 320.0f + dashSinf(t * 0.11f) * 24.0f;
	cam.cy = 224.0f + dashCosf(t * 0.085f) * 17.0f;
	cam.cam_h = 150.0f + 15.0f * dashSinf(t * 0.19f);
	cam.f = 300.0f;

	dashRect(0, 0, DASH_VW, DASH_VH, black);
	/* the fog pool the field falls into */
	dashGlow(cam.cx, cam.cy, 300, 230, colA(dashColorScale(fog, 0.7f), 150));

	for (n = 0; n < tw_count; n++) {
		const TowerCell *c;
		float x0, x1, y0, y1;
		float tx[4], ty[4], bx[4], by[4];
		float lum;
		DashColor top, side_hi;

		if (fx != DASH_FX_FULL && (n & 1))
			continue;
		c = &tw_cells[tw_order[n]];
		x0 = c->x - TW_HALF;
		x1 = c->x + TW_HALF;
		y0 = c->y - TW_HALF;
		y1 = c->y + TW_HALF;
		lum = c->lum;
		top = colA(dashColorScale(wall, lum * 0.62f), 255);
		side_hi = colA(dashColorScale(wall, lum * 0.36f), 255);
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

static void rotV(V3 *v, float cax, float sax, float cay, float say)
{
	float y = v->y * cax - v->z * sax;
	float z = v->y * sax + v->z * cax;
	float x;

	v->y = y;
	v->z = z;
	x = v->x * cay + v->z * say;
	z = -v->x * say + v->z * cay;
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
	float cax = dashCosf(ax), sax = dashSinf(ax);
	float cay = dashCosf(ay), say = dashSinf(ay);
	int i;

	for (i = 0; i < 8; i++) {
		V3 v = {vert[i][0] * 1.0f, vert[i][1] * 1.0f, vert[i][2] * 1.0f};
		float k;
		rotV(&v, cax, sax, cay, say);
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
	dashGlow(cx, cy, 380, 300, colA(dashColorScale(red, 0.55f), 145));

	/* Render broad, overlapping cloud shapes with ordinary alpha. The
	 * previous additive pass stacked every cloud into shiny clipped reds,
	 * especially on interlaced 4:3 displays. Reserve additive blending
	 * for the focused flare below. */
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

		dashGlow(x, y, size, size * 0.8f, colA(cc, (int)(30 * a)));
		dashGlow(x + size * 0.35f, y - size * 0.2f, size * 0.7f, size * 0.55f, colA(cc, (int)(19 * a)));
	}
	/* A restrained additive focal flare keeps the red disc-error
	 * identity without burning out all of the atmospheric cloud detail. */
	dashGfxBlend(DASH_BLEND_ADD);
	/* the flare above the centre */
	dashGlow(cx + 8, cy - 26, 60 * pulse, 52 * pulse, colA(DC(255, 110, 110, 255), 52));
	dashGlow(cx + 8, cy - 26, 230, 190, colA(dashColorScale(red, 0.8f), 35));
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
// OSD orbs (main menu, System Configuration)
//--------------------------------------------------------------
typedef struct
{
	int hour, minute;
	float second; /* 0..60, smooth between the RTC's once-a-second ticks */
} OsdTime;

/* RTC-anchored visual time. A stalled/invalid RTC must not freeze the orbs;
 * continue on the dashboard's visual clock, still held at 0% motion. This
 * does not synthesize a persistent wall-clock date for the footer. */
static void osdTime(int fx, OsdTime *o)
{
	static int ready, held_hour, held_minute, rtc_hour, rtc_minute, rtc_second, rtc_valid;
	static float held_second, tick_t;
	float now = dashTime();
	int hour = 0, minute = 0, second = 0, valid;

	valid = menuTitleGetClockFields(NULL, NULL, NULL, &hour, &minute, &second, NULL, NULL);
	if (!ready) {
		rtc_hour = held_hour = valid ? hour : 0;
		rtc_minute = held_minute = valid ? minute : 0;
		rtc_second = valid ? second : 0;
		held_second = (float)rtc_second;
		rtc_valid = valid;
		tick_t = now;
		ready = 1;
	}
	if (fx != DASH_FX_OFF) {
		float seconds;
		int minutes;
		/* Re-anchor only on a valid RTC tick or recovery, not on computed
		 * minute rollover. Otherwise frozen RTC fields would reset the phase. */
		if (valid && (!rtc_valid || hour != rtc_hour || minute != rtc_minute || second != rtc_second)) {
			rtc_hour = hour;
			rtc_minute = minute;
			rtc_second = second;
			tick_t = now;
		}
		rtc_valid = valid;
		seconds = rtc_second + (now > tick_t ? now - tick_t : 0.0f);
		minutes = (int)(seconds / 60.0f);
		held_second = seconds - minutes * 60.0f;
		minutes += rtc_hour * 60 + rtc_minute;
		held_hour = (minutes / 60) % 24;
		held_minute = minutes % 60;
	}
	o->hour = held_hour;
	o->minute = held_minute;
	o->second = held_second;
}

#define ORB_N 7
#define ORB_SAMPLES 32 /* visually continuous trails without spending 672 line draws/frame */
#define ORB_SPAN 6.5f  /* seconds of after-image */
#define ORB_BASE 20.0f /* nominal full revolution (orb speeds intentionally differ) */
#define ORB_CAM 420.0f /* perspective distance */

typedef struct
{
	float m[9];
} OrbMat;

/* orientation of the orbs' circle at scene time t: a slow tumble */
static void orbPlane(float t, OrbMat *o)
{
	float yaw = 0.9f + t * 0.33f;
	float pitch = 0.62f + 0.55f * dashSinf(t * 0.19f);
	float roll = 0.3f + 0.35f * dashSinf(t * 0.13f + 1.3f);
	float cy = dashCosf(yaw), sy = dashSinf(yaw), cp = dashCosf(pitch), sp = dashSinf(pitch);
	float cr = dashCosf(roll), sr = dashSinf(roll);

	/* Rz(roll) * Rx(pitch) * Ry(yaw); only columns 0 and 2 are used */
	o->m[0] = cr * cy - sr * sp * sy;
	o->m[2] = cr * sy + sr * sp * cy;
	o->m[3] = sr * cy + cr * sp * sy;
	o->m[5] = sr * sy - cr * sp * cy;
	o->m[6] = -cp * sy;
	o->m[8] = cp * cy;
}

typedef struct
{
	float x, y, s, d; /* screen position, perspective scale, depth 0 back..1 front */
} OrbPt;

static void orbProject(const OrbStyle *st, const OrbMat *o, float phi, float r, OrbPt *p)
{
	float ca = dashCosf(phi), sa = dashSinf(phi);
	float x = r * (o->m[0] * ca + o->m[2] * sa);
	float y = r * (o->m[3] * ca + o->m[5] * sa);
	float z = r * (o->m[6] * ca + o->m[8] * sa);
	float k = ORB_CAM / (ORB_CAM - z);

	p->x = st->cx + x * k;
	p->y = st->cy - y * k;
	p->s = k;
	p->d = (z / r + 1.0f) * 0.5f;
}

/* Time-continuous orbit phase. Multiplying a modulo-20-second phase by
 * per-orb non-integer speeds made every orb teleport at each wrap. The
 * dashboard scene clock is monotonic (and freezes at 0% effects), unlike
 * the RTC seconds field, so no time-base restart is needed. */
static float orbPhaseAt(int k, float t)
{
	float dir = (k & 1) ? -1.0f : 1.0f;
	float rate = (TAU_F / ORB_BASE) * (0.70f + dashBgHash(k, 71) * 0.75f);
	float wobble_rate = 0.08f + dashBgHash(k, 72) * 0.08f;

	return k * (TAU_F / ORB_N) + dir * rate * t +
	       0.12f * dashSinf(t * wobble_rate + dashBgHash(k, 73) * TAU_F);
}

/* Smooth AND periodic at the hour boundary: small at :00, fullest at
 * :30, shrinking naturally towards :00 again. The former linear grow
 * jumped from rmax straight back to rmin at every hour transition. */
static float orbRadiusAt(const OrbStyle *st, int minute, float second)
{
	float hour_frac = (minute + second / 60.0f) / 60.0f;
	float amount = 0.5f - 0.5f * dashCosf(TAU_F * hour_frac);

	return st->rmin + (st->rmax - st->rmin) * amount;
}

static void osdOrbs(const OrbStyle *st, float t, int fx)
{
	static OrbMat mats[ORB_SAMPLES + 1];
	OrbPt pts[ORB_SAMPLES + 1], head[ORB_N];
	float head_phi[ORB_N];
	OsdTime ot;
	float r;
	DashColor glow_c = dashColorScale(st->c, 0.8f), core_c = dashColorLighten(st->c, 0.35f);
	DashColor halo_c = dashColorLighten(st->c, 0.62f);
	int n = (fx == DASH_FX_SUBTLE) ? ORB_SAMPLES / 2 : ORB_SAMPLES;
	float span = (fx == DASH_FX_SUBTLE) ? ORB_SPAN * 0.6f : ORB_SPAN;
	int k, i, j;

	if (fx == DASH_FX_OFF) {
		/* a still frame still shows short tracks */
		n = ORB_SAMPLES / 3;
		span = ORB_SPAN * 0.4f;
	}
	osdTime(fx, &ot);
	r = orbRadiusAt(st, ot.minute, ot.second);

	/* faint haze around the sphere */
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlow(st->cx, st->cy, r * 2.4f, r * 2.4f, colA(dashColorScale(st->c, 0.5f), 40));

	/* the circle's plane at each trail moment, shared by all orbs */
	for (i = 0; i <= n; i++)
		orbPlane(t - span * i / n, &mats[i]);

	for (k = 0; k < ORB_N; k++) {
		float phi = orbPhaseAt(k, t);

		/* Sample the same physical trajectory at the actual moment of each
		 * after-image. This includes the slow wobble at that past moment,
		 * instead of extrapolating a different motion for the trail. */
		for (i = 0; i <= n; i++)
			orbProject(st, &mats[i], orbPhaseAt(k, t - span * i / n), r, &pts[i]);
		/* after-image: a wide soft glow, then a thin bright core */
		for (j = 0; j < 2; j++) {
			float width = j ? 2.2f : 11.0f;
			float amax = j ? 178.0f : 22.0f;
			DashColor c = j ? core_c : glow_c;

			for (i = 1; i <= n; i++) {
				float fade = 1.0f - (float)i / n;
				float a = amax * fade * (0.3f + 0.7f * pts[i].d);
				if (a < 1.0f)
					continue;
				dashLine(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y,
				         width * (0.35f + 0.65f * fade) * pts[i].s, colA(c, (int)a));
			}
		}
		head[k] = pts[0];
		head_phi[k] = fmodf(phi, TAU_F);
		if (head_phi[k] < 0.0f)
			head_phi[k] += TAU_F;
	}

	/* orbs that meet merge into one bigger, brighter orb. They share one
	 * circle, so meeting means equal phase; screen distance would also
	 * bloom a front and a back orb that merely cross in the projection. */
	for (k = 0; k < ORB_N; k++) {
		int together = 1, already_drawn = 0;
		float grow, share, sz = head[k].s * (0.7f + 0.4f * head[k].d);

		for (j = 0; j < ORB_N; j++) {
			float dp = fabsf(head_phi[j] - head_phi[k]);
			if (dp > PI_F)
				dp = TAU_F - dp;
			if (j != k && dp < 0.1f) {
				together++;
				if (j < k)
					already_drawn = 1;
			}
		}
		/* Render each phase cluster once. Drawing the same merged orb for
		 * every member made additive blending overexpose collisions. */
		if (already_drawn)
			continue;
		grow = dashClampf(1.0f + 0.25f * (together - 1), 1.0f, 1.8f);
		share = together > 1 ? 0.85f : 1.0f;
		dashGlow(head[k].x, head[k].y, 30.0f * sz * grow, 30.0f * sz * grow,
		         colA(glow_c, (int)((56.0f + 46.0f * head[k].d) * share)));
		dashGlow(head[k].x, head[k].y, 11.0f * sz * grow, 11.0f * sz * grow,
		         colA(halo_c, (int)((153.0f + 77.0f * head[k].d) * share)));
		/* One central highlight per cluster prevents stacked white cores. */
		dashGlow(head[k].x, head[k].y, 5.2f * sz * grow, 5.2f * sz * grow,
		         colA(DC(255, 255, 255, 255), together > 1 ? 210 : 255));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
}

/* where the real main menu has them: left of centre, beside the menu */
static OrbStyle menuOrbStyle(DashColor c)
{
	OrbStyle st;

	st.cx = 236.0f;
	st.cy = 214.0f;
	st.rmin = 44.0f;
	st.rmax = 84.0f;
	st.c = c;
	return st;
}

//--------------------------------------------------------------
// Orbit (OSD Minimal): the main menu's orbs on near black
//--------------------------------------------------------------
void dashBgSceneOrbit(float t, int fx)
{
	OrbStyle st = menuOrbStyle(dashTheme.bg1);

	dashRect(0, 0, DASH_VW, DASH_VH, dashTheme.bg2);
	osdOrbs(&st, t, fx);
	dashBgVignette(0.9f);
}

//--------------------------------------------------------------
// Clock ring (System Configuration)
//--------------------------------------------------------------
void dashBgSceneClockRing(float t, int fx)
{
	DashColor rod = dashTheme.bg1, b = dashTheme.bg2;
	float cx = 430.0f, cy = 226.0f;
	float spin, fill;
	int i, held_hour;
	OsdTime ot;
	OrbStyle st;

	osdTime(fx, &ot);
	held_hour = ot.hour;
	spin = -(ot.second / 60.0f) * TAU_F;
	fill = (ot.minute + ot.second / 60.0f) / 60.0f;

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
			DashColor cb = colA(dashColorLighten(rod, 0.18f), 100);
			DashColor tip = colA(cb, 0);
			/* A solid-color segment ended in a sharp rectangular box on the
			 * active clock pillar. Feather the moving fill along its length. */
			dashQuadG(cx + ca * r0 + nx * 0.5f, cy + sa * r0 * 0.9f + ny * 0.5f, cx + ca * r0 - nx * 0.5f, cy + sa * r0 * 0.9f - ny * 0.5f,
			          cx + ca * rf + nx * 0.5f, cy + sa * rf * 0.9f + ny * 0.5f, cx + ca * rf - nx * 0.5f, cy + sa * rf * 0.9f - ny * 0.5f, cb, cb, tip, tip);
		}
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	/* the orbs fly inside the rods, as in System Configuration */
	st.cx = cx;
	st.cy = cy;
	st.rmin = 22.0f;
	st.rmax = 52.0f;
	st.c = DC(80, 160, 255, 255);
	osdOrbs(&st, t, fx);
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
