//--------------------------------------------------------------
//File name:   dash_main.c
//Dashboard shell: main loop, section switching, boot-time launch-key
//compatibility and action dispatch.
//
//Every launch or tool goes through ExecuteMainAction(), the same proven
//path the classic launch-key menu and the FileBrowser use (argument
//sidecars, device loading, KELF/HDD/PSX handling, cleanup before exec).
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "filer_shared.h"
#include "init.h"

extern void (*mainActionLaunchHook)(const char *path);

static MainExecuteContext exec_ctx;
static int dash_active;
static int dash_inited;
static int section = DASH_SEC_HOME;
static int prev_section = DASH_SEC_HOME;
static float section_anim = 1.0f;
static int section_dir = 1;
/* R3 on HOME hides dashboard chrome until R3 is pressed again.
 * This is presentation-only: background updates and all device state stay live. */
static int background_only;
static float anim_time;
static float fade_in;
static int autoprobe_pending;
static u64 last_frame_ms;
static float frame_dt;

/* boot-time launch key emulation */
static int boot_phase;         /* 0 none, 1 init delay, 2 auto-launch countdown */
static u64 boot_phase_end;
static char boot_auto_title[DASH_TITLE_MAX];

static const DashScreen *const screens[DASH_SEC_COUNT] = {
    &dashHomeScreen, &dashAppsScreen, &dashFilesScreen, &dashDevicesScreen, &dashToolsScreen, &dashSettingsScreen};

static const char *const section_names[DASH_SEC_COUNT] = {"HOME", "APPS", "FILES", "DEVICES", "TOOLS", "SETTINGS"};
static const int section_icons[DASH_SEC_COUNT] = {DI_HOME, DI_APPS, DI_FOLDER, DI_DEVICES, DI_TOOLS, DI_SETTINGS};
static const int visible_sections[] = {DASH_SEC_HOME, DASH_SEC_FILES, DASH_SEC_TOOLS, DASH_SEC_SETTINGS};
#define DASH_VISIBLE_SEC_COUNT ((int)(sizeof(visible_sections) / sizeof(visible_sections[0])))

const char *dashSectionName(int s)
{
	return (s >= 0 && s < DASH_SEC_COUNT) ? section_names[s] : "";
}

int dashSectionIcon(int s)
{
	return (s >= 0 && s < DASH_SEC_COUNT) ? section_icons[s] : DI_HOME;
}

int dashCurrentSection(void)
{
	return section;
}

int dashVisibleSectionCount(void)
{
	return DASH_VISIBLE_SEC_COUNT;
}

int dashVisibleSectionAt(int index)
{
	if (index < 0) index = 0;
	if (index >= DASH_VISIBLE_SEC_COUNT) index = DASH_VISIBLE_SEC_COUNT - 1;
	return visible_sections[index];
}

int dashVisibleSectionIndex(int s)
{
	int i;
	if (s == DASH_SEC_DEVICES) s = DASH_SEC_FILES;
	if (s == DASH_SEC_APPS) s = DASH_SEC_HOME;
	for (i = 0; i < DASH_VISIBLE_SEC_COUNT; i++)
		if (visible_sections[i] == s) return i;
	return 0;
}

const char *dashVisibleSectionName(int index)
{
	return dashSectionName(dashVisibleSectionAt(index));
}

int dashAdjacentSection(int s, int direction)
{
	/* All four tabs now have real section state, including the File Browser. */
	int i = dashVisibleSectionIndex(s) + direction;
	if (i < 0) i = DASH_VISIBLE_SEC_COUNT - 1;
	if (i >= DASH_VISIBLE_SEC_COUNT) i = 0;
	return visible_sections[i];
}

float dashTime(void)
{
	return anim_time;
}

float dashFrameDelta(void)
{
	return frame_dt;
}

/* Main, modal and tool loops share the same clock. Nested dialogs consume
 * elapsed time here, so returning to their caller never counts it twice.
 * Keep this visual-only: no section input/update, device polling or IO. */
float dashVisualUpdate(void)
{
	u64 now = Timer();
	float dt = now >= last_frame_ms ? (now - last_frame_ms) / 1000.0f : 0.0f;

	last_frame_ms = now;
	if (dt > 0.1f)
		dt = 0.1f; /* do not jump after a blocking legacy operation */
	frame_dt = dt;
	anim_time += dt;
	dashBgUpdate(dt);
	dashUiUpdate(dt);
	section_anim = dashApproach(section_anim, 1.0f, 9.0f * dashAnimSpeed(), dt);
	fade_in = dashApproach(fade_in, 1.0f, 5.0f * dashAnimSpeed(), dt);
	return dt;
}

float dashAnimSpeed(void)
{
	/* Existing callers multiply their approach speed by this value.
	 * 0% is instant, 100% is normal, and 200% doubles transition duration. */
	if (dashCfg.anim <= 0)
		return 1000.0f;
	return 100.0f / (float)dashCfg.anim;
}

float dashSectionAnim(void)
{
	return section_anim;
}

const MainExecuteContext *dashExecContext(void)
{
	return &exec_ctx;
}

int DashIsActive(void)
{
	return dash_active;
}

const char *DashMiscCommandName(void)
{
	return "Dashboard";
}

int DashIsEnabled(void)
{
	dashStoreInit();
	return dashCfg.enabled;
}

void dashSwitchSection(int s)
{
	if (s == DASH_SEC_APPS)
		s = DASH_SEC_HOME; /* legacy callers use Home's library */
	if (s == DASH_SEC_DEVICES)
		s = DASH_SEC_FILES;
	if (s < 0 || s >= DASH_SEC_COUNT)
		s = DASH_SEC_HOME;
	if (s == section)
		return;
	if (!dashConfirmLeave())
		return;
	/* A tab switch closes only the in-tab browser session, not the
	 * independent modal browser used by launch-key and Tools actions. */
	if (section == DASH_SEC_FILES)
		dashFileBrowserTabClose();
	section_dir = (s > section) ? 1 : -1;
	prev_section = section;
	section = s;
	section_anim = 0.0f;
	dashUiFocusPoke();
	dashSoundPlay(DASH_SND_TRANSITION);
	if (screens[section]->enter)
		screens[section]->enter();
}

int dashConfirmLeave(void)
{
	if (screens[section]->leave == NULL)
		return 1;
	return screens[section]->leave();
}

//--------------------------------------------------------------
// Legacy message hook: driver loads and other drawMsg() calls made while
// the dashboard owns the screen are shown in the loading overlay.
//--------------------------------------------------------------
static void dashMsgHook(const char *msg)
{
	dashLoadingStatus(msg, 0, 0);
}

static void dashLaunchHook(const char *path)
{
	/* Called for every ELF launched through ExecuteMainAction while the
	 * dashboard is active (library, FileBrowser, launch keys). */
	if (path != NULL && strchr(path, ':') != NULL && strlen(path) < DASH_PATH_MAX && dashCfg.recent_count > 0)
		dashStorePushRecent(path);
}

/* Everything the background worker is doing (saves, scans, driver starts)
 * finishes before classic code gets the screen: it is not thread-safe and
 * may reboot the IOP. Returns 0 when the user backed out of the wait. */
static int settleBackgroundWork(const char *title)
{
	dashThemeSavePending(1 << 30); /* never lose theme edits */
	return dashStoreFlushWait(title);
}

int DashPrepareKeepIopHandoff(const char *title)
{
	if (!dash_active)
		return 1;

	/*
	 * RiptOPL's Ember launch drains every background IO user before its
	 * keep-IOP ExecPS2. Do the same here: a worker left inside fileXio RPC
	 * would be destroyed with this EE process while Ember inherits the same
	 * IOP-side RPC/device state. Also end audsrv cleanly rather than leaving
	 * our dashboard audio client/server state behind for Ember to inherit.
	 */
	if (!settleBackgroundWork(title != NULL ? title : "Preparing application"))
		return 0;
	filerRootMcPollIdle();
	dashSoundShutdown();
	return 1;
}

/* After a command that may have rebooted the IOP (network, reboot IOP, a
 * launch that failed): sound and the auto-started devices come back. */
static void afterMiscCommand(void)
{
	drawMsgOverride = dashMsgHook;
	dashClassicUiInstall(1);
	dashInputFlush();
	last_frame_ms = Timer();
	dashSoundMaintain(); /* recover only when the IOP actually changed */
	dashSoundApplyVolume();
	dashBgmStart();
	/* Restart auto-start devices (an IOP reset may have dropped them) from
	 * the main loop. */
	autoprobe_pending = 1;
	dashDevicesResetBaseline(); /* whatever is attached now stays silent */
}

static void showActionMessage(void)
{
	const char *msg = exec_ctx.main_msg;

	if (msg != NULL && msg[0]) {
		dashToast(DASH_TOAST_INFO, msg);
		exec_ctx.main_msg[0] = '\0';
	}
}

/* The MISC commands without a screen of their own (start disc, OSDSYS, PS2
 * browser, network, power off, reboot IOP...) run behind the loading
 * overlay; the ones that leave wLaunchELF hand the screen to the loader. */
static void runMiscCommand(const char *misc_path)
{
	char path[MAX_PATH];
	const char *title = "Working";
	int leaves = 0;
	int network_command = 0;

	if (!stricmp(misc_path, setting->Misc_PS2Disc))
		title = "Starting the disc", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_OSDSYS))
		title = "Starting OSDSYS", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_PS2Browser))
		title = "Opening the PS2 browser", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_PS2PowerOff))
		title = "Powering off", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_PS2Net))
		title = "Starting FTP / remote access", network_command = 1;
	else if (!stricmp(misc_path, setting->Misc_Reboot_IOP))
		title = "Rebooting the IOP";
	else if (!stricmp(misc_path, setting->Misc_Configure) || !stricmp(misc_path, setting->Misc_Exploit_Installer))
		return; /* handled natively by dashRunMisc() */

	dashStrCopy(path, misc_path, sizeof(path));
	/* saves, scans and driver work finish first: the command may reboot
	 * the IOP or leave wLaunchELF */
	if (!settleBackgroundWork(title))
		return;
	filerRootMcPollIdle();
	dashLoadingBegin(title);
	exec_ctx.main_msg[0] = '\0';
#ifdef ETH
#ifdef EXFAT
	/* MrRexD's PC command protocol uses ata0:/PS2-COMANDO.TXT. Keep the
	 * device demand-driven: only bring ATA up when the user explicitly starts
	 * FTP / remote access, and do it before the network so a storage-stack
	 * reset cannot immediately tear FTP back down. FTP itself still starts
	 * even when no internal exFAT HDD is present. */
	if (network_command) {
		dashLoadingStatus("Preparing HDD command inbox", 0, 0);
		ensurePathDeviceStackReady("ata0:/");
	}
#endif
#endif
	if (leaves) {
		/*
		 * Any successful handoff destroys this EE process. Stop audsrv/BGM
		 * before entering the legacy launcher. Mute returnable launches here;
		 * final cleanup quits audsrv once the handoff is committed. Poweroff
		 * has no launch cleanup, so it performs the full teardown here.
		 */
		if (!stricmp(misc_path, setting->Misc_PS2PowerOff))
			dashSoundShutdown();
		else
			dashSoundSuspend();
		dashGfxRestoreLegacyState();
	}
	ExecuteMainAction(path, &exec_ctx);
	/* back here: the command finished, or leaving failed */
	if (leaves)
		dashGfxCheckScreenChange();
	afterMiscCommand();
	dashLoadingEnd();
	showActionMessage();
}

void dashRunMisc(const char *misc_path)
{
	if (misc_path == NULL || misc_path[0] == '\0')
		return;
	/* screens and tools that have a native dashboard version */
	if (!stricmp(misc_path, setting->Misc_Configure)) {
		/* every classic configuration option lives in SETTINGS now */
		dashSwitchSection(DASH_SEC_SETTINGS);
		return;
	}
	if (!stricmp(misc_path, setting->Misc_HddManager)) {
		/* PSX too: like the classic one it manages hdd0: (no formatting) */
		dashHddManager();
		return;
	}
	if (!stricmp(misc_path, setting->Misc_FileBrowser)) {
		dashOpenBrowser("");
		return;
	}
	if (!stricmp(misc_path, setting->Misc_TextEditor)) {
		dashOpenTextEditor(NULL);
		return;
	}
	if (!stricmp(misc_path, setting->Misc_Exploit_Installer)) {
		dashExploitInstaller();
		return;
	}
	if (!stricmp(misc_path, setting->Misc_About_uLE)) {
		dashShowAbout();
		return;
	}
	if (!stricmp(misc_path, setting->Misc_Show_Build_Info)) {
		dashShowBuildInfo();
		return;
	}
	if (!stricmp(misc_path, setting->Misc_Debug_Info)) {
		dashShowSystemInfo();
		return;
	}
	if (!stricmp(misc_path, setting->Misc_ShowFont)) {
		dashShowFontViewer();
		return;
	}
	runMiscCommand(misc_path);
}

void dashOpenBrowser(const char *start_dir)
{
	if (section == DASH_SEC_FILES)
		dashFileBrowserTabOpen(start_dir); /* use the already-active tab */
	else
		dashFileBrowser(start_dir); /* modal path for other callers */
}

/* Navigation shortcuts (Home Last folder and Apps file location) should
 * enter the actual Files tab. Keep dashOpenBrowser() for explicitly modal
 * Tool/MISC browser actions. */
void dashNavigateFilesTab(const char *start_dir)
{
	dashSwitchSection(DASH_SEC_FILES);
	if (section == DASH_SEC_FILES && start_dir != NULL && start_dir[0])
		dashFileBrowserTabOpen(start_dir);
}


int dashOpenTextEditor(const char *file)
{
	if (dashTextEditor(file) == TEXTEDITOR_RESULT_LAUNCH_ARGS) {
		/* like the classic File Browser: the arguments wait for an ELF */
		char elf[MAX_PATH], dir[MAX_PATH];
		const char *slash;
		DashApp tmp;

		dir[0] = '\0';
		if (file != NULL) {
			dashStrCopy(dir, file, sizeof(dir));
			slash = strrchr(dir, '/');
			if (slash != NULL)
				dir[slash - dir + 1] = '\0';
			else
				dir[0] = '\0';
		}
		elf[0] = '\0';
		if (dashPickPath("Start which application with these arguments?", dir, DASH_PICK_FILE, "ELF", elf, sizeof(elf))) {
			dashLaunchApp(dashLibResolve(elf, &tmp), DASH_LAUNCH_PENDING_ARGS);
		}
		LaunchArgsClear();
	}
	return 1;
}

//--------------------------------------------------------------
// Launching
//--------------------------------------------------------------
static int argSidecarExists(const char *elf)
{
	char arg[MAX_PATH], fixed[MAX_PATH];
	const char *slash = strrchr(elf, '/');
	const char *dot = strrchr(slash ? slash : elf, '.');
	int base = dot ? (int)(dot - elf) : (int)strlen(elf);
	int fd;

	if (base + 5 >= (int)sizeof(arg))
		return 0;
	snprintf(arg, sizeof(arg), "%.*s.arg", base, elf);
	if (genFixPath(arg, fixed) < 0)
		return 0;
	fd = genOpen(fixed, FIO_O_RDONLY);
	if (fd < 0)
		return 0;
	genClose(fd);
	return 1;
}

static void friendlyLaunchError(const DashApp *app, const char *tech)
{
	char dev[48], body[200];
	int not_found = (tech != NULL && strstr(tech, LNG(is_Not_Found)) != NULL);
	int not_elf = (tech != NULL && strstr(tech, LNG(This_file_isnt_an_ELF)) != NULL);

	dashLibDeviceLabel(app->path, dev, sizeof(dev));
	if (not_elf)
		snprintf(body, sizeof(body), "\"%s\" is not a valid PlayStation 2 executable, or it is damaged.", app->title);
	else if (not_found || tech == NULL || tech[0] == '\0')
		snprintf(body, sizeof(body), "\"%s\" could not be found. Make sure the %s is connected and the file has not been moved.", app->title, dev);
	else
		snprintf(body, sizeof(body), "\"%s\" could not be started.", app->title);
	dashModalMessage(DI_WARNING, not_elf ? "Not an application" : "Application unavailable", body,
	                 (tech && tech[0]) ? tech : app->path);
	if (not_found && (dashStoreIsFavorite(app->path) || dashStoreIsPinned(app->path) || (app->flags & DASH_APP_TRANSIENT))) {
		if (dashModalConfirm("Remove shortcut?", "Remove this application from Home, Favorites and Recent?", "Remove", 0))
			dashStoreRemoveEverywhere(app->path);
	}
}

void dashLaunchApp(DashApp *app, int with_args_prompt)
{
	char path[MAX_PATH];
	char title[DASH_TITLE_MAX + 16];
	DashApp copy;

	if (app == NULL)
		return;
	memcpy(&copy, app, sizeof(copy));
	dashStrCopy(path, copy.path, sizeof(path));
	if (with_args_prompt != DASH_LAUNCH_PENDING_ARGS)
		LaunchArgsClear();

	if (with_args_prompt == 1) {
		char argp[MAX_PATH];
		const char *slash = strrchr(path, '/');
		const char *dot = strrchr(slash ? slash : path, '.');
		int base = dot ? (int)(dot - path) : (int)strlen(path);
		int r;

		snprintf(argp, sizeof(argp), "%.*s.arg", base, path);
		dashModalMessage(DI_EDIT, "Launch with arguments",
		                 "Write one argument per line, then choose \"Launch with these arguments\" "
		                 "in the Text Editor menu (START). Save keeps them next to the application.",
		                 argp);
		/* the sidecar is opened, or started as a new document with its name */
		r = dashTextEditorEx(argp, 1);
		if (r != TEXTEDITOR_RESULT_LAUNCH_ARGS) {
			LaunchArgsClear();
			return;
		}
	} else if (with_args_prompt == DASH_LAUNCH_PENDING_ARGS) {
		/* arguments already loaded (Text Editor "Launch with these arguments") */
	} else if (!settleBackgroundWork("Finishing up")) {
		return; /* the user backed out while saves/driver work finished */
	} else if ((copy.flags & DASH_APP_HAS_ARGS) && !argSidecarExists(path)) {
		char args[1024];
		int n = dashLibLoadArgs(&copy, args, sizeof(args));
		if (n > 0 && LaunchArgsLoadFromBuffer(copy.path, args, strlen(args), exec_ctx.main_msg, MAX_PATH) < 0) {
			dashModalMessage(DI_WARNING, "Invalid arguments", "The arguments in this application's metadata could not be used.", exec_ctx.main_msg);
			LaunchArgsClear();
			return;
		}
	}

	snprintf(title, sizeof(title), "Starting %s", copy.title);
	if (dashArtTakeDirty())
		dashLibSaveCache();
	dashStorePushRecent(copy.path);
	/* the recent list, cache and anything the worker picked up meanwhile
	 * (e.g. drivers restarted after the Text Editor) are written first */
	if (!settleBackgroundWork(title)) {
		LaunchArgsClear();
		return;
	}
	filerRootMcPollIdle();
	dashBgFlash(0.8f);
	dashLoadingBegin(title);
	dashLoadingStatus(copy.path, 0, 0);
	exec_ctx.main_msg[0] = '\0';
	mainActionLaunchHook = NULL; /* already recorded */
	/*
	 * Ordinary ELF launches used to leave audsrv's looping BGM voice alive.
	 * Mute until the legacy launcher validates the target; final cleanup
	 * stops audsrv. A failed validation can restore the existing audio below.
	 */
	dashSoundSuspend();
	dashGfxRestoreLegacyState();
	dashClassicUiInstall(0); /* launch-time prompts stay classic */
	ExecuteMainAction(path, &exec_ctx);
	/* Only reached when the launch failed. */
	mainActionLaunchHook = dashLaunchHook;
	LaunchArgsClear();
	dashGfxCheckScreenChange();
	drawMsgOverride = dashMsgHook;
	dashClassicUiInstall(1);
	dashSoundMaintain();
	dashSoundApplyVolume();
	dashBgmStart();
	dashLoadingEnd();
	friendlyLaunchError(&copy, exec_ctx.main_msg);
	exec_ctx.main_msg[0] = '\0';
}

//--------------------------------------------------------------
// Library refresh
//--------------------------------------------------------------
/* The scan runs in the background (activity indicator); the library is
 * swapped in by applyScanResult() from the main loop. */
static int scan_count_before = -1;

void dashRescanLibrary(int interactive)
{
	if (dashLibScanRunning() && !interactive)
		return;
	if (interactive && !dashLibScanRunning())
		scan_count_before = dashAppCount;
	if (interactive) {
		/* a full refresh looks everywhere: start the drivers it needs here,
		 * the background scan only reads devices that are running */
		int i;
		for (i = 0; i < dashCfg.nscan; i++)
			dashDevicesStartForPath(dashCfg.scan[i]);
	}
	if (dashLibScanStart(interactive) < 0)
		dashToast(DASH_TOAST_ERROR, "Application scan failed (out of memory)");
	else if (interactive)
		dashToast(DASH_TOAST_INFO, "Looking for applications...");
}

static void applyScanResult(void)
{
	int interactive = 0, n;
	char msg[64];

	n = dashLibApplyScan(&interactive);
	if (n == -2)
		return;
	dashAppsRebuildView();
	dashHomeRebuild();
	if (n < 0)
		dashToast(DASH_TOAST_ERROR, "Application scan failed");
	else if (interactive) {
		int before = scan_count_before;
		snprintf(msg, sizeof(msg), "%d application%s found", n, n == 1 ? "" : "s");
		dashToast(before >= 0 && n > before ? DASH_TOAST_OK : DASH_TOAST_INFO, msg);
		scan_count_before = -1;
	}
}

//--------------------------------------------------------------
// MrRexD-compatible PC game receiver. A complete PS2-RECEBER.TXT switches
// FTP -> UDPFS, streams into the HDD staging directory, writes the result,
// then restores FTP. Only HOME polls it, and only after remote access is on.
//--------------------------------------------------------------
static void pollPcGameReceiver(void)
{
#if defined(ETH) && defined(UDPFS) && defined(EXFAT)
	char msg[MAX_PATH];
	int r;

	if (section != DASH_SEC_HOME || boot_phase || dashWorkerIoBusy() || dashIopParked() || !isFtpNetworkReady())
		return;

	r = pcGameReceiverPoll(msg, sizeof(msg));
	if (r < 0) {
		dashToast(DASH_TOAST_ERROR, msg[0] ? msg : "Invalid PC game-transfer request");
		return;
	}
	if (r == 0)
		return;

	/* Switching between the ETH and UDPFS IOP stacks invalidates dashboard
	 * audio/device state even though the receiver restores FTP before return. */
	afterMiscCommand();
	if (msg[0])
		dashToast(DASH_TOAST_INFO, msg);
#endif
}

//--------------------------------------------------------------
// MrRexD-compatible PC remote commands. The file protocol is polled only on
// HOME after FTP has explicitly been started; no network or HDD work occurs
// during ordinary dashboard boot.
//--------------------------------------------------------------
static void pollRemotePcCommand(void)
{
#ifdef ETH
	char path[MAX_PATH], msg[128];
	DashApp tmp;
	int r;

	if (section != DASH_SEC_HOME || boot_phase || dashWorkerIoBusy() || dashIopParked() || !isFtpNetworkReady())
		return;

	r = remoteCommandPoll(path, sizeof(path), msg, sizeof(msg));
	if (r < 0) {
		dashToast(DASH_TOAST_ERROR, msg[0] ? msg : "Invalid PC command");
		return;
	}
	if (r == 0)
		return;

	if (!stricmp(path, setting->Misc_OSDSYS) || !stricmp(path, setting->Misc_PS2PowerOff)) {
		dashRunMisc(path);
		return;
	}
	if (strchr(path, ':') != NULL) {
		dashToast(DASH_TOAST_INFO, "PC requested application launch");
		dashLaunchApp(dashLibResolve(path, &tmp), 0);
		return;
	}
	dashToast(DASH_TOAST_ERROR, "PC command path is not launchable");
#endif
}

//--------------------------------------------------------------
// Global (START) menu
//--------------------------------------------------------------
static void globalMenu(void)
{
	DashMenuItem items[] = {
	    {DI_HOME, "Home", NULL, 0},
	    {DI_REFRESH, "Refresh application library", NULL, 0},
	    {DI_FOLDER, "File Browser", NULL, 0},
	    {DI_SETTINGS, "Settings", NULL, 0},
	    {DI_REFRESH, "Reboot IOP (reset drivers)", NULL, 0},
	    {DI_POWER, "Power off", NULL, 0}};
	int r = dashModalMenu("Dashboard", NULL, items, sizeof(items) / sizeof(items[0]), 0);

	/* Switching tabs handles any unsaved Settings confirmation inside
	 * dashSwitchSection(). System actions require their own leave guard. */
	if ((r == 4 || r == 5) && !dashConfirmLeave())
		return;
	switch (r) {
		case 0:
			dashSwitchSection(DASH_SEC_HOME);
			break;
		case 1:
			dashRescanLibrary(1);
			break;
		case 2:
			dashSwitchSection(DASH_SEC_FILES);
			break;
		case 3:
			dashSwitchSection(DASH_SEC_SETTINGS);
			break;
		case 4:
			if (dashModalConfirm("Reboot IOP", "All loaded device drivers will be restarted. Devices will be detected again when used.", "Reboot", 0))
				dashRunMisc(setting->Misc_Reboot_IOP);
			break;
		case 5:
			if (dashModalConfirm("Power off", "Turn off the PlayStation 2 now?", "Power off", 1))
				dashRunMisc(setting->Misc_PS2PowerOff);
			break;
	}
}

//--------------------------------------------------------------
// Boot behaviour (mirrors the classic menu's Init_Delay / LK_auto logic)
//--------------------------------------------------------------
extern u32 paddata;

/* launch-key buttons, in the classic menu's order */
static const struct
{
	u32 mask;
	int lk;
	int icon;
} lk_map[] = {
    {PAD_CIRCLE, SETTING_LK_CIRCLE, DI_BTN_CIRCLE},
    {PAD_CROSS, SETTING_LK_CROSS, DI_BTN_CROSS},
    {PAD_SQUARE, SETTING_LK_SQUARE, DI_BTN_SQUARE},
    {PAD_TRIANGLE, SETTING_LK_TRIANGLE, DI_BTN_TRIANGLE},
    {PAD_L1, SETTING_LK_L1, DI_BTN_L1},
    {PAD_R1, SETTING_LK_R1, DI_BTN_R1},
    {PAD_L2, SETTING_LK_L2, DI_BTN_L2},
    {PAD_R2, SETTING_LK_R2, DI_BTN_R2},
    {PAD_L3, SETTING_LK_L3, DI_BTN_L3},
    {PAD_R3, SETTING_LK_R3, DI_BTN_R3},
    {PAD_START, SETTING_LK_START, DI_BTN_START},
    {PAD_SELECT, SETTING_LK_SELECT, DI_BTN_SELECT},
    {PAD_LEFT, SETTING_LK_LEFT, DI_BTN_LEFT},
    {PAD_RIGHT, SETTING_LK_RIGHT, DI_BTN_RIGHT}};
#define LK_MAP_N ((int)(sizeof(lk_map) / sizeof(lk_map[0])))

/* a launch key is usable: a path is set (LEFT/RIGHT only when enabled) */
static int lkUsable(int lk)
{
	if ((lk == SETTING_LK_LEFT || lk == SETTING_LK_RIGHT) && !setting->LK_Flag[lk])
		return 0;
	return setting->LK_Path[lk][0] != '\0';
}

static int bootHeldLaunchKey(void)
{
	int i;

	waitAnyPadReady();
	readpad_no_KB();
	for (i = 0; i < LK_MAP_N; i++)
		if ((paddata & lk_map[i].mask) && lkUsable(lk_map[i].lk))
			return lk_map[i].lk;
	return -1;
}

static void runLaunchKey(int lk)
{
	DashApp tmp;

	if (lk < 0 || !setting->LK_Path[lk][0])
		return;
	if (strchr(setting->LK_Path[lk], ':') != NULL && genCmpFileExt(setting->LK_Path[lk], "ELF")) {
		DashApp *a = dashLibResolve(setting->LK_Path[lk], &tmp);
		if (lk < SETTING_LK_BTN_COUNT && setting->LK_Title[lk][0])
			dashStrCopy(a->title, setting->LK_Title[lk], sizeof(a->title));
		dashLaunchApp(a, 0);
	} else
		dashRunMisc(setting->LK_Path[lk]);
}

//--------------------------------------------------------------
// Quick launch: the classic menu's "press a button to start its
// application". Hold SELECT: the launch keys appear, and a button starts
// its application. A short SELECT tap keeps its normal meaning.
//--------------------------------------------------------------
static int ql_down, ql_used;
static u64 ql_since;

static void quickLaunchInput(DashInput *in)
{
	int i;

	if (in->raw_new & PAD_SELECT) {
		ql_down = 1;
		ql_used = 0;
		ql_since = Timer();
		in->pressed &= ~DASH_IN_SELECT;
	}
	if (!ql_down)
		return;
	if (paddata & PAD_SELECT) {
		u32 other = in->raw_new & ~PAD_SELECT;
		/* while SELECT is held the buttons are launch keys, not navigation */
		in->pressed = 0;
		in->any = 0;
		if (!other)
			return;
		ql_used = 1;
		for (i = 0; i < LK_MAP_N; i++) {
			if (lk_map[i].mask == PAD_SELECT || !(other & lk_map[i].mask))
				continue;
			if (lkUsable(lk_map[i].lk)) {
				ql_down = 0;
				dashSoundPlay(DASH_SND_CONFIRM);
				runLaunchKey(lk_map[i].lk);
				dashInputFlush();
				return;
			}
		}
		dashToast(DASH_TOAST_INFO, "No application on that button (Settings > Launching > Launch keys)");
		return;
	}
	/* released: a short tap without another button is a normal SELECT */
	ql_down = 0;
	if (!ql_used && Timer() - ql_since < 500) {
		in->pressed |= DASH_IN_SELECT;
		in->any = 1;
	}
}

static void lkLabel(int lk, char *out, int size)
{
	const char *p = setting->LK_Path[lk], *s;

	if (setting->LK_Title[lk][0]) {
		dashStrCopy(out, setting->LK_Title[lk], size);
		return;
	}
	s = strrchr(p, '/');
	if (s == NULL)
		s = strrchr(p, ':');
	dashStrCopy(out, s != NULL ? s + 1 : p, size);
}

static void quickLaunchDraw(void)
{
	int i, n = 0, col;
	float w = 520.0f, h, x, y, row_h = 28.0f, k;
	char label[64];

	if (!ql_down || !(paddata & PAD_SELECT) || Timer() - ql_since < 250)
		return;
	for (i = 0; i < LK_MAP_N; i++)
		if (lk_map[i].mask != PAD_SELECT && lkUsable(lk_map[i].lk))
			n++;
	h = 70.0f + (n > 0 ? ((n + 1) / 2) * row_h : row_h) + 12.0f;
	x = (DASH_VW - w) * 0.5f;
	y = (DASH_VH - h) * 0.5f;
	k = dashClampf((Timer() - ql_since - 250) / 120.0f, 0.0f, 1.0f);
	dashModalBackdrop(k * 0.8f);
	dashGfxPushAlpha(k);
	dashModalPanel(x, y, w, h, k);
	dashIcon(DI_KEY, x + 24, y + 18, 24, DASH_C_ACCENT);
	dashTextS(x + 58, y + 20, "Launch keys", DASH_FS_LABEL, DASH_C_TEXT);
	dashText(x + 58, y + 44, "Keep SELECT held and press a button", DASH_FS_CAPTION, DASH_C_TEXT3);
	if (n == 0)
		dashText(x + 28, y + 72, "Set launch keys in Settings > Launching > Launch keys.", DASH_FS_SMALL, DASH_C_TEXT2);
	for (i = 0, col = 0; i < LK_MAP_N; i++) {
		float bx, by;
		if (lk_map[i].mask == PAD_SELECT || !lkUsable(lk_map[i].lk))
			continue;
		bx = x + 24 + (col % 2) * (w - 48) * 0.5f;
		by = y + 70 + (col / 2) * row_h;
		bx += dashDrawButton(bx, by, lk_map[i].icon, NULL) + 8;
		lkLabel(lk_map[i].lk, label, sizeof(label));
		dashTextFit(bx, by + 4, (w - 48) * 0.5f - 44, label, DASH_FS_SMALL, DASH_C_TEXT);
		col++;
	}
	dashGfxPopAlpha();
}

static void bootBegin(void)
{
	boot_phase = 0;
	if (setting->Init_Delay > 0) {
		boot_phase = 1;
		boot_phase_end = Timer() + setting->Init_Delay * 1000;
	} else {
		int lk = bootHeldLaunchKey();
		if (lk >= 0) {
			runLaunchKey(lk);
			return;
		}
		if (setting->LK_Path[SETTING_LK_AUTO][0]) {
			boot_phase = 2;
			boot_phase_end = Timer() + (setting->timeout + 1) * 1000;
		}
	}
	if (setting->LK_Path[SETTING_LK_AUTO][0]) {
		const char *p = setting->LK_Path[SETTING_LK_AUTO];
		if (setting->LK_Title[SETTING_LK_AUTO][0])
			dashStrCopy(boot_auto_title, setting->LK_Title[SETTING_LK_AUTO], sizeof(boot_auto_title));
		else if (strchr(p, ':') != NULL)
			dashLibPrettyName(p, boot_auto_title, sizeof(boot_auto_title));
		else {
			const char *slash = strrchr(p, '/');
			dashStrCopy(boot_auto_title, slash ? slash + 1 : p, sizeof(boot_auto_title));
		}
	}
}

static void bootUpdate(const DashInput *in)
{
	u64 now = Timer();

	if (boot_phase == 1) {
		if (now >= boot_phase_end) {
			int lk = bootHeldLaunchKey();
			boot_phase = 0;
			if (lk >= 0) {
				runLaunchKey(lk);
				return;
			}
			if (setting->LK_Path[SETTING_LK_AUTO][0]) {
				boot_phase = 2;
				boot_phase_end = now + (setting->timeout + 1) * 1000;
			}
		}
	} else if (boot_phase == 2) {
		if (in->any) {
			boot_phase = 0;
			dashToast(DASH_TOAST_INFO, "Automatic start cancelled");
		} else if (now >= boot_phase_end) {
			boot_phase = 0;
			runLaunchKey(SETTING_LK_AUTO);
		}
	}
}

static void bootDraw(void)
{
	char msg[128];
	float w, x, y = 72.0f;
	u64 now = Timer();
	int secs;

	if (boot_phase != 2)
		return;
	secs = (int)((boot_phase_end > now ? boot_phase_end - now : 0) / 1000);
	snprintf(msg, sizeof(msg), "Starting %s in %d", boot_auto_title, secs);
	w = dashTextWidth(msg, DASH_FS_BODY) + 230.0f;
	x = (DASH_VW - w) * 0.5f;
	dashShadow(x, y, w, 40, 14, 0.7f);
	{
		DashColor c = dashColorMix(DASH_C_PANEL_HI, DASH_C_SELECT, 0.6f);
		c.a = 235;
		dashPanel(x, y, w, 40, 14, c);
		dashPanelBorder(x, y, w, 40, 14, DASH_C_SELEDGE);
	}
	dashIcon(DI_CLOCK, x + 14, y + 10, 20, DASH_C_ACCENT);
	dashText(x + 44, y + 8, msg, DASH_FS_BODY, DASH_C_TEXT);
	dashTextRight(x + w - 16, y + 11, "Press any button to stay", DASH_FS_SMALL, DASH_C_TEXT2);
}

//--------------------------------------------------------------
// Rendering
//--------------------------------------------------------------
/* A full-screen tool (the Text Editor) draws its own scene underneath the
 * dialogs it opens. Tools nest (the File Browser opens the Text Editor), so
 * leaving one must restore the scene of the tool that opened it rather than
 * the dashboard: otherwise the browser keeps reading input underneath a
 * dashboard scene and the console looks frozen. */
#define SCENE_OVERRIDE_DEPTH 8
static void (*scene_override)(void);
static void (*scene_override_stack[SCENE_OVERRIDE_DEPTH])(void);
static int scene_override_depth;

void dashPushSceneOverride(void (*fn)(void))
{
	if (scene_override_depth < SCENE_OVERRIDE_DEPTH)
		scene_override_stack[scene_override_depth] = scene_override;
	scene_override_depth++;
	scene_override = fn;
}

void dashPopSceneOverride(void)
{
	if (scene_override_depth <= 0) {
		scene_override = NULL;
		return;
	}
	scene_override_depth--;
	scene_override = (scene_override_depth < SCENE_OVERRIDE_DEPTH) ? scene_override_stack[scene_override_depth] : NULL;
}

void dashRenderScene(void)
{
	float e;

	if (scene_override != NULL) {
		scene_override();
		return;
	}
	dashArtBeginFrame();
	dashGfxBeginFrame();
	dashBgDraw();
	if (background_only)
		return; /* background scene only, no hidden interactive UI */

	dashDrawHeader(section, section_anim);

	e = dashEaseOutCubic(section_anim);
	dashGfxPushAlpha(e);
	dashGfxPushOffset(28.0f * (1.0f - e) * section_dir, 0);
	screens[section]->draw();
	dashGfxPopOffset();
	dashGfxPopAlpha();

	if (screens[section]->hints)
		screens[section]->hints();
	bootDraw();
	dashDrawToasts();
	dashDrawActivity();

	if (fade_in < 1.0f)
		dashRect(0, 0, DASH_VW, DASH_VH, DC(0, 0, 0, (int)(255 * (1.0f - fade_in))));
}

//--------------------------------------------------------------
// Entry
//--------------------------------------------------------------
static void firstInit(void)
{
	dashStoreInit();
	dashLibInit();
	dashArtInit();
	dashUiInit();
	dashBgInit();
	dashDevicesInit();
	dashGfxInit();
	dashFontInit();
	dashIconsInit();
	dashWorkerInit();
	if (boot_safe_mode)
		dashToast(DASH_TOAST_WARN,
		          "Safe mode: DASHBOARD.CNF skipped for this boot; saved settings were not changed");
	if (dashLibLoadCache() < 0 || (dashCfg.autoscan && dashAppCount >= 0)) {
		/* First run (no cache) or auto-scan enabled: look at locations whose
		 * drivers are already running, starting the auto-start devices first
		 * so they contribute applications too. Drivers load here, on the UI
		 * thread; the scan itself runs in the background. */
		dashDevicesAutoStart();
		dashRescanLibrary(0);
	} else
		dashDevicesAutoStart();
#ifdef DASH_AUTOTEST
	/* development-only sample library (see include/dash_autotest.h) */
	{
		static const char *const demo[] = {
		    "mass:/APPS/OPL/OPNPS2LD.ELF", "mc0:/APPS/SMS/SMS.ELF", "mass:/APPS/NHDDL/nhddl.elf", "mc0:/BOOT/ESR.ELF",
		    "mass:/APPS/APP_RetroArch/BOOT.ELF", "hdd0:/+OPL/APPS/GSM.ELF", "mc1:/APPS/WLE/BOOT.ELF", "mass:/APPS/PS2Ident/PS2IDENT.ELF",
		    "mass:/APPS/HDLGameInstaller/HDLGAMEINSTALLER.ELF", "mmce0:/APPS/Neutrino/NEUTRINO.ELF", "mass:/APPS/Doom/PS2DOOM.ELF", "mc0:/APPS/Cheat_Device/CHEATDEVICE.ELF"};
		unsigned int i;
		for (i = 0; i < sizeof(demo) / sizeof(demo[0]) && dashAppCount < DASH_MAX_APPS; i++) {
			dashLibMakeTransient(&dashApps[dashAppCount], demo[i]);
			dashApps[dashAppCount].flags = 0;
			dashApps[dashAppCount].art_state = (i == 0) ? DASH_ART_UNKNOWN : DASH_ART_NONE;
			if (i == 0)
				dashStrCopy(dashApps[dashAppCount].desc, "Game loader for USB, HDD, SMB and MX4SIO. Metadata sidecars make descriptions like this appear.", DASH_DESC_MAX);
			dashAppCount++;
		}
		dashStorePushRecent(demo[0]);
		dashStorePushRecent(demo[3]);
	}
#endif
	section = dashCfg.start_section;
	/* Apps now lives inside Home; Files and old Devices startup values
	 * both open the real File Browser tab instead of a modal. */
	if (section == DASH_SEC_APPS)
		section = DASH_SEC_HOME;
	else if (section == DASH_SEC_DEVICES)
		section = DASH_SEC_FILES;
	else if (section < 0 || section >= DASH_SEC_COUNT)
		section = DASH_SEC_HOME;
	dash_inited = 1;
}

void DashRun(const MainExecuteContext *ctx, int from_boot)
{
	DashInput in;
	u64 next_device_poll;
	u64 next_disc_poll;
	u64 next_remote_poll;

	if (dash_active || ctx == NULL)
		return;
	dash_active = 1;
	background_only = 0; /* never persist hidden UI across launches/restarts */
	exec_ctx = *ctx;

	dashGfxCheckScreenChange();
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 5: entering dashboard initialization");
#endif
	drawMsgOverride = dashMsgHook;
	dashClassicUiInstall(1);
	if (!dash_inited)
		firstInit();
	else {
		dashGfxInit();
		dashFontInit();
	}
	DashThemeSetLegacyEnabled(1);
	mainActionLaunchHook = dashLaunchHook;
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 6: dashboard initialized; starting audio");
#endif
	dashSoundInit();
	dashBgmStart();
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 7: audio startup returned");
#endif
	if (from_boot)
		dashSoundPlay(DASH_SND_BOOT);

	section_anim = 1.0f;
	fade_in = 0.0f;
	if (screens[section]->enter)
		screens[section]->enter();
	dashAppsRebuildView();
	dashInputFlush();
	last_frame_ms = Timer();
	next_device_poll = last_frame_ms;
	next_disc_poll = last_frame_ms + 750;
	next_remote_poll = last_frame_ms;
	if (from_boot)
		bootBegin();
	/* the dashboard is the only shell: it never returns */
	while (1) {
		float dt = dashVisualUpdate();
		u64 now = Timer();
		u64 idle_ms;

		if (dashGfxCheckScreenChange())
			dashArtInvalidateVram();

		/* USB fileXio completion can outlive its Files tab; serialize PAD
		 * input across all dashboard sections until the final USB read ends. */
		if (!dashBrowserPadInputReady())
			memset(&in, 0, sizeof(in));
		else
			dashInputPoll(&in);
		if (boot_phase) {
			bootUpdate(&in);
			if (boot_phase == 1)
				memset(&in, 0, sizeof(in));
		}
		/* R3 is unused by HOME navigation; SELECT+R3 remains a launch key
		 * because quick-launch intercepts it before dashboard dispatch. */
		if (!boot_phase && section == DASH_SEC_HOME && !ql_down &&
		    !(paddata & PAD_SELECT) && (in.pressed & DASH_IN_R3)) {
			background_only = !background_only;
			dashInputFlush();
			memset(&in, 0, sizeof(in));
		}
		if (background_only)
			memset(&in, 0, sizeof(in));
		else if (!boot_phase)
			quickLaunchInput(&in);
		/* Main dashboard screens do not run through the modal helpers, so give
		 * their ordinary navigation/activation inputs the same UI feedback. */
		if (in.pressed & (DASH_IN_UP | DASH_IN_DOWN | DASH_IN_LEFT | DASH_IN_RIGHT))
			dashSoundPlay(DASH_SND_CURSOR);
		if (in.pressed & DASH_IN_OK)
			dashSoundPlay(DASH_SND_CONFIRM);
		if (in.pressed & DASH_IN_L1)
			dashSwitchSection(dashAdjacentSection(section, -1));
		else if (in.pressed & DASH_IN_R1)
			dashSwitchSection(dashAdjacentSection(section, 1));
		else if (in.pressed & DASH_IN_START)
			globalMenu();
		else if (in.any && screens[section]->input)
			screens[section]->input(&in);
		if (screens[section]->update)
			screens[section]->update(dt);

		/* Results of background work that change what screens point at. */
		applyScanResult();
		dashFilesPoll();
		/*
		 * Remote-control and receiver probes touch the network filesystem.
		 * Polling them every rendered frame was needlessly hammering the IOP
		 * once FTP/network access was enabled and showed up as animation/input
		 * stutter. Half-second latency is still effectively immediate for a
		 * file-based command inbox.
		 */
		if (now >= next_remote_poll) {
			pollPcGameReceiver();
			pollRemotePcCommand();
			next_remote_poll = now + 500;
		}
		if (autoprobe_pending) {
			autoprobe_pending = 0;
			dashDevicesAutoStart();
		}

		/* Housekeeping is wall-clock throttled. The old per-frame device poll
		 * and frame-count CD probe caused regular IOP RPC traffic even when
		 * nothing changed. Keep hotplug responsive without spending render time
		 * polling hardware tens of times per second. */
		if (!dashWorkerIoBusy() && !dashIopParked()) {
			if (now >= next_disc_poll) {
				uLE_cdStop();
				next_disc_poll = now + 1000;
			}
			else if (now >= next_device_poll) {
				dashDevicesPoll();
				next_device_poll = now + 250;
			}
		}
		idle_ms = dashInputIdleMs();
		dashSoundMaintain();
		dashThemeSavePending((int)idle_ms);
		if (!boot_phase)
			dashArtStep((int)idle_ms);
		if (idle_ms > 6000 && dashArtTakeDirty())
			dashLibSaveCache();
		dashStoreFlush(0);

#ifdef DASH_THEME_TOUR
		/* development only: show every preset for five seconds */
		{
			static u64 tour_next;
			static int tour_idx = -1;
			u64 now = Timer();
			if (now >= tour_next) {
				tour_idx = (tour_idx + 1) % dashThemePresetCount();
				dashThemeSetPreset(tour_idx);
				dashToast(DASH_TOAST_INFO, dashThemePresetInfo(tour_idx)->name);
				tour_next = now + 5000;
			}
			dashCfg.intro_done = 1;
		}
#endif
		dashRenderScene();
		if (!background_only)
			quickLaunchDraw();
		dashGfxPresent();

		/* first run: short introduction once the dashboard has faded in */
		if (!dashCfg.intro_done && boot_phase != 1 && fade_in > 0.95f) {
			dashModalMessage(DI_APP, "Welcome to Launchstation",
			                 "HOME contains Continue, your full application library and Last folder. "
			                 "Use the confirm button to open, Triangle for options and Square for favorites. "
			                 "L1/R1 switch sections; FILES goes directly to File Browser and MISC commands are in TOOLS.",
			                 NULL);
			dashCfg.intro_done = 1;
			dashStoreSaveSettings();
			last_frame_ms = Timer();
		}
	}
}
//--------------------------------------------------------------
//End of file: dash_main.c
//--------------------------------------------------------------

