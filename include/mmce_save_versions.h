//--------------------------------------------------------------
//File name:   mmce_save_versions.h
//Per-save versions: each save on a verified card history copy is kept as a
//.psu, one folder per save, a new version only when its bytes change. See
//src/mmce_save_versions.c.
//--------------------------------------------------------------
#ifndef MMCE_SAVE_VERSIONS_H
#define MMCE_SAVE_VERSIONS_H

/* The folder inside a card's history folder that holds its per-save versions. */
#define MMCE_SAVE_VERSIONS_DIR "WLE-Saves"

/* Largest .psu written or accepted: an 8 MB card's worth of save data plus
 * the .psu headers. Bigger cards hold bigger saves only in theory. */
#define MMCE_SAVE_VERSION_MAX (8u * 1024u * 1024u + 512u * 1024u)

typedef struct {
	int saves;     /* saves on the card, system folders included */
	int versioned; /* new .psu versions written and verified */
	int unchanged; /* same bytes as the newest version already kept */
	int skipped;   /* system folders, saves holding sub-folders, too large */
	int failed;    /* could not be read, written or verified */
} MmceSaveVersionStats;

/* Reads the card history copy history (a verified .mcd or .ps2 named
 * WLE-<stamp>.mcd/.ps2) and, for every save on it, writes
 * saves_dir/<save>/WLE-<stamp>.psu when the save differs from the newest
 * .psu already there. Each new file is written as .PART, read back and
 * checked against what was meant, and only then renamed into place; an
 * existing file is never replaced. keep (0 = all) limits the versions per
 * save; the oldest go first and the one just written is never removed.
 * saves_dir/SOURCE.TXT records the history copy last done without errors,
 * so asking again for the same copy reads nothing else.
 * The history copy is only read. Returns 0, or -1 if the card itself could
 * not be read (single saves that fail are counted in st->failed). */
int mmceSaveVersionsFromHistory(const char *history, const char *saves_dir, int keep, MmceSaveVersionStats *st);

/* 0 when path is a complete, well-formed .psu of at most
 * MMCE_SAVE_VERSION_MAX bytes (every entry, name and size checked), else -1.
 * Used before a version is uploaded or published after a download. */
int mmceSaveVersionValidate(const char *path);

/* The folder name used for a save: the save's own name when it only uses
 * letters, digits, '-', '_' and '.', otherwise those characters kept, the
 * rest as '_', and "~" plus 8 hex digits of the name's SHA-256 so two saves
 * never share a folder. out needs at least 48 bytes. */
void mmceSaveVersionFolder(const char *save_name, char *out, int out_size);

#endif
