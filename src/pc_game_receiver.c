//--------------------------------------------------------------
// File name: pc_game_receiver.c
// MrRexD-compatible "Send Games to PS2" receiver.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "filer_shared.h"
#include "init.h"

#if defined(ETH) && defined(UDPFS) && defined(EXFAT)

#define PC_RECV_REQUEST "ata0:/PS2-RECEBER.TXT"
#define PC_RECV_RESULT "ata0:/PS2-RECEBER.RES"
#define PC_RECV_STAGE_DIR "ata0:/PS2-RECEBENDO"
#define PC_RECV_STAGE PC_RECV_STAGE_DIR "/"
#define PC_RECV_MAX_ITEMS 32
#define PC_RECV_INTERVAL_MS 3000
#define PC_RECV_BLOCK (256 * 1024)
#define PC_RECV_RETRIES 8

typedef struct
{
	char folder[4];
	u64 size;
	char name[MAX_NAME];
	const char *state;
	u64 written;
	int seconds;
} PcReceiveItem;

static PcReceiveItem items[PC_RECV_MAX_ITEMS];
static char request_text[8192];
static u8 transfer_block[PC_RECV_BLOCK] __attribute__((aligned(64)));
static u64 next_poll;

static int readRequest(int *count)
{
	char *line, *next, *field2, *field3, *p;
	int fd, len, n = 0, complete = 0;

	fd = fileXioOpen(PC_RECV_REQUEST, FIO_O_RDONLY);
	if (fd < 0)
		return 0;
	len = fileXioRead(fd, request_text, sizeof(request_text) - 1);
	fileXioClose(fd);
	if (len <= 0)
		return 0;
	request_text[len] = '\0';

	if (strncmp(request_text, "PS2-RECEBER 1", 13))
		return -1;
	for (line = strchr(request_text, '\n'); line != NULL; line = next) {
		line++;
		next = strchr(line, '\n');
		if (next != NULL)
			*next = '\0';
		if ((p = strchr(line, '\r')) != NULL)
			*p = '\0';
		if (!strcmp(line, "FIM")) {
			complete = 1;
			break;
		}
		if (line[0] == '\0')
			continue;
		if ((field2 = strchr(line, '\t')) == NULL)
			return -1;
		*field2++ = '\0';
		if ((field3 = strchr(field2, '\t')) == NULL)
			return -1;
		*field3++ = '\0';
		if ((strcmp(line, "DVD") && strcmp(line, "CD")) || n >= PC_RECV_MAX_ITEMS ||
		    field3[0] == '\0' || strlen(field3) >= MAX_NAME || strchr(field3, '/') || strchr(field3, '\\'))
			return -1;
		strcpy(items[n].folder, line);
		items[n].size = strtoull(field2, NULL, 10);
		if (items[n].size == 0)
			return -1;
		strcpy(items[n].name, field3);
		n++;
	}
	if (!complete)
		return 0;
	*count = n;
	return n > 0 ? 1 : -1;
}

static void writeResult(int count)
{
	char line[MAX_NAME + 64];
	int fd, i;

	fd = fileXioOpen(PC_RECV_RESULT, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, fileMode);
	if (fd < 0)
		return;
	strcpy(line, "PS2-RECEBER-RES 1\n");
	fileXioWrite(fd, line, strlen(line));
	for (i = 0; i < count; i++) {
		snprintf(line, sizeof(line), "%s\t%llu\t%d\t%s\n", items[i].state,
		         (unsigned long long)items[i].written, items[i].seconds, items[i].name);
		fileXioWrite(fd, line, strlen(line));
	}
	strcpy(line, "FIM\n");
	fileXioWrite(fd, line, strlen(line));
	fileXioClose(fd);
}

static u64 fileSize(const char *path, int *exists)
{
	iox_stat_t st;

	*exists = fileXioGetStat(path, &st) >= 0;
	return *exists ? (((u64)st.hisize << 32) | st.size) : 0;
}

static void showProgress(const PcReceiveItem *item, int index, int count, u64 pos, u64 started, int retries)
{
	char text[MAX_PATH];
	u64 ms = Timer() - started;
	unsigned int kbs = ms ? (unsigned int)((pos / 1024) * 1000 / ms) : 0;
	unsigned int left = kbs ? (unsigned int)(((item->size - pos) / 1024) / kbs) : 0;

	snprintf(text, sizeof(text),
	         "Receiving %d/%d: %.40s  %u%%  %u/%u MB  %u KB/s  ~%u min%s",
	         index + 1, count, item->name,
	         (unsigned int)(item->size ? pos * 100 / item->size : 0),
	         (unsigned int)(pos >> 20), (unsigned int)(item->size >> 20),
	         kbs, (left + 59) / 60, retries ? "  (resumed)" : "");
	drawMsg(text);
}

static int copyWithResume(PcReceiveItem *item, const char *source, const char *dest, int index, int count)
{
	static char zero;
	int in = -1, out, got, want, retries = 0;
	u64 pos = 0, started = Timer(), last_draw = 0;

	out = fileXioOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, fileMode);
	if (out < 0)
		return -1;

	/* Reserve the final size before streaming. This keeps cluster allocation
	 * out of the middle of a network/HDD transfer. */
	if (item->size > 0) {
		drawMsg("Receiving games from PC: reserving HDD space...");
		if (fileXioLseek64(out, (s64)item->size - 1, SEEK_SET) == (s64)item->size - 1)
			fileXioWrite(out, &zero, 1);
		fileXioLseek64(out, 0, SEEK_SET);
	}

	while (pos < item->size) {
		if (in < 0) {
			in = fileXioOpen(source, FIO_O_RDONLY);
			if (in >= 0 && pos > 0 && fileXioLseek64(in, (s64)pos, SEEK_SET) != (s64)pos) {
				fileXioClose(in);
				in = -1;
			}
		}

		want = (item->size - pos > PC_RECV_BLOCK) ? PC_RECV_BLOCK : (int)(item->size - pos);
		got = (in >= 0) ? fileXioRead(in, transfer_block, want) : -1;
		if (got <= 0) {
			if (in >= 0)
				fileXioClose(in);
			in = -1;
			fileXioClose(out);
			if (++retries > PC_RECV_RETRIES) {
				item->written = pos;
				return -1;
			}
			{
				char text[128];
				snprintf(text, sizeof(text), "Connection lost at %u MB; reconnecting (%d/%d)...",
				         (unsigned int)(pos >> 20), retries, PC_RECV_RETRIES);
				drawMsg(text);
			}
			reloadUdpfsModules();
			loadAtaModules();
			out = fileXioOpen(dest, FIO_O_WRONLY);
			if (out < 0 || fileXioLseek64(out, (s64)pos, SEEK_SET) != (s64)pos) {
				if (out >= 0)
					fileXioClose(out);
				item->written = pos;
				return -1;
			}
			continue;
		}

		if (fileXioWrite(out, transfer_block, got) != got) {
			fileXioClose(in);
			fileXioClose(out);
			item->written = pos;
			return -1;
		}
		pos += got;

		if (Timer() - last_draw >= 500) {
			last_draw = Timer();
			showProgress(item, index, count, pos, started, retries);
			if (readpad_noRepeat() && (new_pad & PAD_TRIANGLE) &&
			    ynDialog("Cancel this game transfer?") > 0) {
				fileXioClose(in);
				fileXioClose(out);
				item->written = pos;
				return 1;
			}
		}
	}
	if (in >= 0)
		fileXioClose(in);
	fileXioClose(out);
	item->written = pos;
	return 0;
}

static void receiveGames(int count, char *message, int message_size)
{
	char dest[MAX_PATH], stage[MAX_PATH], folder[MAX_PATH], text[MAX_PATH], source[MAX_PATH];
	int restore_mmce = 0, restore_mx4sio = 0;
	u64 started;
	int i, r, dd, exists, server = 0, stop = 0, received = 0;

#ifdef MMCE
	restore_mmce = isPathDeviceStackLoaded("mmce0:/");
#endif
#ifdef MX4SIO
	restore_mx4sio = isPathDeviceStackLoaded("mx4sio0:/");
#endif

	for (i = 0; i < count; i++) {
		items[i].state = "NAO_FEITO";
		items[i].written = 0;
		items[i].seconds = 0;
	}

	drawMsg("Receiving games from PC: switching network to UDPFS...");
	if (prepareTransferDeviceStacks("udpfs:/", PC_RECV_STAGE) == TRANSFER_STACK_READY) {
		for (i = 0; i < 5 && !server; i++) {
			if ((dd = fileXioDopen("udpfs:/")) >= 0) {
				fileXioDclose(dd);
				server = 1;
			} else
				DelayThread(2000000);
		}
	}

	if (!server) {
		for (i = 0; i < count; i++)
			items[i].state = "SEM_SERVIDOR";
	} else {
		fileXioMkdir(PC_RECV_STAGE_DIR, fileMode);
		for (i = 0; i < count && !stop; i++) {
			snprintf(dest, sizeof(dest), "ata0:/%.3s/%.255s", items[i].folder, items[i].name);
			fileSize(dest, &exists);
			if (exists) {
				items[i].state = "JA_EXISTIA";
				continue;
			}
			snprintf(text, sizeof(text), "Receiving %d/%d (%s): %.255s",
			         i + 1, count, items[i].folder, items[i].name);
			drawMsg(text);

			snprintf(stage, sizeof(stage), "%s%.255s", PC_RECV_STAGE, items[i].name);
			snprintf(source, sizeof(source), "udpfs:/%.255s", items[i].name);
			started = Timer();
			r = copyWithResume(&items[i], source, stage, i, count);
			items[i].seconds = (int)((Timer() - started) / 1000);

			fileSize(stage, &exists);
			if (r == 0 && exists && items[i].written == items[i].size) {
				snprintf(folder, sizeof(folder), "ata0:/%.3s", items[i].folder);
				fileXioMkdir(folder, fileMode);
				if (fileXioRename(stage, dest) >= 0) {
					items[i].state = "OK";
					received++;
				} else {
					items[i].state = "FICOU_EM_PS2-RECEBENDO";
				}
			} else {
				if (exists)
					fileXioRemove(stage);
				items[i].state = (r == 1) ? "CANCELADO" : "FALHOU";
				stop = 1;
			}
		}
		fileXioRmdir(PC_RECV_STAGE_DIR);
	}

	writeResult(count);

	/* The sender expects FTP to return so it can read PS2-RECEBER.RES.
	 * Re-establish the explicit remote-access stack and only restore SIO2
	 * storage that was active before the network switch. */
	drawMsg("Receiving games from PC: restoring FTP...");
	loadNetModules();
	loadAtaModules();
#ifdef MMCE
	if (restore_mmce)
		loadMmceModules();
#endif
#ifdef MX4SIO
	if (restore_mx4sio)
		loadMx4sioModules();
#endif

	if (!server)
		snprintf(message, message_size, "PC UDPFS server was not found; no games received");
	else
		snprintf(message, message_size, "PC game transfer finished: %d of %d received", received, count);
}

int pcGameReceiverPoll(char *message, int message_size)
{
	iox_stat_t st;
	int count = 0, r;

	if (message == NULL || message_size <= 0)
		return 0;
	message[0] = '\0';

	if (Timer() < next_poll)
		return 0;
	next_poll = Timer() + PC_RECV_INTERVAL_MS;
	if (fileXioGetStat(PC_RECV_REQUEST, &st) < 0)
		return 0;

	r = readRequest(&count);
	if (r == 0)
		return 0; /* FTP is still writing it. */

	/* A complete or malformed job is consumed once. */
	fileXioRemove(PC_RECV_REQUEST);
	if (r < 0) {
		snprintf(message, message_size, "Invalid PC game-transfer request");
		return -1;
	}

	dashLoadingBegin("Receiving games from PC");
	receiveGames(count, message, message_size);
	dashLoadingEnd();
	next_poll = Timer() + PC_RECV_INTERVAL_MS;
	return 1;
}

#else

int pcGameReceiverPoll(char *message, int message_size)
{
	(void)message;
	(void)message_size;
	return 0;
}

#endif
//--------------------------------------------------------------
// End of file: pc_game_receiver.c
//--------------------------------------------------------------
