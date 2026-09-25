//--------------------------------------------------------------
//File name:   dash_tools.c
//TOOLS: every classic wLaunchELF function with a friendly name and a
//short description. Each entry dispatches the existing MISC command path
//through ExecuteMainAction(), so the classic screens (and their own
//confirmations and safety warnings) run unchanged.
//--------------------------------------------------------------
#include "dash_internal.h"
#include <stddef.h>

enum ToolAction {
	TA_HEADER = 0,
	TA_MISC,
	TA_BROWSER,
	TA_BROWSER_AT,
	TA_CLASSIC,
	TA_EDITOR
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
    {TA_BROWSER, DI_FOLDER, "File Browser", "Browse every device, copy, move, rename, delete, create folders, build and extract PSU saves, mount VMC images and launch anything.", "Press R1 inside the browser for file operations.", 0, 0, NULL},
    {TA_EDITOR, DI_TEXT, "Text Editor", "Edit CNF, CFG, TXT, INI and argument files. Also used to launch an ELF with custom arguments.", NULL, 0, 0, NULL},
    {TA_MISC, DI_DISC, "Start disc", "Boot the disc in the drive: PS2 and PS1 games, DVD Video (with the console's player) and ESR discs.", NULL, 0, MISC(Misc_PS2Disc), NULL},
    {TA_BROWSER_AT, DI_DISC, "PS1 games (POPStarter)", "Open the File Browser; select a .VCD image to start it through POPStarter.", "Configure the POPStarter ELF under Settings > Advanced.", 0, 0, ""},
    {TA_HEADER, -1, "STORAGE", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_HDD, "HDD Manager", "Create, remove, rename and expand APA partitions, view HDL game information and format the internal HDD.", "Partition changes and formatting are permanent.", 0, MISC(Misc_HddManager), NULL},
    {TA_MISC, DI_MC, "Memory card exploit installer", "Install a memory card system-update exploit for this console, another model or region, or PSX/DESR.", "Writes system folders to the memory card. Follow the on-screen checks carefully.", 1, MISC(Misc_Exploit_Installer), NULL},
    {TA_HEADER, -1, "SYSTEM", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_APP, "PS2 system browser", "Leave wLaunchELF and open the console's own Browser.", NULL, 1, MISC(Misc_PS2Browser), NULL},
    {TA_MISC, DI_APP, "OSDSYS", "Start the console's OSD (or the OSDSYS path configured in wLaunchELF).", NULL, 1, MISC(Misc_OSDSYS), NULL},
#ifdef ETH
    {TA_MISC, DI_NETWORK, "Network", "Start the network stack (FTP and PS2Link host access) with the configured IP settings.", NULL, 0, MISC(Misc_PS2Net), NULL},
#endif
    {TA_MISC, DI_INFO, "System information", "Console model, ROM version, boot path, loaded configuration and debug details.", NULL, 0, MISC(Misc_Debug_Info), NULL},
    {TA_MISC, DI_CHIP, "Build information", "Features and drivers included in this build of wLaunchELF.", NULL, 0, MISC(Misc_Show_Build_Info), NULL},
    {TA_MISC, DI_TEXT, "Font viewer", "Show every glyph of the active font.", NULL, 0, MISC(Misc_ShowFont), NULL},
    {TA_MISC, DI_INFO, "About wLaunchELF", "Credits and version.", NULL, 0, MISC(Misc_About_uLE), NULL},
    {TA_HEADER, -1, "CLASSIC", NULL, NULL, 0, 0, NULL},
    {TA_MISC, DI_SETTINGS, "Classic configuration", "The full wLaunchELF configuration: launch keys, skins, screen, startup, network and advanced settings.", "Saved to LAUNCHELF.CNF as usual.", 0, MISC(Misc_Configure), NULL},
    {TA_CLASSIC, DI_KEY, "Classic launch-key menu", "Leave the dashboard and use the traditional button-per-application menu.", "Choose \"Dashboard\" in that menu (or MISC/Dashboard) to come back.", 0, 0, NULL},
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
		case TA_CLASSIC:
			dashRequestExitToClassic();
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

	dashPanel(lx - 6, ly - 6, lw + 12, rows * row_h + 12, 14, DC(12, 15, 30, 150));
	dashGfxScissor(lx - 6, ly - 4, lw + 12, rows * row_h + 8);
	{
		float fy = ly + (tsel_anim - ttop) * row_h;
		dashPanel(lx, fy, lw, row_h - 3, 9, DC(60, 120, 210, 170));
		dashPanelBorder(lx, fy, lw, row_h - 3, 9, DC(170, 225, 255, 130));
	}
	for (i = ttop; i < NTOOLS && i < ttop + rows; i++) {
		float y = ly + (i - ttop) * row_h;

		if (tools[i].action == TA_HEADER) {
			dashText(lx + 8, y + 7, tools[i].name, DASH_FS_CAPTION, DASH_C_ACCENT);
			continue;
		}
		dashIcon(tools[i].icon, lx + 10, y + 5, 16, i == tsel ? DASH_C_TEXT : DASH_C_TEXT2);
		dashTextFit(lx + 34, y + 6.5f, lw - 44, tools[i].name, DASH_FS_SMALL, i == tsel ? DASH_C_TEXT : DASH_C_TEXT2);
		if (tools[i].confirm == 2 || tools[i].note)
			dashGlow(lx + lw - 14, y + 13, 4, 4, tools[i].confirm == 2 ? DASH_C_DANGER : DASH_C_WARM);
	}
	dashGfxScissorReset();
	dashDrawScrollbar(lx + lw + 10, ly, rows * row_h, ttop, rows, NTOOLS);

	{
		float dx = 372, dy = 98, dw = 228, dh = 292;

		dashPanel(dx, dy, dw, dh, 16, DC(14, 18, 36, 180));
		dashGfxBlend(DASH_BLEND_ADD);
		dashGlow(dx + dw * 0.5f, dy + 66, 80, 60, DC(80, 150, 255, 60));
		dashGfxBlend(DASH_BLEND_NORMAL);
		dashIcon(t->icon, dx + dw * 0.5f - 30, dy + 34, 60, DASH_C_TEXT);
		dashTextCenter(dx + dw * 0.5f, dy + 108, t->name, DASH_FS_BODY, DASH_C_TEXT);
		dashTextWrap(dx + 16, dy + 142, dw - 32, 4, t->desc, DASH_FS_CAPTION, DASH_C_TEXT2, 19);
		if (t->note) {
			dashPanel(dx + 12, dy + dh - 64, dw - 24, 52, 10, DC(60, 40, 20, 120));
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
