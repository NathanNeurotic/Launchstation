"""Exercise shared viewport easing and edge-row coverage at real frame rates."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
gfx = (root / "src/dash_gfx.c").read_text()
start = gfx.index("float dashApproach(")
end = gfx.index("\n}\n", start) + 3
program = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
'''
program += '#include "' + str(root / 'include/list_scroll.h') + '"\n'
program += gfx[start:end] + "\n"
header = (root / "include/dash_internal.h").read_text()
# Compile the real thumb geometry and editor scroll model against host drawing.
def function(text, signature):
    begin = text.index(signature)
    end = text.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end] + '\n'

ui = (root / 'src/dash_ui.c').read_text()
editor = (root / 'src/dash_editor.c').read_text()
program += r'''
typedef int DashColor;
#define DASH_C_TEXT3 1
#define DASH_C_ACCENT 2
static DashColor dashColorA(DashColor color,float alpha) { (void)alpha;return color; }
static int bar_calls;
static float thumb_y,thumb_h;
static void scrollbarShape(float x,float y,float h,DashColor color) {
    assert(x==600 && h>0);
    if(color==DASH_C_ACCENT) { thumb_y=y;thumb_h=h; }
    bar_calls++;
}
'''
program += function(header, 'static inline float dashClampf(') + function(ui, 'void dashDrawScrollbar(')
program += r'''
static void checkThumb(float first,int visible,int total,float h) {
    bar_calls=0;dashDrawScrollbar(600,100,h,first,visible,total);
    if(total<=visible || visible<=0 || h<=0) { assert(!bar_calls);return; }
    assert(bar_calls==2 && thumb_h>0 && thumb_h<=h);
    assert(thumb_y>=100 && thumb_y+thumb_h<=100+h+0.0001f);
    float maximum=total-visible;
    float bounded=first<0?0:first>maximum?maximum:first;
    assert(fabsf(thumb_y-(100+(h-thumb_h)*bounded/maximum))<0.0001f);
}
static struct {int anim;} dashCfg={100};
#define MAX_PATH 1024
'''
program += function((root/'src/dash_main.c').read_text(), 'float dashAnimSpeed(')
doc_end=editor.index('} EdDoc;')+len('} EdDoc;')
program += editor[editor.rfind('typedef struct',0,doc_end):doc_end]+'\n'
program += re.search(r'^#define ED_TAB .+$',editor,re.M).group()+'\n'
for signature in ('static void docRelines(', 'static int docLineOf(', 'static int docLineEnd(',
                  'static int docColOf(', 'static int docOffAtCol(', 'static void moveCursor(',
                  'static void moveLines(', 'static void scrollToCursor(', 'static void animateScroll('):
    program += function(editor,signature)
program += r'''
static void checkEditorScrollbar(void) {
    char text[1024];int len=0;
    for(int i=0;i<80;i++)len+=snprintf(text+len,sizeof(text)-len,"line %d\n",i);
    EdDoc d={.buf=text,.len=len,.mark=-1},other={.buf=text,.len=len,.mark=-1};
    docRelines(&d);docRelines(&other);
    const int rows=18;
    /* Preserve upstream's page/wrap snap: the thumb eases for visible
     * line steps, but never lags far enough to hide the focused row. */
    for(int i=0;i<2;i++) { moveLines(&d,rows-1);scrollToCursor(&d,rows,60); }
    assert(d.top==rows-1 && d.top_anim==0);
    animateScroll(&d,rows,1.0f/60);
    assert(d.top_anim>0 && d.top_anim<d.top && d.top_anim!=floorf(d.top_anim));
    moveLines(&d,-rows);scrollToCursor(&d,rows,60);animateScroll(&d,rows,1.0f/60);
    assert(d.top==rows-2 && d.top_anim>0 && d.top_anim<d.top);
    assert(d.top_anim!=floorf(d.top_anim));
    checkThumb(d.top_anim,rows,d.nlines,240);
    float before=d.top_anim;
    for(int i=0;i<2;i++) { moveLines(&d,-(rows-1));scrollToCursor(&d,rows,60); }
    assert(!d.top);animateScroll(&d,rows,1.0f/60);
    assert(d.top_anim>=0 && d.top_anim<=before); /* reverse page may snap */
    animateScroll(&other,rows,1.0f/60);assert(other.top_anim==0); /* per-document state */
    dashCfg.anim=0;
    moveLines(&d,2*(rows-1));scrollToCursor(&d,rows,60);animateScroll(&d,rows,1.0f/60);
    assert(d.top_anim==d.top); /* animation off */
    dashCfg.anim=100;
    scrollToCursor(&d,11,60);animateScroll(&d,11,1.0f/60);
    assert(d.top_anim>=0 && d.top_anim<=d.nlines-11); /* keyboard viewport resize */
    d.len=d.cur=0;docRelines(&d);scrollToCursor(&d,rows,60);animateScroll(&d,rows,1.0f/60);
    assert(!d.top_anim);checkThumb(d.top_anim,rows,d.nlines,240);
    free(d.lines);free(other.lines);
}
'''
program += r'''
static float slide(float current,int target,int count,int rows,float dt,float speed) {
    return listSlideTop(current,target,target+(rows-1)/2,count,rows,dashApproach(0,1,14*speed,dt));
}
static void coverage(float top,int count,int rows) {
    assert(top>=0 && top<=(count>rows?count-rows:0));
    int first=(int)top,last=first;
    for(int i=first;i<count && i<top+rows;i++) {
        float y=(i-top)*24;
        assert(y<rows*24 && y+24>0); last=i;
    }
    if(count>=rows) {
        assert((first-top)*24<=0);
        assert((last+1-top)*24>=rows*24-0.001f);
    }
}
int main(void) {
    checkEditorScrollbar();
    /* Fractional rows must survive the public API, rather than snapping to an int. */
    checkThumb(0.375f,9,40,240);assert(fabsf(thumb_y-102.25f)<0.0001f);
    checkThumb(-3.5f,9,40,240);assert(thumb_y==100);
    checkThumb(100.5f,9,40,240);assert(fabsf(thumb_y+thumb_h-340)<0.0001f);
    checkThumb(7.5f,9,40,10);assert(thumb_h==10 && thumb_y==100);
    checkThumb(0,0,40,240);checkThumb(0,40,40,240);checkThumb(0,9,0,240);
    const float rates[]={30,60,120};
    float result[3];
    for(int rate=0;rate<3;rate++) {
        float top=0;
        for(int frame=0;frame<(int)(rates[rate]*0.2f);frame++)
            top=slide(top,1,40,9,1/rates[rate],1);
        assert(top>0 && top<1);
        checkThumb(top,9,40,240);assert(thumb_y>100 && thumb_y<106);
        result[rate]=top;
        assert(fabsf(top-(1-expf(-14*0.2f)))<0.0001f);
    }
    assert(fabsf(result[0]-result[1])<0.0001f && fabsf(result[1]-result[2])<0.0001f);
    for(int rows=1;rows<=12;rows++) for(int count=1;count<=300;count+=7) {
        float top=-1;
        for(int selected=0;selected<count;selected++) {
            int target=listCenteredTop(selected,count,rows);
            float previous=top;
            top=slide(top,target,count,rows,1.0f/60,1);
            coverage(top,count,rows);
            if(previous>=0 && target>previous && target-previous<rows &&
               target+(rows-1)/2<previous+rows)
                assert(top>previous && top<target);
        }
        for(int selected=count-1;selected>=0;selected--) {
            int target=listCenteredTop(selected,count,rows);
            top=slide(top,target,count,rows,1.0f/30,1);
            coverage(top,count,rows);
        }
    }
    /* Short-list wraps must reset even when the top changes by one row. */
    assert(listSlideTop(1,0,0,10,9,0.2f)==0);
    assert(listSlideTop(0,1,9,10,9,0.2f)==1);
    float first=slide(4,5,40,9,1.0f/60,1);
    float reverse=slide(first,4,40,9,1.0f/60,1);
    assert(reverse>4 && reverse<first);
    assert(slide(-1,7,40,9,0,1)==7);
    assert(slide(4,5,40,9,0,1)==4);
    assert(slide(4,5,40,9,1.0f/60,1000)==5);
    assert(slide(4,30,40,9,1.0f/60,1)==30);
    assert(slide(30,0,40,9,1.0f/60,1)==0);
    assert(slide(20,0,3,9,1.0f/60,1)==0);
    assert(slide(20,0,0,9,1.0f/60,1)==0);
    assert(listSlideTop(4,5,9,40,9,-1)==4 && listSlideTop(4,5,9,40,9,2)==5);
    puts("PASS: centered viewport slides at 30/60/120 Hz, clips complete incoming/outgoing rows, reverses smoothly, resets safely and honors motion off, paging/wrap and list shrinkage");
}
'''
compiler = shutil.which("gcc") or shutil.which("clang")
assert compiler, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="list-slide-") as folder:
    source, binary = Path(folder) / "check.c", Path(folder) / "check"
    source.write_text(program)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(source), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

for filename, camera, count, rows in (
    ("dash_apps.c", "list_top_anim", "norder", "LIST_ROWS"),
    ("dash_games.c", "list_top_anim", "norder", "LIST_ROWS"),
    ("dash_browser.c", "fb.top_anim", "rows_total", "FB_ROWS"),
    ("dash_tools.c", "ttop_anim", "NTOOLS", "rows"),
    ("dash_settings.c", "item_top_anim", "c->n", "ITEM_ROWS"),
    ("dash_hdd.c", "top_anim", "hm.n", "ROWS"),
    ("dash_ui.c", "top_anim", "n", "rows"),
    ("dash_widgets.c", "top_anim", "n_rows", "rows"),
):
    text = (root / "src" / filename).read_text()
    assert f"{camera} = listSlideTop({camera}," in text, filename
    assert f"i = (int){camera}; i < {count} && i < {camera} + {rows}" in text, filename
    assert f"(i - {camera}) *" in text, filename
    assert "dashGfxScissor(" in text and "dashGfxScissorReset();" in text, filename
    assert f", {camera}, {rows}, {count});" in text, filename
print("PASS: Home/Applications, File Browser, Tools, Settings, HDD Manager, menus and pickers draw the shared animated viewport and clipped edge rows")

assert 'camera / TS_ROW_H, TS_ROWS, e->count);' in (root/'src/dash_browser.c').read_text()
assert 'grid_cam / GRID_PITCH_Y, 3,' in (root/'src/dash_apps.c').read_text()
assert 'body_h, scroll,' in (root/'src/dash_info.c').read_text()
assert 'd->top_anim, rows, d->nlines);' in editor
print('PASS: every scrollbar follows its animated viewport; editor line steps ease, page jumps snap safely, buffers stay independent, and animation off is honored')
