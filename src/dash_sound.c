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
extern u8 sfx_connect_adp[];
extern int size_sfx_connect_adp;
extern u8 sfx_disconnect_adp[];
extern int size_sfx_disconnect_adp;

extern u8 sfx2_cursor_adp[];
extern int size_sfx2_cursor_adp;
extern u8 sfx2_confirm_adp[];
extern int size_sfx2_confirm_adp;
extern u8 sfx2_cancel_adp[];
extern int size_sfx2_cancel_adp;
extern u8 sfx2_transition_adp[];
extern int size_sfx2_transition_adp;
extern u8 sfx2_message_adp[];
extern int size_sfx2_message_adp;
extern u8 sfx2_boot_adp[];
extern int size_sfx2_boot_adp;
extern u8 sfx2_bd_connect_adp[];
extern int size_sfx2_bd_connect_adp;
extern u8 sfx2_bd_disconnect_adp[];
extern int size_sfx2_bd_disconnect_adp;

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
static int theme_reload_pending;
static int sound_job_id, sound_closing, sound_boot_pending, sound_volume_pending;
typedef struct SoundJob {
	unsigned int generation;
	int initialize, reset_samples, theme, effects, volume, music, ready;
	char directory[MAX_PATH], warning[128];
} SoundJob;
static SoundJob sound_job;
static int loadBgmSidecar(const char *dir);



/* audsrv lives on the IOP: after any IOP reboot the server and the uploaded
 * samples are gone, and calling into the stale RPC client hangs. Every
 * entry point checks this first. */
static int soundAlive(void)
{
	return sound_ready && getIopResetGeneration() == sound_reboot_generation;
}

/* SPU2 voice allocation (24 hardware voices total):
 *   0..7: fixed single-voice SFX (boot, confirm, cancel, message, connect, disconnect)
 *   8..13: cursor SFX (6-voice rotating pool)
 *  14..17: transition SFX (4-voice rotating pool for L1/R1, #136)
 *  18..22: general SFX headroom
 *     23: DASH_BGM_CHANNEL (looping background music, isolated from SFX)
 */
#define DASH_BGM_CHANNEL 23
#define SND_SFX_CH_MAX 23
#define SND_CURSOR_CH_BASE 8
#define SND_CURSOR_CH_COUNT 6
#define SND_TRANS_CH_BASE 14
#define SND_TRANS_CH_COUNT 4

/* Theme 0 is the original set, theme 1 the synthesized "PS2" set. */
#define SND_THEME_PS2 1

static void *blob_data(int id, int theme)
{
	if (theme == SND_THEME_PS2) {
		switch (id) {
			case DASH_SND_CURSOR: return sfx2_cursor_adp;
			case DASH_SND_CONFIRM: return sfx2_confirm_adp;
			case DASH_SND_CANCEL: return sfx2_cancel_adp;
			case DASH_SND_TRANSITION: return sfx2_transition_adp;
			case DASH_SND_MESSAGE: return sfx2_message_adp;
			case DASH_SND_BOOT: return sfx2_boot_adp;
			case DASH_SND_CONNECT: return sfx2_bd_connect_adp;
			default: return sfx2_bd_disconnect_adp;
		}
	}
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

static int blob_size(int id, int theme)
{
	if (theme == SND_THEME_PS2) {
		switch (id) {
			case DASH_SND_CURSOR: return size_sfx2_cursor_adp;
			case DASH_SND_CONFIRM: return size_sfx2_confirm_adp;
			case DASH_SND_CANCEL: return size_sfx2_cancel_adp;
			case DASH_SND_TRANSITION: return size_sfx2_transition_adp;
			case DASH_SND_MESSAGE: return size_sfx2_message_adp;
			case DASH_SND_BOOT: return size_sfx2_boot_adp;
			case DASH_SND_CONNECT: return size_sfx2_bd_connect_adp;
			default: return size_sfx2_bd_disconnect_adp;
		}
	}
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

/* One job owns audsrv and the permanent sample objects until completion.
 * Settings may change while it runs; the worker reads only this snapshot. */
static void soundJobRun(void *arg)
{
	SoundJob *job = arg;
	int i, result;
	if (job->generation != getIopResetGeneration()) return;
	if (job->initialize) {
		if (SifExecModuleBuffer(libsd_irx, size_libsd_irx, 0, NULL, &result) < 0 || result < 0) {
			snprintf(job->warning, sizeof(job->warning), "Sound unavailable: libsd would not start"); return;
		}
		if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &result) < 0 || result < 0) {
			snprintf(job->warning, sizeof(job->warning), "Sound unavailable: audio driver would not start"); return;
		}
		if (audsrv_init() != 0) {
			snprintf(job->warning, sizeof(job->warning), "Sound unavailable: audio init failed"); return;
		}
	}
	if (job->reset_samples) {
		int failed = 0;
		if (audsrv_adpcm_init() != 0) {
			if (job->initialize) audsrv_quit();
			snprintf(job->warning, sizeof(job->warning), "Sound unavailable: adpcm init failed"); return;
		}
		/* The hard reset also drops the music voice and its old sample ID. */
		bgm_loaded = bgm_tried = 0; bgm_channel = -1; bgm_sample.buffer = NULL;
		free(bgm_data); bgm_data = NULL;
		for (i = 0; i < DASH_SND_COUNT; i++) {
			samples[i].buffer = NULL;
			if (audsrv_load_adpcm(&samples[i], blob_data(i, job->theme), blob_size(i, job->theme)) != 0) {
				samples[i].buffer = NULL; failed++;
			}
		}
		if (job->effects && failed)
			snprintf(job->warning, sizeof(job->warning), "%d sound effect%s unavailable", failed, failed == 1 ? "" : "s");
		for (i = 0; i < SND_SFX_CH_MAX; i++)
			audsrv_adpcm_set_volume_and_pan(i, job->effects ? job->volume : 0, 0);
	}
	job->ready = 1;
	if (job->music) {
		bgm_tried = 1;
		loadBgmSidecar(job->directory);
	}
}

static void soundJobDone(void *arg)
{
	SoundJob *job = arg;
	sound_job_id = 0;
	if (job->generation != getIopResetGeneration()) {
		/* Do not contact the destroyed RPC server, even for cleanup. */
		sound_ready = bgm_loaded = bgm_tried = 0; bgm_channel = -1;
		bgm_sample.buffer = NULL; free(bgm_data); bgm_data = NULL;
		return;
	}
	if (job->ready) {
		sound_ready = 1;
		sound_reboot_generation = job->generation;
		if (job->reset_samples && job->theme != dashCfg.sound_theme) theme_reload_pending = 1;
	}
	if (job->warning[0] && !sound_closing) dashToast(DASH_TOAST_WARN, job->warning);
	if (sound_closing) return;
	dashSoundApplyVolume();
	/* A queue drain may be waiting for this job's ID. Preparation of a
	 * newly enabled BGM bus belongs to Maintain in the next dashboard frame. */
	if (!theme_reload_pending && bgm_loaded) dashBgmStart();
	if (sound_boot_pending && !theme_reload_pending) {
		sound_boot_pending = 0;
		dashSoundPlay(DASH_SND_BOOT);
	}
}

int dashSoundInit(void)
{
	if (!sound_job_id && !soundAlive()) sound_retry = 1;
	dashSoundMaintain();
	return !sound_job_id && soundAlive();
}

void dashSoundReloadTheme(void)
{
	if (soundAlive() || sound_job_id) theme_reload_pending = 1;
	dashSoundMaintain();
}

void dashSoundMaintain(void)
{
	const char *dir;
	int initialize, reset_samples, music;
	if (sound_closing || sound_job_id || dashWorkerIoBusy() || dashIopParked()) return;
	if (!dashCfg.sound && !dashCfg.bgm) return;
	initialize = !soundAlive();
	if (initialize && sound_tried_generation == getIopResetGeneration() && !sound_retry) return;
	if (initialize && sound_ready) dashSoundShutdown();
	reset_samples = initialize || theme_reload_pending;
	dir = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
	music = dashCfg.bgm && (reset_samples || (!bgm_loaded && !bgm_tried)) && isPathDeviceStackLoaded(dir);
	if (!reset_samples && !music) {
		if (sound_volume_pending) { dashSoundApplyVolume(); dashBgmSetVolume(); }
		if (dashCfg.bgm && bgm_loaded && bgm_channel < 0) dashBgmStart();
		return;
	}
	memset(&sound_job, 0, sizeof(sound_job));
	sound_job.generation = getIopResetGeneration();
	sound_job.initialize = initialize; sound_job.reset_samples = reset_samples;
	sound_job.theme = dashCfg.sound_theme; sound_job.effects = dashCfg.sound;
	sound_job.volume = dashCfg.sound_vol; sound_job.music = music;
	snprintf(sound_job.directory, sizeof(sound_job.directory), "%s", dir);
	sound_job_id = dashWorkerSubmit("Preparing audio", 0, soundJobRun, soundJobDone, &sound_job);
	if (sound_job_id < 0) { sound_job_id = 0; return; } /* retry without blocking the UI */
	theme_reload_pending = 0;
	if (initialize) {
		sound_tried_generation = sound_job.generation;
		sound_retry = 0;
	}
}

void dashSoundShutdown(void)
{
	sound_closing = 1; /* a draining job must never enqueue a replacement */
	if (sound_job_id) dashWorkerAwait(sound_job_id, "Finishing audio", 0);
	if (soundAlive()) {
		audsrv_adpcm_init();
		audsrv_stop_audio();
		audsrv_quit();
	}
	sound_ready = bgm_loaded = bgm_tried = 0;
	sound_boot_pending = sound_volume_pending = theme_reload_pending = 0;
	bgm_channel = -1; bgm_sample.buffer = NULL;
	free(bgm_data); bgm_data = NULL;
	sound_closing = 0;
}

void dashSoundSuspend(void)
{
	int i;

	if (sound_job_id || sound_closing || !soundAlive() || dashIopParked())
		return;
	/* Keep the RPC client and samples valid until the launch is committed.
	 * audsrv_quit() destroys client state that audsrv_init() cannot recreate
	 * in the same IOP session. Failed launches must be able to resume music. */
	for (i = 0; i < SND_SFX_CH_MAX; i++)
		audsrv_adpcm_set_volume_and_pan(i, 0, 0);
	if (bgm_channel >= 0)
		audsrv_adpcm_set_volume_and_pan(bgm_channel, 0, 0);
}

void dashSoundApplyVolume(void)
{
	int i;

	if (sound_job_id || sound_closing || !soundAlive() || dashIopParked()) {
		sound_volume_pending = 1; return;
	}
	sound_volume_pending = 0;
	for (i = 0; i < SND_SFX_CH_MAX; i++)
		audsrv_adpcm_set_volume_and_pan(i, dashCfg.sound ? dashCfg.sound_vol : 0, 0);
}

void dashSoundPlay(int id)
{
	static int cursorSlot;
	static int transSlot;
	int ch, i;

	if (sound_job_id || sound_closing || (!soundAlive() && sound_retry)) {
		if (id == DASH_SND_BOOT && !sound_closing) sound_boot_pending = 1;
		return;
	}
	if (!soundAlive() || !dashCfg.sound || id < 0 || id >= DASH_SND_COUNT)
		return;
	if (dashIopParked())
		return; /* a background job is rebooting the IOP */
	if (samples[id].buffer == NULL)
		return;
	if (id == DASH_SND_CURSOR) {
		/* One tick per move: pad autorepeat already paces scrolling (down to
		 * 40 ms), and a 45 ms gate here dropped every other tick at full
		 * speed. The rotating voices keep ticks from cutting each other off.
		 * From saildot4k/Launchstation's sound audit. */
		ch = SND_CURSOR_CH_BASE + cursorSlot;
		cursorSlot = (cursorSlot + 1) % SND_CURSOR_CH_COUNT;
	} else if (id == DASH_SND_TRANSITION) {
		ch = SND_TRANS_CH_BASE + transSlot;
		transSlot = (transSlot + 1) % SND_TRANS_CH_COUNT;
	} else {
		ch = id; /* any fixed voice < 8; cursor occupies 8..13, transition 14..17 */
	}
	/* saildot4k's fallback: restore gain before key-on, and never let
	 * audsrv's automatic allocation choose the reserved music voice.
	 * An explicit voice replaces whatever is sounding there (audsrv.h), so
	 * this loop covers a failed RPC or rejected voice, not a busy one. The
	 * round-robin cursor and transition slots above keep repeats from
	 * cutting each other off; per-voice busy probes would add an IOP round
	 * trip per voice on every cursor tick. */
	audsrv_adpcm_set_volume_and_pan(ch, dashCfg.sound_vol, 0);
	if (audsrv_ch_play_adpcm(ch, &samples[id]) >= 0) return;
	for (i = 0; i < SND_SFX_CH_MAX; i++) {
		int fallback = (18 + i) % SND_SFX_CH_MAX;
		if (fallback == ch) continue;
		audsrv_adpcm_set_volume_and_pan(fallback, dashCfg.sound_vol, 0);
		if (audsrv_ch_play_adpcm(fallback, &samples[id]) >= 0) return;
	}
}

//--------------------------------------------------------------
// Background music
//
// Optional BGM.ADP beside the boot ELF: an adpenc-produced mono PS-ADPCM
// track is uploaded once to SPU2 RAM, then the SPU2 loops it in hardware.
// Voice 23 is reserved exclusively for BGM; interface sounds use 0..22.
//--------------------------------------------------------------

#define BGM_MAX_BYTES (1024 * 1024)

/* SPU2 does not loop merely because the audsrv APCM header requests it.
 * The 16-byte ADPCM frames ALSO require a loop-start marker (6), a
 * repeating section (2), and an end/repeat marker (3). A one-shot-encoded
 * BGM can otherwise continue reading stale SPU RAM at the end of the sample,
 * producing a sustained buzzing/horn note. Work on our own EE copy only;
 * never rewrite a user's BGM.ADP sidecar. */
static int prepareBgmLoop(u8 *data, int size)
{
	int i, end;

	if (data == NULL || size < 16 + 32 || ((size - 16) & 15))
		return 0;
	end = size - 16;
	for (i = 16; i < size; i += 16) {
		/* Mask away old one-shot flags rather than keeping an incompatible
		 * loop/end combination from whichever encoder was used. */
		data[i + 1] = (i == 16) ? 6 : (i == end) ? 3 : 2;
	}
	data[6] = 1; /* APCM 'loop' byte */
	return 1;
}

static int loadBgmSidecar(const char *dir)
{
	char path[MAX_PATH], fixed[MAX_PATH];
	int fd, size, got, n, closed;
	u8 *data;

	n = snprintf(path, sizeof(path), "%s%sBGM.ADP", dir,
	             dir[0] && dir[strlen(dir) - 1] != '/' && dir[strlen(dir) - 1] != '\\' && dir[strlen(dir) - 1] != ':' ? "/" : "");
	if (n < 0 || n >= (int)sizeof(path))
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
	/* The soundtrack is a permanent looping voice, unlike one-shot SFX.
	 * Normalize SPU2 per-frame loop control BEFORE audsrv DMA uploads it. */
	if (!prepareBgmLoop(data, size)) {
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
	if (sound_job_id || sound_closing || !soundAlive() || bgm_channel < 0 || dashIopParked()) {
		sound_volume_pending = 1; return;
	}
	audsrv_adpcm_set_volume_and_pan(bgm_channel, dashCfg.bgm ? dashCfg.bgm_vol : 0, 0);
}

void dashBgmStop(void)
{
	bgm_tried = 0; /* an explicit off/on can discover a newly added sidecar */
	if (sound_job_id) sound_volume_pending = 1;
	/* audsrv has no per-channel stop API. Keep the looping voice alive but
	 * muted so re-enabling BGM is instant and does not allocate another voice. */
	if (!sound_job_id && !sound_closing && soundAlive() && bgm_channel >= 0 && !dashIopParked())
		audsrv_adpcm_set_volume_and_pan(bgm_channel, 0, 0);
}

void dashBgmStart(void)
{
	int ch;
	if (!dashCfg.bgm || sound_closing || sound_job_id || dashIopParked() || dashWorkerIoBusy()) return;
	if (!soundAlive() || !bgm_loaded) { dashSoundMaintain(); return; }
	if (bgm_channel >= 0) { dashBgmSetVolume(); return; }
	ch = audsrv_ch_play_adpcm(DASH_BGM_CHANNEL, &bgm_sample);
	if (ch < 0) {
		dashToast(DASH_TOAST_WARN, "Music unavailable: reserved SPU2 voice busy"); return;
	}
	bgm_channel = ch;
	dashBgmSetVolume();
}
