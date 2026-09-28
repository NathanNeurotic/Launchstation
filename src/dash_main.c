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
#include "init.h"

extern void (*mainActionLaunchHook)(const char *path);

static MainExecuteContext exec_ctx;
static int dash_active;
static int dash_inited;
static int section = DASH_SEC_HOME;
static int prev_section = DASH_SEC_HOME;
static float section_anim = 1.0f;
static int section_dir = 1;
static float anim_time;
static float fade_in;
static int autoprobe_pending;
static u64 last_frame_ms;
static int frame_counter;

/* boot-time launch key emulation */
static int boot_phase;         /* 0 none, 1 init delay, 2 auto-launch countdown */
static u64 boot_phase_end;
static char boot_auto_title[DASH_TITLE_MAX];

static const DashScreen *const screens[DASH_SEC_COUNT] = {
    &dashHomeScreen, &dashAppsScreen, &dashFilesScreen, &dashDevicesScreen, &dashToolsScreen, &dashSettingsScreen};

static const char *const section_names[DASH_SEC_COUNT] = {"HOME", "APPS", "FILES", "DEVICES", "TOOLS", "SETTINGS"};
static const int section_icons[DASH_SEC_COUNT] = {DI_HOME, DI_APPS, DI_FOLDER, DI_DEVICES, DI_TOOLS, DI_SETTINGS};

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

float dashTime(void)
{
	return anim_time;
}

float dashAnimSpeed(void)
{
	if (dashCfg.anim == DASH_ANIM_OFF)
		return 1000.0f;
	if (dashCfg.anim == DASH_ANIM_REDUCED)
		return 2.0f;
	return 1.0f;
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
	if (s < 0)
		s = DASH_SEC_COUNT - 1;
	if (s >= DASH_SEC_COUNT)
		s = 0;
	if (s == section)
		return;
	if (!dashConfirmLeave())
		return;
	section_dir = (s > section) ? 1 : -1;
	prev_section = section;
	section = s;
	section_anim = 0.0f;
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

/* After a command that may have rebooted the IOP (network, reboot IOP, a
 * launch that failed): sound and the auto-started devices come back. */
static void afterMiscCommand(void)
{
	drawMsgOverride = dashMsgHook;
	dashClassicUiInstall(1);
	dashInputFlush();
	last_frame_ms = Timer();
	dashSoundShutdown(); /* drop stale state after IOP resets */
	dashSoundInit();
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

	if (!stricmp(misc_path, setting->Misc_PS2Disc))
		title = "Starting the disc", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_OSDSYS))
		title = "Starting OSDSYS", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_PS2Browser))
		title = "Opening the PS2 browser", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_PS2PowerOff))
		title = "Powering off", leaves = 1;
	else if (!stricmp(misc_path, setting->Misc_PS2Net))
		title = "Starting the network";
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
	if (leaves)
		dashGfxRestoreLegacyState();
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
	dashFileBrowser(start_dir);
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
	dashGfxRestoreLegacyState();
	dashClassicUiInstall(0); /* launch-time prompts stay classic */
	ExecuteMainAction(path, &exec_ctx);
	/* Only reached when the launch failed. */
	mainActionLaunchHook = dashLaunchHook;
	LaunchArgsClear();
	dashGfxCheckScreenChange();
	drawMsgOverride = dashMsgHook;
	dashClassicUiInstall(1);
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

	/* everything except "Settings" and a refresh leaves the current section */
	if (r >= 0 && r != 1 && r != 3 && !dashConfirmLeave())
		return;
	switch (r) {
		case 0:
			dashSwitchSection(DASH_SEC_HOME);
			break;
		case 1:
			dashRescanLibrary(1);
			break;
		case 2:
			dashOpenBrowser("");
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

static int bootHeldLaunchKey(void)
{
	static const struct
	{
		u32 mask;
		int lk;
	} map[] = {
	    {PAD_CIRCLE, SETTING_LK_CIRCLE},
	    {PAD_CROSS, SETTING_LK_CROSS},
	    {PAD_SQUARE, SETTING_LK_SQUARE},
	    {PAD_TRIANGLE, SETTING_LK_TRIANGLE},
	    {PAD_L1, SETTING_LK_L1},
	    {PAD_R1, SETTING_LK_R1},
	    {PAD_L2, SETTING_LK_L2},
	    {PAD_R2, SETTING_LK_R2},
	    {PAD_L3, SETTING_LK_L3},
	    {PAD_R3, SETTING_LK_R3},
	    {PAD_START, SETTING_LK_START},
	    {PAD_SELECT, SETTING_LK_SELECT},
	    {PAD_LEFT, SETTING_LK_LEFT},
	    {PAD_RIGHT, SETTING_LK_RIGHT}};
	unsigned int i;

	waitAnyPadReady();
	readpad_no_KB();
	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		int lk = map[i].lk;
		if (!(paddata & map[i].mask))
			continue;
		if ((lk == SETTING_LK_LEFT || lk == SETTING_LK_RIGHT) && !setting->LK_Flag[lk])
			continue;
		if (setting->LK_Path[lk][0])
			return lk;
	}
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
 * dialogs it opens. */
static void (*scene_override)(void);

void dashSetSceneOverride(void (*fn)(void))
{
	scene_override = fn;
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
	dash_inited = 1;
}

void DashRun(const MainExecuteContext *ctx, int from_boot)
{
	DashInput in;

	if (dash_active || ctx == NULL)
		return;
	dash_active = 1;
	exec_ctx = *ctx;

	dashGfxCheckScreenChange();
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
	dashSoundInit();
	dashBgmStart();
	if (from_boot)
		dashSoundPlay(DASH_SND_BOOT);

	section_anim = 1.0f;
	fade_in = 0.0f;
	if (screens[section]->enter)
		screens[section]->enter();
	dashAppsRebuildView();
	dashInputFlush();
	last_frame_ms = Timer();
	if (from_boot)
		bootBegin();
	/* the dashboard is the only shell: it never returns */
	while (1) {
		u64 now = Timer();
		float dt = (now - last_frame_ms) / 1000.0f;
		last_frame_ms = now;
		if (dt > 0.1f)
			dt = 0.1f;
		if (dt < 0.0f)
			dt = 0.0f;
		anim_time += dt;
		frame_counter++;

		if (dashGfxCheckScreenChange())
			dashArtInvalidateVram();

		dashInputPoll(&in);
		if (boot_phase) {
			bootUpdate(&in);
			if (boot_phase == 1)
				memset(&in, 0, sizeof(in));
		}
		/* Main dashboard screens do not run through the modal helpers, so give
		 * their ordinary navigation/activation inputs the same UI feedback. */
		if (in.pressed & (DASH_IN_UP | DASH_IN_DOWN | DASH_IN_LEFT | DASH_IN_RIGHT))
			dashSoundPlay(DASH_SND_CURSOR);
		if (in.pressed & DASH_IN_OK)
			dashSoundPlay(DASH_SND_CONFIRM);
		if (in.pressed & DASH_IN_L1)
			dashSwitchSection(section - 1);
		else if (in.pressed & DASH_IN_R1)
			dashSwitchSection(section + 1);
		else if (in.pressed & DASH_IN_START)
			globalMenu();
		else if (in.any && screens[section]->input)
			screens[section]->input(&in);
		dashBgUpdate(dt);
		dashUiUpdate(dt);
		if (screens[section]->update)
			screens[section]->update(dt);
		section_anim = dashApproach(section_anim, 1.0f, 9.0f * dashAnimSpeed(), dt);
		fade_in = dashApproach(fade_in, 1.0f, 5.0f * dashAnimSpeed(), dt);

		/* results of background work that change what screens point at */
		applyScanResult();
		dashFilesPoll();
		if (autoprobe_pending) {
			autoprobe_pending = 0;
			dashDevicesAutoStart();
		}

		/* Housekeeping. The UI thread does no IO of its own while the
		 * background worker does (libmc and the IOP are not shared). */
		if (!dashWorkerIoBusy() && !dashIopParked()) {
			if ((frame_counter % 30) == 0)
				uLE_cdStop();
			dashDevicesPoll();
		}
		dashSoundMaintain();
		dashThemeSavePending((int)dashInputIdleMs());
		if (!boot_phase)
			dashArtStep((int)dashInputIdleMs());
		if (dashInputIdleMs() > 6000 && dashArtTakeDirty())
			dashLibSaveCache();
		dashStoreFlush(0);

#ifdef DASH_THEME_TOUR
		/* development only: show every preset for five seconds */
		{
			static u64 tour_next;
			static int tour_idx = -1;
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
		dashGfxPresent();

		/* first run: short introduction once the dashboard has faded in */
		if (!dashCfg.intro_done && boot_phase != 1 && fade_in > 0.95f) {
			dashModalMessage(DI_APP, "Welcome to wLaunchELF",
			                 "Your applications are on HOME and APPS. Use the D-pad to move, the confirm button to start, "
			                 "Triangle for options and Square to add favorites. L1/R1 switch sections; everything "
			                 "wLaunchELF can do is still under FILES and TOOLS.",
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

