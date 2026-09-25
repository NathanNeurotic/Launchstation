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

extern u32 paddata;

static u64 last_input_ms;
static u32 held_prev;

//--------------------------------------------------------------
// Input
//--------------------------------------------------------------
void dashInputPoll(DashInput *in)
{
	u32 np;
	u32 ok_btn = swapKeys ? PAD_CROSS : PAD_CIRCLE;
	u32 back_btn = swapKeys ? PAD_CIRCLE : PAD_CROSS;

	memset(in, 0, sizeof(*in));
	waitAnyPadReady();
	if (!readpad())
		return;
	np = new_pad;
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
	if (in->any || paddata != held_prev)
		last_input_ms = Timer();
	held_prev = paddata;
}

void dashInputFlush(void)
{
	int i;

	/* swallow presses that were in flight when a screen changed */
	for (i = 0; i < 2; i++) {
		waitAnyPadReady();
		readpad_noRepeat();
	}
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

static float drawButton(float x, float y, int icon, const char *key)
{
	const float d = 22.0f;

	if (icon >= 0) {
		dashPanel(x, y, d, d, d * 0.5f, DC(10, 12, 22, 220));
		dashPanelBorder(x, y, d, d, d * 0.5f, DC(255, 255, 255, 40));
		dashIcon(icon, x + 4.5f, y + 4.5f, d - 9.0f, buttonColor(icon));
		return d;
	} else {
		float tw = dashTextWidth(key, DASH_FS_CAPTION);
		float w = tw + 14.0f;
		dashPanel(x, y, w, d, 7.0f, DC(10, 12, 22, 220));
		dashPanelBorder(x, y, w, d, 7.0f, DC(255, 255, 255, 40));
		dashText(x + 7.0f, y + 2.5f, key, DASH_FS_CAPTION, DASH_C_TEXT);
		return w;
	}
}

void dashDrawHints(const DashHint *hints, int n)
{
	float total = 0.0f, x;
	float y = 400.0f;
	int i;

	for (i = 0; i < n; i++) {
		float bw = (hints[i].button >= 0) ? 22.0f : dashTextWidth(hints[i].key, DASH_FS_CAPTION) + 14.0f;
		total += bw + 6.0f + dashTextWidth(hints[i].label, DASH_FS_SMALL) + 18.0f;
	}
	x = DASH_SAFE_R - total + 12.0f;
	if (x < DASH_SAFE_L)
		x = DASH_SAFE_L;
	dashRectGradH(x - 40, y - 6, DASH_SAFE_R - x + 60, 34, DC(4, 6, 14, 0), DC(4, 6, 14, 150));
	for (i = 0; i < n; i++) {
		x += drawButton(x, y, hints[i].button, hints[i].key) + 6.0f;
		x = dashTextS(x, y + 1.5f, hints[i].label, DASH_FS_SMALL, DASH_C_TEXT2) + 18.0f;
	}
}

//--------------------------------------------------------------
// Header
//--------------------------------------------------------------
static float tab_anim_x = -1.0f;

void dashDrawClock(void)
{
	char t[24], d[24];

	menuTitleGetClockText(t, sizeof(t), d, sizeof(d));
	/* "HH:MM:SS" or "HH:MM:SS PM" -> drop the seconds */
	if (strlen(t) >= 8 && t[2] == ':' && t[5] == ':')
		memmove(t + 5, t + 8, strlen(t + 8) + 1);
	dashTextRight(DASH_SAFE_R, 22, t, DASH_FS_LABEL, DASH_C_TEXT);
	dashTextRight(DASH_SAFE_R, 46, d, DASH_FS_CAPTION, DASH_C_TEXT3);
}

void dashDrawHeader(int section, float section_anim)
{
	const float tab_w = 61.0f, tab_x0 = 110.0f, tab_y = 20.0f;
	float target_x = tab_x0 + section * tab_w;
	int i;

	(void)section_anim;
	if (tab_anim_x < 0.0f)
		tab_anim_x = target_x;
	tab_anim_x = dashApproach(tab_anim_x, target_x, 16.0f * dashAnimSpeed(), 1.0f / 60.0f);

	/* brand */
	dashGlow(51, 36, 22, 18, DC(120, 200, 255, 70));
	dashIcon(DI_APP, 38, 23, 26, DASH_C_ACCENT);

	/* tab strip */
	dashPanel(tab_x0 - 30, tab_y - 2, tab_w * DASH_SEC_COUNT + 60, 44, 14, DC(12, 15, 30, 150));
	dashPanelBorder(tab_x0 - 30, tab_y - 2, tab_w * DASH_SEC_COUNT + 60, 44, 14, DC(255, 255, 255, 18));
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlow(tab_anim_x + tab_w * 0.5f, tab_y + 20, tab_w * 0.75f, 26, DC(90, 170, 255, 70));
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashPanel(tab_anim_x + 3, tab_y + 1, tab_w - 6, 38, 11, DC(60, 110, 190, 110));
	dashRect(tab_anim_x + 16, tab_y + 38, tab_w - 32, 3, DASH_C_ACCENT);

	for (i = 0; i < DASH_SEC_COUNT; i++) {
		float cx = tab_x0 + i * tab_w + tab_w * 0.5f;
		float dist = fabsf((tab_anim_x - (tab_x0 + i * tab_w)) / tab_w);
		float k = dashClampf(1.0f - dist, 0.0f, 1.0f);
		DashColor c = dashColorLerp(DASH_C_TEXT3, DASH_C_TEXT, k);
		dashIcon(dashSectionIcon(i), cx - 8, tab_y + 3, 16, c);
		dashTextCenter(cx, tab_y + 20, dashSectionName(i), DASH_FS_CAPTION * 0.92f, c);
	}
	/* L1/R1 key pills at the strip ends */
	drawButton(tab_x0 - 26, tab_y + 9, -1, "L1");
	drawButton(tab_x0 + tab_w * DASH_SEC_COUNT + 2, tab_y + 9, -1, "R1");

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
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlow(cx, cy - 34, 70, 60, DC(80, 150, 255, 50));
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashIcon(icon, cx - 26, cy - 60, 52, DC(170, 200, 255, 170));
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
	dashPanel(x, y, 5, h, 2.5f, DC(255, 255, 255, 26));
	th = h * visible / (float)total;
	if (th < 18)
		th = 18;
	ty = y + (h - th) * first / (float)(total - visible);
	dashPanel(x, ty, 5, th, 2.5f, DC(150, 205, 255, 190));
}

void dashDrawFocusFrame(float x, float y, float w, float h, float r, float strength)
{
	float pulse = 0.82f + 0.18f * dashSinf(dashTime() * 4.2f);

	if (strength <= 0.01f)
		return;
	dashGfxBlend(DASH_BLEND_ADD);
	dashGlowRect(x, y, w, h, 16.0f, DC(70, 160, 255, (int)(120 * strength * pulse)));
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashPanelBorder(x - 2, y - 2, w + 4, h + 4, r + 2, DC(160, 225, 255, (int)(230 * strength)));
}

void dashDrawSpinner(float cx, float cy, float r, DashColor c)
{
	int i;
	float t = dashTime() * 7.0f;

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
	char msg[96];
	float t;
} Toast;

static Toast toasts[TOAST_MAX];
static int ntoasts;

void dashUiInit(void)
{
	ntoasts = 0;
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
}

void dashUiUpdate(float dt)
{
	int i;

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
	float y = 358.0f;

	for (i = ntoasts - 1; i >= 0; i--) {
		Toast *t = &toasts[i];
		float in = dashClampf(t->t / 0.25f, 0.0f, 1.0f);
		float out = dashClampf((3.6f - t->t) / 0.4f, 0.0f, 1.0f);
		float a = dashEaseOutCubic(in) * out;
		float w = dashTextWidth(t->msg, DASH_FS_SMALL) + 58.0f;
		float x = DASH_SAFE_L - 20.0f * (1.0f - dashEaseOutCubic(in));
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
		dashPanel(x, y, w, 32, 12, DC(22, 27, 48, 235));
		dashRect(x + 3, y + 7, 3, 18, ic);
		dashIcon(icon, x + 14, y + 7, 18, ic);
		dashText(x + 40, y + 5, t->msg, DASH_FS_SMALL, DASH_C_TEXT);
		dashGfxPopAlpha();
		y -= 38.0f;
	}
}

//--------------------------------------------------------------
// Modal helpers
//--------------------------------------------------------------
static void modalBackdrop(float k)
{
	dashRect(0, 0, DASH_VW, DASH_VH, DC(2, 3, 10, (int)(150 * k)));
}

static void modalPanel(float x, float y, float w, float h, float k)
{
	dashShadow(x, y, w, h, 26, 0.9f * k);
	dashPanelGradV(x, y, w, h, 18, DC(34, 40, 70, 245), DC(18, 21, 38, 245));
	dashPanelBorder(x, y, w, h, 18, DC(150, 200, 255, 60));
	dashRectGradH(x + 20, y + 1.5f, w - 40, 2, DC(160, 220, 255, 0), DC(160, 220, 255, 0));
	dashRectGrad4(x + 18, y + 2, w - 36, 2, DC(160, 220, 255, 0), DC(160, 220, 255, 0), DC(160, 220, 255, 90), DC(160, 220, 255, 90));
}

static void modalHintRow(float x, float y, int ok_icon, const char *ok, int back_icon, const char *back)
{
	x += drawButton(x, y, ok_icon, NULL) + 6.0f;
	x = dashText(x, y + 1.5f, ok, DASH_FS_SMALL, DASH_C_TEXT2) + 18.0f;
	if (back != NULL) {
		x += drawButton(x, y, back_icon, NULL) + 6.0f;
		dashText(x, y + 1.5f, back, DASH_FS_SMALL, DASH_C_TEXT2);
	}
}

int dashModalMenu(const char *title, const char *subtitle, const DashMenuItem *items, int n, int initial)
{
	int sel = initial, top = 0, i;
	const int max_rows = 8;
	float sel_anim, open = 0.0f;
	float w = 400.0f, row_h = 34.0f;
	int rows = (n < max_rows) ? n : max_rows;
	float h = 76.0f + rows * row_h + 44.0f + (subtitle ? 18.0f : 0.0f);
	float x = (DASH_VW - w) * 0.5f, y;
	DashInput in;

	if (n <= 0)
		return -1;
	if (sel < 0 || sel >= n)
		sel = 0;
	while (items[sel].disabled && sel < n - 1)
		sel++;
	sel_anim = (float)sel;
	y = (DASH_VH - h) * 0.5f;
	dashInputFlush();

	while (1) {
		dashInputPoll(&in);
		if (in.pressed & DASH_IN_UP) {
			int s = sel;
			do
				s = (s > 0) ? s - 1 : n - 1;
			while (items[s].disabled && s != sel);
			sel = s;
		}
		if (in.pressed & DASH_IN_DOWN) {
			int s = sel;
			do
				s = (s < n - 1) ? s + 1 : 0;
			while (items[s].disabled && s != sel);
			sel = s;
		}
		if (in.pressed & DASH_IN_L2)
			sel = 0;
		if (in.pressed & DASH_IN_R2)
			sel = n - 1;
		if ((in.pressed & DASH_IN_OK) && !items[sel].disabled) {
			dashInputFlush();
			return sel;
		}
		if (in.pressed & (DASH_IN_BACK | DASH_IN_MENU)) {
			dashInputFlush();
			return -1;
		}
		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;

		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), 1.0f / 60.0f);
		open = dashApproach(open, 1.0f, 14.0f * dashAnimSpeed(), 1.0f / 60.0f);

		dashRenderScene();
		modalBackdrop(open);
		dashGfxPushAlpha(open);
		dashGfxPushOffset(0, 16.0f * (1.0f - open));
		modalPanel(x, y, w, h, open);
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
				dashPanel(x + 14, fy, w - 28, row_h - 4, 10, DC(70, 130, 220, 150));
				dashPanelBorder(x + 14, fy, w - 28, row_h - 4, 10, DC(170, 225, 255, 120));
			}
			for (i = top; i < n && i < top + rows; i++) {
				float ry = ly + (i - top) * row_h;
				DashColor c = items[i].disabled ? DASH_C_TEXT3 : (i == sel ? DASH_C_TEXT : DASH_C_TEXT2);
				float tx = x + 28;
				if (items[i].icon >= 0) {
					dashIcon(items[i].icon, x + 26, ry + 6, 18, c);
					tx = x + 54;
				}
				dashTextFit(tx, ry + 4, w - (tx - x) - 30 - (items[i].detail ? 90 : 0), items[i].label, DASH_FS_BODY * 0.92f, c);
				if (items[i].detail)
					dashTextRight(x + w - 28, ry + 6, items[i].detail, DASH_FS_SMALL, DASH_C_TEXT3);
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
	DashMenuItem items[16];
	int i;

	if (n > 16)
		n = 16;
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
	float open = 0.0f, sel_anim = (float)default_sel;
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
		if (cancel_label != NULL && (in.pressed & (DASH_IN_LEFT | DASH_IN_RIGHT)))
			sel ^= 1;
		if (in.pressed & DASH_IN_OK) {
			dashInputFlush();
			return (cancel_label == NULL) ? 1 : (sel == 0);
		}
		if (in.pressed & DASH_IN_BACK) {
			dashInputFlush();
			return (cancel_label == NULL) ? 1 : 0;
		}
		open = dashApproach(open, 1.0f, 14.0f * dashAnimSpeed(), 1.0f / 60.0f);
		sel_anim = dashApproach(sel_anim, (float)sel, 20.0f * dashAnimSpeed(), 1.0f / 60.0f);

		dashRenderScene();
		modalBackdrop(open);
		dashGfxPushAlpha(open);
		dashGfxPushOffset(0, 16.0f * (1.0f - open));
		modalPanel(x, y, w, h, open);
		dashGfxBlend(DASH_BLEND_ADD);
		dashGlow(x + 44, y + 42, 34, 34, dashColorA(icon_col, 0.35f));
		dashGfxBlend(DASH_BLEND_NORMAL);
		dashIcon(icon, x + 26, y + 24, 36, icon_col);
		dashTextS(x + 76, y + 28, title, DASH_FS_LABEL, DASH_C_TEXT);
		if (body)
			dashTextWrap(x + 30, y + 78, w - 60, 6, body, DASH_FS_BODY * 0.9f, DASH_C_TEXT2, 24);
		if (detail) {
			float dy = y + 84 + body_lines * 24.0f;
			dashTextFit(x + 30, dy, w - 60, detail, DASH_FS_CAPTION, DASH_C_TEXT3);
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
			dashPanel(fx, by, bw, 34, 12, (sel == 0 && icon == DI_WARNING && cancel_label) ? DC(200, 70, 90, 170) : DC(70, 130, 220, 170));
			dashPanelBorder(fx, by, bw, 34, 12, DC(190, 230, 255, 140));
			dashIcon(dashOkIcon(), bx0 + 12, by + 8, 18, sel == 0 ? DASH_C_TEXT : DASH_C_TEXT3);
			dashTextCenter(bx0 + bw * 0.5f + 10, by + 6, ok_label, DASH_FS_BODY * 0.9f, sel == 0 ? DASH_C_TEXT : DASH_C_TEXT2);
			if (cancel_label != NULL) {
				dashIcon(dashBackIcon(), bx1 + 12, by + 8, 18, sel == 1 ? DASH_C_TEXT : DASH_C_TEXT3);
				dashTextCenter(bx1 + bw * 0.5f + 10, by + 6, cancel_label, DASH_FS_BODY * 0.9f, sel == 1 ? DASH_C_TEXT : DASH_C_TEXT2);
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
static int loading_done, loading_total;
static u64 loading_last_draw;
static float loading_open;

int dashLoadingActive(void)
{
	return loading_active;
}

static void loadingDraw(void)
{
	float w = 420.0f, h = 128.0f;
	float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;

	loading_open = dashApproach(loading_open, 1.0f, 12.0f, 1.0f / 30.0f);
	dashRenderScene();
	modalBackdrop(loading_open);
	dashGfxPushAlpha(loading_open);
	modalPanel(x, y, w, h, loading_open);
	dashGfxBlend(DASH_BLEND_ADD);
	dashDrawSpinner(x + 46, y + 50, 16, DASH_C_ACCENT);
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashTextS(x + 84, y + 26, loading_title, DASH_FS_LABEL, DASH_C_TEXT);
	dashTextFit(x + 84, y + 58, w - 110, loading_status, DASH_FS_SMALL, DASH_C_TEXT2);
	if (loading_total > 0) {
		float p = dashClampf(loading_done / (float)loading_total, 0.0f, 1.0f);
		dashPanel(x + 30, y + h - 26, w - 60, 8, 4, DC(255, 255, 255, 30));
		dashPanel(x + 30, y + h - 26, (w - 60) * p + 8, 8, 4, DASH_C_ACCENT);
	}
	dashGfxPopAlpha();
	dashGfxPresent();
	loading_last_draw = Timer();
}

void dashLoadingBegin(const char *title)
{
	loading_active = 1;
	loading_open = 0.3f;
	dashStrCopy(loading_title, title, sizeof(loading_title));
	loading_status[0] = '\0';
	loading_done = loading_total = 0;
	loadingDraw();
}

void dashLoadingStatus(const char *status, int done, int total)
{
	if (status != NULL)
		dashStrCopy(loading_status, status, sizeof(loading_status));
	loading_done = done;
	loading_total = total;
	if (!loading_active) {
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

void dashLoadingEnd(void)
{
	loading_active = 0;
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_ui.c
//--------------------------------------------------------------
