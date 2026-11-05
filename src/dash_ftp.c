/* Exclusive FTP screen: status rendering and Stop; no dashboard storage work. */
#ifdef ETH
#include "dash_internal.h"
#include "dash_sound.h"
#include "ftp_session.h"
#include "init.h"
#include <delaythread.h>

static void ftpFrame(const LsFtpStatus *status, int stopping)
{
	char line[160];
	const float w = 490, h = 260, x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	dashVisualUpdate();
	dashGfxBeginFrame();
	dashBgDraw();
	dashModalBackdrop(1.0f);
	dashModalPanel(x, y, w, h, 1.0f);
	dashIcon(DI_NETWORK, x + 24, y + 22, 24, DASH_C_ACCENT);
	dashTextS(x + 60, y + 24, stopping ? "Stopping FTP" : "FTP server running", DASH_FS_LABEL, DASH_C_TEXT);
	snprintf(line, sizeof(line), "IP address: %s    Port: 21", ip);
	dashTextFit(x + 24, y + 65, w - 48, line, DASH_FS_SMALL, DASH_C_TEXT);
	snprintf(line, sizeof(line), "Connected clients: %d    Active transfers: %d", status->clients,
	         status->transfers);
	dashTextFit(x + 24, y + 93, w - 48, line, DASH_FS_SMALL, DASH_C_TEXT2);
	dashTextFit(x + 24, y + 125, w - 48,
	            status->operation[0] ? status->operation : "Waiting for a connection", DASH_FS_SMALL,
	            DASH_C_TEXT2);
	if (status->transfers) {
		u64 bytes = ((u64)status->bytes_high << 32) | status->bytes_low;
		snprintf(line, sizeof(line), "Transferred: %llu KB", (unsigned long long)(bytes / 1024));
		dashTextFit(x + 24, y + 150, w - 48, line, DASH_FS_SMALL, DASH_C_TEXT2);
	}
	dashTextFit(x + 24, y + 181, w - 48,
	            stopping ? "Closing files and connections..." : "Back returns to Launchstation with FTP running.",
	            DASH_FS_SMALL, DASH_C_TEXT3);
	if (!stopping)
		dashModalHintRow(x + 24, y + h - 42, dashOkIcon(), "Stop FTP", dashBackIcon(), "Keep running");
	dashGfxPresent();
}

/* The control RPC went unanswered. Only the user may decide that files still
 * open over FTP are expendable; otherwise the IOP keeps them open. */
static int ftpUnresponsive(const char *what)
{
	char body[200];
	snprintf(body, sizeof(body),
	         "FTP control did not respond to %s. Reset the IOP anyway? Files still open over FTP may be left incomplete.",
	         what);
	if (!dashModalConfirm("FTP Server", body, "Reset", 1)) {
		dashModalMessage(DI_WARNING, "FTP Server", "The IOP was not reset over open files. FTP stays loaded.", NULL);
		return 0;
	}
	ftpSessionAbandon();
	rebootIopAndReloadCoreStackSilent();
	return 1;
}

int DashFtpServer(void)
{
	LsFtpStatus status;
	const char *unanswered = NULL;
	int closing = 0, ok = 1;
	if (!dashWorkerAwaitAll("Preparing FTP Server"))
		return 0;
	filerRootMcPollIdle();
	dashLoadingBegin("Starting FTP Server");
	/* A STOP that timed out may have finished since: drop the stopped module and start again. */
	if (isFtpServerRunning() && ftpSessionQuery(&status) && status.state == LS_FTP_STOPPED)
		rebootIopAndReloadCoreStackSilent();
	loadNetModules();
	if (!isFtpNetworkReady()) {
		dashLoadingEnd();
		dashModalMessage(DI_WARNING, "FTP Server", dashboardIpNetworkError(), NULL);
		return 0;
	}
	memset(&status, 0, sizeof(status));
	dashInputFlush();
	while (isFtpNetworkReady()) {
		DashInput input;
		if (!ftpSessionQuery(&status)) {
			unanswered = "a status request";
			break;
		}
		if (status.state == LS_FTP_STOPPED) {
			rebootIopAndReloadCoreStackSilent();
			break;
		}
		ftpFrame(&status, 0);
		dashInputPoll(&input);
		/* Back returns with FTP running. Normal dashboard remote/ISO polling resumes. */
		if (input.pressed & DASH_IN_BACK)
			break;
		if (input.pressed & DASH_IN_OK) {
			ftpFrame(&status, 1);
			if (ftpSessionShutdown())
				rebootIopAndReloadCoreStackSilent();
			else if (ftpSessionQuery(&status))
				closing = 1; /* still answering: slow to close files, not stuck */
			else
				unanswered = "Stop";
			break;
		}
		if (dashFtpPollRemote())
			break;
		DelayThread(100000);
	}
	dashLoadingEnd();
	dashInputFlush();
	if (closing) {
		dashModalMessage(DI_WARNING, "FTP Server", "FTP is still closing files and connections. Stop it again in a moment.",
		                 NULL);
		ok = 0;
	} else if (unanswered) {
		ok = ftpUnresponsive(unanswered);
	}
	return ok;
}
#else
int DashFtpServer(void) { return 0; }
#endif
