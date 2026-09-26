//--------------------------------------------------------------
//File name:   dash_sound.h
//Dashboard UI sounds (audsrv ADPCM one-shots). Every call is a
//safe no-op when sound is off or the driver failed to start.
//--------------------------------------------------------------
#ifndef DASH_SOUND_H
#define DASH_SOUND_H

enum DashSound {
	DASH_SND_CURSOR = 0,
	DASH_SND_CONFIRM,
	DASH_SND_CANCEL,
	DASH_SND_TRANSITION,
	DASH_SND_MESSAGE,
	DASH_SND_BOOT,
	DASH_SND_CONNECT,
	DASH_SND_DISCONNECT,
	DASH_SND_COUNT
};

int dashSoundInit(void);
void dashSoundShutdown(void);
void dashSoundPlay(int id);
void dashBgmStart(void);
void dashBgmStop(void);

#endif
