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
#define SELECT_HALF_W 0.68f

/* Header shapes and action icons use the narrow right margin. Keep the
 * existing atlas allocation and the 56 full-size public icon slots. */
#define APP_PART_SIZE 32
#define APP_PART_SLOT (APP_PART_SIZE + 4)
#define APP_PART_X (ICON_COLS * ICON_SLOT)
#define CLIP_ICON_Y (2 * APP_PART_SLOT)
/* Include every margin icon in the compile-time capacity check. */
typedef char IconAtlasMarginFits[
	APP_PART_X + APP_PART_SLOT <= ICON_TEX_W &&
	CLIP_ICON_Y + (DI_COUNT - DI_CUT) * APP_PART_SLOT <= ICON_TEX_H ? 1 : -1];

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

static float appHexagonSdf(V2 p)
{
	V2 q = {fabsf(p.x), fabsf(p.y)};
	float hex = fmaxf2(q.x * 0.866025f + q.y * 0.5f, q.y) - 0.74f;
	return fabsf(hex) - T;
}

static float appTriangleSdf(V2 p)
{
	return sdTriP(p, -0.26f, 0.2f, 0.0f, -0.32f, 0.26f, 0.2f);
}

/* Pictogrammers MDI rocket-launch, Apache-2.0; see
 * res/icons/rocket-launch.svg and docs/licenses/Material-Design-Icons-LICENSE.txt.
 * SVG contours in their original 24x24 coordinates: C/S curves sampled at
 * t = 1/8 ... 1. The nonzero fill rule preserves the transparent window.
 * Source SHA-256: 9d74afce10d849ebca9831c1005329b8ed7d6e6ce475e662ef612ac5f61de723 */
static float rocketLaunchSdf(V2 p)
{
	static const V2 points[] = {
		{13.130f, 22.190f}, {11.500f, 18.360f}, {12.084f, 18.133f}, {12.659f, 17.889f},
		{13.224f, 17.627f}, {13.779f, 17.349f}, {14.324f, 17.055f}, {14.860f, 16.747f},
		{15.385f, 16.425f}, {15.900f, 16.090f}, {13.130f, 22.190f}, {5.640f, 12.500f},
		{1.810f, 10.870f}, {7.910f, 8.100f}, {7.575f, 8.615f}, {7.253f, 9.140f},
		{6.945f, 9.676f}, {6.651f, 10.221f}, {6.373f, 10.776f}, {6.111f, 11.341f},
		{5.867f, 11.916f}, {5.640f, 12.500f}, {21.610f, 2.390f}, {21.386f, 2.310f},
		{20.748f, 2.147f}, {19.745f, 2.017f}, {18.428f, 2.037f}, {16.844f, 2.322f},
		{15.046f, 2.989f}, {13.081f, 4.153f}, {11.000f, 5.930f}, {10.219f, 6.761f},
		{9.516f, 7.606f}, {8.885f, 8.459f}, {8.322f, 9.315f}, {7.822f, 10.167f},
		{7.380f, 11.009f}, {6.991f, 11.836f}, {6.650f, 12.640f}, {6.567f, 12.924f},
		{6.526f, 13.210f}, {6.527f, 13.496f}, {6.569f, 13.776f}, {6.649f, 14.048f},
		{6.767f, 14.306f}, {6.921f, 14.548f}, {7.110f, 14.770f}, {9.240f, 16.890f},
		{9.458f, 17.082f}, {9.698f, 17.238f}, {9.955f, 17.355f}, {10.225f, 17.435f},
		{10.505f, 17.475f}, {10.790f, 17.475f}, {11.076f, 17.434f}, {11.360f, 17.350f},
		{12.173f, 17.017f}, {13.003f, 16.632f}, {13.846f, 16.191f}, {14.696f, 15.689f},
		{15.549f, 15.123f}, {16.399f, 14.488f}, {17.241f, 13.782f}, {18.070f, 13.000f},
		{19.847f, 10.919f}, {21.011f, 8.954f}, {21.677f, 7.156f}, {21.962f, 5.572f},
		{21.982f, 4.255f}, {21.853f, 3.252f}, {21.690f, 2.614f}, {21.610f, 2.390f},
		{14.540f, 9.460f}, {14.284f, 9.146f}, {14.101f, 8.798f}, {13.992f, 8.427f},
		{13.955f, 8.045f}, {13.992f, 7.663f}, {14.101f, 7.292f}, {14.284f, 6.944f},
		{14.540f, 6.630f}, {14.854f, 6.374f}, {15.202f, 6.191f}, {15.573f, 6.082f},
		{15.955f, 6.045f}, {16.337f, 6.082f}, {16.708f, 6.191f}, {17.056f, 6.374f},
		{17.370f, 6.630f}, {17.623f, 6.944f}, {17.805f, 7.292f}, {17.914f, 7.663f},
		{17.951f, 8.045f}, {17.916f, 8.427f}, {17.807f, 8.798f}, {17.626f, 9.146f},
		{17.370f, 9.460f}, {17.056f, 9.716f}, {16.708f, 9.899f}, {16.337f, 10.008f},
		{15.955f, 10.045f}, {15.573f, 10.008f}, {15.202f, 9.899f}, {14.854f, 9.716f},
		{14.540f, 9.460f}, {6.240f, 22.000f}, {9.880f, 18.360f}, {9.753f, 18.323f},
		{9.627f, 18.281f}, {9.503f, 18.233f}, {9.380f, 18.180f}, {9.259f, 18.121f},
		{9.140f, 18.056f}, {9.024f, 17.986f}, {8.910f, 17.910f}, {4.830f, 22.000f},
		{6.240f, 22.000f}, {2.000f, 22.000f}, {3.410f, 22.000f}, {8.180f, 17.240f},
		{6.760f, 15.830f}, {2.000f, 20.590f}, {2.000f, 22.000f}, {2.000f, 19.170f},
		{6.090f, 15.090f}, {6.014f, 14.977f}, {5.944f, 14.861f}, {5.879f, 14.744f},
		{5.820f, 14.624f}, {5.767f, 14.502f}, {5.719f, 14.377f}, {5.677f, 14.250f},
		{5.640f, 14.120f}, {2.000f, 17.760f}, {2.000f, 19.170f},
	};
	static const unsigned short ends[] = {11, 22, 72, 105, 117, 123, 135};
	float nearest = 1e9f;
	int contour, start = 0, winding = 0;

	p.x = p.x * 12.0f + 12.0f;
	p.y = p.y * 12.0f + 12.0f;
	for (contour = 0; contour < (int)(sizeof(ends) / sizeof(ends[0])); contour++) {
		int i, end = ends[contour];
		V2 a = points[end - 1];

		for (i = start; i < end; i++) {
			V2 b = points[i];
			float ex = b.x - a.x, ey = b.y - a.y;
			float px = p.x - a.x, py = p.y - a.y;
			float length_sq = ex * ex + ey * ey;
			float h = length_sq > 0.0f ? dashClampf((px * ex + py * ey) / length_sq, 0.0f, 1.0f) : 0.0f;
			float dx = px - h * ex, dy = py - h * ey;
			float cross = ex * py - ey * px;

			nearest = fminf2(nearest, dx * dx + dy * dy);
			if (a.y <= p.y && b.y > p.y && cross > 0.0f)
				winding++;
			else if (a.y > p.y && b.y <= p.y && cross < 0.0f)
				winding--;
			a = b;
		}
		start = end;
	}
	return (winding ? -1.0f : 1.0f) * sqrtf(nearest) / 12.0f;
}

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
			d = U(appHexagonSdf(p), appTriangleSdf(p));
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
		case DI_NEW_DOCUMENT:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.56f, 0.78f, 0.08f, T);
			d = U(d, sdSeg(p, -0.3f, -0.36f, 0.3f, -0.36f, 0.05f));
			d = U(d, sdSeg(p, -0.3f, -0.06f, 0.3f, -0.06f, 0.05f));
			d = U(d, sdSeg(p, -0.3f, 0.24f, 0.14f, 0.24f, 0.05f));
			if (id == DI_NEW_DOCUMENT) {
				/* Clear the corner behind the badge so its plus stays distinct. */
				d = S(d, sdBox(p, 0.60f, -0.60f, 0.40f, 0.40f, 0.08f));
				d = U(d, sdSeg(p, 0.36f, -0.60f, 0.84f, -0.60f, 0.085f));
				d = U(d, sdSeg(p, 0.60f, -0.84f, 0.60f, -0.36f, 0.085f));
			}
			break;
		case DI_KEY_TAB:
			{
				/* Mirror one arm about the shaft to keep both sides identical. */
				V2 q = {p.x, fabsf(p.y)};
				const float stroke = 0.75f / 12.0f;

				d = sdSeg(p, -10.0f / 12.0f, 0.0f, 6.0f / 12.0f, 0.0f, stroke);
				d = U(d, sdSeg(q, 2.0f / 12.0f, 4.0f / 12.0f, 6.0f / 12.0f, 0.0f, stroke));
				d = U(d, sdSeg(p, 10.0f / 12.0f, -6.0f / 12.0f, 10.0f / 12.0f, 6.0f / 12.0f, stroke));
			}
			break;
		case DI_KEY_RETURN:
			{
				/* Put the arrowhead on the atlas centerline for mirrored filtering.
				 * The keyboard positions this axis two pixels below the key center. */
				V2 q = {p.x, fabsf(p.y)};
				const float stroke = 0.75f / 12.0f;

				d = sdSeg(p, 9.0f / 12.0f, -8.0f / 12.0f, 9.0f / 12.0f, 0.0f, stroke);
				d = U(d, sdSeg(p, 9.0f / 12.0f, 0.0f, -9.0f / 12.0f, 0.0f, stroke));
				d = U(d, sdSeg(q, -4.0f / 12.0f, 5.0f / 12.0f, -9.0f / 12.0f, 0.0f, stroke));
			}
			break;
		case DI_EDIT:
		case DI_PASTE_RENAME:
			{
				V2 q = rot(p, 0.785398f);
				d = sdBox(q, 0.0f, -0.12f, 0.17f, 0.56f, 0.03f);
				d = U(d, sdTriP(q, -0.17f, 0.5f, 0.17f, 0.5f, 0.0f, 0.84f));
				d = S(d, sdSeg(q, -0.3f, -0.46f, 0.3f, -0.46f, 0.035f));
			}
			if (id == DI_PASTE_RENAME) {
				d = U(d, sdSeg(p, 0.36f, -0.60f, 0.84f, -0.60f, 0.085f));
				d = U(d, sdSeg(p, 0.60f, -0.84f, 0.60f, -0.36f, 0.085f));
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
		case DI_CUT:
			/* Open scissors: hollow handles on the left, blades to the right. */
			d = sdRing(p, -0.54f, -0.40f, 0.25f, T);
			d = U(d, sdRing(p, -0.54f, 0.40f, 0.25f, T));
			d = U(d, sdSeg(p, -0.34f, -0.22f, 0.80f, 0.48f, T));
			d = U(d, sdSeg(p, -0.34f, 0.22f, 0.80f, -0.48f, T));
			break;
		case DI_LAUNCH_ARGS:
			d = rocketLaunchSdf(p);
			break;
		case DI_COPY:
			/* The front rounded square occludes the rear square's inner edges. */
			d = sdBoxOutline(p, -0.20f, -0.20f, 0.52f, 0.52f, 0.14f, T);
			d = S(d, sdBox(p, 0.20f, 0.20f, 0.52f, 0.52f, 0.14f));
			d = U(d, sdBoxOutline(p, 0.20f, 0.20f, 0.52f, 0.52f, 0.14f, T));
			break;
		case DI_CHECK:
		case DI_SELECT_ALL:
		case DI_CLEAR_SELECTION:
			d = sdSeg(p, -0.64f, 0.02f, -0.18f, 0.5f, 0.1f);
			d = U(d, sdSeg(p, -0.18f, 0.5f, 0.68f, -0.46f, 0.1f));
			if (id == DI_SELECT_ALL || id == DI_CLEAR_SELECTION)
				d = U(d, sdSeg(p, -0.84f, -0.60f, -0.36f, -0.60f, 0.085f));
			if (id == DI_SELECT_ALL)
				d = U(d, sdSeg(p, -0.60f, -0.84f, -0.60f, -0.36f, 0.085f));
			break;
		case DI_INVERT_SELECTION:
			{
				float check = U(sdSeg(p, -0.46f, -0.02f, -0.12f, 0.34f, 0.12f),
				                sdSeg(p, -0.12f, 0.34f, 0.50f, -0.34f, 0.12f));
				/* Cut the entire checkmark out of the filled rounded square. */
				d = S(sdBox(p, 0.0f, 0.0f, 0.78f, 0.78f, 0.18f), check);
			}
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

/* Part 0 is the hexagon; part 1 is the inner triangle. Like the main atlas,
 * these are rasterized once with transparent two-texel gutters for filtering. */
static void initHeaderIconParts(u8 *mem)
{
	int part, x, y;

	for (part = 0; part < 2; part++) {
		for (y = 0; y < APP_PART_SIZE; y++) {
			for (x = 0; x < APP_PART_SIZE; x++) {
				V2 p = {(x + 0.5f) / (APP_PART_SIZE * 0.5f) - 1.0f,
				        (y + 0.5f) / (APP_PART_SIZE * 0.5f) - 1.0f};
				float d = part == 0 ? appHexagonSdf(p) : appTriangleSdf(p);
				float a = dashClampf(0.5f - d * (APP_PART_SIZE * 0.5f), 0.0f, 1.0f);
				mem[(part * APP_PART_SLOT + y + 2) * ICON_TEX_W + APP_PART_X + x + 2] = (u8)(a * 255.0f + 0.5f);
			}
		}
	}
}

/* Clipboard/action tiles use the same 32px size and gutters as header parts. */
static void initClipboardIcons(u8 *mem)
{
	int id, x, y;

	for (id = DI_CUT; id < DI_COUNT; id++) {
		int sy = CLIP_ICON_Y + (id - DI_CUT) * APP_PART_SLOT + 2;

		for (y = 0; y < APP_PART_SIZE; y++) {
			for (x = 0; x < APP_PART_SIZE; x++) {
				V2 p = {(x + 0.5f) / (APP_PART_SIZE * 0.5f) - 1.0f,
				        (y + 0.5f) / (APP_PART_SIZE * 0.5f) - 1.0f};
				float a = dashClampf(0.5f - iconSdf(id, p) * (APP_PART_SIZE * 0.5f), 0.0f, 1.0f);

				mem[(sy + y) * ICON_TEX_W + APP_PART_X + x + 2] = (u8)(a * 255.0f + 0.5f);
			}
		}
	}
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

	for (id = 0; id < DI_CUT; id++) {
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

	initHeaderIconParts(mem);
	initClipboardIcons(mem);

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
	float u0, v0, pad, slot;
	int keyboard_symbol;

	if (!icons_ready || id < 0 || id >= DI_COUNT || size <= 0.0f || c.a == 0)
		return;
	keyboard_symbol = id == DI_KEY_TAB || id == DI_KEY_RETURN;
	if ((id < DI_PAD_BADGE || id > DI_BTN_R3) && !keyboard_symbol)
		c = dashIconTint(c);
	slot = id >= DI_CUT ? APP_PART_SLOT : ICON_SLOT;
	u0 = id >= DI_CUT ? APP_PART_X : (float)((id % ICON_COLS) * ICON_SLOT);
	v0 = id >= DI_CUT ? CLIP_ICON_Y + (id - DI_CUT) * APP_PART_SLOT : (float)((id / ICON_COLS) * ICON_SLOT);
	pad = size * 2.0f / (id >= DI_CUT ? APP_PART_SIZE : ICON_SIZE);
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
		dashSpriteBatchAddAligned(x + go, y + go, u0, v0, x + go + g, y + go + g, u0 + ICON_SLOT, v0 + ICON_SLOT);
		dashSpriteBatchEnd();
		return;
	}
	dashSpriteBatchBegin(&icon_tex, c);
	if (keyboard_symbol)
		dashSpriteBatchAddAligned(x - pad, y - pad, u0, v0, x + size + pad, y + size + pad, u0 + slot, v0 + slot);
	else
		dashSpriteBatchAdd(x - pad, y - pad, u0, v0, x + size + pad, y + size + pad, u0 + slot, v0 + slot);
	dashSpriteBatchEnd();
}
/* Rotate a filtered atlas tile in virtual coordinates, before PAL scaling. */
static void drawHeaderIconPart(int part, float cx, float cy, float size, float cs, float sn, DashColor c)
{
	float half = size * 0.5f + size * 2.0f / APP_PART_SIZE;
	float rx = half * cs, ry = half * sn;
	float dx = -ry, dy = rx;
	float u = APP_PART_X, v = part * APP_PART_SLOT;

	dashTexDrawQuadUV(&icon_tex,
	                  cx - rx - dx, cy - ry - dy, u, v,
	                  cx + rx - dx, cy + ry - dy, u + APP_PART_SLOT, v,
	                  cx - rx + dx, cy - ry + dy, u, v + APP_PART_SLOT,
	                  cx + rx + dx, cy + ry + dy, u + APP_PART_SLOT, v + APP_PART_SLOT, c);
}

void dashHeaderIcon(float cx, float cy, float size, DashColor c)
{
	float angle, cs, sn;

	if (!icons_ready || size <= 0.0f || c.a == 0)
		return;
	/* One revolution every 30 seconds, independent of frame rate and animation
	 * settings. Reduce the integer clock first to avoid long-session drift. */
	angle = (float)(Timer() % 30000) * (6.28318530718f / 30000.0f);
	cs = cosf(angle);
	sn = sinf(angle);
	c = dashIconTint(c);
	/* Screen Y grows downward: negative sine turns counterclockwise. */
	drawHeaderIconPart(0, cx, cy, size, cs, -sn, c);
	drawHeaderIconPart(1, cx, cy, size, cs, sn, c);
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
	float u0, v0, pad, slot;
	int i;

	if (!icons_ready || id < 0 || id >= DI_COUNT || size <= 0.0f || c.a == 0 || n <= 0)
		return;
	slot = id >= DI_CUT ? APP_PART_SLOT : ICON_SLOT;
	u0 = id >= DI_CUT ? APP_PART_X : (float)((id % ICON_COLS) * ICON_SLOT);
	v0 = id >= DI_CUT ? CLIP_ICON_Y + (id - DI_CUT) * APP_PART_SLOT : (float)((id / ICON_COLS) * ICON_SLOT);
	pad = size * 2.0f / (id >= DI_CUT ? APP_PART_SIZE : ICON_SIZE);
	dashSpriteBatchBegin(&icon_tex, c);
	for (i = 0; i < n; i++)
		dashSpriteBatchAdd(xy[i * 2] - pad, xy[i * 2 + 1] - pad, u0, v0,
		                   xy[i * 2] + size + pad, xy[i * 2 + 1] + size + pad, u0 + slot, v0 + slot);
	dashSpriteBatchEnd();
}
//--------------------------------------------------------------
//End of file: dash_icons.c
//--------------------------------------------------------------
