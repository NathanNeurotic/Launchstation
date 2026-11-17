//---------------------------------------------------------------------------
// File name:   pad_rumble.c
//
// Opt-in controller vibration for interface feedback (Settings > Input).
// Ported from RiptOPL's menu rumble (src/core/pad.cpp, src/core/sfx.cpp),
// including its two hardware lessons:
//
// 1. padman can drop padSetActDirect while it is busy and still return 1.
//    A dropped "on" loses one pulse; a dropped "off" leaves the motor
//    running. Each change is therefore sent a few times over the next polls
//    and then stops, rather than once or on every frame.
// 2. padPortOpen resets the actuator alignment, and padSetActDirect drives
//    nothing until padSetActAlign succeeds. A failed alignment is retried
//    only while the controller is idle, because padman answers no reads
//    while it aligns and a press in that window would be lost.
//
// padRumble() only arms state and may be called from any thread. The
// motors are driven only from padRumbleTick(), which the pad read
// functions call on the thread that owns the pad bus.
//---------------------------------------------------------------------------
#include "launchelf.h"
#include "dash_sound.h"

#define RUMBLE_MAX_MS         500  /* menu feedback, never a sustained buzz */
#define RUMBLE_CMD_REPEATS    3    /* extra deliveries after the first */
#define RUMBLE_OFF_SYNC_SENDS 2    /* stops sent at once by padRumbleFlush() */
#define RUMBLE_ALIGN_IDLE_MS  1000 /* controller quiet this long before a re-align */

static volatile int r_active, r_large, r_on_repeats, r_off_repeats, r_wanted;
static volatile u64 r_start, r_dur;
static int r_aligned[2], r_actuators[2];
static u64 r_idle_since;

static int padUsable(int port)
{
	int state = padGetState(port, 0);
	return state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
}

static void actSet(int port, int small, int large)
{
	char act[6];

	if (!r_aligned[port] || r_actuators[port] <= 0 || !padUsable(port))
		return;
	memset(act, 0, sizeof(act));
	act[0] = small ? 1 : 0;        /* small motor: on/off only */
	act[1] = (char)(large & 0xff); /* large motor: 0..255 */
	padSetActDirect(port, 0, act);
}

/* Runs from the idle gate in padRumbleTick(), never in the middle of input. */
static void padRumbleAlign(int port)
{
	char align[6] = {0, 1, 0xff, 0xff, 0xff, 0xff}; /* small -> byte 0, large -> byte 1 */

	if (port < 0 || port > 1 || r_aligned[port])
		return;
	if (padGetState(port, 0) != PAD_STATE_STABLE || padGetReqState(port, 0) == PAD_RSTAT_BUSY)
		return;
	if (r_actuators[port] <= 0)
		r_actuators[port] = padInfoAct(port, 0, -1, 0);
	if (r_actuators[port] <= 0) {
		r_actuators[port] = 0;
		return;
	}
	if (padSetActAlign(port, 0, align))
		r_aligned[port] = 1;
}

/* padPortOpen resets the alignment: forget it whenever the ports reopen. */
void padRumbleReset(void)
{
	r_aligned[0] = r_aligned[1] = 0;
	r_actuators[0] = r_actuators[1] = 0;
	r_active = 0;
	r_on_repeats = r_off_repeats = 0;
}

/* Settings > Input > Vibration. Off: no pad traffic at all. */
void padRumbleEnable(int on)
{
	if (!on)
		padRumbleFlush();
	r_wanted = on ? 1 : 0;
}

void padRumble(int duration_ms, int large)
{
	if (!r_wanted || duration_ms <= 0)
		return;
	if (duration_ms > RUMBLE_MAX_MS)
		duration_ms = RUMBLE_MAX_MS;
	if (large < 0)
		large = 0;
	if (large > 255)
		large = 255;
	if (r_active && r_large == large) {
		/* extend the pulse in flight; the motor is already on */
		r_start = Timer();
		r_dur = duration_ms;
		return;
	}
	r_start = Timer();
	r_dur = duration_ms;
	r_large = large;
	r_off_repeats = 0; /* a new pulse cancels a stop still being repeated */
	r_on_repeats = 1 + RUMBLE_CMD_REPEATS;
	r_active = 1;
}

void padRumbleFlush(void)
{
	int r;

	if (!r_active)
		return;
	r_active = 0;
	r_on_repeats = 0;
	r_large = 0;
	for (r = 0; r < RUMBLE_OFF_SYNC_SENDS; r++) {
		actSet(0, 0, 0);
		actSet(1, 0, 0);
	}
	r_off_repeats = RUMBLE_CMD_REPEATS;
}

/* DS3/DS4 level for the DS34 build's polling thread. Self-expiring, so a
 * missed flush cannot leave those motors running. */
int padRumbleLevel(void)
{
	if (r_active && Timer() - r_start < r_dur)
		return r_large;
	return 0;
}

/* held: buttons down this poll, for the idle gate of the re-align. */
void padRumbleTick(u32 held)
{
	u64 now;
	int port;

	if (!r_wanted && !r_off_repeats)
		return; /* vibration off: no pad traffic at all */
	now = Timer();
	for (port = 0; port < 2; port++)
		if (padGetState(port, 0) == PAD_STATE_DISCONN)
			r_aligned[port] = r_actuators[port] = 0; /* a new pad needs aligning */
	if (held || r_active)
		r_idle_since = now;
	else if (r_wanted && now - r_idle_since >= RUMBLE_ALIGN_IDLE_MS) {
		r_idle_since = now; /* at most one attempt a second */
		for (port = 0; port < 2; port++)
			if (!r_aligned[port] && padGetState(port, 0) == PAD_STATE_STABLE) {
				padRumbleAlign(port);
				break;
			}
	}
	if (r_active) {
		if (now - r_start >= r_dur)
			padRumbleFlush();
		else if (r_on_repeats > 0) {
			actSet(0, 1, r_large);
			actSet(1, 1, r_large);
			r_on_repeats--;
		}
	} else if (r_off_repeats > 0) {
		actSet(0, 0, 0);
		actSet(1, 0, 0);
		r_off_repeats--;
	}
}

//---------------------------------------------------------------------------
// Interface feedback: one short thump per cursor step, a firmer bump for
// decisions. Values from RiptOPL's sfx.cpp: an eccentric-mass motor needs
// 50-100 ms to reach a level the hand can feel, so length matters more
// than strength, and steps faster than the motor can answer stay silent.
//---------------------------------------------------------------------------
#define RUMBLE_NAV_MS              240
#define RUMBLE_NAV_LEVEL           0x50
#define RUMBLE_NAV_REPEAT_WINDOW   400
#define RUMBLE_NAV_REPEAT_DUTY_PCT 45
#define RUMBLE_NAV_MIN_MS          80
#define RUMBLE_DECISION_MS         110
#define RUMBLE_DECISION_LEVEL      0x60

void padRumbleNav(void)
{
	static u64 last;
	static int have_last;
	u64 now = Timer();
	int ms = RUMBLE_NAV_MS;

	if (have_last && now - last < RUMBLE_NAV_REPEAT_WINDOW) {
		int duty = (int)((now - last) * RUMBLE_NAV_REPEAT_DUTY_PCT / 100);
		if (duty < RUMBLE_NAV_MIN_MS)
			return;
		if (duty < ms)
			ms = duty;
	}
	last = now;
	have_last = 1;
	padRumble(ms, RUMBLE_NAV_LEVEL);
}

void padRumbleDecision(void)
{
	padRumble(RUMBLE_DECISION_MS, RUMBLE_DECISION_LEVEL);
}

/* dashSoundPlay(): the same events with or without interface sounds. Boot
 * and device hotplug sounds stay quiet: hotplug comes from the worker. */
void padRumbleForSound(int id)
{
	if (id == DASH_SND_CURSOR)
		padRumbleNav();
	else if (id == DASH_SND_CONFIRM || id == DASH_SND_CANCEL ||
	         id == DASH_SND_TRANSITION || id == DASH_SND_MESSAGE)
		padRumbleDecision();
}

//---------------------------------------------------------------------------
// End of file: pad_rumble.c
//---------------------------------------------------------------------------
