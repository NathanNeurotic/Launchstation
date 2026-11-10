//--------------------------------------------------------------
//File name:   dash_editor.c
//Native dashboard Text Editor. Replaces the classic TextEditor() whenever
//the dashboard opens a text file (Files, Tools, application metadata,
//"Start with arguments...").
//
//* Up to ED_MAX_DOCS documents; switch with the menu.
//* Text is kept with '\n' line breaks; the file's own line endings
//  (CR/LF, LF or CR) are remembered and written back on save.
//* The wLaunchELF 8x16 font (built-in or .fnt) is used, so every byte of
//  a configuration file stays visible and columns line up.
//* A docked on-screen keyboard (SELECT) uses the user's layout; a USB
//  keyboard types directly.
//* Files are read and written by the background worker.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "init.h"
#include "filer_actions.h"

#define ED_MAX_DOCS 8
#define ED_MAX_SIZE (512 * 1024)
#define ED_TAB 4

enum {
	EOL_CRLF = 0,
	EOL_LF,
	EOL_CR,
	EOL_COUNT
};

static const char *const eol_names[EOL_COUNT] = {"CR/LF (PC)", "LF (Unix)", "CR (old Mac)"};

typedef struct
{
	char path[MAX_PATH]; /* "" = new document, never saved */
	char *buf;
	int len, cap;
	int eol;
	int dirty;
	int cur;         /* cursor, byte offset */
	int mark;        /* selection anchor, -1 = none */
	int top, left;   /* first visible line / column */
	int want_col;    /* column kept while moving up and down */
	int *lines;      /* start offset of every line */
	int nlines, lines_cap;
} EdDoc;

static EdDoc docs[ED_MAX_DOCS];
static int ndocs, active;
static char *clip;
static int clip_len;
static int kb_shown;
static DashKeyboard editor_keyboard;
static int create_missing; /* dashTextEditorEx: a missing file starts a new document */

//--------------------------------------------------------------
// Document model
//--------------------------------------------------------------
static void docRelines(EdDoc *d)
{
	int i, n = 1;

	for (i = 0; i < d->len; i++)
		if (d->buf[i] == '\n')
			n++;
	if (n > d->lines_cap) {
		int cap = n + 256;
		int *nl = (int *)realloc(d->lines, sizeof(int) * cap);
		if (nl == NULL)
			return; /* keep the old table; drawing clamps */
		d->lines = nl;
		d->lines_cap = cap;
	}
	d->lines[0] = 0;
	n = 1;
	for (i = 0; i < d->len; i++)
		if (d->buf[i] == '\n')
			d->lines[n++] = i + 1;
	d->nlines = n;
}

static int docLineOf(const EdDoc *d, int off)
{
	int lo = 0, hi = d->nlines - 1;

	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (d->lines[mid] <= off)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

static int docLineEnd(const EdDoc *d, int line)
{
	return (line + 1 < d->nlines) ? d->lines[line + 1] - 1 : d->len;
}

/* visual column of `off` (tabs expand to ED_TAB) */
static int docColOf(const EdDoc *d, int off)
{
	int line = docLineOf(d, off), i, col = 0;

	for (i = d->lines[line]; i < off; i++)
		col = (d->buf[i] == '\t') ? (col / ED_TAB + 1) * ED_TAB : col + 1;
	return col;
}

/* offset in `line` closest to visual column `col` */
static int docOffAtCol(const EdDoc *d, int line, int col)
{
	int i = d->lines[line], end = docLineEnd(d, line), c = 0;

	while (i < end) {
		int next = (d->buf[i] == '\t') ? (c / ED_TAB + 1) * ED_TAB : c + 1;
		if (next > col)
			break;
		c = next;
		i++;
	}
	return i;
}

static int docReserve(EdDoc *d, int extra)
{
	char *nb;
	int cap;

	if (d->len + extra <= d->cap)
		return 1;
	if (d->len + extra > ED_MAX_SIZE)
		return 0;
	cap = d->len + extra + 4096;
	if (cap > ED_MAX_SIZE)
		cap = ED_MAX_SIZE;
	nb = (char *)realloc(d->buf, cap + 1);
	if (nb == NULL)
		return 0;
	d->buf = nb;
	d->cap = cap;
	return 1;
}

static int selRange(const EdDoc *d, int *a, int *b)
{
	if (d->mark < 0 || d->mark == d->cur)
		return 0;
	*a = d->mark < d->cur ? d->mark : d->cur;
	*b = d->mark < d->cur ? d->cur : d->mark;
	return 1;
}

static void docDelete(EdDoc *d, int a, int b)
{
	if (a < 0)
		a = 0;
	if (b > d->len)
		b = d->len;
	if (a >= b)
		return;
	memmove(d->buf + a, d->buf + b, d->len - b);
	d->len -= b - a;
	d->cur = a;
	d->mark = -1;
	d->dirty = 1;
	docRelines(d);
}

/* inserts at the cursor, replacing the selection */
static int docInsert(EdDoc *d, const char *s, int n)
{
	int a, b;

	if (selRange(d, &a, &b))
		docDelete(d, a, b);
	d->mark = -1;
	if (!docReserve(d, n)) {
		dashToast(DASH_TOAST_WARN, "Text is too large (512 KB limit)");
		return 0;
	}
	memmove(d->buf + d->cur + n, d->buf + d->cur, d->len - d->cur);
	memcpy(d->buf + d->cur, s, n);
	d->len += n;
	d->cur += n;
	d->dirty = 1;
	docRelines(d);
	d->want_col = docColOf(d, d->cur);
	return 1;
}

static void docBackspace(EdDoc *d)
{
	int a, b;

	if (selRange(d, &a, &b))
		docDelete(d, a, b);
	else if (d->cur > 0)
		docDelete(d, d->cur - 1, d->cur);
	d->want_col = docColOf(d, d->cur);
}

static void docDeleteForward(EdDoc *d)
{
	int a, b;

	if (selRange(d, &a, &b))
		docDelete(d, a, b);
	else if (d->cur < d->len)
		docDelete(d, d->cur, d->cur + 1);
}

static void docFree(EdDoc *d)
{
	free(d->buf);
	free(d->lines);
	memset(d, 0, sizeof(*d));
}

/* takes ownership of `text` (file bytes); converts line endings */
static void docSetText(EdDoc *d, char *text, int len)
{
	int i, o = 0, crlf = 0, lf = 0, cr = 0;

	for (i = 0; i < len; i++) {
		if (text[i] == '\r') {
			if (i + 1 < len && text[i + 1] == '\n') {
				crlf++;
				i++;
			} else
				cr++;
		} else if (text[i] == '\n')
			lf++;
	}
	d->eol = (lf > crlf && lf >= cr) ? EOL_LF : (cr > crlf && cr > lf) ? EOL_CR : EOL_CRLF;
	for (i = 0; i < len; i++) {
		if (text[i] == '\r') {
			if (i + 1 < len && text[i + 1] == '\n')
				i++;
			text[o++] = '\n';
		} else
			text[o++] = text[i];
	}
	d->buf = text;
	d->len = o;
	d->cap = len;
	d->cur = 0;
	d->mark = -1;
	d->top = d->left = 0;
	d->dirty = 0;
	docRelines(d);
}

/* the file bytes for saving, with the document's line endings */
static char *docFileBytes(const EdDoc *d, int *out_len)
{
	int i, o = 0, extra = 0;
	char *out;

	if (d->eol == EOL_CRLF)
		for (i = 0; i < d->len; i++)
			if (d->buf[i] == '\n')
				extra++;
	out = (char *)malloc(d->len + extra + 1);
	if (out == NULL)
		return NULL;
	for (i = 0; i < d->len; i++) {
		if (d->buf[i] == '\n') {
			if (d->eol == EOL_CRLF) {
				out[o++] = '\r';
				out[o++] = '\n';
			} else
				out[o++] = (d->eol == EOL_CR) ? '\r' : '\n';
		} else
			out[o++] = d->buf[i];
	}
	*out_len = o;
	return out;
}

static const char *docName(const EdDoc *d)
{
	const char *slash;

	if (d->path[0] == '\0')
		return "New document";
	slash = strrchr(d->path, '/');
	if (slash == NULL)
		slash = strchr(d->path, ':');
	return slash ? slash + 1 : d->path;
}

//--------------------------------------------------------------
// File IO (background worker)
//--------------------------------------------------------------
typedef struct
{
	int op; /* 0 load, 1 save, 2 overwrite an existing file in place */
	char path[MAX_PATH];
	char *text;
	int len;
	int ret; /* load: 0 ok, -1 not found/unreadable, -2 too large; save: 0 ok */
	int abandoned;
} EdIoJob;

static void edIoRun(void *arg)
{
	EdIoJob *j = (EdIoJob *)arg;
	char fixed[MAX_PATH];
	int fd, size, total = 0, rd;

	j->ret = -1;
	if (genFixPath(j->path, fixed) < 0)
		dashStrCopy(fixed, j->path, sizeof(fixed));
	if (j->op) { /* op 2 never creates or truncates: a failed write leaves the rest of the file */
		fd = genOpen(fixed, j->op == 2 ? FIO_O_WRONLY : FIO_O_CREAT | FIO_O_WRONLY | FIO_O_TRUNC);
		if (fd >= 0) {
			if (j->len == 0 || genWrite(fd, j->text, j->len) == j->len)
				j->ret = 0;
			genClose(fd);
		}
		if (!strncmp(fixed, "pfs", 3))
			unmountParty(fixed[3] - '0');
		return;
	}
	fd = genOpen(fixed, FIO_O_RDONLY);
	if (fd < 0)
		return;
	size = genLseek(fd, 0, SEEK_END);
	if (size > ED_MAX_SIZE) {
		genClose(fd);
		j->ret = -2;
		return;
	}
	/* some stacks report 0 or fail SEEK_END: read in chunks up to the limit */
	if (genLseek(fd, 0, SEEK_SET) < 0) {
		genClose(fd);
		fd = genOpen(fixed, FIO_O_RDONLY);
		if (fd < 0)
			return;
	}
	j->text = (char *)malloc(ED_MAX_SIZE + 1);
	if (j->text == NULL) {
		genClose(fd);
		return;
	}
	while (total < ED_MAX_SIZE) {
		int chunk = ED_MAX_SIZE - total < 16384 ? ED_MAX_SIZE - total : 16384;
		rd = genRead(fd, j->text + total, chunk);
		if (rd < 0) {
			free(j->text);
			j->text = NULL;
			genClose(fd);
			return;
		}
		if (rd == 0)
			break;
		total += rd;
	}
	if (total == ED_MAX_SIZE) {
		char probe;
		if (genRead(fd, &probe, 1) > 0) {
			free(j->text);
			j->text = NULL;
			genClose(fd);
			j->ret = -2;
			return;
		}
	}
	genClose(fd);
	/* shrink to what was read (+1 spare byte) */
	{
		char *t = (char *)realloc(j->text, total + 1);
		if (t != NULL)
			j->text = t;
	}
	j->len = total;
	j->ret = 0;
}

static void edIoDone(void *arg)
{
	EdIoJob *j = (EdIoJob *)arg;

	if (j->abandoned) {
		free(j->text);
		free(j);
	}
}

/* runs `j` on the worker; 1 = finished (caller owns j), 0 = backed out */
static int edIo(EdIoJob *j, const char *title)
{
	int id = dashWorkerSubmit(title, DASH_JOB_VISIBLE, edIoRun, edIoDone, j);

	if (id < 0) {
		dashToast(DASH_TOAST_WARN, "Busy, try again in a moment");
		free(j->text);
		free(j);
		return 0;
	}
	if (dashWorkerAwait(id, title, 1))
		return 1;
	j->abandoned = 1;
	return 0;
}

static int docNew(void);

static int docOpenPath(const char *path)
{
	EdIoJob *j;
	EdDoc *d;
	int i;

	for (i = 0; i < ndocs; i++) {
		if (docs[i].path[0] && !stricmp(docs[i].path, path)) {
			active = i; /* already open */
			return 1;
		}
	}
	if (ndocs >= ED_MAX_DOCS) {
		dashModalMessage(DI_WARNING, "Too many documents", "Close a document first (8 can be open at once).", NULL);
		return 0;
	}
	j = (EdIoJob *)malloc(sizeof(EdIoJob));
	if (j == NULL)
		return 0;
	memset(j, 0, sizeof(*j));
	dashStrCopy(j->path, path, sizeof(j->path));
	if (!edIo(j, "Opening file"))
		return 0;
	if (j->ret == -1 && create_missing) {
		free(j->text);
		free(j);
		if (!docNew())
			return 0;
		dashStrCopy(docs[active].path, path, sizeof(docs[active].path));
		return 1;
	}
	if (j->ret != 0) {
		dashModalMessage(DI_WARNING, "File not opened",
		                 j->ret == -2 ? "The file is larger than 512 KB, the Text Editor's limit." :
		                                "The file could not be read. Check that the device is connected.",
		                 path);
		free(j->text);
		free(j);
		return 0;
	}
	d = &docs[ndocs];
	memset(d, 0, sizeof(*d));
	dashStrCopy(d->path, path, sizeof(d->path));
	docSetText(d, j->text, j->len);
	free(j);
	if (d->nlines == 0) {
		docFree(d);
		return 0;
	}
	active = ndocs++;
	return 1;
}

static int docNew(void)
{
	EdDoc *d;

	if (ndocs >= ED_MAX_DOCS) {
		dashModalMessage(DI_WARNING, "Too many documents", "Close a document first (8 can be open at once).", NULL);
		return 0;
	}
	d = &docs[ndocs];
	memset(d, 0, sizeof(*d));
	d->buf = (char *)malloc(4096 + 1);
	if (d->buf == NULL)
		return 0;
	d->cap = 4096;
	d->eol = EOL_CRLF;
	d->mark = -1;
	docRelines(d);
	active = ndocs++;
	return 1;
}

static int docWrite(EdDoc *d, const char *path)
{
	EdIoJob *j;
	int ok;

	if (!strncmp(path, "cdfs", 4)) {
		dashModalMessage(DI_WARNING, "Read-only device", "Discs cannot be written. Use \"Save as\" to choose another place.", NULL);
		return 0;
	}
	j = (EdIoJob *)malloc(sizeof(EdIoJob));
	if (j == NULL)
		return 0;
	memset(j, 0, sizeof(*j));
	j->op = 1;
	dashStrCopy(j->path, path, sizeof(j->path));
	j->text = docFileBytes(d, &j->len);
	if (j->text == NULL) {
		free(j);
		return 0;
	}
	if (!edIo(j, "Saving file")) {
		/* still being written in the background; the result is unknown */
		dashToast(DASH_TOAST_INFO, "Saving continues in the background");
		return 0;
	}
	ok = (j->ret == 0);
	free(j->text);
	free(j);
	if (!ok) {
		dashModalMessage(DI_WARNING, "File not saved", "The file could not be written. The device may be read-only or full.", path);
		return 0;
	}
	dashStrCopy(d->path, path, sizeof(d->path));
	d->dirty = 0;
	dashToast(DASH_TOAST_OK, localizeDashboardText("Saved"));
	return 1;
}

static int docSaveAs(EdDoc *d)
{
	char dir[MAX_PATH], name[64], full[MAX_PATH];
	const char *slash;

	dir[0] = '\0';
	if (d->path[0]) {
		dashStrCopy(dir, d->path, sizeof(dir));
		slash = strrchr(dir, '/');
		if (slash != NULL)
			dir[slash - dir + 1] = '\0';
		dashStrCopy(name, docName(d), sizeof(name));
	} else
		dashStrCopy(name, "NEWFILE.TXT", sizeof(name));
	if (!dashPickPath("Save in which folder?", dir, DASH_PICK_FOLDER, NULL, dir, sizeof(dir)))
		return 0;
	if (!dashModalFilename("File name", name, sizeof(name) - 1, dir) || name[0] == '\0')
		return 0;
	if (snprintf(full, sizeof(full), "%s%s", dir, name) >= (int)sizeof(full)) {
		dashModalMessage(DI_WARNING, "Path too long", "Choose a shorter name or folder.", NULL);
		return 0;
	}
	return docWrite(d, full);
}

static int docSave(EdDoc *d)
{
	if (d->path[0] == '\0')
		return docSaveAs(d);
	return docWrite(d, d->path);
}

static void docClose(int i)
{
	docFree(&docs[i]);
	memmove(&docs[i], &docs[i + 1], sizeof(EdDoc) * (ndocs - i - 1));
	memset(&docs[ndocs - 1], 0, sizeof(EdDoc));
	ndocs--;
	if (active >= ndocs)
		active = ndocs - 1;
	if (active < 0)
		active = 0;
}

/* asks about unsaved changes; 1 = go ahead (saved or discarded) */
/* "Save changes?" in the Text Editor and the Hex Editor */
static const DashMenuItem save_menu[] = {
    {DI_SAVE, "Save", NULL, 0},
    {DI_CLOSE, "Don't save", NULL, 0},
    {DI_BACK, "Cancel", NULL, 0}};

static int confirmDiscard(EdDoc *d)
{
	char sub[96];
	int r;

	if (!d->dirty)
		return 1;
	snprintf(sub, sizeof(sub), "%s has unsaved changes", docName(d));
	r = dashModalMenu("Save changes?", sub, save_menu, 3, 0);
	if (r == 0)
		return docSave(d);
	return r == 1;
}

//--------------------------------------------------------------
// Docked keyboard
//--------------------------------------------------------------
static void kbTypeKey(EdDoc *d)
{
	int key = dashKeyboardPress(&editor_keyboard);

	if (key > 0) {
		char c = (char)key;
		docInsert(d, &c, 1);
	}
}

//--------------------------------------------------------------
// Cursor movement
//--------------------------------------------------------------
static void moveCursor(EdDoc *d, int to, int keep_col)
{
	if (to < 0)
		to = 0;
	if (to > d->len)
		to = d->len;
	d->cur = to;
	if (!keep_col)
		d->want_col = docColOf(d, to);
}

static void moveLines(EdDoc *d, int delta)
{
	int line = docLineOf(d, d->cur) + delta;

	if (line < 0)
		line = 0;
	if (line >= d->nlines)
		line = d->nlines - 1;
	moveCursor(d, docOffAtCol(d, line, d->want_col), 1);
}

//--------------------------------------------------------------
// USB keyboard: typed characters and editing keys (codes as used by the
// classic editor). Returns 1 = open the menu, 2 = leave the editor.
//--------------------------------------------------------------
static int usbKeys(EdDoc *d, int rows)
{
	char key;

	if (!setting->usbkbd_used || dashWorkerIoBusy() || dashIopParked() || !ensureUsbKeyboardReady())
		return 0;
	while (PS2KbdRead(&key)) {
		if (key == PS2KBD_ESCAPE_KEY) {
			int line;
			if (!PS2KbdRead(&key))
				break;
			line = docLineOf(d, d->cur);
			switch ((unsigned char)key) {
				case 0x29: /* right */
					moveCursor(d, d->cur + 1, 0);
					break;
				case 0x2a: /* left */
					moveCursor(d, d->cur - 1, 0);
					break;
				case 0x2b: /* down */
					moveLines(d, 1);
					break;
				case 0x2c: /* up */
					moveLines(d, -1);
					break;
				case 0x24: /* home */
					moveCursor(d, d->lines[line], 0);
					break;
				case 0x27: /* end */
					moveCursor(d, docLineEnd(d, line), 0);
					break;
				case 0x25: /* page up */
					moveLines(d, -(rows - 1));
					break;
				case 0x28: /* page down */
					moveLines(d, rows - 1);
					break;
				case 0x26: /* delete */
					docDeleteForward(d);
					break;
				case 0x1b: /* escape */
					return 1;
			}
		} else if (key == 0x07)
			docBackspace(d);
		else if (key == 0x0a)
			docInsert(d, "\n", 1);
		else if (key == 0x09 || (unsigned char)key >= 0x20)
			docInsert(d, &key, 1);
	}
	return 0;
}

//--------------------------------------------------------------
// Menu
//--------------------------------------------------------------
enum {
	M_NEW = 0,
	M_OPEN,
	M_SAVE,
	M_SAVEAS,
	M_CLOSE,
	M_SWITCH,
	M_SELECT,
	M_SELALL,
	M_COPY,
	M_CUT,
	M_PASTE,
	M_EOL,
	M_GOTO_TOP,
	M_GOTO_END,
	M_LAUNCH,
	M_EXIT
};

static void switchDocument(void)
{
	DashMenuItem items[ED_MAX_DOCS];
	char details[ED_MAX_DOCS][16];
	int i, r;

	for (i = 0; i < ndocs; i++) {
		items[i].icon = (i == active) ? DI_CHECK : DI_TEXT;
		items[i].label = docs[i].path[0] ? docs[i].path : "New document";
		snprintf(details[i], sizeof(details[i]), "%s", docs[i].dirty ? "unsaved" : "");
		items[i].detail = details[i][0] ? details[i] : NULL;
		items[i].disabled = 0;
	}
	r = dashModalMenu("Documents", NULL, items, ndocs, active);
	if (r >= 0)
		active = r;
}

static void copySelection(EdDoc *d, int cut)
{
	int a, b;

	if (!selRange(d, &a, &b)) {
		dashToast(DASH_TOAST_INFO, "Nothing selected (Square starts a selection)");
		return;
	}
	free(clip);
	clip = (char *)malloc(b - a + 1);
	if (clip == NULL) {
		clip_len = 0;
		return;
	}
	memcpy(clip, d->buf + a, b - a);
	clip_len = b - a;
	if (cut)
		docDelete(d, a, b);
	else
		d->mark = -1;
	dashToast(DASH_TOAST_INFO, cut ? localizeDashboardText("Cut") : "Copied");
}

/* returns an M_* action, or -1 */
static int editorMenu(EdDoc *d)
{
	DashMenuItem items[16];
	int ids[16], n = 0, r, a, b;
	int has_sel = selRange(d, &a, &b);

#define ADD(ic, lab, det, dis, id) \
	(items[n].icon = (ic), items[n].label = (lab), items[n].detail = (det), items[n].disabled = (dis), ids[n++] = (id))
	ADD(DI_SAVE, "Save", d->dirty ? "unsaved" : NULL, 0, M_SAVE);
	ADD(DI_EDIT, "Save as...", NULL, 0, M_SAVEAS);
	ADD(DI_NEW_DOCUMENT, "New document", NULL, ndocs >= ED_MAX_DOCS, M_NEW);
	ADD(DI_FOLDER, "Open...", NULL, ndocs >= ED_MAX_DOCS, M_OPEN);
	if (ndocs > 1)
		ADD(DI_VIEW_LIST, "Switch document...", NULL, 0, M_SWITCH);
	ADD(d->mark >= 0 ? DI_CLEAR_SELECTION : DI_PIN, d->mark >= 0 ? "Stop selecting" : "Start selecting", "Square", 0, M_SELECT);
	ADD(DI_SELECT_ALL, "Select all", NULL, d->len == 0, M_SELALL);
	ADD(DI_COPY, "Copy", NULL, !has_sel, M_COPY);
	ADD(DI_CUT, "Cut", NULL, !has_sel, M_CUT);
	ADD(DI_PLUS, "Paste", NULL, clip == NULL, M_PASTE);
	ADD(DI_SETTINGS, "Line endings", eol_names[d->eol], 0, M_EOL);
	ADD(DI_BTN_UP, "Go to the start", NULL, 0, M_GOTO_TOP);
	ADD(DI_BTN_DOWN, "Go to the end", NULL, 0, M_GOTO_END);
	ADD(DI_LAUNCH_ARGS, "Use as launch arguments", NULL, d->len == 0, M_LAUNCH);
	ADD(DI_CLOSE, "Close document", NULL, 0, M_CLOSE);
	ADD(DI_BACK, "Exit Text Editor", NULL, 0, M_EXIT);
#undef ADD
	r = dashModalMenu("Text Editor", docs[active].path[0] ? docs[active].path : "New document", items, n, 0);
	return (r < 0) ? -1 : ids[r];
}

//--------------------------------------------------------------
// Drawing
//--------------------------------------------------------------
#define ED_X 40.0f
#define ED_Y 60.0f
#define ED_W 560.0f
#define ED_ROW 17.0f
#define ED_CW 8.0f
#define ED_GUTTER 48.0f
#define ED_KB_PADDING 6.0f
#define ED_KB_BOTTOM (DASH_VH - 59.0f)

static float keyboardTop(void)
{
	return ED_KB_BOTTOM - dashKeyboardHeight() - 2 * ED_KB_PADDING;
}

static int visibleRows(void)
{
	/* Text panel extends 4px below the last row; leave another 4px gap. */
	float bottom = kb_shown ? keyboardTop() - 8 : DASH_VH - 70;

	return (int)((bottom - ED_Y) / ED_ROW);
}

static int visibleCols(void)
{
	return (int)((ED_W - ED_GUTTER - 16) / ED_CW);
}

static void scrollToCursor(EdDoc *d, int rows, int cols)
{
	int line = docLineOf(d, d->cur), col = docColOf(d, d->cur);

	if (line < d->top)
		d->top = line;
	if (line >= d->top + rows)
		d->top = line - rows + 1;
	if (col < d->left)
		d->left = col;
	if (col >= d->left + cols)
		d->left = col - cols + 1;
	/* Fill the expanded viewport when hiding the keyboard near EOF. */
	if (d->top > d->nlines - rows)
		d->top = d->nlines - rows;
	if (d->top < 0)
		d->top = 0;
}

static void drawText(EdDoc *d, int rows, int cols)
{
	const float tx = ED_X + ED_GUTTER;
	const DashColor fg = DASH_C_TEXT, dim = DASH_C_TEXT3;
	int r, sa = 0, sb = 0, has_sel = selRange(d, &sa, &sb);
	int cur_line = docLineOf(d, d->cur);
	float h = rows * ED_ROW + 8;

	dashDrawPanelBox(ED_X - 6, ED_Y - 4, ED_W + 12, h, 12);
	for (r = 0; r < rows; r++) {
		int line = d->top + r, i, end, col = 0;
		float y = ED_Y + r * ED_ROW;
		char num[12];

		if (line >= d->nlines)
			break;
		if (line == cur_line)
			dashRect(ED_X, y, ED_W, ED_ROW, dashColorA(DASH_C_SELECT, 0.25f));
		snprintf(num, sizeof(num), "%d", line + 1);
		dashTextRight(ED_X + ED_GUTTER - 10, y + 1, num, DASH_FS_CAPTION, line == cur_line ? DASH_C_ACCENT : DASH_C_TEXT3);
		end = docLineEnd(d, line);
		/* selection background for this line */
		if (has_sel && sa <= end && sb >= d->lines[line]) {
			int a = sa > d->lines[line] ? docColOf(d, sa) : 0;
			int b = sb < end ? docColOf(d, sb) : docColOf(d, end) + 1;
			a -= d->left, b -= d->left;
			if (a < 0)
				a = 0;
			if (b > cols)
				b = cols;
			if (b > a)
				dashRect(tx + a * ED_CW, y, (b - a) * ED_CW, ED_ROW, dashColorA(DASH_C_SELECT, 0.9f));
		}
		dashSpriteBatchEnd();
		for (i = d->lines[line]; i < end; i++) {
			unsigned char ch = (unsigned char)d->buf[i];
			int next = (ch == '\t') ? (col / ED_TAB + 1) * ED_TAB : col + 1;
			if (col >= d->left + cols)
				break;
			if (col >= d->left && ch != '\t' && ch != ' ')
				dashFontMonoByte(ch < 0x20 ? '.' : ch, tx + (col - d->left) * ED_CW,
				                 y + 1, ch < 0x20 ? dim : fg);
			col = next;
		}
	}
	/* caret */
	if ((Timer() / 450) & 1 || kb_shown) {
		int line = cur_line - d->top, col = docColOf(d, d->cur) - d->left;
		if (line >= 0 && line < rows && col >= 0 && col <= cols)
			dashRect(tx + col * ED_CW - 1, ED_Y + line * ED_ROW + 1, 2, ED_ROW - 2, DASH_C_ACCENT);
	}
	if (d->nlines > rows)
		dashDrawScrollbar(ED_X + ED_W + 10, ED_Y, rows * ED_ROW, d->top, rows, d->nlines);
}

static void drawKeyboard(void)
{
	float top = keyboardTop();

	dashDrawPanelBox(ED_X - 6, top, ED_W + 12,
	                 dashKeyboardHeight() + 2 * ED_KB_PADDING, 12);

	dashKeyboardDraw(&editor_keyboard, DASH_VW * 0.5f, top + ED_KB_PADDING);
}

/* The editor is the scene while it is open: dialogs draw over it. */
static void editorScene(void)
{
	EdDoc *d;
	char status[160], title[MAX_PATH + 16];
	int line, col, rows = visibleRows(), cols = visibleCols();

	dashGfxBeginFrame();
	dashBgDraw();
	if (ndocs == 0)
		return;
	d = &docs[active];
	line = docLineOf(d, d->cur) + 1;
	col = docColOf(d, d->cur) + 1;
	snprintf(title, sizeof(title), "%s%s", docName(d), d->dirty ? "  *" : "");
	dashIcon(DI_TEXT, ED_X, 20, 22, DASH_C_ACCENT);
	dashTextFit(ED_X + 30, 20, 300, title, DASH_FS_LABEL, DASH_C_TEXT);
	snprintf(status, sizeof(status), "Line %d, Col %d  |  %s  |  %d bytes%s", line, col,
	         d->eol == EOL_CRLF ? "CR/LF" : d->eol == EOL_LF ? "LF" : "CR", d->len,
	         ndocs > 1 ? "" : "");
	dashTextRight(ED_X + ED_W, 26, status, DASH_FS_CAPTION, DASH_C_TEXT3);
	{
		char sub[MAX_PATH + 32];
		if (d->path[0] && ndocs > 1)
			snprintf(sub, sizeof(sub), "%s  |  document %d of %d", d->path, active + 1, ndocs);
		else if (d->path[0])
			dashStrCopy(sub, d->path, sizeof(sub));
		else if (ndocs > 1)
			snprintf(sub, sizeof(sub), "New document  |  document %d of %d", active + 1, ndocs);
		else
			snprintf(sub, sizeof(sub), "New document");
		dashTextScroll(ED_X + 30, 42, ED_W - 30, sub, DASH_FS_CAPTION, DASH_C_TEXT3);
	}
	drawText(d, rows, cols);
	if (kb_shown) {
		DashHint h[] = {{dashOkIcon(), NULL, "Type"}, {dashBackIcon(), NULL, "Delete"}, {DI_BTN_SQUARE, NULL, "Caps"},
		                {-1, "L1/R1", "Move"}, {-1, "SELECT", "Hide keys"}, {DI_BTN_TRIANGLE, NULL, "Options"}};
		drawKeyboard();
		dashDrawHints(h, 6);
	} else {
		DashHint h[] = {{DI_BTN_SQUARE, NULL, d->mark >= 0 ? "End select" : "Select"},
		                {-1, "L1/R1", "Line"}, {-1, "L2/R2", "Page"}, {-1, "SELECT", "Keys"},
		                {DI_BTN_TRIANGLE, NULL, "Options"}, {dashBackIcon(), NULL, "Exit"}};
		dashDrawHints(h, 6);
	}
	dashDrawToasts();
	dashDrawActivity();
}

//--------------------------------------------------------------
// Entry
//--------------------------------------------------------------
static int leaveEditor(void)
{
	int i;

	for (i = 0; i < ndocs; i++) {
		active = i;
		if (!confirmDiscard(&docs[i]))
			return 0;
	}
	return 1;
}

static void closeAll(void)
{
	while (ndocs > 0)
		docClose(ndocs - 1);
	free(clip);
	clip = NULL;
	clip_len = 0;
}

/* the editor with nothing open: offer New / Open / Exit */
static int emptyMenu(void)
{
	DashMenuItem items[] = {
	    {DI_NEW_DOCUMENT, "New document", NULL, 0},
	    {DI_FOLDER, "Open...", NULL, 0},
	    {DI_BACK, "Exit Text Editor", NULL, 0}};
	char path[MAX_PATH];
	int r = dashModalMenu("Text Editor", "No document open", items, 3, 0);

	if (r == 0)
		return docNew();
	if (r == 1) {
		path[0] = '\0';
		if (dashPickPath("Open a text file", LastDir, DASH_PICK_FILE, NULL, path, sizeof(path)))
			return docOpenPath(path);
		return 1; /* stay; the menu comes back */
	}
	return 0;
}

int dashTextEditor(const char *path)
{
	return dashTextEditorEx(path, 0);
}

int dashTextEditorEx(const char *path, int create_if_missing)
{
	int result = TEXTEDITOR_RESULT_EXIT;
	DashInput in;

	ndocs = 0;
	active = 0;
	kb_shown = 0;
	dashKeyboardInit(&editor_keyboard, setting->virtual_keyboard_layout);
	dashFontInit(); /* the legacy glyph atlas draws the text */
	dashPushSceneOverride(editorScene);
	create_missing = create_if_missing;
	if (path != NULL && path[0])
		docOpenPath(path);
	create_missing = 0;
	dashInputFlush();

	while (1) {
		EdDoc *d;
		int rows, cols, action = -1;

		if (dashDashboardPending()) {
			if (dashDashboardConfirmLeave(leaveEditor))
				goto done;
			continue;
		}
		if (ndocs == 0) {
			if (!emptyMenu())
				break;
			continue;
		}
		d = &docs[active];
		rows = visibleRows();
		cols = visibleCols();

		dashInputPollEx(&in, 0);
		if (dashDashboardPending())
			continue;
		if (usbKeys(d, rows) == 1)
			action = editorMenu(d);

		if (in.pressed & DASH_IN_MENU)
			action = editorMenu(d);
		else if (in.pressed & DASH_IN_SELECT)
			kb_shown = !kb_shown;
		else if (kb_shown) {
			/* D-pad selects keys; the right stick moves the document cursor.
			 * raw_new keeps these separate from the shared navigation mapping. */
			if (in.raw_new & PAD_UP)
				dashKeyboardMove(&editor_keyboard, 0, -1);
			if (in.raw_new & PAD_DOWN)
				dashKeyboardMove(&editor_keyboard, 0, 1);
			if (in.raw_new & PAD_LEFT)
				dashKeyboardMove(&editor_keyboard, -1, 0);
			if (in.raw_new & PAD_RIGHT)
				dashKeyboardMove(&editor_keyboard, 1, 0);
			if (in.raw_new & PAD_R3_H0)
				moveCursor(d, d->cur - 1, 0);
			if (in.raw_new & PAD_R3_H1)
				moveCursor(d, d->cur + 1, 0);
			if (in.raw_new & PAD_R3_V0)
				moveLines(d, -1);
			if (in.raw_new & PAD_R3_V1)
				moveLines(d, 1);
			if ((in.pressed & DASH_IN_OK) ||
			    ((in.text_repeat & DASH_IN_OK) && dashKeyboardCanRepeat(&editor_keyboard)))
				kbTypeKey(d);
			if (in.text_repeat & DASH_IN_BACK)
				docBackspace(d);
			if (in.pressed & DASH_IN_FAV)
				editor_keyboard.caps = !editor_keyboard.caps;
			if (in.pressed & DASH_IN_L1)
				moveCursor(d, d->cur - 1, 0);
			if (in.pressed & DASH_IN_R1)
				moveCursor(d, d->cur + 1, 0);
			if (in.pressed & DASH_IN_L2)
				moveLines(d, -1);
			if (in.pressed & DASH_IN_R2)
				moveLines(d, 1);
		} else {
			int line = docLineOf(d, d->cur);
			if (in.pressed & DASH_IN_UP)
				moveLines(d, -1);
			if (in.pressed & DASH_IN_DOWN)
				moveLines(d, 1);
			if (in.pressed & DASH_IN_LEFT)
				moveCursor(d, d->cur - 1, 0);
			if (in.pressed & DASH_IN_RIGHT)
				moveCursor(d, d->cur + 1, 0);
			if (in.pressed & DASH_IN_L2)
				moveLines(d, -(rows - 1));
			if (in.pressed & DASH_IN_R2)
				moveLines(d, rows - 1);
			if (in.pressed & DASH_IN_L1)
				moveCursor(d, d->lines[line], 0);
			if (in.pressed & DASH_IN_R1)
				moveCursor(d, docLineEnd(d, line), 0);
			if (in.pressed & DASH_IN_FAV)
				d->mark = (d->mark >= 0) ? -1 : d->cur;
			if (in.pressed & DASH_IN_OK)
				kb_shown = 1;
			if (in.pressed & DASH_IN_BACK)
				action = M_EXIT;
		}

		switch (action) {
			case M_SAVE:
				docSave(d);
				break;
			case M_SAVEAS:
				docSaveAs(d);
				break;
			case M_NEW:
				docNew();
				break;
			case M_OPEN: {
				char p[MAX_PATH], dir[MAX_PATH];
				const char *slash;
				dashStrCopy(dir, d->path[0] ? d->path : LastDir, sizeof(dir));
				slash = strrchr(dir, '/');
				if (slash != NULL)
					dir[slash - dir + 1] = '\0';
				p[0] = '\0';
				if (dashPickPath("Open a text file", dir, DASH_PICK_FILE, NULL, p, sizeof(p)))
					docOpenPath(p);
				break;
			}
			case M_SWITCH:
				switchDocument();
				break;
			case M_SELECT:
				d->mark = (d->mark >= 0) ? -1 : d->cur;
				break;
			case M_SELALL:
				d->mark = 0;
				moveCursor(d, d->len, 0);
				break;
			case M_COPY:
				copySelection(d, 0);
				break;
			case M_CUT:
				copySelection(d, 1);
				break;
			case M_PASTE:
				if (clip != NULL)
					docInsert(d, clip, clip_len);
				break;
			case M_EOL: {
				int r = dashModalChoice("Line endings", eol_names, EOL_COUNT, d->eol);
				if (r >= 0 && r != d->eol) {
					d->eol = r;
					d->dirty = 1;
				}
				break;
			}
			case M_GOTO_TOP:
				moveCursor(d, 0, 0);
				break;
			case M_GOTO_END:
				moveCursor(d, d->len, 0);
				break;
			case M_LAUNCH: {
				char msg[MAX_PATH];
				msg[0] = '\0';
				if (LaunchArgsLoadFromBuffer(d->path, d->buf, d->len, msg, sizeof(msg)) > 0) {
					if (leaveEditor()) {
						result = TEXTEDITOR_RESULT_LAUNCH_ARGS;
						goto done;
					}
					LaunchArgsClear();
				} else
					dashModalMessage(DI_WARNING, "Arguments not usable", "Write one argument per line.", msg[0] ? msg : NULL);
				break;
			}
			case M_CLOSE:
				if (confirmDiscard(d))
					docClose(active);
				break;
			case M_EXIT:
				if (leaveEditor())
					goto done;
				break;
		}
		if (ndocs == 0)
			continue;
		d = &docs[active];
		rows = visibleRows();
		scrollToCursor(d, rows, cols);
		dashVisualUpdate();
		dashRenderScene();
		dashGfxPresent();
	}
done:
	if (ndocs > 0 && docs[active].path[0]) {
		/* like the classic editor: the folder of the last document */
		const char *slash = strrchr(docs[active].path, '/');
		if (slash != NULL && slash - docs[active].path + 1 < MAX_NAME) {
			memcpy(LastDir, docs[active].path, slash - docs[active].path + 1);
			LastDir[slash - docs[active].path + 1] = '\0';
		}
	}
	closeAll();
	dashPopSceneOverride();
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
	return result;
}
//--------------------------------------------------------------
// Hex Editor: overwrite the bytes of any file up to 512 KB. No insert,
// delete or undo: Exit without saving keeps the file as it was. It shares
// the Text Editor's worker IO, font and layout.
//--------------------------------------------------------------
#define HX_COLS 8

static unsigned char *hx_buf;
static int hx_len, hx_cur, hx_top, hx_dirty, hx_edit, hx_nib;
static const char *hx_path;

static int hexDigit(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

/* "1A2B" or "0x1a2b"; -1 when it is not a hex number */
static int hexParse(const char *s)
{
	int v = 0, n = 0, d;

	if (s[0] == '0' && (s[1] | 0x20) == 'x')
		s += 2;
	for (; *s; s++) {
		if ((d = hexDigit(*s)) < 0 || ++n > 7)
			return -1;
		v = v * 16 + d;
	}
	return n ? v : -1;
}

/* a typed digit: high nibble, then low nibble and on to the next byte */
static void hexType(int d)
{
	unsigned char *b = &hx_buf[hx_cur];

	*b = hx_nib ? (*b & 0xF0) | d : (d << 4) | (*b & 0x0F);
	hx_dirty = 1;
	if (hx_nib && hx_cur < hx_len - 1)
		hx_cur++;
	hx_nib = !hx_nib;
}

/* "0001A0  41 42 43 44 45 46 47 48  ABCDEFGH"; byte i is at column 8 + 3 * i */
static void hexRow(char *out, int off)
{
	static const char hx[] = "0123456789ABCDEF";
	int i, n = sprintf(out, "%06X ", off);

	for (i = 0; i < HX_COLS; i++, n += 3) {
		int k = off + i < hx_len;
		out[n] = ' ';
		out[n + 1] = k ? hx[hx_buf[off + i] >> 4] : ' ';
		out[n + 2] = k ? hx[hx_buf[off + i] & 15] : ' ';
	}
	out[n++] = ' ';
	out[n++] = ' ';
	for (i = 0; i < HX_COLS && off + i < hx_len; i++) {
		unsigned char c = hx_buf[off + i];
		out[n++] = (c < 0x20 || c > 0x7E) ? '.' : c;
	}
	out[n] = '\0';
}

static void hexMove(int to)
{
	hx_cur = to < 0 ? 0 : to >= hx_len ? hx_len - 1 : to;
	hx_nib = 0;
}

static void hexScene(void)
{
	char line[64];
	int r, i, rows = visibleRows(), cr = hx_cur / HX_COLS - hx_top, cc = hx_cur % HX_COLS;

	dashGfxBeginFrame();
	dashBgDraw();
	snprintf(line, sizeof(line), "Offset %06X  |  %d bytes", hx_cur, hx_len);
	dashIcon(DI_EDIT, ED_X, 20, 22, DASH_C_ACCENT);
	dashTextFit(ED_X + 30, 20, 300, hx_dirty ? "Hex Editor  *" : "Hex Editor", DASH_FS_LABEL, DASH_C_TEXT);
	dashTextRight(ED_X + ED_W, 26, line, DASH_FS_CAPTION, DASH_C_TEXT3);
	dashTextScroll(ED_X + 30, 42, ED_W - 30, hx_path, DASH_FS_CAPTION, DASH_C_TEXT3);
	dashDrawPanelBox(ED_X - 6, ED_Y - 4, ED_W + 12, rows * ED_ROW + 8, 12);
	if (cr >= 0 && cr < rows) {
		float y = ED_Y + cr * ED_ROW;
		dashRect(ED_X, y, ED_W, ED_ROW, dashColorA(DASH_C_SELECT, 0.25f));
		dashRect(ED_X + 8 + (8 + 3 * cc + (hx_edit ? 0 : hx_nib)) * ED_CW, y,
		         (hx_edit || !hx_nib ? 2 : 1) * ED_CW, ED_ROW, dashColorA(hx_edit ? DASH_C_ACCENT : DASH_C_SELECT, 0.9f));
		dashRect(ED_X + 8 + (33 + cc) * ED_CW, y, ED_CW, ED_ROW, dashColorA(DASH_C_SELECT, 0.9f));
	}
	dashSpriteBatchEnd();
	for (r = 0; r < rows && (hx_top + r) * HX_COLS < hx_len; r++) {
		hexRow(line, (hx_top + r) * HX_COLS);
		for (i = 0; line[i]; i++)
			if (line[i] != ' ')
				dashFontMonoByte((unsigned char)line[i], ED_X + 8 + i * ED_CW, ED_Y + r * ED_ROW + 1,
				                 i < 7 ? DASH_C_TEXT3 : DASH_C_TEXT);
	}
	if ((hx_len + HX_COLS - 1) / HX_COLS > rows)
		dashDrawScrollbar(ED_X + ED_W + 10, ED_Y, rows * ED_ROW, hx_top, rows, (hx_len + HX_COLS - 1) / HX_COLS);
	if (hx_edit) {
		DashHint h[] = {{-1, "Up/Down", "+1 -1"}, {-1, "L1/R1", "-16 +16"}, {-1, "Left/Right", "Byte"},
		                {dashOkIcon(), NULL, "Done"}};
		dashDrawHints(h, 4);
	} else {
		DashHint h[] = {{dashOkIcon(), NULL, "Change"}, {-1, "L2/R2", "Page"}, {-1, "L1/R1", "Start/End"},
		                {DI_BTN_TRIANGLE, NULL, "Options"}, {dashBackIcon(), NULL, "Exit"}};
		dashDrawHints(h, 5);
	}
	dashDrawToasts();
	dashDrawActivity();
}

/* overwrite the file's bytes in place, then read it back and compare */
static int hexSave(void)
{
	EdIoJob *j;
	int ok;

	if (!strncmp(hx_path, "cdfs", 4)) {
		dashModalMessage(DI_WARNING, "Read-only device", "Discs cannot be written.", NULL);
		return 0;
	}
	if (filerConfirmExploitModify(hx_path, NULL) <= 0) /* the File Browser's system-folder warning */
		return 0;
	j = (EdIoJob *)calloc(1, sizeof(EdIoJob));
	if (j == NULL || (j->text = (char *)malloc(hx_len)) == NULL) {
		free(j);
		return 0;
	}
	j->op = 2;
	j->len = hx_len;
	memcpy(j->text, hx_buf, hx_len);
	dashStrCopy(j->path, hx_path, sizeof(j->path));
	if (!edIo(j, "Saving file")) {
		dashToast(DASH_TOAST_INFO, "Saving continues in the background");
		return 0;
	}
	ok = j->ret == 0;
	free(j->text);
	j->text = NULL;
	j->op = 0;
	if (ok) {
		if (!edIo(j, "Checking file"))
			return 0;
		ok = j->ret == 0 && j->len == hx_len && !memcmp(j->text, hx_buf, hx_len);
		free(j->text);
	}
	free(j);
	if (!ok) {
		dashModalMessage(DI_WARNING, "File not saved", "The file could not be written, or reads back different. The device may be read-only or full.", hx_path);
		return 0;
	}
	hx_dirty = 0;
	dashToast(DASH_TOAST_OK, "Saved and checked");
	return 1;
}

static int hexLeave(void)
{
	int r;

	if (!hx_dirty)
		return 1;
	r = dashModalMenu("Save changes?", hx_path, save_menu, 3, 0);
	return r == 0 ? hexSave() : r == 1;
}

void dashHexEditor(const char *path)
{
	EdIoJob *j = (EdIoJob *)calloc(1, sizeof(EdIoJob));
	DashInput in;
	char key, off[12];

	if (j == NULL)
		return;
	dashStrCopy(j->path, path, sizeof(j->path));
	if (!edIo(j, "Opening file"))
		return;
	if (j->ret != 0 || j->len == 0) {
		dashModalMessage(DI_WARNING, "File not opened",
		                 j->ret == -2 ? "The file is larger than 512 KB, the Hex Editor's limit." :
		                 j->ret == 0  ? "The file is empty." :
		                                "The file could not be read. Check that the device is connected.",
		                 path);
		free(j->text);
		free(j);
		return;
	}
	hx_buf = (unsigned char *)j->text;
	hx_len = j->len;
	free(j);
	hx_path = path;
	hx_cur = hx_top = hx_dirty = hx_edit = hx_nib = kb_shown = 0; /* the Text Editor's keyboard may be left up */
	dashFontInit();
	dashPushSceneOverride(hexScene);
	dashInputFlush();
	while (1) {
		int rows = visibleRows(), act = -1;

		if (dashDashboardPending()) {
			if (dashDashboardConfirmLeave(hexLeave))
				break;
			continue;
		}
		dashInputPollEx(&in, 0);
		if (dashDashboardPending())
			continue;
		if (setting->usbkbd_used && !dashWorkerIoBusy() && !dashIopParked() && ensureUsbKeyboardReady())
			while (PS2KbdRead(&key)) {
				if (key == PS2KBD_ESCAPE_KEY && PS2KbdRead(&key)) {
					if (key == 0x29 || key == 0x2a)
						hexMove(hx_cur + (key == 0x29 ? 1 : -1));
					else if (key == 0x2b || key == 0x2c)
						hexMove(hx_cur + (key == 0x2b ? HX_COLS : -HX_COLS));
					else if (key == 0x1b)
						act = 0;
				} else if (hexDigit(key) >= 0)
					hexType(hexDigit(key));
			}
		if (hx_edit) {
			int d = (in.pressed & DASH_IN_UP) ? 1 : (in.pressed & DASH_IN_DOWN) ? -1 :
			        (in.pressed & DASH_IN_R1) ? 16 : (in.pressed & DASH_IN_L1) ? -16 : 0;
			if (d) { /* hx_cur, after any key the USB keyboard moved it by */
				hx_buf[hx_cur] = (unsigned char)(hx_buf[hx_cur] + d);
				hx_dirty = 1;
			}
			if (in.pressed & (DASH_IN_LEFT | DASH_IN_RIGHT))
				hexMove(hx_cur + ((in.pressed & DASH_IN_RIGHT) ? 1 : -1));
			if (in.pressed & (DASH_IN_OK | DASH_IN_BACK))
				hx_edit = 0;
		} else {
			if (in.pressed & (DASH_IN_UP | DASH_IN_DOWN))
				hexMove(hx_cur + ((in.pressed & DASH_IN_DOWN) ? HX_COLS : -HX_COLS));
			if (in.pressed & (DASH_IN_LEFT | DASH_IN_RIGHT))
				hexMove(hx_cur + ((in.pressed & DASH_IN_RIGHT) ? 1 : -1));
			if (in.pressed & (DASH_IN_L2 | DASH_IN_R2))
				hexMove(hx_cur + ((in.pressed & DASH_IN_R2) ? 1 : -1) * HX_COLS * (rows - 1));
			if (in.pressed & (DASH_IN_L1 | DASH_IN_R1))
				hexMove((in.pressed & DASH_IN_R1) ? hx_len - 1 : 0);
			if (in.pressed & DASH_IN_OK)
				hx_edit = 1, hx_nib = 0;
			if (in.pressed & DASH_IN_MENU)
				act = 0;
			if ((in.pressed & DASH_IN_BACK) && hexLeave())
				break;
		}
		if (act == 0) {
			static const DashMenuItem items[] = {
			    {DI_SAVE, "Save", NULL, 0},
			    {DI_BTN_DOWN, "Go to offset...", NULL, 0},
			    {DI_BACK, "Exit Hex Editor", NULL, 0}};
			int r = dashModalMenu("Hex Editor", hx_path, items, 3, 0);
			if (r == 0)
				hexSave();
			else if (r == 1) {
				off[0] = '\0';
				if (dashModalKeyboard("Offset (hex)", off, sizeof(off) - 1)) {
					int o = hexParse(off);
					if (o < 0 || o >= hx_len)
						dashToast(DASH_TOAST_WARN, "Not an offset in this file");
					else
						hexMove(o);
				}
			} else if (r == 2 && hexLeave())
				break;
		}
		if (hx_cur / HX_COLS < hx_top)
			hx_top = hx_cur / HX_COLS;
		if (hx_cur / HX_COLS >= hx_top + rows)
			hx_top = hx_cur / HX_COLS - rows + 1;
		dashVisualUpdate();
		dashRenderScene();
		dashGfxPresent();
	}
	free(hx_buf);
	hx_buf = NULL;
	dashPopSceneOverride();
	dashSoundPlay(DASH_SND_CANCEL);
	dashInputFlush();
}
//--------------------------------------------------------------
//End of file: dash_editor.c
//--------------------------------------------------------------
