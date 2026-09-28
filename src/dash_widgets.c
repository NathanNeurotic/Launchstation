//--------------------------------------------------------------
//File name:   dash_widgets.c
//Native dashboard dialogs that replace the classic wLaunchELF prompts:
//* dashModalKeyboard(): text entry with the user's virtual keyboard
//  layout (Settings > Controls) and USB keyboard typing.
//* dashPickPath(): a file or folder picker. Folders are read by the
//  background worker, so the picker never freezes on a slow device, and
//  the user can always back out.
//--------------------------------------------------------------
#include "dash_internal.h"
#include "dash_sound.h"
#include "init.h"


//--------------------------------------------------------------
// Keyboard
//--------------------------------------------------------------
enum {
	KB_ACT_CAPS = 0,
	KB_ACT_SPACE,
	KB_ACT_DELETE,
	KB_ACT_DONE,
	KB_ACT_CANCEL,
	KB_ACT_COUNT
};

static const char *const kb_act_names[KB_ACT_COUNT] = {"Caps", "Space", "Delete", "Done", "Cancel"};

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

/* USB keyboard: typed characters and editing keys (same codes as the
 * classic keyboard). Returns 1 = done, -1 = cancel, 0 = keep going. */
static int kbUsb(char *buf, int max_len, int *cur)
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
		else if ((unsigned char)key >= 0x20)
			kbInsert(buf, max_len, cur, key);
	}
	return 0;
}

int dashModalKeyboard(const char *title, char *buf, int max_len)
{
	const int layout = normalizeVirtualKeyboardLayout(setting->virtual_keyboard_layout);
	const int first_col = getVirtualKeyboardLayoutFirstColumn(layout);
	const int ncols = getVirtualKeyboardLayoutColumnCount(layout);
	const float key_w = 32.0f, key_h = 30.0f, gap = 4.0f;
	char orig[256];
	int sel = getVirtualKeyboardLayoutNextKey(layout, VKEY_LAYOUT_COLS + first_col, 0, 0);
	int act = -1; /* >= 0: focus is on the action row */
	int caps = 0, cur, result = 0;
	float open = 0.0f;
	float grid_w = ncols * key_w + (ncols - 1) * gap;
	float w = grid_w + 60.0f, h;
	float x, y;
	DashInput in;

	if (max_len > (int)sizeof(orig) - 1)
		max_len = sizeof(orig) - 1;
	if (w < 460.0f)
		w = 460.0f;
	h = 70.0f + 44.0f + VKEY_LAYOUT_ROWS * (key_h + gap) + 14.0f + 34.0f + 50.0f;
	x = (DASH_VW - w) * 0.5f;
	y = (DASH_VH - h) * 0.5f;
	dashStrCopy(orig, buf, sizeof(orig));
	cur = strlen(buf);
	dashInputFlush();

	while (1) {
		int r;

		dashInputPollEx(&in, 0);
		r = kbUsb(buf, max_len, &cur);
		if (r != 0) {
			result = (r > 0);
			break;
		}
		if (in.pressed & DASH_IN_UP) {
			if (act >= 0) {
				act = -1;
				sel = getVirtualKeyboardLayoutNextKey(layout, (VKEY_LAYOUT_ROWS - 1) * VKEY_LAYOUT_COLS + first_col + (ncols * 3) / 4, 0, 0);
			} else if (sel >= VKEY_LAYOUT_COLS)
				sel = getVirtualKeyboardLayoutNextKey(layout, sel, 0, -1);
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_DOWN) {
			if (act < 0) {
				if (sel / VKEY_LAYOUT_COLS >= VKEY_LAYOUT_ROWS - 1)
					act = KB_ACT_DONE;
				else
					sel = getVirtualKeyboardLayoutNextKey(layout, sel, 0, 1);
			}
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_LEFT) {
			if (act >= 0)
				act = (act + KB_ACT_COUNT - 1) % KB_ACT_COUNT;
			else
				sel = getVirtualKeyboardLayoutNextKey(layout, sel, -1, 0);
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_RIGHT) {
			if (act >= 0)
				act = (act + 1) % KB_ACT_COUNT;
			else
				sel = getVirtualKeyboardLayoutNextKey(layout, sel, 1, 0);
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_L1) {
			if (cur > 0)
				cur--;
		}
		if (in.pressed & DASH_IN_R1) {
			if (cur < (int)strlen(buf))
				cur++;
		}
		if (in.pressed & DASH_IN_FAV) /* Square: caps, as on the classic keyboard */
			caps = !caps;
		if (in.pressed & DASH_IN_BACK) { /* cancel button deletes, as before */
			kbDelete(buf, &cur);
			dashSoundPlay(DASH_SND_CANCEL);
		}
		if (in.pressed & DASH_IN_START) {
			result = 1;
			break;
		}
		if (in.pressed & DASH_IN_MENU) {
			result = 0;
			break;
		}
		if (in.pressed & DASH_IN_OK) {
			dashSoundPlay(DASH_SND_CONFIRM);
			if (act < 0) {
				if (isVirtualKeyboardLayoutKey(layout, sel))
					kbInsert(buf, max_len, &cur, getVirtualKeyboardLayoutChar(layout, sel, caps));
			} else if (act == KB_ACT_CAPS)
				caps = !caps;
			else if (act == KB_ACT_SPACE)
				kbInsert(buf, max_len, &cur, ' ');
			else if (act == KB_ACT_DELETE)
				kbDelete(buf, &cur);
			else {
				result = (act == KB_ACT_DONE);
				break;
			}
		}

		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), 1.0f / 60.0f);
		{
			float oe = dashEaseOutCubic(open);
			float fx = x + 30, fy = y + 56, fw = w - 60;
			float gx = x + (w - grid_w) * 0.5f, gy = fy + 48;
			int row, col;

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

			/* text field with a blinking caret */
			dashPanel(fx, fy, fw, 36, 10, dashColorA(DASH_C_PANEL, 1.0f));
			dashPanelBorder(fx, fy, fw, 36, 10, dashColorA(DASH_C_ACCENT, 0.5f));
			{
				float tx = fx + 12, caret;
				float before = dashTextWidthN(buf, cur, DASH_FS_BODY);
				float shift = 0.0f;
				if (before > fw - 30)
					shift = before - (fw - 30);
				dashGfxScissor(fx + 6, fy + 2, fw - 12, 32);
				dashText(tx - shift, fy + 8, buf, DASH_FS_BODY, DASH_C_TEXT);
				caret = tx - shift + before;
				if ((Timer() / 400) & 1)
					dashRect(caret, fy + 8, 2, 20, DASH_C_ACCENT);
				dashGfxScissorReset();
			}

			/* key grid */
			for (row = 0; row < VKEY_LAYOUT_ROWS; row++) {
				for (col = 0; col < ncols; col++) {
					int idx = row * VKEY_LAYOUT_COLS + first_col + col;
					float kx = gx + col * (key_w + gap), ky = gy + row * (key_h + gap);
					char label[2];
					int focused = (act < 0 && idx == sel);

					if (!isVirtualKeyboardLayoutKey(layout, idx))
						continue;
					label[0] = getVirtualKeyboardLayoutDisplayChar(layout, idx, caps);
					label[1] = '\0';
					if (focused)
						dashDrawSelection(kx - 1, ky - 1, key_w + 2, key_h + 2, 8, 1.0f);
					else
						dashPanel(kx, ky, key_w, key_h, 8, DC(255, 255, 255, 16));
					if (label[0] == ' ' || label[0] == '_')
						dashTextCenter(kx + key_w * 0.5f, ky + 7, label[0] == '_' ? "_" : "sp", DASH_FS_SMALL, dashItemColor(focused));
					else
						dashTextCenter(kx + key_w * 0.5f, ky + 6, label, DASH_FS_BODY, dashItemColor(focused));
				}
			}

			/* action row */
			{
				float ay = gy + VKEY_LAYOUT_ROWS * (key_h + gap) + 10;
				float aw = (fw - (KB_ACT_COUNT - 1) * 8.0f) / KB_ACT_COUNT;
				int a;
				for (a = 0; a < KB_ACT_COUNT; a++) {
					float ax = fx + a * (aw + 8.0f);
					int focused = (act == a);
					const char *name = (a == KB_ACT_CAPS) ? (caps ? "CAPS" : "Caps") : kb_act_names[a];
					if (focused)
						dashDrawSelection(ax, ay, aw, 30, 10, 1.0f);
					else
						dashPanel(ax, ay, aw, 30, 10, DC(255, 255, 255, (a == KB_ACT_CAPS && caps) ? 40 : 18));
					dashTextCenter(ax + aw * 0.5f, ay + 7, name, DASH_FS_SMALL, dashItemColor(focused));
				}
			}

			/* hints */
			{
				float hx = x + 24, hy = y + h - 36;
				hx += dashDrawButton(hx, hy, dashOkIcon(), NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, "Type", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, dashBackIcon(), NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, "Delete", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, DI_BTN_SQUARE, NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, "Caps", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, -1, "L1") + 2;
				hx += dashDrawButton(hx, hy, -1, "R1") + 4;
				hx = dashTextS(hx, hy + 4.5f, "Move", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, -1, "START") + 4;
				hx = dashTextS(hx, hy + 4.5f, "Done", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, DI_BTN_TRIANGLE, NULL) + 4;
				dashTextS(hx, hy + 4.5f, "Cancel", DASH_FS_SMALL, DASH_C_TEXT2);
			}
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
		if (in.pressed & (DASH_IN_OK | DASH_IN_START)) {
			result = 1;
			break;
		}
		if (in.pressed & (DASH_IN_BACK | DASH_IN_MENU)) {
			memcpy(oct, orig, sizeof(oct));
			break;
		}
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), 1.0f / 60.0f);
		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), 1.0f / 60.0f);
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
				hx += dashDrawButton(hx, hy, DI_BTN_UP, NULL) + 2;
				hx += dashDrawButton(hx, hy, DI_BTN_DOWN, NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, "Change", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				hx += dashDrawButton(hx, hy, -1, "L1") + 2;
				hx += dashDrawButton(hx, hy, -1, "R1") + 4;
				hx = dashTextS(hx, hy + 4.5f, "10", DASH_FS_SMALL, DASH_C_TEXT2) + 12;
				dashModalHintRow(hx, hy, dashOkIcon(), "OK", dashBackIcon(), "Cancel");
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
	/* reading a disc needs the CDVD driver: IOP modules are only ever
	 * loaded on the UI thread, never by the background worker */
	if (!strncmp(dir, "cdfs", 4) && !isPathDeviceStackLoaded(dir)) {
		if (!dashWorkerAwaitAll("Finishing up")) {
			pk.loading = 0;
			return;
		}
		loadCdModules();
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

		if ((in.pressed & DASH_IN_UP) && sel > 0) {
			sel--;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if ((in.pressed & DASH_IN_DOWN) && sel < n_rows - 1) {
			sel++;
			dashSoundPlay(DASH_SND_CURSOR);
		}
		if (in.pressed & DASH_IN_L2)
			sel = 0;
		if (in.pressed & DASH_IN_R2)
			sel = n_rows > 0 ? n_rows - 1 : 0;
		if (in.pressed & DASH_IN_MENU) {
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
		if ((in.pressed & DASH_IN_START) && mode == DASH_PICK_FOLDER && pk.dir[0]) {
			dashStrCopy(out, pk.dir, out_size);
			result = 1;
			break;
		}
		if ((in.pressed & DASH_IN_OK) && n_rows > 0 && !pk.starting) {
			dashSoundPlay(DASH_SND_CONFIRM);
			if (pk.dir[0] == '\0') {
				DashDevice *d = &pk.devs[sel];
				if (d->state == DASH_DEV_READY)
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

		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;
		sel_anim = dashApproach(sel_anim, (float)sel, 22.0f * dashAnimSpeed(), 1.0f / 60.0f);
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), 1.0f / 60.0f);
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
					icon = e->dir ? DI_FOLDER : pickIsMisc(pk.dir) ? DI_TOOLS : genCmpFileExt(e->name, "PNG") ? DI_IMAGE : genCmpFileExt(e->name, "ELF") ? DI_APP : DI_FILE;
					label = e->name;
				}
				dashIcon(icon, lx + 10, ry + 5, 16, c);
				dashTextFit(lx + 36, ry + 4, lw - 60 - (detail ? 130 : 0), label, DASH_FS_SMALL, c);
				if (detail)
					dashTextRight(lx + lw - 12, ry + 6, detail, DASH_FS_CAPTION, DASH_C_TEXT3);
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
				hx = dashTextS(hx, hy + 4.5f, "Open", DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				hx += dashDrawButton(hx, hy, dashBackIcon(), NULL) + 4;
				hx = dashTextS(hx, hy + 4.5f, pk.dir[0] ? "Up" : "Cancel", DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				if (mode == DASH_PICK_FOLDER && pk.dir[0]) {
					hx += dashDrawButton(hx, hy, -1, "START") + 4;
					hx = dashTextS(hx, hy + 4.5f, "Use this folder", DASH_FS_SMALL, DASH_C_TEXT2) + 14;
				}
				hx += dashDrawButton(hx, hy, DI_BTN_TRIANGLE, NULL) + 4;
				dashTextS(hx, hy + 4.5f, "Cancel", DASH_FS_SMALL, DASH_C_TEXT2);
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
