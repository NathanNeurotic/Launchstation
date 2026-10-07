//--------------------------------------------------------------
//File name:   dash_image.c
//Native dashboard image viewer.
//
//PNG decoding reuses dash_png.c, the same bounded libpng path used by
//dashboard artwork and theme images. The viewer owns its texture only
//while it is open and releases both VRAM and EE RAM on exit.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"

void dashShowPngViewer(const char *path, const char *title)
{
	const float x = 30.0f, y = 24.0f, w = 580.0f, h = 400.0f;
	const float image_x = x + 18.0f, image_y = y + 62.0f;
	const float image_w = w - 36.0f, image_h = h - 112.0f;
	u32 *pixels = NULL;
	GSTEXTURE tex;
	DashInput in;
	float open = 0.0f;
	int iw = 0, ih = 0;

	if (path == NULL || path[0] == '\0')
		return;
	if (!dashWorkerAwaitAll("Finishing up"))
		return;

	dashLoadingBegin("Opening PNG image");
	if (dashPngLoadFile(path, (int)image_w, (int)image_h,
	                    &pixels, &iw, &ih) != 0) {
		dashLoadingEnd();
		dashModalMessage(DI_WARNING, "PNG could not be opened",
		                 "The file is not a supported PNG, is damaged, or exceeds the dashboard PNG safety limits.",
		                 path);
		return;
	}
	dashLoadingEnd();

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
		            (title != NULL && title[0]) ? title : "PNG image",
		            DASH_FS_LABEL, DASH_C_TEXT);
		dashTextRight(x + w - 22, y + 22, "PNG", DASH_FS_CAPTION, DASH_C_TEXT3);

		/* Keep transparent PNGs readable against the normal modal surface. */
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
