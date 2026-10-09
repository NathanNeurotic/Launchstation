//--------------------------------------------------------------
//File name:   dash_tools.c
//TOOLS: every wLaunchELF function with a friendly name and a short
//description. Each entry dispatches the MISC command path through
//dashRunMisc(), which opens the native dashboard version of each tool or
//runs the command (disc, OSDSYS, network...) behind the loading overlay.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "mmce_igr.h"
#include "mmce_backup.h"
#include "mmce_igr.h"
#include "cloud_google.h"
#include "self_update.h"
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
#include "cloud_http.h"
#include "init.h"
#endif
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
    {TA_MISC, DI_APP, "PS2 system browser", "Leave Launchstation and open the console's own Browser.", NULL, 1, MISC(Misc_PS2Browser), NULL},
    {TA_MISC, DI_APP, "OSDSYS", "Start the console's OSD (or the OSDSYS path configured in Launchstation).", NULL, 1, MISC(Misc_OSDSYS), NULL},
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
	         "The token is stored only on this PS2. Scope: files created by Launchstation.");
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
	else if (!j->any_unit)
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
		snprintf(mmce_status_text, sizeof(mmce_status_text), "All synced");

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
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Checking...");
	else
		snprintf(mmce_status_text, sizeof(mmce_status_text), "Not checked");
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
	snprintf(google_status_text, sizeof(google_status_text), "Checking...");
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

static int ensureMmceBackupRoot(char *out, size_t out_size)
{
	char start[MAX_PATH];

	if (dashCfg.igr_backup_path[0] != '\0') {
		dashStrCopy(out, dashCfg.igr_backup_path, out_size);
		return 1;
	}

	dashStrCopy(start, LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir, sizeof(start));
	out[0] = '\0';
	if (!dashPickPath("MMCE backup destination", start, DASH_PICK_FOLDER, NULL, out, out_size))
		return 0;
	if (out[0] == '\0')
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
	if (!ensureMmceBackupRoot(root, sizeof(root))) {
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

	if (!any_unit) {
		dashModalMessage(DI_WARNING, "No MMCE cards found",
		                 "No readable MemoryCards tree was found on either MMCE unit.", NULL);
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

void dashToolsCheckUpdates(void)
{
	CloudHttpResponse response;
	char ca_path[MAX_PATH], error[192], latest[80], asset_digest[80], body[320];
	const char headers[] =
	    "User-Agent: Launchstation-PS2\r\n"
	    "Accept: application/vnd.github+json\r\n";
	int same = 0;

	if (dashStoreBuildPath(ca_path, sizeof(ca_path), "WLE-UPDATE-CA.PEM") < 0 ||
	    !wleExists(ca_path)) {
		dashModalMessage(DI_WARNING, "Update check needs the CA bundle",
		                 "WLE-UPDATE-CA.PEM was not found in the dashboard settings folder.",
		                 "Copy res/WLE-UPDATE-CA.PEM from the source repository to enable the GitHub update check.");
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
		int installed;

		snprintf(body, sizeof(body),
		         "Current: %s\nLatest: %.16s\n\nDownload, verify and install the latest packed ELF beside the running copy?",
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
static int cloudSetupGui(void)
{
	char client_id[384], client_secret[384], error[192], start[MAX_PATH], picked[MAX_PATH];
	int ca_present, linked, r, selected = -1;

	for (;;) {
		DashMenuItem items[4];
		int ids[4], n = 0;

		error[0] = '\0';
		if (cloudGoogleGetClientConfig(client_id, sizeof(client_id),
		                               client_secret, sizeof(client_secret),
		                               &ca_present, error, sizeof(error)) < 0) {
			dashModalMessage(DI_WARNING, "Google Drive setup unavailable",
			                 error[0] ? error : "The cloud settings folder is unavailable.", NULL);
			return 0;
		}
		linked = cloudGoogleIsLinked();

		items[n].icon = DI_TEXT;
		items[n].label = "OAuth client ID...";
		items[n].detail = client_id[0] ? "Configured" : "Required";
		items[n].disabled = 0;
		ids[n++] = 0;

		items[n].icon = DI_TEXT;
		items[n].label = "OAuth client secret...";
		items[n].detail = client_secret[0] ? "Configured (optional)" : "Not set (optional)";
		items[n].disabled = 0;
		ids[n++] = 1;

		items[n].icon = DI_FILE;
		items[n].label = ca_present ? "Replace Google CA bundle..." : "Install Google CA bundle...";
		items[n].detail = ca_present ? "WLE-CLOUD-CA.PEM installed" : "Required for verified HTTPS";
		items[n].disabled = 0;
		ids[n++] = 2;

		items[n].icon = DI_BACK;
		items[n].label = "Done";
		items[n].detail = (client_id[0] && ca_present) ? "Setup ready" : "Setup incomplete";
		items[n].disabled = 0;
		ids[n++] = 3;

		r = dashModalMenu("Google Drive setup",
		                  linked ? "Account linked — changing OAuth credentials disconnects it"
		                         : "Configure the Google TV / limited-input OAuth client",
		                  items, n, selected >= 0 ? selected : (!client_id[0] ? 0 : !ca_present ? 2 : 3));
		selected = r;
		if (r < 0 || ids[r] == 3)
			return client_id[0] && ca_present;

		if (ids[r] == 0 || ids[r] == 1) {
			char previous[384];
			char *field = ids[r] == 0 ? client_id : client_secret;
			const char *title = ids[r] == 0 ? "Google OAuth client ID" : "Google OAuth client secret (optional)";

			dashStrCopy(previous, field, sizeof(previous));
			if (!dashModalKeyboard(title, field, 384) || !strcmp(previous, field))
				continue;
			if (ids[r] == 0 && field[0] == '\0') {
				dashModalMessage(DI_WARNING, "Client ID required",
				                 "Google Drive linking needs a TV / limited-input OAuth client ID.", NULL);
				continue;
			}
			if (linked &&
			    !dashModalConfirm("Change Google OAuth credentials?",
			                      "The saved Google refresh token belongs to the current OAuth client. Changing credentials will disconnect this PS2 first.",
			                      "Change & disconnect", 1))
				continue;
			if (linked) {
				error[0] = '\0';
				if (cloudGoogleDisconnect(error, sizeof(error)) < 0) {
					dashModalMessage(DI_WARNING, "Could not disconnect Google Drive",
					                 error[0] ? error : "The saved token could not be removed.", NULL);
					continue;
				}
				dashCfg.igr_cloud_sync = 0;
				dashStoreSaveSettings();
				dashStoreFlush(1);
			}
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

void dashToolsCloudBackup(void)
{
	char error[192], client_id[384], client_secret[384];
	int ca_present, configured, linked, has_token, r, selected = -1;

	for (;;) {
		DashMenuItem items[5];
		int ids[5], n = 0;

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

		items[n].icon = DI_SETTINGS;
		items[n].label = "Google Drive setup...";
		items[n].detail = configured ? "OAuth client and CA bundle ready" :
		                  (client_id[0] ? "CA bundle or configuration missing" : "OAuth client ID required");
		items[n].disabled = 0;
		ids[n++] = 0;

		items[n].icon = DI_NETWORK;
		items[n].label = linked ? "Relink Google Drive" : "Link Google Drive";
		items[n].detail = linked ? "Request a new Google authorization" : "Authorize this PS2 with a device code";
		items[n].disabled = !configured;
		ids[n++] = 1;

		items[n].icon = DI_REFRESH;
		items[n].label = "Upload existing backups";
		items[n].detail = dashCfg.igr_backup_path[0] ? dashCfg.igr_backup_path : "Choose Backup folder in MMCE backup settings first";
		items[n].disabled = !linked || !dashCfg.igr_backup_path[0];
		ids[n++] = 2;

		items[n].icon = DI_CLOSE;
		items[n].label = linked ? "Disconnect Google Drive" : "Clear saved Google token";
		items[n].detail = "Drive files are not deleted";
		items[n].disabled = !has_token;
		ids[n++] = 3;

		r = dashModalMenu("Google Drive backups",
		                  linked ? "Linked" : configured ? "Ready to link" : "Setup required",
		                  items, n, selected >= 0 ? selected : (linked ? 0 : configured ? 2 : 1));
		selected = r;
		if (r < 0)
			return;

		switch (ids[r]) {
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
	const float lx = 48, ly = 102, row_h = 29, lw = 300;
	const int rows = 10;
	const Tool *t = &tools[tsel];
	int i;

	dashDrawSectionTitle(48, 68, "Tools", "everything Launchstation can do");
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
