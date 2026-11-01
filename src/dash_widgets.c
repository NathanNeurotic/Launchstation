//--------------------------------------------------------------
//File name:   dash_widgets.c
//Native dashboard dialogs that replace the classic wLaunchELF prompts:
//* dashModalKeyboard(): text entry with the user's virtual keyboard
//  layout (Settings > Input) and USB keyboard typing.
//* dashPickPath(): a file or folder picker. Folders are read by the
//  background worker, so the picker never freezes on a slow device, and
//  the user can always back out.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "editor_private.h"
#include "init.h"


//--------------------------------------------------------------
// Keyboard
//--------------------------------------------------------------
static int kbFilenameChar(unsigned char c)
{
	return isVirtualKeyboardFilenameChar(c);
}

enum { KA_TAB = 0, KA_RETURN, KA_SYMBOLS, KA_COUNT };

#define KB_KEY_H 22.0f
#define KB_KEY_GAP 3.0f
#define KB_CHAR_ROWS 4
#define KB_MIN_COLS 12
#define KB_SPACE_KEY (KB_CHAR_ROWS * VKEY_LAYOUT_COLS + 4)

static const char *const ka_names[KA_COUNT] = {"Tab", "Return", "Symbols"};
/* Follow the R3Z editor's extended glyph set, with an ASCII punctuation page
 * first so every layout can enter the same symbols without changing Caps. */
static const unsigned char kb_ascii_symbols[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

static int kbColumnCount(const DashKeyboard *kb)
{
	int ncols = getVirtualKeyboardLayoutColumnCount(kb->layout);

	/* Letters and Symbols share the same width and right-side action column. */
	return ncols < KB_MIN_COLS ? KB_MIN_COLS : ncols;
}

static int kbSymbolPageSize(const DashKeyboard *kb)
{
	return KB_CHAR_ROWS * kbColumnCount(kb);
}

static int kbSymbolPageCount(const DashKeyboard *kb)
{
	int size = kbSymbolPageSize(kb);

	return ((int)sizeof(kb_ascii_symbols) - 1 + EditorSpecialCharCount + size - 1) / size;
}

static unsigned char kbSymbolChar(const DashKeyboard *kb, int index)
{
	int row = index / VKEY_LAYOUT_COLS, col = index % VKEY_LAYOUT_COLS;
	int cols = kbColumnCount(kb), offset;

	if (index < 0 || row >= KB_CHAR_ROWS || col >= cols || kb->page <= 0)
		return 0;
	offset = (kb->page - 1) * kbSymbolPageSize(kb) + row * cols + col;
	if (offset < (int)sizeof(kb_ascii_symbols) - 1)
		return kb_ascii_symbols[offset];
	offset -= (int)sizeof(kb_ascii_symbols) - 1;
	return offset < EditorSpecialCharCount ? EditorSpecialChars[offset] : 0;
}

static int kbIsKey(const DashKeyboard *kb, int index)
{
	if (index == KB_SPACE_KEY)
		return 1;
	if (index < 0 || index >= KB_CHAR_ROWS * VKEY_LAYOUT_COLS)
		return 0;
	if (kb->page)
		return !kb->filename && kbSymbolChar(kb, index) != 0;
	if (!isVirtualKeyboardLayoutKey(kb->layout, index))
		return 0;
	return !kb->filename ||
	       (isVirtualKeyboardFilenameKey(kb->layout, index) &&
	        kbFilenameChar((unsigned char)getVirtualKeyboardFilenameChar(kb->layout, index, kb->caps)));
}

static int kbHasAction(const DashKeyboard *kb, int action)
{
	return action >= 0 && action < KA_COUNT && !kb->filename &&
	       (!kb->single_line || action == KA_SYMBOLS);
}

static int kbActionRow(int action)
{
	static const int rows[KA_COUNT] = {0, 1, KB_CHAR_ROWS};

	return rows[action];
}

static int kbActionColumn(const DashKeyboard *kb)
{
	/* Logical navigation column; side-button pixel spacing is independent. */
	return getVirtualKeyboardLayoutFirstColumn(kb->layout) + kbColumnCount(kb) + 1;
}

static int kbSelectInRow(DashKeyboard *kb, int row, int col)
{
	int key, a, distance, best_distance = VKEY_LAYOUT_COLS + 2;
	int best_key = -1, best_action = -1;

	for (key = 0; key < VKEY_LAYOUT_COLS; key++) {
		if (!kbIsKey(kb, row * VKEY_LAYOUT_COLS + key))
			continue;
		distance = abs(key - col);
		if (distance < best_distance) {
			best_key = row * VKEY_LAYOUT_COLS + key;
			best_distance = distance;
		}
	}
	for (a = 0; a < KA_COUNT; a++) {
		if (!kbHasAction(kb, a) || kbActionRow(a) != row)
			continue;
		distance = abs(kbActionColumn(kb) - col);
		if (distance < best_distance) {
			best_key = -1;
			best_action = a;
			best_distance = distance;
		}
	}
	if (best_key < 0 && best_action < 0)
		return 0;
	kb->action = best_action;
	if (best_key >= 0)
		kb->selected = best_key;
	return 1;
}

static void kbMoveFocus(DashKeyboard *kb, int dx, int dy)
{
	int row, col, tries, a;

	if (kb->action >= 0) {
		row = kbActionRow(kb->action);
		col = kbActionColumn(kb);
	} else {
		if (!kbIsKey(kb, kb->selected))
			kb->selected = KB_SPACE_KEY;
		row = kb->selected / VKEY_LAYOUT_COLS;
		col = kb->selected % VKEY_LAYOUT_COLS;
	}
	if (dx) {
		/* Include the side keys in the same row as their neighbouring letters. */
		for (tries = 0; tries < VKEY_LAYOUT_COLS + 2; tries++) {
			col += dx;
			if (col < -1)
				col = VKEY_LAYOUT_COLS;
			if (col > VKEY_LAYOUT_COLS)
				col = -1;
			for (a = 0; a < KA_COUNT; a++) {
				if (kbHasAction(kb, a) && kbActionRow(a) == row && kbActionColumn(kb) == col) {
					kb->action = a;
					return;
				}
			}
			if (col >= 0 && col < VKEY_LAYOUT_COLS && kbIsKey(kb, row * VKEY_LAYOUT_COLS + col)) {
				kb->action = -1;
				kb->selected = row * VKEY_LAYOUT_COLS + col;
				return;
			}
		}
	} else if (dy) {
		for (tries = 0; tries < VKEY_LAYOUT_ROWS; tries++) {
			row = (row + dy + VKEY_LAYOUT_ROWS) % VKEY_LAYOUT_ROWS;
			if (kbSelectInRow(kb, row, col))
				return;
		}
	}
}

void dashKeyboardMove(DashKeyboard *kb, int dx, int dy)
{
	int selected = kb->selected, action = kb->action;

	/* Space spans several columns. Reversing the move that entered it
	 * returns to the actual previous key rather than Space's fixed column. */
	if (action < 0 && selected == KB_SPACE_KEY &&
	    dx == -kb->space_dx && dy == -kb->space_dy &&
	    (kb->space_action >= 0 ? kbHasAction(kb, kb->space_action) :
	     (kb->space_selected != KB_SPACE_KEY && kbIsKey(kb, kb->space_selected)))) {
		kb->selected = kb->space_selected;
		kb->action = kb->space_action;
		return;
	}
	kbMoveFocus(kb, dx, dy);
	if (kb->action < 0 && kb->selected == KB_SPACE_KEY && (action >= 0 || selected != KB_SPACE_KEY)) {
		kb->space_selected = selected;
		kb->space_action = action;
		kb->space_dx = dx;
		kb->space_dy = dy;
	}
}

void dashKeyboardInit(DashKeyboard *kb, int layout)
{
	memset(kb, 0, sizeof(*kb));
	kb->layout = normalizeVirtualKeyboardLayout(layout);
	kb->action = -1;
	kb->space_selected = kb->space_action = -1;
	kb->selected = getVirtualKeyboardLayoutNextKey(kb->layout,
	                                               VKEY_LAYOUT_COLS + getVirtualKeyboardLayoutFirstColumn(kb->layout), 0, 0);
}

/* Return a typed byte, or zero when only the page changes. */
int dashKeyboardPress(DashKeyboard *kb)
{
	if (kb->action < 0) {
		if (!kbIsKey(kb, kb->selected))
			return 0;
		if (kb->selected == KB_SPACE_KEY)
			return ' ';
		if (kb->page)
			return kbSymbolChar(kb, kb->selected);
		return (unsigned char)(kb->filename ?
		       getVirtualKeyboardFilenameChar(kb->layout, kb->selected, kb->caps) :
		       getVirtualKeyboardLayoutChar(kb->layout, kb->selected, kb->caps));
	}
	if (!kbHasAction(kb, kb->action))
		return 0;
	switch (kb->action) {
		case KA_TAB: return '\t';
		case KA_RETURN: return '\n';
		case KA_SYMBOLS:
			kb->page = (kb->page + 1) % (kbSymbolPageCount(kb) + 1);
			kb->selected = KB_SPACE_KEY;
			kb->space_selected = kb->space_action = -1;
			break;
	}
	return 0;
}

int dashKeyboardCanRepeat(const DashKeyboard *kb)
{
	return kb->action < 0 ? kbIsKey(kb, kb->selected) :
	       kbHasAction(kb, kb->action) && kb->action != KA_SYMBOLS;
}

static float kbActionWidth(int action)
{
	return action == KA_SYMBOLS ? 90.0f : 60.0f;
}

static float kbActionX(int action, float center_x)
{
	/* Align the side buttons' right edges at a 40px screen inset. The narrower
	 * Tab/Return keys fit beside even a centered 13-column character grid. */
	return center_x + DASH_VW * 0.5f - 40.0f - kbActionWidth(action);
}

static float kbGridOrigin(float center_x, float kw, float gap)
{
	/* All native keyboards center column 7 (one-based), independently of
	 * visible keys, layout width, Caps, Symbols pages and side buttons. */
	return center_x - 6.0f * (kw + gap) - kw * 0.5f;
}

float dashKeyboardHeight(void)
{
	return VKEY_LAYOUT_ROWS * (KB_KEY_H + KB_KEY_GAP) - KB_KEY_GAP;
}

static void kbDrawActionSymbol(int action, float cx, float cy, DashColor color)
{
	/* Filtered, pixel-aligned atlas glyphs keep the arrowhead arms matched. */
	int icon = action == KA_TAB ? DI_KEY_TAB : DI_KEY_RETURN;
	float y = cy - 12.0f + (action == KA_RETURN ? 2.0f : 0.0f);

	dashIcon(icon, cx - 12.0f, y, 24.0f, color);
}

void dashKeyboardDraw(const DashKeyboard *kb, float center_x, float y0)
{
	const int layout = kb->layout;
	const int first_col = getVirtualKeyboardLayoutFirstColumn(layout);
	const int ncols = kbColumnCount(kb);
	const float kw = 30.0f, kh = KB_KEY_H, gap = KB_KEY_GAP;
	float gx = kbGridOrigin(center_x, kw, gap);
	int row, col, a;

	for (row = 0; row < VKEY_LAYOUT_ROWS; row++) {
		for (col = 0; col < ncols; col++) {
			int idx = row * VKEY_LAYOUT_COLS + first_col + col;
			float kx = gx + col * (kw + gap), ky = y0 + row * (kh + gap), key_w = kw;
			char label[2];
			int on = (kb->action < 0 && idx == kb->selected);

			if (!kbIsKey(kb, idx))
				continue;
			if (idx == KB_SPACE_KEY) {
				/* QWERTY: left edge of C (column 3) to right edge of . (column 9).
				 * Keep the same physical span for the other keyboard layouts. */
				kx = gx + (3 - first_col) * (kw + gap);
				key_w = 6 * (kw + gap) + kw;
			}
			if (on)
				dashDrawSelection(kx - 1, ky - 1, key_w + 2, kh + 2, 6, 1.0f);
			else
				dashPanel(kx, ky, key_w, kh, 6, DC(255, 255, 255, 16));
			if (idx == KB_SPACE_KEY)
				dashTextCenter(kx + key_w * 0.5f, ky + 2, "Space", DASH_FS_SMALL, dashItemColor(on));
			else if (kb->page) {
				/* Share the document's theme font and Bold setting while treating
				 * 0xFF as a literal character rather than a controller icon prefix. */
				dashFontMonoByte(kbSymbolChar(kb, idx), kx + key_w * 0.5f - 4,
				             ky + 4, dashItemColor(on));
			} else {
				label[0] = kb->filename ? getVirtualKeyboardFilenameChar(layout, idx, kb->caps) :
				           getVirtualKeyboardLayoutDisplayChar(layout, idx, kb->caps);
				label[1] = '\0';
				dashTextCenter(kx + key_w * 0.5f, ky + 2, label, DASH_FS_SMALL, dashItemColor(on));
			}
		}
	}
	for (a = 0; a < KA_COUNT; a++) {
		float aw = kbActionWidth(a);
		float ay = y0 + kbActionRow(a) * (kh + gap);
		float ax = kbActionX(a, center_x);
		int on = (kb->action == a);

		if (!kbHasAction(kb, a))
			continue;
		if (on)
			dashDrawSelection(ax - 1, ay - 1, aw + 2, kh + 2, 6, 1.0f);
		else
			dashPanel(ax, ay, aw, kh, 6, DC(255, 255, 255, 18));
		if (a == KA_SYMBOLS)
			dashTextCenter(ax + aw * 0.5f, ay + 2, ka_names[a], DASH_FS_SMALL, dashItemColor(on));
		else
			kbDrawActionSymbol(a, ax + aw * 0.5f, ay + kh * 0.5f, dashItemColor(on));
	}
}

static void kbInsert(char *buf, int max_len, int *cur, char c)
{
	int len = strlen(buf);

	if (len >= max_len || c == '\0')
		return;
	memmove(buf + *cur + 1, buf + *cur, len - *cur + 1);
	buf[*cur] = c;
	(*cur)++;
}

static void kbDelete(char *buf, int *cur)
{
	int len = strlen(buf);

	if (*cur <= 0)
		return;
	memmove(buf + *cur - 1, buf + *cur, len - *cur + 1);
	(*cur)--;
}

/* Portable filename characters: shared by pad and USB entry. Adapted from
 * saildot4k's filename prompts without changing free-form text dialogs. */


static int kbFilenameValid(const char *name, int max_len)
{
	size_t n = strlen(name);
	if (!n || n > (size_t)max_len || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
	if (name[n - 1] == '.' || name[n - 1] == ' ') return 0;
	while (*name) if (!kbFilenameChar((unsigned char)*name++)) return 0;
	return 1;
}

/* USB keyboard: typed characters and editing keys (same codes as the
 * classic keyboard). Returns 1 = done, -1 = cancel, 0 = keep going. */
static int kbUsb(char *buf, int max_len, int *cur, int filename)
{
	char key;
	int len;

	if (!setting->usbkbd_used || dashWorkerIoBusy() || dashIopParked())
		return 0;
	if (!ensureUsbKeyboardReady())
		return 0;
	while (PS2KbdRead(&key)) {
		len = strlen(buf);
		if (key == PS2KBD_ESCAPE_KEY) {
			if (!PS2KbdRead(&key))
				break;
			if (key == 0x29 && *cur < len)
				(*cur)++;
			else if (key == 0x2a && *cur > 0)
				(*cur)--;
			else if (key == 0x24)
				*cur = 0;
			else if (key == 0x27)
				*cur = len;
			else if (key == 0x26 && *cur < len)
				memmove(buf + *cur, buf + *cur + 1, len - *cur);
			else if (key == 0x1b)
				return -1;
		} else if (key == 0x07)
			kbDelete(buf, cur);
		else if (key == 0x0a)
			return 1;
		else if ((unsigned char)key >= 0x20 && (!filename || kbFilenameChar((unsigned char)key)))
			kbInsert(buf, max_len, cur, key);
	}
	return 0;
}

static void kbDrawHints(float center_x, float y, float available_width)
{
	DashHint hints[] = {{dashOkIcon(), NULL, "Type"}, {dashBackIcon(), NULL, "Delete"},
	                    {DI_BTN_SQUARE, NULL, "Caps"}, {DI_BTN_R3, NULL, "Cursor"},
	                    {-1, "SELECT", "Done"}, {DI_BTN_TRIANGLE, NULL, "Cancel"}};
	const int count = sizeof(hints) / sizeof(hints[0]);
	float width = 0.0f, gap, x;
	/* Font layouts put the x-height center at text_y + 16 * scale.
	 * Align that reference with the 22px button's center. */
	const float text_y = y + 11.0f - 16.0f * DASH_FS_SMALL;
	int i;

	for (i = 0; i < count; i++)
		width += dashButtonWidth(hints[i].button, hints[i].key) + 4.0f +
		         dashTextWidth(localizeDashboardText(hints[i].label), DASH_FS_SMALL);

	/* Distribute the spare width between complete icon/label groups. */
	gap = (available_width - width) / (count - 1);
	if (gap < 0.0f)
		gap = 0.0f;
	x = center_x - (width + gap * (count - 1)) * 0.5f;
	for (i = 0; i < count; i++) {
		if (i)
			x += gap;
		x += dashDrawButton(x, y, hints[i].button, hints[i].key) + 4.0f;
		x = dashTextS(x, text_y, localizeDashboardText(hints[i].label), DASH_FS_SMALL, DASH_C_TEXT2);
	}
}

static int modalKeyboard(const char *title, char *buf, int max_len, int filename, const char *directory)
{
	const int has_directory = directory != NULL && directory[0] != '\0';
	const float w = 572.0f, h = has_directory ? 300.0f : 276.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	char orig[256];
	DashKeyboard keyboard;
	int cur, result = 0;
	float open = 0.0f, title_age = 0.0f;
	DashInput in;

	if (max_len > (int)sizeof(orig) - 1)
		max_len = sizeof(orig) - 1;
	dashKeyboardInit(&keyboard, setting->virtual_keyboard_layout);
	keyboard.single_line = 1;
	keyboard.filename = filename;
	dashStrCopy(orig, buf, sizeof(orig));
	cur = strlen(buf);
	dashInputFlush();

	while (1) {
		int r;

		dashInputPollEx(&in, 0);
		if (dashDashboardPending())
			break;
		r = kbUsb(buf, max_len, &cur, filename);
		if (r > 0 && filename && !kbFilenameValid(buf, max_len)) {
			dashToast(DASH_TOAST_WARN, "Choose a valid filename without separators or reserved characters");
			r = 0;
		}
		if (r != 0) {
			result = (r > 0);
			break;
		}
		/* Match the editor: D-pad selects keys, right stick moves the caret. */
		if (in.raw_new & PAD_UP)
			dashKeyboardMove(&keyboard, 0, -1);
		if (in.raw_new & PAD_DOWN)
			dashKeyboardMove(&keyboard, 0, 1);
		if (in.raw_new & PAD_LEFT)
			dashKeyboardMove(&keyboard, -1, 0);
		if (in.raw_new & PAD_RIGHT)
			dashKeyboardMove(&keyboard, 1, 0);
		if (in.raw_new & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT))
			dashSoundPlay(DASH_SND_CURSOR);
		if ((in.raw_new & PAD_R3_H0) && cur > 0)
			cur--;
		if ((in.raw_new & PAD_R3_H1) && cur < (int)strlen(buf))
			cur++;
		if (in.pressed & DASH_IN_L1) {
			if (cur > 0)
				cur--;
		}
		if (in.pressed & DASH_IN_R1) {
			if (cur < (int)strlen(buf))
				cur++;
		}
		if (in.pressed & DASH_IN_FAV) { /* Square: caps, as on the classic keyboard */
			keyboard.caps = !keyboard.caps;
			/* A shifted key can become a hidden path separator. Keep focus visible. */
			if (keyboard.action < 0 && !kbIsKey(&keyboard, keyboard.selected))
				kbSelectInRow(&keyboard, keyboard.selected / VKEY_LAYOUT_COLS,
				              keyboard.selected % VKEY_LAYOUT_COLS);
		}
		if (in.text_repeat & DASH_IN_BACK) { /* cancel button deletes, as before */
			kbDelete(buf, &cur);
			dashSoundPlay(DASH_SND_CANCEL);
		}
		if (in.pressed & DASH_IN_SELECT) {
			if (filename && !kbFilenameValid(buf, max_len)) {
				dashToast(DASH_TOAST_WARN, "Choose a valid filename");
				continue;
			}
			result = 1;
			break;
		}
		if (in.pressed & DASH_IN_MENU) {
			result = 0;
			break;
		}
		if ((in.pressed & DASH_IN_OK) ||
		    ((in.text_repeat & DASH_IN_OK) && dashKeyboardCanRepeat(&keyboard))) {
			int key = dashKeyboardPress(&keyboard);

			dashSoundPlay(DASH_SND_CONFIRM);
			if (key >= 0x20 && (!filename || kbFilenameChar((unsigned char)key)))
				kbInsert(buf, max_len, &cur, (char)key);
		}

		{
			float dt = dashVisualUpdate();
			title_age += dt;
			open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		}
		{
			float oe = dashEaseOutCubic(open);
			float fx = x + 30, fy = y + (has_directory ? 80 : 56), fw = w - 60;
			float gy = fy + 48;

			dashRenderScene();
			dashModalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			dashModalPanel(x, y, w, h, oe);
			dashIcon(DI_EDIT, x + 24, y + 18, 22, DASH_C_ACCENT);
			dashTextS(x + 56, y + 18, title, DASH_FS_LABEL, DASH_C_TEXT);
			{
				char count[16];
				snprintf(count, sizeof(count), "%d/%d", (int)strlen(buf), max_len);
				dashTextRight(x + w - 26, y + 22, count, DASH_FS_CAPTION, DASH_C_TEXT3);
			}
			if (has_directory)
				dashTextScrollTimed(x + 30, y + 44, w - 60, directory, DASH_FS_CAPTION, DASH_C_TEXT3, title_age);

			/* text field with a blinking caret */
			dashPanel(fx, fy, fw, 36, 10, dashColorA(DASH_C_PANEL, 1.0f));
			dashPanelBorder(fx, fy, fw, 36, 10, dashColorA(DASH_C_ACCENT, 0.5f));
			{
				float tx = fx + 12, caret;
				float before = cur * 8.0f;
				int i;
				float shift = 0.0f;
				if (before > fw - 30)
					shift = before - (fw - 30);
				dashGfxScissor(fx + 6, fy + 2, fw - 12, 32);
				/* Match the editor's theme font and Bold, including literal 0xFF. */
				for (i = 0; buf[i]; i++)
					dashFontMonoByte((unsigned char)buf[i] < 0x20 ? '.' : (unsigned char)buf[i],
					             tx - shift + i * 8.0f, fy + 11, DASH_C_TEXT);
				caret = tx - shift + before;
				if ((Timer() / 400) & 1)
					dashRect(caret, fy + 8, 2, 20, DASH_C_ACCENT);
				dashGfxScissorReset();
			}

			dashKeyboardDraw(&keyboard, DASH_VW * 0.5f, gy);

			kbDrawHints(x + w * 0.5f, y + h - 36, w - 48);
			dashGfxPopOffset();
			dashGfxPopAlpha();
			dashGfxPresent();
		}
	}
	if (!result)
		dashStrCopy(buf, orig, max_len + 1);
	dashSoundPlay(result ? DASH_SND_CONFIRM : DASH_SND_CANCEL);
	dashInputFlush();
	return result;
}

//--------------------------------------------------------------
// Classic dialog replacements. While the dashboard owns the screen, the
// classic operations it calls (copy, delete, PSU, VMC mount...) ask their
// questions and report progress through these instead of drawing the
// classic pop-ups (see ynDialogOverride in gui.c).
//--------------------------------------------------------------
static int classic_progress;

/* the classic texts pad with blank lines; drop them */
static void classicClean(const char *in, char *out, int size, int one_line)
{
	int o = 0, pending_break = 0;

	while (*in == '\n' || *in == ' ')
		in++;
	for (; *in && o < size - 3; in++) {
		if (*in == '\n') {
			pending_break = 1;
			continue;
		}
		if (pending_break && o > 0) {
			if (one_line) {
				out[o++] = ' ';
				out[o++] = ' ';
			} else
				out[o++] = '\n';
		}
		pending_break = 0;
		out[o++] = *in;
	}
	out[o] = '\0';
}

static int classicYesNo(const char *message)
{
	char body[512];

	classicClean(message ? message : "", body, sizeof(body), 0);
	return dashModalConfirm("Please confirm", body, "Yes", 0) ? 1 : -1;
}

static void classicProgress(const char *message)
{
	char line[160];

	if (message == NULL) {
		if (classic_progress) {
			classic_progress = 0;
			dashLoadingEnd();
		}
		return;
	}
	classicClean(message, line, sizeof(line), 1);
	if (!dashLoadingActive()) {
		dashLoadingBegin("Working");
		classic_progress = 1;
	}
	dashLoadingStatus(line, 0, 0);
}

int dashModalKeyboard(const char *title, char *buf, int max_len)
{
	return modalKeyboard(title, buf, max_len, 0, NULL);
}

int dashModalFilename(const char *title, char *buf, int max_len, const char *directory)
{
	return modalKeyboard(title, buf, max_len, 1, directory);
}

static int classicKeyboard(char *out, int max)
{
	if (!dashModalKeyboard("Enter a name", out, max))
		return -1;
	return (int)strlen(out);
}

void dashClassicUiInstall(int on)
{
	ynDialogOverride = on ? classicYesNo : NULL;
	nonDialogOverride = on ? classicProgress : NULL;
	keyboardOverride = on ? classicKeyboard : NULL;
	if (!on && classic_progress) {
		classic_progress = 0;
		dashLoadingEnd();
	}
}

/* ends a progress overlay a classic operation left open */
void dashClassicUiDone(void)
{
	classicProgress(NULL);
}

//--------------------------------------------------------------
// IPv4 address editor: four octets, D-pad to pick and change them.
//--------------------------------------------------------------
static void ipParse(const char *s, int oct[4])
{
	int i;

	for (i = 0; i < 4; i++) {
		oct[i] = 0;
		while (*s >= '0' && *s <= '9') {
			oct[i] = oct[i] * 10 + (*s - '0');
			s++;
		}
		if (oct[i] > 255)
			oct[i] = 255;
		if (*s == '.')
			s++;
	}
}

int dashModalIp(const char *title, const char *help, char *ip_str, int size)
{
	int oct[4], orig[4], sel = 0, i, result = 0;
	float open = 0.0f, sel_anim = 0.0f;
	const float w = 420.0f, h = 210.0f;
	const float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	DashInput in;

	ipParse(ip_str, oct);
	memcpy(orig, oct, sizeof(orig));
	dashInputFlush();
	while (1) {
		int step = 0;

		dashInputPoll(&in);
		if (in.pressed & DASH_IN_LEFT) {
			sel = (sel + 3) % 4;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_RIGHT) {
			sel = (sel + 1) % 4;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_UP)
			step = 1;
		if (in.pressed & DASH_IN_DOWN)
			step = -1;
		if (in.pressed & (DASH_IN_R1 | DASH_IN_R2))
			step = 10;
		if (in.pressed & (DASH_IN_L1 | DASH_IN_L2))
			step = -10;
		if (step) {
			oct[sel] += step;
			if (oct[sel] < 0)
				oct[sel] = 0;
			if (oct[sel] > 255)
				oct[sel] = 255;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_OK) {
			result = 1;
			break;
		}
		if (dashDashboardPending() || (in.pressed & (DASH_IN_BACK | DASH_IN_MENU))) {
			memcpy(oct, orig, sizeof(oct));
			break;
		}
		float dt = dashVisualUpdate();
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), dt);
		{
			float oe = dashEaseOutCubic(open);
			const float bw = 70.0f, bgap = 20.0f;
			float bx0 = x + (w - (4 * bw + 3 * bgap)) * 0.5f, by = y + 76;

			dashRenderScene();
			dashModalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			dashModalPanel(x, y, w, h, oe);
			dashIcon(DI_NETWORK, x + 24, y + 18, 22, DASH_C_ACCENT);
			dashTextS(x + 56, y + 18, title, DASH_FS_LABEL, DASH_C_TEXT);
			if (help)
				dashTextFit(x + 24, y + 48, w - 48, help, DASH_FS_CAPTION, DASH_C_TEXT3);
			dashDrawSelection(bx0 + sel_anim * (bw + bgap), by, bw, 44, 10, 1.0f);
			for (i = 0; i < 4; i++) {
				char num[8];
				float bx = bx0 + i * (bw + bgap);
				snprintf(num, sizeof(num), "%d", oct[i]);
				if (i != sel)
					dashPanel(bx, by, bw, 44, 10, DC(255, 255, 255, 16));
				dashTextCenter(bx + bw * 0.5f, by + 10, num, DASH_FS_LABEL, dashItemColor(i == sel));
				if (i < 3)
					dashTextCenter(bx + bw + bgap * 0.5f, by + 12, ".", DASH_FS_LABEL, DASH_C_TEXT2);
				if (i == sel) {
					dashTextCenter(bx + bw * 0.5f, by - 20, "+", DASH_FS_BODY, DASH_C_ACCENT);
					dashTextCenter(bx + bw * 0.5f, by + 48, "-", DASH_FS_BODY, DASH_C_ACCENT);
				}
			}
			{
				float hx = x + 24, hy = y + h - 36;
				/* Pad tiles are slightly larger than their 22px logical slots.
				 * Match the 12px gap used by the credits sheet. */
				hx += dashDrawButton(hx, hy, DI_BTN_UP, NULL) + 12;
				hx += dashDrawButton(hx, hy, DI_BTN_DOWN, NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, localizeDashboardText("Change"), DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, -1, "L1") + 2;
				hx += dashDrawButton(hx, hy, -1, "R1") + 4;
				hx = dashTextS(hx, hy + 4.5f, "10", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				dashModalHintRow(hx, hy, dashOkIcon(), localizeDashboardText("OK"), dashBackIcon(), localizeDashboardText("Cancel"));
			}
			dashGfxPopOffset();
			dashGfxPopAlpha();
			dashGfxPresent();
		}
	}
	if (result)
		snprintf(ip_str, size, "%d.%d.%d.%d", oct[0], oct[1], oct[2], oct[3]);
	dashSoundPlay(result ? DASH_SND_CONFIRM : DASH_SND_CANCEL);
	dashInputFlush();
	return result;
}

//--------------------------------------------------------------
// File / folder picker
//--------------------------------------------------------------
#define PICK_MAX 1024

typedef struct
{
	char name[MAX_NAME];
	u8 dir;
} PickEntry;

typedef struct
{
	int gen;
	char path[MAX_PATH];
	FILEINFO *files;
	int n;
} PickListJob;

static struct
{
	int active;
	int gen;         /* bumped on every navigation; stale listings are dropped */
	int loading;
	char dir[MAX_PATH]; /* "" = device list */
	PickEntry *entries;
	int n;
	int mode;
	const char *ext;
	/* device list */
	DashDevice devs[DASH_MAX_DEVICES];
	int ndevs;
	/* a device being started for the picker */
	int starting;
	int start_ok;
	int start_done;
	char start_dir[24];
} pk;

static int pickCmp(const void *a, const void *b)
{
	const PickEntry *x = (const PickEntry *)a, *y = (const PickEntry *)b;

	if (x->dir != y->dir)
		return y->dir - x->dir;
	return stricmp(x->name, y->name);
}

static void pickListRun(void *arg)
{
	PickListJob *j = (PickListJob *)arg;

	dashWorkerStatus(j->path, 0, 0);
	j->n = getDir(j->path, j->files);
}

static void pickListDone(void *arg)
{
	PickListJob *j = (PickListJob *)arg;
	int i;

	if (pk.active && j->gen == pk.gen && pk.entries != NULL) {
		pk.n = 0;
		for (i = 0; i < j->n && pk.n < PICK_MAX; i++) {
			FILEINFO *f = &j->files[i];
			int is_dir = (f->stats.AttrFile & sceMcFileAttrSubdir) != 0;

			if (f->name[0] == '\0' || !strcmp(f->name, ".") || !strcmp(f->name, ".."))
				continue;
			if (!is_dir && (pk.mode == DASH_PICK_FOLDER || (pk.ext != NULL && !genCmpFileExt(f->name, pk.ext))))
				continue;
			if (!is_dir && pk.mode == DASH_PICK_LAUNCH && !genCmpFileExt(f->name, "ELF"))
				continue;
			dashStrCopy(pk.entries[pk.n].name, f->name, MAX_NAME);
			pk.entries[pk.n].dir = (u8)is_dir;
			pk.n++;
		}
		qsort(pk.entries, pk.n, sizeof(PickEntry), pickCmp);
		pk.loading = 0;
	}
	free(j->files);
	free(j);
}

static void pickMiscList(void);
static int pickIsMisc(const char *dir);

static void pickStartDone(const char *mount, int ok, void *user)
{
	(void)mount;
	(void)user;
	pk.start_done = 1;
	pk.start_ok = ok;
}

static void pickOpen(const char *dir)
{
	PickListJob *j;

	pk.gen++;
	pk.n = 0;
	dashStrCopy(pk.dir, dir, sizeof(pk.dir));
	if (dir[0] == '\0') {
		/* device list: no IO, straight from the device model */
		pk.ndevs = dashDevicesList(pk.devs, DASH_MAX_DEVICES - 1);
		if (pk.mode == DASH_PICK_LAUNCH) {
			DashDevice *d = &pk.devs[pk.ndevs++];
			memset(d, 0, sizeof(*d));
			d->icon = DI_TOOLS;
			d->state = DASH_DEV_READY;
			dashStrCopy(d->name, "Built-in commands", sizeof(d->name));
			dashStrCopy(d->browse, setting->Misc, sizeof(d->browse));
			dashStrCopy(d->status, "MISC", sizeof(d->status));
		}
		pk.loading = 0;
		return;
	}
	if (pickIsMisc(dir)) {
		pickMiscList();
		pk.loading = 0;
		return;
	}
	/* drivers / the HDD partition list for this folder: IOP modules are only ever
	 * loaded on the UI thread, never by the background worker */
	if (filerDirReadNeedsPrep(dir)) {
		int ready;

		if (!dashWorkerAwaitAll("Finishing up")) {
			pickOpen("");
			return;
		}
		dashLoadingBegin("Starting device");
		ready = filerPrepareDirRead(dir);
		dashLoadingEnd();
		if (!ready) {
			pickOpen("");
			dashToast(DASH_TOAST_WARN, "Device could not be opened");
			return;
		}
	}
	j = (PickListJob *)malloc(sizeof(PickListJob));
	if (j != NULL)
		j->files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	if (j == NULL || j->files == NULL) {
		free(j);
		pk.loading = 0;
		dashToast(DASH_TOAST_ERROR, "Not enough memory to list this folder");
		return;
	}
	j->gen = pk.gen;
	j->n = 0;
	dashStrCopy(j->path, dir, sizeof(j->path));
	pk.loading = 1;
	if (dashWorkerSubmit("Reading folder", DASH_JOB_VISIBLE, pickListRun, pickListDone, j) < 0) {
		free(j->files);
		free(j);
		pk.loading = 0;
		dashToast(DASH_TOAST_WARN, "Folder could not be queued; reopen it to retry");
	}
}

/* MISC commands (launch-key targets), in the classic MISC folder order */
static void pickMiscList(void)
{
	const char *cmds[] = {setting->Misc_PS2Disc, setting->Misc_FileBrowser, setting->Misc_PS2Browser,
	                      setting->Misc_PS2Net, setting->Misc_PS2PowerOff, setting->Misc_HddManager,
	                      setting->Misc_TextEditor, setting->Misc_Configure, setting->Misc_ShowFont,
	                      setting->Misc_Debug_Info, setting->Misc_About_uLE, setting->Misc_Show_Build_Info,
	                      setting->Misc_OSDSYS, setting->Misc_Exploit_Installer, setting->Misc_Reboot_IOP};
	int misc_len = strlen(setting->Misc);
	unsigned int i;

	pk.n = 0;
	for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]) && pk.n < PICK_MAX; i++) {
		if (strncmp(cmds[i], setting->Misc, misc_len) || cmds[i][misc_len] == '\0')
			continue;
		dashStrCopy(pk.entries[pk.n].name, cmds[i] + misc_len, MAX_NAME);
		pk.entries[pk.n].dir = 0;
		pk.n++;
	}
	if (pk.n < PICK_MAX) {
		dashStrCopy(pk.entries[pk.n].name, DashMiscCommandName(), MAX_NAME);
		pk.entries[pk.n].dir = 0;
		pk.n++;
	}
}

static int pickIsMisc(const char *dir)
{
	return pk.mode == DASH_PICK_LAUNCH && setting->Misc[0] && !strcmp(dir, setting->Misc);
}

/* parent of "mc0:/APPS/FOO/" is "mc0:/APPS/"; of "mc0:/" the device list */
static void pickParent(char *dir, int size)
{
	int len = strlen(dir);
	char *colon = strchr(dir, ':');

	if (pickIsMisc(dir)) {
		dir[0] = '\0';
		return;
	}

	if (colon == NULL || (colon[1] == '/' && colon[2] == '\0') || colon[1] == '\0') {
		dir[0] = '\0';
		return;
	}
	if (len > 0 && dir[len - 1] == '/')
		dir[--len] = '\0';
	while (len > 0 && dir[len - 1] != '/')
		dir[--len] = '\0';
	(void)size;
}

int dashPickPath(const char *title, const char *start_dir, int mode, const char *ext, char *out, int out_size)
{
	int sel = 0, top = 0, result = 0;
	const int rows = 9;
	float open = 0.0f, sel_anim = 0.0f;
	float w = 480.0f, row_h = 28.0f;
	float h = 96.0f + rows * row_h + 48.0f;
	float x = (DASH_VW - w) * 0.5f, y = (DASH_VH - h) * 0.5f;
	DashInput in;

	if (pk.active)
		return 0; /* not re-entrant */
	memset(&pk, 0, sizeof(pk));
	pk.entries = (PickEntry *)malloc(sizeof(PickEntry) * PICK_MAX);
	if (pk.entries == NULL) {
		dashToast(DASH_TOAST_ERROR, "Not enough memory for the file picker");
		return 0;
	}
	pk.active = 1;
	pk.mode = mode;
	pk.ext = ext;
	pickOpen((start_dir != NULL && strchr(start_dir, ':') != NULL && dashDevicePathReady(start_dir)) ? start_dir : "");
	dashInputFlush();

	while (1) {
		int n_rows, has_use = (mode == DASH_PICK_FOLDER && pk.dir[0] != '\0');
		int first_entry; /* index of the first listing row */

		dashInputPoll(&in);
		if (pk.starting && pk.start_done) {
			pk.starting = 0;
			if (pk.start_ok) {
				pickOpen(pk.start_dir);
				sel = 0;
			} else {
				dashToast(DASH_TOAST_WARN, "Device not found");
				pickOpen("");
			}
		}
		/* rows: [Use this folder] [Up] entries... ; device list: devices */
		if (pk.dir[0] == '\0') {
			n_rows = pk.ndevs;
			first_entry = 0;
		} else {
			first_entry = has_use + 1;
			n_rows = first_entry + (pk.loading ? 0 : pk.n);
		}
		if (sel >= n_rows)
			sel = n_rows > 0 ? n_rows - 1 : 0;

		if ((in.pressed & DASH_IN_UP) && n_rows > 0) {
			sel = sel > 0 ? sel - 1 : n_rows - 1;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if ((in.pressed & DASH_IN_DOWN) && n_rows > 0) {
			sel = sel < n_rows - 1 ? sel + 1 : 0;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_L2)
			sel = 0;
		if (in.pressed & DASH_IN_R2)
			sel = n_rows > 0 ? n_rows - 1 : 0;
		if (dashDashboardPending() || (in.pressed & DASH_IN_MENU)) {
			result = 0;
			break;
		}
		if (in.pressed & DASH_IN_BACK) {
			dashSoundPlay(DASH_SND_CANCEL);
			if (pk.dir[0] == '\0') {
				result = 0;
				break;
			}
			{
				char parent[MAX_PATH];
				dashStrCopy(parent, pk.dir, sizeof(parent));
				pickParent(parent, sizeof(parent));
				pickOpen(parent);
				sel = 0;
			}
		}
		/* R3, as in the classic browser: a file is chosen relative to
		 * wLaunchELF's own folder ("uLE:/NAME", follows wLaunchELF when it
		 * moves); elsewhere it jumps to that folder */
		if ((in.pressed & DASH_IN_R3) && !pk.starting) {
			int ent = (pk.dir[0] && !pk.loading) ? sel - first_entry : -1;
			if (mode != DASH_PICK_FOLDER && ent >= 0 && ent < pk.n && !pk.entries[ent].dir && !pickIsMisc(pk.dir)) {
				dashSoundPlay(DASH_SND_CONFIRM);
				snprintf(out, out_size, "uLE:/%s", pk.entries[ent].name);
				result = 1;
				break;
			} else {
				char dir[MAX_PATH];
				filerLaunchElfFolder(dir, sizeof(dir));
				if (dir[0] && strchr(dir, ':') != NULL) {
					pickOpen(dir);
					sel = 0;
				}
			}
		}
		if ((in.pressed & DASH_IN_SELECT) && mode == DASH_PICK_FOLDER && pk.dir[0]) {
			dashStrCopy(out, pk.dir, out_size);
			result = 1;
			break;
		}
		if ((in.pressed & DASH_IN_OK) && n_rows > 0 && !pk.starting) {
			dashSoundPlay(DASH_SND_CONFIRM);
			if (pk.dir[0] == '\0') {
				DashDevice *d = &pk.devs[sel];
				if (d->state == DASH_DEV_READY || (d->icon == DI_MC && d->state == DASH_DEV_BUSY))
					pickOpen(d->browse);
				else if (d->state == DASH_DEV_PROBE || d->state == DASH_DEV_ABSENT) {
					if (d->icon == DI_MC || d->icon == DI_DISC)
						dashToast(DASH_TOAST_INFO, d->status);
					else {
						pk.starting = 1;
						pk.start_done = 0;
						dashStrCopy(pk.start_dir, d->browse, sizeof(pk.start_dir));
						if (dashDevicesStart(d, pickStartDone, NULL) < 0)
							pk.starting = 0;
					}
				}
				sel = 0;
			} else if (has_use && sel == 0) {
				dashStrCopy(out, pk.dir, out_size);
				result = 1;
				break;
			} else if (sel == has_use) {
				char parent[MAX_PATH];
				dashStrCopy(parent, pk.dir, sizeof(parent));
				pickParent(parent, sizeof(parent));
				pickOpen(parent);
				sel = 0;
			} else if (!pk.loading) {
				PickEntry *e = &pk.entries[sel - first_entry];
				char next[MAX_PATH];
				if (snprintf(next, sizeof(next), "%s%s%s", pk.dir, e->name, e->dir ? "/" : "") >= (int)sizeof(next)) {
					dashToast(DASH_TOAST_WARN, "Path too long");
				} else if (e->dir) {
					pickOpen(next);
					sel = 0;
				} else {
					dashStrCopy(out, next, out_size);
					result = 1;
					break;
				}
			}
		}

		top = listCenteredTop(sel, n_rows, rows);
		if (sel_anim < top || sel_anim >= top + rows)
			sel_anim = (float)sel;
		float dt = dashVisualUpdate();
		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), dt);
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), dt);
		{
			float oe = dashEaseOutCubic(open);
			float lx = x + 18, ly = y + 78, lw = w - 36;
			int i;

			dashRenderScene();
			dashModalBackdrop(oe);
			dashGfxPushAlpha(oe);
			dashGfxPushOffset(0, 18.0f * (1.0f - oe));
			dashModalPanel(x, y, w, h, oe);
			dashIcon(mode == DASH_PICK_FOLDER ? DI_FOLDER : mode == DASH_PICK_LAUNCH ? DI_APP : DI_FILE, x + 24, y + 18, 22, DASH_C_ACCENT);
			dashTextS(x + 56, y + 18, title, DASH_FS_LABEL, DASH_C_TEXT);
			dashTextFit(x + 24, y + 50, w - 48, pk.dir[0] ? pk.dir : "Choose a device", DASH_FS_CAPTION, DASH_C_TEXT3);

			dashGfxScissor(lx - 4, ly - 2, lw + 8, rows * row_h + 4);
			if (n_rows > 0)
				dashDrawSelection(lx, ly + (sel_anim - top) * row_h, lw, row_h - 3, 9, 1.0f);
			for (i = top; i < n_rows && i < top + rows; i++) {
				float ry = ly + (i - top) * row_h;
				DashColor c = dashItemColor(i == sel);
				int icon;
				const char *label, *detail = NULL;

				if (pk.dir[0] == '\0') {
					DashDevice *d = &pk.devs[i];
					icon = d->icon;
					label = d->name;
					detail = d->status;
					if (d->state != DASH_DEV_READY && i != sel)
						c = DASH_C_TEXT3;
				} else if (has_use && i == 0) {
					icon = DI_CHECK;
					label = "Use this folder";
				} else if (i == has_use) {
					icon = DI_BACK;
					label = "Up one level";
				} else {
					PickEntry *e = &pk.entries[i - first_entry];
					icon = e->dir ? DI_FOLDER : pickIsMisc(pk.dir) ? DI_TOOLS : (genCmpFileExt(e->name, "PNG") || genCmpFileExt(e->name, "JPG") || genCmpFileExt(e->name, "JPEG")) ? DI_IMAGE : genCmpFileExt(e->name, "ELF") ? DI_APP : DI_FILE;
					label = e->name;
				}
				dashIcon(icon, lx + 10, ry + 5, 16, c);
				dashTextFit(lx + 36, ry + 2.5f, lw - 60 - (detail ? 130 : 0), label, DASH_FS_SMALL, c);
				if (detail)
					dashTextRight(lx + lw - 12, ry + 3.5f, detail, DASH_FS_CAPTION, DASH_C_TEXT3);
			}
			dashGfxScissorReset();
			if (pk.loading || pk.starting) {
				float cy = ly + (first_entry + 1.5f) * row_h;
				dashGfxBlend(DASH_BLEND_ADD);
				dashDrawSpinner(x + w * 0.5f - 70, cy, 8, DASH_C_ACCENT);
				dashGfxBlend(DASH_BLEND_NORMAL);
				dashText(x + w * 0.5f - 52, cy - 9, pk.starting ? "Starting device..." : "Reading folder...", DASH_FS_SMALL, DASH_C_TEXT2);
			} else if (pk.dir[0] && pk.n == 0) {
				dashTextCenter(x + w * 0.5f, ly + (first_entry + 1) * row_h, mode == DASH_PICK_FOLDER ? "No folders here" : "Nothing to choose here", DASH_FS_SMALL, DASH_C_TEXT3);
			}
			dashDrawScrollbar(x + w - 12, ly, rows * row_h - 4, top, rows, n_rows);
			{
				float hx = x + 24, hy = y + h - 36;
				hx += dashDrawButton(hx, hy, dashOkIcon(), NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, localizeDashboardText("Open"), DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				hx += dashDrawButton(hx, hy, dashBackIcon(), NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, pk.dir[0] ? localizeDashboardText("Up") : localizeDashboardText("Cancel"), DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				if (mode == DASH_PICK_FOLDER && pk.dir[0]) {
					hx += dashDrawButton(hx, hy, -1, "SELECT") + 4;
					hx = dashTextS(hx, hy + 4.5f, "Use this folder", DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				} else if (mode != DASH_PICK_FOLDER) {
					hx += dashDrawButton(hx, hy, DI_BTN_R3, NULL) + 4;
					hx = dashTextS(hx, hy + 4.5f, "uLE:/ path", DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				}
				hx += dashDrawButton(hx, hy, DI_BTN_TRIANGLE, NULL) + 4;
				dashTextS(hx, hy + 4.5f, localizeDashboardText("Cancel"), DASH_FS_SMALL, DASH_C_TEXT2);
			}
			dashGfxPopOffset();
			dashGfxPopAlpha();
			dashGfxPresent();
		}
	}
	/* a listing still in flight finds pk inactive and frees itself */
	pk.active = 0;
	pk.gen++;
	free(pk.entries);
	pk.entries = NULL;
	dashInputFlush();
	return result;
}
//--------------------------------------------------------------
//End of file: dash_widgets.c
//--------------------------------------------------------------
