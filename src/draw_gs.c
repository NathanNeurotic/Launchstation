//--------------------------------------------------------------
//File name:   draw_gs.c
//--------------------------------------------------------------
#include "draw_private.h"

int is576pSupported(void)
{
	int i, version = 0;

	/* Retail PS2 ROMs support native SetGsCrt(0x53) from 2.20 onward.
	 * PSX/DESR 1.80J and 2.10J also support it despite the lower version.
	 * Other older/unknown ROMs use interlace, avoiding a GS init stall. */
	for (i = 0; i < 4; i++) {
		if (ROMVER_data[i] < '0' || ROMVER_data[i] > '9')
			return 0;
		version = version * 10 + ROMVER_data[i] - '0';
	}
	if (console_is_PSX && ROMVER_data[4] == 'J' && (version == 180 || version == 210))
		return 1;
	return version >= 220;
}

static int requestedTVMode(void)
{
	int mode = setting->TV_mode;

	/* Validate before any GS reset, including startup/configuration reloads.
	 * Meaningless values are normalized to Auto. A valid choice this console
	 * cannot drive (576p on an unsupported ROM, e.g. a memory card moved from
	 * another console) boots in Auto too, but stays saved so it applies again
	 * on a console that supports it. */
	if (mode < TV_mode_AUTO || mode >= TV_mode_COUNT)
		setting->TV_mode = mode = TV_mode_AUTO;
	if (mode == TV_mode_576P && !is576pSupported())
		mode = TV_mode_AUTO;
	if (mode == TV_mode_576I)
		return TV_mode_PAL;
	return mode == TV_mode_AUTO ? uLE_InitializeRegion() : mode;
}

static void applyGSParams(void)
{
	switch (gsGlobal->Mode) {
		case GS_MODE_VGA_640_60:
		case GS_MODE_DTV_480P:
			SCREEN_WIDTH = 640;
			SCREEN_HEIGHT = 448;
			gsGlobal->Interlace = GS_NONINTERLACED;
			gsGlobal->Field = GS_FRAME;
			Menu_end_y = Menu_start_y + 22 * FONT_HEIGHT;
			break;
		case GS_MODE_DTV_576P:
			SCREEN_WIDTH = 640;
			SCREEN_HEIGHT = 512;
			gsGlobal->Interlace = GS_NONINTERLACED;
			gsGlobal->Field = GS_FRAME;
			Menu_end_y = Menu_start_y + 26 * FONT_HEIGHT;
			break;
		case GS_MODE_PAL:
			SCREEN_WIDTH = 640;
			SCREEN_HEIGHT = 512;
			gsGlobal->Interlace = GS_INTERLACED;
			gsGlobal->Field = GS_FIELD;
			Menu_end_y = Menu_start_y + 26 * FONT_HEIGHT;
			break;
		default:
		case GS_MODE_NTSC:
			SCREEN_WIDTH = 640;
			SCREEN_HEIGHT = 448;
			gsGlobal->Interlace = GS_INTERLACED;
			gsGlobal->Field = GS_FIELD;
			Menu_end_y = Menu_start_y + 22 * FONT_HEIGHT;
	}  // end TV_Mode change

	Frame_end_y = Menu_end_y + 3;
	Menu_tooltip_y = Frame_end_y + LINE_THICKNESS + 2;

	// Init screen size
	gsGlobal->Width = SCREEN_WIDTH;
	gsGlobal->Height = SCREEN_HEIGHT;
}
static void initScreenParams(void)
{
	// Ensure that the offsets are within valid ranges.
	if (setting->screen_x < -gsGlobal->StartX)
		setting->screen_x = -gsGlobal->StartX;
	else if (setting->screen_x > gsGlobal->StartX)
		setting->screen_x = gsGlobal->StartX;

	if (setting->screen_y < -gsGlobal->StartY)
		setting->screen_y = -gsGlobal->StartY;
	else if (setting->screen_y > gsGlobal->StartY)
		setting->screen_y = gsGlobal->StartY;
}
void setupGS(void)
{
	int New_TV_mode = requestedTVMode();

	// GS Init
	gsGlobal = gsKit_init_global();

	// Screen display mode
	TV_mode = New_TV_mode;
	switch (New_TV_mode) {
		case TV_mode_PAL:
			gsGlobal->Mode = GS_MODE_PAL;
			break;
		case TV_mode_576P:
			gsGlobal->Mode = GS_MODE_DTV_576P;
			break;
		case TV_mode_480P:
			gsGlobal->Mode = GS_MODE_DTV_480P;
			break;
		case TV_mode_VGA:
			gsGlobal->Mode = GS_MODE_VGA_640_60;
			break;
		default:
		case TV_mode_NTSC:
			gsGlobal->Mode = GS_MODE_NTSC;
			break;
	}

	// Screen size
	applyGSParams();

	// Buffer Init
	gsGlobal->PrimAAEnable = GS_SETTING_ON;
	gsGlobal->DoubleBuffering = GS_SETTING_OFF;
	gsGlobal->ZBuffering = GS_SETTING_OFF;

	// DMAC Init
	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);

	// Screen Init (will also clear screen once)
	gsKit_init_screen(gsGlobal);
	gsScreenGeneration++;

	// Screen Position Init
	initScreenParams();

	gsKit_set_display_offset(gsGlobal, setting->screen_x, setting->screen_y);

	// Screen render mode
	gsKit_mode_switch(gsGlobal, GS_ONESHOT);
}
void updateScreenMode(void)
{
	int setGS_flag = 0;
	int New_TV_mode = requestedTVMode();
	int requested_gs_mode;
	int requested_height, requested_interlace, requested_field;

	switch (New_TV_mode) {
		case TV_mode_PAL: requested_gs_mode = GS_MODE_PAL; break;
		case TV_mode_576P: requested_gs_mode = GS_MODE_DTV_576P; break;
		case TV_mode_480P: requested_gs_mode = GS_MODE_DTV_480P; break;
		case TV_mode_VGA: requested_gs_mode = GS_MODE_VGA_640_60; break;
		default: requested_gs_mode = GS_MODE_NTSC; break;
	}
	requested_height = (requested_gs_mode == GS_MODE_PAL || requested_gs_mode == GS_MODE_DTV_576P) ? 512 : 448;
	requested_interlace = (requested_gs_mode == GS_MODE_NTSC || requested_gs_mode == GS_MODE_PAL) ? GS_INTERLACED : GS_NONINTERLACED;
	requested_field = requested_interlace == GS_INTERLACED ? GS_FIELD : GS_FRAME;
	/* Compare the current GS object too, rather than assuming the cached
	 * menu TV_mode also guarantees matching timing and framebuffer geometry. */
	if (New_TV_mode != TV_mode || gsGlobal->Mode != requested_gs_mode ||
	    gsGlobal->Width != 640 || gsGlobal->Height != requested_height ||
	    gsGlobal->Interlace != requested_interlace || gsGlobal->Field != requested_field) {
		/* Keep the outgoing picture from flashing while GS changes timings. */
		fadeScreenToBlack();
		/* Complete the final old-mode queue before changing framebuffer
		 * addresses and resetting the GS. Both buffers are cleared below. */
		gsKit_queue_exec(gsGlobal);
		/* Drain the last GIF transfer and its GS FINISH before
		 * gsKit_init_screen() resets the hardware; resetting mid-transfer can
		 * strand the next DMA/FINISH wait. */
		dmaKit_wait_fast();
		gsKit_finish();
		setGS_flag = 1;
		TV_mode = New_TV_mode;

		gsGlobal->Mode = requested_gs_mode;
	}  // end TV_Mode change

	// Init screen parameters
	applyGSParams();

	if (setGS_flag) {
		// Init screen modes
		gsKit_init_screen(gsGlobal);
		gsKit_mode_switch(gsGlobal, GS_ONESHOT);
		clearScreenToBlack();
		gsScreenGeneration++;
	}

	// Screen Position Init
	initScreenParams();

	// Init screen position
	gsKit_set_display_offset(gsGlobal, setting->screen_x, setting->screen_y);
}
//--------------------------------------------------------------
//End of file: draw_gs.c
//--------------------------------------------------------------
