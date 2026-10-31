//--------------------------------------------------------------
//File name:   main_modules.c
//--------------------------------------------------------------
#include "launchelf.h"
#include "init.h"
#include "main_modules.h"

void validateConfiguredCnfPath(void)
{
	char cnf_path[MAX_PATH];

	if (setting->CNF_Path[0] != '\0') {
		if (genFixPath(setting->CNF_Path, cnf_path) >= 0)
			strcpy(LaunchElfDir, setting->CNF_Path);
	}
}

void initializeRuntimeInputModules(enum BOOT_DEVICE boot_device)
{
	//It's time to load and init drivers
	DPRINTF("Getting IPCONFIG\n");
	if (boot_device != BOOT_DEVICE_HOST)
		getIpConfig();
#ifdef SMB
	/* Only reads SMB2.CNF, from where IPCONFIG.DAT is read: the device list
	 * shows smb1: when it has a VERSION = 1 share. No network here. */
	smbSharesConfigured(SMB_PATH_V1);
#endif

	WaitTime = Timer();
#ifdef DS34
	loadDs34InputModules();
#endif
	DPRINTF("setup pad\n");
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 1: setting up controller ports");
#endif
	setupPad();  //Comment out this line when using early setupPad above
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 2: controller setup returned");
#endif
	waitAnyPadReady();
	if (readpad_no_KB() && (new_pad & PAD_SELECT)) {
		/* Recovery boot: keep legacy "show all devices" behavior, but also
		 * ignore DASHBOARD.CNF for this session. That file owns device
		 * auto-start, so a bad saved stack combination can never trap boot. */
		boot_show_all_devices = 1;
		boot_safe_mode = 1;
	} else {
		/* Keep non-Select boot-held launch keys visible to the main menu. */
		clearPadPressState();
	}
	DPRINTF("Starting keyboard\n");
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 3: starting keyboard");
#endif
	startKbd();
#ifdef WLE_BOOT_DIAGNOSTICS
	drawMsg("Boot check 4: input initialization complete");
#endif
	WaitTime = Timer();
}

void initializeRuntimeDisplayModules(int cnf_error, char *main_msg)
{
	Load_External_Language();
	loadConfiguredBootFont();

	gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
	if (cnf_error < 0)
		sprintf(main_msg, "%s", LNG(Failed_To_Load));
	else
		sprintf(main_msg, "%s", LNG(Loaded_Config));
}
