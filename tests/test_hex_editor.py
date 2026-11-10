"""Compile the real Hex Editor (and the Text Editor's file IO it shares) on the
host with stub drawing and scripted input, then edit real files with it."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_editor.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
header = (root / "include/dash_internal.h").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


io_start = source.index("typedef struct\n{\n\tint op; /* 0 load, 1 save")
io_end = source.index("static int docNew(void);")
menu_start = source.index("/* \"Save changes?\" in the Text Editor")
menu_end = source.index("static int confirmDiscard(EdDoc *d)")
hex_start = source.index("#define HX_COLS 8")
hex_end = source.index("//--------------------------------------------------------------\n//End of file")

code = r'''
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MAX_PATH 1024
#define ED_MAX_SIZE (512 * 1024)
#define ED_X 40.0f
#define ED_Y 60.0f
#define ED_W 560.0f
#define ED_ROW 17.0f
#define ED_CW 8.0f
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_CREAT O_CREAT
#define FIO_O_TRUNC O_TRUNC
#define PS2KBD_ESCAPE_KEY 0x1B
typedef int DashColor;
typedef struct { int button; const char *key; const char *label; } DashHint;
typedef struct { int icon; const char *label; const char *detail; int disabled; } DashMenuItem;
typedef struct { unsigned pressed; } DashInput;
enum { DASH_IN_UP = 1, DASH_IN_DOWN = 2, DASH_IN_LEFT = 4, DASH_IN_RIGHT = 8, DASH_IN_OK = 16,
       DASH_IN_BACK = 32, DASH_IN_MENU = 64, DASH_IN_L1 = 256, DASH_IN_R1 = 512, DASH_IN_L2 = 1024,
       DASH_IN_R2 = 2048 };
enum { DI_EDIT, DI_SAVE, DI_CLOSE, DI_BACK, DI_BTN_DOWN, DI_BTN_TRIANGLE, DI_WARNING };
enum { DASH_TOAST_INFO, DASH_TOAST_OK, DASH_TOAST_WARN };
enum { DASH_JOB_VISIBLE = 1, DASH_SND_CANCEL = 0 };
#define DASH_C_ACCENT 1
#define DASH_C_SELECT 2
#define DASH_C_TEXT 3
#define DASH_C_TEXT3 4
#define DASH_FS_LABEL 1.0f
#define DASH_FS_CAPTION 0.8f
static struct { int usbkbd_used; } settings, *setting = &settings;

/* scripted input: one DashInput per frame, then menu answers */
static const unsigned *script; static int step;
static const int *menu_answers; static int menu_step;
static const char *typed_offset;
static char last_message[256], last_toast[128];
static int frames, saves, menus, save_cancel;

static int genFixPath(const char *in, char *out) { strcpy(out, in); return 0; }
static void dashStrCopy(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s); }
static int genOpen(const char *p, int f) { return open(p, f, 0644); }
static int genWrite(int fd, const void *b, int n) { return (int)write(fd, b, n); }
static int genRead(int fd, void *b, int n) { return (int)read(fd, b, n); }
static int genLseek(int fd, int o, int w) { return (int)lseek(fd, o, w); }
static int genClose(int fd) { return close(fd); }
static void unmountParty(int n) { (void)n; }
static void (*job_fn)(void *), (*done_fn)(void *); static void *job_arg;
static int dashWorkerSubmit(const char *t, int f, void (*fn)(void *), void (*done)(void *), void *a)
{ (void)t; (void)f; job_fn = fn; done_fn = done; job_arg = a; return 1; }
static int dashWorkerAwait(int id, const char *t, int c)
{ (void)id; if (!strcmp(t, "Saving file")) { saves++; save_cancel |= c; } job_fn(job_arg); done_fn(job_arg); return 1; }
static void dashToast(int k, const char *s) { (void)k; snprintf(last_toast, sizeof(last_toast), "%s", s); }
static void dashModalMessage(int i, const char *t, const char *b, const char *d)
{ (void)i; (void)d; snprintf(last_message, sizeof(last_message), "%s|%s", t, b); }
static int dashModalMenu(const char *t, const char *s, const DashMenuItem *it, int n, int init)
{ (void)t; (void)s; (void)it; (void)n; (void)init; menus++; return menu_answers[menu_step++]; }
static int dashModalKeyboard(const char *t, char *buf, int max)
{ (void)t; snprintf(buf, max + 1, "%s", typed_offset); return 1; }
static int dashDashboardPending(void) { return 0; }
static int dashDashboardConfirmLeave(int (*fn)(void)) { return fn(); }
static void dashInputPollEx(DashInput *in, int f) { (void)f; in->pressed = script[step] ? script[step++] : 0; assert(++frames < 1000); }
static void dashInputFlush(void) {}
static void dashFontInit(void) {}
static void (*scene)(void);
static void dashPushSceneOverride(void (*fn)(void)) { scene = fn; }
static void dashPopSceneOverride(void) { scene = NULL; }
static void dashSoundPlay(int s) { (void)s; }
static void dashVisualUpdate(void) {}
static void dashRenderScene(void) { scene(); }
static void dashGfxPresent(void) {}
static int dashWorkerIoBusy(void) { return 0; }
static int dashIopParked(void) { return 0; }
static int ensureUsbKeyboardReady(void) { return 1; }
/* USB keyboard: the queued keys arrive on the frame after input step kbd_at */
static const char *kbd_keys; static int kbd_at = -1;
static int PS2KbdRead(char *k) { if (step != kbd_at || !kbd_keys || !*kbd_keys) return 0; *k = *kbd_keys++; return 1; }
static int kb_shown;
static int exploit_answer = 1, exploit_asks;
static int filerConfirmExploitModify(const char *p, const void *f) { (void)p; (void)f; exploit_asks++; return exploit_answer; }
static int visibleRows(void) { return 18; }
static void dashGfxBeginFrame(void) {}
static void dashBgDraw(void) {}
static void dashIcon(int i, float x, float y, float s, int c) { (void)i; (void)x; (void)y; (void)s; (void)c; }
static void dashTextFit(float x, float y, float w, const char *s, float f, int c) { (void)x; (void)y; (void)w; (void)s; (void)f; (void)c; }
static void dashTextRight(float x, float y, const char *s, float f, int c) { (void)x; (void)y; (void)s; (void)f; (void)c; }
static void dashTextScroll(float x, float y, float w, const char *s, float f, int c) { (void)x; (void)y; (void)w; (void)s; (void)f; (void)c; }
static void dashDrawPanelBox(float x, float y, float w, float h, float r) { (void)x; (void)y; (void)w; (void)h; (void)r; }
static int dashColorA(int c, float a) { (void)a; return c; }
static void dashRect(float x, float y, float w, float h, int c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void dashSpriteBatchEnd(void) {}
static int glyphs;
static void dashFontMonoByte(unsigned char ch, float x, float y, int c)
{ (void)x; (void)y; (void)c; assert(ch > 0x20 && ch < 0x7F); assert(x + ED_CW <= ED_X + ED_W); glyphs++; }
static void dashDrawScrollbar(float x, float y, float h, int top, int rows, int n) { (void)x; (void)y; (void)h; (void)top; (void)rows; (void)n; }
static int dashOkIcon(void) { return 0; }
static int dashBackIcon(void) { return 0; }
static void dashDrawHints(const DashHint *h, int n) { (void)h; (void)n; }
static void dashDrawToasts(void) {}
static void dashDrawActivity(void) {}
''' + source[io_start:io_end] + source[menu_start:menu_end] + source[hex_start:hex_end] + r'''

static void put(const char *p, const unsigned char *b, int n) { FILE *f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f); }
static int get(const char *p, unsigned char *b, int n) { FILE *f = fopen(p, "rb"); int r = (int)fread(b, 1, n, f); fclose(f); return r; }
static void run(const char *path, const unsigned *keys, const int *answers)
{ script = keys; step = 0; menu_answers = answers; menu_step = menus = saves = 0; last_message[0] = last_toast[0] = 0; dashHexEditor(path); }

int main(int argc, char **argv)
{
	char path[MAX_PATH], row[64];
	unsigned char data[300], back[400];
	int i;

	assert(argc == 2);
	/* pure helpers */
	assert(hexParse("1A2b") == 0x1A2B && hexParse("0x10") == 16 && hexParse("0X0") == 0);
	assert(hexParse("") == -1 && hexParse("0x") == -1 && hexParse("12g") == -1 && hexParse("12345678") == -1);
	assert(hexParse("7FFFFFF") == 0x7FFFFFF);
	hx_buf = (unsigned char *)"ABCDEFGHI\x01\xff"; hx_len = 11;
	hexRow(row, 0);
	assert(!strcmp(row, "000000  41 42 43 44 45 46 47 48  ABCDEFGH"));
	hexRow(row, 8);
	assert(!strcmp(row, "000008  49 01 FF                 I.."));
	assert(row[8 + 3 * 2] == 'F' && row[33 + 2] == '.');

	for (i = 0; i < (int)sizeof(data); i++)
		data[i] = (unsigned char)i;
	snprintf(path, sizeof(path), "%s/save.bin", argv[1]);

	/* OK, Up twice, R1 (+16), OK, then Triangle > Save > Exit */
	put(path, data, sizeof(data));
	{
		static const unsigned k[] = {DASH_IN_RIGHT, DASH_IN_OK, DASH_IN_UP, DASH_IN_UP, DASH_IN_R1, DASH_IN_OK, DASH_IN_MENU, DASH_IN_MENU, 0};
		static const int a[] = {0, 2};
		run(path, k, a);
	}
	assert(saves == 1 && !save_cancel && !strcmp(last_toast, "Saved and checked")); /* Back cannot leave a write running */
	assert(get(path, back, sizeof(back)) == (int)sizeof(data));
	assert(back[1] == (unsigned char)(1 + 2 + 16) && !memcmp(back + 2, data + 2, sizeof(data) - 2) && back[0] == 0);
	assert(hx_buf == NULL && scene == NULL && glyphs > 0);

	/* Down wraps a byte below zero; Back then "Don't save" keeps the file */
	put(path, data, sizeof(data));
	{
		static const unsigned k[] = {DASH_IN_OK, DASH_IN_DOWN, DASH_IN_OK, DASH_IN_BACK, 0};
		static const int a[] = {1};
		run(path, k, a);
	}
	assert(saves == 0 && menus == 1);
	assert(get(path, back, sizeof(back)) == (int)sizeof(data) && !memcmp(back, data, sizeof(data)));

	/* Back with no change leaves without asking */
	{
		static const unsigned k[] = {DASH_IN_DOWN, DASH_IN_BACK, 0};
		static const int a[] = {-1};
		run(path, k, a);
	}
	assert(menus == 0 && saves == 0);

	/* Go to offset 0x12C (300 = past the end) is refused; 0x2B is taken; L1/R1 clamp */
	{
		static const unsigned k[] = {DASH_IN_MENU, DASH_IN_R1, DASH_IN_R2, DASH_IN_RIGHT, DASH_IN_BACK, 0};
		static const int a[] = {1};
		typed_offset = "12C";
		run(path, k, a);
		assert(!strcmp(last_toast, "Not an offset in this file"));
	}
	{
		static const unsigned k[] = {DASH_IN_MENU, DASH_IN_OK, DASH_IN_UP, DASH_IN_OK, DASH_IN_MENU, DASH_IN_MENU, 0};
		static const int a[] = {1, 0, 2};
		typed_offset = "0x2b";
		run(path, k, a);
	}
	assert(get(path, back, sizeof(back)) == (int)sizeof(data) && back[0x2B] == 0x2C && back[0x2A] == 0x2A);

	/* empty, missing and too-large files are not opened */
	put(path, data, 0);
	run(path, NULL, NULL);
	assert(strstr(last_message, "The file is empty.") && frames > 0);
	snprintf(path, sizeof(path), "%s/missing.bin", argv[1]);
	run(path, NULL, NULL);
	assert(strstr(last_message, "could not be read"));
	snprintf(path, sizeof(path), "%s/big.bin", argv[1]);
	{
		unsigned char *big = (unsigned char *)calloc(1, ED_MAX_SIZE + 1);
		put(path, big, ED_MAX_SIZE + 1);
		free(big);
	}
	run(path, NULL, NULL);
	assert(strstr(last_message, "larger than 512 KB"));

	/* USB keyboard: typed digits set a byte; a pad change in the frame the keyboard moved the
	 * cursor lands on the new byte, not the old one */
	snprintf(path, sizeof(path), "%s/kbd.bin", argv[1]);
	put(path, data, 16);
	settings.usbkbd_used = 1;
	kbd_keys = "4a"; kbd_at = 1;
	{
		static const unsigned k[] = {DASH_IN_DOWN, DASH_IN_MENU, DASH_IN_MENU, 0};
		static const int a[] = {0, 2};
		run(path, k, a);
	}
	assert(get(path, back, sizeof(back)) == 16 && back[0] == 0x4A && !memcmp(back + 1, data + 1, 15));
	kbd_keys = "\x1b\x29"; kbd_at = 2;
	{
		static const unsigned k[] = {DASH_IN_OK, DASH_IN_UP, DASH_IN_OK, DASH_IN_MENU, DASH_IN_MENU, 0};
		static const int a[] = {0, 2};
		run(path, k, a);
	}
	assert(get(path, back, sizeof(back)) == 16 && back[0] == 0x4A && back[1] == 2 && back[2] == 2);
	/* at the last byte, typing stays there: "1234" leaves 0x34 */
	kbd_keys = "1234"; kbd_at = 2;
	{
		static const unsigned k[] = {DASH_IN_R1, DASH_IN_MENU, DASH_IN_MENU, 0};
		static const int a[] = {0, 2};
		run(path, k, a);
	}
	assert(get(path, back, sizeof(back)) == 16 && back[15] == 0x34 && back[14] == 14 && back[1] == 2);
	settings.usbkbd_used = 0; kbd_keys = NULL; kbd_at = -1;

	/* a system folder save asks first; No writes nothing */
	exploit_answer = 0; exploit_asks = 0;
	{
		static const unsigned k[] = {DASH_IN_OK, DASH_IN_UP, DASH_IN_OK, DASH_IN_MENU, DASH_IN_BACK, 0};
		static const int a[] = {0, 1};
		run(path, k, a);
	}
	assert(exploit_asks == 1 && saves == 0 && get(path, back, sizeof(back)) == 16 && back[0] == 0x4A);
	exploit_answer = 1;

	/* saving writes in place: it never creates a file that went away, and never cuts one */
	hx_path = path; hx_buf = data; hx_len = 4;
	{
		unsigned char longer[32];
		memset(longer, 0xEE, sizeof(longer));
		put(path, longer, sizeof(longer));
		assert(hexSave() == 0 && strstr(last_message, "File not saved"));
		assert(get(path, back, sizeof(back)) == 32 && !memcmp(back, data, 4) && back[4] == 0xEE);
	}
	snprintf(path, sizeof(path), "%s/gone.bin", argv[1]);
	hx_path = path;
	assert(hexSave() == 0 && access(path, F_OK) != 0);

	/* the Text Editor's keyboard left up does not shrink the Hex Editor */
	kb_shown = 1;
	snprintf(path, sizeof(path), "%s/save.bin", argv[1]);
	put(path, data, 8);
	run(path, (const unsigned[]){DASH_IN_BACK, 0}, (const int[]){-1});
	assert(kb_shown == 0);

	/* discs are read-only */
	hx_path = "cdfs:/X.BIN";
	hx_buf = data; hx_len = 4;
	assert(hexSave() == 0 && strstr(last_message, "Read-only device"));
	puts("hex editor ok");
	return 0;
}
'''

compiler = shutil.which("cc") or shutil.which("gcc")
assert compiler, "a C compiler is required"
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / "hex.c"
    exe = Path(tmp) / "hex"
    src.write_text(code, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-o", str(exe), str(src)], check=True)
    out = subprocess.run([str(exe), tmp], check=True, capture_output=True, text=True).stdout
    assert "hex editor ok" in out, out

# the File Browser offers it beside the Text Editor, for any file
assert ('if (f != NULL && !isDir(f) && !isMisc(fb.dir) && (!isPcCardCfPath(fb.dir) || isPcCardCfOnline()))\n'
        '\t\t\tADD(DI_EDIT, "Open in Hex Editor", "change bytes, up to 512 KB", 0, F_HEX);') in browser
assert "case F_HEX:" in browser and "dashHexEditor(path);" in browser
assert "void dashHexEditor(const char *path);" in header
print("hex editor tests passed")
