/* FTP status and cooperative shutdown, independently of blocked data sockets. */
#include "FtpControl.h"
#include "FtpServer.h"
#include "irx_imports.h"
#ifndef FTP_CONTROL_TEST
#include <sifrpc.h>
#endif
#include <errno.h>
static FtpServer *server;
static volatile int state, last_error;
static LsFtpCommand history[LS_FTP_HISTORY_COUNT];
static LsFtpStatus reply __attribute__((aligned(64)));
int FtpControl_Stopping(void)
{
	return state != LS_FTP_RUNNING;
}
static unsigned char bcd(unsigned char v)
{
	return (v >> 4) * 10 + (v & 15);
}
void FtpControl_Operation(const char *text)
{
	LsFtpCommand entry;
	sceCdCLOCK clock;
	int intr;
	memset(&entry, 0, sizeof(entry));
	strncpy(entry.text, text, sizeof(entry.text) - 1);
	/* The clock read can yield: take it before the short critical section. */
	if (sceCdReadClock(&clock) && !clock.stat) {
		entry.year = bcd(clock.year);
		entry.month = bcd(clock.month & 0x1f);
		entry.day = bcd(clock.day);
		entry.hour = bcd(clock.hour);
		entry.minute = bcd(clock.minute);
		entry.second = bcd(clock.second);
	}
	CpuSuspendIntr(&intr);
	memmove(&history[1], &history[0], sizeof(history) - sizeof(history[0]));
	history[0] = entry;
	CpuResumeIntr(intr);
}
void *FtpControl_Exchange(int command, void *data, int size)
{
	int intr;
	CpuSuspendIntr(&intr);
	if (command == LS_FTP_STOP && state == LS_FTP_RUNNING)
		state = LS_FTP_STOPPING;
	(void)data;
	(void)size;
	memset(&reply, 0, sizeof(reply));
	reply.version = LS_FTP_PROTOCOL_VERSION;
	reply.state = state;
	reply.error = last_error;
	memcpy(reply.history, history, sizeof(history));
	if (server)
		for (int i = 0; i < MAX_CLIENTS; i++) {
			FtpClient *client = &server->m_kClientArray[i];
			int j;
			if (!client->m_pServer)
				continue;
			for (j = 0; j < i; j++) /* one machine may open several connections */
				if (server->m_kClientArray[j].m_pServer &&
				    server->m_kClientArray[j].m_uiPeerAddress == client->m_uiPeerAddress)
					break;
			reply.clients += j == i;
			if (client->m_eDataMode != DATAMODE_IDLE) {
				u64 bytes = ((u64)reply.bytes_high << 32) | reply.bytes_low;
				reply.transfers++;
				bytes += client->m_uiDataOffset;
				reply.bytes_low = (unsigned int)bytes;
				reply.bytes_high = (unsigned int)(bytes >> 32);
			}
		}
	CpuResumeIntr(intr);
	/* Socket APIs may yield; never call them with interrupts disabled. Only
	 * the FTP owner closes files/clients, after the current driver call. */
	if (command == LS_FTP_STOP && server)
		for (int i = 0; i < MAX_CLIENTS; i++) {
			FtpClient *client = &server->m_kClientArray[i];
			if (!client->m_pServer)
				continue;
			if (client->m_iControlSocket >= 0)
				lwip_shutdown(client->m_iControlSocket, 2);
			if (client->m_iDataSocket >= 0)
				lwip_shutdown(client->m_iDataSocket, 2);
		}
	return &reply;
}
void FtpControl_Stopped(void)
{
	state = LS_FTP_STOPPED;
}
void FtpControl_Error(int error)
{
	if (error < 0)
		last_error = error;
}
#ifndef FTP_CONTROL_TEST
static SifRpcDataQueue_t queue;
static SifRpcServerData_t rpc;
static unsigned char rpc_buffer[sizeof(LsFtpStatus)] __attribute__((aligned(64)));
static volatile int registered;
static void control_thread(void *arg)
{
	(void)arg;
	sceSifSetRpcQueue(&queue, GetThreadId());
	sceSifRegisterRpc(&rpc, LS_FTP_RPC_ID, FtpControl_Exchange, rpc_buffer, NULL, NULL, &queue);
	registered = 1;
	sceSifRpcLoop(&queue);
}
#endif
int FtpControl_Init(FtpServer *owner)
{
	server = owner;
	state = LS_FTP_RUNNING;
	last_error = 0;
	memset(history, 0, sizeof(history));
#ifndef FTP_CONTROL_TEST
	iop_thread_t thread;
	int id;
	memset(&thread, 0, sizeof(thread));
	thread.attr = TH_C;
	thread.thread = control_thread;
	thread.stacksize = 0x1000;
	thread.priority = 0x42;
	id = CreateThread(&thread);
	if (id < 0)
		return id;
	if (StartThread(id, NULL) < 0) {
		DeleteThread(id);
		return -EIO;
	}
	/* The EE binds as soon as the module reports resident. */
	while (!registered)
		DelayThread(1000);
#endif
	return 0;
}
