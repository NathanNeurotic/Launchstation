/* SMB file server screen: shares both memory cards to a PC (\\<ip>\mc0, \\<ip>\mc1).
 * OK stops the server; Back returns with it running, as FTP does. Issue #249. */
#ifdef ETH
#include "dash_internal.h"
#include "init.h"
#include <delaythread.h>

static void smbFrame(int status, int stopping)
{
	char line[160];
	const float w = 490, h = 290, x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	dashVisualUpdate();
	dashGfxBeginFrame();
	dashBgDraw();
	dashModalBackdrop(1.0f);
	dashModalPanel(x, y, w, h, 1.0f);
	dashIcon(DI_NETWORK, x + 24, y + 22, 24, DASH_C_ACCENT);
	dashTextS(x + 60, y + 24, stopping ? "Stopping SMB" : (status & 2) ? "SMB server running" : "Starting SMB",
	          DASH_FS_LABEL, DASH_C_TEXT);
	snprintf(line, sizeof(line), "\\\\%s\\mc0    \\\\%s\\mc1", ip, ip);
	dashTextFit(x + 24, y + 65, w - 48, line, DASH_FS_SMALL, DASH_C_TEXT);
	dashTextFit(x + 24, y + 93, w - 48, "User name: ps2    Password: ps2", DASH_FS_SMALL, DASH_C_TEXT);
	snprintf(line, sizeof(line), "Connected clients: %d", status >> 2);
	dashTextFit(x + 24, y + 121, w - 48, line, DASH_FS_SMALL, DASH_C_TEXT2);
	dashTextFit(x + 24, y + 160, w - 48, "This login is the same on every PS2: use a network you trust.", DASH_FS_SMALL,
	            DASH_C_TEXT3);
	dashTextFit(x + 24, y + 188, w - 48,
	            stopping ? "Closing files and connections..." : "Back returns to Launchstation with SMB running.",
	            DASH_FS_SMALL, DASH_C_TEXT3);
	if (!stopping)
		dashModalHintRow(x + 24, y + h - 42, dashOkIcon(), "Stop SMB", dashBackIcon(), "Keep running");
	dashGfxPresent();
}

int DashSmbServer(void)
{
	int status;
	if (!dashWorkerAwaitAll("Preparing SMB Server"))
		return 0;
	dashLoadingBegin("Starting SMB Server");
	status = startSmbServer();
	dashLoadingEnd();
	if (status < 0) {
		dashModalMessage(DI_WARNING, "SMB Server",
		                 status == -ENETDOWN ? dashboardIpNetworkError() : "The SMB server could not be started.", NULL);
		return 0;
	}
	dashInputFlush();
	for (;;) {
		DashInput input;
		status = smbServerStatus();
		if (status <= 0) {
			/* The server thread ended by itself, usually because port 445 could not be opened. */
			char body[96];
			stopSmbServer();
			snprintf(body, sizeof(body), "The SMB server stopped (error %d).", status);
			dashModalMessage(DI_WARNING, "SMB Server", body, NULL);
			return 0;
		}
		smbFrame(status, 0);
		dashInputPoll(&input);
		if (input.pressed & DASH_IN_BACK)
			break;
		if (input.pressed & DASH_IN_OK) {
			smbFrame(status, 1);
			if (stopSmbServer() < 0)
				dashModalMessage(DI_WARNING, "SMB Server",
				                 "SMB is still closing files and connections. Stop it again in a moment.", NULL);
			break;
		}
		DelayThread(100000);
	}
	dashInputFlush();
	return 1;
}
#else
int DashSmbServer(void) { return 0; }
#endif
