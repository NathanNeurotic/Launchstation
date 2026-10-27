"""Exercise real GS transition code, including cached-mode/geometry drift."""
from pathlib import Path
import shutil
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/draw_gs.c').read_text()
source = source.replace('#include "draw_private.h"', '')
prefix = r'''
#include <assert.h>
#include <string.h>
#define GS_MODE_NTSC 2
#define GS_MODE_PAL 3
#define GS_MODE_VGA_640_60 4
#define GS_MODE_DTV_480P 5
#define GS_MODE_DTV_576P 6
#define GS_INTERLACED 1
#define GS_NONINTERLACED 0
#define GS_FIELD 0
#define GS_FRAME 1
#define GS_ONESHOT 1
#define GS_SETTING_ON 1
#define GS_SETTING_OFF 0
#define FONT_HEIGHT 16
#define LINE_THICKNESS 2
enum {TV_mode_AUTO,TV_mode_NTSC,TV_mode_PAL,TV_mode_VGA,TV_mode_480P,TV_mode_576P,TV_mode_576I,TV_mode_COUNT};
static char ROMVER_data[16]="0220E";
static unsigned char console_is_PSX;
typedef struct { int Mode,Width,Height,Interlace,Field,StartX,StartY;
    int PrimAAEnable,DoubleBuffering,ZBuffering; } GSGLOBAL;
static GSGLOBAL g,*gsGlobal=&g;
static struct { int TV_mode,screen_x,screen_y; } config,*setting=&config;
static int TV_mode,SCREEN_WIDTH,SCREEN_HEIGHT,Menu_start_y=20,Menu_end_y,Frame_end_y,Menu_tooltip_y;
static int gsScreenGeneration,stage,rebuilds,native_mode=TV_mode_PAL;
static int uLE_InitializeRegion(void) { return native_mode; }
static void fadeScreenToBlack(void) { assert(stage==0);stage=1; }
static void gsKit_queue_exec(GSGLOBAL *p) { (void)p;assert(stage==1);stage=2; }
static void dmaKit_wait_fast(void) { assert(stage==2); }
static void gsKit_finish(void) { assert(stage==2);stage=3; }
static void gsKit_init_screen(GSGLOBAL *p) {
    assert(stage==3);stage=4;rebuilds++;p->StartX=500;p->StartY=40;
    assert(p->Width==640);
    if(p->Mode==GS_MODE_PAL || p->Mode==GS_MODE_NTSC) {
        assert(p->Interlace==GS_INTERLACED && p->Field==GS_FIELD);
        assert(p->Height==(p->Mode==GS_MODE_PAL?512:448));
    } else assert(p->Interlace==GS_NONINTERLACED && p->Field==GS_FRAME);
}
static void gsKit_mode_switch(GSGLOBAL *p,int mode) { (void)p;assert(stage==4 && mode==GS_ONESHOT);stage=5; }
static void clearScreenToBlack(void) { assert(stage==5);stage=6; }
static void gsKit_set_display_offset(GSGLOBAL *p,int x,int y) { (void)p;assert(stage==0 || stage==6);assert(x==3 && y==2); }
'''
# setupGS is exercised by target builds; this test isolates live transitions.
a = source.index('void setupGS(void)')
b = source.index('void updateScreenMode(void)')
source = source[:a] + source[b:]
program = prefix + source + r'''
int main(void) {
    g.StartX=500;g.StartY=40;setting->screen_x=3;setting->screen_y=2;
    for(int repeat=0;repeat<4;repeat++) for(int mode=1;mode<=5;mode++) {
        stage=0;setting->TV_mode=mode;updateScreenMode();assert(stage==6);
        int old=rebuilds,gen=gsScreenGeneration;
        stage=0;updateScreenMode();assert(stage==0 && old==rebuilds && gen==gsScreenGeneration);
        /* A stale cached TV mode must not suppress a live timing repair. */
        stage=0;g.Mode=-1;updateScreenMode();assert(stage==6 && rebuilds==old+1);
        stage=0;g.Height=1;updateScreenMode();assert(stage==6);
        stage=0;g.Interlace=!g.Interlace;updateScreenMode();assert(stage==6);
    }
    stage=0;setting->TV_mode=TV_mode_AUTO;updateScreenMode();assert(g.Mode==GS_MODE_PAL);
    native_mode=TV_mode_NTSC;stage=0;updateScreenMode();assert(g.Mode==GS_MODE_NTSC);
    native_mode=TV_mode_PAL;stage=0;updateScreenMode();assert(g.Mode==GS_MODE_PAL);
    assert(gsScreenGeneration==rebuilds);
    /* 576i is PAL timing under its own saved ID. */
    stage=0;setting->TV_mode=TV_mode_576I;updateScreenMode();
    assert(g.Mode==GS_MODE_PAL && TV_mode==TV_mode_PAL && setting->TV_mode==TV_mode_576I);
    /* 576p on a pre-2.20 ROM (and non-PSX 1.80J) boots in Auto before any GS reset, but stays saved. */
    const struct { const char *rom; int psx, ok; } roms[]={
        {"0100J",0,0},{"0200E",0,0},{"0219E",0,0},{"0220E",0,1},{"0250E",0,1},{"",0,0},{"02x0E",0,0},
        {"0180J",0,0},{"0180J",1,1},{"0210J",1,1},{"0180E",1,0},{"0200J",1,0}
    };
    for(unsigned i=0;i<sizeof(roms)/sizeof(roms[0]);i++) {
        strcpy(ROMVER_data,roms[i].rom);console_is_PSX=(unsigned char)roms[i].psx;
        assert(is576pSupported()==roms[i].ok);
        setting->TV_mode=TV_mode_576P;
        assert(requestedTVMode()==(roms[i].ok?TV_mode_576P:native_mode));
        assert(setting->TV_mode==TV_mode_576P); /* the saved choice survives a console that cannot use it */
    }
    const int invalid[]={-1,TV_mode_COUNT,9999};
    for(unsigned i=0;i<3;i++) { setting->TV_mode=invalid[i];assert(requestedTVMode()==native_mode && setting->TV_mode==TV_mode_AUTO); }
    assert(TV_mode_576P==5 && TV_mode_576I==6); /* saved IDs remain stable */
}
'''
with tempfile.TemporaryDirectory(prefix='r3z-video-') as td:
    path=Path(td)
    (path/'test.c').write_text(program)
    subprocess.run([shutil.which('cc') or shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(path/'test.c'), '-o', str(path/'test.exe')], check=True)
    subprocess.run([str(path/'test.exe')], check=True)
print('PASS: repeated video mode/Auto transitions, live state repair, DMA/reset/clear ordering, 576i as PAL, 576p ROM gating')
