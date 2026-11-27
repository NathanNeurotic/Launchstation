//--------------------------------------------------------------
//File name:   dash_tester.c
//Controller tester: shows the button and stick state Launchstation
//reads from each pad, and logs every press and release.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include <stdio.h>
#include <string.h>

int readpad_noKBnoRepeat(void);
extern struct padButtonStatus buttons_t[];

#ifdef DS34
// Port 0 is the first PS2 pad; slot 3 is the USB pad (DS3/DS4/GIP).
static const int tester_port[2] = {0, 3};
static const char *tester_port_name[2] = {"PS2 port 1", "USB pad"};
#else
static const int tester_port[2] = {0, 1};
static const char *tester_port_name[2] = {"PS2 port 1", "PS2 port 2"};
#endif

#define TESTER_BTN_COUNT 16
#define TESTER_LOG_LINES 3

static const struct {
	const char *name;
	unsigned mask;
} tester_btn[TESTER_BTN_COUNT] = {
    {"Select", PAD_SELECT}, {"L3", PAD_L3}, {"R3", PAD_R3}, {"Start", PAD_START},
    {"Up", PAD_UP}, {"Right", PAD_RIGHT}, {"Down", PAD_DOWN}, {"Left", PAD_LEFT},
    {"L2", PAD_L2}, {"R2", PAD_R2}, {"L1", PAD_L1}, {"R1", PAD_R1},
    {"Triangle", PAD_TRIANGLE}, {"Circle", PAD_CIRCLE}, {"Cross", PAD_CROSS}, {"Square", PAD_SQUARE},
};

static unsigned tester_prev[2];
static int tester_seeded;
static char tester_log[TESTER_LOG_LINES][48];
#ifdef DS34
extern u8 gipRumbleTestLevel;
#else
static u8 gipRumbleTestLevel = 0; /* the standard build has no GIP pad: the test only sets this */
#endif
extern u32 new_pad;
static u64 tester_rumble_until;
static int tester_rumble_on;

static void tester_push(const char *text)
{
	int i;

	for (i = 0; i < TESTER_LOG_LINES - 1; i++)
		strcpy(tester_log[i], tester_log[i + 1]);

	strncpy(tester_log[TESTER_LOG_LINES - 1], text, sizeof(tester_log[0]) - 1);
	tester_log[TESTER_LOG_LINES - 1][sizeof(tester_log[0]) - 1] = 0;
}

// Pressed buttons as a 16-bit mask. PS2 pads report active-low bits.
static unsigned tester_pressed(int c)
{
	return (~buttons_t[tester_port[c]].btns) & 0xFFFF;
}

static void tester_update(void)
{
	int c, i;
	char line[48];

	for (c = 0; c < 2; c++) {
		unsigned now = tester_pressed(c);
		unsigned changed = now ^ tester_prev[c];

		if (tester_seeded) {
			for (i = 0; i < TESTER_BTN_COUNT; i++) {
				if (!(changed & tester_btn[i].mask))
					continue;
				snprintf(line, sizeof(line), "%s %s %s", c == 0 ? "P1" : "P2", tester_btn[i].name,
				         (now & tester_btn[i].mask) ? "down" : "up");
				tester_push(line);
			}
		}
		tester_prev[c] = now;
	}
	tester_seeded = 1;
}

static void tester_draw_stick(float x, float y, unsigned char hx, unsigned char vy, const char *label)
{
	const float s = 70.0f;
	float px = x + (hx * s) / 255.0f;
	float py = y + (vy * s) / 255.0f;

	dashRect(x, y, s, 1, dashColorA(DASH_C_TEXT, 0.3f));
	dashRect(x, y + s, s, 1, dashColorA(DASH_C_TEXT, 0.3f));
	dashRect(x, y, 1, s, dashColorA(DASH_C_TEXT, 0.3f));
	dashRect(x + s, y, 1, s, dashColorA(DASH_C_TEXT, 0.3f));
	dashRect(px - 3, py - 3, 6, 6, DASH_C_ACCENT);
	dashTextS(x, y + s + 4, label, DASH_FS_CAPTION, DASH_C_TEXT3);
}

static void tester_draw_port(int c, float x, float y)
{
	const struct padButtonStatus *s = &buttons_t[tester_port[c]];
	unsigned now = tester_pressed(c);
	int connected = !(s->btns == 0 && s->ljoy_h == 0 && s->ljoy_v == 0 && s->rjoy_h == 0 && s->rjoy_v == 0);
	char raw[32];
	int i;

	dashTextS(x, y, tester_port_name[c], DASH_FS_LABEL, DASH_C_TEXT);
	if (!connected) {
		dashTextS(x, y + 22, "no data yet", DASH_FS_CAPTION, DASH_C_TEXT3);
		return;
	}

	for (i = 0; i < TESTER_BTN_COUNT; i++) {
		float ry = y + 22 + i * 16.0f;
		int lit = (now & tester_btn[i].mask) != 0;

		dashRect(x, ry, 10, 10, lit ? DASH_C_ACCENT : dashColorA(DASH_C_TEXT, 0.15f));
		dashTextS(x + 18, ry - 1, tester_btn[i].name, DASH_FS_CAPTION, lit ? DASH_C_TEXT : DASH_C_TEXT3);
	}

	tester_draw_stick(x + 150, y + 22, s->ljoy_h, s->ljoy_v, "left stick");
	tester_draw_stick(x + 150, y + 118, s->rjoy_h, s->rjoy_v, "right stick");

	snprintf(raw, sizeof(raw), "raw btns %04X", s->btns & 0xFFFF);
	dashTextS(x + 150, y + 210, raw, DASH_FS_CAPTION, DASH_C_TEXT3);
}

static void tester_draw_log(float x, float y)
{
	int i;

	dashTextS(x, y, "Events", DASH_FS_CAPTION, DASH_C_TEXT3);
	for (i = 0; i < TESTER_LOG_LINES; i++)
		dashTextS(x, y + 16 + i * 14.0f, tester_log[i], DASH_FS_CAPTION, DASH_C_TEXT);
}

void dashControllerTester(void)
{
	const float w = 600.0f, h = 430.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	float open = 0.0f;
	int c;
	DashInput in;

	memset(tester_log, 0, sizeof(tester_log));
	memset(tester_prev, 0, sizeof(tester_prev));
	tester_seeded = 0;
	tester_rumble_on = 0;
	gipRumbleTestLevel = 0;

	dashInputFlush();
	while (1) {
		dashInputPoll(&in);
		if (dashDashboardPending() || (in.pressed & (DASH_IN_OK | DASH_IN_BACK | DASH_IN_MENU)))
			break;

		readpad_noKBnoRepeat();
		tester_update();

		/* Square: vibrate the USB pad at full strength for 1.5 s, then stop. */
		if (new_pad & PAD_SQUARE) {
			gipRumbleTestLevel = 255;
			tester_rumble_on = 1;
			tester_rumble_until = Timer() + 1500;
			tester_push("vibration test: on");
		}
		if (tester_rumble_on && Timer() >= tester_rumble_until) {
			gipRumbleTestLevel = 0;
			tester_rumble_on = 0;
			tester_push("vibration test: off");
		}

		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		dashRenderScene();
		dashModalBackdrop(dashEaseOutCubic(open));
		dashModalPanel(x, y, w, h, 1.0f);
		dashIcon(DI_USB, x + 24, y + 18, 22, DASH_C_ACCENT);
		dashTextS(x + 56, y + 18, localizeDashboardText("Controller tester"), DASH_FS_LABEL, DASH_C_TEXT);

		for (c = 0; c < 2; c++)
			tester_draw_port(c, x + 24 + c * 290.0f, y + 52);

		tester_draw_log(x + 24, y + h - 100);
		dashModalHintRow(x + 24, y + h - 36, dashOkIcon(), localizeDashboardText("Close"), -1, NULL);
		dashGfxPresent();
	}
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_tester.c
//--------------------------------------------------------------
