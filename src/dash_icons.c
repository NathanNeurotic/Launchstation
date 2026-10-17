//--------------------------------------------------------------
//File name:   dash_icons.c
//Dashboard icon set, rasterized once from signed distance functions into
//one 512x544 PSMT8 alpha atlas (64x64 icons, tinted by vertex colour).
//The pad glyphs use the same theme-aware SDF atlas as other icons.
//No additional CT32 pad tile atlas or PNG decoding is needed at startup.
//--------------------------------------------------------------
#include "dash_internal.h"

#define ICON_SIZE 64
#define ICON_SLOT 68
#define ICON_COLS 7
#define ICON_TEX_W 512
#define ICON_TEX_H 544

static GSTEXTURE icon_tex;
static u32 icon_clut[256] __attribute__((aligned(64)));
static int icons_ready;

typedef struct
{
	float x, y;
} V2;

static inline float len2(float x, float y)
{
	return sqrtf(x * x + y * y);
}

static inline float fmaxf2(float a, float b)
{
	return a > b ? a : b;
}

static inline float fminf2(float a, float b)
{
	return a < b ? a : b;
}

static float sdCircle(V2 p, float cx, float cy, float r)
{
	return len2(p.x - cx, p.y - cy) - r;
}

static float sdRing(V2 p, float cx, float cy, float r, float th)
{
	return fabsf(len2(p.x - cx, p.y - cy) - r) - th;
}

static float sdBox(V2 p, float cx, float cy, float hx, float hy, float r)
{
	float qx = fabsf(p.x - cx) - (hx - r);
	float qy = fabsf(p.y - cy) - (hy - r);
	float ox = fmaxf2(qx, 0.0f), oy = fmaxf2(qy, 0.0f);

	return len2(ox, oy) + fminf2(fmaxf2(qx, qy), 0.0f) - r;
}

static float sdBoxOutline(V2 p, float cx, float cy, float hx, float hy, float r, float th)
{
	return fabsf(sdBox(p, cx, cy, hx, hy, r)) - th;
}

static float sdSeg(V2 p, float ax, float ay, float bx, float by, float th)
{
	float pax = p.x - ax, pay = p.y - ay;
	float bax = bx - ax, bay = by - ay;
	float h = dashClampf((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0f, 1.0f);

	return len2(pax - bax * h, pay - bay * h) - th;
}

static float sdTri(V2 p, V2 a, V2 b, V2 c)
{
	/* Inigo Quilez' exact triangle distance */
	V2 e0 = {b.x - a.x, b.y - a.y}, e1 = {c.x - b.x, c.y - b.y}, e2 = {a.x - c.x, a.y - c.y};
	V2 v0 = {p.x - a.x, p.y - a.y}, v1 = {p.x - b.x, p.y - b.y}, v2 = {p.x - c.x, p.y - c.y};
	float h0 = dashClampf((v0.x * e0.x + v0.y * e0.y) / (e0.x * e0.x + e0.y * e0.y), 0.0f, 1.0f);
	float h1 = dashClampf((v1.x * e1.x + v1.y * e1.y) / (e1.x * e1.x + e1.y * e1.y), 0.0f, 1.0f);
	float h2 = dashClampf((v2.x * e2.x + v2.y * e2.y) / (e2.x * e2.x + e2.y * e2.y), 0.0f, 1.0f);
	V2 pq0 = {v0.x - e0.x * h0, v0.y - e0.y * h0};
	V2 pq1 = {v1.x - e1.x * h1, v1.y - e1.y * h1};
	V2 pq2 = {v2.x - e2.x * h2, v2.y - e2.y * h2};
	float s = (e0.x * e2.y - e0.y * e2.x) > 0.0f ? 1.0f : -1.0f;
	float d0 = pq0.x * pq0.x + pq0.y * pq0.y, s0 = s * (v0.x * e0.y - v0.y * e0.x);
	float d1 = pq1.x * pq1.x + pq1.y * pq1.y, s1 = s * (v1.x * e1.y - v1.y * e1.x);
	float d2 = pq2.x * pq2.x + pq2.y * pq2.y, s2 = s * (v2.x * e2.y - v2.y * e2.x);
	float d = fminf2(fminf2(d0, d1), d2);
	float sg = fminf2(fminf2(s0, s1), s2);

	return -sqrtf(d) * (sg > 0.0f ? 1.0f : (sg < 0.0f ? -1.0f : 0.0f));
}

static float sdTriP(V2 p, float ax, float ay, float bx, float by, float cx, float cy)
{
	V2 a = {ax, ay}, b = {bx, by}, c = {cx, cy};
	return sdTri(p, a, b, c);
}

static V2 rot(V2 p, float ang)
{
	V2 r;
	float c = cosf(ang), s = sinf(ang);

	r.x = p.x * c - p.y * s;
	r.y = p.x * s + p.y * c;
	return r;
}

static float sdStar5(V2 p, float r, float rf)
{
	/* Inigo Quilez' 5-point star (y axis flipped so the star points up) */
	const float k1x = 0.809016994f, k1y = -0.587785252f;
	const float k2x = -0.809016994f, k2y = -0.587785252f;
	float d, bax, bay, h;

	p.y = -p.y;
	p.x = fabsf(p.x);
	d = fmaxf2(k1x * p.x + k1y * p.y, 0.0f);
	p.x -= 2.0f * d * k1x;
	p.y -= 2.0f * d * k1y;
	d = fmaxf2(k2x * p.x + k2y * p.y, 0.0f);
	p.x -= 2.0f * d * k2x;
	p.y -= 2.0f * d * k2y;
	p.x = fabsf(p.x);
	p.y -= r;
	bax = rf * -k1y;
	bay = rf * k1x - 1.0f;
	h = dashClampf((p.x * bax + p.y * bay) / (bax * bax + bay * bay), 0.0f, r);
	return len2(p.x - bax * h, p.y - bay * h) * ((p.y * bax - p.x * bay) > 0.0f ? 1.0f : -1.0f);
}

static float U(float a, float b)
{
	return fminf2(a, b);
}

static float S(float a, float b) /* a minus b */
{
	return fmaxf2(a, -b);
}

#define T 0.075f /* standard stroke half-width */

static float iconSdf(int id, V2 p)
{
	float d = 1e9f;
	int k;

	switch (id) {
		case DI_HOME:
			d = sdTriP(p, -0.82f, -0.02f, 0.0f, -0.80f, 0.82f, -0.02f);
			d = U(d, sdBox(p, 0.0f, 0.36f, 0.56f, 0.42f, 0.04f));
			d = S(d, sdBox(p, 0.0f, 0.52f, 0.15f, 0.28f, 0.03f));
			break;
		case DI_APPS:
			for (k = 0; k < 4; k++)
				d = U(d, sdBox(p, (k & 1) ? 0.4f : -0.4f, (k & 2) ? 0.4f : -0.4f, 0.3f, 0.3f, 0.1f));
			break;
		case DI_FOLDER:
			d = sdBox(p, 0.0f, 0.14f, 0.82f, 0.56f, 0.1f);
			d = U(d, sdBox(p, -0.42f, -0.46f, 0.38f, 0.16f, 0.08f));
			d = S(d, sdBox(p, 0.0f, -0.24f, 0.72f, 0.04f, 0.0f));
			break;
		case DI_DEVICES:
			d = sdBoxOutline(p, 0.0f, -0.36f, 0.78f, 0.28f, 0.1f, T);
			d = U(d, sdBoxOutline(p, 0.0f, 0.36f, 0.78f, 0.28f, 0.1f, T));
			d = U(d, sdCircle(p, 0.5f, -0.36f, 0.08f));
			d = U(d, sdCircle(p, 0.5f, 0.36f, 0.08f));
			d = U(d, sdSeg(p, -0.52f, -0.36f, 0.18f, -0.36f, 0.05f));
			d = U(d, sdSeg(p, -0.52f, 0.36f, 0.18f, 0.36f, 0.05f));
			break;
		case DI_TOOLS:
			d = sdSeg(p, -0.62f, 0.62f, 0.12f, -0.12f, 0.13f);
			d = U(d, sdCircle(p, 0.32f, -0.32f, 0.36f));
			d = S(d, sdCircle(p, 0.52f, -0.52f, 0.24f));
			d = S(d, sdCircle(p, 0.32f, -0.32f, 0.16f));
			break;
		case DI_SETTINGS:
			d = sdCircle(p, 0.0f, 0.0f, 0.56f);
			for (k = 0; k < 8; k++) {
				V2 q = rot(p, -k * 0.7853982f);
				d = U(d, sdBox(q, 0.0f, -0.70f, 0.14f, 0.2f, 0.04f));
			}
			d = S(d, sdCircle(p, 0.0f, 0.0f, 0.24f));
			break;
		case DI_MC:
			d = sdBoxOutline(p, 0.0f, 0.02f, 0.52f, 0.78f, 0.08f, T);
			d = U(d, sdBoxOutline(p, 0.0f, 0.3f, 0.3f, 0.26f, 0.04f, 0.05f));
			d = U(d, sdTriP(p, -0.14f, -0.5f, 0.0f, -0.62f, 0.14f, -0.5f));
			break;
		case DI_USB:
			d = sdSeg(p, 0.0f, 0.62f, 0.0f, -0.52f, T);
			d = U(d, sdTriP(p, -0.18f, -0.46f, 0.0f, -0.82f, 0.18f, -0.46f));
			d = U(d, sdSeg(p, 0.0f, 0.28f, -0.42f, 0.0f, T));
			d = U(d, sdSeg(p, -0.42f, 0.0f, -0.42f, -0.22f, T));
			d = U(d, sdCircle(p, -0.42f, -0.3f, 0.16f));
			d = U(d, sdSeg(p, 0.0f, 0.12f, 0.42f, -0.14f, T));
			d = U(d, sdSeg(p, 0.42f, -0.14f, 0.42f, -0.3f, T));
			d = U(d, sdBox(p, 0.42f, -0.38f, 0.11f, 0.11f, 0.0f));
			d = U(d, sdCircle(p, 0.0f, 0.72f, 0.15f));
			break;
		case DI_HDD:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.8f, 0.56f, 0.12f, T);
			d = U(d, sdCircle(p, 0.48f, 0.22f, 0.09f));
			d = U(d, sdSeg(p, -0.54f, 0.22f, 0.18f, 0.22f, 0.05f));
			d = U(d, sdSeg(p, -0.54f, -0.18f, 0.54f, -0.18f, 0.03f));
			break;
		case DI_DISC:
			d = sdRing(p, 0.0f, 0.0f, 0.78f, T);
			d = U(d, sdRing(p, 0.0f, 0.0f, 0.2f, T));
			{
				float a = len2(p.x, p.y);
				if (p.x > 0.0f && p.y < 0.0f && a > 0.36f && a < 0.62f)
					d = U(d, fabsf(a - 0.49f) - 0.04f);
			}
			break;
		case DI_NETWORK:
			d = sdRing(p, 0.0f, 0.0f, 0.78f, T);
			{
				V2 q = {p.x * 2.1f, p.y};
				d = U(d, (fabsf(len2(q.x, q.y) - 0.78f) - T * 1.6f) * 0.5f);
			}
			d = U(d, sdSeg(p, -0.78f, 0.0f, 0.78f, 0.0f, 0.05f));
			d = U(d, sdSeg(p, -0.64f, -0.4f, 0.64f, -0.4f, 0.045f));
			d = U(d, sdSeg(p, -0.64f, 0.4f, 0.64f, 0.4f, 0.045f));
			d = fmaxf2(d, len2(p.x, p.y) - 0.86f);
			break;
		case DI_STAR:
			d = sdStar5(p, 0.86f, 0.44f) - 0.04f;
			break;
		case DI_CLOCK:
			d = sdRing(p, 0.0f, 0.0f, 0.76f, T);
			d = U(d, sdSeg(p, 0.0f, 0.0f, 0.0f, -0.48f, 0.07f));
			d = U(d, sdSeg(p, 0.0f, 0.0f, 0.34f, 0.14f, 0.07f));
			break;
		case DI_APP:
			/* hexagonal crystal */
			{
				V2 q = {fabsf(p.x), fabsf(p.y)};
				float hex = fmaxf2(q.x * 0.866025f + q.y * 0.5f, q.y) - 0.74f;
				d = fabsf(hex) - T;
				d = U(d, sdTriP(p, -0.26f, 0.2f, 0.0f, -0.32f, 0.26f, 0.2f));
			}
			break;
		case DI_PLAY:
			d = sdTriP(p, -0.42f, -0.62f, -0.42f, 0.62f, 0.66f, 0.0f) - 0.04f;
			break;
		case DI_INFO:
			d = sdRing(p, 0.0f, 0.0f, 0.78f, T);
			d = U(d, sdCircle(p, 0.0f, -0.38f, 0.1f));
			d = U(d, sdSeg(p, 0.0f, -0.08f, 0.0f, 0.44f, 0.09f));
			break;
		case DI_WARNING:
			d = fabsf(sdTriP(p, -0.82f, 0.68f, 0.0f, -0.78f, 0.82f, 0.68f) + 0.04f) - T;
			d = U(d, sdSeg(p, 0.0f, -0.32f, 0.0f, 0.18f, 0.08f));
			d = U(d, sdCircle(p, 0.0f, 0.42f, 0.09f));
			break;
		case DI_POWER:
			d = sdRing(p, 0.0f, 0.06f, 0.62f, T);
			d = S(d, sdTriP(p, -0.44f, -0.9f, 0.0f, 0.06f, 0.44f, -0.9f));
			d = U(d, sdSeg(p, 0.0f, -0.82f, 0.0f, -0.08f, 0.08f));
			break;
		case DI_TEXT:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.56f, 0.78f, 0.08f, T);
			d = U(d, sdSeg(p, -0.3f, -0.36f, 0.3f, -0.36f, 0.05f));
			d = U(d, sdSeg(p, -0.3f, -0.06f, 0.3f, -0.06f, 0.05f));
			d = U(d, sdSeg(p, -0.3f, 0.24f, 0.14f, 0.24f, 0.05f));
			break;
		case DI_EDIT:
			{
				V2 q = rot(p, 0.785398f);
				d = sdBox(q, 0.0f, -0.12f, 0.17f, 0.56f, 0.03f);
				d = U(d, sdTriP(q, -0.17f, 0.5f, 0.17f, 0.5f, 0.0f, 0.84f));
				d = S(d, sdSeg(q, -0.3f, -0.46f, 0.3f, -0.46f, 0.035f));
			}
			break;
		case DI_REFRESH:
			d = sdRing(p, 0.0f, 0.0f, 0.62f, T);
			d = S(d, sdBox(p, 0.4f, -0.4f, 0.4f, 0.4f, 0.0f));
			d = U(d, sdTriP(p, 0.18f, -0.9f, 0.18f, -0.34f, 0.62f, -0.62f));
			break;
		case DI_PIN:
			d = sdCircle(p, 0.0f, -0.36f, 0.34f);
			d = U(d, sdTriP(p, -0.28f, -0.14f, 0.28f, -0.14f, 0.0f, 0.84f) + 0.02f);
			d = S(d, sdCircle(p, -0.1f, -0.46f, 0.1f));
			break;
		case DI_PLUS:
			d = sdSeg(p, -0.66f, 0.0f, 0.66f, 0.0f, 0.1f);
			d = U(d, sdSeg(p, 0.0f, -0.66f, 0.0f, 0.66f, 0.1f));
			break;
		case DI_CHECK:
			d = sdSeg(p, -0.64f, 0.02f, -0.18f, 0.5f, 0.1f);
			d = U(d, sdSeg(p, -0.18f, 0.5f, 0.68f, -0.46f, 0.1f));
			break;
		case DI_CLOSE:
			d = sdSeg(p, -0.56f, -0.56f, 0.56f, 0.56f, 0.1f);
			d = U(d, sdSeg(p, -0.56f, 0.56f, 0.56f, -0.56f, 0.1f));
			break;
		case DI_SDCARD:
			d = sdBox(p, 0.0f, 0.04f, 0.54f, 0.78f, 0.08f);
			d = S(d, sdTriP(p, 0.2f, -1.0f, 0.7f, -1.0f, 0.7f, -0.44f));
			d = fabsf(d) - T;
			for (k = 0; k < 3; k++)
				d = U(d, sdSeg(p, -0.3f + k * 0.2f, -0.56f, -0.3f + k * 0.2f, -0.3f, 0.045f));
			break;
		case DI_CHIP:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.5f, 0.5f, 0.08f, T);
			d = U(d, sdBox(p, 0.0f, 0.0f, 0.22f, 0.22f, 0.03f));
			for (k = 0; k < 3; k++) {
				float o = -0.28f + k * 0.28f;
				d = U(d, sdSeg(p, o, -0.58f, o, -0.82f, 0.05f));
				d = U(d, sdSeg(p, o, 0.58f, o, 0.82f, 0.05f));
				d = U(d, sdSeg(p, -0.58f, o, -0.82f, o, 0.05f));
				d = U(d, sdSeg(p, 0.58f, o, 0.82f, o, 0.05f));
			}
			break;
		case DI_VIEW_GRID:
			for (k = 0; k < 9; k++)
				d = U(d, sdBox(p, -0.56f + (k % 3) * 0.56f, -0.56f + (k / 3) * 0.56f, 0.2f, 0.2f, 0.05f));
			break;
		case DI_VIEW_LIST:
			for (k = 0; k < 3; k++) {
				d = U(d, sdBox(p, -0.6f, -0.56f + k * 0.56f, 0.18f, 0.18f, 0.04f));
				d = U(d, sdSeg(p, -0.24f, -0.56f + k * 0.56f, 0.76f, -0.56f + k * 0.56f, 0.08f));
			}
			break;
		case DI_VIEW_CAROUSEL:
			d = sdBox(p, 0.0f, 0.0f, 0.36f, 0.56f, 0.06f);
			d = U(d, sdBox(p, -0.72f, 0.0f, 0.14f, 0.38f, 0.04f));
			d = U(d, sdBox(p, 0.72f, 0.0f, 0.14f, 0.38f, 0.04f));
			break;
		case DI_FILE:
			d = sdBox(p, 0.0f, 0.0f, 0.56f, 0.78f, 0.06f);
			d = S(d, sdTriP(p, 0.12f, -0.9f, 0.7f, -0.9f, 0.7f, -0.32f));
			d = fabsf(d) - T;
			d = U(d, sdSeg(p, 0.18f, -0.74f, 0.18f, -0.34f, 0.05f));
			d = U(d, sdSeg(p, 0.18f, -0.34f, 0.54f, -0.34f, 0.05f));
			break;
		case DI_BACK:
			d = sdSeg(p, -0.6f, 0.0f, 0.66f, 0.0f, 0.09f);
			d = U(d, sdSeg(p, -0.6f, 0.0f, -0.12f, -0.48f, 0.09f));
			d = U(d, sdSeg(p, -0.6f, 0.0f, -0.12f, 0.48f, 0.09f));
			break;
		case DI_PAD_BADGE:
			d = sdBox(p, 0.0f, 0.0f, 0.92f, 0.92f, 0.20f);
			break;
		case DI_BTN_CROSS:
			d = sdSeg(p, -0.52f, -0.52f, 0.52f, 0.52f, 0.17f);
			d = U(d, sdSeg(p, -0.52f, 0.52f, 0.52f, -0.52f, 0.17f));
			break;
		case DI_BTN_CIRCLE:
			d = sdRing(p, 0.0f, 0.0f, 0.58f, 0.16f);
			break;
		case DI_BTN_SQUARE:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.58f, 0.58f, 0.03f, 0.15f);
			break;
		case DI_BTN_TRIANGLE:
			d = fabsf(sdTriP(p, -0.7f, 0.5f, 0.0f, -0.72f, 0.7f, 0.5f) + 0.04f) - 0.15f;
			break;
		case DI_BTN_UP:
			d = U(sdSeg(p, -0.5f, -0.22f, 0.0f, 0.34f, 0.16f),
			    sdSeg(p, 0.0f, 0.34f, 0.5f, -0.22f, 0.16f));
			break;
		case DI_BTN_DOWN:
			d = U(sdSeg(p, -0.5f, 0.22f, 0.0f, -0.34f, 0.16f),
			    sdSeg(p, 0.0f, -0.34f, 0.5f, 0.22f, 0.16f));
			break;
		case DI_BTN_LEFT:
			d = U(sdSeg(p, 0.22f, -0.5f, -0.34f, 0.0f, 0.16f),
			    sdSeg(p, -0.34f, 0.0f, 0.22f, 0.5f, 0.16f));
			break;
		case DI_BTN_RIGHT:
			d = U(sdSeg(p, -0.22f, -0.5f, 0.34f, 0.0f, 0.16f),
			    sdSeg(p, 0.34f, 0.0f, -0.22f, 0.5f, 0.16f));
			break;
		case DI_BTN_START:
			d = sdTriP(p, -0.36f, -0.44f, -0.36f, 0.44f, 0.44f, 0.0f);
			break;
		case DI_BTN_SELECT:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.58f, 0.38f, 0.17f, 0.1f);
			break;
		case DI_BTN_L1:
		case DI_BTN_L2:
		case DI_BTN_L3:
		case DI_BTN_R1:
		case DI_BTN_R2:
		case DI_BTN_R3: {
			/* The baked PNG atlas used to supply L/R trigger labels. Draw
			 * both letters and numbers directly as SDF strokes instead, so
			 * quick-launch and hint glyphs remain visible without the PNGs. */
			int right = (id >= DI_BTN_R1);
			int digit = right ? id - DI_BTN_R1 + 1 : id - DI_BTN_L1 + 1;
			const float t = 0.13f;
			d = 1000.0f; /* one outer keycap; no thin nested shoulder border */
			if (!right) {
				d = U(d, sdSeg(p, -0.60f, -0.52f, -0.60f, 0.48f, t));
				d = U(d, sdSeg(p, -0.60f, 0.48f, -0.12f, 0.48f, t));
			} else {
				d = U(d, sdSeg(p, -0.65f, -0.52f, -0.65f, 0.48f, t));
				d = U(d, sdSeg(p, -0.65f, -0.52f, -0.25f, -0.52f, t));
				d = U(d, sdSeg(p, -0.25f, -0.52f, -0.25f, -0.06f, t));
				d = U(d, sdSeg(p, -0.65f, -0.06f, -0.25f, -0.06f, t));
				d = U(d, sdSeg(p, -0.47f, -0.06f, -0.16f, 0.48f, t));
			}
			if (digit == 1) {
				d = U(d, sdSeg(p, 0.46f, -0.52f, 0.46f, 0.48f, t));
				d = U(d, sdSeg(p, 0.24f, -0.30f, 0.46f, -0.52f, t));
			} else if (digit == 2) {
				d = U(d, sdSeg(p, 0.10f, -0.52f, 0.70f, -0.52f, t));
				d = U(d, sdSeg(p, 0.70f, -0.52f, 0.70f, -0.12f, t));
				d = U(d, sdSeg(p, 0.70f, -0.12f, 0.10f, 0.48f, t));
				d = U(d, sdSeg(p, 0.10f, 0.48f, 0.70f, 0.48f, t));
			} else {
				d = U(d, sdSeg(p, 0.10f, -0.52f, 0.70f, -0.52f, t));
				d = U(d, sdSeg(p, 0.12f, -0.02f, 0.70f, -0.02f, t));
				d = U(d, sdSeg(p, 0.10f, 0.48f, 0.70f, 0.48f, t));
				d = U(d, sdSeg(p, 0.70f, -0.52f, 0.70f, 0.48f, t));
			}
			break;
		}
		case DI_KEY:
			d = sdRing(p, -0.4f, 0.0f, 0.3f, T);
			d = U(d, sdSeg(p, -0.1f, 0.0f, 0.78f, 0.0f, T));
			d = U(d, sdSeg(p, 0.5f, 0.0f, 0.5f, 0.24f, T));
			d = U(d, sdSeg(p, 0.7f, 0.0f, 0.7f, 0.3f, T));
			break;
		case DI_GAMEPAD:
			d = sdBox(p, 0.0f, -0.08f, 0.62f, 0.3f, 0.26f);
			d = U(d, sdCircle(p, -0.5f, 0.24f, 0.28f));
			d = U(d, sdCircle(p, 0.5f, 0.24f, 0.28f));
			d = S(d, sdSeg(p, -0.62f, -0.06f, -0.3f, -0.06f, 0.055f));
			d = S(d, sdSeg(p, -0.46f, -0.22f, -0.46f, 0.1f, 0.055f));
			d = S(d, sdCircle(p, 0.36f, -0.12f, 0.07f));
			d = S(d, sdCircle(p, 0.56f, 0.02f, 0.07f));
			break;
		case DI_EXEC:
			/* an application window with a disc emblem: the PS2 program
			 * tile used when an application has no artwork */
			d = sdBoxOutline(p, 0.0f, 0.02f, 0.8f, 0.72f, 0.1f, T);
			d = U(d, sdBox(p, 0.0f, -0.55f, 0.8f, 0.19f, 0.08f));
			d = S(d, sdCircle(p, -0.56f, -0.55f, 0.06f));
			d = U(d, sdRing(p, 0.0f, 0.2f, 0.3f, T * 0.9f));
			d = U(d, sdCircle(p, 0.0f, 0.2f, 0.08f));
			break;
		case DI_PALETTE:
			/* three overlapping swatches */
			d = sdBox(p, -0.34f, -0.3f, 0.3f, 0.3f, 0.05f);
			d = U(d, fabsf(sdBox(p, 0.06f, 0.02f, 0.3f, 0.3f, 0.05f)) - T);
			d = U(d, fabsf(sdBox(p, 0.44f, 0.38f, 0.3f, 0.3f, 0.05f)) - T);
			break;
		case DI_RECENT:
			d = sdRing(p, 0.0f, 0.0f, 0.7f, T);
			d = S(d, sdBox(p, -0.62f, -0.5f, 0.2f, 0.26f, 0.0f));
			d = U(d, sdTriP(p, -0.9f, -0.34f, -0.36f, -0.34f, -0.63f, -0.02f));
			d = U(d, sdSeg(p, 0.0f, 0.0f, 0.0f, -0.4f, 0.07f));
			d = U(d, sdSeg(p, 0.0f, 0.0f, 0.3f, 0.2f, 0.07f));
			break;
		case DI_IMAGE:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.82f, 0.62f, 0.08f, T);
			d = U(d, sdCircle(p, 0.36f, -0.24f, 0.13f));
			d = U(d, fmaxf2(sdTriP(p, -0.66f, 0.46f, -0.18f, -0.12f, 0.3f, 0.46f), sdBox(p, 0.0f, 0.0f, 0.74f, 0.54f, 0.0f)));
			d = U(d, fmaxf2(sdTriP(p, 0.0f, 0.46f, 0.36f, 0.1f, 0.72f, 0.46f), sdBox(p, 0.0f, 0.0f, 0.74f, 0.54f, 0.0f)));
			break;
		case DI_DPAD_UD:
			/* one solid D-pad; large arrow cut-outs mark the up/down arms.
			 * No hairline outlines: those flickered on interlaced TVs (#127). */
			d = sdBox(p, 0.0f, 0.0f, 0.92f, 0.3f, 0.1f);
			d = U(d, sdBox(p, 0.0f, 0.0f, 0.3f, 0.92f, 0.1f));
			d = S(d, sdTriP(p, 0.0f, -0.8f, -0.2f, -0.42f, 0.2f, -0.42f));
			d = S(d, sdTriP(p, 0.0f, 0.8f, 0.2f, 0.42f, -0.2f, 0.42f));
			break;
		default:
			d = sdCircle(p, 0.0f, 0.0f, 0.5f);
			break;
	}
	return d;
}

static inline int clutSwizzle(int i)
{
	return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

void dashIconsInit(void)
{
	u8 *mem;
	int id, x, y, i;

	if (icons_ready)
		return;
	mem = (u8 *)memalign(128, ICON_TEX_W * ICON_TEX_H);
	if (mem == NULL)
		return;
	memset(mem, 0, ICON_TEX_W * ICON_TEX_H);

	for (id = 0; id < DI_COUNT; id++) {
		int sx = (id % ICON_COLS) * ICON_SLOT + 2;
		int sy = (id / ICON_COLS) * ICON_SLOT + 2;

		for (y = 0; y < ICON_SIZE; y++) {
			for (x = 0; x < ICON_SIZE; x++) {
				V2 p;
				float d, a;

				p.x = (x + 0.5f) / (ICON_SIZE * 0.5f) - 1.0f;
				p.y = (y + 0.5f) / (ICON_SIZE * 0.5f) - 1.0f;
				d = iconSdf(id, p);
				/* one pixel == 2/64 normalized units */
				a = dashClampf(0.5f - d * (ICON_SIZE * 0.5f), 0.0f, 1.0f);
				if (id == DI_PAD_BADGE)
					a *= 0.86f + 0.14f * (1.0f - p.y) * 0.5f; /* soft top light */
				mem[(sy + y) * ICON_TEX_W + sx + x] = (u8)(a * 255.0f + 0.5f);
			}
		}
	}

	for (i = 0; i < 256; i++) {
		int a = (i * 128 + 127) / 255;
		icon_clut[clutSwizzle(i)] = 0x00808080 | ((u32)a << 24);
	}

	memset(&icon_tex, 0, sizeof(icon_tex));
	icon_tex.Width = ICON_TEX_W;
	icon_tex.Height = ICON_TEX_H;
	icon_tex.PSM = GS_PSM_T8;
	icon_tex.ClutPSM = GS_PSM_CT32;
	icon_tex.ClutStorageMode = GS_CLUT_STORAGE_CSM1;
	icon_tex.Clut = icon_clut;
	icon_tex.Mem = (u32 *)mem;
	icon_tex.Filter = GS_FILTER_LINEAR;
	icon_tex.Delayed = 1;
	gsKit_setup_tbw(&icon_tex);
	SyncDCache(mem, mem + ICON_TEX_W * ICON_TEX_H);
	SyncDCache(icon_clut, (u8 *)icon_clut + sizeof(icon_clut));
	icons_ready = 1;
}

void dashIconsShutdown(void)
{
	dashTexRelease(&icon_tex);
}

GSTEXTURE *dashIconsTexture(void)
{
	return icons_ready ? &icon_tex : NULL;
}

void dashIcon(int id, float x, float y, float size, DashColor c)
{
	float u0, v0, pad;

	if (!icons_ready || id < 0 || id >= DI_COUNT || size <= 0.0f || c.a == 0)
		return;
	if (id < DI_PAD_BADGE || id > DI_BTN_R3)
		c = dashIconTint(c);
	u0 = (float)((id % ICON_COLS) * ICON_SLOT);
	v0 = (float)((id / ICON_COLS) * ICON_SLOT);
	pad = size * 2.0f / ICON_SIZE;
	if ((id > DI_PAD_BADGE && id <= DI_BTN_R3) || id == DI_DPAD_UD) {
		/* Preserve colored controller glyphs while letting each theme decide
		 * whether they sit on a background. Baked PNGs forced a fixed navy
		 * tile into every theme, ignoring icon-backings entirely. */
		int shoulder = id >= DI_BTN_L1 && id <= DI_BTN_R3;
		float g = size * (shoulder ? 0.96f : (dashCfg.icon_backing ? 0.74f : 0.86f)); /* #152: legible on CRTs */
		float go = (size - g) * 0.5f;
		if (dashCfg.icon_backing) {
			float bu0 = (float)((DI_PAD_BADGE % ICON_COLS) * ICON_SLOT);
			float bv0 = (float)((DI_PAD_BADGE / ICON_COLS) * ICON_SLOT);
			/* A fixed neutral keycap remains visible on dark and bright
			 * wallpapers, regardless of transparent panel/theme colors. */
			DashColor badge = DC(18, 22, 30, 230);
			dashSpriteBatchBegin(&icon_tex, badge);
			dashSpriteBatchAdd(x - pad, y - pad, bu0, bv0, x + size + pad, y + size + pad, bu0 + ICON_SLOT, bv0 + ICON_SLOT);
			dashSpriteBatchEnd();
			/* Match the SDF badge's 0.92 half extent and fixed corner radius. */
			dashRoundedBorder(x + size * 0.04f, y + size * 0.04f,
			                  size * 0.92f, size * 0.92f, size * 0.10f,
			                  DC(210, 220, 240, 120));
		}
		dashSpriteBatchBegin(&icon_tex, c);
		dashSpriteBatchAdd(x + go, y + go, u0, v0, x + go + g, y + go + g, u0 + ICON_SLOT, v0 + ICON_SLOT);
		dashSpriteBatchEnd();
		return;
	}
	dashSpriteBatchBegin(&icon_tex, c);
	dashSpriteBatchAdd(x - pad, y - pad, u0, v0, x + size + pad, y + size + pad, u0 + ICON_SLOT, v0 + ICON_SLOT);
	dashSpriteBatchEnd();
}
/* Theme icon tint: TEXT keeps the caller's colour, ACCENT leans it towards
 * the accent colour (keeping its brightness), MONO removes the colour. */
DashColor dashIconTint(DashColor c)
{
	int l = (c.r * 77 + c.g * 150 + c.b * 29) >> 8;

	switch (dashTheme.icon_tint) {
		case DASH_ICONS_ACCENT: {
			DashColor a = DASH_C_ACCENT;
			int la = (a.r * 77 + a.g * 150 + a.b * 29) >> 8;
			float k = (la > 0) ? l / (float)la : 1.0f;
			DashColor r = dashColorScale(a, k > 1.3f ? 1.3f : k);
			r.a = c.a;
			return dashColorMix(c, r, 0.7f);
		}
		case DASH_ICONS_MONO:
			return DC(l, l, l, c.a);
		default:
			return c;
	}
}

/* Many copies of one icon in a single sprite batch (background motifs). */
void dashIconMany(int id, const float *xy, int n, float size, DashColor c)
{
	float u0, v0, pad;
	int i;

	if (!icons_ready || id < 0 || id >= DI_COUNT || size <= 0.0f || c.a == 0 || n <= 0)
		return;
	u0 = (float)((id % ICON_COLS) * ICON_SLOT);
	v0 = (float)((id / ICON_COLS) * ICON_SLOT);
	pad = size * 2.0f / ICON_SIZE;
	dashSpriteBatchBegin(&icon_tex, c);
	for (i = 0; i < n; i++)
		dashSpriteBatchAdd(xy[i * 2] - pad, xy[i * 2 + 1] - pad, u0, v0,
		                   xy[i * 2] + size + pad, xy[i * 2 + 1] + size + pad, u0 + ICON_SLOT, v0 + ICON_SLOT);
	dashSpriteBatchEnd();
}
//--------------------------------------------------------------
//End of file: dash_icons.c
//--------------------------------------------------------------
