//--------------------------------------------------------------
//File name:   dash_image.c
//Native dashboard image viewer.
//
//PNG uses the existing bounded libpng path. JPG/JPEG uses the bounded
//libjpeg path in dash_jpeg.c. Both end up as the same CT32 texture and
//therefore share one native viewer instead of reviving the old classic
//JPG-only UI.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"

void dashShowImageViewer(const char *path, const char *title)
{
	const float x = 30.0f, y = 24.0f, w = 580.0f, h = 400.0f;
	const float image_x = x + 18.0f, image_y = y + 62.0f;
	const float image_w = w - 36.0f, image_h = h - 112.0f;
	u32 *pixels = NULL;
	GSTEXTURE tex;
	DashInput in;
	const char *fmt = "Image";
	float open = 0.0f;
	int iw = 0, ih = 0, ok = 0;

	if (path == NULL || path[0] == '\0')
		return;
	if (!dashWorkerAwaitAll("Finishing up"))
		return;

	if (genCmpFileExt(path, "PNG")) {
		fmt = "PNG";
		dashLoadingBegin("Opening PNG image");
		ok = (dashPngLoadFile(path, (int)image_w, (int)image_h,
		                      &pixels, &iw, &ih) == 0);
	} else if (genCmpFileExt(path, "JPG") || genCmpFileExt(path, "JPEG")) {
		fmt = "JPEG";
		dashLoadingBegin("Opening JPEG image");
		ok = (dashJpegLoadFile(path, (int)image_w, (int)image_h,
		                       &pixels, &iw, &ih) == 0);
	} else {
		return;
	}
	dashLoadingEnd();

	if (!ok) {
		char heading[64];
		snprintf(heading, sizeof(heading), "%s could not be opened", fmt);
		dashModalMessage(DI_WARNING, heading,
		                 "The file is damaged, unsupported, too large, or exceeds the dashboard image safety limits.",
		                 path);
		return;
	}

	memset(&tex, 0, sizeof(tex));
	tex.Width = iw;
	tex.Height = ih;
	tex.PSM = GS_PSM_CT32;
	tex.Mem = pixels;
	tex.Filter = GS_FILTER_LINEAR;
	tex.Delayed = 1;
	gsKit_setup_tbw(&tex);

	dashInputFlush();
	while (1) {
		float oe;

		dashInputPoll(&in);
		if (in.pressed & (DASH_IN_OK | DASH_IN_BACK | DASH_IN_MENU | DASH_IN_START))
			break;

		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		oe = dashEaseOutCubic(open);

		dashRenderScene();
		dashModalBackdrop(oe);
		dashGfxPushAlpha(oe);
		dashGfxPushOffset(0, 18.0f * (1.0f - oe));
		dashModalPanel(x, y, w, h, oe);

		dashIcon(DI_IMAGE, x + 22, y + 18, 24, DASH_C_ACCENT);
		dashTextFit(x + 56, y + 18, w - 126,
		            (title != NULL && title[0]) ? title : "Image",
		            DASH_FS_LABEL, DASH_C_TEXT);
		dashTextRight(x + w - 22, y + 22, fmt, DASH_FS_CAPTION, DASH_C_TEXT3);

		/* A stable panel makes transparent PNGs readable while remaining
		 * harmless for opaque JPEG content. */
		dashRect(image_x - 2, image_y - 2, image_w + 4, image_h + 4,
		         dashColorA(DASH_C_BORDER.a ? DASH_C_BORDER : DASH_C_TEXT3, 0.35f));
		dashRect(image_x, image_y, image_w, image_h, dashColorA(DASH_C_PANEL_HI, 0.72f));
		dashTexDrawFit(&tex, image_x, image_y, image_w, image_h, DC(255, 255, 255, 255));

		dashModalHintRow(x + 22, y + h - 36, dashOkIcon(), "Close",
		                 dashBackIcon(), "Back");
		dashGfxPopOffset();
		dashGfxPopAlpha();
		dashGfxPresent();
	}

	dashTexRelease(&tex);
	free(pixels);
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_image.c
//--------------------------------------------------------------
