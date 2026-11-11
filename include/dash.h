//--------------------------------------------------------------
//File name:   dash.h
//Public entry points of the wLaunchELF dashboard shell.
//--------------------------------------------------------------
#ifndef DASH_H
#define DASH_H

#include <tamtypes.h>
#include "main_actions.h"

/* Runs the dashboard shell. Returns only when the user asks for the classic
 * launch-key menu; everything else (launching, tools, the FileBrowser) is
 * dispatched through ExecuteMainAction() exactly as the classic menu does. */
void DashRun(const MainExecuteContext *ctx, int from_boot);

/* Non-zero when the dashboard should replace the classic menu at boot.
 * Reads the dashboard settings file on first use; defaults to enabled. */
int DashIsEnabled(void);

/* Non-zero while DashRun() is on the call stack (guards re-entry). */
int DashIsActive(void);
/* The HDD Manager (dash_hdd.c). Needs the dashboard running. */
void dashHddManager(void);
int DashFtpServer(void);
int dashFtpPollRemote(void);

/* Publish the next blocking storage operation before entering its RPC.
 * Worker callers only update status; UI callers draw an existing overlay. */
void DashStorageProbeStatus(const char *status);
/* Nonzero while the dashboard worker owns IO (classic mode: always 0, so
 * classic callers may do their own IO). UI-thread code must not do IO then. */
int dashWorkerIoBusy(void);
/* Main-thread file-operation progress. Returns zero in classic mode. */
int DashFileProgress(const char *action, const char *name, u64 remaining, u64 total);
void DashFileDeleteProgress(const char *name);
/* Runs one file-transfer step on the dashboard worker while the progress
 * overlay keeps drawing. Returns 1 once `run` has finished there, or 0
 * without calling it (classic mode, no overlay, worker unavailable): the
 * caller then runs it itself. */
int DashFileTransferChunk(void (*run)(void *arg), void *arg);
/* DelayThread(usec) that keeps an open dashboard progress popup moving
 * (idea: saildot4k). Never returns early; otherwise a plain DelayThread. */
void DashPause(u32 usec);

/* Loads IPCONFIG.DAT (cached afterwards) without violating worker IO
 * exclusion: direct when the worker is idle, queued and awaited while the
 * worker owns IO, skipped while the IOP is parked. Safe from any thread. */
void dashEnsureIpConfig(void);

/* Finish dashboard-owned IO/audio before a child ELF that deliberately
 * inherits the live IOP. Returns 0 if the user cancels the bounded wait. */
int DashPrepareKeepIopHandoff(const char *title);
/* One-second Confirm hold reverses the saved IOP mode for this launch only. */
int DashChooseElfIopMode(int configured);

/* Path of the MISC pseudo-command that opens the dashboard. */
const char *DashMiscCommandName(void);

/* Legacy-screen theming (dash_theme.c). */
int DashThemeLegacyEnabled(void);
void DashThemeSetLegacyEnabled(int enabled);
void DashThemeBackgroundRect(int x1, int y1, int x2, int y2);
void DashThemePopupRect(int x1, int y1, int x2, int y2);
void DashThemeFrame(int x1, int y1, int x2, int y2, u64 color);
void DashThemeChar(unsigned int c, int x, int y, u64 colour);
void DashThemeSelectionBar(int x1, int y1, int x2, int y2);

#endif
