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
