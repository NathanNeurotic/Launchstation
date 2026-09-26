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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <vorbis/vorbisfile.h>
#pragma GCC diagnostic pop
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
extern u8 bgm_ogg[];
extern int size_bgm_ogg;

static audsrv_adpcm_t samples[DASH_SND_COUNT];
static int sound_ready;

static void bgmClose(void);

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
	int i, r, failed;

	if (sound_ready)
		return 1;
	if (!dashCfg.sound)
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
	/* the driver does not set a usable master volume by itself */
	audsrv_set_volume(dashCfg.sound_vol);
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
	if (failed == DASH_SND_COUNT) {
		dashToast(DASH_TOAST_WARN, "Sound unavailable: no samples loaded");
		return 0;
	}
	sound_ready = 1;
	return 1;
}

void dashSoundShutdown(void)
{
	if (!sound_ready)
		return;
	bgmClose();
	if (!HasIopRebootedSinceLastCall())
		audsrv_quit();
	sound_ready = 0;
}

void dashSoundPlay(int id)
{
	static int rr;
	static int last_vol = -1;
	int ch;

	if (!sound_ready || !dashCfg.sound || id < 0 || id >= DASH_SND_COUNT)
		return;
	if (samples[id].buffer == NULL)
		return;
	if (dashCfg.sound_vol != last_vol) {
		audsrv_set_volume(dashCfg.sound_vol);
		last_vol = dashCfg.sound_vol;
	}
	/* cursor ping-pongs 0/1 so rapid ticks overlap; everything else lets
	 * the driver pick a free voice so the boot jingle cannot starve beeps */
	ch = (id == DASH_SND_CURSOR) ? (rr++ & 1) : -1;
	if (ch >= 0)
		audsrv_adpcm_set_volume_and_pan(ch, dashCfg.sound_vol, 0);
	audsrv_ch_play_adpcm(ch, &samples[id]);
}

//--------------------------------------------------------------
// Background music
//
// Default source is the embedded bgm.ogg; a bgm.ogg next to the ELF
// (sidecar) overrides it, OPL-style. Decoded with libvorbis in the
// main loop (dashBgmPump feeds audsrv a chunk per frame, no thread).
//--------------------------------------------------------------
typedef struct
{
	const u8 *data;
	long size;
	long pos;
} BgmMem;

static OggVorbis_File bgm_vf;
static BgmMem bgm_mem;
static u8 *bgm_filebuf; /* sidecar copy, NULL when using the embedded blob */
static int bgm_opened;
static int bgm_active;

static size_t bgm_mem_read(void *ptr, size_t size, size_t nmemb, void *ds)
{
	BgmMem *m = (BgmMem *)ds;
	long avail = m->size - m->pos;
	long want = (long)(size * nmemb);
	long n = want < avail ? want : avail;

	if (n <= 0)
		return 0;
	memcpy(ptr, m->data + m->pos, n);
	m->pos += n;
	return (size_t)(n / size);
}

static int bgm_mem_seek(void *ds, ogg_int64_t offset, int whence)
{
	BgmMem *m = (BgmMem *)ds;
	long p;

	switch (whence) {
		case SEEK_SET:
			p = (long)offset;
			break;
		case SEEK_CUR:
			p = m->pos + (long)offset;
			break;
		default:
			p = m->size + (long)offset;
			break;
	}
	if (p < 0 || p > m->size)
		return -1;
	m->pos = p;
	return 0;
}

static long bgm_mem_tell(void *ds)
{
	return ((BgmMem *)ds)->pos;
}

static void bgmClose(void)
{
	if (bgm_opened) {
		ov_clear(&bgm_vf);
		bgm_opened = 0;
	}
	bgm_active = 0;
	if (bgm_filebuf != NULL) {
		free(bgm_filebuf);
		bgm_filebuf = NULL;
	}
}

void dashBgmStart(void)
{
	char path[MAX_PATH];
	const u8 *data;
	long len;
	int size;
	vorbis_info *vi;
	struct audsrv_fmt_t fmt;
	ov_callbacks cb;
	FILE *f;

	if (bgm_active || !sound_ready || !dashCfg.sound)
		return;
	bgmClose();
	data = bgm_ogg;
	size = size_bgm_ogg;
	/* sidecar: <ELF folder>/bgm.ogg wins over the embedded track */
	if (LaunchElfDir[0] && snprintf(path, sizeof(path), "%sbgm.ogg", LaunchElfDir) < (int)sizeof(path)) {
		f = fopen(path, "rb");
		if (f != NULL) {
			fseek(f, 0, SEEK_END);
			len = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (len > 0 && len < 8 * 1024 * 1024) {
				u8 *buf = (u8 *)malloc(len);

				if (buf != NULL && fread(buf, 1, len, f) == (size_t)len) {
					bgm_filebuf = buf;
					data = buf;
					size = (int)len;
				} else {
					free(buf);
				}
			}
			fclose(f);
		}
	}
	bgm_mem.data = data;
	bgm_mem.size = size;
	bgm_mem.pos = 0;
	cb.read_func = bgm_mem_read;
	cb.seek_func = bgm_mem_seek;
	cb.close_func = NULL;
	cb.tell_func = bgm_mem_tell;
	if (ov_open_callbacks(&bgm_mem, &bgm_vf, NULL, 0, cb) < 0) {
		dashToast(DASH_TOAST_WARN, "BGM skipped: not a playable Ogg Vorbis file");
		goto fail;
	}
	bgm_opened = 1;
	vi = ov_info(&bgm_vf, -1);
	if (vi == NULL || (vi->channels != 1 && vi->channels != 2)) {
		dashToast(DASH_TOAST_WARN, "BGM skipped: needs 1 or 2 channels");
		goto fail;
	}
	fmt.freq = vi->rate;
	fmt.bits = 16;
	fmt.channels = vi->channels;
	if (audsrv_set_format(&fmt) != 0) {
		dashToast(DASH_TOAST_WARN, "BGM skipped: use 44100 or 48000 Hz");
		goto fail;
	}
	bgm_active = 1;
	return;
fail:
	bgmClose();
}

void dashBgmPump(void)
{
	char pcm[2048];
	int n, sec = 0, fed = 0;

	if (!bgm_active || !sound_ready || !dashCfg.sound)
		return;
	/* audsrv's ring buffer is only ~10-20 KB; feed a little each frame
	 * (at most 2 chunks) so we never block and never outrun the buffer */
	while (fed < 2 && audsrv_available() >= (int)sizeof(pcm)) {
		n = ov_read(&bgm_vf, pcm, sizeof(pcm), 0, 2, 1, &sec);
		if (n <= 0) {
			if (ov_pcm_seek(&bgm_vf, 0) != 0) {
				bgm_active = 0;
				return;
			}
			n = ov_read(&bgm_vf, pcm, sizeof(pcm), 0, 2, 1, &sec);
			if (n <= 0) {
				bgm_active = 0;
				return;
			}
		}
		audsrv_play_audio(pcm, n);
		fed++;
	}
}
