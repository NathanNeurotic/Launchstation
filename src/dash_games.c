//--------------------------------------------------------------
//File name:   dash_games.c
//GAMES: PS2 ISO images in the OPL folders (<root>/DVD, <root>/CD) of every
//running device Neutrino can start from, with OPL cover art
//(<root>/ART/<GAME ID>_COV.png or .jpg, see dash_art.c). Starting one takes
//the same Neutrino route as the File Browser, so the per-game Neutrino
//options saved there with "Save for this game" apply here too (issue #391).
//PS1 games are listed the way RiptOPL lists them: <root>/POPS/*.VCD for
//POPStarter (on the devices POPStarter starts from) and <root>/EMBER/games/
//<Game>/ for Ember (.cue, else .exe, else .bin: Ember's own order).
//The list is built on first visit and by Refresh; nothing is cached on disk.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "filer_shared.h"
#include "init.h"
#include "neutrino_gameid.h"
#include "popstarter_internal.h"
#include "gui_neutrino.h"

enum { GAME_PS2, GAME_VCD, GAME_EMBER };
#define EMBER_GAMES_MAX 256

#define GAMES_MAX DASH_MAX_APPS
#define GAME_ROOTS 24
#define GAME_SHARES 16
#define LIST_ROWS 9
#define LIST_ROW_H 31.0f

typedef struct
{
	char roots[GAME_ROOTS][24];
	int nroots;
	DashApp *games;
	int n, cap;
	FILEINFO *files;
} GameScan;

static DashApp *games;
static int ngames;
static int order[GAMES_MAX];
static int norder;
static int sel;
static float sel_anim;
static float list_top_anim = -1.0f;
static int scanned;
static GameScan *scan_job, *scan_result;

static int titleCmp(const void *pa, const void *pb)
{
	return stricmp(((const DashApp *)pa)->title, ((const DashApp *)pb)->title);
}

/* OPL's older file naming, "SLUS_123.45.Title.iso", carries the game ID. */
static int nameGameId(const char *name, char *id)
{
	int i;

	for (i = 0; i < 11; i++) {
		char c = name[i];
		if (i < 4 ? !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) :
		    i == 4 ? c != '_' : i == 8 ? c != '.' : (c < '0' || c > '9'))
			return 0;
	}
	if (name[11] != '.')
		return 0;
	for (i = 0; i < 11; i++) /* profiles and OPL covers use upper-case IDs */
		id[i] = (name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i];
	id[11] = '\0';
	return 1;
}

//--------------------------------------------------------------
// Scanning (background worker)
//--------------------------------------------------------------
/* title: the name shown (an Ember game is named by its folder), else name. */
static void addGame(GameScan *s, const char *dir, const char *name, const char *title, int kind)
{
	char base[DASH_PATH_MAX], *dot;
	DashApp *g;
	int skip;

	if (title == NULL)
		title = name;

	if (s->n >= GAMES_MAX || strlen(dir) + strlen(name) >= DASH_PATH_MAX)
		return;
	if (s->n == s->cap) {
		DashApp *grown = (DashApp *)realloc(s->games, sizeof(DashApp) * (s->cap + 32));
		if (grown == NULL)
			return;
		s->games = grown;
		s->cap += 32;
	}
	g = &s->games[s->n];
	memset(g, 0, sizeof(*g));
	snprintf(g->path, sizeof(g->path), "%s%s", dir, name);
	g->flags = DASH_APP_GAME;
	skip = nameGameId(title, g->category) ? 12 : 0;
	if (!skip && kind == GAME_PS2)
		prepareNeutrinoGameID(g->path, g->category, sizeof(g->category));
	else if (!skip && kind == GAME_VCD)
		buildPopstarterVcdGameID(g->path, g->category, sizeof(g->category));
	else if (!skip)
		buildEmberGameID(g->path, g->category, sizeof(g->category));
	dashStrCopy(base, title + skip, sizeof(base));
	dot = kind == GAME_EMBER ? NULL : strrchr(base, '.');
	if (dot != NULL)
		*dot = '\0';
	dashStrCopy(g->title, base[0] ? base : title, sizeof(g->title));
	for (dot = g->path; *dot; dot++)
		g->hue = (u16)((g->hue * 31 + (u8)*dot) % 360);
	s->n++;
}

static int scanFolder(GameScan *s, const char *dir)
{
	int n, i, more = 0;

	dashWorkerStatus(dir, 0, 0);
	n = getDirComplete(dir, s->files, &more);
	for (i = 0; i < n && i < MAX_ENTRY - 2; i++) {
		FILEINFO *f = &s->files[i];
		if (!(f->stats.AttrFile & sceMcFileAttrSubdir) && f->name[0] != '.' && IsNeutrinoIsoPath(f->name))
			addGame(s, dir, f->name, NULL, GAME_PS2);
	}
	return n >= 0;
}

/* POPS/<name>.VCD; POPSTARTER.VCD is reserved (its selector would be POPStarter's own ELF). */
static int scanPops(GameScan *s, const char *root)
{
	char dir[DASH_PATH_MAX];
	int n, i, more = 0;

	if (!IsPopstarterVcdDevice(root))
		return 0;
	snprintf(dir, sizeof(dir), "%sPOPS/", root);
	dashWorkerStatus(dir, 0, 0);
	n = getDirComplete(dir, s->files, &more);
	for (i = 0; i < n && i < MAX_ENTRY - 2; i++) {
		FILEINFO *f = &s->files[i];
		if (!(f->stats.AttrFile & sceMcFileAttrSubdir) && f->name[0] != '.' && IsPopstarterVcdPath(f->name) &&
		    stricmp(f->name, "POPSTARTER.VCD"))
			addGame(s, dir, f->name, NULL, GAME_VCD);
	}
	return n >= 0;
}

/* EMBER/games/<Game>/: one row per folder that holds an image Ember can boot. */
static int scanEmber(GameScan *s, const char *root)
{
	static const char *const exts[] = {"CUE", "EXE", "BIN"};
	char dir[DASH_PATH_MAX], game[DASH_PATH_MAX], (*names)[MAX_NAME];
	int n, i, j, e, k = 0, more = 0;

	if (snprintf(dir, sizeof(dir), "%sEMBER/games/", root) >= DASH_PATH_MAX)
		return 0;
	dashWorkerStatus(dir, 0, 0);
	n = getDirComplete(dir, s->files, &more);
	if (n <= 0)
		return n >= 0;
	names = malloc(sizeof(*names) * EMBER_GAMES_MAX);
	if (names == NULL)
		return 1;
	/* the folder listings below reuse s->files */
	for (i = 0; i < n && i < MAX_ENTRY - 2 && k < EMBER_GAMES_MAX; i++)
		if ((s->files[i].stats.AttrFile & sceMcFileAttrSubdir) && s->files[i].name[0] != '.')
			dashStrCopy(names[k++], s->files[i].name, MAX_NAME);
	for (i = 0; i < k; i++) {
		int pick = -1, best = 3;
		if (snprintf(game, sizeof(game), "%s%s/", dir, names[i]) >= DASH_PATH_MAX - 8)
			continue;
		n = getDirComplete(game, s->files, &more);
		for (j = 0; j < n && j < MAX_ENTRY - 2 && best > 0; j++)
			for (e = 0; e < best; e++)
				if (!(s->files[j].stats.AttrFile & sceMcFileAttrSubdir) && genCmpFileExt(s->files[j].name, exts[e])) {
					pick = j, best = e;
					break;
				}
		if (pick >= 0)
			addGame(s, game, s->files[pick].name, names[i], GAME_EMBER);
	}
	free(names);
	return 1;
}

static void scanRoot(GameScan *s, const char *root, int shares)
{
	char dir[DASH_PATH_MAX], names[GAME_SHARES][MAX_NAME];
	int found, n, i, k = 0, more = 0;

	snprintf(dir, sizeof(dir), "%sDVD/", root);
	found = scanFolder(s, dir);
	snprintf(dir, sizeof(dir), "%sCD/", root);
	found |= scanFolder(s, dir);
	if (!shares) { /* smb:/ itself lists shares: an EMBER share must not stop that */
		found |= scanPops(s, root);
		found |= scanEmber(s, root);
	}
	if (found || !shares)
		return;
	/* smb:/ lists the shares; each one is an OPL root. */
	n = getDirComplete(root, s->files, &more);
	for (i = 0; i < n && i < MAX_ENTRY - 2 && k < GAME_SHARES; i++)
		if ((s->files[i].stats.AttrFile & sceMcFileAttrSubdir) && s->files[i].name[0] != '.')
			dashStrCopy(names[k++], s->files[i].name, MAX_NAME);
	for (i = 0; i < k; i++)
		if (snprintf(dir, sizeof(dir), "%s%s/", root, names[i]) < DASH_PATH_MAX - 16)
			scanRoot(s, dir, 0);
}

static void scanRun(void *arg)
{
	GameScan *s = (GameScan *)arg;
	int i;

	for (i = 0; i < s->nroots; i++)
		if (isPathDeviceStackLoaded(s->roots[i]))
			scanRoot(s, s->roots[i], !strncmp(s->roots[i], "smb", 3));
	if (s->n > 1)
		qsort(s->games, s->n, sizeof(DashApp), titleCmp);
}

static void scanDone(void *arg)
{
	GameScan *s = (GameScan *)arg;

	free(s->files);
	s->files = NULL;
	scan_job = NULL;
	if (scan_result != NULL) {
		free(scan_result->games);
		free(scan_result);
	}
	scan_result = s;
}

/* Lists the devices on the UI thread; the worker only reads running ones. */
static void scanStart(void)
{
	DashDevice devs[DASH_MAX_DEVICES];
	GameScan *s;
	int nd, i;

	if (scan_job != NULL)
		return;
	s = (GameScan *)calloc(1, sizeof(*s));
	if (s == NULL)
		return;
	s->files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	if (s->files == NULL) {
		free(s);
		return;
	}
	nd = dashDevicesList(devs, DASH_MAX_DEVICES);
	for (i = 0; i < nd && s->nroots < GAME_ROOTS; i++)
		if (devs[i].state == DASH_DEV_READY && IsNeutrinoGameDevice(devs[i].browse))
			dashStrCopy(s->roots[s->nroots++], devs[i].browse, sizeof(s->roots[0]));
	scan_job = s;
	if (dashWorkerSubmit("Looking for games", DASH_JOB_SCAN | DASH_JOB_VISIBLE, scanRun, scanDone, s) < 0) {
		scan_job = NULL;
		free(s->files);
		free(s);
	}
}

//--------------------------------------------------------------
// View
//--------------------------------------------------------------
static void rebuild(const char *keep)
{
	int i;

	list_top_anim = -1.0f;
	norder = 0;
	for (i = 0; i < ngames; i++)
		if (dashDevicePathReady(games[i].path))
			order[norder++] = i;
	sel = 0;
	for (i = 0; i < norder && keep != NULL && keep[0]; i++)
		if (!stricmp(games[order[i]].path, keep))
			sel = i;
}

/* An artwork job holds a pointer into the list: swap only when none runs. */
static void applyScan(void)
{
	char keep[DASH_PATH_MAX];

	if (scan_result == NULL || dashArtBusy())
		return;
	keep[0] = '\0';
	if (sel < norder)
		dashStrCopy(keep, games[order[sel]].path, sizeof(keep));
	dashArtFlushAll();
	free(games);
	games = scan_result->games;
	ngames = scan_result->n;
	free(scan_result);
	scan_result = NULL;
	scanned = 1;
	rebuild(keep);
	sel_anim = (float)sel;
}

static DashApp *selGame(void)
{
	return (sel >= 0 && sel < norder) ? &games[order[sel]] : NULL;
}

/* The core a GAMES row starts with, from its file (as ExecuteMainAction routes it). */
static const char *gameCore(const DashApp *g)
{
	if (IsPopstarterVcdPath(g->path))
		return "PS1 - POPStarter";
	if (!IsNeutrinoIsoPath(g->path))
		return "PS1 - Ember";
	return "PS2 - Neutrino";
}

/* Details is a scrolling sheet; a long path is cut into short lines so all of it can be read. */
#define DETAIL_CHUNK 44
#define DETAIL_LINES 24
static void showDetails(DashApp *g)
{
	static char text[DETAIL_LINES + 4][DETAIL_CHUNK + 12];
	const char *lines[DETAIL_LINES + 4];
	const char *cover = g->art_state == DASH_ART_FOUND ? g->art : (g->category[0] ? "none - add ART/<Game ID>_COV.png" : "none");
	char dev[48];
	int n = 0, i;

	dashLibDeviceLabel(g->path, dev, sizeof(dev));
	snprintf(text[n], sizeof(text[n]), "Starts with: %s", gameCore(g)), lines[n] = text[n], n++;
	snprintf(text[n], sizeof(text[n]), "Game ID: %s", g->category[0] ? g->category : "not found"), lines[n] = text[n], n++;
	snprintf(text[n], sizeof(text[n]), "Location: %s", dev), lines[n] = text[n], n++;
	for (i = 0; i < 2; i++) {
		const char *v = i ? cover : g->path;
		size_t len = strlen(v), pos = 0;

		lines[n] = "", n++; /* gap */
		snprintf(text[n], sizeof(text[n]), "%s:", i ? "Cover" : "File"), lines[n] = text[n], n++;
		while (pos < len && n < DETAIL_LINES) {
			snprintf(text[n], sizeof(text[n]), "%.*s", DETAIL_CHUNK, v + pos), lines[n] = text[n], n++;
			pos += DETAIL_CHUNK;
		}
	}
	dashShowTextLines(DI_INFO, g->title, "Game details", lines, n);
}

/* Triangle: per-game Neutrino options are edited and saved here without
 * starting the game; Start then opens them already filled in. */
static void gameSettings(DashApp *g)
{
	int r = editNeutrinoGameProfile(g->category, g->path);

	if (r < 0)
		dashModalMessage(DI_INFO, g->title,
		                 "Game settings are saved by Game ID, and none was found for this image. "
		                 "Name it like SLUS_123.45.Title.iso, or start it and set the options for that launch.", NULL);
}

static void gamesMenu(void)
{
	enum { M_START, M_SETTINGS, M_DETAILS, M_SHOW, M_REFRESH };
	DashApp *g = selGame();
	DashMenuItem items[5];
	int act[5], n = 0, r;
	char dir[DASH_PATH_MAX];

	memset(items, 0, sizeof(items));
	if (g != NULL) {
		items[n].icon = DI_PLAY, items[n].label = localizeDashboardText("Start"), act[n++] = M_START;
		if (IsNeutrinoIsoPath(g->path))
			items[n].icon = DI_SETTINGS, items[n].label = "Game settings", items[n].detail = "Neutrino options",
			act[n++] = M_SETTINGS;
		items[n].icon = DI_INFO, items[n].label = "Details", act[n++] = M_DETAILS;
		items[n].icon = DI_FOLDER, items[n].label = "Show in File Browser", act[n++] = M_SHOW;
	}
	items[n].icon = DI_REFRESH, items[n].label = "Refresh games", items[n].detail = "scan now", act[n++] = M_REFRESH;
	r = dashModalMenu(localizeDashboardText("Games"), g ? g->title : NULL, items, n, 0);
	if (r < 0 || r >= n)
		return;
	r = act[r];
	if (r == M_START)
		dashLaunchApp(g, 0);
	else if (r == M_SETTINGS)
		gameSettings(g);
	else if (r == M_DETAILS)
		showDetails(g);
	else if (r == M_SHOW) {
		const char *slash = strrchr(g->path, '/');
		snprintf(dir, sizeof(dir), "%.*s", slash ? (int)(slash - g->path) + 1 : 0, g->path);
		if (dir[0])
			dashNavigateFilesTab(dir);
	} else
		scanStart();
}

static void gamesEnter(void)
{
	if (!scanned && scan_job == NULL && scan_result == NULL)
		scanStart();
	rebuild(sel < norder ? games[order[sel]].path : NULL);
	sel_anim = (float)sel;
}

static void move(int delta)
{
	int old = sel;

	if (norder <= 0)
		return;
	sel = (sel + delta) % norder;
	if (sel < 0)
		sel += norder;
	if (sel != old)
		dashUiFocusPoke();
}

static void gamesInput(const DashInput *in)
{
	if (in->pressed & DASH_IN_BACK) {
		dashSwitchSection(dashBackSection());
		return;
	}
	if (in->pressed & DASH_IN_UP)
		move(-1);
	if (in->pressed & DASH_IN_DOWN)
		move(1);
	if (in->pressed & (DASH_IN_L2 | DASH_IN_LEFT))
		move(-LIST_ROWS);
	if (in->pressed & (DASH_IN_R2 | DASH_IN_RIGHT))
		move(LIST_ROWS);
	if (in->pressed & DASH_IN_OK) {
		if (norder > 0)
			dashLaunchApp(selGame(), 0);
		else
			scanStart();
	} else if (in->pressed & DASH_IN_MENU)
		gamesMenu();
}

static void gamesUpdate(float dt)
{
	applyScan();
	sel_anim = dashApproach(sel_anim, (float)sel, 14.0f * dashAnimSpeed(), dt);
}

static void gamesDraw(void)
{
	DashApp *g = selGame();
	float lx = DASH_SAFE_L, ly = 104, lw = 290;
	float dx = lx + lw + 24, dy = 98, dw = DASH_PANE_R - dx;
	char sub[48], dev[48];
	int top, i, off;
	GSTEXTURE *art;

	snprintf(sub, sizeof(sub), "%d %s", norder, norder == 1 ? "game" : "games");
	dashDrawSectionTitle(DASH_SAFE_L, 68, localizeDashboardText("Games"), sub);
	if (norder == 0) {
		if (scan_job != NULL || scan_result != NULL)
			dashDrawEmptyState(320, 238, DI_DISC, "Looking for games...", "The DVD, CD, POPS and EMBER folders of your devices are being scanned.");
		else
			dashDrawEmptyState(320, 238, DI_DISC, "No games found",
			                   "Put PS2 ISO images in the DVD or CD folder of a USB, MX4SIO, MMCE, iLink, exFAT HDD, UDPFS or SMB device, "
			                   "PS1 VCDs in POPS, or PS1 games in EMBER/games/<Game>/. "
			                   "Open the device in Files first if it is not running, then press the confirm button to scan.");
		return;
	}

	top = listCenteredTop(sel, norder, LIST_ROWS);
	list_top_anim = listSlideTop(list_top_anim, top, sel, norder, LIST_ROWS,
	                             dashApproach(0.0f, 1.0f, 14.0f * dashAnimSpeed(), dashFrameDelta()));
	if (sel_anim < top || sel_anim >= top + LIST_ROWS)
		sel_anim = (float)sel;
	dashDrawPanelBox(lx - 6, ly - 6, lw + 12, LIST_ROWS * LIST_ROW_H + 12, 14);
	dashGfxScissor(lx - 6, ly - 4, lw + 12, LIST_ROWS * LIST_ROW_H + 8);
	dashDrawSelection(lx, ly + (sel_anim - list_top_anim) * LIST_ROW_H, lw, LIST_ROW_H - 3, 9, 1.0f);
	for (i = (int)list_top_anim; i < norder && i < list_top_anim + LIST_ROWS; i++) {
		DashApp *e = &games[order[i]];
		float y = ly + (i - list_top_anim) * LIST_ROW_H;
		GSTEXTURE *t = dashArtGet(e, abs(i - sel) + 1);

		if (t != NULL)
			dashTexDrawFit(t, lx + 6, y + 2, 24, 24, DC(255, 255, 255, 255));
		else
			dashIcon(DI_DISC, lx + 9, y + 5, 18, dashItemColor(i == sel));
		dashTextFit(lx + 38, y + 3, lw - 50, e->title, DASH_FS_SMALL, dashItemColor(i == sel));
	}
	dashGfxScissorReset();
	dashDrawScrollbar(lx + lw + 10, ly, LIST_ROWS * LIST_ROW_H, top, LIST_ROWS, norder);

	dashDrawCard(dx, dy, dw, 290, 16);
	/* With cover art: the art alone, large, no plaque. Without: the frame as the stand-in. */
	art = dashArtGet(g, 0);
	if (art != NULL) {
		dashTexDrawFit(art, dx + 14, dy + 10, dw - 28, 174, DC(255, 255, 255, 255));
		off = 36;
	} else {
		dashDrawAppArt(g, dx + (dw - 132) * 0.5f, dy + 14, 132, 132, 1.0f, 0);
		off = 0;
	}
	dashTextFit(dx + 14, dy + 156 + off, dw - 28, g->title, DASH_FS_LABEL, DASH_C_TEXT);
	dashLibDeviceLabel(g->path, dev, sizeof(dev));
	dashIcon(dashLibDeviceIcon(g->path), dx + 14, dy + 186 + off, 14, DASH_C_ACCENT);
	dashTextFit(dx + 34, dy + 184 + off, dw - 48, dev, DASH_FS_CAPTION, DASH_C_ACCENT);
	snprintf(sub, sizeof(sub), "%s%s%s", gameCore(g), g->category[0] ? "  " : "", g->category);
	dashTextFit(dx + 14, dy + 206 + off, dw - 28, sub, DASH_FS_CAPTION, DASH_C_TEXT2);
	if (art == NULL) /* the full path is in Details; the card is full when the art is big */
		dashTextFit(dx + 14, dy + 264, dw - 28, g->path, DASH_FS_CAPTION * 0.9f, DASH_C_TEXT3);
}

static void gamesHints(void)
{
	DashHint h[3];
	int n = 0;

	h[n].button = dashOkIcon(), h[n].key = NULL, h[n].label = norder ? "Start" : "Scan now", n++;
	h[n].button = DI_BTN_TRIANGLE, h[n].key = NULL, h[n].label = "Options", n++;
	if (dashBackSection() != DASH_SEC_GAMES)
		h[n].button = dashBackIcon(), h[n].key = NULL, h[n].label = dashSectionLabel(dashBackSection()), n++;
	dashDrawHints(h, n);
}

const DashScreen dashGamesScreen = {gamesEnter, gamesInput, gamesUpdate, gamesDraw, gamesHints};
//--------------------------------------------------------------
//End of file: dash_games.c
//--------------------------------------------------------------
