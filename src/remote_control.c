//--------------------------------------------------------------
// File name: remote_control.c
// PC -> PS2 remote command inbox, compatible with oMrRexD's
// wLaunchELF_R3Z_FTP Command PS2 helper.
//--------------------------------------------------------------
#include "launchelf.h"

#define REMOTE_COMMAND_FILE "ata0:/PS2-COMANDO.TXT"
#define REMOTE_COMMAND_INTERVAL_MS 3000
#define REMOTE_COMMAND_MAX 1024

static u64 next_remote_poll;
static char remote_text[REMOTE_COMMAND_MAX];

static char *remoteNextLine(char **cursor)
{
	char *line, *end;

	if (cursor == NULL || *cursor == NULL || **cursor == '\0')
		return NULL;
	line = *cursor;
	end = strchr(line, '\n');
	if (end != NULL) {
		*end = '\0';
		*cursor = end + 1;
	} else {
		*cursor = line + strlen(line);
	}
	end = strchr(line, '\r');
	if (end != NULL)
		*end = '\0';
	return line;
}

/* Returns 1 with an action in runpath, 0 when no complete command is ready,
 * and -1 when a complete request was consumed but was invalid. */
int remoteCommandPoll(char *runpath, int runpath_size, char *message, int message_size)
{
#if defined(ETH) && defined(EXFAT)
	iox_stat_t st;
	char *cursor, *header, *command, *line;
	int fd, len, complete = 0;

	if (runpath == NULL || runpath_size <= 0 || message == NULL || message_size <= 0)
		return 0;
	runpath[0] = '\0';
	message[0] = '\0';

	if (Timer() < next_remote_poll)
		return 0;
	next_remote_poll = Timer() + REMOTE_COMMAND_INTERVAL_MS;

	if (fileXioGetStat(REMOTE_COMMAND_FILE, &st) < 0)
		return 0;

	fd = fileXioOpen(REMOTE_COMMAND_FILE, FIO_O_RDONLY);
	if (fd < 0)
		return 0;
	len = fileXioRead(fd, remote_text, sizeof(remote_text) - 1);
	fileXioClose(fd);
	if (len <= 0)
		return 0;
	remote_text[len] = '\0';

	/* Do not consume a request while FTP is still writing it. */
	cursor = remote_text;
	header = remoteNextLine(&cursor);
	command = remoteNextLine(&cursor);
	while ((line = remoteNextLine(&cursor)) != NULL) {
		if (!strcmp(line, "FIM")) {
			complete = 1;
			break;
		}
	}
	if (!complete)
		return 0;

	/* Complete requests are one-shot, including malformed ones. */
	fileXioRemove(REMOTE_COMMAND_FILE);

	if (header == NULL || strcmp(header, "PS2-COMANDO 1") || command == NULL || command[0] == '\0') {
		snprintf(message, message_size, "Invalid PC command request");
		return -1;
	}

	if (!strncmp(command, "EXECUTAR\t", 9) && command[9] != '\0') {
		snprintf(runpath, runpath_size, "%s", command + 9);
		return 1;
	}
	if (!strcmp(command, "MENU")) {
		snprintf(runpath, runpath_size, "%s", setting->Misc_OSDSYS);
		return 1;
	}
	if (!strcmp(command, "DESLIGAR")) {
		snprintf(runpath, runpath_size, "%s", setting->Misc_PS2PowerOff);
		return 1;
	}

	snprintf(message, message_size, "Unknown PC command: %.60s", command);
	return -1;
#else
	(void)runpath;
	(void)runpath_size;
	(void)message;
	(void)message_size;
	return 0;
#endif
}
//--------------------------------------------------------------
// End of file: remote_control.c
//--------------------------------------------------------------
