#ifndef GUI_NEUTRINO_H
#define GUI_NEUTRINO_H

#define NEUTRINO_OPTION_ARGC_MAX 4
#define NEUTRINO_VMC_NAME_MAX 64

typedef struct
{
	unsigned int game_compatibility;
	int video_mode;
	int video_compatibility;
	int debug_colors;
	int logo;
	char vmc[2][NEUTRINO_VMC_NAME_MAX]; /* VMC/<name>.bin for -mc0/-mc1; set by hand ($VMC_0/$VMC_1) */
} NEUTRINO_LAUNCH_OPTIONS;

typedef struct
{
	char game_arg[sizeof("-gc=02357")];
	char video_arg[sizeof("-gsm=1080ix3:3")];
	char *argv[NEUTRINO_OPTION_ARGC_MAX];
} NEUTRINO_OPTION_ARGS;

int showNeutrinoLaunchOptions(NEUTRINO_LAUNCH_OPTIONS *options);
int showNeutrinoLaunchOptionsForGame(NEUTRINO_LAUNCH_OPTIONS *options, const char *game_id, const char *game_path);
int editNeutrinoGameProfile(const char *game_id, const char *game_path);
int neutrinoVmcNameValid(const char *name);
int buildNeutrinoOptionArgs(const NEUTRINO_LAUNCH_OPTIONS *options, NEUTRINO_OPTION_ARGS *args);

#endif
