//--------------------------------------------------------------
//File name:   main_actions.c
//--------------------------------------------------------------
#include "launchelf.h"
#include "init.h"
#include "main_actions.h"
#include "main_exploit_installer.h"
#include "main_gameid.h"
#include "main_history.h"
#include "main_info_screens.h"
#include "dash.h"
#include "dash_sound.h"

/* Optional observer called right before an ELF is handed to the loader
 * (used by the dashboard to maintain its "recent" list). */
void (*mainActionLaunchHook)(const char *path) = NULL;

//CleanUpForExec releases uLE stuff preparatory to launching some other application
//------------------------------
static void cleanUpForExecInternal(int end_pad_rpc)
{
	/* The launched ELF (or its loader's IOP reset) must not find the SMB server writing a card. */
	stopSmbServer();
	fadeScreenToBlack();
	dashSoundShutdown(); /* the launch is now committed */
	DashThemeSetLegacyEnabled(0);
	clearScreenToBlack();
	free(setting);
	free(elisaFnt);
	free(External_Lang_Buffer);

	/*
	 * Closing the ports releases our pad buffers. Ordinary launches retain the
	 * historical wLaunchELF behavior. Ember also ends this EE libpad RPC client.
	 * On USB, its launcher then rebuilds a launcHER-compatible storage IOP
	 * without PADMAN and performs no further reset before ExecPS2.
	 */
	padPortClose(1, 0);
	padPortClose(0, 0);
	if (end_pad_rpc)
		padEnd();

	closeKeyboardIfOpened();
#ifdef DS34
	WaitSema(semRunning);
	isRunning = 0;
	SignalSema(semRunning);
	WaitSema(semFinish);
	ds34usb_reset();
	ds34bt_reset();
#endif
}

int PS2BrowserSkipMc;

void CleanUpForExec(void)
{
	cleanUpForExecInternal(0);
}

int CleanUpForKeepIopExec(const char *title)
{
	/*
	 * Any ELF that inherits the current IOP also inherits every IOP-side RPC
	 * server and device driver. Drain dashboard IO/audio first and release our
	 * libpad client so the child can bind cleanly instead of inheriting stale
	 * EE ownership.
	 */
	if (!DashPrepareKeepIopHandoff(title != NULL ? title : "Preparing application"))
		return 0;

	cleanUpForExecInternal(1);
	return 1;
}

int CleanUpForEmberExec(void)
{
	return CleanUpForKeepIopExec("Preparing Ember");
}
//------------------------------
//endfunc CleanUpForExec / CleanUpForEmberExec
//---------------------------------------------------------------------------
static int isHddLaunchPath(const char *path)
{
	return (!strncmp(path, "hdd", 3) && path[3] >= '0' && path[3] <= '9' && path[4] == ':' && path[5] == '/');
}

static int isMiscAboutWlePath(const char *path)
{
	char current_path[MAX_PATH];
	char legacy_path[MAX_PATH];

	if (path == NULL || setting == NULL)
		return FALSE;
	if (!stricmp(path, setting->Misc_About_uLE))
		return TRUE;

	snprintf(current_path, sizeof(current_path), "%s%s", setting->Misc, LNG(About_uLE));
	if (!stricmp(path, current_path))
		return TRUE;

	snprintf(legacy_path, sizeof(legacy_path), "%sAbout uLE", setting->Misc);
	return !stricmp(path, legacy_path);
}

#ifdef XFROM
static int isMbrLaunchPath(const char *path)
{
	return (path != NULL &&
	        (!stricmp(path, "xfrom:/BIEXEC-SYSTEM/xosdmain") ||
	         !stricmp(path, "xfrom:/BIEXEC-SYSTEM/xosdmain.elf") ||
	         !stricmp(path, "hdd0:__system:pfs:/BIEXEC-SYSTEM/xosdmain.elf")));
}
#endif

static const char *getLaunchArgsSidecarBasePath(const char *path, const char *fullpath)
{
	if (path != NULL && !strncmp(path, "mc:/", 4) && fullpath != NULL && fullpath[0] != '\0')
		return fullpath;
	return path;
}

/* Card updates that ignore BootBrowser, so only SkipMc reaches the Browser.
   Read-only checks on the first card OSDSYS would use:
   - LoadBOOTer, which our exploit installer writes, always runs
     mc0:/BOOT/BOOT.ELF; recognise it by the path it carries.
   - FMCB opens its own menu (issue #346): its SYS-CONF/FREEMCB.CNF is on
     that card. If OSDMenu's SYS-CONF/OSDMENU.CNF is on either card, leave
     it alone (OSDMenu may leave FREEMCB.CNF behind). */
static int cardUpdateIgnoresBrowser(const char *update_path)
{
	char path[MAX_PATH], buf[18];
	int port, other, fd, match;

	if (strncmp(update_path, "mc:", 3))
		return 0;
	for (port = 0; port < 2; port++) {
		snprintf(path, sizeof(path), "mc%d%s", port, update_path + 2);
		if ((fd = genOpen(path, FIO_O_RDONLY)) < 0)
			continue;
		match = genLseek(fd, 12440, SEEK_SET) == 12440 && genRead(fd, buf, 18) == 18 && !memcmp(buf, "mc0:/BOOT/BOOT.ELF", 18);
		genClose(fd);
		if (!match) {
			snprintf(path, sizeof(path), "mc%d:/SYS-CONF/FREEMCB.CNF", port);
			if ((fd = genOpen(path, FIO_O_RDONLY)) >= 0) {
				genClose(fd);
				match = 1;
				for (other = 0; other < 2 && match; other++) {
					snprintf(path, sizeof(path), "mc%d:/SYS-CONF/OSDMENU.CNF", other);
					if ((fd = genOpen(path, FIO_O_RDONLY)) >= 0) {
						genClose(fd);
						match = 0;
					}
				}
			}
		}
		return match;
	}
	return 0;
}

static int isMiscDashboardPath(const char *path)
{
	char dash_path[MAX_PATH];

	if (path == NULL || setting == NULL)
		return FALSE;
	snprintf(dash_path, sizeof(dash_path), "%s%s", setting->Misc, DashMiscCommandName());
	return !stricmp(path, dash_path);
}

static void getTextEditorReturnPath(char *out)
{
	getFilePath(out, FALSE);
	LastDir[0] = '\0';
}

static void executeBrowserSelection(char *selected_path, const MainExecuteContext *ctx)
{
	int editor_result;

	while (selected_path[0]) {
		if (IsTextEditorFileType(selected_path)) {
			editor_result = TextEditor(selected_path);
			selected_path[0] = 0;
			if (editor_result == TEXTEDITOR_RESULT_LAUNCH_ARGS) {
				getFilePath(selected_path, FALSE);
				if (!selected_path[0])
					LaunchArgsClear();
				continue;
			}
			LaunchArgsClear();
			if (editor_result == TEXTEDITOR_RESULT_BROWSE_DIR) {
				getTextEditorReturnPath(selected_path);
				continue;
			}
		} else {
			if (LaunchArgsPending() &&
			    !genCmpFileExt(selected_path, "ELF") &&
			    !genCmpFileExt(selected_path, "KELF") &&
			    !genCmpFileExt(selected_path, "XLF"))
				LaunchArgsClear();
			ExecuteMainAction(selected_path, ctx);
		}
		break;
	}
}

static int elfLaunchIopMode(const MainExecuteContext *ctx)
{
	if (ctx->elf_iop_mode == MAIN_ELF_IOP_KEEP)
		return 0;
	if (ctx->elf_iop_mode == MAIN_ELF_IOP_REBOOT)
		return 1;
	return DashChooseElfIopMode(setting->reboot_iop_elf_load);
}

/* A launch whose device drivers did not load says so, never that the file is
 * not found: the dashboard then offers to remove a shortcut that still works. */
static void setDriversNotLoadedMsg(char *msg, const char *path, const char *device)
{
#ifdef SMB
	int smb = smbPathKind(path);

	if (smb != SMB_PATH_NONE && smbLoadError(smb)[0]) {
		snprintf(msg, MAX_PATH, "%s", smbLoadError(smb));
		return;
	}
#else
	(void)path;
#endif
	snprintf(msg, MAX_PATH, "%s drivers could not be loaded.", device);
}

// Execute. Execute an action. May be called recursively.
// For any path specified, its device must be accessible.
//------------------------------
void ExecuteMainAction(char *pathin, const MainExecuteContext *ctx)
{
	char tmp[MAX_PATH];
	static char path[MAX_PATH];
	static char fullpath[MAX_PATH];
	static char party[MAX_PATH];
	char *p;
	const char *no_drivers = NULL;  //device whose drivers failed to load
	int x, t = 0;
	char dvdpl_path[] = "mc0:/BREXEC-DVDPLAYER/dvdplayer.elf";
	int dvdpl_update;
#ifdef XFROM
	char mbr_mem_arg[32];
#endif

	if (ctx == NULL || pathin == NULL || pathin[0] == 0)
		return;

	/* A chosen OSDSYS program is an ordinary executable launch (plain ELF or
	 * KELF, any device), resolved before device dispatch like a browser file.
	 * From saildot4k/Launchstation ea15d90. */
	if (!stricmp(pathin, setting->Misc_OSDSYS) &&
	    setting->LK_Flag[SETTING_LK_OSDSYS] && setting->LK_Path[SETTING_LK_OSDSYS][0]) {
		pathin = setting->LK_Path[SETTING_LK_OSDSYS];
		if (strchr(pathin, ':') == NULL) {
			snprintf(ctx->main_msg, MAX_PATH, "Choose an executable file for the OSDSYS program.");
			return;
		}
	}
	if (!uLE_related(path, pathin))  //1==uLE_rel 0==missing, -1==other dev
		return;

	if (LaunchArgsPending() &&
	    !genCmpFileExt(path, "ELF") &&
	    !genCmpFileExt(path, "KELF") &&
	    !genCmpFileExt(path, "XLF"))
		LaunchArgsClear();

Recurse_for_ESR:  //Recurse here for PS2Disc command with ESR disc
	strcpy(fullpath, path);  //a failure names this path until a branch sets fullpath

	if (isPcCardCfPath(path)) {
		LaunchArgsClear();
		snprintf(ctx->main_msg, MAX_PATH, "Direct launch from PC CARD CF is not supported; copy to another device first.");
		return;
	}

	if (IsPopstarterVcdPath(path)) {
		LaunchPopstarterVcd(path, ctx->main_msg, MAX_PATH);
		return;
	}
	if (genCmpFileExt(path, "CUE") || IsEmberGamePath(path)) {
		/* A .CUE selection is always intended for Ember.  BIN/EXE remain
		 * structure-aware so unrelated binary/executable files are not
		 * hijacked.  LaunchEmberGame performs the strict games/<Game>/
		 * validation and returns a useful layout error if it is misplaced. */
		LaunchArgsClear();
		LaunchEmberGame(path, ctx->main_msg, MAX_PATH);
		return;
	}
	if (IsNeutrinoIsoPath(path)) {
		LaunchArgsClear();
		LaunchNeutrinoIso(path, ctx->main_msg, MAX_PATH);
		return;
	}

	if (!strncmp(path, "mc", 2)) {
		party[0] = 0;
		if (path[2] != ':')
			goto CheckELF_path;
		strcpy(fullpath, "mc0:");
		strcat(fullpath, path + 3);
		if ((t = checkELFheader(fullpath)) > 0)
			goto ELFchecked;
		{
			int first = t;  //-1: mc0: opened the file and it is no ELF
			fullpath[2] = '1';
			if ((t = checkELFheader(fullpath)) > 0)
				goto ELFchecked;
			if (first == -1 && t != -1) {  //a file that opened is not missing because mc1: has none
				t = -1;
				fullpath[2] = '0';  //and the message names the card it opened on
			}
			goto ELFnotFound;
		}

	} else if (!strncmp(path, "vmc", 3)) {
		x = path[3] - '0';
		if ((x < 0) || (x > 1) || !vmcMounted[x])
			goto ELFnotFound;
		goto CheckELF_path;
	}
#ifdef XFROM
	else if (isMbrLaunchPath(path)) {
		snprintf(fullpath, sizeof(fullpath), "%s", path);
		t = 0;
		if (!console_is_PSX)
			goto ELFnotFound;
		if (PrepareMbrLaunchPayload(path, mbr_mem_arg, sizeof(mbr_mem_arg)) < 0)
			goto ELFnotFound;

		x = elfLaunchIopMode(ctx);
		if (x)
			CleanUpForExec();
		else if (!CleanUpForKeepIopExec("Preparing application"))
			return;
		LaunchArgsClear();
		RunLoaderMemory("rom0:HDDBOOT", mbr_mem_arg, x);
		return;
	}
#endif
	else if (isHddLaunchPath(path)) {
		if (!loadHddModules())
			no_drivers = "HDD";
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		//coming here means the ELF is fine
		snprintf(party, sizeof(party), "hdd%c:%s", path[3], path + 6);
		p = strchr(party, '/');
		snprintf(fullpath, sizeof(fullpath), "pfs0:%s", p);
		*p = 0;
		goto ELFchecked;
	} else if (!strncmp(path, "dvr_hdd0:/", 10)) {
#ifdef DVRP
		if (!console_is_PSX)
			goto ELFnotFound;
		if (!loadDVRPHddModules())
			no_drivers = "DVR HDD";
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		//coming here means the ELF is fine
		snprintf(party, sizeof(party), "dvr_hdd0:%s", path + 10);
		p = strchr(party, '/');
		if (p != NULL) {
			snprintf(fullpath, sizeof(fullpath), "dvr_pfs0:%s", p);
			*p = 0;
			fullpath[7] = (getDVRPPartyMountIndex(party) == 1) ? '1' : '0';
		} else {
			snprintf(fullpath, sizeof(fullpath), "dvr_pfs%d:/", (getDVRPPartyMountIndex(party) == 1) ? 1 : 0);
		}
		goto ELFchecked;
#else
		goto ELFnotFound;
#endif
	} else if (!strncmp(path, "xfrom", 5)) {
#ifdef XFROM
		if (!console_is_PSX)
			goto ELFnotFound;
		if (!loadFlashModules()) {
			no_drivers = "XFROM";
			goto ELFnotFound;
		}
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		strcpy(fullpath, path);
		goto ELFchecked;
#else
		goto ELFnotFound;
#endif
	} else if (!strncmp(path, "mx4sio", 6)) {
#ifdef MX4SIO
		if (!mx4sio_driver_running && !loadMx4sioModules()) {
			no_drivers = "MX4SIO";
			goto ELFnotFound;
		}
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		party[0] = 0;
		strcpy(fullpath, path);
		goto ELFchecked;
#else
		goto ELFnotFound;
#endif
	} else if (!strncmp(path, "ilink", 5)) {
#ifdef ILINK
		if (!loadIlinkModules()) {
			no_drivers = "iLink";
			goto ELFnotFound;
		}
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		party[0] = 0;
		strcpy(fullpath, path);
		goto ELFchecked;
#else
		goto ELFnotFound;
#endif
	} else if (!strncmp(path, "mmce", 4)) {
#ifdef MMCE
		if (!loadMmceModules())
			no_drivers = "MMCE";
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		strcpy(fullpath, path);
		goto ELFchecked;
#else
		goto ELFnotFound;
#endif
	} else if (!strncmp(path, "usb", 3)) {
		char *pathSep;

		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		party[0] = 0;
		if (genFixPath(path, fullpath) < 0)
			goto ELFnotFound;
		pathSep = strchr(fullpath, '/');
		if (pathSep && (pathSep - fullpath < 7) && pathSep[-1] == ':')
			memmove(pathSep, pathSep + 1, strlen(pathSep + 1) + 1);
		goto ELFchecked;
	} else if (!strncmp(path, "ata", 3)) {
		char *pathSep;

#ifdef EXFAT
		if (!loadAtaModules())
			no_drivers = "ATA";
#endif
		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		party[0] = 0;
		if (!strncmp(path, "ata:", 4)) {
			size_t tail_len;

			tail_len = strlen(path + 4);
			if (tail_len >= sizeof(fullpath) - 5)
				goto ELFnotFound;
			memcpy(fullpath, "ata0:", 5);
			memcpy(fullpath + 5, path + 4, tail_len + 1);
		} else
			strcpy(fullpath, path);
		pathSep = strchr(fullpath, '/');
		if (pathSep && (pathSep - fullpath < 7) && pathSep[-1] == ':')
			memmove(pathSep, pathSep + 1, strlen(pathSep + 1) + 1);
		goto ELFchecked;
	} else if (!strncmp(path, "mass", 4)) {
		char *pathSep;

		if ((t = checkELFheader(path)) <= 0)
			goto ELFnotFound;
		party[0] = 0;

		strcpy(fullpath, path);
		pathSep = strchr(path, '/');
		if (pathSep && (pathSep - path < 7) && pathSep[-1] == ':')
			strcpy(fullpath + (pathSep - path), pathSep + 1);
		goto ELFchecked;
	} else if (!strncmp(path, "host:", 5)) {
#ifdef ETH
		initHOST();
		if (!isPathDeviceStackLoaded(path))
			no_drivers = "Host";
		party[0] = 0;
		strcpy(fullpath, "host:");
		if (path[5] == '/')
			strcat(fullpath, path + 6);
		else
			strcat(fullpath, path + 5);
		makeHostPath(fullpath, fullpath);
		goto CheckELF_fullpath;
#else
		goto ELFnotFound;
#endif
	} else if (!strncmp(path, "udpfs:", 6)) {
#ifdef UDPFS
		if (!load_udpfs())
			no_drivers = "UDPFS";
		party[0] = 0;
		snprintf(fullpath, sizeof(fullpath), "%s", path);
		goto CheckELF_fullpath;
#else
		goto ELFnotFound;
#endif
#ifdef SMB
	} else if (smbPathKind(path) != SMB_PATH_NONE) {
		/* as udpfs: the loader reads the ELF while the SMB driver is resident */
		if (!isSmbDriverLoaded(smbPathKind(path)) && !load_smb(smbPathKind(path))) {
			no_drivers = "SMB";
			goto ELFnotFound;
		}
		party[0] = 0;
		snprintf(fullpath, sizeof(fullpath), "%s", path);
		goto CheckELF_fullpath;
#endif
	} else if (!stricmp(path, setting->Misc_OSDSYS)) {
		char arg0[20], arg1[20], arg2[20], arg3[40];
		char *args[4] = {arg0, arg1, arg2, arg3};
		char kelf_loader[40];
		int fd, argc;

		/* No override: the native memory-card system-update boot path. */
		strcpy(path, ctx->default_osdsys_path);

		fd = genOpen(path, FIO_O_RDONLY);
		if (fd >= 0)
			goto close_fd_and_launch_OSDSYS;
		strcpy(fullpath, path);
		if (strncmp(path, "mc:", 3) != 0)
			goto ELFnotFound;
		path[2] = '0';
		strcpy(path + 3, fullpath + 2);
		fd = genOpen(path, FIO_O_RDONLY);
		if (fd >= 0)
			goto close_fd_and_launch_OSDSYS;
		path[2] = '1';
		fd = genOpen(path, FIO_O_RDONLY);
		if (fd >= 0)
			goto close_fd_and_launch_OSDSYS;
		if (fd < 0)
			goto ELFnotFound;
	close_fd_and_launch_OSDSYS:
		genClose(fd);
		strcpy(arg0, "-m rom0:SIO2MAN");
		strcpy(arg1, "-m rom0:MCMAN");
		strcpy(arg2, "-m rom0:MCSERV");
		snprintf(arg3, sizeof(arg3), "-x %s", path);
		argc = 4;
		strcpy(kelf_loader, "moduleload");
		CleanUpForExec();
		LoadExecPS2(kelf_loader, argc, args);

	} else if (!stricmp(path, setting->Misc_PS2Disc)) {
		/* With no disc, the SYSTEM.CNF read below can hang (issue #346). */
		if (sceCdGetDiskType() == SCECdNODISC) {
			sprintf(ctx->main_msg, "%s => %s", LNG(PS2Disc), LNG(No_Disc));
			goto Done_PS2Disc;
		}
		drawMsg(LNG(Reading_SYSTEMCNF));
		party[0] = 0;
		readSystemCnf();
		if (BootDiscType == 2) {  //Boot a PS2 disc
			strcpy(fullpath, SystemCnf_BOOT2);
			goto CheckELF_fullpath;
		}
		if (BootDiscType == 1) {  //Boot a PS1 disc
			char disc_gameid[12];
			int have_disc_gameid;
			char *args[2] = {SystemCnf_BOOT, SystemCnf_VER};

			have_disc_gameid = buildLaunchGameID(SystemCnf_BOOT, disc_gameid, sizeof(disc_gameid));
			if (have_disc_gameid) {
				updateOSDHistoryFile(disc_gameid);
				applyXPARAM(disc_gameid);
			}

			CleanUpForExec();
			if (have_disc_gameid && !setting->cdrom_disable_gameid)
				displayRetroGemGameID(disc_gameid, 2);
			LoadExecPS2("rom0:PS1DRV", 2, args);
			sprintf(ctx->main_msg, "PS1DRV %s", LNG(Failed));
			goto Done_PS2Disc;
		}
		if (uLE_cdDiscValid()) {
			if (cdmode == SCECdDVDV) {
				x = Check_ESR_Disc();
				DPRINTF("Check_ESR_Disc => %d\n", x);
				if (x > 0) {  //ESR Disc, so launch ESR
					if (setting->LK_Flag[SETTING_LK_ESR] && setting->LK_Path[SETTING_LK_ESR][0])
						strcpy(path, setting->LK_Path[SETTING_LK_ESR]);
					else
						strcpy(path, ctx->default_esr_path);

					goto Recurse_for_ESR;
				}

				//DVD Video Disc, so launch DVD player
				char arg0[20], arg1[20], arg2[20], arg3[40];
				char *args[4] = {arg0, arg1, arg2, arg3};
				char kelf_loader[40];
				char MG_region[10];
				int i, pos, tst, argc;

				if ((tst = SifLoadModule("rom0:ADDDRV", 0, NULL)) < 0)
					goto Fail_DVD_Video;

				strcpy(arg0, "-k rom1:EROMDRVA");
				strcpy(arg1, "-m erom0:UDFIO");
				strcpy(arg2, "-x erom0:DVDPLA");
				argc = 3;
				strcpy(kelf_loader, "moduleload2 rom1:UDNL rom1:DVDCNF");

				strcpy(MG_region, "ACEJMORU");
				pos = strlen(arg0) - 1;
				for (i = 0; i < 9; i++) {  //NB: MG_region[8] is a string terminator
					arg0[pos] = MG_region[i];
					tst = SifLoadModuleEncrypted(arg0 + 3, 0, NULL);
					if (tst >= 0)
						break;
				}

				pos = strlen(arg2);
				if (i == 8)
					strcpy(&arg2[pos - 3], "ELF");
				else
					arg2[pos - 1] = MG_region[i];
				//At this point all args are ready to use internal DVD player

				//We must check for an updated player on MC
				dvdpl_path[6] = ctx->rough_region;
				dvdpl_update = 0;
				for (i = 0; i < 2; i++) {
					dvdpl_path[2] = '0' + i;
					if (wleExists(dvdpl_path)) {
						dvdpl_update = 1;
						break;
					}
				}

				if ((tst < 0) && (dvdpl_update == 0))
					goto Fail_PS2Disc;  //We must abort if no working kelf found

				if (dvdpl_update) {  // Launch DVD player from memory card
					strcpy(arg0, "-m rom0:SIO2MAN");
					strcpy(arg1, "-m rom0:MCMAN");
					strcpy(arg2, "-m rom0:MCSERV");
					sprintf(arg3, "-x %s", dvdpl_path);  // -x :elf is encrypted for mc
					argc = 4;
					strcpy(kelf_loader, "moduleload");
				}

				CleanUpForExec();
				LoadExecPS2(kelf_loader, argc, args);

			Fail_DVD_Video:
				sprintf(ctx->main_msg, "DVD-Video %s", LNG(Failed));
				goto Done_PS2Disc;
			}
			if (cdmode == SCECdCDDA) {
				//Fail_CDDA:
				sprintf(ctx->main_msg, "CDDA %s", LNG(Failed));
				goto Done_PS2Disc;
			}
		}
	Fail_PS2Disc:
		sprintf(ctx->main_msg, "%s => %s CDVD 0x%02X", LNG(PS2Disc), LNG(Failed), cdmode);
	Done_PS2Disc:
		x = x;
	} else if (isMiscDashboardPath(path)) {
		ctx->main_msg[0] = 0;
		if (!DashIsActive())
			DashRun(ctx, 0);
		return;
	} else if (!stricmp(path, setting->Misc_FileBrowser)) {
		ctx->main_msg[0] = 0;
		tmp[0] = 0;
		LastDir[0] = 0;
		getFilePath(tmp, FALSE);
		executeBrowserSelection(tmp, ctx);
		return;
	} else if (!stricmp(path, setting->Misc_PS2Browser)) {
		/* BootBrowser alone lets OSDSYS run the memory card update (FMCB,
		   OSDMenu) first. The Tools "skip card update" entry adds SkipMc,
		   and so do LoadBOOTer and FMCB cards, which would never open the Browser. */
		char *args[2] = {"BootBrowser", "SkipMc"};
		int skip_mc = PS2BrowserSkipMc || cardUpdateIgnoresBrowser(ctx->default_osdsys_path);

		CleanUpForExec();
		ExecOSD(1 + skip_mc, args);
#ifdef ETH
	} else if (!stricmp(path, setting->Misc_PS2Net)) {
		ctx->main_msg[0] = 0;
		DashFtpServer();
		return;
#endif
	} else if (!stricmp(path, setting->Misc_PS2PowerOff)) {
		ctx->main_msg[0] = 0;
		drawMsg(LNG(Powering_Off_Console));
		setupPowerOff();
		if (!isPowerOffReady()) {
			snprintf(ctx->main_msg, MAX_PATH, "Power-off service could not be started. Use the console power button.");
			return;
		}
		closeAllAndPoweroff();
		return;
	} else if (!stricmp(path, setting->Misc_HddManager)) {
		if (DashIsActive())
			dashHddManager();
		return;
	} else if (!stricmp(path, setting->Misc_TextEditor)) {
		t = TextEditor(NULL);
		if (t == TEXTEDITOR_RESULT_LAUNCH_ARGS) {
			tmp[0] = 0;
			getFilePath(tmp, FALSE);
			if (!tmp[0])
				LaunchArgsClear();
			executeBrowserSelection(tmp, ctx);
		} else if (t == TEXTEDITOR_RESULT_BROWSE_DIR) {
			tmp[0] = 0;
			getTextEditorReturnPath(tmp);
			executeBrowserSelection(tmp, ctx);
		}
		return;
	} else if (!stricmp(path, setting->Misc_Configure)) {
		Load_External_Language();
		loadFont(setting->font_file);
		config(ctx->main_msg, ctx->cnf_path);
		return;
		//Next clause is for an optional font test routine
	} else if (!stricmp(path, setting->Misc_ShowFont)) {
		ShowFont();
		return;
	} else if (!stricmp(path, setting->Misc_Debug_Info)) {
		ShowDebugInfo(ctx->boot_argc, ctx->boot_argv, ctx->boot_path, ctx->default_osdsys_path2, ctx->rough_region, ctx->romver_data);
		return;
	} else if (isMiscAboutWlePath(path)) {
		Show_About_uLE();
		return;
	} else if (!stricmp(path, setting->Misc_Show_Build_Info)) {
		Show_build_info();
		return;
	} else if (!stricmp(path, setting->Misc_Exploit_Installer)) {
		ShowExploitInstaller(ctx);
		return;
	} else if (!stricmp(path, setting->Misc_Reboot_IOP)) {
		ctx->main_msg[0] = 0;
		rebootIopAndReloadCoreStack();
		ctx->main_msg[0] = 0;
		return;
	} else if (!strncmp(path, "cdfs", 4)) {
		loadCdModules();
		LCDVD_FLUSHCACHE();
		LCDVD_DISKREADY(0);
		party[0] = 0;
		goto CheckELF_path;
	} else if (!strncmp(path, "rom", 3)) {
		party[0] = 0;
	CheckELF_path:
		strcpy(fullpath, path);
	CheckELF_fullpath:
		if ((t = checkELFheader(fullpath)) <= 0)
			goto ELFnotFound;
	ELFchecked:
		{
			int show_launch_gameid = 0;
			int disc_launch = isLikelyDiscLaunch(path);
			int have_launch_gameid = 0;
			char launch_gameid[12];

			if (!LaunchArgsPending() &&
			    LaunchArgsLoadSidecarForExec(getLaunchArgsSidecarBasePath(path, fullpath), ctx->main_msg, MAX_PATH) < 0)
				return;

			if (disc_launch) {
				have_launch_gameid = buildLaunchGameID(fullpath, launch_gameid, sizeof(launch_gameid));
				if (have_launch_gameid) {
					updateOSDHistoryFile(launch_gameid);
					applyXPARAM(launch_gameid);
				}
				show_launch_gameid = have_launch_gameid && !setting->cdrom_disable_gameid;
			} else if (setting->app_gameid) {
				show_launch_gameid = buildLaunchGameID(fullpath, launch_gameid, sizeof(launch_gameid));
			}

			if (mainActionLaunchHook != NULL)
				mainActionLaunchHook(path);
			x = elfLaunchIopMode(ctx);
			if (x)
				CleanUpForExec();
			else if (!CleanUpForKeepIopExec("Preparing application"))
				return;
			if (show_launch_gameid)
				displayRetroGemGameID(launch_gameid, 2);
			RunLoaderElf(fullpath, party, path, t, x);
		}
	} else {  //Invalid path
		t = 0;
	ELFnotFound:
		LaunchArgsClear();
		if (t == -1 || t > 0)  //it opened, so it is not missing
			sprintf(ctx->main_msg, "%s: %s.", LNG(This_file_isnt_an_ELF), fullpath);
		else if (no_drivers != NULL)  //it could not be opened
			setDriversNotLoadedMsg(ctx->main_msg, path, no_drivers);
		else
			sprintf(ctx->main_msg, "%s %s.", fullpath, LNG(is_Not_Found));
		return;
	}
}
//------------------------------
//endfunc ExecuteMainAction
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// ExecuteBrowserFrom opens the FileBrowser in start_dir ("" = device list)
// and then processes the selection exactly like MISC/FileBrowser does.
//---------------------------------------------------------------------------
void ExecuteBrowserFrom(const char *start_dir, const MainExecuteContext *ctx)
{
	char tmp[MAX_PATH];

	if (ctx == NULL)
		return;
	ctx->main_msg[0] = 0;
	tmp[0] = 0;
	snprintf(LastDir, MAX_NAME, "%s", start_dir != NULL ? start_dir : "");
	getFilePath(tmp, FALSE);
	executeBrowserSelection(tmp, ctx);
}
