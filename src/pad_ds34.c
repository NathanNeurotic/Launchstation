//---------------------------------------------------------------------------
// File name:   pad_ds34.c
// description: variation of pad.c with support for the PlayStation 3 and 4 dualshock drivers by Alex Parrado
//---------------------------------------------------------------------------
#include "launchelf.h"
#include "dash.h"
#include "hdd_install_recovery.h"
#include <kernel.h>
#include <timer.h>
#include <time.h>

// Pad polling time in HSync cycles
#define POLLING_TIME ((10 * 15734) / 1000)


static char padBuf_t[2][256] __attribute__((aligned(64)));
u8 gipRumbleTestLevel = 0; // set by the controller tester to check vibration on the GIP pad
struct padButtonStatus buttons_t[4];
u32 padtype_t[4];
u32 paddata, paddata_t[4];
u32 old_pad = 0, old_pad_t[4] = {0, 0, 0, 0};
int ret[4] = {0, 0, 0, 0};
u32 new_pad, new_pad_t[4];
u32 joy_value = 0;


int semPoll,semRunning,semFinish;
int isRunning;
static int recovery_poll_tid = -1, recovery_alarm_sem = -1;
static volatile int recovery_alarm_id = -1;

void clearPadPressState(void)
{
	memset(old_pad_t, 0, sizeof(old_pad_t));
	memset(new_pad_t, 0, sizeof(new_pad_t));
	memset(paddata_t, 0, sizeof(paddata_t));
	memset(ret, 0, sizeof(ret));
	old_pad = 0;
	new_pad = 0;
	paddata = 0;
	joy_value = 0;
}

// Alarm handler
static void alrmHandler(s32 alarm_id, u16 time, void *arg2)
{
	s32 *pSema = (s32 *)arg2;
	recovery_alarm_id = -1;
	iSignalSema(*pSema);
	ExitHandler();
}



// Pads polling thread
void padPollingThread(void *args)
{
	int state;
	ee_sema_t semData;
	int iIsRunning;

	semData.option = semData.attr = 0;
	semData.init_count = 0;
	semData.max_count = 1;

	while (1) {
	
		WaitSema(semRunning);
		iIsRunning=isRunning;
		SignalSema(semRunning);
		
		if(iIsRunning==0)
		    break;
		    
		

		state = padGetState(0, 0);
		if (state == PAD_STATE_STABLE || (state == PAD_STATE_FINDCTP1)) {
			ret[0] = padRead(0, 0, &buttons_t[0]);
			if (ret[0] != 0)
				paddata_t[0] = 0xffff ^ buttons_t[0].btns;
		}

		state = padGetState(1, 0);
		if (state == PAD_STATE_STABLE || (state == PAD_STATE_FINDCTP1)) {
			ret[1] = padRead(1, 0, &buttons_t[1]);
			if (ret[1] != 0)
				paddata_t[1] = 0xffff ^ buttons_t[1].btns;
		}


		if (ds34bt_get_status(0) & DS34BT_STATE_RUNNING) {
			ret[2] = ds34bt_get_data(0, (u8 *)&buttons_t[2].btns);
			ds34bt_set_rumble(0, padRumbleLevel(), padRumbleLevel());
			if (ret[2] != 0) {
				paddata_t[2] = 0xffff ^ buttons_t[2].btns;
			}
		}

		if (ds34usb_get_status(0) & DS34USB_STATE_RUNNING) {
			ret[3] = ds34usb_get_data(0, (u8 *)&buttons_t[3].btns);
			ds34usb_set_rumble(0, padRumbleLevel(), padRumbleLevel());
			if (ret[3] != 0) {
				paddata_t[3] = 0xffff ^ buttons_t[3].btns;
			}
		} else if (gipusb_get_status(0) & GIPUSB_STATE_RUNNING) {
			// Xbox One / Series pad. Shares the USB slot: a DS3/DS4 pad takes priority.
			ret[3] = gipusb_get_data(0, (u8 *)&buttons_t[3].btns);
			{
				u8 lvl = (u8)padRumbleLevel();
				if (gipRumbleTestLevel > lvl)
					lvl = gipRumbleTestLevel; /* Controller tester: vibration check */
				gipusb_set_rumble(0, lvl, lvl);
			}
			if (ret[3] != 0) {
				paddata_t[3] = 0xffff ^ buttons_t[3].btns;
			}
		}


		// Signal readpad_no* functions
		SignalSema(semPoll);

		// nanosleep from the scratch
		recovery_alarm_sem = CreateSema(&semData);
		recovery_alarm_id = SetAlarm(POLLING_TIME, alrmHandler, &recovery_alarm_sem);
		WaitSema(recovery_alarm_sem);
		DeleteSema(recovery_alarm_sem);
		recovery_alarm_sem = -1;
	}
	
	recovery_poll_tid = -1;
	SignalSema(semFinish);
	ExitDeleteThread();
}

void padSuspendForRecovery(void)
{
	if (recovery_poll_tid >= 0) SuspendThread(recovery_poll_tid);
}
void padForgetAfterRecovery(void)
{
	/* Old IOP/DMA is stopped before releasing any EE polling resources. */
	if (recovery_poll_tid >= 0) {
		TerminateThread(recovery_poll_tid); DeleteThread(recovery_poll_tid);
		recovery_poll_tid = -1;
	}
	if (recovery_alarm_id >= 0) ReleaseAlarm(recovery_alarm_id);
	recovery_alarm_id = -1;
	if (recovery_alarm_sem >= 0) DeleteSema(recovery_alarm_sem);
	recovery_alarm_sem = -1;
	DeleteSema(semPoll); DeleteSema(semRunning); DeleteSema(semFinish);
	isRunning = 0;
	ds34usb_reset(); ds34bt_reset();
	clearPadPressState();
}

// Init polling thread
void padPollingInit(void)
{

	ee_thread_t th_attr;
	int pad_thr_id;	
	ee_sema_t semData;
	static unsigned char stack[4096] __attribute__((aligned(16)));

	semData.option = semData.attr = 0;
	semData.init_count = 0;
	semData.max_count = 1;
	semPoll = CreateSema(&semData);
	semFinish = CreateSema(&semData);
	semData.init_count = 1;
	semRunning = CreateSema(&semData);


	th_attr.func = padPollingThread;
	th_attr.stack = stack;
	th_attr.stack_size = 4096;
	th_attr.gp_reg = &_gp;
	th_attr.initial_priority = 2;
	th_attr.attr = th_attr.option = 0;
	
	isRunning=1;
	pad_thr_id = CreateThread(&th_attr);
	recovery_poll_tid = pad_thr_id;
	StartThread(pad_thr_id, NULL);
}



u64 msTime()
{
	return Timer();
}

//---------------------------------------------------------------------------
// read PAD, without KB, and allow no auto-repeat. This is needed in code
// that is used regardless of VSync cycles, and where KB is not wanted.
//---------------------------------------------------------------------------
int readpad_noKBnoRepeat(void)
{
	int port;

	if (dashWorkerIoBusy()) { if (PollSema(semPoll) < 0) return 0; }
	else WaitSema(semPoll);

	for (port = 0; port < 4; port++) {

		new_pad_t[port] = paddata_t[port] & ~old_pad_t[port];
		old_pad_t[port] = paddata_t[port];

	}                                                                     // ends for
	new_pad = new_pad_t[0] | new_pad_t[1] | new_pad_t[2] | new_pad_t[3];  // This has only new button bits
	paddata = paddata_t[0] | paddata_t[1] | paddata_t[2] | paddata_t[3];  // This has all pressed button bits
	padRumbleTick(paddata);
	return (ret[0] | ret[1] | ret[2] | ret[3]);
}
//------------------------------
// endfunc readpad_noKBnoRepeat
//---------------------------------------------------------------------------
// read PAD, but ignore KB. This is needed in code with own KB handlers,
// such as the virtual keyboard input routines for 'Rename' and 'New Dir'
//---------------------------------------------------------------------------
int readpad_no_KB(void)
{
	static u64 rpt_time[4] = {0, 0, 0, 0};
	static int rpt_count[4] = {0, 0, 0, 0};
	int port;

	if (dashWorkerIoBusy()) { if (PollSema(semPoll) < 0) return 0; }
	else WaitSema(semPoll);

	for (port = 0; port < 4; port++) {
		if (ret[port] != 0) {
			// DualShock stick scan, same as pad.c: OR into the held mask on every call.
			// Replacing the mask (=) or scanning every other call made the buttons
			// disappear and retrigger while a stick was held.
			if (padtype_t[port] == 2) {  // DualShock
				joy_value = 0;
				if (buttons_t[port].rjoy_h >= 0xbf) {
					paddata_t[port] |= PAD_R3_H1;
					joy_value = buttons_t[port].rjoy_h - 0xbf;
				} else if (buttons_t[port].rjoy_h <= 0x40) {
					paddata_t[port] |= PAD_R3_H0;
					joy_value = -(buttons_t[port].rjoy_h - 0x40);
				} else if (buttons_t[port].rjoy_v <= 0x40) {
					paddata_t[port] |= PAD_R3_V0;
					joy_value = -(buttons_t[port].rjoy_v - 0x40);
				} else if (buttons_t[port].rjoy_v >= 0xbf) {
					paddata_t[port] |= PAD_R3_V1;
					joy_value = buttons_t[port].rjoy_v - 0xbf;
				} else if (buttons_t[port].ljoy_h >= 0xbf) {
					paddata_t[port] |= PAD_L3_H1;
					joy_value = buttons_t[port].ljoy_h - 0xbf;
				} else if (buttons_t[port].ljoy_h <= 0x40) {
					paddata_t[port] |= PAD_L3_H0;
					joy_value = -(buttons_t[port].ljoy_h - 0x40);
				} else if (buttons_t[port].ljoy_v <= 0x40) {
					paddata_t[port] |= PAD_L3_V0;
					joy_value = -(buttons_t[port].ljoy_v - 0x40);
				} else if (buttons_t[port].ljoy_v >= 0xbf) {
					paddata_t[port] |= PAD_L3_V1;
					joy_value = buttons_t[port].ljoy_v - 0xbf;
				}
			}
			new_pad_t[port] = paddata_t[port] & ~old_pad_t[port];
			if (old_pad_t[port] == paddata_t[port]) {

				// no change of pad data
				if (msTime() > rpt_time[port]) {
					new_pad_t[port] = paddata_t[port];  // Accept repeated buttons as new
					rpt_time[port] = msTime() + 40;     // Min delay = 40ms => 25Hz repeat
					if (rpt_count[port]++ < 20)
						rpt_time[port] += 43;  // Early delays = 83ms => 12Hz repeat
				}
			} else {
				// pad data has changed !
				rpt_count[port] = 0;
				rpt_time[port] = msTime() + 400;  // Init delay = 400ms
				old_pad_t[port] = paddata_t[port];
			}
		} else {
			// Deal with cases where pad state is not valid for padRead
			// NB: This should NOT clear KB repeat test variables
			new_pad_t[port] = 0;
			// old_pad_t[port] = 0;  //Clearing this could cause hasty repeats
		}  // ends 'if' testing for state valid for padRead
	}
	// ends for

	new_pad = new_pad_t[0] | new_pad_t[1] | new_pad_t[2] | new_pad_t[3];
	paddata = paddata_t[0] | paddata_t[1] | paddata_t[2] | paddata_t[3];  // This has all pressed button bits
	padRumbleTick(paddata);
	return (ret[0] | ret[1] | ret[2] | ret[3]);
}
//------------------------------
// endfunc readpad_no_KB
//---------------------------------------------------------------------------
// simPadKB attempts reading data from a USB keyboard, and map this as a
// virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int simPadKB(void)
{
	int ret, command;
	char KeyPress;

	if (!setting->usbkbd_used || !ensureUsbKeyboardReady() || !PS2KbdRead(&KeyPress))
		return 0;
	if (KeyPress != PS2KBD_ESCAPE_KEY)
		command = KeyPress;
	else {
		PS2KbdRead(&KeyPress);
		command = 0x100 + KeyPress;
	}
	ret = 1;  // Assume that the entered key is a valid command
	switch (command) {
		case 0x11B:  // Escape == Triangle
			new_pad = PAD_TRIANGLE;
			break;
		case 0x00A:  // Enter == OK
			if (!swapKeys)
				new_pad = PAD_CIRCLE;
			else
				new_pad = PAD_CROSS;
			break;
		case 0x020:  // Space == Cancel/Mark
			if (!swapKeys)
				new_pad = PAD_CROSS;
			else
				new_pad = PAD_CIRCLE;
			break;
		case 0x031:  //'1' == L1
			new_pad = PAD_L1;
			break;
		case 0x032:  //'2' == L2
			new_pad = PAD_L2;
			break;
		case 0x033:  //'3' == L3
			new_pad = PAD_L3;
			break;
		case 0x077:  //'w' == Up
			new_pad = PAD_UP;
			break;
		case 0x061:  //'a' == Left
			new_pad = PAD_LEFT;
			break;
		case 0x073:  //'s' == Right
			new_pad = PAD_RIGHT;
			break;
		case 0x07A:  //'z' == Down
			new_pad = PAD_DOWN;
			break;
		case 0x030:  //'0' == R1
			new_pad = PAD_R1;
			break;
		case 0x039:  //'9' == R2
			new_pad = PAD_R2;
			break;
		case 0x038:  //'8' == R3
			new_pad = PAD_R3;
			break;
		case 0x069:  //'i' == Triangle
			new_pad = PAD_TRIANGLE;
			break;
		case 0x06A:  //'j' == Square
			new_pad = PAD_SQUARE;
			break;
		case 0x06B:  //'k' == Circle
			new_pad = PAD_CIRCLE;
			break;
		case 0x06D:  //'m' == Cross
			new_pad = PAD_CROSS;
			break;
		case 0x101:  // F1 == L1
			new_pad = PAD_L1;
			break;
		case 0x102:  // F2 == L2
			new_pad = PAD_L2;
			break;
		case 0x103:  // F3 == L3
			new_pad = PAD_L3;
			break;
		case 0x12C:  // Up == Up
			new_pad = PAD_UP;
			break;
		case 0x12A:  // Left == Left
			new_pad = PAD_LEFT;
			break;
		case 0x129:  // Right == Right
			new_pad = PAD_RIGHT;
			break;
		case 0x12B:  // Down == Down
			new_pad = PAD_DOWN;
			break;
		case 0x123:  // Insert == Select
			new_pad = PAD_SELECT;
			break;
		case 0x10C:  // F12 == R1
			new_pad = PAD_R1;
			break;
		case 0x10B:  // F11 == R2
			new_pad = PAD_R2;
			break;
		case 0x10A:  // F10 == R3
			new_pad = PAD_R3;
			break;
		case 0x124:  // Home == Triangle
			new_pad = PAD_TRIANGLE;
			break;
		case 0x127:  // End == Square
			new_pad = PAD_SQUARE;
			break;
		case 0x125:  // PgUp == Circle
			new_pad = PAD_CIRCLE;
			break;
		case 0x128:  // PgDn == Cross
			new_pad = PAD_CROSS;
			break;
		case 0x126:  // Delete == Start
			new_pad = PAD_START;
			break;
		default:  // Unrecognized key => no pad button
			ret = 0;
			break;
	}
	return ret;
}
//------------------------------
// endfunc simPadKB
//---------------------------------------------------------------------------
// readpad will call readpad_no_KB, and if no new pad buttons are found, it
// will also attempt reading data from a USB keyboard, and map this as a
// virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int readpad(void)
{
	int ret;

	if ((ret = readpad_no_KB()) && new_pad)
		return ret;

	return simPadKB();
}
//------------------------------
// endfunc readpad
//---------------------------------------------------------------------------
// readpad_noRepeat calls readpad_noKBnoRepeat, and if no new pad buttons are
// found, it also attempts reading data from a USB keyboard, and map this as
// a virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int readpad_noRepeat(void)
{
	int ret;

	if ((ret = readpad_noKBnoRepeat()) && new_pad)
		return ret;

	return simPadKB();
}
//------------------------------
// endfunc readpad_noRepeat
//---------------------------------------------------------------------------
// Wait for specific PAD, but also accept disconnected state
static int waitPadReadyUntil(int port, int slot, u64 deadline)
{
	for (;;) {
		int state = padGetState(port, slot);
		if (state == PAD_STATE_DISCONN || state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1)
			return 1;
		if (deadline) {
			if (Timer() >= deadline) return 0;
			DelayThread(1000);
		}
	}
}

void waitPadReady(int port, int slot)
{
	waitPadReadyUntil(port, slot, 0);
}
//---------------------------------------------------------------------------
// Wait for any PAD, but also accept disconnected states
void waitAnyPadReady(void)
{
	int state_1, state_2;

	state_1 = padGetState(0, 0);
	state_2 = padGetState(1, 0);
	while ((state_1 != PAD_STATE_DISCONN) && (state_2 != PAD_STATE_DISCONN) && (state_1 != PAD_STATE_STABLE) && (state_2 != PAD_STATE_STABLE) && (state_1 != PAD_STATE_FINDCTP1) && (state_2 != PAD_STATE_FINDCTP1)) {
		state_1 = padGetState(0, 0);
		state_2 = padGetState(1, 0);
	}
}
//---------------------------------------------------------------------------
// setup PAD
static int setupPadUntil(u64 deadline)
{
	int ret, i, port, state, modes;

	if (padInit(0) < 0 && deadline) return 0;
	padRumbleReset();
	int usb_ready = ds34usb_init(deadline);
	int bt_ready = ds34bt_init(deadline);
	if (deadline && (!usb_ready || !bt_ready)) return 0;
	gipusb_init(); /* Xbox One / Series pads; not part of the ready check, so a missing one never fails startup */

	for (port = 0; port < 2; port++) {
		padtype_t[port] = 0;  // Assume that we don't have a proper PS2 controller
		if ((ret = padPortOpen(port, 0, &padBuf_t[port][0])) == 0)
			return 0;
		if (!waitPadReadyUntil(port, 0, deadline)) return 0;
		state = padGetState(port, 0);
		if (state != PAD_STATE_DISCONN) {  // if anything connected to this port
			modes = padInfoMode(port, 0, PAD_MODETABLE, -1);
			if (modes != 0) {  // modes != 0, so it may be a dualshock type
				for (i = 0; i < modes; i++) {
					if (padInfoMode(port, 0, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
						padtype_t[port] = 2;  // flag normal PS2 controller
						break;
					}
				}                     // ends for (modes)
			} else {                  // modes == 0, so this is a digital controller
				padtype_t[port] = 1;  // flag digital controller
			}
			if (padtype_t[port] == 2)                                          // if DualShock
				padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);  // Set DualShock
			else                                                               // else
				padSetMainMode(port, 0, PAD_MMODE_DIGITAL, PAD_MMODE_UNLOCK);  // Set Digital
			if (!waitPadReadyUntil(port, 0, deadline)) return 0;
		} else {                                                               // Nothing is connected to this port
			padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);      // Fake DualShock
			if (!waitPadReadyUntil(port, 0, deadline)) return 0;
		}
	}  // ends for (port)

	padtype_t[2] = 2;
	padtype_t[3] = 2;
	if (deadline && !hddInstallPadRpcReady()) return 0;
	padPollingInit();
	return 1;
}

int setupPad(void)
{
	return setupPadUntil(0);
}

int setupPadForRecovery(void)
{
	u64 deadline = Timer() + 5000;
	hddInstallPadRpcBegin(deadline);
	int ready = setupPadUntil(deadline);
	hddInstallPadRpcEnd();
	return ready;
}
//---------------------------------------------------------------------------
// DS3/DS4 builds also have a pad polling thread. Stop it before tearing down
// libpad, then let setupPad() rebuild the wired ports and polling state.
//---------------------------------------------------------------------------
int reopenPadPorts(void)
{
	int attempt;

	for (attempt = 0; attempt < 2; attempt++) {
		if (isRunning) {
			WaitSema(semRunning);
			isRunning = 0;
			SignalSema(semRunning);
			WaitSema(semFinish);
			ds34usb_reset();
			ds34bt_reset();
		}
		padPortClose(1, 0);
		padPortClose(0, 0);
		padEnd();
		DelayThread((attempt ? 100 : 50) * 1000);
		if (setupPad()) {
			clearPadPressState();
			return 1;
		}
	}

	clearPadPressState();
	return 0;
}

//---------------------------------------------------------------------------
// End of file: pad_ds34.c
//---------------------------------------------------------------------------
