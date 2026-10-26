//--------------------------------------------------------------
//File name:   dash_tools.c
//TOOLS: every wLaunchELF function with a friendly name and a short
//description. Each entry dispatches the MISC command path through
//dashRunMisc(), which opens the native dashboard version of each tool or
//runs the command (disc, OSDSYS, network...) behind the loading overlay.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "mmce_igr.h"
#include "mmce_backup.h"
#include "mmce_igr.h"
#include "cloud_google.h"
#include "self_update.h"
#include "init.h"
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
#include "cloud_http.h"
#include "qrcodegen.h"
#endif
#include <stddef.h>
#include <stdlib.h>

enum ToolAction {
	TA_HEADER = 0,
	TA_MISC,
	TA_BROWSER,
	TA_BROWSER_AT,
	TA_MISC_MENU,
	TA_EDITOR,
	TA_USB_DIAG,
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
	int confirm;      /* 1: ask before running, 2: dangerous or leaves Launchstation (red confirm) */
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
    {TA_MISC_MENU, DI_TOOLS, "MISC commands", "Choose a classic wLaunchELF built-in command from a popup menu.", NULL, 0, 0, NULL},
    {TA_USB_DIAG, DI_USB, "USB unit diagnostics", "Inspect the cached USB drive mask without IO, or manually probe logical USB unit 0 or 1 one at a time.", "Each manual probe is a synchronous IOP request and could freeze a failing console. Logical units may not match physical USB ports.", 0, 0, NULL},
    {TA_MISC, DI_HDD, "HDD Manager", "Create, remove, rename and expand APA partitions, view HDL game information and format the internal HDD.", "Partition changes and formatting are permanent.", 0, MISC(Misc_HddManager), NULL},
    {TA_MISC, DI_MC, "Memory card exploit installer", "Install a memory card system-update exploit for this console, another model or region, or PSX/DESR.", "Writes system folders to the memory card. Follow the on-screen checks carefully.", 1, MISC(Misc_Exploit_Installer), NULL},
    {TA_IGR_SETUP, DI_MC, "OPL IGR automatic MMCE backup", "Set up verified automatic backup of every MMCE card image when OPL exits a game through IGR.", "Installs or updates WLE-IGR.ELF on the required boot medium and tells you the exact OPL Exit-to path.", 0, 0, NULL},
    {TA_HEADER, -1, "SYSTEM", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_APP, "PS2 system browser", "Leave Launchstation and open the console's own Browser.", NULL, 2, MISC(Misc_PS2Browser), NULL},
    {TA_MISC, DI_APP, "OSDSYS", "Start the console's OSD (or the OSDSYS path configured in Launchstation).", NULL, 2, MISC(Misc_OSDSYS), NULL},
#ifdef ETH
    {TA_MISC, DI_NETWORK, "FTP / Remote access", "Start FTP and PS2Link on demand, accept remote commands, and receive staged/resumable ISO transfers from MrRexD-compatible PC tools.", "Network and HDD drivers stay unloaded until you start this tool.", 0, MISC(Misc_PS2Net), NULL},
#ifdef WLE_NATIVE_CLOUD
    {TA_CLOUD, DI_NETWORK, "Google Drive backups", "Link Google Drive and upload missing WLE-MMCE-Backups versions directly from the PS2 over HTTPS.", "Cloud sync is copy-only. It never deletes local or Drive backup history.", 0, 0, NULL},
#endif
#endif
    {TA_MISC, DI_INFO, "System information", "Console model, ROM version, boot path, loaded configuration and debug details.", NULL, 0, MISC(Misc_Debug_Info), NULL},
    {TA_MISC, DI_CHIP, "Build information", "Features and drivers included in this build of Launchstation.", NULL, 0, MISC(Misc_Show_Build_Info), NULL},
    {TA_MISC, DI_TEXT, "Font viewer", "Show every glyph of the active font.", NULL, 0, MISC(Misc_ShowFont), NULL},
    {TA_MISC, DI_INFO, "About Launchstation", "Credits and version.", NULL, 0, MISC(Misc_About_uLE), NULL},
    {TA_HEADER, -1, "POWER", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_REFRESH, "Reboot IOP", "Restart the I/O processor and all device drivers. Devices are detected again when used.", NULL, 2, MISC(Misc_Reboot_IOP), NULL},
    {TA_MISC, DI_POWER, "Power off", "Turn the console off.", NULL, 2, MISC(Misc_PS2PowerOff), NULL},
};

#define NTOOLS ((int)(sizeof(tools) / sizeof(tools[0])))

static int tsel = 1, ttop;
static float tsel_anim = 1.0f;

static void tMove(int d)
{
	int start, s;

	if (NTOOLS <= 0 || d == 0)
		return;
	start = s = tsel;
	do {
		s += d;
		if (s < 0)
			s = NTOOLS - 1;
		else if (s >= NTOOLS)
			s = 0;
		if (tools[s].action != TA_HEADER) {
			if (s != tsel)
				dashUiFocusPoke();
			tsel = s;
			return;
		}
	} while (s != start);
}

static void toolsEnter(void)
{
	if (tools[tsel].action == TA_HEADER)
		tMove(1);
}

void dashToolsIgrBackupSetup(void)
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
		                      "Removes WLE-IGR.ELF from the locations managed by this Launchstation installation. Existing card backups are not deleted.",
		                      localizeDashboardText("Remove"), 1))
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
	                      "Launchstation will install or update a verified WLE-IGR.ELF where OPL can launch it. MMCE-hosted setups update every discovered BootCard image.",
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
		                 "After setting OPL Exit-to to the shown helper, IGR will back up changed MMCE card images and then open Launchstation.");
	} else
		dashModalMessage(DI_WARNING, "OPL IGR setup incomplete", result[0] ? result : "Setup failed.", NULL);
}


#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
static void cloudProgress(const char *status, int done, int total)
{
	dashLoadingStatus(status != NULL ? status : "Google Drive", done, total);
}

#define CLOUD_QR_MAX_VERSION 6
#define CLOUD_LINK_NET_RETRIES 3

enum { CLOUD_LINK_DONE = 0, CLOUD_LINK_FAILED = -1, CLOUD_LINK_CANCELLED = -2, CLOUD_LINK_EXPIRED = -3 };

/* A QR symbol as dark row runs on a white square with a 4-module quiet zone,
 * so a phone can scan it straight off the TV. */
static void cloudDrawQr(const uint8_t *qr, float x, float y, float side)
{
	int size = qrcodegen_getSize(qr), mx, my;
	float m = (float)(int)(side / (size + 8));
	float ox, oy;

	if (m < 1.0f)
		m = 1.0f;
	ox = x + (side - m * size) * 0.5f;
	oy = y + (side - m * size) * 0.5f;
	dashRect(x, y, side, side, DC(255, 255, 255, 255));
	for (my = 0; my < size; my++) {
		for (mx = 0; mx < size;) {
			int run = 0;
			while (mx + run < size && qrcodegen_getModule(qr, mx + run, my))
				run++;
			if (run) {
				dashRect(ox + mx * m, oy + my * m, run * m, m, DC(0, 0, 0, 255));
				mx += run;
			} else
				mx++;
		}
	}
}

/* One screen for the whole device flow: QR + address + code, then poll
 * Google at its requested interval until approved, expired or cancelled. */
static void cloudLink(void)
{
	CloudGoogleDeviceCode code;
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(CLOUD_QR_MAX_VERSION)];
	uint8_t qr_tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(CLOUD_QR_MAX_VERSION)];
	char error[256], status[160];
	const char *url;
	const float w = 540.0f, h = 300.0f, x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	const float qside = 150.0f, rx = x + 24 + qside + 24, rw = w - (rx - x) - 24;
	u64 start, deadline, next_poll;
	int r, have_qr, interval, failures = 0, result;
	float open = 0.0f;
	DashInput in;

	error[0] = '\0';
	dashLoadingBegin("Starting Google Drive authorization");
	r = cloudGoogleBeginLink(&code, error, sizeof(error));
	dashLoadingEnd();
	if (r < 0) {
		dashModalMessage(DI_WARNING, "Google Drive link unavailable",
		                 error[0] ? error : "Could not start Google authorization.",
		                 "Check the Ethernet connection and that the PS2 date and time are correct.");
		return;
	}

	have_qr = qrcodegen_encodeText(code.verification_url, qr_tmp, qr, qrcodegen_Ecc_MEDIUM,
	                               qrcodegen_VERSION_MIN, CLOUD_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
	url = strncmp(code.verification_url, "https://", 8) ? code.verification_url : code.verification_url + 8;
	interval = code.interval > 0 ? code.interval : 5;
	start = Timer();
	deadline = start + (u64)(code.expires_in > 0 ? code.expires_in : 1800) * 1000;
	next_poll = start + (u64)interval * 1000;

	dashInputFlush();
	for (;;) {
		u64 now = Timer();
		int checking = now >= next_poll;
		float oe;

		dashInputPoll(&in);
		if (dashDashboardPending()) {
			result = CLOUD_LINK_CANCELLED;
			break;
		}
		if (in.pressed & DASH_IN_BACK) {
			dashSoundPlay(DASH_SND_CANCEL);
			result = CLOUD_LINK_CANCELLED;
			break;
		}
		if (now >= deadline) {
			result = CLOUD_LINK_EXPIRED;
			break;
		}
		if (checking)
			snprintf(status, sizeof(status), "Checking with Google...");
		else if (failures)
			snprintf(status, sizeof(status), "Network problem - retrying in %d s", (int)((next_poll - now + 999) / 1000));
		else
			snprintf(status, sizeof(status), "Waiting for approval - code expires in %d min",
			         (int)((deadline - now + 59999) / 60000));

		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		oe = dashEaseOutCubic(open);
		dashRenderScene();
		dashModalBackdrop(oe);
		dashGfxPushAlpha(oe);
		dashGfxPushOffset(0, 18.0f * (1.0f - oe));
		dashModalPanel(x, y, w, h, oe);
		dashIcon(DI_NETWORK, x + 24, y + 18, 24, DASH_C_ACCENT);
		dashTextS(x + 58, y + 20, "Link Google Drive", DASH_FS_LABEL, DASH_C_TEXT);
		if (have_qr)
			cloudDrawQr(qr, x + 24, y + 58, qside);
		dashTextFit(rx, y + 58, rw, have_qr ? "1. Scan the code, or open:" : "1. On a phone or computer, open:",
		            DASH_FS_CAPTION, DASH_C_TEXT2);
		dashTextFit(rx, y + 76, rw, url, DASH_FS_SMALL, DASH_C_ACCENT);
		dashTextFit(rx, y + 106, rw, "2. Enter this code:", DASH_FS_CAPTION, DASH_C_TEXT2);
		dashTextFit(rx, y + 122, rw, code.user_code, DASH_FS_TITLE, DASH_C_TEXT);
		dashTextFit(rx, y + 158, rw, "3. Sign in and choose Allow.", DASH_FS_CAPTION, DASH_C_TEXT2);
		dashTextFit(rx, y + 186, rw, status, DASH_FS_CAPTION, failures ? DASH_C_WARM : DASH_C_ACCENT);
		dashTextWrap(x + 24, y + 222, w - 48, 2,
		             "No Google Cloud setup or payment details are needed. Launchstation can only see files it creates.",
		             DASH_FS_CAPTION, DASH_C_TEXT3, 15);
		dashModalHintRow(x + 24, y + h - 36, -1, NULL, dashBackIcon(), localizeDashboardText("Cancel"));
		dashGfxPopOffset();
		dashGfxPopAlpha();
		dashGfxPresent();

		if (!checking)
			continue;
		/* Blocking HTTPS round trip; the frame above already says so. */
		error[0] = '\0';
		r = cloudGoogleFinishLink(&code, error, sizeof(error));
		if (r == 0) {
			result = CLOUD_LINK_DONE;
			break;
		}
		if (r == 2)
			interval += 5; /* Google's slow_down: back off as RFC 8628 requires */
		if (r < 0) {
			/* An OAuth answer (denied, expired...) is final; a network hiccup
			 * is retried a few times before giving up. */
			if (!strncmp(error, "Google OAuth:", 13) || ++failures >= CLOUD_LINK_NET_RETRIES) {
				result = CLOUD_LINK_FAILED;
				break;
			}
		} else
			failures = 0;
		next_poll = Timer() + (u64)interval * 1000;
	}
	dashInputFlush();

	if (result == CLOUD_LINK_DONE)
		dashModalMessage(DI_CHECK, "Google Drive linked",
		                 "Native PS2 cloud backup is ready.",
		                 "Use this tool to sync now, or enable Google Drive after IGR under Settings > Storage & devices > MMCE backups.");
	else if (result == CLOUD_LINK_EXPIRED)
		dashModalMessage(DI_WARNING, "Google code expired",
		                 "The sign-in code was not approved in time.",
		                 "Choose Link Google Drive again to get a new code.");
	else if (result == CLOUD_LINK_FAILED)
		dashModalMessage(DI_WARNING, "Google Drive link failed",
		                 error[0] ? error : "Google authorization failed.",
		                 "Choose Link Google Drive again to retry.");
}

static void cloudSyncNow(void)
{
	CloudGoogleSyncStats stats;
	char error[256], result[256];
	int r;

	if (dashCfg.igr_backup_path[0] == '\0') {
		dashModalMessage(DI_WARNING, "No MMCE backup destination saved",
		                 "Choose Backup folder under Settings > Storage & devices > MMCE backups. Select the parent of WLE-MMCE-Backups, then upload again.",
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

#endif

typedef struct
{
	char root[MAX_PATH];
	MmceBackupOptions options;
	MmceBackupScanStats total;
	WleIgrStatusStats igr;
	int igr_status;
	int scan_mmce;
	int any_unit;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
	int google_linked;
#endif
} MmceStatusJob;

static int mmce_status_job;
static char mmce_status_text[48] = "Not checked";
static char igr_status_text[32] = "Not checked";
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
static char google_status_text[24] = "Not checked";
#endif

static void mmceStatusRun(void *arg)
{
	MmceStatusJob *j = (MmceStatusJob *)arg;
	int unit;

	memset(&j->total, 0, sizeof(j->total));
	if (j->scan_mmce) {
		for (unit = 0; unit < 2; unit++) {
			MmceBackupScanStats one;
			memset(&one, 0, sizeof(one));
			if (mmceBackupScanCards(unit, j->root, &j->options, &one, NULL) < 0)
				continue;
			j->any_unit = 1;
			j->total.candidates += one.candidates;
			j->total.pending += one.pending;
			j->total.unchanged += one.unchanged;
			j->total.failed += one.failed;
		}
	}
	j->igr_status = wleIgrStatus(j->root, &j->igr);
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
	j->google_linked = cloudGoogleIsLinked();
#endif
}

void dashToolsRefreshMmceStatus(void);

static void mmceStatusDone(void *arg)
{
	MmceStatusJob *j = (MmceStatusJob *)arg;
	int stale = strcmp(j->root, dashCfg.igr_backup_path) != 0 ||
	            j->options.format != dashCfg.mmce_backup_format;

	if (j->root[0] == '\0')
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Set destination");
	else if (!j->scan_mmce)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Not checked");
	/* A failed scan of both units cannot establish that no cards exist.
	 * Distinguish inaccessible/MMCE-not-ready from a successful empty scan. */
	else if (!j->any_unit)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "MMCE unavailable");
	else if (!j->total.candidates && !j->total.failed)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "No cards");
	else if (j->total.failed && j->total.pending)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "%d pending, %d errors",
		         j->total.pending, j->total.failed);
	else if (j->total.failed)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "%d scan error%s",
		         j->total.failed, j->total.failed == 1 ? "" : "s");
	else if (j->total.pending)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "%d pending", j->total.pending);
	else
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Locally backed up");

	switch (j->igr_status) {
		case WLE_IGR_STATUS_CURRENT:
			snprintf(igr_status_text, sizeof(igr_status_text), "Current");
			break;
		case WLE_IGR_STATUS_OUTDATED:
			snprintf(igr_status_text, sizeof(igr_status_text), "Outdated");
			break;
		case WLE_IGR_STATUS_NOT_INSTALLED:
			snprintf(igr_status_text, sizeof(igr_status_text), "Not installed");
			break;
		case WLE_IGR_STATUS_PARTIAL:
			snprintf(igr_status_text, sizeof(igr_status_text), "Partial / mixed");
			break;
		default:
			snprintf(igr_status_text, sizeof(igr_status_text),
			         "%s", j->root[0] ? "Not checked" : "Set destination");
			break;
	}
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
	snprintf(google_status_text, sizeof(google_status_text),
	         "%s", j->google_linked ? "Linked" : "Not linked");
#endif

	mmce_status_job = 0;
	free(j);
	if (stale)
		dashToolsRefreshMmceStatus();
}

void dashToolsPrepareMmceStatus(void)
{
#ifdef MMCE
	int loaded = isPathDeviceStackLoaded("mmce0:/") ||
	             isPathDeviceStackLoaded("mmce1:/");

	/* Entering the MMCE management page is an explicit request for live
	 * status. Start the stack on demand here rather than leaving useful
	 * fields at "Not checked" merely because MMCE auto-start is disabled. */
	if (dashCfg.igr_backup_path[0] != '\0' && !loaded) {
		if (mmce_status_job && !dashWorkerAwaitAll("Finishing previous MMCE status check"))
			return;

		snprintf(mmce_status_text, sizeof(mmce_status_text), "Starting MMCE...");
		snprintf(igr_status_text, sizeof(igr_status_text), "Starting MMCE...");
		dashLoadingBegin("Starting MMCE for backup status");
		loaded = loadMmceModules();
		dashLoadingEnd();

		if (!loaded) {
			snprintf(mmce_status_text, sizeof(mmce_status_text), "Unavailable");
			snprintf(igr_status_text, sizeof(igr_status_text), "Unavailable");
			dashModalMessage(DI_WARNING, "MMCE could not be started",
			                 "This page needs MMCE to inspect live cards and the installed IGR helper.",
			                 "You can stay on the page and retry an MMCE action, or check the device/slot and driver settings.");
			return;
		}
	}
#endif
	dashToolsRefreshMmceStatus();
}

void dashToolsRefreshMmceStatus(void)
{
	MmceStatusJob *j;
	int id;

	if (mmce_status_job)
		return;
	j = (MmceStatusJob *)calloc(1, sizeof(*j));
	if (j == NULL)
		return;

	dashStrCopy(j->root, dashCfg.igr_backup_path, sizeof(j->root));
	j->options.keep = dashCfg.mmce_backup_keep;
	j->options.format = dashCfg.mmce_backup_format;
#ifdef MMCE
	j->scan_mmce = j->root[0] != '\0' &&
	               (isPathDeviceStackLoaded("mmce0:/") ||
	                isPathDeviceStackLoaded("mmce1:/"));
#else
	j->scan_mmce = 0;
#endif

	if (j->root[0] == '\0')
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Set destination");
	else if (j->scan_mmce)
		snprintf(mmce_status_text, sizeof(mmce_status_text), "%s", localizeDashboardText("Checking..."));
	else
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Not checked");
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
	snprintf(google_status_text, sizeof(google_status_text), "%s", localizeDashboardText("Checking..."));
#endif

	id = dashWorkerSubmit("Checking MMCE backup status",
	                      DASH_JOB_DEVICE | DASH_JOB_VISIBLE,
	                      mmceStatusRun, mmceStatusDone, j);
	if (id < 0) {
		free(j);
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		snprintf(google_status_text, sizeof(google_status_text), "Not checked");
#endif
		return;
	}
	mmce_status_job = id;
}

const char *dashToolsMmceStatus(void)
{
	return mmce_status_text;
}

const char *dashToolsIgrStatus(void)
{
	return igr_status_text;
}

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
const char *dashToolsGoogleStatus(void)
{
	return google_status_text;
}
#endif

static void mmceSyncProgress(const char *source, int done, int total)
{
	const char *leaf = source != NULL ? strrchr(source, '/') : NULL;
	dashLoadingStatus(leaf != NULL ? leaf + 1 : (source != NULL ? source : "MMCE card"), done, total);
}

static int chooseMmceBackupRoot(char *out, size_t out_size, int change)
{
	char start[MAX_PATH];

	if (!change && dashCfg.igr_backup_path[0] != '\0') {
		dashStrCopy(out, dashCfg.igr_backup_path, out_size);
		return 1;
	}

	dashStrCopy(start, dashCfg.igr_backup_path[0] ? dashCfg.igr_backup_path :
	            (LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir), sizeof(start));
	out[0] = '\0';
	if (!dashPickPath("Parent folder for WLE-MMCE-Backups", start, DASH_PICK_FOLDER, NULL, out, out_size))
		return 0;
	if (out[0] == '\0')
		return 0;
	if (!strcmp(out, dashCfg.igr_backup_path))
		return 1;
	if (dashCfg.igr_backup_path[0] &&
	    !dashModalConfirm("Change backup folder?",
	                      "Existing history stays in the old folder. Reinstall any installed IGR helper to use the new destination.",
	                      "Use folder", 0))
		return 0;

	dashStrCopy(dashCfg.igr_backup_path, out, sizeof(dashCfg.igr_backup_path));
	dashStoreSaveSettings();
	dashStoreFlush(1);
	return 1;
}

void dashToolsSyncAllMmceCards(void)
{
	MmceBackupOptions options = {dashCfg.mmce_backup_keep, dashCfg.mmce_backup_format};
	MmceBackupStats total, one;
	char root[MAX_PATH], summary[384];
	int unit, any_unit = 0;

	if (!dashWorkerAwaitAll("Finishing backup status check"))
		return;
	memset(&total, 0, sizeof(total));
	if (!chooseMmceBackupRoot(root, sizeof(root), 0)) {
		dashToolsRefreshMmceStatus();
		return;
	}
	if (!loadMmceModules()) {
		dashModalMessage(DI_WARNING, "MMCE is unavailable",
		                 "The MMCE driver could not be started.", NULL);
		dashToolsRefreshMmceStatus();
		return;
	}

	dashLoadingBegin("Backing up all MMCE cards");
	for (unit = 0; unit < 2; unit++) {
		memset(&one, 0, sizeof(one));
		if (mmceBackupAllCards(unit, root, &options, &one, mmceSyncProgress) < 0)
			continue;
		any_unit = 1;
		total.candidates += one.candidates;
		total.backed_up += one.backed_up;
		total.unchanged += one.unchanged;
		total.failed += one.failed;
	}
	dashLoadingEnd();

	if (!any_unit || total.candidates == 0) {
		dashModalMessage(DI_WARNING, "No MMCE cards found",
		                 any_unit
		                     ? "MemoryCards folders were found, but contain no .mcd cards. No cloud upload was attempted."
		                     : "No readable MemoryCards tree was found on either MMCE unit.",
		                 NULL);
		dashToolsRefreshMmceStatus();
		return;
	}

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
	if (cloudGoogleIsLinked()) {
		CloudGoogleSyncStats cloud;
		char cloud_error[256];
		int r;

		if (!dashStoreFlushWait("Saving settings before cloud sync"))
			return;
		memset(&cloud, 0, sizeof(cloud));
		cloud_error[0] = '\0';
		dashLoadingBegin("Uploading MMCE backup history");
		r = cloudGoogleSyncBackupRoot(root, &cloud, cloudProgress,
		                              cloud_error, sizeof(cloud_error));
		dashLoadingEnd();
		if (r < 0) {
			snprintf(summary, sizeof(summary),
			         "Local: %d new, %d unchanged, %d failed.\nDrive upload failed: %s",
			         total.backed_up, total.unchanged, total.failed,
			         cloud_error[0] ? cloud_error : "unknown error");
			dashModalMessage(DI_WARNING, "Card sync completed locally", summary,
			                 "Your local backup history was kept.");
			dashToolsRefreshMmceStatus();
			return;
		}
		snprintf(summary, sizeof(summary),
		         "Local: %d new, %d unchanged, %d failed.\nDrive: %d uploaded, %d already present, %d failed.",
		         total.backed_up, total.unchanged, total.failed,
		         cloud.uploaded, cloud.skipped, cloud.failed);
		dashModalMessage((total.failed || cloud.failed) ? DI_WARNING : DI_CHECK,
		                 (total.failed || cloud.failed) ? "Sync all cards completed with errors" : "All cards synchronized",
		                 summary,
		                 "Only changed local cards create new history. Drive sync is copy-only.");
		dashToolsRefreshMmceStatus();
		return;
	}
#endif

	snprintf(summary, sizeof(summary),
	         "%d new backup%s, %d unchanged, %d failed.",
	         total.backed_up, total.backed_up == 1 ? "" : "s",
	         total.unchanged, total.failed);
	dashModalMessage(total.failed ? DI_WARNING : DI_CHECK,
	                 total.failed ? "Local card backup completed with errors" : "All cards backed up locally",
	                 summary,
	                 "Link Google Drive to upload this history too.");
	dashToolsRefreshMmceStatus();
}

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
static int releaseJsonString(const char *json, const char *key, char *out, int out_size)
{
	char needle[64];
	const char *p, *end;
	int n;

	if (json == NULL || key == NULL || out == NULL || out_size <= 0)
		return 0;
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	p = strstr(json, needle);
	if (p == NULL)
		return 0;
	p = strchr(p + strlen(needle), ':');
	if (p == NULL)
		return 0;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p != '\"')
		return 0;
	p++;
	end = strchr(p, '\"');
	if (end == NULL)
		return 0;
	n = (int)(end - p);
	if (n <= 0 || n >= out_size)
		return 0;
	memcpy(out, p, n);
	out[n] = '\0';
	return 1;
}

static int releaseAssetDigest(const char *json, const char *asset_name, char *out, int out_size)
{
	const char *p = json;
	char name[96], digest[96];

	if (json == NULL || asset_name == NULL || out == NULL || out_size < 72)
		return 0;

	while ((p = strstr(p, "\"name\"")) != NULL) {
		const char *next_name;
		const char *digest_key;

		if (!releaseJsonString(p, "name", name, sizeof(name))) {
			p += 6;
			continue;
		}
		if (strcmp(name, asset_name)) {
			p += 6;
			continue;
		}

		next_name = strstr(p + 6, "\"name\"");
		digest_key = strstr(p, "\"digest\"");
		if (digest_key == NULL || (next_name != NULL && digest_key > next_name) ||
		    !releaseJsonString(digest_key, "digest", digest, sizeof(digest)) ||
		    strncmp(digest, "sha256:", 7) != 0 || strlen(digest) != 71)
			return 0;

		snprintf(out, out_size, "%s", digest);
		return 1;
	}

	return 0;
}

/* Prefer the CA shipped with the running ELF. Existing installs may
 * still keep a user-provided CA in the dashboard configuration folder. */
static int launchstationUpdateCaPath(char *path, int path_size)
{
    const char *dir = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
    int n;

    if (dir[0]) {
        size_t len = strlen(dir);
        n = snprintf(path, path_size, "%s%sWLE-UPDATE-CA.PEM", dir,
                     dir[len - 1] == '/' ? "" : "/");
        if (n > 0 && n < path_size && wleExists(path))
            return 1;
    }
    return dashStoreBuildPath(path, path_size, "WLE-UPDATE-CA.PEM") == 0 &&
           wleExists(path);
}

/* Compare the installed commit to the published release before offering to
 * replace the running ELF. A different hash alone cannot prove an upgrade.
 * Page 2 suppresses the large first-page changed-files array in GitHub's
 * compare response, keeping the metadata within the PS2 HTTP body limit. */
enum { UPDATE_RELATION_UNKNOWN = -1, UPDATE_RELATION_SAME = 0,
       UPDATE_RELATION_NEWER = 1, UPDATE_RELATION_OLDER = 2,
       UPDATE_RELATION_DIVERGED = 3 };

static int updateIsCommitId(const char *value)
{
    int i, n;
    if (value == NULL)
        return 0;
    n = (int)strlen(value);
    if (n < 7 || n > 40)
        return 0;
    for (i = 0; i < n; i++) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static int updateCommitRelation(const char *current, const char *latest,
                                const char *ca_path, const char *headers,
                                char *error, int error_size)
{
    CloudHttpResponse response;
    char path[256], status[24];
    int result = UPDATE_RELATION_UNKNOWN, n;

    if (!updateIsCommitId(current) || !updateIsCommitId(latest)) {
        snprintf(error, error_size,
                 "Cannot establish update order for an unrecognized or local build.");
        return result;
    }

    /* compare/base...head: ahead means the release is newer; behind
     * means the installed build is newer. Diverged is NOT an upgrade. */
    n = snprintf(path, sizeof(path),
                 "/repos/NathanNeurotic/Launchstation/compare/%s...%s?per_page=1&page=2",
                 current, latest);
    if (n <= 0 || n >= (int)sizeof(path)) {
        snprintf(error, error_size, "Commit comparison URL was too long.");
        return result;
    }

    memset(&response, 0, sizeof(response));
    if (cloudHttpRequest("GET", "api.github.com", path, headers, NULL, 0,
                         ca_path, &response, error, error_size) < 0)
        return result;
    if (response.status != 200 ||
        !releaseJsonString(response.body, "status", status, sizeof(status))) {
        snprintf(error, error_size,
                 "GitHub did not provide a valid commit ancestry comparison (HTTP %d).",
                 response.status);
    } else if (!strcmp(status, "ahead")) {
        result = UPDATE_RELATION_NEWER;
    } else if (!strcmp(status, "behind")) {
        result = UPDATE_RELATION_OLDER;
    } else if (!strcmp(status, "identical")) {
        result = UPDATE_RELATION_SAME;
    } else if (!strcmp(status, "diverged")) {
        result = UPDATE_RELATION_DIVERGED;
    } else {
        snprintf(error, error_size, "GitHub returned an unknown commit relationship.");
    }
    cloudHttpResponseFree(&response);
    return result;
}

void dashToolsCheckUpdates(void)
{
	CloudHttpResponse response;
	char ca_path[MAX_PATH], error[192], latest[80], asset_digest[80], body[320];
	const char headers[] =
	    "User-Agent: Launchstation-PS2\r\n"
	    "Accept: application/vnd.github+json\r\n";
	int same = 0;

	if (!launchstationUpdateCaPath(ca_path, sizeof(ca_path))) {
		dashModalMessage(DI_WARNING, "Update check needs the CA bundle",
		                 "WLE-UPDATE-CA.PEM was not found beside the running ELF or in the dashboard settings folder.",
		                 "Install the complete Launchstation ZIP/PSU package, or copy res/WLE-UPDATE-CA.PEM beside the boot ELF.");
		return;
	}
	if (!ensureDashboardIpNetworkReady()) {
		dashModalMessage(DI_WARNING, "Update check unavailable",
		                 "The Ethernet stack could not be started.", NULL);
		return;
	}

	memset(&response, 0, sizeof(response));
	error[0] = latest[0] = asset_digest[0] = '\0';
	dashLoadingBegin("Checking for Launchstation updates");
	if (cloudHttpRequest("GET", "api.github.com",
	                     "/repos/NathanNeurotic/Launchstation/releases/tags/latest",
	                     headers, NULL, 0, ca_path, &response,
	                     error, sizeof(error)) < 0) {
		dashLoadingEnd();
		dashModalMessage(DI_WARNING, "Update check failed",
		                 error[0] ? error : "GitHub could not be reached.", NULL);
		return;
	}
	dashLoadingEnd();

	if (response.status != 200 ||
	    !releaseJsonString(response.body, "target_commitish", latest, sizeof(latest)) ||
	    !releaseAssetDigest(response.body, SELF_UPDATE_ASSET,
	                        asset_digest, sizeof(asset_digest))) {
		snprintf(body, sizeof(body), "GitHub returned HTTP %d or an unexpected release response.", response.status);
		cloudHttpResponseFree(&response);
		dashModalMessage(DI_WARNING, "Update check failed", body, NULL);
		return;
	}
	cloudHttpResponseFree(&response);

	if (strcmp(GIT_HASH, "local") != 0 && strlen(GIT_HASH) <= strlen(latest) &&
	    !strncmp(latest, GIT_HASH, strlen(GIT_HASH)))
		same = 1;

	if (same) {
		snprintf(body, sizeof(body), "This build (%s) matches the current latest release.", GIT_HASH);
		dashModalMessage(DI_CHECK, "Launchstation is up to date", body, NULL);
	} else {
		char install_result[384];
		int installed, relation;

		error[0] = '\0';
		dashLoadingBegin("Verifying release is newer");
		relation = updateCommitRelation(GIT_HASH, latest, ca_path, headers,
		                                error, sizeof(error));
		dashLoadingEnd();
		if (relation == UPDATE_RELATION_SAME || relation == UPDATE_RELATION_OLDER) {
			snprintf(body, sizeof(body),
			         "Installed: %s\nPublished: %.16s\n\nThe published release is not newer. No update is needed.",
			         GIT_HASH, latest);
			dashModalMessage(DI_CHECK, "No newer release available", body, NULL);
			return;
		}
		if (relation == UPDATE_RELATION_DIVERGED) {
			dashModalMessage(DI_WARNING, "Release is on another commit history",
			                 "The published release is not a verified descendant of this build. Automatic replacement was blocked.",
			                 "Use a complete ZIP/PSU package to change builds manually.");
			return;
		}
		if (relation != UPDATE_RELATION_NEWER) {
			dashModalMessage(DI_WARNING, "Could not verify update order",
			                 error[0] ? error : "Unable to compare installed and published commits.",
			                 "No files were changed. Update manually only after checking the release.");
			return;
		}

		snprintf(body, sizeof(body),
		         "Current: %s\nLatest: %.16s\n\nGitHub confirms this release is newer. Download and install the packed ELF?",
		         GIT_HASH, latest);
		if (!dashModalConfirm("A newer Launchstation build is available",
		                      body, "Install update", 0))
			return;

		install_result[0] = '\0';
		dashLoadingBegin("Downloading and verifying Launchstation update");
		installed = selfUpdateInstall(ca_path, asset_digest, install_result, sizeof(install_result));
		dashLoadingEnd();
		dashModalMessage(installed == 0 ? DI_CHECK : DI_WARNING,
		                 installed == 0 ? "Update installed" : "Update was not installed",
		                 install_result[0] ? install_result :
		                     (installed == 0 ? "The update was installed." : "The update failed."),
		                 installed == 0
		                     ? "The current session keeps running the old code until you restart. The previous ELF remains beside it as .BAK."
		                     : "The updater stages and verifies the complete download before changing the running ELF.");
	}
}
#else
void dashToolsCheckUpdates(void)
{
	dashModalMessage(DI_INFO, "Update checking unavailable",
	                 "This build does not include native HTTPS support.", NULL);
}
#endif

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
/* The saved refresh token belongs to the OAuth client that issued it, so any
 * client change must unlink this PS2 first. Returns 0 if the user declined. */
static int cloudDisconnectForClientChange(int linked)
{
	char error[192];

	if (!linked)
		return 1;
	if (!dashModalConfirm("Change Google OAuth client?",
	                      "The saved Google authorization belongs to the current OAuth client. Changing it will disconnect this PS2 first.",
	                      "Change & disconnect", 1))
		return 0;
	error[0] = '\0';
	if (cloudGoogleDisconnect(error, sizeof(error)) < 0) {
		dashModalMessage(DI_WARNING, "Could not disconnect Google Drive",
		                 error[0] ? error : "The saved token could not be removed.", NULL);
		return 0;
	}
	dashCfg.igr_cloud_sync = 0;
	dashStoreSaveSettings();
	dashStoreFlush(1);
	return 1;
}

/* Advanced overrides only. Official builds link Google Drive with the built-in
 * Launchstation client and Google root CAs; nothing here is needed normally. */
static void cloudSetupGui(void)
{
	char client_id[384], client_secret[384], error[192], start[MAX_PATH], picked[MAX_PATH];
	int ca_present, linked, builtin, r, selected = -1;

	builtin = cloudGoogleHasBuiltinClient();
	for (;;) {
		DashMenuItem items[5];
		int ids[5], n = 0;

		error[0] = '\0';
		if (cloudGoogleGetClientConfig(client_id, sizeof(client_id),
		                               client_secret, sizeof(client_secret),
		                               &ca_present, error, sizeof(error)) < 0) {
			dashModalMessage(DI_WARNING, "Google Drive setup unavailable",
			                 error[0] ? error : "The cloud settings folder is unavailable.", NULL);
			return;
		}
		linked = cloudGoogleIsLinked();

		items[n].icon = DI_TEXT;
		items[n].label = "Custom OAuth client ID...";
		items[n].detail = client_id[0] ? "Custom client in use" :
		                  builtin ? "Not set (built-in client in use)" : "Required: this build has no built-in client";
		items[n].disabled = 0;
		ids[n++] = 0;

		items[n].icon = DI_TEXT;
		items[n].label = "Custom OAuth client secret...";
		items[n].detail = client_secret[0] ? "Configured" : client_id[0] ? "Required by Google" : "Not set";
		items[n].disabled = !client_id[0];
		ids[n++] = 1;

		if (client_id[0] && builtin) {
			items[n].icon = DI_REFRESH;
			items[n].label = "Use built-in Launchstation client";
			items[n].detail = "Remove the custom client from WLE-CLOUD.CNF";
			items[n].disabled = 0;
			ids[n++] = 4;
		}

		items[n].icon = DI_FILE;
		items[n].label = ca_present ? "Replace custom CA bundle..." : "Install custom CA bundle...";
		items[n].detail = ca_present ? "WLE-CLOUD-CA.PEM overrides built-in Google roots" : "Not needed (built-in Google roots in use)";
		items[n].disabled = 0;
		ids[n++] = 2;

		items[n].icon = DI_BACK;
		items[n].label = "Done";
		items[n].detail = NULL;
		items[n].disabled = 0;
		ids[n++] = 3;

		r = dashModalMenu("Advanced Google Drive setup",
		                  "Optional: use your own Google Cloud OAuth client",
		                  items, n, selected >= 0 ? selected : n - 1);
		selected = r;
		if (r < 0 || ids[r] == 3)
			return;

		if (ids[r] == 4) {
			if (!cloudDisconnectForClientChange(linked))
				continue;
			error[0] = '\0';
			if (cloudGoogleClearClientConfig(error, sizeof(error)) < 0)
				dashModalMessage(DI_WARNING, "Custom client not removed",
				                 error[0] ? error : "WLE-CLOUD.CNF could not be removed.", NULL);
			else
				dashToast(DASH_TOAST_OK, "Using built-in Google client");
			continue;
		}

		if (ids[r] == 0 || ids[r] == 1) {
			char previous[384];
			char *field = ids[r] == 0 ? client_id : client_secret;
			const char *title = ids[r] == 0 ? "Google OAuth client ID (TV / limited input)" : "Google OAuth client secret";

			dashStrCopy(previous, field, sizeof(previous));
			if (!dashModalKeyboard(title, field, 384) || !strcmp(previous, field))
				continue;
			if (ids[r] == 0 && field[0] == '\0') {
				dashModalMessage(DI_WARNING, "Client ID required",
				                 builtin ? "To go back to the built-in client, choose Use built-in Launchstation client."
				                         : "Google Drive linking needs a TV / limited-input OAuth client ID.", NULL);
				continue;
			}
			if (!cloudDisconnectForClientChange(linked))
				continue;
			error[0] = '\0';
			if (cloudGoogleSaveClientConfig(client_id, client_secret, error, sizeof(error)) < 0)
				dashModalMessage(DI_WARNING, "Google OAuth settings not saved",
				                 error[0] ? error : "WLE-CLOUD.CNF could not be written.", NULL);
			else
				dashToast(DASH_TOAST_OK, "Google OAuth settings saved");
			continue;
		}

		if (ids[r] == 2) {
			dashStrCopy(start, LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir, sizeof(start));
			picked[0] = '\0';
			if (!dashPickPath("Choose Google CA PEM bundle", start, DASH_PICK_FILE, "PEM",
			                  picked, sizeof(picked)))
				continue;
			error[0] = '\0';
			if (cloudGoogleInstallCaBundle(picked, error, sizeof(error)) < 0)
				dashModalMessage(DI_WARNING, "CA bundle not installed",
				                 error[0] ? error : "The selected PEM bundle could not be installed.", picked);
			else
				dashModalMessage(DI_CHECK, "Google CA bundle installed",
				                 "Verified HTTPS for Google Drive can now use WLE-CLOUD-CA.PEM.", picked);
		}
	}
}


/* Read-only, bounded navigation through the app-created Drive backup tree.
 * There is deliberately no direct restore or download button here: an MMCE
 * card must not be replaced without active-card and write safety guarantees. */
static void cloudBrowseHistory(void)
{
    CloudGoogleBrowsePage *page;
    char current[192] = "", token[256] = "", parent_ids[12][192];
    char folder_name[256] = "WLE-MMCE-Backups", parent_names[12][256];
    char names[CLOUD_GOOGLE_BROWSE_MAX][256];
    int depth = 0, i;

    if (!cloudGoogleIsLinked()) {
        dashModalMessage(DI_INFO, "Google Drive is not linked",
                         "Link Google Drive before browsing backed-up snapshots.", NULL);
        return;
    }
    if (!dashWorkerAwaitAll("Finishing device operations"))
        return;
    page = (CloudGoogleBrowsePage *)calloc(1, sizeof(*page));
    if (!page) {
        dashModalMessage(DI_WARNING, "Not enough memory",
                         "Could not allocate the Drive history page.", NULL);
        return;
    }
    for (;;) {
        char error[192];
        DashMenuItem items[CLOUD_GOOGLE_BROWSE_MAX + 3];
        int actions[CLOUD_GOOGLE_BROWSE_MAX + 3], n = 0, choice;

        error[0] = '\0';
        dashLoadingBegin("Reading Google Drive backup history");
        i = cloudGoogleListBackupHistory(current, token, page,
                                         error, sizeof(error));
        dashLoadingEnd();
        if (i < 0) {
            dashModalMessage(DI_WARNING, "Cloud history unavailable",
                             error[0] ? error : "Could not list the Drive backup folder.",
                             "Existing local and cloud backups were not changed.");
            break;
        }
        if (current[0] == '\0')
            dashStrCopy(current, page->folder_id, sizeof(current));
        items[n].icon = DI_BACK;
        items[n].label = depth ? "Up one folder" : "Close";
        items[n].detail = depth ? "Return to the parent backup folder" : NULL;
        items[n].disabled = 0;
        actions[n++] = -1;
        for (i = 0; i < page->count; ++i) {
            CloudGoogleBrowseItem *entry = &page->items[i];
            /* Modal item labels must remain live while the selection runs. */
            dashStrCopy(names[i], entry->name, sizeof(names[i]));
            items[n].icon = entry->is_folder ? DI_FOLDER : DI_MC;
            items[n].label = names[i];
            items[n].detail = entry->is_folder ? "Browse backup folder" :
                              (entry->sha256[0] ? "Select to download, verify and recover locally" :
                               "Missing uploaded SHA-256: inspect only");
            items[n].disabled = 0;
            actions[n++] = i;
        }
        if (page->next_page_token[0]) {
            items[n].icon = DI_REFRESH;
            items[n].label = "Next page";
            items[n].detail = "Load more Drive backup entries";
            items[n].disabled = 0;
            actions[n++] = -2;
        }
        if (page->count == 0 && !page->next_page_token[0]) {
            items[n].icon = DI_INFO;
            items[n].label = "No backup files in this folder";
            items[n].detail = "This page contains no visible app-created snapshots";
            items[n].disabled = 1;
            actions[n++] = -3;
        }
        choice = dashModalMenu("Google Drive backup history", folder_name,
                               items, n, 0);
        if (choice < 0 || actions[choice] == -1) {
            if (depth == 0)
                break;
            dashStrCopy(current, parent_ids[--depth], sizeof(current));
            dashStrCopy(folder_name, parent_names[depth], sizeof(folder_name));
            token[0] = '\0';
            continue;
        }
        if (actions[choice] == -2) {
            dashStrCopy(token, page->next_page_token, sizeof(token));
            continue;
        }
        if (actions[choice] < 0)
            continue;
        {
            CloudGoogleBrowseItem *item = &page->items[actions[choice]];
            if (!item->is_folder) {
                char folder[MAX_PATH], result[MAX_PATH], error[256];
                int ok;
                if (item->sha256[0] == '\0') {
                    dashModalMessage(DI_INFO, "Cloud snapshot lacks a verified digest",
                                     item->name,
                                     "Only backups with an uploaded SHA-256 can be downloaded and recovered safely.");
                    continue;
                }
                dashStrCopy(folder, dashCfg.igr_backup_path[0] ?
                            dashCfg.igr_backup_path :
                            (LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir),
                            sizeof(folder));
                if (!dashPickPath("Download snapshot to NEW image on USB / HDD",
                                  folder, DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
                    continue;
                if (!dashModalConfirm("Recover selected cloud snapshot?",
                                      "The complete Drive file will be downloaded, SHA-256 checked and converted to a NEW .mcd on USB/HDD. Existing MMCE cards and backup history are untouched.",
                                      "Recover", 0))
                    continue;
                if (!dashWorkerAwaitAll("Finishing device operations"))
                    continue;
                error[0] = result[0] = '\0';
                dashLoadingBegin("Downloading and verifying Google card backup");
                ok = cloudGoogleRecoverSnapshot(item, folder, result, sizeof(result),
                                                error, sizeof(error));
                dashLoadingEnd();
                if (ok == 0)
                    dashModalMessage(DI_CHECK, "Cloud card recovered as a new image",
                                     result,
                                     "SHA-256 and PS2 card geometry verified. No MMCE card was replaced or activated.");
                else
                    dashModalMessage(DI_WARNING, "Cloud card recovery failed",
                                     error[0] ? error : "No verified image was published.",
                                     "An existing card and saved backup history were not changed.");
                continue;
            }
            if (depth >= 12) {
                dashToast(DASH_TOAST_WARN, "Backup folder nesting limit reached");
                continue;
            }
            dashStrCopy(parent_ids[depth], current, sizeof(parent_ids[0]));
            dashStrCopy(parent_names[depth], folder_name, sizeof(parent_names[0]));
            depth++;
            dashStrCopy(current, item->id, sizeof(current));
            dashStrCopy(folder_name, item->name, sizeof(folder_name));
            token[0] = '\0';
        }
    }
    free(page);
}

void dashToolsCloudBackup(void)
{
	char error[192], client_id[384], client_secret[384];
	int ca_present, configured, linked, has_token, r, selected = -1;
	int builtin = cloudGoogleHasBuiltinClient();

	for (;;) {
		DashMenuItem items[7];
		int ids[7], n = 0;

		error[0] = '\0';
		if (cloudGoogleGetClientConfig(client_id, sizeof(client_id),
		                               client_secret, sizeof(client_secret),
		                               &ca_present, error, sizeof(error)) < 0) {
			dashModalMessage(DI_WARNING, "Google Drive unavailable",
			                 error[0] ? error : "The cloud settings folder is unavailable.", NULL);
			return;
		}
		error[0] = '\0';
		configured = cloudGoogleConfigured(error, sizeof(error));
		linked = configured && cloudGoogleIsLinked();
		has_token = cloudGoogleHasLocalToken();

		items[n].icon = DI_REFRESH;
		items[n].label = "Back up cards & upload";
		items[n].detail = linked ? "Create changed card backups, then upload history" : "Link Google Drive before backing up and uploading";
		items[n].disabled = !linked;
		ids[n++] = 4;

		items[n].icon = DI_NETWORK;
		items[n].label = linked ? "Relink Google Drive" : "Link Google Drive";
		items[n].detail = linked ? "Request a new Google authorization" :
		                  configured ? "Enter a short code at google.com/device" : "This build has no built-in Google client";
		items[n].disabled = !configured;
		ids[n++] = 1;

		items[n].icon = DI_REFRESH;
		items[n].label = "Upload existing backups";
		items[n].detail = dashCfg.igr_backup_path[0] ? dashCfg.igr_backup_path : "Choose Backup folder below first";
		items[n].disabled = !linked || !dashCfg.igr_backup_path[0];
		ids[n++] = 2;

		items[n].icon = DI_FOLDER;
		items[n].label = "Browse cloud history...";
		items[n].detail = "Read-only folders and saved card snapshots";
		items[n].disabled = !linked;
		ids[n++] = 6;

		items[n].icon = DI_FILE;
		items[n].label = "Backup folder...";
		items[n].detail = dashCfg.igr_backup_path[0] ? dashCfg.igr_backup_path : "Select the parent of WLE-MMCE-Backups";
		items[n].disabled = 0;
		ids[n++] = 5;

		items[n].icon = DI_CLOSE;
		items[n].label = linked ? "Disconnect Google Drive" : "Clear saved Google token";
		items[n].detail = "Drive files are not deleted";
		items[n].disabled = !has_token;
		ids[n++] = 3;

		items[n].icon = DI_SETTINGS;
		items[n].label = "Advanced setup...";
		items[n].detail = client_id[0] ? "Custom OAuth client in WLE-CLOUD.CNF" :
		                  builtin ? "Optional: custom OAuth client or CA bundle" : "Custom OAuth client required";
		items[n].disabled = 0;
		ids[n++] = 0;

		r = dashModalMenu("Google Drive backups",
		                  linked ? "Linked" : configured ? "Not linked yet" : "Setup required",
		                  items, n, selected >= 0 ? selected : (linked ? 0 : configured ? 1 : n - 1));
		selected = r;
		if (r < 0)
			return;

		switch (ids[r]) {
			case 6:
				cloudBrowseHistory();
				continue;
			case 5: {
				char folder[MAX_PATH];
				if (!dashWorkerAwaitAll("Finishing backup status check"))
					continue;
				chooseMmceBackupRoot(folder, sizeof(folder), 1);
				dashToolsRefreshMmceStatus();
				continue;
			}
			case 4:
				dashToolsSyncAllMmceCards();
				continue;
			case 0:
				cloudSetupGui();
				continue;
			case 1:
				cloudLink();
				continue;
			case 2:
				cloudSyncNow();
				continue;
			case 3:
				if (!dashModalConfirm(linked ? "Disconnect Google Drive?" : "Clear saved Google token?",
				                      "Deletes the saved refresh token from this PS2. Files already uploaded to Drive are untouched.",
				                      linked ? "Disconnect" : "Clear token", 1))
					continue;
				error[0] = '\0';
				if (cloudGoogleDisconnect(error, sizeof(error)) == 0) {
					dashCfg.igr_cloud_sync = 0;
					dashStoreSaveSettings();
					dashStoreFlush(1);
					dashModalMessage(DI_CHECK, linked ? "Google Drive disconnected" : "Saved Google token removed",
					                 "The local OAuth token was removed and automatic IGR cloud sync was turned off.",
					                 "Files already uploaded to Drive are untouched.");
				} else
					dashModalMessage(DI_WARNING, "Could not remove saved Google token",
					                 error[0] ? error : "Token removal failed.", NULL);
				continue;
		}
	}
}
#endif

/* Explicit hardware diagnostic for two connected USB drives. Never called by
 * startup/device auto-probe and never modifies global USB mount discovery.
 * A wedged IOP fileXio RPC cannot be cancelled; warn before every probe. */
static void dashToolsUsbUnitDiagnostics(void)
{
	static const char *const choices[] = {
		"Show cached units (no I/O)",
		"Probe logical unit 0 once",
		"Probe logical unit 1 once"
	};
	char path[12], status[96], body[240];
	iox_stat_t stat;
	int choice = dashModalChoice("USB unit diagnostics", choices, 3, 0);
	int mask, result;

	if (choice < 0 || choice > 2)
		return;
	if (choice == 0) {
		if (!filerUsbDriversLoaded()) {
			dashModalMessage(DI_INFO, "USB cache", "USB storage drivers are not loaded. No USB filesystem request was made.", NULL);
			return;
		}
		mask = filerUsbDriveMask();
		snprintf(body, sizeof(body),
		         "Last recorded units: %s%s%s. This is cached state only; no device I/O was performed.",
		         (mask & 1) ? "unit 0" : "",
		         ((mask & 3) == 3) ? ", " : "",
		         (mask & 2) ? "unit 1" : ((mask & 1) ? "" : "none confirmed"));
		dashModalMessage(DI_INFO, "USB cached units", body, NULL);
		return;
	}

	if (!dashModalConfirm("Probe one USB unit?",
	                      "A single synchronous USB filesystem request may freeze a failing console. No automatic IOP reset or timeout can interrupt it. Continue?",
	                      "Probe once", 0))
		return;
	if (!dashWorkerAwaitAll("Finishing background storage work"))
		return;
	/* Worker/file operations must finish before accessing synchronous IOP
	 * fileXio, and libmc may still have a pending EE-side status RPC. */
	filerRootMcPollIdle();
	dashLoadingBegin("Preparing USB diagnostic");
	if (!isUsbMassStackLoaded())
		loadUsbModules();
	if (!isUsbMassStackLoaded()) {
		dashLoadingEnd();
		dashModalMessage(DI_WARNING, "USB driver unavailable",
		                 "USB modules could not be started. No filesystem probe was issued.", NULL);
		return;
	}
#ifdef EXFAT
	snprintf(path, sizeof(path), "usb%c:/", (char)('0' + choice - 1));
#else
	snprintf(path, sizeof(path), "mass%c:/", (char)('0' + choice - 1));
#endif
	snprintf(status, sizeof(status), "Checking USB root %s", path);
	/* Present this exact unit before the blocking RPC, so the last frame
	 * identifies the stuck request even if the pad/IOP becomes unresponsive. */
	dashLoadingFrame(status, 0, 0);
	DPRINTF("[USB diagnostic] begin %s\n", path);
	memset(&stat, 0, sizeof(stat));
	result = fileXioGetStat(path, &stat);
	DPRINTF("[USB diagnostic] end %s result=%d\n", path, result);
	dashLoadingEnd();
	snprintf(body, sizeof(body),
	         "%s returned %d (%s). One logical unit was probed, not the other. A successful root stat does not prove file reads or power stability.",
	         path, result, result >= 0 ? "root responded" : "root unavailable / error");
	dashModalMessage(result >= 0 ? DI_CHECK : DI_WARNING,
	                 "Single-unit USB probe result", body,
	                 "Logical USB units are not guaranteed to match physical ports.");
}

/* Close the command popup before dispatching so native tab switches are
 * immediately visible. Labels follow the configured MISC paths. */
static void showMiscCommands(void)
{
	const struct {
		const char *path;
		int icon;
	} commands[] = {
	    {setting->Misc_PS2Disc, DI_DISC},
	    {setting->Misc_FileBrowser, DI_FOLDER},
	    {setting->Misc_PS2Browser, DI_APP},
	    {setting->Misc_PS2Net, DI_NETWORK},
	    {setting->Misc_PS2PowerOff, DI_POWER},
	    {setting->Misc_HddManager, DI_HDD},
	    {setting->Misc_TextEditor, DI_TEXT},
	    {setting->Misc_Configure, DI_SETTINGS},
	    {setting->Misc_ShowFont, DI_TEXT},
	    {setting->Misc_Debug_Info, DI_INFO},
	    {setting->Misc_About_uLE, DI_INFO},
	    {setting->Misc_Show_Build_Info, DI_INFO},
	    {setting->Misc_OSDSYS, DI_APP},
	    {setting->Misc_Exploit_Installer, DI_MC},
	    {setting->Misc_Reboot_IOP, DI_REFRESH}};
	DashMenuItem items[sizeof(commands) / sizeof(commands[0])];
	const char *paths[sizeof(commands) / sizeof(commands[0])];
	size_t prefix = strlen(setting->Misc);
	unsigned int i;
	int count = 0, selected;

	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		const char *path = commands[i].path;

		if (strncmp(path, setting->Misc, prefix) || path[prefix] == '\0')
			continue;
		items[count].icon = commands[i].icon;
		items[count].label = path + prefix;
		items[count].detail = NULL;
		items[count].disabled = 0;
		paths[count++] = path;
	}
	if (count == 0)
		return;
	selected = dashModalMenu(localizeDashboardText("MISC commands"), NULL, items, count, 0);
	if (selected >= 0 && selected < count)
		dashRunMisc(paths[selected]);
}

static void runTool(const Tool *t)
{
	if (t->confirm) {
		char body[200];
		const char *action_label = t->confirm == 2 ? localizeDashboardText(t->name) : "Continue";
		if (t->action == TA_MISC &&
		    (t->misc_offset == MISC(Misc_PS2Browser) || t->misc_offset == MISC(Misc_OSDSYS)))
			action_label = "Leave";
		snprintf(body, sizeof(body), "%s", localizeDashboardText(t->desc));
		if (!dashModalConfirm(localizeDashboardText(t->name), body, action_label, t->confirm == 2))
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
		case TA_MISC_MENU:
			showMiscCommands();
			break;
		case TA_EDITOR:
			dashOpenTextEditor(NULL);
			break;
		case TA_USB_DIAG:
			dashToolsUsbUnitDiagnostics();
			break;
		case TA_IGR_SETUP:
			dashToolsIgrBackupSetup();
			break;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
		case TA_CLOUD:
			dashToolsCloudBackup();
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
	const float lx = DASH_SAFE_L, ly = 102, row_h = 29, lw = 300;
	const int rows = 10;
	const Tool *t = &tools[tsel];
	int i;

	dashDrawSectionTitle(lx, 68, localizeDashboardText("Tools"), "everything Launchstation can do");
	if (tsel < ttop + 1)
		ttop = tsel - 1;
	if (tsel >= ttop + rows)
		ttop = tsel - rows + 1;
	if (ttop < 0)
		ttop = 0;
	if (tsel_anim < ttop || tsel_anim >= ttop + rows)
		tsel_anim = (float)tsel;

	dashDrawPanelBox(lx - 6, ly - 6, lw + 12, rows * row_h + 12, 14);
	dashGfxScissor(lx - 6, ly - 4, lw + 12, rows * row_h + 8);
	{
		float fy = ly + (tsel_anim - ttop) * row_h;
		dashDrawSelection(lx, fy, lw, row_h - 3, 9, 1.0f);
	}
	for (i = ttop; i < NTOOLS && i < ttop + rows; i++) {
		float y = ly + (i - ttop) * row_h;

		if (tools[i].action == TA_HEADER) {
			dashText(lx + 8, y + 7, localizeDashboardText(tools[i].name), DASH_FS_CAPTION, DASH_C_ACCENT);
			continue;
		}
		dashIcon(tools[i].icon, lx + 10, y + 5, 16, dashItemColor(i == tsel));
		dashTextFit(lx + 34, y + 3.0f, lw - 44, localizeDashboardText(tools[i].name), DASH_FS_SMALL, dashItemColor(i == tsel));
		if (tools[i].confirm == 2 || tools[i].note)
			dashGlow(lx + lw - 14, y + 13, 4, 4, tools[i].confirm == 2 ? DASH_C_DANGER : DASH_C_WARM);
	}
	dashGfxScissorReset();
	dashDrawScrollbar(lx + lw + 10, ly, rows * row_h, ttop, rows, NTOOLS);

	{
		float dx = lx + lw + 24, dy = 98, dw = DASH_PANE_R - dx, dh = 292;

		dashDrawCard(dx, dy, dw, dh, 16);
		dashAccentGlow(dx + dw * 0.5f, dy + 66, 80, 60, 1.0f);
		dashIcon(t->icon, dx + dw * 0.5f - 30, dy + 34, 60, DASH_C_TEXT);
		dashTextCenter(dx + dw * 0.5f, dy + 108, localizeDashboardText(t->name), DASH_FS_BODY, DASH_C_TEXT);
		dashTextWrap(dx + 16, dy + 142, dw - 32, 4, localizeDashboardText(t->desc), DASH_FS_CAPTION, DASH_C_TEXT2, 19);
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
