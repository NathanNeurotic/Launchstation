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
	dashThemeInit();
	theme_on = enabled ? 1 : 0;
}

static inline u64 rgbaq(int r, int g, int b, int a)
{
	return GS_SETREG_RGBAQ(r, g, b, a, 0x00);
}

/* Background of the classic screens: a pure function of Y (so partial
 * redraws stay seamless) derived from the active theme's background
 * colours, plus the user's classic background colour as a tint so the
 * classic colour skins still matter. */
static void gradColor(float t, int *r, int *g, int *b)
{
	u64 bg = setting->color[COLOR_BACKGR];
	int br = (int)(bg & 0xFF), bgc = (int)((bg >> 8) & 0xFF), bb = (int)((bg >> 16) & 0xFF);
	DashColor top = dashColorScale(dashTheme.bg1, 0.42f * dashTheme.bg_bright / 100.0f);
	DashColor bot = dashColorScale(dashTheme.bg2, dashTheme.bg_bright / 100.0f);
	DashColor c;

	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;
	/* single-colour themes keep a hint of depth */
	if (dashTheme.bg == DASH_BG_SOLID)
		top = dashColorLighten(dashTheme.bg1, 0.03f), bot = dashTheme.bg1;
	c = dashColorLerp(top, bot, t * t);
	*r = (c.r + br) > 255 ? 255 : c.r + br;
	*g = (c.g + bgc) > 255 ? 255 : c.g + bgc;
	*b = (c.b + bb) > 255 ? 255 : c.b + bb;
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
	/* the theme's panel, tinted a little towards the selection colour */
	DashColor tint = dashColorMix(dashTheme.panel, dashTheme.select, 0.3f);
	DashColor top = dashColorLighten(tint, 0.14f), bot = dashColorLighten(tint, 0.06f);

	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
	gsKit_prim_quad_gouraud_3d(gsGlobal, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0,
	                           rgbaq(top.r, top.g, top.b, 0x80), rgbaq(top.r, top.g, top.b, 0x80),
	                           rgbaq(bot.r, bot.g, bot.b, 0x80), rgbaq(bot.r, bot.g, bot.b, 0x80));
}

void DashThemeFrame(int x1, int y1, int x2, int y2, u64 color)
{
	/* soft two-tone border: user frame colour lifted towards the accent */
	int r = (int)(color & 0xFF), g = (int)((color >> 8) & 0xFF), b = (int)((color >> 16) & 0xFF);
	u64 c;

	r = (r + dashTheme.accent.r) / 2 + 20;
	g = (g + dashTheme.accent.g) / 2 + 20;
	b = (b + dashTheme.accent.b) / 2 + 10;
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
	DashColor sc = dashColorLighten(dashTheme.select, 0.12f), sb = dashTheme.select;
	DashColor ac = dashTheme.accent;

	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	gsKit_prim_quad_gouraud_3d(gsGlobal, x1, y1, 0, x2, y1, 0, x1, y2, 0, x2, y2, 0,
	                           rgbaq(sc.r, sc.g, sc.b, 0x50), rgbaq(sc.r, sc.g, sc.b, 0x18),
	                           rgbaq(sb.r, sb.g, sb.b, 0x50), rgbaq(sb.r, sb.g, sb.b, 0x18));
	gsKit_prim_sprite(gsGlobal, x1, y1, x1 + 3, y2, 0, rgbaq(ac.r, ac.g, ac.b, 0x70));
	gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
}
//--------------------------------------------------------------
//End of file: dash_theme.c
//--------------------------------------------------------------
