//--------------------------------------------------------------
//File name:   riptopl_settings.c
//Per-game RiptOPL settings edited from Launchstation: which loader starts the game, and every
//pad-emulator option RiptOPL's Controller Emulation dialog offers. One flat list, no submenus.
//
//Values live in the game's CFG file, I:\CFG\<Game ID>.cfg on its device, as "$Key=value" lines.
//Only the keys below are rewritten. Every other line is kept, and the old file is copied to .bak first.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "neutrino_gameid.h"
#include "riptopl_iso.h"
#include "popstarter_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_MAX 8192

/* Keys this module owns. Anything else in the CFG is copied through untouched. */
#define KEY_LOADER     "LaunchstationLoader" /* RIPTOPL, or absent for Neutrino */
#define KEY_PAD_ENABLE "EnablePadEmu"
#define KEY_PAD        "PadEmuSettings"
#define KEY_MACRO      "PadMacroSettings"
/* RiptOPL reads a game's pad settings only when these say "per game" (1). Without them it silently uses
 * its own global defaults, and the keys above are ignored. */
#define KEY_PAD_SOURCE   "PADEMUSource"
#define KEY_MACRO_SOURCE "PadMacroSource"

/* Bit positions, as RiptOPL's guigame.cpp sets them. */
#define PAD_MODE_BT      (1u << 0)  /* 0 = USB, 1 = Bluetooth */
#define PAD_PORT_EMU(p)  (1u << (8 + (p)))  /* p = 0 port 1, 1 port 2 */
#define PAD_PORT_VIB(p)  (1u << (16 + (p)))
#define PAD_MTAP         (1u << 24)
#define PAD_MTAP_PORT    (1u << 25)  /* 0 = multitap on port 1, 1 = on port 2 */
#define PAD_WORKAROUND   (1u << 26)
#define PAD_MTAP_ON_MASK 0x00000E00u /* RiptOPL sets these while multitap is on */
#define PAD_MTAP_OFF_MASK 0xFFFF03FFu /* ...and clears them when it is off */

#define MAC_LSLOW_AMT_SHIFT 0  /* 4 bits */
#define MAC_RSLOW_AMT_SHIFT 4  /* 4 bits */
#define MAC_LSLOW_EN   (1u << 8)
#define MAC_LSLOW_TOG  (1u << 9)
#define MAC_RSLOW_EN   (1u << 10)
#define MAC_RSLOW_TOG  (1u << 11)
#define MAC_LX_INV     (1u << 12)
#define MAC_LY_INV     (1u << 13)
#define MAC_RX_INV     (1u << 14)
#define MAC_RY_INV     (1u << 15)
#define MAC_TURBO_SHIFT 16 /* 2 bits */

typedef struct
{
	int loader_riptopl;
	int pad_enable;
	unsigned int pad;
	unsigned int macro;
} RiptoplGameSettings;

enum {
	I_LOADER, I_PAD_ENABLE, I_MODE, I_P1_EMU, I_P2_EMU, I_P1_VIB, I_P2_VIB,
	I_MTAP, I_MTAP_PORT, I_WORKAROUND,
	I_LSLOW_AMT, I_RSLOW_AMT, I_LSLOW_EN, I_LSLOW_TOG, I_RSLOW_EN, I_RSLOW_TOG,
	I_LX_INV, I_LY_INV, I_RX_INV, I_RY_INV, I_TURBO,
	I_SAVE, I_CANCEL, I_COUNT
};

static const char *onoff(int v) { return v ? "On" : "Off"; }

/* Set while the dialog edits a disc. The loader row is then fixed: a disc always starts with RiptOPL. */
static int sDisc;
#define DISC_DEFAULT_PAD (PAD_PORT_EMU(0) | PAD_PORT_VIB(0)) /* port 1 from USB, vibration on port 1 */

/* Reads "$key=value" from the CFG text. Returns 1 when the key is present. */
static int cfgInt(const char *text, const char *key, unsigned int *out)
{
	const char *p = text;
	size_t kl = strlen(key);

	while (*p) {
		if (p[0] == '$' && !strncmp(p + 1, key, kl) && p[1 + kl] == '=') {
			*out = (unsigned int)strtoul(p + 2 + kl, NULL, 0);
			return 1;
		}
		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			p++;
	}
	return 0;
}


/* Loads the file into buf. Returns 1 when it exists, 0 when it does not, -1 on an unreadable file. */
static int readCfg(const char *path, char *buf, int cap)
{
	int fd, len;

	fd = genOpen(path, FIO_O_RDONLY);
	if (fd < 0)
		return 0;
	len = (int)genLseek(fd, 0, SEEK_END);
	if (len < 0 || len >= cap) {
		genClose(fd);
		return -1;
	}
	genLseek(fd, 0, SEEK_SET);
	if (len > 0 && genRead(fd, buf, len) != len) {
		genClose(fd);
		return -1;
	}
	genClose(fd);
	buf[len] = '\0';
	return 1;
}

static int writeFile(const char *path, const char *data, int len)
{
	int fd = genOpen(path, FIO_O_CREAT | FIO_O_WRONLY | FIO_O_TRUNC);

	if (fd < 0)
		return 0;
	if (genWrite(fd, (void *)data, len) != len) {
		genClose(fd);
		return 0;
	}
	genClose(fd);
	return 1;
}

static int isManagedLine(const char *line, size_t len)
{
	static const char *const keys[] = {KEY_LOADER, KEY_PAD_ENABLE, KEY_PAD, KEY_MACRO, KEY_PAD_SOURCE, KEY_MACRO_SOURCE};
	size_t i;

	if (len < 2 || line[0] != '$')
		return 0;
	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		size_t kl = strlen(keys[i]);
		if (len > kl + 1 && !strncmp(line + 1, keys[i], kl) && line[1 + kl] == '=')
			return 1;
	}
	return 0;
}

static void loadSettings(const char *text, RiptoplGameSettings *s)
{
	unsigned int v;

	memset(s, 0, sizeof(*s));
	s->loader_riptopl = (text != NULL && cfgInt(text, KEY_LOADER, &v) && v == 1);
	s->pad_enable = (text != NULL && cfgInt(text, KEY_PAD_ENABLE, &v)) ? (int)v : 0;
	s->pad = (text != NULL && cfgInt(text, KEY_PAD, &v)) ? v : 0;
	s->macro = (text != NULL && cfgInt(text, KEY_MACRO, &v)) ? v : 0;
}

/* Builds the new file: the unmanaged lines in their order, then the managed keys, then $End when the
 * old file had it. Returns the length, or -1 when it does not fit. */
static int buildCfg(const char *old, const RiptoplGameSettings *s, char *out, int cap)
{
	const char *p = old;
	int n = 0, i;
	int had_end = 0;
	char line[192];

	while (*p) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		if (len == 0 && !eol)
			break;
		if (!strncmp(p, "$End=", 5))
			had_end = 1;
		else if (!isManagedLine(p, len)) {
			if (n + (int)len + 1 >= cap)
				return -1;
			memcpy(out + n, p, len);
			n += (int)len;
			out[n++] = '\n';
		}
		if (!eol)
			break;
		p = eol + 1;
	}

	i = snprintf(line, sizeof(line), "$%s=1\n$%s=%d\n$%s=%u\n$%s=1\n$%s=%u\n$%s=%d\n", KEY_PAD_SOURCE, KEY_PAD_ENABLE,
	             s->pad_enable, KEY_PAD, s->pad, KEY_MACRO_SOURCE, KEY_MACRO, s->macro, KEY_LOADER, s->loader_riptopl ? 1 : 0);
	if (i <= 0 || n + i >= cap)
		return -1;
	memcpy(out + n, line, i);
	n += i;
	if (had_end) {
		if (n + 9 >= cap)
			return -1;
		memcpy(out + n, "$End=1\n", 7);
		n += 7;
	}
	out[n] = '\0';
	return n;
}

/* The CFG file for a game: <device root>CFG/<Game ID>.cfg. Returns 0 when the path is not on a device. */
static int cfgPathFor(const char *iso_path, const char *gameID, char *out, size_t cap)
{
	const char *colon = strchr(iso_path, ':');
	int root_len;

	if (colon == NULL || colon[1] != '/')
		return 0;
	root_len = (int)(colon - iso_path) + 2; /* "dev:/" */
	if (root_len >= (int)cap)
		return 0;
	return snprintf(out, cap, "%.*sCFG/%s.cfg", root_len, iso_path, gameID) < (int)cap;
}

/* Starts the game with RiptOPL when its CFG says so. Anything unreadable falls back to Neutrino. */
int riptoplGameUsesRiptopl(const char *iso_path)
{
	char gameID[NEUTRINO_GAME_ID_SIZE], cfg[MAX_PATH], text[CFG_MAX];
	RiptoplGameSettings s;

	if (!IsNeutrinoIsoPath(iso_path))
		return 0;
	if (!prepareNeutrinoGameID(iso_path, gameID, sizeof(gameID)))
		return 0;
	if (!cfgPathFor(iso_path, gameID, cfg, sizeof(cfg)))
		return 0;
	if (readCfg(cfg, text, sizeof(text)) != 1)
		return 0;
	loadSettings(text, &s);
	return s.loader_riptopl;
}

/* Defaults for a disc with no saved pad settings: the setup that was tested on hardware. */
static void discDefaults(RiptoplGameSettings *s)
{
	memset(s, 0, sizeof(*s));
	s->loader_riptopl = 1;
	s->pad_enable = 1;
	s->pad = DISC_DEFAULT_PAD;
}

/* The disc's CFG lives on the device RIPTOPL.ELF is on, in the same CFG folder the ISOs use. */
static int discCfgPath(const char *game_id, char *out, size_t cap)
{
	char elf[MAX_PATH];

	if (game_id == NULL || game_id[0] == '\0' || !RiptoplFindElf(elf, sizeof(elf)))
		return 0;
	return cfgPathFor(elf, game_id, out, cap);
}

/* The single argument RiptOPL gets for a disc: "disc,<on>,<PadEmuSettings>,<PadMacroSettings>". */
int riptoplDiscArgLine(const char *game_id, char *out, size_t cap)
{
	char cfg[MAX_PATH], text[CFG_MAX];
	RiptoplGameSettings s;
	unsigned int v;

	if (discCfgPath(game_id, cfg, sizeof(cfg)) && readCfg(cfg, text, sizeof(text)) == 1 && cfgInt(text, KEY_PAD_ENABLE, &v))
		loadSettings(text, &s);
	else
		discDefaults(&s);
	return snprintf(out, cap, "disc,%d,%u,%u", s.pad_enable ? 1 : 0, s.pad, s.macro) < (int)cap;
}

static void setPadBit(unsigned int *word, unsigned int bit, int on)
{
	if (on)
		*word |= bit;
	else
		*word &= ~bit;
}

static void toggle(RiptoplGameSettings *s, int item)
{
	unsigned int amt;

	switch (item) {
	case I_LOADER:      if (!sDisc) s->loader_riptopl = !s->loader_riptopl; break;
	case I_PAD_ENABLE:  s->pad_enable = !s->pad_enable; break;
	case I_MODE:        s->pad ^= PAD_MODE_BT; break;
	case I_P1_EMU:      s->pad ^= PAD_PORT_EMU(0); break;
	case I_P2_EMU:      s->pad ^= PAD_PORT_EMU(1); break;
	case I_P1_VIB:      s->pad ^= PAD_PORT_VIB(0); break;
	case I_P2_VIB:      s->pad ^= PAD_PORT_VIB(1); break;
	case I_MTAP:
		s->pad ^= PAD_MTAP;
		if (s->pad & PAD_MTAP)
			s->pad |= PAD_MTAP_ON_MASK;
		else
			s->pad &= PAD_MTAP_OFF_MASK;
		break;
	case I_MTAP_PORT:   s->pad ^= PAD_MTAP_PORT; break;
	case I_WORKAROUND:  s->pad ^= PAD_WORKAROUND; break;
	case I_LSLOW_AMT:
		amt = (s->macro >> MAC_LSLOW_AMT_SHIFT) & 0xF;
		s->macro = (s->macro & ~(0xFu << MAC_LSLOW_AMT_SHIFT)) | (((amt + 1) & 0xF) << MAC_LSLOW_AMT_SHIFT);
		break;
	case I_RSLOW_AMT:
		amt = (s->macro >> MAC_RSLOW_AMT_SHIFT) & 0xF;
		s->macro = (s->macro & ~(0xFu << MAC_RSLOW_AMT_SHIFT)) | (((amt + 1) & 0xF) << MAC_RSLOW_AMT_SHIFT);
		break;
	case I_LSLOW_EN:    s->macro ^= MAC_LSLOW_EN; break;
	case I_LSLOW_TOG:   s->macro ^= MAC_LSLOW_TOG; break;
	case I_RSLOW_EN:    s->macro ^= MAC_RSLOW_EN; break;
	case I_RSLOW_TOG:   s->macro ^= MAC_RSLOW_TOG; break;
	case I_LX_INV:      s->macro ^= MAC_LX_INV; break;
	case I_LY_INV:      s->macro ^= MAC_LY_INV; break;
	case I_RX_INV:      s->macro ^= MAC_RX_INV; break;
	case I_RY_INV:      s->macro ^= MAC_RY_INV; break;
	case I_TURBO:
		amt = (s->macro >> MAC_TURBO_SHIFT) & 0x3;
		s->macro = (s->macro & ~(0x3u << MAC_TURBO_SHIFT)) | (((amt + 1) & 0x3) << MAC_TURBO_SHIFT);
		break;
	default: break;
	}
}

/* Text shown on each row, from the current values. Written into the caller's buffers. */
static void describe(const RiptoplGameSettings *s, int item, char *label, size_t lcap, char *detail, size_t dcap)
{
	static const char *const labels[I_COUNT] = {
	    "Start with", "Pad emulator", "Pad emulator mode", "Port 1 emulation", "Port 2 emulation",
	    "Port 1 vibration", "Port 2 vibration", "Multitap emulation", "Multitap on port", "Fake DS3 workaround",
	    "Left stick slowdown", "Right stick slowdown", "Left slowdown enable", "Left slowdown toggle",
	    "Right slowdown enable", "Right slowdown toggle", "Invert left stick X", "Invert left stick Y",
	    "Invert right stick X", "Invert right stick Y", "Turbo speed",
	    "Save", "Cancel"};

	snprintf(label, lcap, "%s", labels[item]);
	detail[0] = '\0';
	switch (item) {
	case I_LOADER:      snprintf(detail, dcap, "%s", sDisc ? "RiptOPL (discs)" : (s->loader_riptopl ? "RiptOPL" : "Neutrino")); break;
	case I_PAD_ENABLE:  snprintf(detail, dcap, "%s", onoff(s->pad_enable)); break;
	case I_MODE:        snprintf(detail, dcap, "%s", (s->pad & PAD_MODE_BT) ? "Bluetooth" : "USB"); break;
	case I_P1_EMU:      snprintf(detail, dcap, "%s", onoff(s->pad & PAD_PORT_EMU(0))); break;
	case I_P2_EMU:      snprintf(detail, dcap, "%s", onoff(s->pad & PAD_PORT_EMU(1))); break;
	case I_P1_VIB:      snprintf(detail, dcap, "%s", onoff(s->pad & PAD_PORT_VIB(0))); break;
	case I_P2_VIB:      snprintf(detail, dcap, "%s", onoff(s->pad & PAD_PORT_VIB(1))); break;
	case I_MTAP:        snprintf(detail, dcap, "%s", onoff(s->pad & PAD_MTAP)); break;
	case I_MTAP_PORT:   snprintf(detail, dcap, "port %d", (s->pad & PAD_MTAP_PORT) ? 2 : 1); break;
	case I_WORKAROUND:  snprintf(detail, dcap, "%s", onoff(s->pad & PAD_WORKAROUND)); break;
	case I_LSLOW_AMT:   snprintf(detail, dcap, "%u", (s->macro >> MAC_LSLOW_AMT_SHIFT) & 0xF); break;
	case I_RSLOW_AMT:   snprintf(detail, dcap, "%u", (s->macro >> MAC_RSLOW_AMT_SHIFT) & 0xF); break;
	case I_LSLOW_EN:    snprintf(detail, dcap, "%s", onoff(s->macro & MAC_LSLOW_EN)); break;
	case I_LSLOW_TOG:   snprintf(detail, dcap, "%s", onoff(s->macro & MAC_LSLOW_TOG)); break;
	case I_RSLOW_EN:    snprintf(detail, dcap, "%s", onoff(s->macro & MAC_RSLOW_EN)); break;
	case I_RSLOW_TOG:   snprintf(detail, dcap, "%s", onoff(s->macro & MAC_RSLOW_TOG)); break;
	case I_LX_INV:      snprintf(detail, dcap, "%s", onoff(s->macro & MAC_LX_INV)); break;
	case I_LY_INV:      snprintf(detail, dcap, "%s", onoff(s->macro & MAC_LY_INV)); break;
	case I_RX_INV:      snprintf(detail, dcap, "%s", onoff(s->macro & MAC_RX_INV)); break;
	case I_RY_INV:      snprintf(detail, dcap, "%s", onoff(s->macro & MAC_RY_INV)); break;
	case I_TURBO:       snprintf(detail, dcap, "%u", (s->macro >> MAC_TURBO_SHIFT) & 0x3); break;
	default: break;
	}
}

/* Opens the per-game settings list for a game. Save rewrites only the managed keys; Cancel changes nothing. */
void dashRiptoplSettings(const char *iso_path, const char *title)
{
	char gameID[NEUTRINO_GAME_ID_SIZE], cfg[MAX_PATH];
	char text[CFG_MAX], out[CFG_MAX];
	static char labels[I_COUNT][48], details[I_COUNT][40];
	DashMenuItem items[I_COUNT];
	RiptoplGameSettings s;
	int exists, sel = 0, r, i, len;

	if (sDisc) {
		/* iso_path carries the disc's Game ID here. */
		snprintf(gameID, sizeof(gameID), "%s", iso_path);
		if (!discCfgPath(gameID, cfg, sizeof(cfg))) {
			dashModalMessage(DI_WARNING, title, "RIPTOPL.ELF was not found, so the disc's settings have nowhere to go.", "Set its path in Settings > Disc & image loaders.");
			return;
		}
	} else {
		if (!IsNeutrinoIsoPath(iso_path))
			return;
		if (!prepareNeutrinoGameID(iso_path, gameID, sizeof(gameID)) || !cfgPathFor(iso_path, gameID, cfg, sizeof(cfg))) {
			dashModalMessage(DI_WARNING, title, "This game has no readable Game ID, so its RiptOPL settings cannot be saved.", NULL);
			return;
		}
	}
	exists = readCfg(cfg, text, sizeof(text));
	if (exists < 0) {
		dashModalMessage(DI_WARNING, title, "The game's CFG file could not be read.", cfg);
		return;
	}
	loadSettings(exists == 1 ? text : NULL, &s);
	if (sDisc) {
		unsigned int v;
		int loader = 1; /* no CFG yet: RiptOPL, which is what a disc always uses */

		/* The ISO of the same game shares this file. Its loader choice is not the disc's to change. */
		if (exists == 1 && cfgInt(text, KEY_LOADER, &v))
			loader = (v == 1);
		if (!(exists == 1 && cfgInt(text, KEY_PAD_ENABLE, &v)))
			discDefaults(&s);
		s.loader_riptopl = loader;
	} else if (exists != 1) {
		/* A game with no CFG yet: the pad emulator starts on, port 1 from USB, so choosing RiptOPL is enough. */
		s.pad_enable = 1;
		s.pad = DISC_DEFAULT_PAD;
	}

	for (;;) {
		memset(items, 0, sizeof(items));
		for (i = 0; i < I_COUNT; i++) {
			describe(&s, i, labels[i], sizeof(labels[i]), details[i], sizeof(details[i]));
			items[i].label = labels[i];
			items[i].detail = details[i];
			items[i].icon = (i == I_SAVE || i == I_CANCEL) ? DI_INFO : DI_SETTINGS;
		}
		r = dashModalMenu("RiptOPL settings", title, items, I_COUNT, sel);
		if (r < 0 || r == I_CANCEL)
			return;
		if (r == I_SAVE)
			break;
		toggle(&s, r);
		sel = r;
	}

	/* Keep a copy of the file as it was before this write. */
	if (exists == 1) {
		char bak[MAX_PATH];
		snprintf(bak, sizeof(bak), "%s.bak", cfg);
		if (!writeFile(bak, text, (int)strlen(text))) {
			dashModalMessage(DI_WARNING, title, "Could not write the backup of the CFG, so nothing was saved.", bak);
			return;
		}
	}
	len = buildCfg(exists == 1 ? text : "", &s, out, sizeof(out));
	if (len < 0) {
		dashModalMessage(DI_WARNING, title, "The settings do not fit in the game's CFG file.", NULL);
		return;
	}
	if (sDisc) {
		/* The CFG folder may not exist on a memory card yet. */
		char dir[MAX_PATH];
		char *slash;

		snprintf(dir, sizeof(dir), "%s", cfg);
		slash = strrchr(dir, '/');
		if (slash != NULL) {
			*slash = '\0';
			genMkdir(dir, 0777);
		}
	}
	if (!writeFile(cfg, out, len)) {
		dashModalMessage(DI_WARNING, title, "The CFG file could not be written. Check that its CFG folder exists.", cfg);
		return;
	}
	dashModalMessage(DI_INFO, title, "RiptOPL settings saved for this game.", cfg);
}
/* The same list for the disc in the drive, keyed by its Game ID. */
void dashRiptoplDiscSettings(const char *game_id, const char *title)
{
	sDisc = 1;
	dashRiptoplSettings(game_id, title);
	sDisc = 0;
}
//--------------------------------------------------------------
//End of file: riptopl_settings.c
//--------------------------------------------------------------
