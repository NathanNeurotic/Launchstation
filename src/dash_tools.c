//--------------------------------------------------------------
//File name:   dash_tools.c
//TOOLS: every wLaunchELF function with a friendly name and a short
//description. Each entry dispatches the MISC command path through
//dashRunMisc(), which opens the native dashboard version of each tool or
//runs the command (disc, OSDSYS, network...) behind the loading overlay.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "mmce_igr.h"
#include <stddef.h>

enum ToolAction {
	TA_HEADER = 0,
	TA_MISC,
	TA_BROWSER,
	TA_BROWSER_AT,
	TA_EDITOR,
	TA_IGR_SETUP
};

typedef struct
{
	int action;
	int icon;
	const char *name;
	const char *desc;
	const char *note; /* safety note shown in the detail panel */
	int confirm;      /* 1: ask before running, 2: dangerous */
	size_t misc_offset;
	const char *path;
} Tool;

#define MISC(field) offsetof(SETTING, field)

static const Tool tools[] = {
    {TA_HEADER, -1, "ESSENTIALS", NULL, NULL, 0, 0, NULL},
    {TA_BROWSER, DI_FOLDER, "File Browser", "Browse every device, copy, move, rename, delete, create folders, build and extract PSU saves, mount VMC images and launch anything.", "Press Triangle inside the browser for file operations; Square selects.", 0, 0, NULL},
    {TA_EDITOR, DI_TEXT, "Text Editor", "Edit CNF, CFG, TXT, INI and argument files. Also used to launch an ELF with custom arguments.", NULL, 0, 0, NULL},
    {TA_MISC, DI_DISC, "Start disc", "Boot the disc in the drive: PS2 and PS1 games, DVD Video (with the console's player) and ESR discs.", NULL, 0, MISC(Misc_PS2Disc), NULL},
    {TA_BROWSER_AT, DI_DISC, "PS1 games (POPStarter)", "Open the File Browser; select a .VCD image to start it through POPStarter.", "Set POPStarter under Settings > Launching > Disc & PS1 loaders.", 0, 0, ""},
    {TA_HEADER, -1, "STORAGE", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_HDD, "HDD Manager", "Create, remove, rename and expand APA partitions, view HDL game information and format the internal HDD.", "Partition changes and formatting are permanent.", 0, MISC(Misc_HddManager), NULL},
    {TA_MISC, DI_MC, "Memory card exploit installer", "Install a memory card system-update exploit for this console, another model or region, or PSX/DESR.", "Writes system folders to the memory card. Follow the on-screen checks carefully.", 1, MISC(Misc_Exploit_Installer), NULL},
    {TA_IGR_SETUP, DI_MC, "OPL IGR automatic MMCE backup", "Set up verified automatic backup of every MMCE card image when OPL exits a game through IGR.", "Installs or updates WLE-IGR.ELF on the required boot medium and tells you the exact OPL Exit-to path.", 0, 0, NULL},
    {TA_HEADER, -1, "SYSTEM", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_APP, "PS2 system browser", "Leave wLaunchELF and open the console's own Browser.", NULL, 1, MISC(Misc_PS2Browser), NULL},
    {TA_MISC, DI_APP, "OSDSYS", "Start the console's OSD (or the OSDSYS path configured in wLaunchELF).", NULL, 1, MISC(Misc_OSDSYS), NULL},
#ifdef ETH
    {TA_MISC, DI_NETWORK, "FTP / Remote access", "Start FTP and PS2Link on demand, accept remote commands, and receive staged/resumable ISO transfers from MrRexD-compatible PC tools.", "Network and HDD drivers stay unloaded until you start this tool.", 0, MISC(Misc_PS2Net), NULL},
#endif
    {TA_MISC, DI_INFO, "System information", "Console model, ROM version, boot path, loaded configuration and debug details.", NULL, 0, MISC(Misc_Debug_Info), NULL},
    {TA_MISC, DI_CHIP, "Build information", "Features and drivers included in this build of wLaunchELF.", NULL, 0, MISC(Misc_Show_Build_Info), NULL},
    {TA_MISC, DI_TEXT, "Font viewer", "Show every glyph of the active font.", NULL, 0, MISC(Misc_ShowFont), NULL},
    {TA_MISC, DI_INFO, "About wLaunchELF", "Credits and version.", NULL, 0, MISC(Misc_About_uLE), NULL},
    {TA_HEADER, -1, "POWER", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_REFRESH, "Reboot IOP", "Restart the I/O processor and all device drivers. Devices are detected again when used.", NULL, 1, MISC(Misc_Reboot_IOP), NULL},
    {TA_MISC, DI_POWER, "Power off", "Turn the console off.", NULL, 2, MISC(Misc_PS2PowerOff), NULL},
};

#define NTOOLS ((int)(sizeof(tools) / sizeof(tools[0])))

static int tsel = 1, ttop;
static float tsel_anim = 1.0f;

static void tMove(int d)
{
	int s = tsel;

	do {
		s += d;
		if (s < 0 || s >= NTOOLS)
			return;
	} while (tools[s].action == TA_HEADER);
	tsel = s;
}

static void toolsEnter(void)
{
	if (tools[tsel].action == TA_HEADER)
		tMove(1);
}

static void setupIgrBackup(void)
{
	static const char *const actions[] = {"Install / update", "Remove helper"};
	WleIgrInstallStats stats;
	char folder[MAX_PATH], result[384];
	const char *start = LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir;
	int action, r;

	action = dashModalChoice("OPL IGR automatic MMCE backup", actions, 2, 0);
	if (action < 0)
		return;

	if (action == 1) {
		if (!dashModalConfirm("Remove OPL IGR backup helper?",
		                      "Removes WLE-IGR.ELF from the locations managed by this wLaunchELF installation. Existing card backups are not deleted.",
		                      "Remove", 1))
			return;
		dashLoadingBegin("Removing OPL IGR helper");
		r = wleIgrRemove(&stats, result, sizeof(result));
		dashLoadingEnd();
		dashModalMessage(r == 0 ? DI_CHECK : DI_WARNING,
		                 r == 0 ? "OPL IGR helper removed" : "OPL IGR removal incomplete",
		                 result[0] ? result : "Removal failed.",
		                 "Existing WLE-MMCE-Backups history is left untouched.");
		return;
	}

	snprintf(folder, sizeof(folder), "%s", start != NULL ? start : "");
	if (!dashPickPath("Automatic MMCE backup destination", folder,
	                  DASH_PICK_FOLDER, NULL, folder, sizeof(folder)))
		return;

	if (!dashModalConfirm("Install OPL IGR backup helper?",
	                      "wLaunchELF will install or update a verified WLE-IGR.ELF where OPL can launch it. MMCE-hosted setups update every discovered BootCard image.",
	                      "Install", 0))
		return;

	dashLoadingBegin("Configuring OPL IGR automatic backup");
	r = wleIgrConfigure(folder, &stats, result, sizeof(result));
	dashLoadingEnd();

	if (r == 0)
		dashModalMessage(DI_CHECK, "OPL IGR automatic backup ready", result,
		                 "After setting OPL Exit-to to the shown helper, IGR will back up changed MMCE card images and then open wLaunchELF.");
	else
		dashModalMessage(DI_WARNING, "OPL IGR setup incomplete", result[0] ? result : "Setup failed.", NULL);
}

static void runTool(const Tool *t)
{
	if (t->confirm) {
		char body[200];
		snprintf(body, sizeof(body), "%s", t->desc);
		if (!dashModalConfirm(t->name, body, t->confirm == 2 ? t->name : "Continue", t->confirm == 2))
			return;
	}
	switch (t->action) {
		case TA_MISC:
			dashRunMisc((const char *)setting + t->misc_offset);
			break;
		case TA_BROWSER:
			dashOpenBrowser("");
			break;
		case TA_BROWSER_AT:
			dashOpenBrowser(t->path ? t->path : "");
			break;
		case TA_EDITOR:
			dashOpenTextEditor(NULL);
			break;
		case TA_IGR_SETUP:
			setupIgrBackup();
			break;
	}
}

static void toolsInput(const DashInput *in)
{
	int i;

	if (in->pressed & DASH_IN_UP)
		tMove(-1);
	if (in->pressed & DASH_IN_DOWN)
		tMove(1);
	if (in->pressed & DASH_IN_L2)
		for (i = 0; i < 6; i++)
			tMove(-1);
	if (in->pressed & DASH_IN_R2)
		for (i = 0; i < 6; i++)
			tMove(1);
	if (in->pressed & DASH_IN_OK)
		runTool(&tools[tsel]);
}

static void toolsUpdate(float dt)
{
	tsel_anim = dashApproach(tsel_anim, (float)tsel, 16.0f * dashAnimSpeed(), dt);
}

static void toolsDraw(void)
{
	const float lx = 48, ly = 102, row_h = 29, lw = 300;
	const int rows = 10;
	const Tool *t = &tools[tsel];
	int i;

	dashDrawSectionTitle(48, 68, "Tools", "everything wLaunchELF can do");
	if (tsel < ttop + 1)
		ttop = tsel - 1;
	if (tsel >= ttop + rows)
		ttop = tsel - rows + 1;
	if (ttop < 0)
		ttop = 0;

	dashDrawPanelBox(lx - 6, ly - 6, lw + 12, rows * row_h + 12, 14);
	dashGfxScissor(lx - 6, ly - 4, lw + 12, rows * row_h + 8);
	{
		float fy = ly + (tsel_anim - ttop) * row_h;
		dashDrawSelection(lx, fy, lw, row_h - 3, 9, 1.0f);
	}
	for (i = ttop; i < NTOOLS && i < ttop + rows; i++) {
		float y = ly + (i - ttop) * row_h;

		if (tools[i].action == TA_HEADER) {
			dashText(lx + 8, y + 7, tools[i].name, DASH_FS_CAPTION, DASH_C_ACCENT);
			continue;
		}
		dashIcon(tools[i].icon, lx + 10, y + 5, 16, dashItemColor(i == tsel));
		dashTextFit(lx + 34, y + 6.5f, lw - 44, tools[i].name, DASH_FS_SMALL, dashItemColor(i == tsel));
		if (tools[i].confirm == 2 || tools[i].note)
			dashGlow(lx + lw - 14, y + 13, 4, 4, tools[i].confirm == 2 ? DASH_C_DANGER : DASH_C_WARM);
	}
	dashGfxScissorReset();
	dashDrawScrollbar(lx + lw + 10, ly, rows * row_h, ttop, rows, NTOOLS);

	{
		float dx = 372, dy = 98, dw = 228, dh = 292;

		dashDrawCard(dx, dy, dw, dh, 16);
		dashAccentGlow(dx + dw * 0.5f, dy + 66, 80, 60, 1.0f);
		dashIcon(t->icon, dx + dw * 0.5f - 30, dy + 34, 60, DASH_C_TEXT);
		dashTextCenter(dx + dw * 0.5f, dy + 108, t->name, DASH_FS_BODY, DASH_C_TEXT);
		dashTextWrap(dx + 16, dy + 142, dw - 32, 4, t->desc, DASH_FS_CAPTION, DASH_C_TEXT2, 19);
		if (t->note) {
			DashColor wb = dashColorScale(DASH_C_WARM, 0.24f);
			wb.a = 130;
			dashPanel(dx + 12, dy + dh - 64, dw - 24, 52, 10, wb);
			dashRect(dx + 12, dy + dh - 64, 3, 52, dashColorA(DASH_C_WARM, 0.8f));
			dashIcon(DI_WARNING, dx + 20, dy + dh - 54, 16, DASH_C_WARM);
			dashTextWrap(dx + 42, dy + dh - 58, dw - 62, 2, t->note, DASH_FS_CAPTION * 0.95f, DASH_C_WARM, 18);
		}
	}
}

static void toolsHints(void)
{
	DashHint h[2];

	h[0].button = dashOkIcon(), h[0].key = NULL, h[0].label = "Open";
	h[1].button = -1, h[1].key = "START", h[1].label = "Menu";
	dashDrawHints(h, 2);
}

const DashScreen dashToolsScreen = {toolsEnter, toolsInput, toolsUpdate, toolsDraw, toolsHints};
//--------------------------------------------------------------
//End of file: dash_tools.c
//--------------------------------------------------------------
