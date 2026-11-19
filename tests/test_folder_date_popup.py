"""Check manual folder-date controls fit inside their popup and align."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_browser.c").read_text()
header = (root / "include/dash_internal.h").read_text()
icons = header[header.index("enum DashIcon {"):]
icons = icons[:icons.index("};") + 2]


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

# The date-field selection must finish before both rows of hints begin.
rows = int(re.search(r"#define TS_ROWS (\d+)", source).group(1))
row_h = float(re.search(r"#define TS_ROW_H ([\d.]+)f", source).group(1))
height = float(re.search(r'const float w = 580\.0f, h = ([\d.]+)f;', editor).group(1))
assert 70 + rows * row_h + 12 - 3 + 28 + 6 <= height - 36 - 22

code = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef int DashColor;
#define DASH_FS_SMALL 0.66f
#define DASH_C_TEXT2 2
typedef struct { int button; const char *key, *label; } DashHint;
static int mapping, buttons, labels, long_cancel;
static float font_width, last_button_y, last_button_right;
static float hint_y;
'''
code += icons + "\n"
code += r'''
static int dashOkIcon(void) { return mapping ? DI_BTN_CROSS : DI_BTN_CIRCLE; }
static int dashBackIcon(void) { return mapping ? DI_BTN_CIRCLE : DI_BTN_CROSS; }
static const char *localizeDashboardText(const char *s) {
    return long_cancel && !strcmp(s, "Cancel")
        ? "Extremely long localized cancellation text that cannot fit in the popup" : s;
}
static float dashDrawButton(float x, float y, int icon, const char *key) {
    const int expected[] = {DI_DPAD_UD, DI_BTN_L1, DI_BTN_L2, DI_BTN_R1,
                            DI_BTN_R2, dashOkIcon(), dashBackIcon()};
    assert(buttons < 7 && icon == expected[buttons] && !key);
    assert(fabsf(y - (buttons < 3 ? hint_y - 22 : hint_y)) < 0.001f);
    assert(x >= 54 && x + 22 <= 586);
    last_button_right = x + 22; last_button_y = y;
    buttons++;
    return 22;
}
static float dashTextFit(float x, float y, float maxw, const char *text, float scale, DashColor color) {
    float full = strlen(text) * font_width * scale;
    assert(labels < 7 && labels == buttons - 1 && color == DASH_C_TEXT2);
    assert(fabsf(x - last_button_right - 4) < 0.001f);
    assert(fabsf(y + 16 * scale - (last_button_y + 11)) < 0.001f);
    assert(maxw > 1 && x >= 54 && x + maxw <= 586.001f);
    labels++;
    return x + (full > maxw ? maxw : full);
}
'''
# Use the real font-alignment helper, and isolate only localization.
ui = (root / "src/dash_ui.c").read_text()
helper_start = ui.index("float dashButtonTextY(")
code += ui[helper_start:ui.index("\n}\n", helper_start) + 3]
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
    hint_y = 390; /* the production panel (580x372) has origin y=54 */
    for (mapping=0; mapping<2; mapping++) for (long_cancel=0; long_cancel<2; long_cancel++) {
        for (int percent=90; percent<=115; percent+=5) for (int width=8; width<=16; width+=4) {
            font_width = width * percent / 100.0f;
            buttons = labels = 0;
            tsDrawHints(54, hint_y, 532);
            assert(buttons==7 && labels==7);
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
    puts("PASS: two-row hints and localized labels fit popup; folder/date controls match bindings");
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
