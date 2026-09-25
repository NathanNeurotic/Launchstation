//--------------------------------------------------------------
//File name:   dash_theme.c
//Modern look for the classic wLaunchELF screens (FileBrowser, HDD
//Manager, Text Editor, configuration, dialogs...).
//
//The classic screens keep their exact layout and logic; only the drawing
//primitives in draw.c/draw_text.c are routed here when enabled:
//  * background fills become a vertical gradient that is a pure function
//    of Y, so the classic partial redraws (message line, dialogs) stay
//    seamless
//  * frames become soft accent borders, popups become solid panels
//  * text uses the smoothed dashboard font atlas (one textured sprite per
//    glyph instead of up to 16 untextured sprites)
//  * the FileBrowser cursor gets a highlight bar
//Everything works in real framebuffer coordinates (the classic code is
//resolution-aware) and restores the untextured/no-alpha GS state after
//each call.
//--------------------------------------------------------------
#include "dash_internal.h"

static int theme_on;

int DashThemeLegacyEnabled(void)
{
	return theme_on && dashFontTexture() != NULL;
}

void DashThemeSetLegacyEnabled(int enabled)
{
	theme_on = enabled ? 1 : 0;
}

static inline u64 rgbaq(int r, int g, int b, int a)
{
	return GS_SETREG_RGBAQ(r, g, b, a, 0x00);
}

static void gradColor(float t, int *r, int *g, int *b)
{
	/* base tint from the user's background colour so skins still matter */
	u64 bg = setting->color[COLOR_BACKGR];
	int br = (int)(bg & 0xFF), bgc = (int)((bg >> 8) & 0xFF), bb = (int)((bg >> 16) & 0xFF);
	int tr, tg, tb;

	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;
	/* top (16,20,44) -> middle (12,15,32) -> bottom (4,5,11) */
	if (t < 0.5f) {
		float k = t * 2.0f;
		tr = (int)(16 + (12 - 16) * k);
		tg = (int)(20 + (15 - 20) * k);
		tb = (int)(44 + (32 - 44) * k);
	} else {
		float k = (t - 0.5f) * 2.0f;
		tr = (int)(12 + (4 - 12) * k);
		tg = (int)(15 + (5 - 15) * k);
		tb = (int)(32 + (11 - 32) * k);
	}
	*r = (tr + br) > 255 ? 255 : tr + br;
	*g = (tg + bgc) > 255 ? 255 : tg + bgc;
	*b = (tb + bb) > 255 ? 255 : tb + bb;
}

void DashThemeBackgroundRect(int x1, int y1, int x2, int y2)
{
	int r1, g1, b1, r2, g2, b2;
	float h = (float)SCREEN_HEIGHT;

	if (y2 <= y1 || x2 <= x1)
		return;
	gradColor(y1 / h, &r1, &g1, &b1);
	gradColor(y2 / h, &r2, &g2, &b2);
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
	gsKit_prim_quad_gouraud_3d(gsGlobal, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0,
	                           rgbaq(r1, g1, b1, 0x80), rgbaq(r1, g1, b1, 0x80),
	                           rgbaq(r2, g2, b2, 0x80), rgbaq(r2, g2, b2, 0x80));
}

void DashThemePopupRect(int x1, int y1, int x2, int y2)
{
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
	gsKit_prim_quad_gouraud_3d(gsGlobal, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0,
	                           rgbaq(34, 40, 70, 0x80), rgbaq(34, 40, 70, 0x80),
	                           rgbaq(20, 24, 42, 0x80), rgbaq(20, 24, 42, 0x80));
}

void DashThemeFrame(int x1, int y1, int x2, int y2, u64 color)
{
	/* soft two-tone border: user frame colour lifted towards the accent */
	int r = (int)(color & 0xFF), g = (int)((color >> 8) & 0xFF), b = (int)((color >> 16) & 0xFF);
	u64 c;

	r = (r + 110) / 2 + 20;
	g = (g + 190) / 2 + 20;
	b = (b + 255) / 2 + 10;
	if (r > 255)
		r = 255;
	if (g > 255)
		g = 255;
	if (b > 255)
		b = 255;
	c = rgbaq(r, g, b, 0x50);
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	gsKit_prim_sprite(gsGlobal, x1 + 4, y1, x2 - 4, y1 + 2, 0, c);
	gsKit_prim_sprite(gsGlobal, x1 + 4, y2 - 2, x2 - 4, y2, 0, c);
	gsKit_prim_sprite(gsGlobal, x1, y1 + 4, x1 + 2, y2 - 4, 0, c);
	gsKit_prim_sprite(gsGlobal, x2 - 2, y1 + 4, x2, y2 - 4, 0, c);
	/* rounded-looking corners */
	gsKit_prim_sprite(gsGlobal, x1 + 2, y1 + 2, x1 + 4, y1 + 4, 0, c);
	gsKit_prim_sprite(gsGlobal, x2 - 4, y1 + 2, x2 - 2, y1 + 4, 0, c);
	gsKit_prim_sprite(gsGlobal, x1 + 2, y2 - 4, x1 + 4, y2 - 2, 0, c);
	gsKit_prim_sprite(gsGlobal, x2 - 4, y2 - 4, x2 - 2, y2 - 2, 0, c);
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
}

void DashThemeChar(unsigned int c, int x, int y, u64 colour)
{
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	dashFontLegacyChar(c, x, y, colour);
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
}

void DashThemeSelectionBar(int x1, int y1, int x2, int y2)
{
	if (!DashThemeLegacyEnabled())
		return;
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	gsKit_prim_quad_gouraud_3d(gsGlobal, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0,
	                           rgbaq(70, 130, 220, 0x50), rgbaq(70, 130, 220, 0x18),
	                           rgbaq(50, 100, 190, 0x50), rgbaq(50, 100, 190, 0x18));
	gsKit_prim_sprite(gsGlobal, x1, y1, x1 + 3, y2, 0, rgbaq(160, 225, 255, 0x70));
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
}
//--------------------------------------------------------------
//End of file: dash_theme.c
//--------------------------------------------------------------
