//--------------------------------------------------------------
//File name:   dash_sound.c
//Dashboard SFX: loads audsrv, uploads the embedded ADP samples and
//plays them on ADPCM channels. Degrades to silent no-ops on any
//failure (driver missing, sample rejected, sound disabled).
//--------------------------------------------------------------
#include "launchelf.h"
#include "dash_internal.h"
#include "dash_sound.h"
#include <audsrv.h>

extern u8 audsrv_irx[];
extern int size_audsrv_irx;
extern u8 sfx_cursor_adp[];
extern int size_sfx_cursor_adp;
extern u8 sfx_confirm_adp[];
extern int size_sfx_confirm_adp;
extern u8 sfx_cancel_adp[];
extern int size_sfx_cancel_adp;
extern u8 sfx_transition_adp[];
extern int size_sfx_transition_adp;
extern u8 sfx_message_adp[];
extern int size_sfx_message_adp;
extern u8 sfx_boot_adp[];
extern int size_sfx_boot_adp;
extern u8 sfx_connect_adp[];
extern int size_sfx_connect_adp;
extern u8 sfx_disconnect_adp[];
extern int size_sfx_disconnect_adp;

static audsrv_adpcm_t samples[DASH_SND_COUNT];
static int sound_ready;

static void *blob_data(int id)
{
	switch (id) {
		case DASH_SND_CURSOR: return sfx_cursor_adp;
		case DASH_SND_CONFIRM: return sfx_confirm_adp;
		case DASH_SND_CANCEL: return sfx_cancel_adp;
		case DASH_SND_TRANSITION: return sfx_transition_adp;
		case DASH_SND_MESSAGE: return sfx_message_adp;
		case DASH_SND_BOOT: return sfx_boot_adp;
		case DASH_SND_CONNECT: return sfx_connect_adp;
		default: return sfx_disconnect_adp;
	}
}

static int blob_size(int id)
{
	switch (id) {
		case DASH_SND_CURSOR: return size_sfx_cursor_adp;
		case DASH_SND_CONFIRM: return size_sfx_confirm_adp;
		case DASH_SND_CANCEL: return size_sfx_cancel_adp;
		case DASH_SND_TRANSITION: return size_sfx_transition_adp;
		case DASH_SND_MESSAGE: return size_sfx_message_adp;
		case DASH_SND_BOOT: return size_sfx_boot_adp;
		case DASH_SND_CONNECT: return size_sfx_connect_adp;
		default: return size_sfx_disconnect_adp;
	}
}

int dashSoundInit(void)
{
	int i, r;

	if (sound_ready)
		return 1;
	if (!dashCfg.sound)
		return 0;
	if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &r) < 0)
		return 0;
	if (audsrv_init() != 0)
		return 0;
	if (audsrv_adpcm_init() != 0) {
		audsrv_quit();
		return 0;
	}
	for (i = 0; i < DASH_SND_COUNT; i++) {
		samples[i].buffer = NULL;
		if (audsrv_load_adpcm(&samples[i], blob_data(i), blob_size(i)) != 0)
			samples[i].buffer = NULL; /* sample stays silent */
	}
	sound_ready = 1;
	return 1;
}

void dashSoundShutdown(void)
{
	if (!sound_ready)
		return;
	audsrv_quit();
	sound_ready = 0;
}

void dashSoundPlay(int id)
{
	static int rr;
	int ch;

	if (!sound_ready || !dashCfg.sound || id < 0 || id >= DASH_SND_COUNT)
		return;
	if (samples[id].buffer == NULL)
		return;
	ch = (id == DASH_SND_CURSOR) ? (rr++ & 1) : 2;
	audsrv_adpcm_set_volume_and_pan(ch, dashCfg.sound_vol * 0x3FFF / 100, 0);
	audsrv_ch_play_adpcm(ch, &samples[id]);
}
