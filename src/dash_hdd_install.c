#include "dash_internal.h"
#include "hdd_install.h"
#include "init.h"
#include "filer_shared.h"
#include "hdd_install_recovery.h"
#include "dash_sound.h"

const char *dashHddInstallChoiceTitle(const HddInstall *in, unsigned choice, const char *title, char *out, size_t size)
{
    if (!(in->flags & HDD_INSTALL_PSBBN)) return title;
    snprintf(out, size, "Choice %u of %u: %s", choice, (unsigned)HDD_CHOICE_CONFIRM, title);
    return out;
}

int dashHddInstallChoose(HddInstall *in, int jail)
{
    memset(in, 0, sizeof(*in)); in->jail = jail;
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
#ifndef EXFAT
    if (jail) return 1; /* no ATA filesystem driver in this build */
#endif
    {
        static const char *const profiles[] = {
            "No software, format only",
            "Install WLE-R3Z and R3CONFIGURATOR",
            "Install OSDMenu MBR and HOSDMenu",
            "Install MBR, HOSDMenu and included apps"
        };
        static const unsigned flags[] = {0, HDD_INSTALL_APPS, HDD_INSTALL_HOSD, HDD_INSTALL_HOSD | HDD_INSTALL_APPS};
        static const DashMenuItem jail_profiles[] = {
            {-1, "No software, format only", NULL, 0},
            {-1, "PSBBN DP, HOSDMenu", "EXPERIMENTAL", 0}
        };
        int selected = jail ? dashModalMenu("Software after formatting", NULL, jail_profiles, 2, 0) :
                              dashModalSelect("Software after formatting", profiles, 4, 0);
        if (selected < 0) return 0;
        in->flags = jail && selected ? HDD_INSTALL_PSBBN | HDD_INSTALL_HOSD | HDD_INSTALL_APPS : flags[selected];
        if (in->flags & HDD_INSTALL_PSBBN) {
            char title[128];
            dashModalMessage(DI_INFO, dashHddInstallChoiceTitle(in, HDD_CHOICE_WARNING, "PSBBN DP installation", title, sizeof(title)),
                "PSBBN DP requires a Sony Network Adapter. Third party clones are not compatible (Example: GameStar). HOSDMenu, is installed as well which does not have this limitation.", NULL);
            static const char *const launchers[] = {"Neutrino (NHDDL)", "Open PS2 Loader (OPL)"};
            selected = dashModalSelect(dashHddInstallChoiceTitle(in, HDD_CHOICE_LAUNCHER, "Default game launcher", title, sizeof(title)), launchers, 2, 0);
            if (selected < 0) return 0;
            in->game_launcher = selected ? HDD_GAME_OPL : HDD_GAME_NEUTRINO;
        }
    }
#endif
    return 1;
}

#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
static int installSize(HddInstall *in, unsigned choice, const char *label, unsigned maximum, int optional)
{
    char labels[7][24], title[128];
    DashMenuItem items[7];
    int count = optional, selected;
    if (optional) items[0] = (DashMenuItem){-1, "Off", NULL, 0};
    for (unsigned mb = 1024; mb <= maximum && count < 6 + optional; mb *= 2) {
        snprintf(labels[count], sizeof(labels[count]), optional ? "On: %u GiB home" : "%u GiB", mb / 1024);
        items[count] = (DashMenuItem){-1, labels[count], NULL, 0}; count++;
    }
    if (!count) return -1;
    selected = dashModalMenu(dashHddInstallChoiceTitle(in, choice, label, title, sizeof(title)),
                            optional ? "Requires a USB keyboard; adds a 2 GiB system partition." : NULL, items, count, 0);
    return selected < 0 ? -1 : optional && !selected ? 0 : 1024 << (selected - optional);
}
#endif
int dashHddInstallConfigure(HddInstall *in, unsigned max_extent)
{
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
    int value;
    value = installSize(in, HDD_CHOICE_MUSIC, "Music partition size", max_extent, 0);
    if (value < 0) return 0;
    in->music_mb = value;
    value = installSize(in, HDD_CHOICE_CONTENTS, "Contents partition size", max_extent, 0);
    if (value < 0) return 0;
    in->contents_mb = value;
    unsigned maximum = max_extent < HDD_INSTALL_HOME_MAX_MB ? max_extent : HDD_INSTALL_HOME_MAX_MB;
    value = installSize(in, HDD_CHOICE_LINUX, "Standalone PS2 Linux", maximum, 1);
    if (value < 0) return 0;
    in->flags &= ~HDD_INSTALL_PS2LINUX;
    in->linux_home_mb = 0;
    if (value) {
        in->linux_home_mb = value;
        in->flags |= HDD_INSTALL_PS2LINUX;
    }
    return 1;
#else
    (void)in; (void)max_extent; return 0;
#endif
}

/* Preserve the CA file before formatting, including when Launchstation and
 * its settings were loaded from the drive that is about to be erased. */
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
typedef struct {
    HddInstall *install;
    HddInstallLanguage languages[HDD_INSTALL_LANGUAGES];
    int count;
} LanguageChoices;
static void loadLanguages(void *context)
{
    LanguageChoices *choices = context;
    choices->count = hddInstallLanguageChoices(choices->install, choices->languages, HDD_INSTALL_LANGUAGES);
}
static void loadManifest(void *context)
{
    HddInstall *in = context;
    if (hddInstallResolve(in) < 0)
        in->download_bytes = 0;
}
#endif
int dashHddInstallPrepare(HddInstall *in)
{
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
    if (!in->flags) return 1;
    if (!dashWorkerAwaitAll("Preparing installer")) return 0;
    if (!dashHttpsCaPath(in->ca_path, sizeof(in->ca_path)) || hddInstallLoadCa(in) < 0) {
        dashModalMessage(DI_WARNING, "Installer needs the CA bundle",
            "WLE-UPDATE-CA.PEM could not be read beside Launchstation or in its settings folder.", NULL);
        return 0;
    }
    hddInstallLog("Preparing network stack");
    if (!ensureDashboardIpNetworkReady()) {
        dashModalMessage(DI_WARNING, "Internet unavailable", dashboardIpNetworkError(), NULL);
        hddInstallRelease(in); return 0;
    }
    if (in->flags & HDD_INSTALL_PSBBN) {
        LanguageChoices choices = {.install = in, .count = -EIO};
        const char *labels[HDD_INSTALL_LANGUAGES];
        int initial = 0, selected;
        char title[128];
        /* Network setup can reset audsrv. Serialize its restart before the
         * language job so navigation audio is ready for the remaining choices. */
        dashSoundMaintain();
        int id = dashWorkerSubmit("Reading PSBBN languages", DASH_JOB_VISIBLE, loadLanguages, NULL, &choices);
        if (id < 0 || !dashHddInstallAwait(id, "Reading PSBBN languages") || choices.count < 1) {
            dashModalMessage(DI_WARNING, "PSBBN languages unavailable", in->error[0] ? in->error : "Could not read the repository language choices. The HDD has not been changed.", NULL);
            hddInstallRelease(in); return 0;
        }
        for (int i = 0; i < choices.count; i++) {
            labels[i] = choices.languages[i].name;
            if (!strcmp(choices.languages[i].code, "eng")) initial = i;
        }
        selected = dashModalSelect(dashHddInstallChoiceTitle(in, HDD_CHOICE_LANGUAGE, "PSBBN language", title, sizeof(title)), labels, choices.count, initial);
        if (selected < 0) { hddInstallRelease(in); return -ECANCELED; }
        in->language = choices.languages[selected];
        hddInstallLog("PSBBN language: %s (%s); repository %.7s", in->language.name, in->language.code, in->commit);
    }
    {
        int id = dashWorkerSubmit("Checking installer files", DASH_JOB_VISIBLE, loadManifest, NULL, in);
        if (id < 0 || !dashHddInstallAwait(id, "Checking installer files") || !in->download_bytes) {
            dashModalMessage(DI_WARNING, "Installer files unavailable",
                in->error[0] ? in->error : "Could not verify the installer download list. The HDD has not been changed.", NULL);
            hddInstallRelease(in);
            return 0;
        }
    }
    hddInstallOverall(1, 1);
    return 1;
#else
    (void)in;
    return 0;
#endif
}

int dashHddInstallPrepareStorage(HddInstall *in)
{
#if defined(ETH) && defined(WLE_NATIVE_CLOUD)
    /* Formatting resets the IOP. Restore network/storage before the worker
     * mounts PFS or opens ata0; no external staging device is needed. */
    hddInstallLog("Preparing network stack");
    if (!ensureDashboardIpNetworkReady()) {
        snprintf(in->error, sizeof(in->error), "%s", dashboardIpNetworkError());
        return -EIO;
    }
    hddInstallLog("Loading HDD/PFS modules");
    if (!loadHddModules()) return -ENODEV;
#ifdef EXFAT
    hddInstallLog("Loading ATA filesystem modules (jail=%d)", in->jail);
    if (in->jail && !loadAtaModules()) return -ENODEV;
#endif
    if (!in->stage[0])
        snprintf(in->stage, sizeof(in->stage), "%s/PSBBN_DP_TEMP", in->jail ? "ata0:" : "pfs2:");
    return 0;
#else
    (void)in;
    return -ENOSYS;
#endif
}

/* Installation log. The loading-view callback only draws; controller
 * polling belongs to the normal wait/review loops, never RPC wait frames. */
static DashLoadingView debug_previous_view;
static int debug_open, debug_review, debug_result, debug_rows = 8;
static int debug_top = -1;

static void debugInput(const DashInput *in)
{
    HddInstallLogSnapshot snapshot;
    int top, tail;
    hddInstallLogSnapshot(&snapshot);
    tail = (int)snapshot.next - debug_rows;
    if (tail < (int)snapshot.first) tail = snapshot.first;
    top = debug_top < 0 ? tail : debug_top;
    if (in->pressed & DASH_IN_UP) top--;
    if (in->pressed & DASH_IN_DOWN) top++;
    if (in->pressed & DASH_IN_LEFT) top -= debug_rows;
    if (in->pressed & DASH_IN_RIGHT) top += debug_rows;
    if (top < (int)snapshot.first) top = snapshot.first;
    debug_top = top >= tail || (in->pressed & DASH_IN_FAV) ? -1 : top;
}
static void debugDraw(const char *title)
{
    HddInstallLogSnapshot snapshot;
    const float x = 20, y = 20, w = DASH_VW - 40, h = DASH_VH - 40;
    const float log_y = y + 178, hints_y = y + h - 50;
    char line[HDD_INSTALL_LOG_COLS], counters[160];
    unsigned first, tail;
    float hx;
    hddInstallLogSnapshot(&snapshot);
    debug_rows = (int)((hints_y - 10 - log_y) / 24);
    if (debug_rows < 1) debug_rows = 1;
    tail = snapshot.next > (unsigned)debug_rows ? snapshot.next - debug_rows : 0;
    if (tail < snapshot.first) tail = snapshot.first;
    if (debug_top >= 0 && debug_top < (int)snapshot.first) debug_top = snapshot.first;
    if (debug_top >= 0 && debug_top > (int)tail) debug_top = tail;
    first = debug_top < 0 ? tail : (unsigned)debug_top;
    dashRenderScene();
    dashModalBackdrop(1);
    dashModalPanel(x, y, w, h, 1);
    dashIcon(debug_review ? (debug_result < 0 ? DI_WARNING : DI_CHECK) : DI_HDD,
             x + 20, y + 18, 22, debug_result < 0 && debug_review ? DASH_C_DANGER : DASH_C_ACCENT);
    dashTextFit(x + 52, y + 18, w - 90, title, DASH_FS_LABEL, DASH_C_TEXT);
    if (!debug_review) dashDrawSpinner(x + w - 24, y + 29, 7, DASH_C_ACCENT);
    snprintf(counters, sizeof(counters), "Overall (estimated): %u%% | %s", snapshot.overall / 10, snapshot.phase);
    dashTextFit(x + 20, y + 48, w - 40, counters, DASH_FS_SMALL, DASH_C_TEXT2);
    dashRect(x + 20, y + 68, w - 40, 5, dashColorA(DASH_C_TEXT3, 0.3f));
    dashRect(x + 20, y + 68, (w - 40) * dashClampf(snapshot.overall / 1000.0f, 0, 1), 5, DASH_C_ACCENT);
    dashTextFit(x + 20, y + 82, w - 40, snapshot.status, DASH_FS_SMALL, DASH_C_TEXT2);
    dashRect(x + 20, y + 104, w - 40, 5, dashColorA(DASH_C_TEXT3, 0.3f));
    if (snapshot.total > 0)
        dashRect(x + 20, y + 104, (w - 40) * dashClampf(snapshot.done / (float)snapshot.total, 0, 1), 5, DASH_C_ACCENT);
    else if (!debug_review) {
        float part = (snapshot.elapsed % 1600) / 1600.0f;
        dashRect(x + 20 + (w - 40) * 0.8f * part, y + 104, (w - 40) * 0.2f, 5, DASH_C_ACCENT);
    }
    if (snapshot.total > 0)
        snprintf(counters, sizeof(counters), "Elapsed %u:%02u | Last update %u s ago | %d / %d",
                 (unsigned)(snapshot.elapsed / 60000), (unsigned)(snapshot.elapsed / 1000 % 60),
                 (unsigned)(snapshot.idle / 1000), snapshot.done, snapshot.total);
    else
        snprintf(counters, sizeof(counters), "Elapsed %u:%02u | Last update %u s ago | %s",
                 (unsigned)(snapshot.elapsed / 60000), (unsigned)(snapshot.elapsed / 1000 % 60),
                 (unsigned)(snapshot.idle / 1000), debug_review ? "Stopped" : "Working...");
    dashTextFit(x + 20, y + 116, w - 40, counters, DASH_FS_SMALL, DASH_C_TEXT2);
    if (snapshot.io[0]) dashTextFit(x + 20, y + 138, w - 40, snapshot.io, DASH_FS_SMALL, DASH_C_TEXT3);
    snprintf(counters, sizeof(counters), "Installation log: lines %u-%u of %u%s", first + 1,
             first + debug_rows < snapshot.next ? first + debug_rows : snapshot.next,
             snapshot.next, debug_top < 0 ? " (live)" : " (scrolling)");
    dashText(x + 20, y + 160, counters, DASH_FS_CAPTION, DASH_C_TEXT3);
    dashGfxScissorPush(x + 20, log_y, w - 54, debug_rows * 24);
    for (int row = 0; row < debug_rows && first + row < snapshot.next; row++) {
        if (!hddInstallLogRead(first + row, line)) continue;
        dashTextFit(x + 20, log_y + row * 24, w - 54, line, DASH_FS_SMALL,
                    strstr(line, "ERROR") || strstr(line, "FAILED") ? DASH_C_DANGER : DASH_C_TEXT);
    }
    dashGfxScissorPop();
    dashDrawScrollbar(x + w - 16, log_y, debug_rows * 24,
                      (float)(first - snapshot.first), debug_rows, snapshot.next - snapshot.first);
    hx = x + 20;
    hx += dashDrawButton(hx, hints_y, DI_BTN_UP, NULL);
    hx += dashDrawButton(hx, hints_y, DI_BTN_DOWN, NULL) + 4;
    hx = dashTextS(hx, dashButtonTextY(hints_y, DASH_FS_SMALL), "Log", DASH_FS_SMALL, DASH_C_TEXT2) + 16;
    hx += dashDrawButton(hx, hints_y, -1, "LEFT/RIGHT") + 4;
    hx = dashTextS(hx, dashButtonTextY(hints_y, DASH_FS_SMALL), "Page", DASH_FS_SMALL, DASH_C_TEXT2) + 16;
    hx += dashDrawButton(hx, hints_y, DI_BTN_SQUARE, NULL) + 4;
    hx = dashTextS(hx, dashButtonTextY(hints_y, DASH_FS_SMALL), "Latest", DASH_FS_SMALL, DASH_C_TEXT2) + 16;
    if (debug_review) dashModalHintRow(hx, hints_y, dashOkIcon(), "Close", -1, NULL);
    if (snapshot.disk_error)
        snprintf(counters, sizeof(counters), "Log write error %d; messages remain in RAM", snapshot.disk_error);
    else if (snapshot.path[0]) snprintf(counters, sizeof(counters), "Log: %.150s", snapshot.path);
    else snprintf(counters, sizeof(counters), "RAM log; file will be saved when the HDD temporary folder is ready");
    dashTextFit(x + 20, y + h - 22, w - 40, counters, DASH_FS_CAPTION, DASH_C_TEXT3);
    dashGfxPresent();
}
void dashHddInstallDebugBegin(HddInstall *in, const HddFormatPlan *plan)
{
    if (!(in->flags & HDD_INSTALL_PSBBN)) return;
    hddInstallLogStart();
    if (!hddInstallLogEnabled()) return;
    debug_top = -1; debug_open = 1; debug_review = debug_result = 0;
    debug_previous_view = dashLoadingSetView(debugDraw);
    hddInstallLog("Launchstation build %s", GIT_HASH);
    hddInstallLog("Layout kind=%u sectors=%u APA=%u reserve=%u MiB", (unsigned)plan->kind, (unsigned)plan->sectors, (unsigned)plan->apa_sectors, (unsigned)plan->reserve_mb);
    hddInstallLog("exFAT first=%u sectors=%u recovery=%u", (unsigned)plan->exfat_start, (unsigned)plan->exfat_sectors, (unsigned)plan->recovery_start);
    hddInstallLog("Choices: Music=%u MiB Contents=%u MiB PS2 Linux=%s Home=%u MiB Free APA reserve=%u MiB",
                  in->music_mb, in->contents_mb, in->flags & HDD_INSTALL_PS2LINUX ? "On" : "Off", in->linux_home_mb, in->reserved_mb);
    hddInstallLog("Game launchers: install OPL + NHDDL/Neutrino; default %s", hddInstallGameName(in->game_launcher));
    hddInstallStatus("Preparing installer; the HDD has not been erased yet", 0, 0);
    dashLoadingBegin("PSBBN DP installer log (Experimental)");
}
void dashHddInstallRecoveryFailed(void)
{
    /* No acknowledgement means old DMA buffers must remain pinned. Keep an
     * explicit terminal screen alive, with no more RPCs or automatic resets. */
    const char *reason = installerIopRecoveryError();
    hddInstallLog("ERROR IOP recovery failed: %s", reason);
    hddInstallProgress(reason, 0, 0);
    for (;;) dashLoadingFrame(reason, 0, 0);
}
int dashHddInstallAwait(int id, const char *title)
{
    int owns_loading = !dashLoadingActive();
    if (!debug_open && !hddInstallRecoveryActive()) return dashWorkerAwait(id, title, 0);
    if (owns_loading) dashLoadingBegin(title);
    while (!dashWorkerDone(id)) {
        DashInput in;
        if (hddInstallRecoveryResetRequested()) {
            hddInstallLog("RPC watchdog expired; resetting IOP before releasing old DMA buffers");
            hddInstallProgress("Recovering storage: resetting IOP", 0, 0);
            if (!resetInstallerIopTransport()) dashHddInstallRecoveryFailed();
            hddInstallRecoveryResetComplete();
        }
        dashWorkerPump();
        if (dashWorkerDone(id)) break;
        if (!hddInstallRecoveryFault()) { dashInputPoll(&in); if (debug_open) debugInput(&in); }
        if (!debug_open && !hddInstallRecoveryFault()) {
            char status[96]; int done, total;
            dashWorkerGetStatus(status, sizeof(status), &done, &total);
            dashLoadingFrame(status, done, total);
        } else dashLoadingFrame(NULL, 0, 0);
    }
    if (owns_loading) dashLoadingEnd();
    return 1;
}
static void debugSave(void *context)
{
    HddInstall *in = context;
    hddInstallProgress("Saving installation log", 0, 0);
    int ret = hddInstallMountStage(in);
    if (ret >= 0) { hddInstallLogFlush(in); ret = hddInstallUnmountStage(in); }
    if (ret < 0) hddInstallLog("ERROR reopening log volume: %d", ret);
}
void dashHddInstallDebugEnd(HddInstall *in, int result)
{
    if (!debug_open) return;
    int save_log = in->owned && in->stage_ready && result != HDD_INSTALL_ATA_TIMEOUT;
    /* MBR installation can reset the IOP just before this final save. Reload
     * storage on the UI thread so the ATA installation log is reachable again. */
    if (save_log && !loadHddModules()) save_log = 0;
#ifdef EXFAT
    if (save_log && in->jail && !loadAtaModules()) save_log = 0;
#endif
    if (!save_log && in->owned && in->stage_ready)
        hddInstallLog("Final log could not be saved; installation status follows in RAM");
    hddInstallLog("SESSION %s: result=%d", result == -ECANCELED ? "CANCELLED" : result < 0 ? "FAILED" : "COMPLETE", result);
    if (result < 0 && in->error[0]) hddInstallLog("ERROR detail: %s", in->error);
    hddInstallLogResult(result);
    if (save_log) {
        int id = dashWorkerSubmit("Saving installer log", 0, debugSave, NULL, in);
        if (id > 0) dashHddInstallAwait(id, "Saving installer log");
        else hddInstallLog("ERROR log-save job could not be queued");
    }
    if (result == 0) hddInstallPhase(HDD_INSTALL_PHASE_COMPLETE);
    hddInstallLogStop();
    dashLoadingEnd();
    debug_review = 1; debug_result = result; debug_top = -1;
    if (result != -ECANCELED) {
        dashInputFlush();
        for (;;) {
            DashInput input;
            dashInputPoll(&input);
            if (input.pressed & (DASH_IN_OK | DASH_IN_BACK)) break;
            debugInput(&input);
            dashVisualUpdate();
            debugDraw(result < 0 ? "PSBBN DP installer failed (Experimental)" : "PSBBN DP installer complete (Experimental)");
        }
    }
    dashLoadingSetView(debug_previous_view); debug_open = 0;
    dashInputFlush();
}
