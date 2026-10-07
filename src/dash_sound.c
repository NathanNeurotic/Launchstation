//--------------------------------------------------------------
//File name:   dash_sound.c
//Dashboard SFX: loads audsrv, uploads the embedded ADP samples and
//plays them on ADPCM channels. Degrades to silent no-ops on any
//failure (driver missing, sample rejected, sound disabled).
//--------------------------------------------------------------
#include "launchelf.h"
#include "dash_internal.h"
#include "dash_sound.h"
#include "init.h"
#include <audsrv.h>
#include <iopcontrol.h>

extern u8 audsrv_irx[];
extern int size_audsrv_irx;
extern u8 libsd_irx[];
extern int size_libsd_irx;
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
static audsrv_adpcm_t bgm_sample;
static int sound_ready;
static int bgm_loaded;
static int bgm_tried;
static void *bgm_data; /* audsrv uses this buffer address as the sample ID */
static int bgm_channel = -1;
static unsigned int sound_reboot_generation; /* reset epoch when audsrv was started */
static unsigned int sound_tried_generation = (unsigned int)-1; /* last automatic attempt */
static int sound_retry;


/* audsrv lives on the IOP: after any IOP reboot the server and the uploaded
 * samples are gone, and calling into the stale RPC client hangs. Every
 * entry point checks this first. */
static int soundAlive(void)
{
	return sound_ready && getIopResetGeneration() == sound_reboot_generation;
}

/* Voices 0..13 belong to interface SFX (see below). Keep the looping music
 * on a voice they never touch so menu changes cannot replace it. */
#define DASH_BGM_CHANNEL 23

static void *blob_data(int id)
{
	switch (id) {
		case DASH_SND_CURSOR: return sfx_cursor_adp;
		case DASH_SND_CONFIRM: return sfx_confirm_adp;
		case DASH_SND_CANCEL: return sfx_cancel_adp;
		case DASH_SND_TRANSITION: return sfx_transition_adp;
		case DASH_SND_MESSAGE: return sfx_message_adp;
		case DASH_SND_BOOT: return sfx_confirm_adp;
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
		case DASH_SND_BOOT: return size_sfx_confirm_adp;
		case DASH_SND_CONNECT: return size_sfx_connect_adp;
		default: return size_sfx_disconnect_adp;
	}
}

int dashSoundInit(void)
{
	int i, r, failed;

	if (sound_ready && soundAlive())
		return 1;
	if (sound_ready)
		dashSoundShutdown(); /* stale after an IOP reboot: forget it */
	/* the UI thread must not load modules while a background job may */
	if (dashWorkerIoBusy() || dashIopParked()) {
		sound_retry = 1; /* dashSoundMaintain() tries again */
		return 0;
	}
	sound_tried_generation = getIopResetGeneration();
	/* BGM and interface SFX are separate buses; initialize audsrv when
	 * either one is enabled. */
	if (!dashCfg.sound && !dashCfg.bgm)
		return 0;
	/* audsrv imports sceSd* from the libsd module: it must be resident first */
	if (SifExecModuleBuffer(libsd_irx, size_libsd_irx, 0, NULL, &r) < 0 || r < 0) {
		dashToast(DASH_TOAST_WARN, "Sound unavailable: libsd would not start");
		return 0;
	}
	if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &r) < 0 || r < 0) {
		dashToast(DASH_TOAST_WARN, "Sound unavailable: audsrv would not start");
		return 0;
	}
	if (audsrv_init() != 0) {
		dashToast(DASH_TOAST_WARN, "Sound unavailable: audsrv init failed");
		return 0;
	}
	if (audsrv_adpcm_init() != 0) {
		audsrv_quit();
		dashToast(DASH_TOAST_WARN, "Sound unavailable: adpcm init failed");
		return 0;
	}
	failed = 0;
	for (i = 0; i < DASH_SND_COUNT; i++) {
		samples[i].buffer = NULL;
		if (audsrv_load_adpcm(&samples[i], blob_data(i), blob_size(i)) != 0) {
			samples[i].buffer = NULL; /* sample stays silent */
			failed++;
		}
	}
	/* Keep SFX preloaded even when their toggle is off so they can be enabled
	 * later without reinitializing audsrv (BGM may already own the service). */
	if (dashCfg.sound && failed == DASH_SND_COUNT) {
		dashToast(DASH_TOAST_WARN, "Sound unavailable: no samples loaded");
		return 0;
	}
	if (dashCfg.sound && failed > 0) {
		char msg[64];

		snprintf(msg, sizeof(msg), "%d sound effect%s unavailable", failed, failed == 1 ? "" : "s");
		dashToast(DASH_TOAST_WARN, msg);
	}
	/* channel map and volumes follow Open-PS2-Loader: boot..disconnect on
	 * voices 0..7, cursor rotating over 8..13; set once, here */
	if (dashCfg.sound) {
		for (i = 0; i < 8; i++)
			audsrv_adpcm_set_volume_and_pan(i, dashCfg.sound_vol, 0);
		for (i = 8; i < 14; i++)
			audsrv_adpcm_set_volume_and_pan(i, dashCfg.sound_vol, 0);
	}
	sound_reboot_generation = getIopResetGeneration();
	sound_ready = 1;
	return 1;
}

void dashSoundMaintain(void)
{
	if (!dashCfg.sound && !dashCfg.bgm)
		return;
	if (dashWorkerIoBusy() || dashIopParked())
		return;
	if (soundAlive()) {
		/* A sidecar read deferred by IO/driver startup is tried when idle.
		 * Missing/invalid files are attempted once, not on every frame. */
		if (dashCfg.bgm && bgm_channel < 0 && !bgm_tried)
			dashBgmStart();
		return;
	}
	/* one automatic attempt per IOP session (a broken driver would
	 * otherwise be retried every frame), plus retries that were deferred */
	if (sound_tried_generation == getIopResetGeneration() && !sound_retry)
		return;
	sound_tried_generation = getIopResetGeneration();
	sound_retry = 0;
	if (dashSoundInit())
		dashBgmStart();
}

void dashSoundShutdown(void)
{
	if (soundAlive()) {
		/*
		 * BGM is a looping SPU2 ADPCM voice. Merely setting its volume to
		 * zero and dropping the EE RPC client can leave the IOP/SPU2 voice
		 * alive across a keep-IOP ExecPS2 handoff. audsrv_adpcm_init() is the
		 * library's hard reset for ADPCM state: it stops every voice and frees
		 * the uploaded sample list before audsrv_quit() releases RPC.
		 */
		dashBgmStop();
		audsrv_adpcm_init();
		audsrv_stop_audio();
		audsrv_quit();
	}
	sound_ready = 0;
	bgm_loaded = 0;
	bgm_tried = 0;
	bgm_channel = -1;
	bgm_sample.buffer = NULL;
	free(bgm_data);
	bgm_data = NULL;
}

/* voice map: boot..disconnect on 0..7, cursor rotates over 8..13 (OPL-style) */
#define SND_CURSOR_CH_BASE 8
#define SND_CURSOR_CH_COUNT 6

void dashSoundApplyVolume(void)
{
	int i;

	if (!soundAlive())
		return;
	for (i = 0; i < 8 + SND_CURSOR_CH_COUNT; i++)
		audsrv_adpcm_set_volume_and_pan(i, dashCfg.sound_vol, 0);
}

void dashSoundPlay(int id)
{
	static int cursorSlot;
	static u64 lastCursorMs;
	int ch;

	if (!soundAlive() || !dashCfg.sound || id < 0 || id >= DASH_SND_COUNT)
		return;
	if (dashIopParked())
		return; /* a background job is rebooting the IOP */
	if (samples[id].buffer == NULL)
		return;
	if (id == DASH_SND_CURSOR) {
		u64 now = Timer();

		if (now - lastCursorMs < 45)
			return; /* OPL: no machine-gun ticks on autorepeat */
		lastCursorMs = now;
		ch = SND_CURSOR_CH_BASE + cursorSlot;
		cursorSlot = (cursorSlot + 1) % SND_CURSOR_CH_COUNT;
	} else {
		ch = id; /* any fixed voice < 8; cursor occupies 8..13 */
	}
	audsrv_ch_play_adpcm(ch, &samples[id]);
}

//--------------------------------------------------------------
// Background music
//
// Optional BGM.ADP beside the boot ELF: an adpenc-produced mono PS-ADPCM
// track is uploaded once to SPU2 RAM, then the SPU2 loops it in hardware.
// Voice 23 is reserved exclusively for BGM; interface sounds use 0..13.
//--------------------------------------------------------------

#define BGM_MAX_BYTES (1024 * 1024)

static int loadBgmSidecar(void)
{
	char path[MAX_PATH], fixed[MAX_PATH];
	const char *dir = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
	int fd, size, got, n, closed;
	u8 *data;

	n = snprintf(path, sizeof(path), "%s%sBGM.ADP", dir,
	             dir[0] && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\' && dir[strlen(dir) - 1] != ':' ? "/" : "");
	if (n < 0 || n >= sizeof(path))
		return 0;
	filerRootMcPollIdle();
	if (genFixPath(path, fixed) < 0)
		return 0;
	fd = genOpen(fixed, FIO_O_RDONLY);
	if (fd < 0)
		return 0; /* entirely optional, including on read-only boot devices */
	size = genLseek(fd, 0, SEEK_END);
	if (size < 32 || size > BGM_MAX_BYTES || (size & 15) || genLseek(fd, 0, SEEK_SET) < 0) {
		genClose(fd);
		return 0;
	}
	data = memalign(64, size + 64); /* padded for audsrv's DMA transfer */
	if (data == NULL) {
		genClose(fd);
		return 0;
	}
	memset(data, 0, size + 64);
	for (got = 0; got < size; got += n) {
		n = genRead(fd, data + got, size - got);
		if (n <= 0) break;
	}
	closed = genClose(fd);
	/* audsrv reads a 16-byte APCM header before transferring 16-byte ADPCM
	 * blocks. Only mono tracks with a valid SPU2 pitch fit our one voice. */
	if (got != size || closed < 0 || memcmp(data, "APCM", 4) || data[4] != 1 ||
	    data[5] != 1 || data[6] > 1 || data[7] != 0 ||
	    *(u32 *)(data + 8) == 0 || *(u32 *)(data + 8) > 0x3fff) {
		free(data);
		return 0;
	}
	bgm_sample.buffer = NULL;
	if (audsrv_load_adpcm(&bgm_sample, data, size) != 0) {
		free(data);
		bgm_sample.buffer = NULL;
		return 0;
	}
	bgm_data = data;
	bgm_loaded = 1;
	return 1;
}

void dashBgmSetVolume(void)
{
	if (!soundAlive() || bgm_channel < 0 || dashIopParked())
		return;
	audsrv_adpcm_set_volume_and_pan(bgm_channel, dashCfg.bgm ? dashCfg.bgm_vol : 0, 0);
}

void dashBgmStop(void)
{
	bgm_tried = 0; /* an explicit off/on can discover a newly added sidecar */
	/* audsrv has no per-channel stop API. Keep the looping voice alive but
	 * muted so re-enabling BGM is instant and does not allocate another voice. */
	if (soundAlive() && bgm_channel >= 0 && !dashIopParked())
		audsrv_adpcm_set_volume_and_pan(bgm_channel, 0, 0);
}

void dashBgmStart(void)
{
	int ch;

	if (!dashCfg.bgm || dashIopParked() || dashWorkerIoBusy())
		return;
	if (!soundAlive() && !dashSoundInit())
		return;

	/* Already running: section/menu changes only preserve/restore volume. */
	if (bgm_channel >= 0) {
		dashBgmSetVolume();
		return;
	}

	if (!bgm_loaded) {
		const char *dir = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
		/* Use only an already active boot stack. Reading music must not start
		 * storage drivers or reset the IOP merely to look for an optional file. */
		if (bgm_tried || !isPathDeviceStackLoaded(dir))
			return;
		bgm_tried = 1;
		if (!loadBgmSidecar())
			return;
	}

	ch = audsrv_ch_play_adpcm(DASH_BGM_CHANNEL, &bgm_sample);
	if (ch < 0) {
		dashToast(DASH_TOAST_WARN, "Music unavailable: reserved SPU2 voice busy");
		return;
	}
	bgm_channel = ch;
	dashBgmSetVolume();
}
