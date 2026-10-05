//--------------------------------------------------------------
// File name: mmce_control.c
// EE-side wrappers for sd2psXtd MMCE control commands.
//--------------------------------------------------------------
#include "launchelf.h"
#include "mmce_cmds.h"
#include "mmce_control.h"

static int validUnit(int unit)
{
	return unit == 0 || unit == 1;
}

static void rootForUnit(int unit, char root[8])
{
	snprintf(root, 8, "mmce%d:", unit);
}

int mmceControlProbe(int unit)
{
	char root[8];

	if (!validUnit(unit))
		return -1;
	rootForUnit(unit, root);
	return fileXioDevctl(root, MMCE_CMD_PING, NULL, 0, NULL, 0);
}

int mmceControlGetStatus(int unit)
{
	char root[8];

	if (!validUnit(unit))
		return -1;
	rootForUnit(unit, root);
	return fileXioDevctl(root, MMCE_CMD_GET_STATUS, NULL, 0, NULL, 0);
}

int mmceControlGetCard(int unit)
{
	char root[8];

	if (!validUnit(unit))
		return -1;
	rootForUnit(unit, root);
	return fileXioDevctl(root, MMCE_CMD_GET_CARD, NULL, 0, NULL, 0);
}

int mmceControlGetChannel(int unit)
{
	char root[8];

	if (!validUnit(unit))
		return -1;
	rootForUnit(unit, root);
	return fileXioDevctl(root, MMCE_CMD_GET_CHANNEL, NULL, 0, NULL, 0);
}

int mmceControlGetGameId(int unit, char *out, size_t out_size)
{
	char root[8];
	char gameid[256];
	int ret;

	if (!validUnit(unit) || out == NULL || out_size == 0)
		return -1;
	out[0] = '\0';
	memset(gameid, 0, sizeof(gameid));
	rootForUnit(unit, root);
	ret = fileXioDevctl(root, MMCE_CMD_GET_GAMEID, NULL, 0, gameid, sizeof(gameid));
	if (ret < 0)
		return ret;
	gameid[sizeof(gameid) - 1] = '\0';
	snprintf(out, out_size, "%s", gameid);
	return 0;
}

int mmceControlWaitIdle(int unit, int attempts, int delay_ms)
{
	int i, status;

	if (!validUnit(unit) || attempts <= 0)
		return -1;
	if (delay_ms < 0)
		delay_ms = 0;

	for (i = 0; i < attempts; i++) {
		status = mmceControlGetStatus(unit);
		if (status >= 0 && (status & 1) == 0)
			return 0;
		if (status < 0)
			return -1;
		if (delay_ms)
			DelayThread(delay_ms * 1000);
	}
	return -1;
}

int mmceControlRestoreFromBootCard(int unit)
{
	char root[8];
	int card, ret;

	if (!validUnit(unit) || mmceControlProbe(unit) < 0)
		return -1;
	if (mmceControlWaitIdle(unit, 30, 250) < 0)
		return -1;

	card = mmceControlGetCard(unit);
	if (card < 0)
		return -1;
	if (card != 0)
		return 0;

	rootForUnit(unit, root);
	ret = fileXioDevctl(root, MMCE_CMD_UNMOUNT_BOOTCARD, NULL, 0, NULL, 0);
	if (ret < 0)
		return -1;
	if (mmceControlWaitIdle(unit, 30, 250) < 0)
		return -1;
	return 1;
}
