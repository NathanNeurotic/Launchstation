//---------------------------------------------------------------------------
// File name:   config_network.c
//---------------------------------------------------------------------------
#include "config_private.h"
#include <stdlib.h>

//---------------------------------------------------------------------------
// Network settings GUI by Slam-Tilt
//---------------------------------------------------------------------------
/* Write an immutable first line while preserving the existing tail.
 * A failed read must not turn a settings update into a truncated file. */
int configWriteNetworkSnapshot(const char *target_path, const char *firstline)
{
	char path[MAX_PATH];
	char *old = NULL;
	int fd, size = 0, skip = 0, ret = -1, close_ret;
	int first_size = strlen(firstline);
	if (target_path == NULL || !target_path[0] || genFixPath(target_path, path) < 0)
		return -1;
	fd = genOpen(path, FIO_O_RDONLY);
	if (fd >= 0) {
		size = genLseek(fd, 0, SEEK_END);
		if (size < 0 || size > 1024 * 1024) {
			genClose(fd);
			return -1;
		}
		if (size > 0) {
			old = memalign(64, size);
			if (old == NULL || genLseek(fd, 0, SEEK_SET) < 0 || genRead(fd, old, size) != size) {
				genClose(fd);
				free(old);
				return -1;
			}
			while (skip < size && old[skip] != '\r' && old[skip] != '\n')
				skip++;
			while (skip < size && (old[skip] == '\r' || old[skip] == '\n'))
				skip++;
		}
		if (genClose(fd) < 0) {
			free(old);
			return -1;
		}
	}
	configEnsureSysconfDir(target_path);
	fd = genOpen(path, FIO_O_WRONLY | FIO_O_TRUNC | FIO_O_CREAT);
	if (fd >= 0) {
		if (genWrite(fd, (void *)firstline, first_size) == first_size &&
		    (skip == size || genWrite(fd, old + skip, size - skip) == size - skip))
			ret = 0;
		close_ret = genClose(fd);
		if (close_ret < 0)
			ret = -1;
	}
	free(old);
	return ret;
}

static void saveNetworkSettings(char *Message, const char *target_path)
{
	char firstline[50];
	snprintf(firstline, sizeof(firstline), "%s %s %s\n\r", ip, netmask, gw);
	if (configWriteNetworkSnapshot(target_path, firstline) == 0) {
		snprintf(Message, MAX_PATH, "%s %.*s", LNG(Saved), MAX_PATH - 4, target_path);
		snprintf(LoadedIPConfigPath, sizeof(LoadedIPConfigPath), "%s", target_path);
	} else
		snprintf(Message, MAX_PATH, "%s", LNG(Saved_Failed));
}

void configSaveNetworkToPath(char *msg, const char *target_path)
{
	saveNetworkSettings(msg, target_path);
}

static void applyNetworkSettings(const data_ip_struct *ipdata)
{
	snprintf(ip, sizeof(ip), "%i.%i.%i.%i", ipdata->ip[0], ipdata->ip[1], ipdata->ip[2], ipdata->ip[3]);
	snprintf(netmask, sizeof(netmask), "%i.%i.%i.%i", ipdata->nm[0], ipdata->nm[1], ipdata->nm[2], ipdata->nm[3]);
	snprintf(gw, sizeof(gw), "%i.%i.%i.%i", ipdata->gw[0], ipdata->gw[1], ipdata->gw[2], ipdata->gw[3]);
	updateIpConfigRuntimeState();
}

//---------------------------------------------------------------------------
// Convert IP string to numbers
//---------------------------------------------------------------------------
static void ipStringToOctet(char *ip, int ip_octet[4])
{

	// This takes a string (ip) representing an IP address and converts it
	// into an array of ints (ip_octet)
	// Rewritten 22/10/05

	char oct_str[5];
	int oct_cnt, i, oct_len;

	oct_cnt = 0;
	oct_len = 0;
	oct_str[0] = '\0';

	for (i = 0; ((i <= strlen(ip)) && (oct_cnt < 4)); i++) {
		if ((ip[i] == '.') || (i == strlen(ip))) {
			ip_octet[oct_cnt] = atoi(oct_str);
			oct_cnt++;
			oct_len = 0;
			oct_str[0] = '\0';
		} else if (oct_len < (int)sizeof(oct_str) - 1) {
			oct_str[oct_len++] = ip[i];
			oct_str[oct_len] = '\0';
		}
	}
}
//---------------------------------------------------------------------------
static data_ip_struct BuildOctets(char *ip, char *nm, char *gw)
{

	// Populate 3 arrays with the ip address (as ints)

	data_ip_struct iplist;

	ipStringToOctet(ip, iplist.ip);
	ipStringToOctet(nm, iplist.nm);
	ipStringToOctet(gw, iplist.gw);

	return (iplist);
}
//---------------------------------------------------------------------------
enum CONFIG_NET {
	CONFIG_NET_FIRST = 1,
	CONFIG_NET_IP = CONFIG_NET_FIRST,
	CONFIG_NET_NM,
	CONFIG_NET_GW,

	//Settings after IP addresses
	CONFIG_NET_AFT_IP,
	CONFIG_NET_SAVE = CONFIG_NET_AFT_IP,
	CONFIG_NET_RETURN,

	CONFIG_NET_COUNT
};

void Config_Network(void)
{
	// Menu System for Network Settings Page.

	int s, l;
	int x, y;
	int event, post_event = 0;
	int len;
	char c[MAX_PATH];
	char value[MAX_PATH];
	data_ip_struct ipdata;
	char NetMsg[MAX_PATH] = "";
	char save_override_path[MAX_PATH];
	char save_cwd_path[MAX_PATH];
	char save_sysconf_path[MAX_PATH];
	int has_override_path;
	int save_target;
	char *save_path;

	event = 1;  //event = initial entry
	s = CONFIG_NET_FIRST;
	l = 1;
	ipdata = BuildOctets(ip, netmask, gw);
	configBuildSaveTargets(save_override_path, sizeof(save_override_path),
	                       save_cwd_path, sizeof(save_cwd_path),
	                       save_sysconf_path, sizeof(save_sysconf_path),
	                       "IPCONFIG.DAT", LoadedIPConfigPath, &has_override_path);

	while (1) {
		//Pad response section
		waitPadReady(0, 0);
		if (readpad()) {
			if (new_pad & PAD_UP) {
				event |= 2;  //event |= valid pad command
				if (s != CONFIG_NET_FIRST)
					s--;
				else {
					s = CONFIG_NET_RETURN;
					l = 1;
				}
			} else if (new_pad & PAD_DOWN) {
				event |= 2;  //event |= valid pad command
				if (s != CONFIG_NET_COUNT - 1)
					s++;
				else
					s = CONFIG_NET_FIRST;
				if (s >= CONFIG_NET_AFT_IP)
					l = 1;
			} else if (new_pad & PAD_LEFT) {
				event |= 2;  //event |= valid pad command
				if (s < CONFIG_NET_AFT_IP)
					if (l > 1)
						l--;
			} else if (new_pad & PAD_RIGHT) {
				event |= 2;  //event |= valid pad command
				if (s < CONFIG_NET_AFT_IP)
					if (l < 5)
						l++;
			} else if ((!swapKeys && new_pad & PAD_CROSS) || (swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;  //event |= valid pad command
				if ((s < CONFIG_NET_AFT_IP) && (l > 1)) {
					if (s == CONFIG_NET_IP) {
						if (ipdata.ip[l - 2] > 0) {
							ipdata.ip[l - 2]--;
						}
					} else if (s == CONFIG_NET_NM) {
						if (ipdata.nm[l - 2] > 0) {
							ipdata.nm[l - 2]--;
						}
					} else if (s == CONFIG_NET_GW) {
						if (ipdata.gw[l - 2] > 0) {
							ipdata.gw[l - 2]--;
						}
					}
				}
			} else if ((swapKeys && new_pad & PAD_CROSS) || (!swapKeys && new_pad & PAD_CIRCLE)) {
				event |= 2;  //event |= valid pad command
				if ((s < CONFIG_NET_AFT_IP) && (l > 1)) {
					if (s == CONFIG_NET_IP) {
						if (ipdata.ip[l - 2] < 255) {
							ipdata.ip[l - 2]++;
						}
					} else if (s == CONFIG_NET_NM) {
						if (ipdata.nm[l - 2] < 255) {
							ipdata.nm[l - 2]++;
						}
					} else if (s == CONFIG_NET_GW) {
						if (ipdata.gw[l - 2] < 255) {
							ipdata.gw[l - 2]++;
						}
					}

					} else if (s == CONFIG_NET_SAVE) {
						save_target = configSaveTargetPrompt(save_override_path, save_cwd_path, save_sysconf_path, LoadedIPConfigPath, has_override_path);
						if (save_target != CONFIG_SAVE_TARGET_CANCEL) {
							applyNetworkSettings(&ipdata);

							if (save_target == CONFIG_SAVE_TARGET_OVERRIDE)
								save_path = save_override_path;
							else if (save_target == CONFIG_SAVE_TARGET_CWD)
								save_path = save_cwd_path;
							else
								save_path = save_sysconf_path;
							configRefreshSaveTargetForWrite(save_target, save_path, MAX_PATH, "IPCONFIG.DAT", LoadedIPConfigPath);
							saveNetworkSettings(NetMsg, save_path);
						}
					} else {  //s == CONFIG_NET_RETURN
						applyNetworkSettings(&ipdata);
						return;
					}
			} else if (new_pad & PAD_TRIANGLE) {
				applyNetworkSettings(&ipdata);
				return;
			}
		}

		if (event || post_event) {  //NB: We need to update two frame buffers per event

			//Display section
			clrScr(setting->color[COLOR_BACKGR]);

			x = Menu_start_x;
			y = Menu_start_y;

			printXY(LNG(NETWORK_SETTINGS), x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;

			len = (strlen(LNG(IP_Address)) + 5 > strlen(LNG(Netmask)) + 5) ?
			          strlen(LNG(IP_Address)) + 5 :
			          strlen(LNG(Netmask)) + 5;
			len = (len > strlen(LNG(Gateway)) + 5) ? len : strlen(LNG(Gateway)) + 5;
			sprintf(c, "%s:", LNG(IP_Address));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.ip[0], ipdata.ip[1], ipdata.ip[2], ipdata.ip[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(Netmask));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.nm[0], ipdata.nm[1], ipdata.nm[2], ipdata.nm[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			sprintf(c, "%s:", LNG(Gateway));
			printXY(c, x + 2 * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			sprintf(c, "%.3i . %.3i . %.3i . %.3i", ipdata.gw[0], ipdata.gw[1], ipdata.gw[2], ipdata.gw[3]);
			printXY(c, x + len * FONT_WIDTH, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;

			snprintf(value, sizeof(value), "...");
			configFormatLabelValue(c, sizeof(c), LNG(Save_to), value);
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			y += FONT_HEIGHT / 2;
			sprintf(c, "  %s", LNG(RETURN));
			printXY(c, x, y, setting->color[COLOR_TEXT], TRUE, 0);
			y += FONT_HEIGHT;

			//Cursor positioning section
			y = Menu_start_y + s * FONT_HEIGHT + FONT_HEIGHT / 2;

			if (s >= CONFIG_NET_AFT_IP)
				y += FONT_HEIGHT / 2;
			if (s >= CONFIG_NET_RETURN)
				y += FONT_HEIGHT / 2;
			if (l > 1)
				x += (len - 1) * FONT_WIDTH - 1 + (l - 2) * 6 * FONT_WIDTH;
			drawChar(LEFT_CUR, x, y, setting->color[COLOR_TEXT]);

			//Tooltip section
			if ((s < CONFIG_NET_AFT_IP) && (l == 1)) {
				len = sprintf(c, "%s", LNG(Right_DPad_to_Edit));
			} else if (s < CONFIG_NET_AFT_IP) {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s \xFF"
					                 "0:%s",
					              LNG(Add), LNG(Subtract));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s \xFF"
					                 "1:%s",
					              LNG(Add), LNG(Subtract));
				} else if (s == CONFIG_NET_SAVE) {
					if (swapKeys)
						len = sprintf(c, "\xFF"
						                 "1:%s",
						              LNG(Select));
					else
						len = sprintf(c, "\xFF"
						                 "0:%s",
						              LNG(Select));
			} else {
				if (swapKeys)
					len = sprintf(c, "\xFF"
					                 "1:%s",
					              LNG(OK));
				else
					len = sprintf(c, "\xFF"
					                 "0:%s",
					              LNG(OK));
			}
			sprintf(&c[len], " \xFF"
			                 "3:%s",
			        LNG(Return));
			setScrTmp(NetMsg, c);
		}  //ends if(event||post_event)
		drawScr();
		post_event = event;
		event = 0;

	}  //ends while
}  //ends Config_Network
//---------------------------------------------------------------------------
// End of file: config_network.c
//---------------------------------------------------------------------------
