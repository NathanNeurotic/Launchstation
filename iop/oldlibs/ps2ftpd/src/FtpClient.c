/*
 * FtpClient.c - FTP client methods
 *
 * Copyright (C) 2004 Jesper Svennevid
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "FtpClient.h"
#include "FtpServer.h"
#include "FtpMessages.h"

#ifndef LINUX
#include "irx_imports.h"
#define assert(x)
#else
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define disconnect(s) close(s)
#endif

#include <errno.h>

#define MAX_CLIENTS 4

// cheap mans itoa
char *itoa(char *in, int val)
{
	char tbuf[16];
	char *t = tbuf;

	do {
		*t++ = (val % 10) + '0';
		val /= 10;
	} while (val > 0);

	while (t > tbuf)
		*in++ = *--t;

	*in = 0;

	return in;
}

// cheap mans larger uitoa
char *uitoa(char *in, u64 val)
{
	char digits[20];
	int count = 0, index;
	do {
		digits[count++] = '0' + val % 10;
		val /= 10;
	} while (val);
	for (index = 0; index < count; index++)
		in[index] = digits[count - index - 1];
	in[count] = '\0';
	return in;
}


// this shared buffer is used for everything the clients do
static char buffer[32768];  // fewer round trips per transfer on slow devices such as mmce
/* Sockets are blocking. Finish each shared-buffer send before another client runs. */
static int FtpClient_SendAll(int socket, const char *data, int length)
{
	int offset = 0;
	while (offset < length) {
		int result = send(socket, data + offset, length - offset, 0);
		if (result <= 0 || result > length - offset)
			return -1;
		offset += result;
	}
	return 0;
}

void FtpClient_Create(FtpClient *pClient, FtpServer *pServer, int iControlSocket)
{
	assert(pClient && pServer);

	memset(pClient, 0, sizeof(FtpClient));

	pClient->m_pServer = pServer;
	pClient->m_iControlSocket = iControlSocket;
	pClient->m_eAuthState = AUTHSTATE_INVALID;
	pClient->m_iAuthAttempt = 0;

	pClient->m_iDataSocket = -1;
	pClient->m_uiDataBufferSize = 0;
	pClient->m_eDataMode = DATAMODE_IDLE;
	pClient->m_eDataAction = DATAACTION_NONE;
	pClient->m_eConnState = CONNSTATE_IDLE;
	pClient->m_iRemotePort = -1;
	pClient->m_iRemoteAddress[0] = 0;
	pClient->m_iRemoteAddress[1] = 0;
	pClient->m_iRemoteAddress[2] = 0;
	pClient->m_iRemoteAddress[3] = 0;
	pClient->m_uiDataOffset = 0;
	pClient->m_iRestartMarker = 0;

	pClient->m_kContainer.m_pClient = pClient;
	pClient->m_kContainer.m_pPrev = &pClient->m_kContainer;
	pClient->m_kContainer.m_pNext = &pClient->m_kContainer;

	pClient->m_iCommandOffset = 0;
	pClient->m_CommandBuffer[0] = '\0';

	FileSystem_Create(&pClient->m_kContext);

	pClient->m_pMessages = FtpMessage;
}

void FtpClient_Destroy(FtpClient *pClient)
{
	assert(pClient);

	FtpClient_OnDataCleanup(pClient);
	FileSystem_Destroy(&pClient->m_kContext);

	if (-1 != pClient->m_iControlSocket) {
		disconnect(pClient->m_iControlSocket);
		pClient->m_iControlSocket = -1;
	}

	pClient->m_pServer = NULL;
}

void FtpClient_Send(FtpClient *pClient, int iReturnCode, const char *pString)
{
	char reply[FS_PATH_MAX + 128];
	/* Prefix, bounded message and CRLF fit even on the IOP sprintf API. */
	int length = sprintf(reply, "%d %.516s\r\n", iReturnCode, pString);
	if (length < 0 || length >= (int)sizeof(reply) ||
		FtpClient_SendAll(pClient->m_iControlSocket, reply, length) < 0) {
		if (pClient->m_pServer)
			FtpServer_OnClientDisconnect(pClient->m_pServer, pClient);
	}
}


void FtpClient_OnConnect(FtpClient *pClient)
{
	FtpClient_Send(pClient, 220, pClient->m_pMessages[FTPMSG_SERVER_READY]);
}

void FtpClient_OnCommand(FtpClient *pClient, const char *pString)
{
	char *c;

	assert(pClient);

	strcpy(buffer, pString);

	c = strtok(buffer, " ");

	if (c) {
		int i;
		unsigned int result = 0;

		for (i = 0; *c && i < 4; i++)
			result = (result << 8) | tolower(*(c++));

		if (result != FTPCMD_RNTO && result != FTPCMD_RNFR)
			pClient->m_Rename.path[0] = '\0';
		if (pClient->m_eDataMode != DATAMODE_IDLE && result != FTPCMD_ABOR &&
		    result != FTPCMD_QUIT && result != FTPCMD_NOOP) {
			FtpClient_Send(pClient, 450, "A data transfer is in progress.");
			return;
		}

		// if user has not logged in, we only allow a small subset of commands

		if (pClient->m_eAuthState != AUTHSTATE_VALID) {
			switch (result) {
				// USER <name>
				case FTPCMD_USER: {
					char *user = strtok(NULL, "");

					if (user)
						FtpClient_OnCmdUser(pClient, user);
					else
						FtpClient_Send(pClient, 500, pClient->m_pMessages[FTPMSG_REQUIRES_PARAMETERS]);
				} break;

				// PASS <password>
				case FTPCMD_PASS: {
					char *pass = strtok(NULL, "");

					if (pass)
						FtpClient_OnCmdPass(pClient, pass);
					else
						FtpClient_Send(pClient, 530, pClient->m_pMessages[FTPMSG_LOGIN_INCORRECT]);
				} break;

				// QUIT
				case FTPCMD_QUIT: {
					FtpClient_OnCmdQuit(pClient);
				} break;

				case FTPCMD_NOOP:
					FtpClient_Send(pClient, 200, pClient->m_pMessages[FTPMSG_COMMAND_SUCCESSFUL]);
					break;
			}

			return;
		}

		switch (result) {
			case FTPCMD_USER:
			case FTPCMD_PASS: {
				FtpClient_Send(pClient, 503, pClient->m_pMessages[FTPMSG_ALREADY_AUTHENTICATED]);
			} break;

			case FTPCMD_ABOR:
				FtpClient_OnDataCleanup(pClient);
				FtpClient_Send(pClient, 226, pClient->m_pMessages[FTPMSG_COMMAND_SUCCESSFUL]);
				break;

			// PASV
			case FTPCMD_PASV: {
				FtpClient_OnCmdPasv(pClient);
			} break;

			// PORT a0,a1,a2,a3,p0,p1
			case FTPCMD_PORT: {
				int i;
				int ip[4];
				int port = 0;

				for (i = 0; i < 6; i++) {
					char *val = strtok(NULL, ",");

					if (!val)
						break;

					if (i >= 0 && i < 4) {
						ip[i] = strtol(val, NULL, 10);
					} else if (4 == i) {
						port = strtol(val, NULL, 10) * 256;
					} else if (5 == i) {
						port += strtol(val, NULL, 10);
					}
				}

				if (6 == i)
					FtpClient_OnCmdPort(pClient, ip, port);
				else
					FtpClient_Send(pClient, 501, pClient->m_pMessages[FTPMSG_ILLEGAL_COMMAND]);
			} break;

			// QUIT
			case FTPCMD_QUIT: {
				FtpClient_OnCmdQuit(pClient);
			} break;

			// SYST
			case FTPCMD_SYST: {
				FtpClient_OnCmdSyst(pClient);
			} break;

			// LIST
			case FTPCMD_LIST: {
				char *path = strtok(NULL, "");

				if (path)
					FtpClient_OnCmdList(pClient, path, 0);
				else
					FtpClient_OnCmdList(pClient, "", 0);
			} break;

			// NLST
			case FTPCMD_NLST: {
				char *path = strtok(NULL, "");

				if (path)
					FtpClient_OnCmdList(pClient, path, 1);
				else
					FtpClient_OnCmdList(pClient, "", 1);
			} break;

			// these all share the same setup

			case FTPCMD_TYPE:
			case FTPCMD_RETR:
			case FTPCMD_STOR:
			case FTPCMD_APPE:
			case FTPCMD_CWD:
			case FTPCMD_XCWD:
			case FTPCMD_DELE:
			case FTPCMD_MKD:
			case FTPCMD_XMKD:
			case FTPCMD_RMD:
			case FTPCMD_XRMD:
			case FTPCMD_RNFR:
			case FTPCMD_RNTO:
			case FTPCMD_SITE:
			case FTPCMD_MODE:
			case FTPCMD_STRU:
			case FTPCMD_SIZE: {
				char *arg = strtok(NULL, "");

				if (arg) {
					switch (result) {
						case FTPCMD_TYPE:
							FtpClient_OnCmdType(pClient, arg);
							break;
						case FTPCMD_RETR:
							FtpClient_OnCmdRetr(pClient, arg);
							break;
						case FTPCMD_STOR:
							FtpClient_OnCmdStor(pClient, arg);
							break;
						case FTPCMD_APPE:
							FtpClient_OnCmdAppe(pClient, arg);
							break;
						case FTPCMD_CWD:
						case FTPCMD_XCWD:
							FtpClient_OnCmdCwd(pClient, arg);
							break;
						case FTPCMD_DELE:
							FtpClient_OnCmdDele(pClient, arg);
							break;
						case FTPCMD_MKD:
						case FTPCMD_XMKD:
							FtpClient_OnCmdMkd(pClient, arg);
							break;
						case FTPCMD_RMD:
						case FTPCMD_XRMD:
							FtpClient_OnCmdRmd(pClient, arg);
							break;
						case FTPCMD_RNFR:
							FtpClient_OnCmdRnfr(pClient, arg);
							break;
						case FTPCMD_RNTO:
							FtpClient_OnCmdRnto(pClient, arg);
							break;
						case FTPCMD_SITE:
							FtpClient_OnCmdSite(pClient, arg);
							break;
						case FTPCMD_MODE:
							FtpClient_OnCmdMode(pClient, arg);
							break;
						case FTPCMD_STRU:
							FtpClient_OnCmdStru(pClient, arg);
							break;
						case FTPCMD_SIZE:
							FtpClient_OnCmdSize(pClient, arg);
							break;
					}
				} else
					FtpClient_Send(pClient, 501, pClient->m_pMessages[FTPMSG_REQUIRES_PARAMETERS]);
			} break;
			// TYPE <type>

			case FTPCMD_PWD:
			case FTPCMD_XPWD: {
				FtpClient_OnCmdPwd(pClient);
			} break;


			case FTPCMD_CDUP: {
				FtpClient_OnCmdCwd(pClient, "..");
			} break;


			case FTPCMD_REST: {
				char *marker = strtok(NULL, "");

				if (marker) {
					char *c = marker;
					while (*c) {
						if ((*c < '0') || (*c > '9'))
							break;
						c++;
					}

					if (!*c) {
						// 64-bit parse (strtol stops at 2 GB); absurd values are refused like bad ones
						s64 iMarker = 0;

						for (c = marker; *c; c++) {
							if (iMarker > 0x0CCCCCCCCCCCCCCCLL) {
								iMarker = -1;
								break;
							}
							iMarker = iMarker * 10 + (*c - '0');
						}
						FtpClient_OnCmdRest(pClient, iMarker);
					} else
						FtpClient_OnCmdRest(pClient, -1);  // used to send no reply at all
				} else
					FtpClient_Send(pClient, 500, pClient->m_pMessages[FTPMSG_REQUIRES_PARAMETERS]);
			} break;

			case FTPCMD_NOOP:
				FtpClient_Send(pClient, 200, pClient->m_pMessages[FTPMSG_COMMAND_SUCCESSFUL]);
				break;

			default:
				FtpClient_Send(pClient, 500, pClient->m_pMessages[FTPMSG_NOT_UNDERSTOOD]);
				break;
		}
	} else
		FtpClient_Send(pClient, 500, pClient->m_pMessages[FTPMSG_NOT_UNDERSTOOD]);
}

void FtpClient_OnDataConnect(FtpClient *pClient, int *ip, int port)
{
	int s;
	struct sockaddr_in sa;

	assert(pClient);

	// close any present socket to avoid resource leakage

	if (-1 != pClient->m_iDataSocket) {
		disconnect(pClient->m_iDataSocket);
		pClient->m_iDataSocket = -1;
	}

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
#ifdef DEBUG
		printf("ps2ftpd: failed to create data socket\n");
#endif
		FtpClient_OnDataFailed(pClient, NULL);
		return;
	}

	pClient->m_iDataSocket = s;

	// attempt to enter non-blocking mode
	/*
	FIXME: non-blocking mode - if this is not enabled, the entire server will
	stall when the connect is made. the ps2 does not seem to have capability
	in the lwip-stack to support non-blocking sockets yet though...

	if( fcntl( s, F_SETFL, O_NONBLOCK ) < 0 )
	{
		FtpClient_OnDataFailed(pClient,NULL);
		return;
	}
*/

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl((ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | (ip[3]));

	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		// if we get an error, see if it's a non-blocking connection

		unsigned int err;
		socklen_t sl = sizeof(err);

		if (getsockopt(pClient->m_iDataSocket, SOL_SOCKET, SO_ERROR, &err, &sl) || (EINPROGRESS != err)) {
			FtpClient_OnDataFailed(pClient, NULL);
			return;
		}

		pClient->m_eConnState = CONNSTATE_CONNECT;
	} else {
		pClient->m_iDataSocket = s;
		pClient->m_eConnState = CONNSTATE_RUNNING;
		FtpClient_OnDataConnected(pClient);
	}
}

void FtpClient_OnDataConnected(FtpClient *pClient)
{
	assert(pClient);

	switch (pClient->m_eDataAction) {
		case DATAACTION_LIST:
		case DATAACTION_NLST: {
			pClient->m_eDataMode = DATAMODE_WRITE;
			pClient->m_eConnState = CONNSTATE_RUNNING;

			FtpClient_Send(pClient, 150, pClient->m_pMessages[FTPMSG_OPENING_ASCII_CONNECTION]);
		} break;

		case DATAACTION_RETR: {
			pClient->m_eDataMode = DATAMODE_WRITE;
			pClient->m_eConnState = CONNSTATE_RUNNING;

			FtpClient_Send(pClient, 150, pClient->m_pMessages[FTPMSG_OPENING_BINARY_CONNECTION]);
		} break;

		case DATAACTION_APPE:
		case DATAACTION_STOR: {
			pClient->m_eDataMode = DATAMODE_READ;
			pClient->m_eConnState = CONNSTATE_RUNNING;

			FtpClient_Send(pClient, 150, pClient->m_pMessages[FTPMSG_OPENING_BINARY_CONNECTION]);
		} break;

		// something has gone wrong, but we have an open socket here, keep it for re-use (some clients support it)
		default: {
			pClient->m_eDataMode = DATAMODE_IDLE;
			pClient->m_eConnState = CONNSTATE_RUNNING;
		} break;
	}

	pClient->m_uiDataBufferSize = sizeof(buffer);
	pClient->m_uiDataOffset = 0;
}

void FtpClient_OnDataRead(FtpClient *pClient)
{
	int received, offset = 0;
	if (pClient->m_eDataAction != DATAACTION_STOR && pClient->m_eDataAction != DATAACTION_APPE) {
		FtpClient_OnDataFailed(pClient, NULL);
		return;
	}
	received = recv(pClient->m_iDataSocket, buffer, pClient->m_uiDataBufferSize, 0);
	if (received < 0) {
		FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_TRANSFER_FAILED]);
		return;
	}
	if (!received) {
		FtpClient_OnDataComplete(pClient, 0, NULL);
		return;
	}
	while (offset < received) {
		int written = FileSystem_WriteFile(&pClient->m_kContext, buffer + offset, received - offset);
		if (written <= 0 || written > received - offset) {
			FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_LOCAL_WRITE_FAILED]);
			return;
		}
		offset += written;
		pClient->m_uiDataOffset += written;
	}
}


void FtpClient_OnDataWrite(FtpClient *pClient)
{
	if (pClient->m_eDataAction == DATAACTION_RETR) {
		int received = FileSystem_ReadFile(&pClient->m_kContext, buffer, pClient->m_uiDataBufferSize);
		if (received < 0 || received > (int)pClient->m_uiDataBufferSize)
			FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_FAILED_READING_DATA]);
		else if (!received)
			FtpClient_OnDataComplete(pClient, 0, NULL);
		else if (FtpClient_SendAll(pClient->m_iDataSocket, buffer, received) < 0)
			FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_PREMATURE_CLIENT_DISCONNECT]);
		else
			pClient->m_uiDataOffset += received;
	} else if (pClient->m_eDataAction == DATAACTION_LIST || pClient->m_eDataAction == DATAACTION_NLST) {
		FSFileInfo info;
		char line[512];
		int result = FileSystem_ReadDir(&pClient->m_kContext, &info), length;
		if (result <= 0) {
			if (result < 0)
				FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_FAILED_READING_DATA]);
			else
				FtpClient_OnDataComplete(pClient, 0, NULL);
			return;
		}
		if (pClient->m_eDataAction == DATAACTION_NLST)
			length = sprintf(line, "%.255s\r\n", info.m_Name);
		else {
			static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
										   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
			char mode[11], size[21], stamp[48];
			int index, month = info.m_TS.m_iMonth;
			mode[0] = info.m_eType == FT_DIRECTORY ? 'd' : info.m_eType == FT_LINK ? 'l' : '-';
			for (index = 0; index < 9; index++)
				mode[index + 1] = info.m_iProtection & (1 << (8 - index)) ? "rwx"[index % 3] : '-';
			mode[10] = '\0';
			if (month < 1 || month > 12)
				month = 1;
			if (info.m_iDaysBetween >= 0 && info.m_iDaysBetween <= 182)
				sprintf(stamp, "%s %2d %02d:%02d", months[month - 1], info.m_TS.m_iDay, info.m_TS.m_iHour,
						info.m_TS.m_iMinute);
			else
				sprintf(stamp, "%s %2d  %04d", months[month - 1], info.m_TS.m_iDay, info.m_TS.m_iYear);
			uitoa(size, info.m_iSize);
			length = sprintf(line, "%s   1 ps2      ps2 %13s %s %.255s\r\n", mode, size, stamp, info.m_Name);
		}
		if (length < 0 || length >= (int)sizeof(line) ||
			FtpClient_SendAll(pClient->m_iDataSocket, line, length) < 0)
			FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_PREMATURE_CLIENT_DISCONNECT]);
	} else
		FtpClient_OnDataFailed(pClient, NULL);
}


void FtpClient_OnDataComplete(FtpClient *pClient, int iReturnCode, const char *pMessage)
{
	assert(pClient);

	int closed = FileSystem_Close(&pClient->m_kContext);
	if (closed < 0 && !iReturnCode) {
		iReturnCode = 451;
		pMessage = "Could not finish closing the transferred file.";
	}
	if (pMessage && iReturnCode > 0) {
		FtpClient_Send(pClient, iReturnCode, pMessage);
	} else {
		switch (pClient->m_eDataAction) {
		case DATAACTION_APPE:
		case DATAACTION_NLST:
		case DATAACTION_LIST:
		case DATAACTION_RETR:
		case DATAACTION_STOR:
			FtpClient_Send(pClient, 226, pClient->m_pMessages[FTPMSG_COMMAND_SUCCESSFUL]);
			break;

		default:
			break;
		}
	}

	// thanks to clients implemented incorrectly, we must close the connection after completed transfer
	FtpClient_OnDataCleanup(pClient);
}

void FtpClient_OnDataFailed(FtpClient *pClient, const char *pMessage)
{
	assert(pClient);

	pClient->m_eDataAction = DATAACTION_NONE;
	FtpClient_OnDataComplete(pClient, 500, pMessage ? pMessage : pClient->m_pMessages[FTPMSG_TRANSFER_FAILED]);
}

void FtpClient_OnDataCleanup(FtpClient *pClient)
{
	assert(pClient);

	FileSystem_Close(&pClient->m_kContext);

	if (-1 != pClient->m_iDataSocket) {
		disconnect(pClient->m_iDataSocket);
		pClient->m_iDataSocket = -1;
	}

	pClient->m_iRemotePort = -1;
	pClient->m_iRemoteAddress[0] = 0;
	pClient->m_iRemoteAddress[1] = 0;
	pClient->m_iRemoteAddress[2] = 0;
	pClient->m_iRemoteAddress[3] = 0;

	pClient->m_eConnState = CONNSTATE_IDLE;
	pClient->m_eDataMode = DATAMODE_IDLE;
	// a data connection accepted before its command arrives must not replay the previous transfer
	pClient->m_eDataAction = DATAACTION_NONE;
}

void FtpClient_HandleDataConnect(FtpClient *pClient)
{
	if (CONNSTATE_RUNNING == pClient->m_eConnState) {
		// we already have an active connection, use it
		FtpClient_OnDataConnected(pClient);
	} else {
		if (-1 != pClient->m_iRemotePort) {
			// connect to remote host
			FtpClient_OnDataConnect(pClient, pClient->m_iRemoteAddress, pClient->m_iRemotePort);
		} else if (CONNSTATE_LISTEN != pClient->m_eConnState) {
			// no connection mode has yet been chosen
			FtpClient_OnDataFailed(pClient, pClient->m_pMessages[FTPMSG_UNABLE_TO_BUILD_DATA_CONNECTION]);
		}
	}
}
