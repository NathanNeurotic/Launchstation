//--------------------------------------------------------------
//File name:   dash_image.c
//Native dashboard image viewer.
//
//PNG uses the existing bounded libpng path. JPG/JPEG uses the bounded
//libjpeg path in dash_jpeg.c. The viewer intentionally keeps one ATTDASH
//UI for all formats while restoring the useful controls from classic
//wLaunchELF's JPG viewer: previous/next, rotation, zoom/pan, fullscreen
//and slideshow timing.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"

static void drawRotatedImage(GSTEXTURE *tex, float x, float y, float w, float h,
                             int rotation, float zoom, float pan_x, float pan_y)
{
	float rw, rh, scale, dw, dh, dx, dy;
	float ox, oy;
	float tw = (float)tex->Width, th = (float)tex->Height;
	float u1, v1, u2, v2, u3, v3, u4, v4;

	if (rotation & 1)
		rw = th, rh = tw;
	else
		rw = tw, rh = th;

	scale = w / rw;
	if (rh * scale > h)
		scale = h / rh;
	dw = rw * scale * zoom;
	dh = rh * scale * zoom;
	ox = dw > w ? (dw - w) * 0.5f : 0.0f;
	oy = dh > h ? (dh - h) * 0.5f : 0.0f;
	dx = x + (w - dw) * 0.5f - pan_x * ox;
	dy = y + (h - dh) * 0.5f - pan_y * oy;

	switch (rotation & 3) {
		case 1: /* 90 degrees clockwise */
			u1 = 0;  v1 = th;
			u2 = 0;  v2 = 0;
			u3 = tw; v3 = th;
			u4 = tw; v4 = 0;
			break;
		case 2:
			u1 = tw; v1 = th;
			u2 = 0;  v2 = th;
			u3 = tw; v3 = 0;
			u4 = 0;  v4 = 0;
			break;
		case 3: /* 90 degrees counter-clockwise */
			u1 = tw; v1 = 0;
			u2 = tw; v2 = th;
			u3 = 0;  v3 = 0;
			u4 = 0;  v4 = th;
			break;
		default:
			u1 = 0;  v1 = 0;
			u2 = tw; v2 = 0;
			u3 = 0;  v3 = th;
			u4 = tw; v4 = th;
			break;
	}

	dashGfxScissor(x, y, w, h);
	/* GS texture modulation uses RGB 0x80 for unity; 0xFF doubles and
	 * clips the decoded image colors. DashColor alpha stays in 0..255. */
	dashTexDrawQuadUV(tex,
	                  dx,      dy,      u1, v1,
	                  dx + dw, dy,      u2, v2,
	                  dx,      dy + dh, u3, v3,
	                  dx + dw, dy + dh, u4, v4,
	                  DC(128, 128, 128, 255));
	dashGfxScissorReset();
}

static void clampPan(float *pan_x, float *pan_y)
{
	*pan_x = dashClampf(*pan_x, -1.0f, 1.0f);
	*pan_y = dashClampf(*pan_y, -1.0f, 1.0f);
}

int dashShowImageViewer(const char *path, const char *title, DashImageViewerState *state)
{
	const float px = 30.0f, py = 24.0f, pw = 580.0f, ph = 400.0f;
	const float panel_ix = px + 18.0f, panel_iy = py + 62.0f;
	const float panel_iw = pw - 36.0f, panel_ih = ph - 112.0f;
	u32 *pixels = NULL;
	GSTEXTURE tex;
	DashInput in;
	DashImageViewerState local_state;
	const char *fmt = "Image";
	float open = 0.0f, zoom = 1.0f, pan_x = 0.0f, pan_y = 0.0f;
	int rotation = 0, iw = 0, ih = 0, ok = 0, nav = DASH_IMAGE_EXIT;
	u64 slide_deadline = 0;

	if (path == NULL || path[0] == '\0')
		return DASH_IMAGE_EXIT;
	if (state == NULL) {
		memset(&local_state, 0, sizeof(local_state));
		local_state.slide_seconds = 5;
		state = &local_state;
	}
	if (state->slide_seconds < 1)
		state->slide_seconds = 5;
	if (!dashWorkerAwaitAll("Finishing up"))
		return DASH_IMAGE_EXIT;

	if (genCmpFileExt(path, "PNG")) {
		fmt = "PNG";
		dashLoadingBegin("Opening PNG image");
		ok = (dashPngLoadFile(path, DASH_VW, DASH_VH, &pixels, &iw, &ih) == 0);
	} else if (genCmpFileExt(path, "JPG") || genCmpFileExt(path, "JPEG")) {
		fmt = "JPEG";
		dashLoadingBegin("Opening JPEG image");
		ok = (dashJpegLoadFile(path, DASH_VW, DASH_VH, &pixels, &iw, &ih) == 0);
	} else {
		return DASH_IMAGE_EXIT;
	}
	dashLoadingEnd();

	if (!ok) {
		char heading[64];
		snprintf(heading, sizeof(heading), "%s could not be opened", fmt);
		dashModalMessage(DI_WARNING, heading,
		                 "The file is damaged, unsupported, too large, or exceeds the dashboard image safety limits.",
		                 path);
		return DASH_IMAGE_EXIT;
	}

	memset(&tex, 0, sizeof(tex));
	tex.Width = iw;
	tex.Height = ih;
	tex.PSM = GS_PSM_CT32;
	tex.Mem = pixels;
	tex.Filter = GS_FILTER_LINEAR;
	tex.Delayed = 1;
	gsKit_setup_tbw(&tex);

	if (state->slideshow)
		slide_deadline = Timer() + (u64)state->slide_seconds * 1000;
	dashInputFlush();

	while (1) {
		float oe, ix, iy, vw, vh;
		u64 now;
		char status[160];

		dashInputPoll(&in);
		now = Timer();

		if (state->slideshow && slide_deadline && now >= slide_deadline) {
			nav = DASH_IMAGE_NEXT;
			break;
		}

		if (in.pressed & (DASH_IN_BACK | DASH_IN_OK | DASH_IN_MENU)) {
			nav = DASH_IMAGE_EXIT;
			break;
		}
		if (in.raw_new & PAD_LEFT) {
			nav = DASH_IMAGE_PREV;
			break;
		}
		if (in.raw_new & PAD_RIGHT) {
			nav = DASH_IMAGE_NEXT;
			break;
		}
		if (in.raw_new & PAD_UP) {
			rotation = (rotation + 3) & 3;
			pan_x = pan_y = 0.0f;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.raw_new & PAD_DOWN) {
			rotation = (rotation + 1) & 3;
			pan_x = pan_y = 0.0f;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_L2) {
			zoom -= 0.15f;
			if (zoom < 1.0f)
				zoom = 1.0f;
			if (zoom == 1.0f)
				pan_x = pan_y = 0.0f;
		}
		if (in.pressed & DASH_IN_R2) {
			zoom += 0.15f;
			if (zoom > 4.0f)
				zoom = 4.0f;
		}
		if (in.raw_new & PAD_L3_H0)
			pan_x -= 0.10f / zoom;
		if (in.raw_new & PAD_L3_H1)
			pan_x += 0.10f / zoom;
		if (in.raw_new & PAD_L3_V0)
			pan_y -= 0.10f / zoom;
		if (in.raw_new & PAD_L3_V1)
			pan_y += 0.10f / zoom;
		clampPan(&pan_x, &pan_y);

		if (in.pressed & DASH_IN_FAV) {
			state->fullscreen = !state->fullscreen;
			dashSoundPlay(DASH_SND_CONFIRM);
		}
		if (in.pressed & DASH_IN_START) {
			state->slideshow = !state->slideshow;
			slide_deadline = state->slideshow ? now + (u64)state->slide_seconds * 1000 : 0;
			dashSoundPlay(DASH_SND_CONFIRM);
		}
		if (in.pressed & DASH_IN_L1) {
			if (state->slide_seconds > 1)
				state->slide_seconds--;
			if (state->slideshow)
				slide_deadline = now + (u64)state->slide_seconds * 1000;
		}
		if (in.pressed & DASH_IN_R1) {
			if (state->slide_seconds < 60)
				state->slide_seconds++;
			if (state->slideshow)
				slide_deadline = now + (u64)state->slide_seconds * 1000;
		}
		if (in.pressed & DASH_IN_SELECT) {
			zoom = 1.0f;
			pan_x = pan_y = 0.0f;
			rotation = 0;
			dashSoundPlay(DASH_SND_CONFIRM);
		}
		if (in.any && state->slideshow && !(in.pressed & DASH_IN_START))
			slide_deadline = now + (u64)state->slide_seconds * 1000;

		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		oe = dashEaseOutCubic(open);

		if (state->fullscreen) {
			/* Fullscreen is opaque. Do not spend a frame rendering the dashboard,
			 * artwork and animated background only to cover all of it with black. */
			dashGfxBeginFrame();
			dashRect(0, 0, DASH_VW, DASH_VH, DC(0, 0, 0, 255));
			ix = 0.0f;
			iy = 0.0f;
			vw = DASH_VW;
			vh = DASH_VH;
		} else {
			dashRenderScene();
			dashModalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			dashModalPanel(px, py, pw, ph, oe);
			dashIcon(DI_IMAGE, px + 22, py + 18, 24, DASH_C_ACCENT);
			dashTextFit(px + 56, py + 18, pw - 126,
			            (title != NULL && title[0]) ? title : "Image",
			            DASH_FS_LABEL, DASH_C_TEXT);
			dashTextRight(px + pw - 22, py + 22, fmt, DASH_FS_CAPTION, DASH_C_TEXT3);
			dashRect(panel_ix - 2, panel_iy - 2, panel_iw + 4, panel_ih + 4,
			         dashColorA(DASH_C_BORDER.a ? DASH_C_BORDER : DASH_C_TEXT3, 0.35f));
			dashRect(panel_ix, panel_iy, panel_iw, panel_ih, dashColorA(DASH_C_PANEL_HI, 0.72f));
			ix = panel_ix;
			iy = panel_iy;
			vw = panel_iw;
			vh = panel_ih;
		}

		drawRotatedImage(&tex, ix, iy, vw, vh, rotation, zoom, pan_x, pan_y);

		if (state->fullscreen) {
			DashColor shade = DC(0, 0, 0, 150);
			dashRect(0, 0, DASH_VW, 36, shade);
			dashRect(0, DASH_VH - 42, DASH_VW, 42, shade);
			dashTextFit(18, 10, 430, (title != NULL && title[0]) ? title : "Image",
			            DASH_FS_SMALL, DASH_C_TEXT);
			dashTextRight(DASH_VW - 18, 10, fmt, DASH_FS_CAPTION, DASH_C_TEXT2);
		}

		if (state->slideshow) {
			int remain = slide_deadline > now ? (int)((slide_deadline - now + 999) / 1000) : 0;
			snprintf(status, sizeof(status), "%d%%  %d deg  Slideshow: %ds (%ds)  L1/R1 speed",
			         (int)(zoom * 100.0f + 0.5f), rotation * 90,
			         state->slide_seconds, remain);
		} else {
			snprintf(status, sizeof(status), "%d%%  %d deg  START slideshow (%ds)  L1/R1 speed",
			         (int)(zoom * 100.0f + 0.5f), rotation * 90, state->slide_seconds);
		}

		if (state->fullscreen) {
			dashTextCenter(DASH_VW * 0.5f, DASH_VH - 34, status, DASH_FS_CAPTION, DASH_C_TEXT2);
			dashTextCenter(DASH_VW * 0.5f, DASH_VH - 18,
			               "D-pad: browse/rotate   L2/R2: zoom   Left stick: pan   Square: window   Select: reset",
			               DASH_FS_CAPTION, DASH_C_TEXT3);
		} else {
			dashTextFit(px + 20, py + ph - 44, pw - 40, status, DASH_FS_CAPTION, DASH_C_TEXT2);
			dashTextFit(px + 20, py + ph - 26, pw - 40,
			            "D-pad: browse/rotate  L2/R2: zoom  Left stick: pan  Square: fullscreen  Select: reset",
			            DASH_FS_CAPTION, DASH_C_TEXT3);
			dashGfxPopOffset();
			dashGfxPopAlpha();
		}

		dashGfxPresent();
	}

	dashTexRelease(&tex);
	free(pixels);
	if (nav == DASH_IMAGE_EXIT)
		dashSoundPlay(DASH_SND_CANCEL);
	else
		dashSoundPlay(DASH_SND_CURSOR);
	dashInputFlush();
	return nav;
}
//--------------------------------------------------------------
//End of file: dash_image.c
//--------------------------------------------------------------
