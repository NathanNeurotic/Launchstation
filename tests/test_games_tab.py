"""Execute the GAMES tab's production scan and cover-art naming (issue #391).

The scan lists ISOs in the OPL DVD and CD folders (and in each SMB share),
takes the game ID from OPL's "SLUS_123.45.Title.iso" naming or else from the
image's SYSTEM.CNF, and the art cache looks for <root>/ART/<ID>_COV.png/.jpg.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
games = (root / "src/dash_games.c").read_text(encoding="utf-8")
art = (root / "src/dash_art.c").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


scan_type = games[games.index("typedef struct\n{\n\tchar roots"):games.index("} GameScan;") + len("} GameScan;")]
code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define stricmp strcasecmp
typedef unsigned short u16;
typedef unsigned char u8;
#define DASH_PATH_MAX 256
#define MAX_NAME 256
#define MAX_ENTRY 64
#define DASH_MAX_APPS 512
#define GAMES_MAX DASH_MAX_APPS
#define GAME_ROOTS 24
#define GAME_SHARES 16
enum { GAME_PS2, GAME_VCD, GAME_EMBER };
#define EMBER_GAMES_MAX 256
#define sceMcFileAttrSubdir 0x20
enum { DASH_ART_UNKNOWN, DASH_ART_FOUND, DASH_ART_NONE };
#define DASH_APP_GAME (1 << 6)
typedef struct { char path[DASH_PATH_MAX], title[64], category[24], art[DASH_PATH_MAX]; u8 art_state, flags; u16 hue; } DashApp;
typedef struct { int AttrFile; } Stats;
typedef struct { char name[MAX_NAME]; Stats stats; } FILEINFO;
static void dashStrCopy(char *d, const char *s, int n) { snprintf(d, n, "%s", s); }
static void dashWorkerStatus(const char *t, int a, int b) { (void)t; (void)a; (void)b; }
static int genCmpFileExt(const char *p, const char *e) { const char *d = strrchr(p, '.'); return d && !strcasecmp(d + 1, e); }
static int IsNeutrinoIsoPath(const char *p) { return genCmpFileExt(p, "iso"); }
static int IsPopstarterVcdPath(const char *p) { return genCmpFileExt(p, "VCD"); }
static int IsPopstarterVcdDevice(const char *p) { return !strncmp(p, "usb", 3); }
static int IsEmberGamePath(const char *p) { return strstr(p, "/games/") != NULL; }
static int ps1_reads;
static int buildPopstarterVcdGameID(const char *p, char *id, size_t n) { ps1_reads++; snprintf(id, n, "SCES_000.01"); (void)p; return 1; }
static int buildEmberGameID(const char *p, char *id, size_t n) { ps1_reads++; assert(n >= 12); snprintf(id, n, "SLUS_000.02"); (void)p; return 1; }
static int cnf_reads;
static int prepareNeutrinoGameID(const char *iso, char *id, size_t n) {
    cnf_reads++; assert(n >= 12);
    if (strstr(iso, "NoCnf")) { id[0] = '\0'; return 0; }
    snprintf(id, n, "SLES_555.55"); return 1;
}
static const char *store = "mc0:/LAUNCHSTATION/";
static const char *dashStoreDir(void) { return store; }
/* A tiny tree: name, directory flag, parent. */
static const struct { const char *dir, *name; int sub; } tree[] = {
    {"usb0:/DVD/", "SLUS_200.62.Gran Turismo 3.iso", 0},
    {"usb0:/DVD/", "Mr. Mosquito.ISO", 0},
    {"usb0:/DVD/", "NoCnf.iso", 0},
    {"usb0:/DVD/", "readme.txt", 0},
    {"usb0:/DVD/", "Folder.iso", 1},
    {"usb0:/CD/", "SCUS_971.13.Ico.iso", 0},
    {"smb:/", "PS2", 1},
    {"smb:/", "Music", 1},
    {"smb:/PS2/DVD/", "Okami.iso", 0},
    {"smb:/", "EMBER", 1},
    {"smb:/EMBER/games/", "Tekken 3", 1},
    {"usb0:/POPS/", "SLUS_007.92.Crash.VCD", 0},
    {"usb0:/POPS/", "Spyro.vcd", 0},
    {"usb0:/POPS/", "POPSTARTER.VCD", 0},
    {"usb0:/POPS/", "POPSTARTER.ELF", 0},
    {"usb0:/EMBER/games/", "Tekken 3", 1},
    {"usb0:/EMBER/games/", "Ape Escape", 1},
    {"usb0:/EMBER/games/", "Empty", 1},
    {"usb0:/EMBER/games/", "notes.txt", 0},
    {"usb0:/EMBER/games/Tekken 3/", "Tekken 3.bin", 0},
    {"usb0:/EMBER/games/Tekken 3/", "Tekken 3.cue", 0},
    {"usb0:/EMBER/games/Ape Escape/", "APE.EXE", 0},
    {"usb0:/EMBER/games/Ape Escape/", "APE.BIN", 0},
    {"usb0:/EMBER/games/Empty/", "readme.txt", 0},
    {"smb:/PS2/POPS/", "NotHere.VCD", 0},
};
static int getDirComplete(const char *path, FILEINFO *info, int *more) {
    int i, n = 0, any = !strcmp(path, "smb:/") || !strcmp(path, "smb:/PS2/DVD/") || !strcmp(path, "usb0:/EMBER/games/Empty/");
    *more = 0;
    for (i = 0; i < (int)(sizeof(tree) / sizeof(tree[0])); i++)
        if (!strcmp(tree[i].dir, path)) {
            snprintf(info[n].name, MAX_NAME, "%s", tree[i].name);
            info[n++].stats.AttrFile = tree[i].sub ? sceMcFileAttrSubdir : 0;
            any = 1;
        }
    return any ? n : -1;
}
'''
code += scan_type + "\n"
for sig in ("static int nameGameId(", "static void addGame(", "static int scanFolder(", "static int scanPops(", "static int scanEmber(", "static void scanRoot("):
    code += function(games, sig)
code += function(art, "static int gameCandidate(")
code += r'''
static const DashApp *find(const struct GameScanMock *unused, DashApp *g, int n, const char *path) {
    (void)unused;
    for (int i = 0; i < n; i++) if (!strcmp(g[i].path, path)) return &g[i];
    return NULL;
}
int main(void) {
    static GameScan s;
    static FILEINFO files[MAX_ENTRY];
    char id[24], out[DASH_PATH_MAX];
    const DashApp *g;
    s.files = files;
    assert(nameGameId("SLUS_200.62.GT3.iso", id) && !strcmp(id, "SLUS_200.62"));
    assert(nameGameId("slus_200.62.GT3.iso", id) && !strcmp(id, "SLUS_200.62"));
    assert(!nameGameId("SLUS_200.62GT3.iso", id) && !nameGameId("SLU", id) && !nameGameId("1LUS_200.62.x.iso", id));

    scanRoot(&s, "usb0:/", 0);
    assert(s.n == 8);
    g = find(NULL, s.games, s.n, "usb0:/DVD/SLUS_200.62.Gran Turismo 3.iso");
    assert(g && !strcmp(g->title, "Gran Turismo 3") && !strcmp(g->category, "SLUS_200.62") && g->flags == DASH_APP_GAME);
    g = find(NULL, s.games, s.n, "usb0:/DVD/Mr. Mosquito.ISO");
    assert(g && !strcmp(g->title, "Mr. Mosquito") && !strcmp(g->category, "SLES_555.55"));
    g = find(NULL, s.games, s.n, "usb0:/DVD/NoCnf.iso");
    assert(g && !strcmp(g->title, "NoCnf") && g->category[0] == '\0');
    assert(find(NULL, s.games, s.n, "usb0:/CD/SCUS_971.13.Ico.iso"));
    assert(cnf_reads == 2); /* the named images are not opened */

    /* PS1: VCDs in POPS (not POPSTARTER.VCD), and one Ember row per games/<Game>/ folder with an image. */
    g = find(NULL, s.games, s.n, "usb0:/POPS/SLUS_007.92.Crash.VCD");
    assert(g && !strcmp(g->title, "Crash") && !strcmp(g->category, "SLUS_007.92"));
    g = find(NULL, s.games, s.n, "usb0:/POPS/Spyro.vcd");
    assert(g && !strcmp(g->title, "Spyro") && !strcmp(g->category, "SCES_000.01"));
    assert(!find(NULL, s.games, s.n, "usb0:/POPS/POPSTARTER.VCD"));
    g = find(NULL, s.games, s.n, "usb0:/EMBER/games/Tekken 3/Tekken 3.cue"); /* .cue beats .bin */
    assert(g && !strcmp(g->title, "Tekken 3") && !strcmp(g->category, "SLUS_000.02"));
    g = find(NULL, s.games, s.n, "usb0:/EMBER/games/Ape Escape/APE.EXE"); /* .exe beats .bin */
    assert(g && !strcmp(g->title, "Ape Escape"));
    assert(ps1_reads == 3);

    /* An SMB root lists shares; each share is searched for DVD and CD. */
    s.n = 0;
    scanRoot(&s, "smb:/", 1);
    assert(s.n == 1 && !strcmp(s.games[0].path, "smb:/PS2/DVD/Okami.iso")); /* POPStarter cannot start SMB VCDs */

    /* Cover names: OPL ART folder beside DVD/CD, then the dashboard ART folder. */
    DashApp a = {0};
    snprintf(a.path, sizeof(a.path), "usb0:/DVD/SLUS_200.62.GT3.iso");
    snprintf(a.category, sizeof(a.category), "SLUS_200.62");
    a.flags = DASH_APP_GAME;
    assert(gameCandidate(&a, 0, out, sizeof(out)) && out[0] == '\0');
    assert(gameCandidate(&a, 1, out, sizeof(out)) && !strcmp(out, "usb0:/ART/SLUS_200.62_COV.png"));
    assert(gameCandidate(&a, 2, out, sizeof(out)) && !strcmp(out, "usb0:/ART/SLUS_200.62_COV.jpg"));
    assert(gameCandidate(&a, 3, out, sizeof(out)) && !strcmp(out, "mc0:/LAUNCHSTATION/ART/SLUS_200.62_COV.png"));
    assert(gameCandidate(&a, 4, out, sizeof(out)) && !strcmp(out, "mc0:/LAUNCHSTATION/ART/SLUS_200.62_COV.jpg"));
    assert(!gameCandidate(&a, 5, out, sizeof(out)));
    snprintf(a.path, sizeof(a.path), "smb:/PS2/CD/x.iso");
    assert(gameCandidate(&a, 1, out, sizeof(out)) && !strcmp(out, "smb:/PS2/ART/SLUS_200.62_COV.png"));
    snprintf(a.art, sizeof(a.art), "usb0:/ART/known.png");
    assert(gameCandidate(&a, 0, out, sizeof(out)) && !strcmp(out, "usb0:/ART/known.png"));
    a.art[0] = '\0';
    snprintf(a.path, sizeof(a.path), "usb0:/POPS/x.VCD");
    assert(gameCandidate(&a, 1, out, sizeof(out)) && !strcmp(out, "usb0:/ART/SLUS_200.62_COV.png"));
    snprintf(a.path, sizeof(a.path), "usb0:/EMBER/games/Tekken 3/Tekken 3.cue");
    assert(gameCandidate(&a, 1, out, sizeof(out)) && !strcmp(out, "usb0:/ART/SLUS_200.62_COV.png"));
    snprintf(a.art, sizeof(a.art), "usb0:/ART/known.png");
    a.category[0] = '\0';
    assert(gameCandidate(&a, 1, out, sizeof(out)) && out[0] == '\0');
    puts("PASS: GAMES scans DVD/CD, POPS, EMBER/games and SMB shares, reads IDs from names or SYSTEM.CNF, and finds OPL covers");
    return 0;
}
'''
code = code.replace("const struct GameScanMock *unused", "const void *unused")

compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-games-") as tmp:
    source = Path(tmp) / "games.c"
    exe = Path(tmp) / "games"
    source.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)

# The tab is wired in: listed after Home, routed, with its own Settings toggle.
main = (root / "src/dash_main.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
header = (root / "include/dash_internal.h").read_text(encoding="utf-8")
assert "&dashSettingsScreen, &dashGamesScreen}" in main
assert "DASH_START_LAUNCH_KEYS,\n\tDASH_SEC_GAMES,\n\tDASH_SEC_COUNT" in header  # saved start value 6 unchanged
assert 'S_TAB_GAMES, "Games tab"' in settings
assert "src/dash_games.c" in (root / "ps2.yaml").read_text(encoding="utf-8")
# Swapping the list waits for artwork jobs, which hold DashApp pointers.
assert "scan_result == NULL || dashArtBusy()" in games and "dashArtFlushAll();" in games
print("PASS: GAMES tab routed after Home with its own tab toggle")
