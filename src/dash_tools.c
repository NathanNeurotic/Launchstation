//--------------------------------------------------------------
//File name:   dash_tools.c
//TOOLS: every wLaunchELF function with a friendly name and a short
//description. Each entry dispatches the MISC command path through
//dashRunMisc(), which opens the native dashboard version of each tool or
//runs the command (disc, OSDSYS, network...) behind the loading overlay.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "mmce_igr.h"
#include "cloud_google.h"
#include <stddef.h>

enum ToolAction {
	TA_HEADER = 0,
	TA_MISC,
	TA_BROWSER,
	TA_BROWSER_AT,
	TA_EDITOR,
	TA_IGR_SETUP,
	TA_CLOUD
};

typedef struct
{
	int action;
	int icon;
	const char *name;
	const char *desc;
	const char *note; /* safety note shown in the detail panel */
	int confirm;      /* 1: ask before running, 2: dangerous */
	size_t misc_offset;
	const char *path;
} Tool;

#define MISC(field) offsetof(SETTING, field)

static const Tool tools[] = {
    {TA_HEADER, -1, "ESSENTIALS", NULL, NULL, 0, 0, NULL},
    {TA_BROWSER, DI_FOLDER, "File Browser", "Browse every device, copy, move, rename, delete, create folders, build and extract PSU saves, mount VMC images and launch anything.", "Press Triangle inside the browser for file operations; Square selects.", 0, 0, NULL},
    {TA_EDITOR, DI_TEXT, "Text Editor", "Edit CNF, CFG, TXT, INI and argument files. Also used to launch an ELF with custom arguments.", NULL, 0, 0, NULL},
    {TA_MISC, DI_DISC, "Start disc", "Boot the disc in the drive: PS2 and PS1 games, DVD Video (with the console's player) and ESR discs.", NULL, 0, MISC(Misc_PS2Disc), NULL},
    {TA_BROWSER_AT, DI_DISC, "PS1 VCDs (POPStarter)", "Open the File Browser; select a .VCD image to start it through POPStarter.", "POPStarter and Ember coexist; the file format chooses the PS1 core automatically.", 0, 0, ""},
    {TA_BROWSER_AT, DI_DISC, "PS1 images (Ember)", "Open the File Browser; select a .CUE, .BIN or PS1 .EXE inside <Ember directory>/games/<Game>/ to start Ember.", "The umbrella can be named anything. Its 512 KB bios.bin stays local; if ember.elf is missing, Settings can supply it from another Ember directory.", 0, 0, ""},
    {TA_BROWSER_AT, DI_DISC, "PS2 ISOs (Neutrino)", "Open the File Browser; select a .ISO image to start it through Neutrino.", "Set Neutrino under Settings > Launching > Disc & image loaders. The game device is also searched automatically.", 0, 0, ""},
    {TA_HEADER, -1, "STORAGE", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_HDD, "HDD Manager", "Create, remove, rename and expand APA partitions, view HDL game information and format the internal HDD.", "Partition changes and formatting are permanent.", 0, MISC(Misc_HddManager), NULL},
    {TA_MISC, DI_MC, "Memory card exploit installer", "Install a memory card system-update exploit for this console, another model or region, or PSX/DESR.", "Writes system folders to the memory card. Follow the on-screen checks carefully.", 1, MISC(Misc_Exploit_Installer), NULL},
    {TA_IGR_SETUP, DI_MC, "OPL IGR automatic MMCE backup", "Set up verified automatic backup of every MMCE card image when OPL exits a game through IGR.", "Installs or updates WLE-IGR.ELF on the required boot medium and tells you the exact OPL Exit-to path.", 0, 0, NULL},
    {TA_HEADER, -1, "SYSTEM", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_APP, "PS2 system browser", "Leave wLaunchELF and open the console's own Browser.", NULL, 1, MISC(Misc_PS2Browser), NULL},
    {TA_MISC, DI_APP, "OSDSYS", "Start the console's OSD (or the OSDSYS path configured in wLaunchELF).", NULL, 1, MISC(Misc_OSDSYS), NULL},
#ifdef ETH
    {TA_MISC, DI_NETWORK, "FTP / Remote access", "Start FTP and PS2Link on demand, accept remote commands, and receive staged/resumable ISO transfers from MrRexD-compatible PC tools.", "Network and HDD drivers stay unloaded until you start this tool.", 0, MISC(Misc_PS2Net), NULL},
#ifdef WLE_NATIVE_CLOUD
    {TA_CLOUD, DI_NETWORK, "Google Drive backups", "Link Google Drive and upload missing WLE-MMCE-Backups versions directly from the PS2 over HTTPS.", "Cloud sync is copy-only. It never deletes local or Drive backup history.", 0, 0, NULL},
#endif
#endif
    {TA_MISC, DI_INFO, "System information", "Console model, ROM version, boot path, loaded configuration and debug details.", NULL, 0, MISC(Misc_Debug_Info), NULL},
    {TA_MISC, DI_CHIP, "Build information", "Features and drivers included in this build of wLaunchELF.", NULL, 0, MISC(Misc_Show_Build_Info), NULL},
    {TA_MISC, DI_TEXT, "Font viewer", "Show every glyph of the active font.", NULL, 0, MISC(Misc_ShowFont), NULL},
    {TA_MISC, DI_INFO, "About wLaunchELF", "Credits and version.", NULL, 0, MISC(Misc_About_uLE), NULL},
    {TA_HEADER, -1, "POWER", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_REFRESH, "Reboot IOP", "Restart the I/O processor and all device drivers. Devices are detected again when used.", NULL, 1, MISC(Misc_Reboot_IOP), NULL},
    {TA_MISC, DI_POWER, "Power off", "Turn the console off.", NULL, 2, MISC(Misc_PS2PowerOff), NULL},
};

#define NTOOLS ((int)(sizeof(tools) / sizeof(tools[0])))

static int tsel = 1, ttop;
static float tsel_anim = 1.0f;

static void tMove(int d)
{
	int s = tsel;

	do {
		s += d;
		if (s < 0 || s >= NTOOLS)
			return;
	} while (tools[s].action == TA_HEADER);
	tsel = s;
}

static void toolsEnter(void)
{
	if (tools[tsel].action == TA_HEADER)
		tMove(1);
}

static void setupIgrBackup(void)
{
	static const char *const actions[] = {"Install / update", "Remove helper"};
	WleIgrInstallStats stats;
	char folder[MAX_PATH], result[384];
	const char *start = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
	int action, r;

	action = dashModalChoice("OPL IGR automatic MMCE backup", actions, 2, 0);
	if (action < 0)
		return;

	if (action == 1) {
		if (!dashModalConfirm("Remove OPL IGR backup helper?",
		                      "Removes WLE-IGR.ELF from the locations managed by this wLaunchELF installation. Existing card backups are not deleted.",
		                      "Remove", 1))
			return;
		dashLoadingBegin("Removing OPL IGR helper");
		r = wleIgrRemove(&stats, result, sizeof(result));
		dashLoadingEnd();
		dashModalMessage(r == 0 ? DI_CHECK : DI_WARNING,
		                 r == 0 ? "OPL IGR helper removed" : "OPL IGR removal incomplete",
		                 result[0] ? result : "Removal failed.",
		                 "Existing WLE-MMCE-Backups history is left untouched.");
		return;
	}

	snprintf(folder, sizeof(folder), "%s", start != NULL ? start : "");
	if (!dashPickPath("Automatic MMCE backup destination", folder,
	                  DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;

	if (!dashModalConfirm("Install OPL IGR backup helper?",
	                      "wLaunchELF will install or update a verified WLE-IGR.ELF where OPL can launch it. MMCE-hosted setups update every discovered BootCard image.",
	                      "Install", 0))
		return;

	dashLoadingBegin("Configuring OPL IGR automatic backup");
	r = wleIgrConfigure(folder, &stats, result, sizeof(result));
	dashLoadingEnd();

	if (r == 0) {
		/* The helper already embeds this same destination. Persist it too so
		 * manual/native cloud sync never has to scan every mounted device. */
		dashStrCopy(dashCfg.igr_backup_path, folder, sizeof(dashCfg.igr_backup_path));
		dashStoreSaveSettings();
		dashStoreFlush(1);
		dashModalMessage(DI_CHECK, "OPL IGR automatic backup ready", result,
		                 "After setting OPL Exit-to to the shown helper, IGR will back up changed MMCE card images and then open wLaunchELF.");
	} else
		dashModalMessage(DI_WARNING, "OPL IGR setup incomplete", result[0] ? result : "Setup failed.", NULL);
}


#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
static void cloudProgress(const char *status, int done, int total)
{
	dashLoadingStatus(status != NULL ? status : "Google Drive", done, total);
}

static void cloudLink(void)
{
	CloudGoogleDeviceCode code;
	char error[256], body[448], detail[256];
	int r;

	error[0] = '\0';
	dashLoadingBegin("Starting Google Drive authorization");
	r = cloudGoogleBeginLink(&code, error, sizeof(error));
	dashLoadingEnd();
	if (r < 0) {
		dashModalMessage(DI_WARNING, "Google Drive link unavailable",
		                 error[0] ? error : "Could not start Google authorization.",
		                 "WLE-CLOUD.CNF and WLE-CLOUD-CA.PEM must be available in the dashboard settings folder.");
		return;
	}

	snprintf(body, sizeof(body),
	         "On a phone or computer, open:\n%s\n\nEnter this code: %s",
	         code.verification_url, code.user_code);
	snprintf(detail, sizeof(detail),
	         "The token is stored only on this PS2. Scope: files created by wLaunchELF.");
	dashModalMessage(DI_INFO, "Link Google Drive", body, detail);

	for (;;) {
		if (!dashModalConfirm("Check Google Drive authorization?",
		                      "Finish the browser step first, then choose Check. Back keeps the account unlinked.",
		                      "Check", 0))
			return;
		error[0] = '\0';
		dashLoadingBegin("Checking Google authorization");
		r = cloudGoogleFinishLink(&code, error, sizeof(error));
		dashLoadingEnd();
		if (r == 0) {
			dashModalMessage(DI_CHECK, "Google Drive linked",
			                 "Native PS2 cloud backup is ready.",
			                 "Use this tool to sync now, or enable Google Drive after IGR under Settings > Storage & devices > MMCE backups.");
			return;
		}
		if (r < 0) {
			dashModalMessage(DI_WARNING, "Google Drive link failed",
			                 error[0] ? error : "Google authorization failed.",
			                 "Start Link Google Drive again if the device code expired.");
			return;
		}
		dashModalMessage(DI_INFO, "Still waiting for Google",
		                 error[0] ? error : "Authorization is not complete yet.",
		                 "Complete the code entry in your browser, then check again.");
	}
}

static void cloudSyncNow(void)
{
	CloudGoogleSyncStats stats;
	char error[256], result[256];
	int r;

	if (dashCfg.igr_backup_path[0] == '\0') {
		dashModalMessage(DI_WARNING, "No MMCE backup destination saved",
		                 "Run Tools > OPL IGR automatic MMCE backup > Install / update once so wLaunchELF knows which local history to upload.",
		                 "Existing backups are not moved or deleted.");
		return;
	}
	if (!cloudGoogleIsLinked()) {
		dashModalMessage(DI_INFO, "Google Drive is not linked",
		                 "Use Google Drive backups > Link Google Drive first.", NULL);
		return;
	}
	if (!dashStoreFlushWait("Saving settings before cloud sync"))
		return;

	error[0] = '\0';
	memset(&stats, 0, sizeof(stats));
	dashLoadingBegin("Syncing MMCE backups to Google Drive");
	r = cloudGoogleSyncBackupRoot(dashCfg.igr_backup_path, &stats,
	                              cloudProgress, error, sizeof(error));
	dashLoadingEnd();

	if (r == 0) {
		snprintf(result, sizeof(result),
		         "%d uploaded, %d already present, %d failed. %d new Drive folders.",
		         stats.uploaded, stats.skipped, stats.failed, stats.folders_created);
		dashModalMessage(stats.failed ? DI_WARNING : DI_CHECK,
		                 stats.failed ? "Google Drive sync completed with errors" : "Google Drive sync complete",
		                 result,
		                 "Copy-only sync: existing Drive files and local history are never deleted.");
	} else {
		dashModalMessage(DI_WARNING, "Google Drive sync failed",
		                 error[0] ? error : "The cloud upload did not complete.",
		                 "Local WLE-MMCE-Backups history is unchanged.");
	}
}

static void cloudBackupTool(void)
{
	static const char *const linked_actions[] = {"Sync now", "Relink Google Drive", "Disconnect Google Drive"};
	static const char *const unlinked_actions[] = {"Link Google Drive"};
	char error[192];
	int linked, action;

	error[0] = '\0';
	if (!cloudGoogleConfigured(error, sizeof(error))) {
		/* Configuration can be removed independently of the saved refresh token.
		 * Never strand credentials just because the client ID or CA file is gone. */
		if (cloudGoogleHasLocalToken()) {
			static const char *const cleanup_actions[] = {"Clear saved Google token"};
			action = dashModalChoice("Google Drive setup incomplete",
			                         cleanup_actions, 1, 0);
			if (action == 0 &&
			    dashModalConfirm("Clear saved Google token?",
			                     "Removes WLE-CLOUD.TOK from this PS2. Drive files are untouched.",
			                     "Clear token", 1)) {
				error[0] = '\0';
				if (cloudGoogleDisconnect(error, sizeof(error)) == 0) {
					dashCfg.igr_cloud_sync = 0;
					dashStoreSaveSettings();
					dashStoreFlush(1);
					dashModalMessage(DI_CHECK, "Saved Google token removed",
					                 "The local OAuth token was removed and automatic IGR cloud sync was turned off.",
					                 "Restore WLE-CLOUD.CNF and WLE-CLOUD-CA.PEM before linking again.");
				} else
					dashModalMessage(DI_WARNING, "Could not remove saved Google token",
					                 error[0] ? error : "Token removal failed.", NULL);
			}
			return;
		}
		dashModalMessage(DI_WARNING, "Native cloud setup is incomplete",
		                 error[0] ? error : "Google Drive is not configured.",
		                 "See docs/NATIVE_CLOUD.md. No Google credentials are built into the repository.");
		return;
	}

	linked = cloudGoogleIsLinked();
	action = dashModalChoice("Google Drive backups",
	                         linked ? linked_actions : unlinked_actions,
	                         linked ? 3 : 1, 0);
	if (action < 0)
		return;
	if (!linked || action == 1) {
		cloudLink();
		return;
	}
	if (action == 0) {
		cloudSyncNow();
		return;
	}
	if (action == 2 &&
	    dashModalConfirm("Disconnect Google Drive?",
	                     "Deletes the saved refresh token from this PS2. Files already uploaded to Drive are untouched.",
	                     "Disconnect", 1)) {
		error[0] = '\0';
		if (cloudGoogleDisconnect(error, sizeof(error)) == 0) {
			/* A disconnected account cannot satisfy automatic IGR sync. Keep the
			 * persisted setting consistent so later IGR returns do not repeatedly
			 * attempt a guaranteed-to-fail cloud operation. */
			dashCfg.igr_cloud_sync = 0;
			dashStoreSaveSettings();
			dashStoreFlush(1);
			dashModalMessage(DI_CHECK, "Google Drive disconnected",
			                 "The local OAuth token was removed and automatic IGR cloud sync was turned off.", NULL);
		} else
			dashModalMessage(DI_WARNING, "Could not disconnect Google Drive",
			                 error[0] ? error : "Token removal failed.", NULL);
	}
}
#endif

static void runTool(const Tool *t)
{
	if (t->confirm) {
		char body[200];
		snprintf(body, sizeof(body), "%s", t->desc);
		if (!dashModalConfirm(t->name, body, t->confirm == 2 ? t->name : "Continue", t->confirm == 2))
			return;
	}
	switch (t->action) {
		case TA_MISC:
			dashRunMisc((const char *)setting + t->misc_offset);
			break;
		case TA_BROWSER:
			dashOpenBrowser("");
			break;
		case TA_BROWSER_AT:
			dashOpenBrowser(t->path ? t->path : "");
			break;
		case TA_EDITOR:
			dashOpenTextEditor(NULL);
			break;
		case TA_IGR_SETUP:
			setupIgrBackup();
			break;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		case TA_CLOUD:
			cloudBackupTool();
			break;
#endif
	}
}

static void toolsInput(const DashInput *in)
{
	int i;

	if (in->pressed & DASH_IN_UP)
		tMove(-1);
	if (in->pressed & DASH_IN_DOWN)
		tMove(1);
	if (in->pressed & DASH_IN_L2)
		for (i = 0; i < 6; i++)
			tMove(-1);
	if (in->pressed & DASH_IN_R2)
		for (i = 0; i < 6; i++)
			tMove(1);
	if (in->pressed & DASH_IN_OK)
		runTool(&tools[tsel]);
}

static void toolsUpdate(float dt)
{
	tsel_anim = dashApproach(tsel_anim, (float)tsel, 16.0f * dashAnimSpeed(), dt);
}

static void toolsDraw(void)
{
	const float lx = 48, ly = 102, row_h = 29, lw = 300;
	const int rows = 10;
	const Tool *t = &tools[tsel];
	int i;

	dashDrawSectionTitle(48, 68, "Tools", "everything wLaunchELF can do");
	if (tsel < ttop + 1)
		ttop = tsel - 1;
	if (tsel >= ttop + rows)
		ttop = tsel - rows + 1;
	if (ttop < 0)
		ttop = 0;

	dashDrawPanelBox(lx - 6, ly - 6, lw + 12, rows * row_h + 12, 14);
	dashGfxScissor(lx - 6, ly - 4, lw + 12, rows * row_h + 8);
	{
		float fy = ly + (tsel_anim - ttop) * row_h;
		dashDrawSelection(lx, fy, lw, row_h - 3, 9, 1.0f);
	}
	for (i = ttop; i < NTOOLS && i < ttop + rows; i++) {
		float y = ly + (i - ttop) * row_h;

		if (tools[i].action == TA_HEADER) {
			dashText(lx + 8, y + 7, tools[i].name, DASH_FS_CAPTION, DASH_C_ACCENT);
			continue;
		}
		dashIcon(tools[i].icon, lx + 10, y + 5, 16, dashItemColor(i == tsel));
		dashTextFit(lx + 34, y + 6.5f, lw - 44, tools[i].name, DASH_FS_SMALL, dashItemColor(i == tsel));
		if (tools[i].confirm == 2 || tools[i].note)
			dashGlow(lx + lw - 14, y + 13, 4, 4, tools[i].confirm == 2 ? DASH_C_DANGER : DASH_C_WARM);
	}
	dashGfxScissorReset();
	dashDrawScrollbar(lx + lw + 10, ly, rows * row_h, ttop, rows, NTOOLS);

	{
		float dx = 372, dy = 98, dw = 228, dh = 292;

		dashDrawCard(dx, dy, dw, dh, 16);
		dashAccentGlow(dx + dw * 0.5f, dy + 66, 80, 60, 1.0f);
		dashIcon(t->icon, dx + dw * 0.5f - 30, dy + 34, 60, DASH_C_TEXT);
		dashTextCenter(dx + dw * 0.5f, dy + 108, t->name, DASH_FS_BODY, DASH_C_TEXT);
		dashTextWrap(dx + 16, dy + 142, dw - 32, 4, t->desc, DASH_FS_CAPTION, DASH_C_TEXT2, 19);
		if (t->note) {
			DashColor wb = dashColorScale(DASH_C_WARM, 0.24f);
			wb.a = 130;
			dashPanel(dx + 12, dy + dh - 64, dw - 24, 52, 10, wb);
			dashRect(dx + 12, dy + dh - 64, 3, 52, dashColorA(DASH_C_WARM, 0.8f));
			dashIcon(DI_WARNING, dx + 20, dy + dh - 54, 16, DASH_C_WARM);
			dashTextWrap(dx + 42, dy + dh - 58, dw - 62, 2, t->note, DASH_FS_CAPTION * 0.95f, DASH_C_WARM, 18);
		}
	}
}

static void toolsHints(void)
{
	DashHint h[2];

	h[0].button = dashOkIcon(), h[0].key = NULL, h[0].label = "Open";
	h[1].button = -1, h[1].key = "START", h[1].label = "Menu";
	dashDrawHints(h, 2);
}

const DashScreen dashToolsScreen = {toolsEnter, toolsInput, toolsUpdate, toolsDraw, toolsHints};
//--------------------------------------------------------------
//End of file: dash_tools.c
//--------------------------------------------------------------
