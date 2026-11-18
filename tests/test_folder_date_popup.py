"""Check manual folder-date controls fit inside their popup and align."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text()
icons = (root / "include/dash_icons.h").read_text()


def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


editor = function("static int tsEdit(")
assert "dashDrawHints(" not in editor, "Popup hints must not use the screen footer"
placement = "tsDrawHints(x + 24, y + h - 36, w - 48);"
assert editor.index("dashGfxPushAlpha(oe);") < editor.index(placement) < editor.index("dashGfxPopAlpha();")
assert "filerDateEditorSelect(e, -1);" in editor and "filerDateEditorSelect(e, 1);" in editor
assert "filerDateEditorMoveNextTo(e, 1);" in editor and "filerDateEditorMoveNextTo(e, 0);" in editor

# The date-field selection must finish before the hint row begins.
rows = int(re.search(r"#define TS_ROWS (\d+)", source).group(1))
row_h = float(re.search(r"#define TS_ROW_H ([\d.]+)f", source).group(1))
assert 70 + rows * row_h + 12 - 3 + 28 + 6 <= 372 - 36

code = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int DashColor;
#define DASH_FS_SMALL 0.66f
#define DASH_C_TEXT2 2
typedef struct { int button; const char *key, *label; } DashHint;
static int mapping, buttons, labels, in_group;
static float font_width, row_y, last_button_right;
static float group_x[4], label_right[4];
'''
code += icons + "\n"
code += r'''
static int dashOkIcon(void) { return mapping ? DI_BTN_CROSS : DI_BTN_CIRCLE; }
static int dashBackIcon(void) { return mapping ? DI_BTN_CIRCLE : DI_BTN_CROSS; }
static float dashButtonWidth(int icon, const char *key) {
    (void)icon; assert(!key); return 22;
}
static float dashTextWidth(const char *text, float scale) { return strlen(text) * font_width * scale; }
static float dashDrawButton(float x, float y, int icon, const char *key) {
    const int expected[] = {DI_BTN_R1, DI_BTN_R2, dashOkIcon(), dashBackIcon()};
    assert(buttons < 4 && icon == expected[buttons] && !key);
    assert(x >= 54 && x + 22 <= 586);
    assert(y == 374 && y + 22 <= 400);
    if (!in_group) group_x[labels] = x;
    last_button_right = x + 22; row_y = y;
    in_group++; buttons++;
    return 22;
}
static float dashTextS(float x, float y, const char *text, float scale, DashColor color) {
    const char *expected[] = {"Up", "Down", "Apply", "Cancel"};
    assert(labels < 4 && !strcmp(text, expected[labels]) && color == DASH_C_TEXT2);
    assert(in_group == 1);
    assert(fabsf(x - last_button_right - 4) < 0.001f);
    assert(fabsf(y + 16 * scale - (row_y + 11)) < 0.001f);
    float right = x + dashTextWidth(text, scale);
    assert(x >= 54 && right <= 586.001f);
    if (labels) assert(group_x[labels] - label_right[labels - 1] >= 12);
    label_right[labels++] = right; in_group = 0;
    return right;
}
'''
code += function("static void tsDrawHints(")
header = (root / "include/dash_internal.h").read_text()
start = header.rfind("enum {", 0, header.index("DASH_IN_UP"))
code += header[start:header.index("};", start) + 2] + "\n"
code += r'''
typedef struct { int position; } FilerDateEditor;
typedef struct { int pressed; } DashInput;
#define DASH_SND_CURSOR 1
static int selections, moves, adjustments, direction, newer;
static void filerDateEditorSelect(FilerDateEditor *e, int dir) { (void)e; selections++; direction=dir; }
static void filerDateEditorMoveNextTo(FilerDateEditor *e, int up) { (void)e; moves++; newer=up; }
static void filerDateEditorAdjust(FilerDateEditor *e, int delta) { (void)e; adjustments+=delta; }
static void dashSoundPlay(int sound) { assert(sound==DASH_SND_CURSOR); }
static void route(FilerDateEditor *e, int pressed) {
    int nfields=6;
    DashInput in={pressed};
'''
code += editor[editor.index("\t\tif (in.pressed & DASH_IN_LEFT)"):editor.index("\n\t\tidx = filerDateEditorSync(e);")]
code += "}\n"
code += r'''
int main(void) {
    /* Include widths above the bundled fonts to stress popup containment. */
    for (mapping = 0; mapping < 2; mapping++) for (int percent = 90; percent <= 115; percent += 5) {
        for (int width = 8; width <= 16; width += 4) {
            font_width = width * percent / 100.0f;
            buttons = labels = in_group = 0;
            tsDrawHints(54, 374, 532);
            assert(buttons == 4 && labels == 4 && !in_group);
            assert(fabsf(group_x[0]-54)<0.001f && fabsf(label_right[3]-586)<0.001f);
            float gap=group_x[1]-label_right[0];
            for (int col = 2; col < 4; col++)
                assert(fabsf(group_x[col]-label_right[col-1]-gap)<0.001f);
        }
    }
    FilerDateEditor editor={0};
    route(&editor,DASH_IN_R1); assert(moves==1 && newer==1 && !selections);
    route(&editor,DASH_IN_R2); assert(moves==2 && newer==0 && !selections);
    route(&editor,DASH_IN_L1); assert(selections==1 && direction==-1 && moves==2);
    route(&editor,DASH_IN_L2); assert(selections==2 && direction==1 && moves==2);
    route(&editor,DASH_IN_LEFT); assert(editor.position==5);
    route(&editor,DASH_IN_RIGHT); assert(editor.position==0);
    route(&editor,DASH_IN_UP); assert(adjustments==1);
    route(&editor,DASH_IN_DOWN); assert(adjustments==0);
    puts("PASS: one aligned popup row with R1 Up, R2 Down, Apply and Cancel at varied font widths/mappings; ordering and folder/date selection match their controls");
}
'''
compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="folder-date-popup-") as folder:
    c = Path(folder) / "check.c"
    executable = Path(folder) / "check"
    c.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(executable), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)

# Render the production folder-row loop with spies for icon/text/highlight
# centers, including selected rows at every visible position after scrolling.
row_loop = editor[editor.index("\t\t\tfor (i = top; i < e->count && i < top + TS_ROWS; i++)"):
                  editor.index("\t\t\tdashDrawScrollbar(")]
row_code = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int DashColor;
#define DASH_C_TEXT 0
#define DASH_C_ACCENT 1
#define DASH_C_TEXT2 2
#define DASH_FS_SMALL 0.66f
typedef struct { int value; } sceMcStDateTime;
typedef struct { char name[32]; struct { sceMcStDateTime _Modify; } stats; } FILEINFO;
typedef struct { FILEINFO *folders; sceMcStDateTime *original; int count; } FilerDateEditor;
static float origin, selected_mid;
static int icons_drawn, names_drawn, dates_drawn, selections_drawn;
'''
for macro in ("TS_ROWS", "TS_ROW_H"):
    row_code += re.search(r"^#define " + macro + r" .+$", source, re.M).group() + "\n"
row_code += icons + r'''
static float rowMid(void) { return origin + (dates_drawn + 0.5f) * TS_ROW_H; }
static void dashDrawSelection(float x, float y, float w, float h, float r, float strength) {
    (void)r; assert(strength==1 && x==46 && w==548);
    assert(fabsf(y + h * 0.5f - selected_mid)<0.001f);
    assert(h==TS_ROW_H-2); selections_drawn++;
}
static void dashIcon(int icon, float x, float y, float size, DashColor color) {
    (void)color; assert(icon==DI_FOLDER && x==54);
    assert(fabsf(y + size * 0.5f - rowMid())<0.001f);
    icons_drawn++;
}
static void dashTextFit(float x, float y, float w, const char *text, float scale, DashColor color) {
    (void)color; assert(x==76 && w==290 && text[0]);
    assert(fabsf(y + 16 * scale - rowMid())<0.001f);
    names_drawn++;
}
static void filerDateEditorFormat(const sceMcStDateTime *date, char *text, int size) {
    snprintf(text,size,"Date %d",date->value);
}
static void dashTextRight(float x, float y, const char *text, float scale, DashColor color) {
    (void)color; assert(x==582 && text[0]);
    assert(fabsf(y + 16 * scale - rowMid())<0.001f);
    dates_drawn++;
}
static void drawRows(FilerDateEditor *e, int idx, int top, float list_y) {
    const float x=30, w=580;
    int i; char date[40];
'''
row_code += row_loop + "}\n"
row_code += r'''
int main(void) {
    FILEINFO folders[20]={0}; sceMcStDateTime original[20]={0};
    FilerDateEditor editor={folders,original,20};
    for(int i=0;i<20;i++) {
        snprintf(folders[i].name,sizeof(folders[i].name),"Folder %d",i);
        folders[i].stats._Modify.value=i%2;
    }
    for(int top=0;top<=20-TS_ROWS;top++) for(int selected=0;selected<TS_ROWS;selected++) {
        origin=108+(top%4)*0.25f;
        selected_mid=origin+(selected+0.5f)*TS_ROW_H;
        icons_drawn=names_drawn=dates_drawn=selections_drawn=0;
        drawRows(&editor,top+selected,top,origin);
        assert(icons_drawn==TS_ROWS && names_drawn==TS_ROWS && dates_drawn==TS_ROWS);
        assert(selections_drawn==1);
    }
    puts("PASS: folder icons, names, date/time and selection bounds share each row's center, including every selected row and scrolled/fractional positions");
}
'''
with tempfile.TemporaryDirectory(prefix="folder-date-rows-") as folder:
    c = Path(folder) / "check.c"
    executable = Path(folder) / "check"
    c.write_text(row_code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(executable), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)
