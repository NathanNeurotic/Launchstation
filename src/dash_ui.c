//--------------------------------------------------------------
//File name:   dash_ui.c
//Dashboard widgets: input mapping, header/tabs/clock, button hints,
//toasts, modal dialogs and the loading overlay.
//
//Confirm/cancel follow wLaunchELF's existing GUI_Swap_Keys setting
//(swapKeys == 0: Circle confirms, Cross cancels; swapKeys == 1: Cross
//confirms, Circle cancels), exactly like the classic screens.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "init.h"

extern u32 paddata;

static u64 last_input_ms;
static u32 held_prev;

//--------------------------------------------------------------
// Input
//--------------------------------------------------------------
void dashInputPoll(DashInput *in)
{
	dashInputPollEx(in, 1);
}

/* usb_kb_as_pad = 0 leaves the USB keyboard to the caller (text entry). */
void dashInputPollEx(DashInput *in, int usb_kb_as_pad)
{
	u32 np;
	int ret;
	u32 ok_btn = swapKeys ? PAD_CROSS : PAD_CIRCLE;
	u32 back_btn = swapKeys ? PAD_CIRCLE : PAD_CROSS;

	memset(in, 0, sizeof(*in));
	dashWorkerParkPoint();
	if (dashIopParked())
		return; /* the IOP (and padman with it) is being rebooted */
	/* readpad checks each port's state and skips unsettled ports. Waiting
	 * here stalls every frame while both controllers are negotiating. */
	/* the USB keyboard driver is on the IOP: leave it alone while the
	 * background worker talks to the IOP */
	ret = usb_kb_as_pad && !dashWorkerIoBusy() ? readpad() : readpad_no_KB();
	/* Only navigation repeats. A held action must not reopen/close a menu,
	 * launch again or toggle a setting after its first press. */
	np = ret ? new_pad & ((PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT |
	                      PAD_L3_H0 | PAD_L3_H1 | PAD_L3_V0 | PAD_L3_V1 |
	                      PAD_L2 | PAD_R2) | ~(paddata & held_prev)) : 0;
	if (paddata != held_prev)
		last_input_ms = Timer();
	held_prev = paddata;
	/* Keyboard/autotest events have no paddata held bits and pass through. */
	in->raw_new = np;
	if (np & (PAD_UP | PAD_L3_V0))
		in->pressed |= DASH_IN_UP;
	if (np & (PAD_DOWN | PAD_L3_V1))
		in->pressed |= DASH_IN_DOWN;
	if (np & (PAD_LEFT | PAD_L3_H0))
		in->pressed |= DASH_IN_LEFT;
	if (np & (PAD_RIGHT | PAD_L3_H1))
		in->pressed |= DASH_IN_RIGHT;
	if (np & ok_btn)
		in->pressed |= DASH_IN_OK;
	if (np & back_btn)
		in->pressed |= DASH_IN_BACK;
	if (np & PAD_TRIANGLE)
		in->pressed |= DASH_IN_MENU;
	if (np & PAD_SQUARE)
		in->pressed |= DASH_IN_FAV;
	if (np & PAD_L1)
		in->pressed |= DASH_IN_L1;
	if (np & PAD_R1)
		in->pressed |= DASH_IN_R1;
	if (np & PAD_L2)
		in->pressed |= DASH_IN_L2;
	if (np & PAD_R2)
		in->pressed |= DASH_IN_R2;
	if (np & PAD_START)
		in->pressed |= DASH_IN_START;
	if (np & PAD_SELECT)
		in->pressed |= DASH_IN_SELECT;
	if (np & PAD_L3)
		in->pressed |= DASH_IN_L3;
	if (np & PAD_R3)
		in->pressed |= DASH_IN_R3;
	in->any = (in->pressed != 0);
	if (in->any)
		last_input_ms = Timer();
	/* Held SELECT turns START into its quick launch key (dash_main.c). */
	if ((in->pressed & DASH_IN_START) && !(paddata & PAD_SELECT)) {
		/* Consume the entire chord: START cannot also type, accept a dialog
		 * or trigger another action after the Dashboard popup closes.
		 * A second press cancels only the open Dashboard through normal Back. */
		memset(in, 0, sizeof(*in));
		if (dashDashboardMenuIsOpen()) {
			in->pressed = DASH_IN_BACK;
			in->any = 1;
		} else {
			dashDashboardMenu();
		}
	}
}

void dashInputFlush(void)
{
	int i;

	/* swallow presses that were in flight when a screen changed */
	for (i = 0; i < 2; i++) {
		readpad_noRepeat();
	}
	held_prev = paddata;
	last_input_ms = Timer();
}

u64 dashInputIdleMs(void)
{
	u64 now = Timer();
	return (now > last_input_ms) ? now - last_input_ms : 0;
}

int dashOkIcon(void)
{
	return swapKeys ? DI_BTN_CROSS : DI_BTN_CIRCLE;
}

int dashBackIcon(void)
{
	return swapKeys ? DI_BTN_CIRCLE : DI_BTN_CROSS;
}

const char *dashOkLabelKey(void)
{
	return swapKeys ? "Cross" : "Circle";
}

//--------------------------------------------------------------
// Buttons / hints
//--------------------------------------------------------------
static float footerClockWidth(char *t, int tsize, char *d, int dsize);
static void drawFooterClock(float y, const char *t, const char *d);

static DashColor buttonColor(int icon)
{
	switch (icon) {
		case DI_BTN_CROSS:
			return DC(0x86, 0xA8, 0xFF, 255);
		case DI_BTN_CIRCLE:
			return DC(0xFF, 0x6E, 0x7C, 255);
		case DI_BTN_SQUARE:
			return DC(0xF0, 0x8C, 0xD8, 255);
		case DI_BTN_TRIANGLE:
			return DC(0x4E, 0xE0, 0xA8, 255);
		default:
			return DASH_C_TEXT;
	}
}

static int keyIcon(const char *key)
{
	static const struct
	{
		const char *key;
		int icon;
	} map[] = {
	    {"L1", DI_BTN_L1}, {"L2", DI_BTN_L2}, {"L3", DI_BTN_L3},
	    {"R1", DI_BTN_R1}, {"R2", DI_BTN_R2}, {"R3", DI_BTN_R3},
	};
	int i;

	if (key == NULL)
		return -1;
	/* START/SELECT always have vector glyphs. Their tiny text-badge fallback
	 * read as a blank SELECT-like bar on CRTs, so "Menu" looked like SELECT. */
	if (!strcmp(key, "START"))
		return DI_BTN_START;
	if (!strcmp(key, "SELECT"))
		return DI_BTN_SELECT;
	/* All six shoulder/stick buttons are rendered by the vector atlas. */
	for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (!strcmp(key, map[i].key))
			return map[i].icon;
	return -1;
}

/* the width drawButton() will use, without drawing */
static float buttonWidth(int icon, const char *key)
{
	float w;

	if (icon >= 0 || keyIcon(key) >= 0)
		return 22.0f;
	w = dashTextWidth(key, DASH_FS_CAPTION) + 14.0f;
	return w < 22.0f ? 22.0f : w;
}

static float drawButton(float x, float y, int icon, const char *key)
{
	const float d = 22.0f;

	if (icon >= 0) {
		dashIcon(icon, x, y, d, buttonColor(icon));
		return d;
	}
	{
		int ki = keyIcon(key);
		float tw;
		int keep;
		float w;
		if (ki >= 0) {
			dashIcon(ki, x, y, d, DASH_C_TEXT);
			return d;
		}
		tw = dashTextWidth(key, DASH_FS_CAPTION);
		w = buttonWidth(icon, key); /* one sizing rule for layout and drawing */
		/* OSDSYS squircle badge for L1/R1/START/SELECT-style labels */
		keep = dashTheme.corner;
		if (dashCfg.icon_backing) {
			dashTheme.corner = DASH_CORNER_SOFT;
			/* Theme-derived badges instead of hard-coded navy/white tiles. */
			dashPanelGradV(x, y, w, d, 6.0f,
			               DC(30, 35, 46, 230),
			               DC(18, 22, 30, 230));
			dashPanelBorder(x, y, w, d, 6.0f, DC(210, 220, 240, 120));
		}
		dashTheme.corner = keep;
		dashTextS(x + (w - tw) * 0.5f, y + 2.5f, key, DASH_FS_CAPTION, DASH_C_TEXT); /* caps centred in the badge */
		return w;
	}
}

void dashDrawHints(const DashHint *hints, int n)
{
	float total = 0.0f, x, labels = 0.0f, label_ratio = 1.0f;
	float y = 400.0f;
	int i;

	/* Measure exactly what the drawing loop below advances by, so every
	 * screen's hint bar ends at the same right edge whatever its keys are
	 * (START/SELECT may be pad icons or text badges). */
	for (i = 0; i < n; i++) {
		total += buttonWidth(hints[i].button, hints[i].key) + 8.0f + 24.0f;
		labels += dashTextWidth(localizeDashboardText(hints[i].label), DASH_FS_SMALL);
	}
	/* Long translated action names share the remaining label space. Keep
	 * button glyphs/readable type sizes intact, ellipsizing labels as needed. */
	if (labels > 0.0f)
		label_ratio = dashClampf((DASH_SAFE_R - DASH_SAFE_L + 12.0f - total) / labels, 0.0f, 1.0f);
	total += labels * label_ratio;
	x = DASH_SAFE_R - total + 12.0f;
	if (x < DASH_SAFE_L)
		x = DASH_SAFE_L;
	x += 14.0f * (1.0f - dashEaseOutCubic(dashSectionAnim()));
	{
		char ct[48], cd[24];
		float cw = footerClockWidth(ct, sizeof(ct), cd, sizeof(cd));
		/* only when it does not collide with the hints */
		if (cw > 0.0f && DASH_SAFE_L + cw + 24.0f < x)
			drawFooterClock(y, ct, cd);
	}
	{
		DashColor sh = dashColorScale(dashTheme.bg2, 0.6f);
		sh.a = 0;
		dashRectGradH(x - 40, y - 6, DASH_SAFE_R - x + 60, 34, sh, DC(sh.r, sh.g, sh.b, 150));
	}
	for (i = 0; i < n; i++) {
		const char *label = localizeDashboardText(hints[i].label);
		float label_w = dashTextWidth(label, DASH_FS_SMALL) * label_ratio;
		x += drawButton(x, y, hints[i].button, hints[i].key) + 8.0f;
		/* x-height centre (y + 16 * scale) about 1 px under the 22 px button's
		 * centre, so mixed-case labels read as centred on the button */
		if (label_w > 0.0f)
			dashTextFit(x, y + 1.5f, label_w, label, DASH_FS_SMALL, DASH_C_TEXT);
		x += label_w + 24.0f;
	}
}

//--------------------------------------------------------------
// Header
//--------------------------------------------------------------
#define HEADER_TAB_X 108.0f
#define HEADER_TAB_W 66.0f
#define HEADER_TAB_PAD 40.0f

static float tab_anim_x = -1.0f;

/* The text renderer uses literal Latin-1 bytes, including degree (0xB0). */
static void appendClockTemperature(char *time, int size)
{
	int raw, tenths;
	size_t len;
	if (!menuTitleGetTemperatureRaw(&raw))
		return;
	if (dashCfg.temp_fahrenheit)
		tenths = (raw * 18 + 64) / 128 + 320;
	else
		tenths = (raw * 10 + 64) / 128;
	len = strlen(time);
	if (len < (size_t)size)
		snprintf(time + len, size - len, "  %d.%d\260%c", tenths / 10,
		         tenths % 10, dashCfg.temp_fahrenheit ? 'F' : 'C');
}

/* Clock text per the theme: 12/24 h and date order follow the console's
 * own settings unless the theme overrides them. */
static int clockText(char *t, int tsize, char *d, int dsize)
{
	int y, mo, dd, h, mi, s, use12, dfmt;
	int valid = menuTitleGetClockFields(&y, &mo, &dd, &h, &mi, &s, &use12, &dfmt);

	if (dashTheme.clock_hours == DASH_HOURS_24)
		use12 = 0;
	else if (dashTheme.clock_hours == DASH_HOURS_12)
		use12 = 1;
	switch (dashTheme.clock_date) {
		case DASH_DATE_YMD:
			dfmt = 0;
			break;
		case DASH_DATE_MDY:
			dfmt = 1;
			break;
		case DASH_DATE_DMY:
			dfmt = 2;
			break;
	}
	if (!valid) {
		dashStrCopy(t, dashTheme.clock_seconds ? "--:--:--" : "--:--", tsize);
		dashStrCopy(d, "", dsize);
		appendClockTemperature(t, tsize);
		return 0;
	}
	menuTitleFormatClockTime(t, tsize, h, mi, s, use12);
	/* "HH:MM:SS[ PM]" -> "HH:MM[ PM]" */
	if (!dashTheme.clock_seconds && strlen(t) >= 8 && t[2] == ':' && t[5] == ':')
		memmove(t + 5, t + 8, strlen(t + 8) + 1);
	if (dashTheme.clock_date == DASH_DATE_OFF)
		d[0] = '\0';
	else
		menuTitleFormatClockDate(d, dsize, y, mo, dd, dfmt);
	appendClockTemperature(t, tsize);
	return 1;
}

void dashDrawClock(void)
{
	char t[48], d[24];
	float scale, width, available;

	if (dashTheme.clock == DASH_CLOCK_OFF || dashTheme.clock_pos != DASH_CLOCKPOS_HEADER)
		return;
	clockText(t, sizeof(t), d, sizeof(d));
	/* Shrink a long time+temperature string rather than overlap the tabs. */
	scale = dashTheme.clock == DASH_CLOCK_SYSTEM ? DASH_FS_BODY : DASH_FS_LABEL;
	available = DASH_SAFE_R - (HEADER_TAB_X + HEADER_TAB_W * dashVisibleSectionCount() + HEADER_TAB_PAD + 8.0f);
	width = dashTextWidth(t, scale);
	if (width > available && available > 0.0f)
		scale *= available / width;
	if (dashTheme.clock == DASH_CLOCK_SYSTEM) {
		/* OSDSYS: light grey, even weight, no emphasis */
		dashTextRight(DASH_SAFE_R, 24, t, scale, DASH_C_TEXT);
		if (d[0])
			dashTextRight(DASH_SAFE_R, 45, d, DASH_FS_CAPTION, DASH_C_TEXT2);
	} else {
		dashTextRight(DASH_SAFE_R, 22, t, scale, DASH_C_TEXT);
		if (d[0])
			dashTextRight(DASH_SAFE_R, 46, d, DASH_FS_CAPTION, DASH_C_TEXT3);
	}
}

/* Footer clock: left end of the hint row. Returns the width used (0 when
 * the clock is not in the footer). */
static float footerClockWidth(char *t, int tsize, char *d, int dsize)
{
	if (dashTheme.clock == DASH_CLOCK_OFF || dashTheme.clock_pos != DASH_CLOCKPOS_FOOTER)
		return 0.0f;
	clockText(t, tsize, d, dsize);
	return dashTextWidth(t, DASH_FS_BODY) + (d[0] ? 14.0f + dashTextWidth(d, DASH_FS_SMALL) : 0.0f);
}

static void drawFooterClock(float y, const char *t, const char *d)
{
	float x = DASH_SAFE_L;

	if (dashTheme.clock == DASH_CLOCK_SYSTEM && d[0]) {
		/* OSDSYS order: date, then time */
		x = dashTextS(x, y + 4.5f, d, DASH_FS_SMALL, DASH_C_TEXT2) + 14.0f;
		dashTextS(x, y + 3.5f, t, DASH_FS_BODY, DASH_C_TEXT);
	} else {
		x = dashTextS(x, y + 3.5f, t, DASH_FS_BODY, DASH_C_TEXT) + 14.0f;
		if (d[0])
			dashTextS(x, y + 4.5f, d, DASH_FS_SMALL, DASH_C_TEXT3);
	}
}

void dashDrawHeader(int section, float section_anim)
{
	const float tab_w = HEADER_TAB_W, tab_x0 = HEADER_TAB_X, tab_y = 20.0f;
	int vcount = dashVisibleSectionCount();
	int vindex = dashVisibleSectionIndex(section);
	float target_x = tab_x0 + vindex * tab_w;
	int i;

	(void)section_anim;
	if (tab_anim_x < 0.0f)
		tab_anim_x = target_x;
	/* Keep the header selector on one monotonic path. The previous 16 Hz
	 * sinusoidal overshoot moved a thin highlight by sub-pixels every frame,
	 * which reads as top-edge/video flicker on interlaced CRT output. */
	tab_anim_x = dashApproach(tab_anim_x, target_x, 14.0f * dashAnimSpeed(), dashFrameDelta());
	if (fabsf(target_x - tab_anim_x) < 0.25f)
		tab_anim_x = target_x;

	dashAccentGlow(51, 36, 22, 18, 1.2f);
	dashIcon(DI_APP, 38, 23, 26, DASH_C_ACCENT);

	dashDrawPanelBox(tab_x0 - 40, tab_y - 2, tab_w * vcount + 80, 44, 14);
	if (DASH_C_PANEL.a && !DASH_C_BORDER.a && dashTheme.glow)
		dashPanelBorder(tab_x0 - 40, tab_y - 2, tab_w * vcount + 80, 44, 14, DC(255, 255, 255, 18));
	dashAccentGlow(tab_anim_x + tab_w * 0.5f, tab_y + 20, tab_w * 0.75f, 26, 1.2f);
	if (dashTheme.highlight != DASH_HL_TEXT)
		dashPanel(tab_anim_x + 3, tab_y + 1, tab_w - 6, 38, 11, dashColorA(DASH_C_SELECT, 0.65f));
	dashRect(tab_anim_x + 17, tab_y + 38, tab_w - 34, dashTheme.highlight == DASH_HL_TEXT ? 2 : 3, DASH_C_ACCENT);

	for (i = 0; i < vcount; i++) {
		int actual = dashVisibleSectionAt(i);
		float cx = tab_x0 + i * tab_w + tab_w * 0.5f;
		float dist = fabsf((tab_anim_x - (tab_x0 + i * tab_w)) / tab_w);
		float k = dashClampf(1.0f - dist, 0.0f, 1.0f);
		DashColor c = dashColorLerp(DASH_C_TEXT3, DASH_C_SELTEXT, k);
		dashIcon(dashSectionIcon(actual), cx - 8, tab_y + 3, 16, c);
		const char *name = localizeDashboardText(dashSectionLabel(actual));
		float scale = DASH_FS_CAPTION * 0.92f;
		if (dashTextWidth(name, scale) <= tab_w - 8)
			dashTextCenter(cx, tab_y + 20, name, scale, c);
		else
			dashTextFit(cx - (tab_w - 8) * 0.5f, tab_y + 20, tab_w - 8, name, scale, c);
	}
	{
		float strip_l = tab_x0 - 40, strip_r = tab_x0 + tab_w * vcount + 40;
		float lw = buttonWidth(-1, "L1"), rw = buttonWidth(-1, "R1");
		drawButton(strip_l + (40.0f - lw) * 0.5f, tab_y + 9, -1, "L1");
		drawButton(strip_r - 40.0f + (40.0f - rw) * 0.5f, tab_y + 9, -1, "R1");
	}

	dashDrawClock();
}

void dashDrawSectionTitle(float x, float y, const char *title, const char *subtitle)
{
	dashTextS(x, y, title, DASH_FS_TITLE, DASH_C_TEXT);
	if (subtitle != NULL && subtitle[0])
		dashTextS(x + dashTextWidth(title, DASH_FS_TITLE) + 12, y + 9, subtitle, DASH_FS_SMALL, DASH_C_TEXT3);
}

void dashDrawEmptyState(float cx, float cy, int icon, const char *title, const char *body)
{
	dashAccentGlow(cx, cy - 34, 70, 60, 0.85f);
	dashIcon(icon, cx - 26, cy - 60, 52, dashColorA(DASH_C_TEXT2, 0.8f));
	dashTextCenter(cx, cy + 4, title, DASH_FS_LABEL, DASH_C_TEXT);
	if (body != NULL) {
		float w = 400.0f;
		dashTextWrap(cx - w * 0.5f + (w - (dashTextWidth(body, DASH_FS_SMALL) < w ? dashTextWidth(body, DASH_FS_SMALL) : w)) * 0.5f,
		             cy + 34, w, 3, body, DASH_FS_SMALL, DASH_C_TEXT2, 22);
	}
}

void dashDrawScrollbar(float x, float y, float h, int first, int visible, int total)
{
	float th, ty;

	if (total <= visible || total <= 0)
		return;
	dashPanel(x, y, 5, h, 2.5f, dashColorA(DASH_C_TEXT3, 0.22f));
	th = h * visible / (float)total;
	if (th < 18)
		th = 18;
	ty = y + (h - th) * first / (float)(total - visible);
	dashPanel(x, ty, 5, th, 2.5f, dashColorA(DASH_C_ACCENT, 0.75f));
}

void dashDrawFocusFrame(float x, float y, float w, float h, float r, float strength)
{
	float pulse = 0.82f + 0.18f * dashSinf(dashTime() * 4.2f);
	DashColor edge = dashColorLighten(DASH_C_ACCENT, 0.45f);

	if (strength <= 0.01f)
		return;
	if (dashTheme.glow) {
		DashColor g = DASH_C_ACCENT;
		g.a = (u8)(120 * strength * pulse);
		dashGfxBlend(DASH_BLEND_ADD);
		dashGlowRect(x, y, w, h, 16.0f, g);
		dashGfxBlend(DASH_BLEND_NORMAL);
	}
	edge.a = (u8)(230 * strength);
	dashPanelBorder(x - 2, y - 2, w + 4, h + 4, r + 2, edge);
	if (!dashTheme.glow) {
		/* no glow: a second, fainter outline keeps focus obvious on a CRT */
		/* Avoid animated shimmer from the second fine line on interlaced CRTs. */
		edge.a = (u8)(65 * strength);
		dashPanelBorder(x - 4, y - 4, w + 8, h + 8, r + 4, edge);
	}
}

/* A panel's outline belongs to the panel: with Panels opacity 0 (transparent)
 * there is no box left to outline, so no stray borders float on the scene. */
static int panelBorderVisible(void)
{
	return DASH_C_PANEL.a && DASH_C_BORDER.a;
}

void dashDrawPanelBox(float x, float y, float w, float h, float r)
{
	if (DASH_C_PANEL.a)
		dashPanel(x, y, w, h, r, DASH_C_PANEL);
	if (panelBorderVisible())
		dashPanelBorder(x, y, w, h, r, DASH_C_BORDER);
}

void dashDrawCard(float x, float y, float w, float h, float r)
{
	DashColor c = DASH_C_PANEL;

	/* cards sit on top of the background without a list panel, so they
	 * get a little more body than list panels */
	if (c.a) {
		c.a = (u8)((c.a + 30 > 230) ? 230 : c.a + 30);
		dashPanel(x, y, w, h, r, c);
	}
	if (panelBorderVisible())
		dashPanelBorder(x, y, w, h, r, DASH_C_BORDER);
}

void dashDrawSelection(float x, float y, float w, float h, float r, float strength)
{
	DashColor fill = dashColorA(DASH_C_SELECT, strength);
	DashColor edge = dashColorA(DASH_C_SELEDGE, strength);

	switch (dashTheme.highlight) {
		case DASH_HL_BAR:
			fill.a = (u8)(fill.a * 0.55f);
			dashPanel(x, y, w, h, r, fill);
			dashRect(x, y + 3, 3, h - 6, dashColorA(DASH_C_ACCENT, strength));
			break;
		case DASH_HL_OUTLINE:
			dashPanelBorder(x, y, w, h, r, dashColorA(DASH_C_ACCENT, strength * 0.9f));
			break;
		case DASH_HL_TEXT:
			/* OSDSYS: the selection is only a colour swap of the text */
			break;
		default:
			dashPanel(x, y, w, h, r, fill);
			dashPanelBorder(x, y, w, h, r, edge);
			break;
	}
}

void dashAccentGlow(float cx, float cy, float rx, float ry, float strength)
{
	DashColor c = DASH_C_ACCENT;

	if (!dashTheme.glow)
		return;
	c.a = (u8)dashClampf(60.0f * strength, 0.0f, 255.0f);
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlow(cx, cy, rx, ry, c);
	dashGfxBlend(DASH_BLEND_NORMAL);
}

DashColor dashItemColor(int selected)
{
	return selected ? DASH_C_SELTEXT : DASH_C_TEXT2;
}

void dashDrawSpinner(float cx, float cy, float r, DashColor c)
{
	int i;
	/* wall clock, so it keeps turning in blocking waits and modal loops */
	float t = (float)(Timer() % 100000u) / 1000.0f * 7.0f;

	for (i = 0; i < 10; i++) {
		float a = i * 0.6283f;
		float k = fmodf(t - i + 100.0f, 10.0f) / 10.0f;
		dashGlow(cx + dashCosf(a) * r, cy + dashSinf(a) * r, 5.0f, 5.0f, dashColorA(c, 1.0f - k));
	}
}

//--------------------------------------------------------------
// Toasts
//--------------------------------------------------------------
#define TOAST_MAX 3
typedef struct
{
	int kind;
	char msg[160];
	float t;
} Toast;

static Toast toasts[TOAST_MAX];
static int ntoasts;
static float focus_pop = 1.0f;
static float focus_since;

void dashUiFocusPoke(void)
{
	focus_since = dashTime();
	if (dashCfg.anim > 0)
		focus_pop = 0.0f;
}

float dashUiFocusAge(void)
{
	return dashTime() - focus_since;
}

float dashUiFocusPop(void)
{
	float k = dashEaseOutCubic(dashClampf(focus_pop, 0.0f, 1.0f));
	return 0.9f + 0.1f * k;
}

void dashUiInit(void)
{
	ntoasts = 0;
	focus_since = dashTime();
	last_input_ms = Timer();
}

void dashToast(int kind, const char *msg)
{
	if (ntoasts == TOAST_MAX) {
		memmove(&toasts[0], &toasts[1], sizeof(Toast) * (TOAST_MAX - 1));
		ntoasts--;
	}
	toasts[ntoasts].kind = kind;
	toasts[ntoasts].t = 0.0f;
	dashStrCopy(toasts[ntoasts].msg, msg, sizeof(toasts[ntoasts].msg));
	ntoasts++;
	dashSoundPlay(DASH_SND_MESSAGE);
}

void dashUiUpdate(float dt)
{
	int i;

	focus_pop += dt * 6.0f;
	if (focus_pop > 1.0f)
		focus_pop = 1.0f;
	for (i = 0; i < ntoasts; i++)
		toasts[i].t += dt;
	while (ntoasts > 0 && toasts[0].t > 3.6f) {
		memmove(&toasts[0], &toasts[1], sizeof(Toast) * (TOAST_MAX - 1));
		ntoasts--;
	}
}

void dashDrawToasts(void)
{
	int i;
	float y = 354.0f;

	for (i = ntoasts - 1; i >= 0; i--) {
		Toast *t = &toasts[i];
		float in = dashClampf(t->t / 0.25f, 0.0f, 1.0f);
		float out = dashClampf((3.6f - t->t) / 0.4f, 0.0f, 1.0f);
		float a = dashEaseOutCubic(in) * out;
		float natural = dashTextWidth(t->msg, DASH_FS_SMALL) + 58.0f;
		float w = dashClampf(natural, 190.0f, 318.0f);
		float x = DASH_SAFE_R - w + 20.0f * (1.0f - dashEaseOutCubic(in));
		int icon = DI_INFO;
		DashColor ic = DASH_C_ACCENT;

		if (t->kind == DASH_TOAST_OK)
			icon = DI_CHECK, ic = DASH_C_OK;
		else if (t->kind == DASH_TOAST_WARN)
			icon = DI_WARNING, ic = DASH_C_WARM;
		else if (t->kind == DASH_TOAST_ERROR)
			icon = DI_WARNING, ic = DASH_C_DANGER;
		dashGfxPushAlpha(a);
		dashShadow(x, y, w, 32, 10, 0.6f);
		dashPanel(x, y, w, 32, 12, DC(DASH_C_PANEL_HI.r, DASH_C_PANEL_HI.g, DASH_C_PANEL_HI.b, 235));
		if (DASH_C_BORDER.a)
			dashPanelBorder(x, y, w, 32, 12, DASH_C_BORDER);
		dashRect(x + 3, y + 7, 3, 18, ic);
		dashIcon(icon, x + 14, y + 7, 18, ic);
		dashTextScrollTimed(x + 40, y + 9.5f, w - 52, t->msg, DASH_FS_SMALL, DASH_C_TEXT, t->t);
		dashGfxPopAlpha();
		y -= 40.0f;
	}
}

//--------------------------------------------------------------
// Modal helpers
//--------------------------------------------------------------
static void modalBackdrop(float k)
{
	DashColor c = dashColorScale(dashTheme.bg2, 0.4f);
	c.a = (u8)(150 * k);
	dashRect(0, 0, DASH_VW, DASH_VH, c);
}

static void modalPanel(float x, float y, float w, float h, float k)
{
	/* modal surface: the panel colour lifted and made nearly opaque, so
	 * dialogs stay readable over any background */
	DashColor top = dashColorLighten(DASH_C_PANEL, 0.14f), bot = dashColorLighten(DASH_C_PANEL, 0.06f);
	DashColor edge = dashColorA(DASH_C_ACCENT, 0.24f);

	top.a = bot.a = 245;
	dashShadow(x, y, w, h, 26, 0.9f * k);
	dashPanelGradV(x, y, w, h, 18, top, bot);
	dashPanelBorder(x, y, w, h, 18, edge);
	edge.a = (u8)(dashTheme.glow ? 90 : 60);
	dashRect(x + 18, y + 2, w - 36, 1.5f, edge);
}

static void modalHintRow(float x, float y, int ok_icon, const char *ok, int back_icon, const char *back)
{
	if (ok != NULL) {
		x += drawButton(x, y, ok_icon, NULL) + 8.0f;
		x = dashTextS(x, y + 4.5f, ok, DASH_FS_SMALL, DASH_C_TEXT2) + 24.0f;
	}
	if (back != NULL) {
		x += drawButton(x, y, back_icon, NULL) + 8.0f;
		dashTextS(x, y + 4.5f, back, DASH_FS_SMALL, DASH_C_TEXT2);
	}
}

/* Shared with the other dashboard dialogs (dash_widgets.c). */
void dashModalBackdrop(float k)
{
	modalBackdrop(k);
}

void dashModalPanel(float x, float y, float w, float h, float k)
{
	modalPanel(x, y, w, h, k);
}

void dashModalHintRow(float x, float y, int ok_icon, const char *ok, int back_icon, const char *back)
{
	modalHintRow(x, y, ok_icon, ok, back_icon, back);
}

float dashDrawButton(float x, float y, int icon, const char *key)
{
	return drawButton(x, y, icon, key);
}

/* Optional live preview for the next dashModalMenu(): called whenever the
 * highlighted row changes, with a light backdrop so the effect is visible. */
static void (*modal_preview)(int index);

void dashModalSetPreview(void (*fn)(int index))
{
	modal_preview = fn;
}

/* L2/R2 jump only to actionable rows. Never focus disabled actions,
 * including a disabled initial selection or a menu without usable entries. */
static int modalEnabledEdge(const DashMenuItem *items, int n, int from_end)
{
	int i;
	if (items == NULL || n <= 0)
		return -1;
	for (i = 0; i < n; i++) {
		int index = from_end ? n - 1 - i : i;
		if (!items[index].disabled)
			return index;
	}
	return -1;
}

int dashModalMenu(const char *title, const char *subtitle, const DashMenuItem *items, int n, int initial)
{
	int sel = initial, top = 0, i, previewed = -1;
	void (*preview)(int) = modal_preview;
	/* Eight subtitled rows left only 3 logical pixels above the popup.
	 * Keep a full safe margin for rounded corners on 4:3/interlaced TVs. */
	const int max_rows = 7;
	float sel_anim, open = 0.0f;
	float w = 400.0f, row_h = 38.0f;
	int rows = (n < max_rows) ? n : max_rows;
	float h = 76.0f + rows * row_h + 44.0f + (subtitle ? 18.0f : 0.0f);
	float x = (DASH_VW - w) * 0.5f, y;
	DashInput in;

	if (items == NULL || n <= 0) {
		modal_preview = NULL;
		return -1;
	}
	if (sel < 0 || sel >= n || items[sel].disabled)
		sel = modalEnabledEdge(items, n, 0);
	if (sel < 0) {
		modal_preview = NULL;
		return -1;
	}
	sel_anim = (float)sel;
	y = (DASH_VH - h) * 0.5f;
	modal_preview = NULL;
	if (preview != NULL)
		x = DASH_SAFE_R - w; /* keep the left half of the screen visible */
	dashInputFlush();

	while (1) {
		dashInputPoll(&in);
		if (dashDashboardPending())
			return -1;
		if (in.pressed & DASH_IN_UP) {
			int s = sel;
			do
				s = (s > 0) ? s - 1 : n - 1;
			while (items[s].disabled && s != sel);
			if (s != sel)
				dashSoundPlay(DASH_SND_CURSOR);
			sel = s;
		}
		if (in.pressed & DASH_IN_DOWN) {
			int s = sel;
			do
				s = (s < n - 1) ? s + 1 : 0;
			while (items[s].disabled && s != sel);
			if (s != sel)
				dashSoundPlay(DASH_SND_CURSOR);
			sel = s;
		}
		if (in.pressed & DASH_IN_L2) {
			int first = modalEnabledEdge(items, n, 0);
			if (first >= 0 && sel != first) {
				dashSoundPlay(DASH_SND_CURSOR);
				sel = first;
			}
		}
		if (in.pressed & DASH_IN_R2) {
			int last = modalEnabledEdge(items, n, 1);
			if (last >= 0 && sel != last) {
				dashSoundPlay(DASH_SND_CURSOR);
				sel = last;
			}
		}
		if ((in.pressed & DASH_IN_OK) && !items[sel].disabled) {
			dashSoundPlay(DASH_SND_CONFIRM);
			dashInputFlush();
			return sel;
		}
		if (in.pressed & (DASH_IN_BACK | DASH_IN_MENU)) {
			dashSoundPlay(DASH_SND_CANCEL);
			dashInputFlush();
			return -1;
		}
		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;
		if (sel_anim < top || sel_anim >= top + rows)
			sel_anim = (float)sel;
		if (preview != NULL) {
			if (sel != previewed) {
				preview(sel);
				previewed = sel;
			}
		}

		float dt = dashVisualUpdate();
		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), dt);
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		{
			float oe = dashEaseOutCubic(open);

			dashRenderScene();
			modalBackdrop(oe * (preview != NULL ? 0.25f : 1.0f));
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			modalPanel(x, y, w, h, oe);
		}
		dashTextS(x + 24, y + 18, title, DASH_FS_LABEL, DASH_C_TEXT);
		{
			float ly = y + 54;
			if (subtitle) {
				dashTextFit(x + 24, y + 44, w - 48, subtitle, DASH_FS_CAPTION, DASH_C_TEXT3);
				ly += 16;
			}
			dashGfxScissor(x + 8, ly - 2, w - 16, rows * row_h + 4);
			{
				float fy = ly + (sel_anim - top) * row_h;
				dashDrawSelection(x + 14, fy + 2, w - 28, row_h - 4, 10, 1.0f);
			}
			for (i = top; i < n && i < top + rows; i++) {
				float ry = ly + (i - top) * row_h;
				DashColor c = items[i].disabled ? DASH_C_TEXT3 : dashItemColor(i == sel);
				float tx = x + 28;
				dashGfxPushAlpha(items[i].disabled ? DASH_DISABLED_ALPHA : 1.0f);
				if (items[i].icon >= 0) {
					dashIcon(items[i].icon, x + 26, ry + 6, 18, c);
					tx = x + 54;
				}
				{
					/* Label first (natural width, at most 60% of the row);
					 * the detail gets only the space that is left, so the
					 * two can never overlap. */
					float avail = w - (tx - x) - 30.0f, label_w = avail;
					if (items[i].detail) {
						float dw = dashTextWidth(items[i].detail, DASH_FS_SMALL), detail_w;
						label_w = dashTextWidth(items[i].label, DASH_FS_BODY * 0.92f);
						if (label_w > avail * 0.6f)
							label_w = avail * 0.6f;
						detail_w = avail - label_w - 14.0f;
						if (dw <= detail_w)
							dashTextRight(x + w - 28, ry + 8, items[i].detail, DASH_FS_SMALL, DASH_C_TEXT3);
						else if (i == sel)
							dashTextScroll(x + w - 28 - detail_w, ry + 8, detail_w, items[i].detail, DASH_FS_SMALL, DASH_C_TEXT3);
						else
							dashTextFit(x + w - 28 - detail_w, ry + 8, detail_w, items[i].detail, DASH_FS_SMALL, DASH_C_TEXT3);
					}
					dashTextFit(tx, ry + 7, label_w, items[i].label, DASH_FS_BODY * 0.92f, c);
				}
				dashGfxPopAlpha();
			}
			dashGfxScissorReset();
			dashDrawScrollbar(x + w - 12, ly, rows * row_h - 4, top, rows, n);
			modalHintRow(x + 24, y + h - 36, dashOkIcon(), "Select", dashBackIcon(), "Back");
		}
		dashGfxPopOffset();
		dashGfxPopAlpha();
		dashGfxPresent();
	}
}

int dashModalChoice(const char *title, const char *const *options, int n, int current)
{
	DashMenuItem items[24];
	int i;

	if (options == NULL || n <= 0)
		return -1;
	if (n > 24)
		n = 24;
	for (i = 0; i < n; i++) {
		items[i].icon = (i == current) ? DI_CHECK : -1;
		items[i].label = options[i];
		items[i].detail = NULL;
		items[i].disabled = 0;
	}
	return dashModalMenu(title, NULL, items, n, current);
}

static int modalButtons(const char *title, const char *body, const char *detail, int icon, DashColor icon_col,
                        const char *ok_label, const char *cancel_label, int default_sel)
{
	int sel = default_sel;
	float open = 0.0f, sel_anim = (float)default_sel, title_age = 0.0f;
	float w = 440.0f, h;
	float x = (DASH_VW - w) * 0.5f, y;
	int body_lines;
	DashInput in;

	body_lines = 1;
	if (body) {
		const char *p;
		float tw = dashTextWidth(body, DASH_FS_BODY * 0.9f);
		body_lines = (int)(tw / (w - 60)) + 1;
		for (p = body; *p; p++)
			if (*p == '\n')
				body_lines++;
		if (body_lines > 6)
			body_lines = 6;
	}
	h = 92.0f + body_lines * 24.0f + (detail ? 26.0f : 0.0f) + 50.0f;
	y = (DASH_VH - h) * 0.5f;
	dashInputFlush();

	while (1) {
		dashInputPoll(&in);
		if (dashDashboardPending())
			return 0;
		if (cancel_label != NULL && (in.pressed & (DASH_IN_LEFT | DASH_IN_RIGHT))) {
			sel ^= 1;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_OK) {
			dashSoundPlay(cancel_label == NULL || sel == 0 ? DASH_SND_CONFIRM : DASH_SND_CANCEL);
			dashInputFlush();
			return (cancel_label == NULL) ? 1 : (sel == 0);
		}
		if (in.pressed & DASH_IN_BACK) {
			dashSoundPlay(DASH_SND_CANCEL);
			dashInputFlush();
			return (cancel_label == NULL) ? 1 : 0;
		}
		float dt = dashVisualUpdate();
		title_age += dt;
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		sel_anim = dashApproach(sel_anim, (float)sel, 20.0f * dashAnimSpeed(), dt);
		{
			float oe = dashEaseOutCubic(open);

			dashRenderScene();
			modalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			modalPanel(x, y, w, h, oe);
		}
		if (dashTheme.glow) {
			dashGfxBlend(DASH_BLEND_ADD);
			dashGlow(x + 44, y + 42, 34, 34, dashColorA(icon_col, 0.35f));
			dashGfxBlend(DASH_BLEND_NORMAL);
		}
		dashIcon(icon, x + 26, y + 24, 36, icon_col);
		dashTextScrollTimed(x + 76, y + 28, w - 106, title, DASH_FS_LABEL, DASH_C_TEXT, title_age);
		if (body)
			dashTextWrap(x + 30, y + 78, w - 60, 6, body, DASH_FS_BODY * 0.9f, DASH_C_TEXT2, 24);
		if (detail) {
			float dy = y + 84 + body_lines * 24.0f;
			dashTextScrollTimed(x + 30, dy, w - 60, detail, DASH_FS_CAPTION, DASH_C_TEXT3, title_age);
		}
		{
			float by = y + h - 50;
			float bw = (cancel_label != NULL) ? 150.0f : 180.0f;
			float bx0 = (cancel_label != NULL) ? x + w * 0.5f - bw - 10 : x + (w - bw) * 0.5f;
			float bx1 = x + w * 0.5f + 10;
			float fx = dashLerpf(bx0, bx1, sel_anim);

			dashPanel(bx0, by, bw, 34, 12, DC(255, 255, 255, 18));
			if (cancel_label != NULL)
				dashPanel(bx1, by, bw, 34, 12, DC(255, 255, 255, 18));
			if (sel == 0 && icon == DI_WARNING && cancel_label) {
				DashColor dz = dashColorScale(DASH_C_DANGER, 0.78f);
				dz.a = 170;
				dashPanel(fx, by, bw, 34, 12, dz);
				dashPanelBorder(fx, by, bw, 34, 12, DASH_C_SELEDGE);
			} else if (dashTheme.highlight == DASH_HL_TEXT) {
				/* buttons need a visible focus even in text-only themes */
				dashPanelBorder(fx, by, bw, 34, 12, dashColorA(DASH_C_ACCENT, 0.8f));
			} else
				dashDrawSelection(fx, by, bw, 34, 12, 1.0f);
			/* Confirmation actions use exactly the same sized glyph/badge
			 * renderer as the dashboard footer and modal hints (#158). */
			dashGfxPushAlpha(sel == 0 ? 1.0f : 0.55f);
			drawButton(bx0 + 12, by + 6, dashOkIcon(), NULL);
			dashGfxPopAlpha();
			dashTextCenter(bx0 + bw * 0.5f + 10, by + 10.0f, ok_label, DASH_FS_BODY * 0.9f, dashItemColor(sel == 0));
			if (cancel_label != NULL) {
				dashGfxPushAlpha(sel == 1 ? 1.0f : 0.55f);
				drawButton(bx1 + 12, by + 6, dashBackIcon(), NULL);
				dashGfxPopAlpha();
				dashTextCenter(bx1 + bw * 0.5f + 10, by + 10.0f, cancel_label, DASH_FS_BODY * 0.9f, dashItemColor(sel == 1));
			}
		}
		dashGfxPopOffset();
		dashGfxPopAlpha();
		dashGfxPresent();
	}
}

int dashModalConfirm(const char *title, const char *body, const char *ok_label, int danger)
{
	return modalButtons(title, body, NULL, danger ? DI_WARNING : DI_INFO,
	                    danger ? DASH_C_WARM : DASH_C_ACCENT, ok_label ? ok_label : "OK", "Cancel", danger ? 1 : 0);
}

void dashModalMessage(int icon, const char *title, const char *body, const char *detail)
{
	DashColor c = (icon == DI_WARNING) ? DASH_C_WARM : DASH_C_ACCENT;
	modalButtons(title, body, detail, icon, c, "OK", NULL, 0);
}

//--------------------------------------------------------------
// Loading overlay
//--------------------------------------------------------------
static int loading_active;
static char loading_title[64];
static char loading_status[128];
static char loading_remaining[64];
static float loading_fraction;
static int loading_done, loading_total;
static u64 loading_last_draw;
static float loading_open;
static int loading_cancel;
static int loading_file_operation;

int dashLoadingActive(void)
{
	return loading_active;
}

static void loadingDraw(void)
{
	float w = loading_file_operation ? 480.0f : 420.0f;
	float h = loading_file_operation ? (loading_cancel ? 210.0f : 174.0f) : loading_remaining[0] ? 158.0f : loading_cancel ? 150.0f : 128.0f;
	float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	float bar_y = y + (loading_file_operation ? 142.0f : loading_remaining[0] ? 126.0f : 102.0f);
	float dt = dashVisualUpdate();

	loading_open = dashApproach(loading_open, 1.0f, 12.0f, dt);
	dashRenderScene();
	modalBackdrop(loading_open);
	dashGfxPushAlpha(loading_open);
	modalPanel(x, y, w, h, loading_open);
	if (loading_file_operation) {
		char line[64];
		dashTextFit(x + 24, y + 24, w - 48, localizeDashboardText(loading_title), DASH_FS_LABEL, DASH_C_TEXT);
		dashTextFit(x + 24, y + 62, w - 48, loading_status, DASH_FS_BODY, DASH_C_TEXT);
		if (loading_remaining[0])
			dashTextFit(x + 24, y + 100, w - 48, loading_remaining, DASH_FS_SMALL, DASH_C_TEXT2);
		else if (loading_total > 0) {
			snprintf(line, sizeof(line), localizeDashboardFormat("Completed items: %d / %d"), loading_done, loading_total);
			dashTextFit(x + 24, y + 100, w - 48, line, DASH_FS_SMALL, DASH_C_TEXT2);
		}
	} else {
		dashGfxBlend(DASH_BLEND_ADD);
		dashDrawSpinner(x + 46, y + 50, 16, DASH_C_ACCENT);
		dashGfxBlend(DASH_BLEND_NORMAL);
		dashTextFit(x + 84, y + 26, w - 110, localizeDashboardText(loading_title), DASH_FS_LABEL, DASH_C_TEXT);
		dashTextFit(x + 84, y + 58, w - 110, loading_status, DASH_FS_SMALL, DASH_C_TEXT2);
		if (loading_remaining[0])
			dashTextFit(x + 84, y + 84, w - 110, loading_remaining, DASH_FS_SMALL, DASH_C_TEXT2);
	}
	if (loading_total > 0) {
		float p = dashClampf(loading_done / (float)loading_total, 0.0f, 1.0f);
		/* The final frame is followed by closing the overlay. It must show
		 * completion rather than disappear with a partly filled bar. */
		loading_fraction = p >= 1.0f ? 1.0f : dashApproach(loading_fraction, p, 14.0f, dt);
		dashPanel(x + 30, bar_y, w - 60, 8, 4, dashColorA(DASH_C_TEXT3, 0.3f));
		if (loading_fraction > 0.0f)
			dashPanel(x + 30, bar_y, (w - 60) * loading_fraction, 8, 4, DASH_C_ACCENT);
	}
	if (loading_cancel)
		modalHintRow(x + 24, y + h - 34, -1, NULL, dashBackIcon(), "Continue in background");
	dashGfxPopAlpha();
	dashGfxPresent();
	loading_last_draw = Timer();
}

static void loadingBegin(const char *title, int file_operation)
{
	loading_file_operation = file_operation;
	loading_active = 1;
	loading_open = 0.3f;
	loading_cancel = 0;
	dashStrCopy(loading_title, title, sizeof(loading_title));
	loading_status[0] = '\0';
	loading_remaining[0] = '\0';
	loading_fraction = 0.0f;
	loading_done = loading_total = 0;
	loadingDraw();
}

void dashLoadingBegin(const char *title)
{
	loadingBegin(title, 0);
}

void dashFileLoadingBegin(const char *title)
{
	loadingBegin(title, 1);
}

void dashLoadingSetCancel(int cancelable)
{
	loading_cancel = cancelable;
}

static void loadingProgress(const char *status, int done, int total)
{
	int quiet = isStartupDriverStatusQuiet();
	loading_remaining[0] = '\0';

	/* Device retry loops can update the overlay directly, bypassing drawMsg.
	 * Keep branded startup progress, but hide its driver stages and counts. */
	if (status != NULL || quiet)
		dashStrCopy(loading_status, quiet ? "" : status, sizeof(loading_status));
	loading_done = quiet ? 0 : done;
	loading_total = quiet ? 0 : total;
}

void dashLoadingStatus(const char *status, int done, int total)
{
	if (dashWorkerOnThread()) {
		/* driver loads started by a background job report here */
		dashWorkerStatus(status, done, total);
		return;
	}
	loadingProgress(status, done, total);
	if (!loading_active) {
		loading_file_operation = 0;
		/* one-shot status (e.g. a driver load started by a legacy path) */
		loading_active = 1;
		loading_open = 0.6f;
		dashStrCopy(loading_title, "Please wait", sizeof(loading_title));
		loadingDraw();
		loading_active = 0;
		return;
	}
	if (Timer() - loading_last_draw >= 30)
		loadingDraw();
}

int DashFileProgress(const char *action, const char *name, u64 remaining, u64 total)
{
	int done, changed;
	if (!DashIsActive() || dashWorkerOnThread()) return 0;
	if (!loading_active) dashFileLoadingBegin(action);
	changed = !loading_file_operation ||
	          strncmp(loading_status, name ? name : "", sizeof(loading_status) - 1) ||
	          strncmp(loading_title, action, sizeof(loading_title) - 1) ||
	          (remaining > 0 && loading_total > 0 && loading_done >= loading_total);
	loading_file_operation = 1;
	if (changed)
		loading_fraction = 0.0f;
	dashStrCopy(loading_title, action, sizeof(loading_title));
	dashStrCopy(loading_status, name ? name : "", sizeof(loading_status));
	if (remaining > total) remaining = total;
	snprintf(loading_remaining, sizeof(loading_remaining), localizeDashboardFormat("Remaining size: %.2f MB"),
	         (double)remaining / (1024.0 * 1024.0));
	done = total ? (int)((1.0 - (double)remaining / (double)total) * 10000.0) : 10000;
	loading_done = done;
	loading_total = 10000;
	if (changed || Timer() - loading_last_draw >= 16 || remaining == 0) loadingDraw();
	return 1;
}

void DashFileDeleteProgress(const char *name)
{
	if (!DashIsActive() || dashWorkerOnThread() || !loading_active) return;
	loading_file_operation = 1;
	if (loading_remaining[0]) {
		loading_done = loading_total = 0;
		loading_fraction = 0.0f;
	}
	loading_remaining[0] = '\0';
	dashStrCopy(loading_title, "Deleting file", sizeof(loading_title));
	dashStrCopy(loading_status, name, sizeof(loading_status));
	if (Timer() - loading_last_draw >= 16) loadingDraw();
}

void dashLoadingFrame(const char *status, int done, int total)
{
	loadingProgress(status, done, total);
	loadingDraw();
}

void DashStorageProbeStatus(const char *status)
{
	if (!DashIsActive() || isStartupDriverStatusQuiet())
		return;
	if (dashWorkerOnThread()) {
		dashWorkerStatus(status, 0, 0);
		return;
	}
	/* Unlike the throttled status setter, present this before the RPC.
	 * Never create an overlay in classic code or from a background job. */
	if (loading_active)
		dashLoadingFrame(status, 0, 0);
}

void dashLoadingEnd(void)
{
	loading_active = 0;
	loading_file_operation = 0;
	loading_cancel = 0;
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_ui.c
//--------------------------------------------------------------
