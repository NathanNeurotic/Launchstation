//--------------------------------------------------------------
//File name:   dash_gfx.c
//Dashboard renderer: GS state, alpha/offset stacks, gradients, rounded
//panels, glows and batched textured sprites.
//
//Rendering model: gsKit ONESHOT queue, double-buffered for the animated
//dashboard to prevent rendering into the displayed frame. All
//dashboard textures are owned by gsKit's texture manager (inline uploads
//inside the draw queue, LRU eviction), the same VRAM strategy LUNA/OPL use.
//Procedural helper textures are tiny (32x32/64x64 CT32).
//--------------------------------------------------------------
#include "dash_internal.h"
#include <gsInline.h>
#include <delaythread.h>

/* Built-in fallback palette; dashThemeApply() overwrites it. */
DashColor DASH_C_TEXT = {0xF2, 0xF4, 0xFA, 0xFF};
DashColor DASH_C_TEXT2 = {0xAE, 0xB6, 0xCC, 0xFF};
DashColor DASH_C_TEXT3 = {0x70, 0x7A, 0x96, 0xFF};
DashColor DASH_C_ACCENT = {0x7F, 0xD8, 0xFF, 0xFF};
DashColor DASH_C_ACCENT2 = {0x9C, 0x8C, 0xFF, 0xFF};
DashColor DASH_C_WARM = {0xFF, 0xBE, 0x5C, 0xFF};
DashColor DASH_C_DANGER = {0xFF, 0x62, 0x70, 0xFF};
DashColor DASH_C_OK = {0x4E, 0xE0, 0xA8, 0xFF};
DashColor DASH_C_PANEL = {0x0C, 0x0F, 0x1E, 0x96};
DashColor DASH_C_PANEL_HI = {0x24, 0x2C, 0x48, 0xD8};
DashColor DASH_C_SHADOW = {0x00, 0x00, 0x04, 0xA0};
DashColor DASH_C_SELECT = {0x3C, 0x78, 0xD2, 0xAA};
DashColor DASH_C_SELEDGE = {0xAA, 0xE1, 0xFF, 0x82};
DashColor DASH_C_SELTEXT = {0xF2, 0xF4, 0xFA, 0xFF};
DashColor DASH_C_BORDER = {0xFF, 0xFF, 0xFF, 0x00};

#define DASH_ALPHA_STACK 12
#define DASH_OFFSET_STACK 8

static float alpha_stack[DASH_ALPHA_STACK];
static int alpha_top;
static float g_alpha = 1.0f;
static float off_x_stack[DASH_OFFSET_STACK], off_y_stack[DASH_OFFSET_STACK];
static int off_top;
static float g_ox, g_oy;
static float g_sy = 1.0f;
static int g_blend = -1;
static int gfx_ready;
static int gfx_screen_gen = -1;

static GSTEXTURE tex_corner;  /* filled AA quarter disc, center at (32,32) */
static GSTEXTURE tex_ring;    /* 2px quarter ring at radius 16 (in 32x32) */
static GSTEXTURE tex_glow;    /* radial gaussian blob */
static GSTEXTURE tex_frame;   /* blurred rounded rect for 9-slice glows */
static GSTEXTURE tex_sparkle; /* 4 point star */

/* Sprite batch (text/icons). */
#define DASH_BATCH_MAX 192
static GSPRIMUVPOINTFLAT batch_pts[DASH_BATCH_MAX * 2] __attribute__((aligned(16)));
static int batch_n;
static GSTEXTURE *batch_tex;
static gs_rgbaq batch_color;

//--------------------------------------------------------------
// Helpers
//--------------------------------------------------------------
float dashSinf(float x)
{
	return sinf(x);
}

float dashCosf(float x)
{
	return cosf(x);
}

float dashApproach(float cur, float target, float speed, float dt)
{
	float k;

	if (speed <= 0.0f)
		return target;
	k = 1.0f - expf(-speed * dt);
	if (k > 1.0f)
		k = 1.0f;
	cur += (target - cur) * k;
	if (fabsf(target - cur) < 0.001f)
		cur = target;
	return cur;
}

float dashEaseOutCubic(float t)
{
	t = dashClampf(t, 0.0f, 1.0f) - 1.0f;
	return t * t * t + 1.0f;
}

DashColor dashColorLerp(DashColor a, DashColor b, float t)
{
	DashColor c;

	t = dashClampf(t, 0.0f, 1.0f);
	c.r = (u8)(a.r + (b.r - a.r) * t);
	c.g = (u8)(a.g + (b.g - a.g) * t);
	c.b = (u8)(a.b + (b.b - a.b) * t);
	c.a = (u8)(a.a + (b.a - a.a) * t);
	return c;
}

DashColor dashColorHue(int hue, int sat, int val, int alpha)
{
	int region, rem, p, q, t;
	int r, g, b;

	hue %= 360;
	if (hue < 0)
		hue += 360;
	if (sat <= 0)
		return DC(val, val, val, alpha);
	region = hue / 60;
	rem = (hue - region * 60) * 255 / 60;
	p = (val * (255 - sat)) / 255;
	q = (val * (255 - (sat * rem) / 255)) / 255;
	t = (val * (255 - (sat * (255 - rem)) / 255)) / 255;
	switch (region) {
		case 0:
			r = val, g = t, b = p;
			break;
		case 1:
			r = q, g = val, b = p;
			break;
		case 2:
			r = p, g = val, b = t;
			break;
		case 3:
			r = p, g = q, b = val;
			break;
		case 4:
			r = t, g = p, b = val;
			break;
		default:
			r = val, g = p, b = q;
			break;
	}
	return DC(r, g, b, alpha);
}

static inline u64 gsColor(DashColor c)
{
	int a = (int)(c.a * g_alpha);

	/* GS alpha: 0x80 == 1.0 */
	a = (a + 1) >> 1;
	if (a > 0x80)
		a = 0x80;
	if (a < 0)
		a = 0;
	return GS_SETREG_RGBAQ(c.r, c.g, c.b, a, 0x00);
}

static inline float SX(float x)
{
	return x + g_ox;
}

static inline float SY(float y)
{
	return (y + g_oy) * g_sy;
}

float dashGfxScaleY(void)
{
	return g_sy;
}

//--------------------------------------------------------------
// Procedural textures
//--------------------------------------------------------------
static u32 *allocTexMem(int w, int h)
{
	u32 *mem = (u32 *)memalign(128, w * h * 4);

	if (mem != NULL)
		memset(mem, 0, w * h * 4);
	return mem;
}

static void setupTex(GSTEXTURE *tex, int w, int h, u32 *mem)
{
	memset(tex, 0, sizeof(*tex));
	tex->Width = w;
	tex->Height = h;
	tex->PSM = GS_PSM_CT32;
	tex->Mem = mem;
	tex->Filter = GS_FILTER_LINEAR;
	tex->Delayed = 1;
	gsKit_setup_tbw(tex);
	SyncDCache(mem, (u8 *)mem + w * h * 4);
}

static inline u32 whiteA(float a)
{
	int ia = (int)(a * 128.0f + 0.5f);

	if (ia < 0)
		ia = 0;
	if (ia > 128)
		ia = 128;
	return 0x00808080 | ((u32)ia << 24);
}

static void buildCornerTextures(void)
{
	u32 *corner = allocTexMem(32, 32);
	u32 *ring = allocTexMem(32, 32);
	int x, y;

	if (corner == NULL || ring == NULL) {
		free(corner);
		free(ring);
		return;
	}

	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			/* Quarter disc: circle center at the texture's bottom-right. */
			float dx = 32.0f - (x + 0.5f);
			float dy = 32.0f - (y + 0.5f);
			float d = sqrtf(dx * dx + dy * dy);
			float a = dashClampf(32.0f - d + 0.5f, 0.0f, 1.0f);
			float ring_a;

			corner[y * 32 + x] = whiteA(a);
			/* Ring: 2px wide band at radius 32 (scaled r=16 -> 1px) */
			ring_a = dashClampf(32.0f - d + 0.5f, 0.0f, 1.0f) * dashClampf(d - 28.5f, 0.0f, 1.0f);
			ring[y * 32 + x] = whiteA(ring_a);
		}
	}
	setupTex(&tex_corner, 32, 32, corner);
	setupTex(&tex_ring, 32, 32, ring);
}

static void buildGlowTextures(void)
{
	u32 *glow = allocTexMem(64, 64);
	u32 *frame = allocTexMem(64, 64);
	u32 *sparkle = allocTexMem(32, 32);
	int x, y;

	if (glow == NULL || frame == NULL || sparkle == NULL) {
		free(glow);
		free(frame);
		free(sparkle);
		return;
	}

	for (y = 0; y < 64; y++) {
		for (x = 0; x < 64; x++) {
			float dx = (x + 0.5f - 32.0f) / 32.0f;
			float dy = (y + 0.5f - 32.0f) / 32.0f;
			float d2 = dx * dx + dy * dy;
			float a = expf(-d2 * 4.0f) - expf(-4.0f);
			float fx, fy, fd, fa;

			if (d2 >= 1.0f)
				a = 0.0f;
			glow[y * 64 + x] = whiteA(dashClampf(a / (1.0f - expf(-4.0f)), 0.0f, 1.0f));

			/* Blurred rounded rect: solid core 24..40, falloff ~20px */
			fx = fabsf(x + 0.5f - 32.0f) - 8.0f;
			fy = fabsf(y + 0.5f - 32.0f) - 8.0f;
			fx = (fx > 0.0f) ? fx : 0.0f;
			fy = (fy > 0.0f) ? fy : 0.0f;
			fd = sqrtf(fx * fx + fy * fy) / 22.0f;
			fa = (fd >= 1.0f) ? 0.0f : (1.0f - fd) * (1.0f - fd);
			frame[y * 64 + x] = whiteA(fa);
		}
	}

	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			float dx = fabsf(x + 0.5f - 16.0f) / 16.0f;
			float dy = fabsf(y + 0.5f - 16.0f) / 16.0f;
			float core = expf(-(dx * dx + dy * dy) * 18.0f);
			float streak = expf(-dy * dy * 900.0f) * (1.0f - dx) + expf(-dx * dx * 900.0f) * (1.0f - dy);
			sparkle[y * 32 + x] = whiteA(dashClampf(core + streak * 0.7f, 0.0f, 1.0f));
		}
	}

	setupTex(&tex_glow, 64, 64, glow);
	setupTex(&tex_frame, 64, 64, frame);
	setupTex(&tex_sparkle, 32, 32, sparkle);
}

//--------------------------------------------------------------
// Texture manager wrappers
//--------------------------------------------------------------
int dashTexBind(GSTEXTURE *tex)
{
	if (tex == NULL || tex->Mem == NULL)
		return 0;
	gsKit_TexManager_bind(gsGlobal, tex);
	return 1;
}

void dashTexInvalidate(GSTEXTURE *tex)
{
	if (tex != NULL)
		gsKit_TexManager_invalidate(gsGlobal, tex);
}

void dashTexRelease(GSTEXTURE *tex)
{
	if (tex == NULL)
		return;
	gsKit_TexManager_free(gsGlobal, tex);
	tex->Vram = 0;
	tex->VramClut = 0;
}

//--------------------------------------------------------------
// Frame lifecycle
//--------------------------------------------------------------
void dashGfxInit(void)
{
	if (!gfx_ready) {
		buildCornerTextures();
		buildGlowTextures();
		gfx_ready = 1;
	}
	/*
	 * The classic wLE menu uses a single framebuffer. That works for its
	 * mostly static screens, but the animated dashboard writes new pixels
	 * into the framebuffer that the GS is currently scanning out. The
	 * resulting horizontal tear is particularly obvious along the header.
	 *
	 * Switch only on entering the dashboard: gsKit allocates the second
	 * framebuffer, and later sync_flip() presents complete frames at VBLANK.
	 * The dashboard is the only UI from here on; launch/IOP handoffs retain
	 * their established paths. Screen-mode changes keep DoubleBuffering ON.
	 */
	if (gsGlobal->DoubleBuffering != GS_SETTING_ON) {
		gsGlobal->DoubleBuffering = GS_SETTING_ON;
		gsKit_init_screen(gsGlobal);
		gsScreenGeneration++; /* drop every stale texture VRAM mapping */
		gsKit_set_display_offset(gsGlobal, setting->screen_x, setting->screen_y);
	}
	gfx_screen_gen = -1;
	dashGfxCheckScreenChange();
}

void dashGfxShutdown(void)
{
	/* Textures stay resident in EE RAM for the life of the program; only the
	 * VRAM side is dropped (the next bind re-uploads). */
	dashTexRelease(&tex_corner);
	dashTexRelease(&tex_ring);
	dashTexRelease(&tex_glow);
	dashTexRelease(&tex_frame);
	dashTexRelease(&tex_sparkle);
}

int dashGfxCheckScreenChange(void)
{
	if (gfx_screen_gen == gsScreenGeneration)
		return 0;

	/* gsKit_init_screen() reset the VRAM layout (TV mode change in the
	 * classic configuration, first entry, ...). Re-create the texture
	 * manager pool above the framebuffer and force re-uploads. */
	gfx_screen_gen = gsScreenGeneration;
	gsKit_TexManager_init(gsGlobal);
	gsKit_TexManager_setmode(gsGlobal, ETM_INLINE);
	g_sy = (float)SCREEN_HEIGHT / (float)DASH_VH;
	return 1;
}

void dashGfxBeginFrame(void)
{
	alpha_top = 0;
	off_top = 0;
	g_alpha = 1.0f;
	g_ox = g_oy = 0.0f;
	g_sy = (float)SCREEN_HEIGHT / (float)DASH_VH;
	batch_n = 0;
	batch_tex = NULL;

	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	gsGlobal->PrimAAEnable = GS_SETTING_OFF;
	g_blend = -1;
	dashGfxBlend(DASH_BLEND_NORMAL);
	gsKit_set_test(gsGlobal, GS_ATEST_OFF);
	gsKit_set_clamp(gsGlobal, GS_CMODE_CLAMP);
	dashGfxScissorReset();
}

/* gsKit_sync_flip() spins on the GS CSR until vblank, which can starve
 * the lower-priority file worker. Use the same flip sequence with short
 * sleeps when IO is pending. This MUST preserve the double-buffer swap:
 * previously the yielding fast path only worked for single buffering. */
static void waitVsyncSharing(void)
{
	*GS_CSR = *GS_CSR & 8;
	while (!(*GS_CSR & 8)) {
		if (dashWorkerIoBusy())
			DelayThread(250);
	}
}

static void flipVsyncSharing(void)
{
	if (!gsGlobal->FirstFrame) {
		waitVsyncSharing();
		if (gsGlobal->DoubleBuffering == GS_SETTING_ON) {
			/* Same display/active-buffer progression as gsKit_sync_flip().
			 * Show the completed buffer, then draw into the other one. */
			GS_SET_DISPFB2(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192,
			                gsGlobal->Width / 64, gsGlobal->PSM, 0, 0);
			gsGlobal->ActiveBuffer ^= 1;
		}
	}
	gsKit_setactive(gsGlobal);
}

void dashGfxPresent(void)
{
	dashSpriteBatchEnd();
	dashWorkerParkPoint();
	if (!gsGlobal->FirstFrame && dashWorkerIoBusy())
		flipVsyncSharing();
	else
		gsKit_sync_flip(gsGlobal);
	gsKit_queue_exec(gsGlobal);
	gsKit_TexManager_nextFrame(gsGlobal);
	/* completions are applied between frames, never mid-draw */
	dashWorkerPump();
}

void dashGfxRestoreLegacyState(void)
{
	/* The classic screens draw opaque, untextured sprites with alpha 0. */
	dashSpriteBatchEnd();
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
	gsGlobal->PrimAAEnable = GS_SETTING_ON;
	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
	g_blend = -1;
	dashGfxScissorReset();
}

//--------------------------------------------------------------
// State stacks
//--------------------------------------------------------------
void dashGfxPushAlpha(float mul)
{
	if (alpha_top < DASH_ALPHA_STACK)
		alpha_stack[alpha_top++] = g_alpha;
	g_alpha *= dashClampf(mul, 0.0f, 1.0f);
}

void dashGfxPopAlpha(void)
{
	if (alpha_top > 0)
		g_alpha = alpha_stack[--alpha_top];
	else
		g_alpha = 1.0f;
}

void dashGfxPushOffset(float dx, float dy)
{
	if (off_top < DASH_OFFSET_STACK) {
		off_x_stack[off_top] = g_ox;
		off_y_stack[off_top] = g_oy;
		off_top++;
	}
	g_ox += dx;
	g_oy += dy;
}

void dashGfxPopOffset(void)
{
	if (off_top > 0) {
		off_top--;
		g_ox = off_x_stack[off_top];
		g_oy = off_y_stack[off_top];
	} else
		g_ox = g_oy = 0.0f;
}

void dashGfxBlend(int mode)
{
	if (mode == g_blend)
		return;
	dashSpriteBatchEnd();
	g_blend = mode;
	if (mode == DASH_BLEND_ADD)
		/* Cv = Cs * As + Cd */
		gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
	else
		/* Cv = (Cs - Cd) * As + Cd */
		gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

/* Clip state is tracked because scrolling text may run inside a clipped
 * menu/list. Its temporary horizontal marquee must not discard the parent
 * scissor: restoring full-screen clipping caused lower rows to draw outside
 * seven-row popups. Coordinates are GS pixels (inclusive). */
typedef struct {
	int x0, x1, y0, y1;
} DashClipRect;

#define DASH_CLIP_STACK 8
static DashClipRect clip_current, clip_stack[DASH_CLIP_STACK];
static int clip_depth, clip_overflow, clip_valid;

static DashClipRect clipFromLogical(float x, float y, float w, float h)
{
	DashClipRect r;
	r.x0 = (int)SX(x);
	r.y0 = (int)SY(y);
	r.x1 = (int)SX(x + w) - 1;
	r.y1 = (int)SY(y + h) - 1;
	if (r.x0 < 0) r.x0 = 0;
	if (r.y0 < 0) r.y0 = 0;
	if (r.x0 >= SCREEN_WIDTH) r.x0 = SCREEN_WIDTH - 1;
	if (r.y0 >= SCREEN_HEIGHT) r.y0 = SCREEN_HEIGHT - 1;
	if (r.x1 > SCREEN_WIDTH - 1) r.x1 = SCREEN_WIDTH - 1;
	if (r.y1 > SCREEN_HEIGHT - 1) r.y1 = SCREEN_HEIGHT - 1;
	if (r.x1 < r.x0) r.x1 = r.x0;
	if (r.y1 < r.y0) r.y1 = r.y0;
	return r;
}

static void clipApply(DashClipRect r)
{
	dashSpriteBatchEnd(); /* queued sprites must keep the preceding clip */
	clip_current = r;
	clip_valid = 1;
	gsKit_set_scissor(gsGlobal,
	                 GS_SETREG_SCISSOR(r.x0, r.x1, r.y0, r.y1));
}

void dashGfxScissor(float x, float y, float w, float h)
{
	clipApply(clipFromLogical(x, y, w, h));
}

void dashGfxScissorReset(void)
{
	DashClipRect full = {0, SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT - 1};
	clip_depth = clip_overflow = 0;
	clipApply(full);
}

/* Child clipping is the intersection with the current parent's scissor.
 * A bounded stack prevents recursive UI from consuming unbounded memory. */
void dashGfxScissorPush(float x, float y, float w, float h)
{
	DashClipRect r;
	if (!clip_valid)
		dashGfxScissorReset();
	if (clip_depth >= DASH_CLIP_STACK) {
		clip_overflow++;
		return; /* keep the stricter existing clip */
	}
	clip_stack[clip_depth++] = clip_current;
	r = clipFromLogical(x, y, w, h);
	if (r.x0 < clip_current.x0) r.x0 = clip_current.x0;
	if (r.y0 < clip_current.y0) r.y0 = clip_current.y0;
	if (r.x1 > clip_current.x1) r.x1 = clip_current.x1;
	if (r.y1 > clip_current.y1) r.y1 = clip_current.y1;
	if (r.x0 > clip_current.x1) r.x0 = clip_current.x1;
	if (r.y0 > clip_current.y1) r.y0 = clip_current.y1;
	if (r.x1 < r.x0) r.x1 = r.x0;
	if (r.y1 < r.y0) r.y1 = r.y0;
	clipApply(r);
}

void dashGfxScissorPop(void)
{
	if (clip_overflow > 0) {
		clip_overflow--;
		return;
	}
	if (clip_depth > 0)
		clipApply(clip_stack[--clip_depth]);
}

//--------------------------------------------------------------
// Primitives
//--------------------------------------------------------------
void dashRect(float x, float y, float w, float h, DashColor c)
{
	if (w <= 0.0f || h <= 0.0f || c.a == 0 || g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	gsKit_prim_sprite(gsGlobal, SX(x), SY(y), SX(x + w), SY(y + h), 0, gsColor(c));
}

void dashRectGrad4(float x, float y, float w, float h, DashColor tl, DashColor tr, DashColor bl, DashColor br)
{
	float x1, y1, x2, y2;

	if (w <= 0.0f || h <= 0.0f || g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	x1 = SX(x);
	y1 = SY(y);
	x2 = SX(x + w);
	y2 = SY(y + h);
	gsKit_prim_quad_gouraud_3d(gsGlobal, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0,
	                           gsColor(tl), gsColor(tr), gsColor(bl), gsColor(br));
}

void dashRectGradV(float x, float y, float w, float h, DashColor top, DashColor bottom)
{
	dashRectGrad4(x, y, w, h, top, top, bottom, bottom);
}

void dashRectGradH(float x, float y, float w, float h, DashColor left, DashColor right)
{
	dashRectGrad4(x, y, w, h, left, right, left, right);
}

void dashTri(float x1, float y1, float x2, float y2, float x3, float y3, DashColor c1, DashColor c2, DashColor c3)
{
	if (g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	gsKit_prim_triangle_gouraud_3d(gsGlobal, SX(x1), SY(y1), 0, SX(x2), SY(y2), 0, SX(x3), SY(y3), 0,
	                               gsColor(c1), gsColor(c2), gsColor(c3));
}

/* Arbitrary quad, vertices in strip order: p1-p2 one edge, p3-p4 the
 * opposite edge (p1 next to p3). Used by the procedural backgrounds. */
void dashQuadG(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
               DashColor c1, DashColor c2, DashColor c3, DashColor c4)
{
	if (g_alpha <= 0.0f || (c1.a == 0 && c2.a == 0 && c3.a == 0 && c4.a == 0))
		return;
	dashSpriteBatchEnd();
	gsKit_prim_quad_gouraud_3d(gsGlobal, SX(x1), SY(y1), 0, SX(x2), SY(y2), 0, SX(x3), SY(y3), 0, SX(x4), SY(y4), 0,
	                           gsColor(c1), gsColor(c2), gsColor(c3), gsColor(c4));
}

/* Line of width w as a quad (1px GS lines flicker on interlaced output). */
void dashLine(float x1, float y1, float x2, float y2, float w, DashColor c)
{
	float dx = x2 - x1, dy = y2 - y1;
	float len = sqrtf(dx * dx + dy * dy);
	float nx, ny;

	if (len < 0.01f || c.a == 0 || g_alpha <= 0.0f)
		return;
	nx = -dy / len * w * 0.5f;
	ny = dx / len * w * 0.5f;
	dashQuadG(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x1 - nx, y1 - ny, x2 - nx, y2 - ny, c, c, c, c);
}

static void texQuad(GSTEXTURE *tex, float x1, float y1, float x2, float y2,
                    float u1, float v1, float u2, float v2, u64 color)
{
	gsKit_prim_sprite_texture(gsGlobal, tex, SX(x1), SY(y1), u1, v1, SX(x2), SY(y2), u2, v2, 0, color);
}

void dashTexDrawUV(GSTEXTURE *tex, float x, float y, float w, float h,
                   float u1, float v1, float u2, float v2, DashColor tint)
{
	if (tex == NULL || w <= 0.0f || h <= 0.0f || tint.a == 0 || g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	if (!dashTexBind(tex))
		return;
	texQuad(tex, x, y, x + w, y + h, u1, v1, u2, v2, gsColor(tint));
}

void dashTexDrawQuadUV(GSTEXTURE *tex,
                       float x1, float y1, float u1, float v1,
                       float x2, float y2, float u2, float v2,
                       float x3, float y3, float u3, float v3,
                       float x4, float y4, float u4, float v4,
                       DashColor tint)
{
	if (tex == NULL || tint.a == 0 || g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	if (!dashTexBind(tex))
		return;
	gsKit_prim_quad_texture_3d(gsGlobal, tex,
	                          SX(x1), SY(y1), 0, u1, v1,
	                          SX(x2), SY(y2), 0, u2, v2,
	                          SX(x3), SY(y3), 0, u3, v3,
	                          SX(x4), SY(y4), 0, u4, v4,
	                          gsColor(tint));
}

void dashTexDraw(GSTEXTURE *tex, float x, float y, float w, float h, DashColor tint)
{
	if (tex == NULL)
		return;
	dashTexDrawUV(tex, x, y, w, h, 0.0f, 0.0f, (float)tex->Width, (float)tex->Height, tint);
}

void dashTexDrawFit(GSTEXTURE *tex, float x, float y, float w, float h, DashColor tint)
{
	float tw, th, s, dw, dh;

	if (tex == NULL || tex->Width == 0 || tex->Height == 0)
		return;
	tw = (float)tex->Width;
	th = (float)tex->Height;
	s = w / tw;
	if (th * s > h)
		s = h / th;
	dw = tw * s;
	dh = th * s;
	dashTexDraw(tex, x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, dw, dh, tint);
}

static void panelCorners(GSTEXTURE *tex, float x, float y, float w, float h, float r,
                         DashColor top, DashColor bottom)
{
	float tw = (float)tex->Width;
	u64 ct = gsColor(top), cb = gsColor(bottom);

	if (!dashTexBind(tex))
		return;
	texQuad(tex, x, y, x + r, y + r, 0, 0, tw, tw, ct);
	texQuad(tex, x + w - r, y, x + w, y + r, tw, 0, 0, tw, ct);
	texQuad(tex, x, y + h - r, x + r, y + h, 0, tw, tw, 0, cb);
	texQuad(tex, x + w - r, y + h - r, x + w, y + h, tw, tw, 0, 0, cb);
}

void dashPanelGradV(float x, float y, float w, float h, float r, DashColor top, DashColor bottom)
{
	dashRoundedGradV(x, y, w, h, dashThemeRadius(r), top, bottom);
}

void dashRoundedGradV(float x, float y, float w, float h, float r, DashColor top, DashColor bottom)
{
	DashColor mid_top, mid_bottom;

	if (w <= 0.0f || h <= 0.0f || g_alpha <= 0.0f)
		return;
	if (r > w * 0.5f)
		r = w * 0.5f;
	if (r > h * 0.5f)
		r = h * 0.5f;
	if (r < 1.0f) {
		dashRectGradV(x, y, w, h, top, bottom);
		return;
	}
	dashSpriteBatchEnd();
	mid_top = dashColorLerp(top, bottom, r / h);
	mid_bottom = dashColorLerp(top, bottom, 1.0f - r / h);
	/* Non-overlapping pieces so translucent fills blend exactly once. */
	dashRectGradV(x + r, y, w - 2 * r, r, top, mid_top);
	dashRectGradV(x, y + r, w, h - 2 * r, mid_top, mid_bottom);
	dashRectGradV(x + r, y + h - r, w - 2 * r, r, mid_bottom, bottom);
	panelCorners(&tex_corner, x, y, w, h, r, dashColorLerp(top, mid_top, 0.5f), dashColorLerp(mid_bottom, bottom, 0.5f));
}

void dashPanel(float x, float y, float w, float h, float r, DashColor fill)
{
	dashPanelGradV(x, y, w, h, r, fill, fill);
}

void dashPanelBorder(float x, float y, float w, float h, float r, DashColor c)
{
	dashRoundedBorder(x, y, w, h, dashThemeRadius(r), c);
}

void dashRoundedBorder(float x, float y, float w, float h, float r, DashColor c)
{
	float t;

	if (w <= 0.0f || h <= 0.0f || c.a == 0 || g_alpha <= 0.0f)
		return;
	if (r > w * 0.5f)
		r = w * 0.5f;
	if (r > h * 0.5f)
		r = h * 0.5f;
	if (r < 2.0f) {
		/* square corners: four plain edges */
		/* Two logical pixels survive interlaced output better than 1.5. */
		float t = 2.0f;
		dashSpriteBatchEnd();
		dashRect(x, y, w, t, c);
		dashRect(x, y + h - t, w, t, c);
		dashRect(x, y + t, t, h - 2 * t, c);
		dashRect(x + w - t, y + t, t, h - 2 * t, c);
		return;
	}
	dashSpriteBatchEnd();
	/* Ring texture band covers the outer 3.5/32 of the corner radius. */
	t = r * 3.5f / 32.0f;
	if (t < 2.0f)
		t = 2.0f;
	dashRect(x + r, y, w - 2 * r, t, c);
	dashRect(x + r, y + h - t, w - 2 * r, t, c);
	dashRect(x, y + r, t, h - 2 * r, c);
	dashRect(x + w - t, y + r, t, h - 2 * r, c);
	panelCorners(&tex_ring, x, y, w, h, r, c, c);
}

void dashGlow(float cx, float cy, float rx, float ry, DashColor c)
{
	if (rx <= 0.0f || ry <= 0.0f || c.a == 0 || g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	if (!dashTexBind(&tex_glow))
		return;
	texQuad(&tex_glow, cx - rx, cy - ry, cx + rx, cy + ry, 0, 0, 64, 64, gsColor(c));
}

void dashSparkle(float cx, float cy, float size, DashColor c)
{
	if (size <= 0.0f || c.a == 0 || g_alpha <= 0.0f)
		return;
	dashSpriteBatchEnd();
	if (!dashTexBind(&tex_sparkle))
		return;
	texQuad(&tex_sparkle, cx - size, cy - size, cx + size, cy + size, 0, 0, 32, 32, gsColor(c));
}

void dashGlowRect(float x, float y, float w, float h, float spread, DashColor c)
{
	/* 9-slice of the blurred rounded rect; the 24px texture border maps to
	 * `spread` pixels outside the rectangle. */
	float s = spread;
	float x0 = x - s, y0 = y - s, x3 = x + w + s, y3 = y + h + s;
	float x1 = x0 + s * 1.33f, x2 = x3 - s * 1.33f;
	float y1 = y0 + s * 1.33f, y2 = y3 - s * 1.33f;
	u64 col;

	if (spread <= 0.0f || c.a == 0 || g_alpha <= 0.0f)
		return;
	if (x2 < x1)
		x1 = x2 = (x0 + x3) * 0.5f;
	if (y2 < y1)
		y1 = y2 = (y0 + y3) * 0.5f;
	dashSpriteBatchEnd();
	if (!dashTexBind(&tex_frame))
		return;
	col = gsColor(c);
	texQuad(&tex_frame, x0, y0, x1, y1, 0, 0, 24, 24, col);
	texQuad(&tex_frame, x1, y0, x2, y1, 24, 0, 40, 24, col);
	texQuad(&tex_frame, x2, y0, x3, y1, 40, 0, 64, 24, col);
	texQuad(&tex_frame, x0, y1, x1, y2, 0, 24, 24, 40, col);
	texQuad(&tex_frame, x1, y1, x2, y2, 24, 24, 40, 40, col);
	texQuad(&tex_frame, x2, y1, x3, y2, 40, 24, 64, 40, col);
	texQuad(&tex_frame, x0, y2, x1, y3, 0, 40, 24, 64, col);
	texQuad(&tex_frame, x1, y2, x2, y3, 24, 40, 40, 64, col);
	texQuad(&tex_frame, x2, y2, x3, y3, 40, 40, 64, 64, col);
}

void dashShadow(float x, float y, float w, float h, float spread, float alpha)
{
	dashGlowRect(x + spread * 0.15f, y + spread * 0.35f, w, h, spread, dashColorA(DASH_C_SHADOW, alpha));
}

//--------------------------------------------------------------
// Sprite batching
//--------------------------------------------------------------
void dashSpriteBatchBegin(GSTEXTURE *tex, DashColor c)
{
	int a;

	dashSpriteBatchEnd();
	if (tex == NULL || !dashTexBind(tex))
		return;
	batch_tex = tex;
	a = (int)(c.a * g_alpha);
	a = (a + 1) >> 1;
	if (a > 0x80)
		a = 0x80;
	batch_color = color_to_RGBAQ(c.r, c.g, c.b, (u8)a, 1.0f);
	batch_n = 0;
}

void dashSpriteBatchAdd(float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2)
{
	if (batch_tex == NULL)
		return;
	if (batch_n >= DASH_BATCH_MAX) {
		GSTEXTURE *tex = batch_tex;
		gs_rgbaq col = batch_color;

		gskit_prim_list_sprite_texture_uv_flat_color(gsGlobal, tex, col, batch_n * 2, batch_pts);
		batch_n = 0;
	}
	batch_pts[batch_n * 2].uv = vertex_to_UV(batch_tex, u1, v1);
	batch_pts[batch_n * 2].xyz2 = vertex_to_XYZ2(gsGlobal, SX(x1), SY(y1), 0);
	batch_pts[batch_n * 2 + 1].uv = vertex_to_UV(batch_tex, u2, v2);
	batch_pts[batch_n * 2 + 1].xyz2 = vertex_to_XYZ2(gsGlobal, SX(x2), SY(y2), 0);
	batch_n++;
}

void dashSpriteBatchEnd(void)
{
	GSTEXTURE *tex = batch_tex;

	batch_tex = NULL;
	if (tex != NULL && batch_n > 0)
		gskit_prim_list_sprite_texture_uv_flat_color(gsGlobal, tex, batch_color, batch_n * 2, batch_pts);
	batch_n = 0;
}
//--------------------------------------------------------------
//End of file: dash_gfx.c
//--------------------------------------------------------------
