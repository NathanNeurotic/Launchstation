"""Exercise real dashboard frame setup and vsync flip code using host GS mocks.

Single-buffer scanout causes horizontal tearing in animated dashboards.
Dashboard setup must allocate the second buffer, preserve calibration, and
invalidate GS texture state. When IO worker is busy, yielding at VBLANK
must still swap/display the completed front buffer (not the drawing one).
The classic GS setup is intentionally unchanged.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
gfx=(root/"src/dash_gfx.c").read_text()
classic=(root/"src/draw_gs.c").read_text()

def fn(signature):
    start=gfx.index(signature)
    pos=gfx.index("{",start)+1
    depth=1
    while depth:
        depth+=(gfx[pos]=="{")-(gfx[pos]=="}")
        pos+=1
    return gfx[start:pos]+"\n"

assert "gsGlobal->DoubleBuffering = GS_SETTING_ON;" in fn("void dashGfxInit(void)")
assert "gsKit_init_screen(gsGlobal);" in fn("void dashGfxInit(void)")
assert "gsScreenGeneration++;" in fn("void dashGfxInit(void)")
assert "gsKit_set_display_offset(gsGlobal, setting->screen_x, setting->screen_y)" in fn("void dashGfxInit(void)")
assert "gsGlobal->DoubleBuffering = GS_SETTING_OFF;" in classic
assert "gsGlobal->DoubleBuffering = GS_SETTING_ON;" not in classic

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {
    int DoubleBuffering,FirstFrame,ActiveBuffer,Width,PSM;
    uint32_t ScreenBuffer[2];
} GSGLOBAL;
typedef struct { int screen_x,screen_y; } Setting;
#define GS_SETTING_OFF 0
#define GS_SETTING_ON 1
static GSGLOBAL g;
static GSGLOBAL *gsGlobal=&g;
static Setting config={3,-2};
static Setting *setting=&config;
static volatile uint32_t csr=8;
static volatile uint32_t *GS_CSR=&csr;
static int gsScreenGeneration, gfx_screen_gen, gfx_ready, bound_generation;
static int begin_count,glow_count,check_count,init_count,offset_count;
static int shown_count,shown_base,shown_width,shown_psm,active_count;
static int queue_count,tex_frames,pump_count,sprite_count,park_count;
static int sync_count,worker_busy,wait_calls;
static void buildCornerTextures(void) { begin_count++; }
static void buildGlowTextures(void) { glow_count++; }
static int dashGfxCheckScreenChange(void) {
    check_count++;
    bound_generation=gsScreenGeneration;
    gfx_screen_gen=gsScreenGeneration;
    return 1;
}
static void gsKit_init_screen(GSGLOBAL *ptr) {
    assert(ptr==gsGlobal && ptr->DoubleBuffering==GS_SETTING_ON);
    init_count++;
    ptr->ScreenBuffer[0]=0;
    ptr->ScreenBuffer[1]=0x120000;
    ptr->Width=640;
    ptr->PSM=1;
    ptr->ActiveBuffer=1;
}
static void gsKit_set_display_offset(GSGLOBAL *ptr,int x,int y) {
    assert(ptr==gsGlobal && x==3 && y==-2); offset_count++;
}
static void dashSpriteBatchEnd(void) { sprite_count++; }
static void dashWorkerParkPoint(void) { park_count++; }
static int dashWorkerIoBusy(void) { return worker_busy; }
static void DelayThread(int duration) { assert(duration==250); wait_calls++; }
static void mockDisplay(int base,int width,int psm,int x,int y) {
    assert(x==0 && y==0);
    shown_base=base;shown_width=width;shown_psm=psm;
    shown_count++;
}
#define GS_SET_DISPFB2(base,width,psm,x,y) mockDisplay(base,width,psm,x,y)
static void gsKit_setactive(GSGLOBAL *ptr) { assert(ptr==gsGlobal);active_count++; }
static void gsKit_sync_flip(GSGLOBAL *ptr) { assert(ptr==gsGlobal);sync_count++; }
static void gsKit_queue_exec(GSGLOBAL *ptr) { assert(ptr==gsGlobal);queue_count++; }
static void gsKit_TexManager_nextFrame(GSGLOBAL *ptr) { assert(ptr==gsGlobal);tex_frames++; }
static void dashWorkerPump(void) { pump_count++; }
'''
code+=fn("void dashGfxInit(void)")
code+=fn("static void waitVsyncSharing(void)")
code+=fn("static void flipVsyncSharing(void)")
code+=fn("void dashGfxPresent(void)")
code+=r'''
int main(void) {
    g.DoubleBuffering=GS_SETTING_OFF;
    dashGfxInit();
    assert(g.DoubleBuffering==GS_SETTING_ON);
    assert(init_count==1 && gsScreenGeneration==1 && bound_generation==1);
    assert(offset_count==1 && begin_count==1 && glow_count==1);
    dashGfxInit(); /* do not tear down the screen a second time */
    assert(init_count==1 && gsScreenGeneration==1 && offset_count==1);

    /* Worker-active path: completed buffer 1 becomes visible and back
     * buffer 0 receives the next frame, exactly matching gsKit. */
    g.FirstFrame=GS_SETTING_OFF;
    worker_busy=1;
    csr=8;
    dashGfxPresent();
    assert(shown_count==1 && shown_base==0x120000/8192);
    assert(shown_width==10 && shown_psm==1);
    assert(g.ActiveBuffer==0 && active_count==1 && sync_count==0);
    assert(queue_count==1 && tex_frames==1 && pump_count==1);
    assert(park_count==1 && sprite_count==1);

    g.ScreenBuffer[0]=0;
    dashGfxPresent();
    assert(shown_count==2 && shown_base==0 && g.ActiveBuffer==1);
    assert(active_count==2 && sync_count==0);

    /* Unbusy path uses gsKit's own flip; this stub intentionally does not
     * change ActiveBuffer because gsKit internals are outside our scope. */
    worker_busy=0;
    dashGfxPresent();
    assert(sync_count==1 && shown_count==2 && queue_count==3);

    /* On the very first frame, keep gsKit's FirstFrame semantics. */
    g.FirstFrame=GS_SETTING_ON;worker_busy=1;
    dashGfxPresent();
    assert(sync_count==2 && shown_count==2);
    puts("PASS: dashboard double buffering, VBLANK front/back swap, texture invalidation, legacy GS isolation");
}
'''
cc=shutil.which("gcc") or shutil.which("clang")
assert cc,"Host compiler required"
with tempfile.TemporaryDirectory(prefix="launchstation-anti-tear-") as tmp:
    src=Path(tmp)/"test.c"
    exe=Path(tmp)/"test"
    src.write_text(code)
    subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=5)
