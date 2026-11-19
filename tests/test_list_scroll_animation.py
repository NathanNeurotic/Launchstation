"""Exercise shared viewport easing and edge-row coverage at real frame rates."""
from pathlib import Path
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
'''
program += '#include "' + str(root / 'include/list_scroll.h') + '"\n'
program += gfx[start:end] + "\n"
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
    const float rates[]={30,60,120};
    float result[3];
    for(int rate=0;rate<3;rate++) {
        float top=0;
        for(int frame=0;frame<(int)(rates[rate]*0.2f);frame++)
            top=slide(top,1,40,9,1/rates[rate],1);
        assert(top>0 && top<1);
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
    assert "dashGfxScissor(" in text, filename
    assert "dashGfxScissorReset();" in text, filename
print("PASS: Applications, Games, File Browser, Tools, Settings, HDD Manager, menus and pickers draw the shared animated viewport and clipped edge rows")
