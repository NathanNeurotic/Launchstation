//--------------------------------------------------------------
//File name:   dash_info.c
//Native versions of the classic information screens: About, Build
//information, System information and the font viewer. They replace the
//classic screens whenever the dashboard is active (see dashRunMisc).
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "main_console_info.h"
#include "main_startup.h"

//--------------------------------------------------------------
// Text sheet: a scrollable list of lines in a modal panel
//--------------------------------------------------------------
enum {
	SL_TEXT = 0,
	SL_HEAD,  /* section heading (accent) */
	SL_MUTED, /* secondary detail */
	SL_GAP    /* half-height spacer */
};

typedef struct
{
	int style;
	char text[128];
} SheetLine;

#define SHEET_MAX 48

typedef struct
{
	SheetLine l[SHEET_MAX];
	int n;
} Sheet;

static void sheetAdd(Sheet *s, int style, const char *fmt, ...)
{
	va_list ap;

	if (s->n >= SHEET_MAX)
		return;
	s->l[s->n].style = style;
	va_start(ap, fmt);
	vsnprintf(s->l[s->n].text, sizeof(s->l[s->n].text), fmt ? fmt : "", ap);
	va_end(ap);
	s->n++;
}

static float lineH(int style)
{
	return style == SL_GAP ? 10.0f : style == SL_HEAD ? 24.0f : 20.0f;
}

static void sheetShow(int icon, const char *title, const char *subtitle, const Sheet *s)
{
	const float w = 520.0f, h = 340.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	const float body_y = y + (subtitle ? 74.0f : 60.0f), body_h = h - (body_y - y) - 48.0f;
	float total = 0.0f, scroll = 0.0f, scroll_target = 0.0f, open = 0.0f;
	int i;
	DashInput in;

	for (i = 0; i < s->n; i++)
		total += lineH(s->l[i].style);
	dashInputFlush();
	while (1) {
		float max_scroll = total > body_h ? total - body_h : 0.0f;

		dashInputPoll(&in);
		if (dashDashboardPending() || (in.pressed & (DASH_IN_OK | DASH_IN_BACK | DASH_IN_MENU)))
			break;
		if (in.pressed & DASH_IN_DOWN)
			scroll_target += 40.0f;
		if (in.pressed & DASH_IN_UP)
			scroll_target -= 40.0f;
		if (in.pressed & DASH_IN_R2)
			scroll_target += body_h;
		if (in.pressed & DASH_IN_L2)
			scroll_target -= body_h;
		scroll_target = dashClampf(scroll_target, 0.0f, max_scroll);
		float dt = dashVisualUpdate();
		scroll = dashApproach(scroll, scroll_target, 18.0f * dashAnimSpeed(), dt);
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		{
			/* Scroll in whole line pairs: fractional Y shimmers, and odd 1 px
			 * steps split the two interlaced fields so text ghosts (#139). */
			float oe = dashEaseOutCubic(open), ly = body_y - 2.0f * floorf(scroll * 0.5f + 0.5f);

			dashRenderScene();
			dashModalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			dashModalPanel(x, y, w, h, oe);
			dashIcon(icon, x + 24, y + 18, 24, DASH_C_ACCENT);
			dashTextS(x + 58, y + 20, title, DASH_FS_LABEL, DASH_C_TEXT);
			if (subtitle)
				dashTextFit(x + 58, y + 46, w - 82, subtitle, DASH_FS_CAPTION, DASH_C_TEXT3);
			dashGfxScissor(x + 10, body_y - 2, w - 20, body_h + 4);
			for (i = 0; i < s->n; i++) {
				const SheetLine *l = &s->l[i];
				float lh = lineH(l->style);
				if (ly + lh > body_y - 4 && ly < body_y + body_h + 4) {
					if (l->style == SL_HEAD)
						dashText(x + 28, ly + 4, l->text, DASH_FS_SMALL, DASH_C_ACCENT);
					else if (l->style != SL_GAP)
						dashTextFit(x + 28, ly + 1, w - 60, l->text, DASH_FS_SMALL,
						            l->style == SL_MUTED ? DASH_C_TEXT3 : DASH_C_TEXT2);
				}
				ly += lh;
			}
			dashGfxScissorReset();
			if (max_scroll > 0.0f)
				dashDrawScrollbar(x + w - 14, body_y, body_h, (int)scroll, (int)body_h, (int)total);
			dashModalHintRow(x + 24, y + h - 36, dashOkIcon(), localizeDashboardText("Close"), -1, NULL);
			if (max_scroll > 0.0f) /* one D-pad glyph, up/down highlighted */
				dashDrawButton(x + w - 24 - 22, y + h - 36, DI_DPAD_UD, NULL);
			dashGfxPopOffset();
			dashGfxPopAlpha();
			dashGfxPresent();
		}
	}
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
}

/* plain lines in a text sheet (empty lines become gaps) */
void dashShowTextLines(int icon, const char *title, const char *subtitle, const char *const *lines, int n)
{
	static Sheet s;
	int i;

	s.n = 0;
	for (i = 0; i < n; i++) {
		if (lines[i] == NULL || lines[i][0] == '\0')
			sheetAdd(&s, SL_GAP, NULL);
		else
			sheetAdd(&s, SL_TEXT, "%s", lines[i]);
	}
	sheetShow(icon, title, subtitle, &s);
}

//--------------------------------------------------------------
// About
//--------------------------------------------------------------
void dashShowAbout(void)
{
	static Sheet s;

	s.n = 0;
	sheetAdd(&s, SL_MUTED, "commit %s", GIT_HASH);
	sheetAdd(&s, SL_GAP, NULL);
	sheetAdd(&s, SL_HEAD, "This build");
	sheetAdd(&s, SL_TEXT, "Maintained by NathanNeurotic / Ripto");
	sheetAdd(&s, SL_HEAD, "Direct base: wLaunchELF_R3Z");
	sheetAdd(&s, SL_TEXT, "R3Z3N / Saildot4K");
	sheetAdd(&s, SL_TEXT, "github.com/saildot4k/wLaunchELF_R3Z");
	sheetAdd(&s, SL_HEAD, "Fork creator");
	sheetAdd(&s, SL_TEXT, "johnson-cooper");
	sheetAdd(&s, SL_TEXT, "github.com/johnson-cooper");
	sheetAdd(&s, SL_TEXT, "Created this fork and handed it to Ripto");
	sheetAdd(&s, SL_TEXT, "to maintain and continue improving");
	sheetAdd(&s, SL_TEXT, "DS3/DS4 support by Alex Parrado");
	sheetAdd(&s, SL_TEXT, "Thanks to TnA for all his insight and support");
	sheetAdd(&s, SL_HEAD, "PC CARD CompactFlash reference");
	sheetAdd(&s, SL_TEXT, "mihhru - PS2CF");
	sheetAdd(&s, SL_TEXT, "github.com/mihhru/PS2CF");
	sheetAdd(&s, SL_HEAD, "wLaunchELF");
	sheetAdd(&s, SL_TEXT, "Project maintainers: sp193 & AKuHAK");
	sheetAdd(&s, SL_HEAD, "uLaunchELF");
	sheetAdd(&s, SL_TEXT, "Eric Price (aka 'E P')");
	sheetAdd(&s, SL_TEXT, "Ronald Andersson (aka 'dlanor')");
	sheetAdd(&s, SL_HEAD, "Other contributors");
	sheetAdd(&s, SL_TEXT, "techwritescode");
	sheetAdd(&s, SL_TEXT, "Polo35, radad, Drakonite, sincro");
	sheetAdd(&s, SL_TEXT, "kthu, Slam-Tilt, chip, pixel, Hermes, PCM720");
	sheetAdd(&s, SL_TEXT, "and others in the PS2Dev community");
	sheetAdd(&s, SL_HEAD, "Releases");
	sheetAdd(&s, SL_TEXT, "github.com/ps2homebrew/wLaunchELF/releases");
	sheetAdd(&s, SL_TEXT, "github.com/NathanNeurotic/Launchstation/releases");
	sheetAdd(&s, SL_GAP, NULL);
	sheetAdd(&s, SL_MUTED, "Ancestral project: LaunchELF v3.41 by Mirakichi");
	sheetAdd(&s, SL_MUTED, "Dashboard text: Roboto (Apache License 2.0)");
	{
		char title[64];
		snprintf(title, sizeof(title), "Launchstation %s", ULE_VERSION);
		sheetShow(DI_INFO, title, ULE_VERDATE, &s);
	}
}

/* Console region from the rom0:ROMVER region letter (5th character). */
static const char *consoleRegionName(char letter)
{
	switch (letter) {
		case 'J': return "Japan (NTSC-J)";
		case 'A': return "Americas (NTSC-U/C)";
		case 'H': return "Asia (NTSC-J)";
		case 'E': return "Europe / Oceania (PAL)";
		case 'C': return "China";
		default: return "unknown";
	}
}

//--------------------------------------------------------------
// Build information
//--------------------------------------------------------------
static void feature(Sheet *s, const char *name, int on)
{
	sheetAdd(s, SL_TEXT, "%-12s %s", name, on ? "yes" : "no");
}

void dashShowBuildInfo(void)
{
	static Sheet s;

	s.n = 0;
	sheetAdd(&s, SL_HEAD, "Devices and features");
#ifdef ETH
	feature(&s, "Network (ETH)", 1);
#else
	feature(&s, "Network (ETH)", 0);
#endif
#ifdef UDPFS
	feature(&s, "UDPFS", 1);
#else
	feature(&s, "UDPFS", 0);
#endif
#ifdef XFROM
	feature(&s, "PSX flash", 1);
#else
	feature(&s, "PSX flash", 0);
#endif
#ifdef DVRP
	feature(&s, "PSX DVR HDD", 1);
#else
	feature(&s, "PSX DVR HDD", 0);
#endif
#ifdef EXFAT
	feature(&s, "exFAT HDD", 1);
#else
	feature(&s, "exFAT HDD", 0);
#endif
#ifdef MX4SIO
	feature(&s, "MX4SIO", 1);
#else
	feature(&s, "MX4SIO", 0);
#endif
#ifdef ILINK
	feature(&s, "iLink", 1);
#else
	feature(&s, "iLink", 0);
#endif
#ifdef MMCE
	feature(&s, "MMCE", 1);
#else
	feature(&s, "MMCE", 0);
#endif
#ifdef DS34
	feature(&s, "DS3/DS4 pads", 1);
#else
	feature(&s, "DS3/DS4 pads", 0);
#endif
#if defined(UDPTTY) || defined(SIO_DEBUG) || defined(POWERPC_UART) || defined(NO_IOP_RESET)
	sheetAdd(&s, SL_HEAD, "Debug features");
#ifdef NO_IOP_RESET
	feature(&s, "IOP reset", 0);
#else
	feature(&s, "IOP reset", 1);
#endif
#ifdef UDPTTY
	feature(&s, "UDPTTY", 1);
#endif
#ifdef SIO_DEBUG
	feature(&s, "SIO debug", 1);
#endif
#ifdef POWERPC_UART
	feature(&s, "PPC UART", 1);
#endif
#endif
	sheetAdd(&s, SL_GAP, NULL);
	sheetAdd(&s, SL_MUTED, "github.com/NathanNeurotic/Launchstation/releases");
	{
		char sub[64];
		snprintf(sub, sizeof(sub), "Launchstation %s (%s)", ULE_VERSION, GIT_HASH);
		sheetShow(DI_CHIP, "Build information", sub, &s);
	}
}

//--------------------------------------------------------------
// System information. The console model and DVD version come from the
// CDVD processor and may load IOP modules (CDVD, rom0:ADDDRV), which is
// only done on the UI thread: never on the background worker.
//--------------------------------------------------------------
typedef struct
{
	char model[CONSOLE_MODEL_NAME_MAX_LEN + 1];
	char dvd[64];
	unsigned short rom_version;
} SysInfo;

/* Hardware identity is read once at startup, before the dashboard worker
 * starts, so normal frames only format cached text. */
static SysInfo cached_console_info;
static int console_identity_initialized;

void dashConsoleIdentityInit(void)
{
	if (console_identity_initialized)
		return;
	console_identity_initialized = 1;
	if (ROMVER_data[0] == '\0')
		uLE_InitializeRegion();
	GetConsoleModelName(ROMVER_data, cached_console_info.model, sizeof(cached_console_info.model));
	if (!strcmp(cached_console_info.model, "Unknown"))
		cached_console_info.model[0] = '\0';
	GetConsoleDvdVersion(cached_console_info.dvd, sizeof(cached_console_info.dvd));
	cached_console_info.rom_version = getROMVersion();
}

void dashConsoleSummaryText(char *out, size_t out_size)
{
	const char *identity;
	const char *region;
	char rom[24];

	if (out == NULL || out_size == 0)
		return;
	identity = cached_console_info.model[0] ? cached_console_info.model : (console_is_PSX ? "PSX" : "PS2");
	region = strlen(ROMVER_data) >= 5 ? consoleRegionName(ROMVER_data[4]) : "region unknown";
	if (strlen(ROMVER_data) >= 5)
		snprintf(rom, sizeof(rom), "ROM: %c.%c%c%c", ROMVER_data[1], ROMVER_data[2], ROMVER_data[3], ROMVER_data[4]);
	else
		snprintf(rom, sizeof(rom), "ROM: unavailable");
	snprintf(out, out_size, "%s | %s | %s | %s", identity, region, rom,
	         cached_console_info.dvd[0] ? cached_console_info.dvd : "DVD: unavailable");
}

void dashShowSystemInfo(void)
{
	static Sheet s;
	static SysInfo job;
	const MainExecuteContext *ctx = dashExecContext();
	int i;

	if (!dashWorkerAwaitAll("Finishing up"))
		return;
	dashConsoleIdentityInit();
	job = cached_console_info;

	s.n = 0;
	sheetAdd(&s, SL_HEAD, "Console");
	sheetAdd(&s, SL_TEXT, "Model: %s", job.model[0] ? job.model : "unknown");
	sheetAdd(&s, SL_TEXT, "ROM version: %s", ctx->romver_data[0] ? ctx->romver_data : "unavailable");
	if (job.dvd[0])
		sheetAdd(&s, SL_TEXT, "DVD player: %s", job.dvd);
	/* ROMVER[4] is the console's real region. rough_region is only the
	 * memory-card system-folder letter (BxEXEC), which USA and Asia share. */
	sheetAdd(&s, SL_TEXT, "Region: %s", consoleRegionName(ctx->romver_data[0] ? ctx->romver_data[4] : '\0'));
	sheetAdd(&s, SL_HEAD, "System update");
	sheetAdd(&s, SL_TEXT, "Main KELF: %s",
	         console_is_PSX ? "BIEXEC-SYSTEM/xosdmain.elf" :
	         (ctx->default_osdsys_path2 && strchr(ctx->default_osdsys_path2, '/')) ? strchr(ctx->default_osdsys_path2, '/') + 1 : "?");
	if (job.rom_version < 0x230 && job.rom_version > 0x130)
		sheetAdd(&s, SL_TEXT, "Specific KELF: B%cEXEC-SYSTEM/osd%03x.elf", ctx->rough_region, (job.rom_version + 10) & ~0x0F);
	sheetAdd(&s, SL_HEAD, "Launchstation");
	sheetAdd(&s, SL_TEXT, "Started from: %s", ctx->boot_path ? ctx->boot_path : "");
	sheetAdd(&s, SL_TEXT, "Folder: %s", LaunchElfDir);
	sheetAdd(&s, SL_TEXT, "Configuration: %s", LoadedConfigPath[0] ? LoadedConfigPath : "defaults (not loaded from a file)");
	sheetAdd(&s, SL_TEXT, "Dashboard files: %s", dashStoreDir());
	sheetAdd(&s, SL_HEAD, "Boot arguments");
	sheetAdd(&s, SL_TEXT, "argc = %d", ctx->boot_argc);
	for (i = 0; i < ctx->boot_argc && i < 8; i++)
		sheetAdd(&s, SL_MUTED, "argv[%d] = %s", i, ctx->boot_argv[i]);
	sheetShow(DI_INFO, "System information", NULL, &s);
}

//--------------------------------------------------------------
// Font viewer: every glyph of the wLaunchELF font (built-in or .fnt)
//--------------------------------------------------------------
static u64 gsColor(DashColor c)
{
	return GS_SETREG_RGBAQ(c.r, c.g, c.b, 0x80, 0x00);
}

void dashShowFontViewer(void)
{
	static const char hex[] = "0123456789ABCDEF";
	const float cell_w = 26.0f, cell_h = 21.0f;
	const float w = 17 * cell_w + 48.0f, h = 17 * cell_h + 100.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	const float gx = x + 24, gy = y + 56;
	float open = 0.0f;
	int r, c;
	DashInput in;

	dashFontInit(); /* picks up a .fnt chosen in Settings meanwhile */
	dashInputFlush();
	while (1) {
		float sy = dashGfxScaleY();

		dashInputPoll(&in);
		if (dashDashboardPending() || (in.pressed & (DASH_IN_OK | DASH_IN_BACK | DASH_IN_MENU)))
			break;
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dashVisualUpdate());
		dashRenderScene();
		dashModalBackdrop(dashEaseOutCubic(open));
		dashModalPanel(x, y, w, h, 1.0f);
		dashIcon(DI_TEXT, x + 24, y + 18, 22, DASH_C_ACCENT);
		dashTextS(x + 56, y + 18, localizeDashboardText("Font viewer"), DASH_FS_LABEL, DASH_C_TEXT);
		dashTextRight(x + w - 24, y + 22, setting->font_file[0] ? setting->font_file : "built-in font", DASH_FS_CAPTION, DASH_C_TEXT3);
		for (r = 0; r <= 16; r++) {
			for (c = 0; c <= 16; c++) {
				float cx = gx + c * cell_w, cy = gy + r * cell_h;
				char label[2] = {0, 0};

				if (r == 0 && c == 0)
					continue;
				if (r == 0 || c == 0) {
					label[0] = hex[(r == 0) ? c - 1 : r - 1];
					dashTextCenter(cx + cell_w * 0.5f, cy + 2, label, DASH_FS_CAPTION, DASH_C_ACCENT);
					continue;
				}
				if (((r + c) & 1) == 0)
					dashRect(cx, cy, cell_w, cell_h, dashColorA(DASH_C_TEXT, 0.05f));
				/* the legacy glyph renderer works in framebuffer pixels */
				dashSpriteBatchEnd();
				dashFontLegacyChar((unsigned)((r - 1) * 16 + (c - 1)), (int)(cx + (cell_w - 8) * 0.5f),
				                   (int)((cy + 3) * sy), gsColor(DASH_C_TEXT));
			}
		}
		dashModalHintRow(x + 24, y + h - 36, dashOkIcon(), localizeDashboardText("Close"), -1, NULL);
		dashGfxPresent();
	}
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_info.c
//--------------------------------------------------------------
