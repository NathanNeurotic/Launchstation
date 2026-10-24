"""Execute production visual timing and modal loops with mocked time/drawing.

Host evidence only, not EE/GS performance. Optional git ref runs the same
modal assertions against an older revision to prove the freeze regression.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]


def read(path):
    if len(sys.argv) > 1:
        return subprocess.check_output(["git", "show", f"{sys.argv[1]}:{path}"],
                                       cwd=root, text=True)
    return (root / path).read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


shell = read("src/dash_main.c")
ui = read("src/dash_ui.c")
gfx = read("src/dash_gfx.c")
header = read("include/dash_internal.h")
orbs = read("src/dash_bg_ps2.c")
menu = function(ui, "int dashModalMenu(")
buttons = function(ui, "static int modalButtons(")
symbols = sorted(set(re.findall(r"\b(?:DI_|DASH_FS_|DASH_SND_|DASH_HL_)\w+",
                               menu + buttons)))
colors = sorted(set(re.findall(r"\bDASH_C_\w+", menu + buttons)))
code = r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
typedef struct { int r,g,b,a; } DashColor;
typedef struct { int icon; const char *label,*detail; int disabled; } DashMenuItem;
typedef struct { int pressed; } DashInput;
static struct { int anim,effects; } dashCfg={100,100};
static struct { int glow,highlight; } dashTheme;
static float anim_time,frame_dt,section_anim=1,fade_in=1,bg_time,ui_time;
static u64 last_frame_ms,now_ms;
static int bg_calls,ui_calls,renders,depth,nested,frames[2],step_ms[3]={16,20,33};
static int preview_calls;
static int nav_scenario, timed_scenario, offscreen, selection_draws;
static float clip_top, clip_bottom, selection_y;
static u64 Timer(void) { return now_ms; }
static void dashBgUpdate(float dt) { bg_calls++; bg_time+=dt*dashCfg.effects/100.0f; }
static void dashUiUpdate(float dt) { ui_calls++; ui_time+=dt; }
static void dashRenderScene(void) { renders++; }
static void dashGfxPresent(void) {}
static void dashInputFlush(void) {}
static void dashSoundPlay(int s) { (void)s; }
static float dashClampf(float x,float lo,float hi) { return x<lo?lo:x>hi?hi:x; }
static float dashLerpf(float a,float b,float t) { return a+(b-a)*t; }
static float dashEaseOutCubic(float x) { return 1-(1-x)*(1-x)*(1-x); }
static DashColor dashItemColor(int on) { return (DashColor){on,0,0,255}; }
static DashColor dashColorScale(DashColor c,float f) { (void)f; return c; }
static DashColor dashColorA(DashColor c,float f) { (void)f; return c; }
static int dashOkIcon(void) { return 0; }
static int dashBackIcon(void) { return 0; }
static float dashTextWidth(const char *s,float f) { return strlen(s)*f; }
static void (*modal_preview)(int);
static void preview(int i) { (void)i; preview_calls++; }
#define DASH_VH 448
#define DASH_VW 640
#define DASH_SAFE_R 592
#define DASH_BLEND_ADD 1
#define DASH_BLEND_NORMAL 0
"""
code += "enum { " + ",".join(symbols) + " };\n"
input_start = header.rfind("enum {", 0, header.index("DASH_IN_UP"))
code += header[input_start:header.index("};", input_start) + 2] + "\n"
for color in colors:
    code += f"#define {color} ((DashColor){{255,255,255,255}})\n"
for draw in ("modalBackdrop", "dashGfxPushAlpha", "dashGfxPushOffset", "modalPanel",
             "dashTextS", "dashTextFit", "dashTextScrollTimed", "dashTextScroll", "dashGfxScissor", "dashDrawSelection",
             "dashIcon", "drawButton", "dashTextRight", "dashGfxScissorReset", "dashDrawScrollbar",
             "modalHintRow", "dashGfxPopOffset", "dashGfxPopAlpha", "dashGfxBlend",
             "dashGlow", "dashTextWrap", "dashPanel", "dashPanelBorder", "dashTextCenter"):
    code += f"#define {draw}(...) ((void)0)\n"
code += "#undef dashGfxScissor\n#define dashGfxScissor(x,y,w,h) (clip_top=(y),clip_bottom=(y)+(h))\n"
code += "#undef dashDrawSelection\n#define dashDrawSelection(x,y,w,h,r,a) ((void)(nav_scenario>=4 && depth==0 ? (selection_draws++, selection_y=(y), offscreen+=((y)<clip_top || (y)+(h)>clip_bottom)) : 0))\n"
code += function(gfx, "float dashApproach(")
code += function(shell, "float dashTime(")
code += function(shell, "float dashAnimSpeed(")
if "float dashVisualUpdate(" in shell:
    code += function(shell, "float dashFrameDelta(")
    code += function(shell, "float dashVisualUpdate(")
else:
    code += "static float dashFrameDelta(void) { return 0; }\n"
    code += "static float dashVisualUpdate(void) { return 0; }\n"
code += r"""
#define DASH_FX_OFF 0
#define DASH_FX_FULL 2
typedef struct { int hour,minute; float second; } OsdTime;
static int clock_stuck, clock_valid=1;
static int menuTitleGetClockFields(int *y,int *mo,int *d,int *h,int *mi,int *s,int *u,int *f) {
    (void)y;(void)mo;(void)d;(void)u;(void)f;
    *h=12; *mi=0; *s=clock_stuck?0:(now_ms/1000)%60; return clock_valid;
}
"""
code += function(orbs, "static void osdTime(")
code += r"""
static int modalButtons(const char *,const char *,const char *,int,DashColor,
                        const char *,const char *,int,u64);
static void dashInputPoll(DashInput *in) {
    int f=++frames[depth];
    now_ms+=step_ms[(f-1)%3];
    in->pressed=timed_scenario ? 0 : (f==4?DASH_IN_BACK:0);
    if(nav_scenario && depth==0) {
        if(f==1)
            in->pressed=nav_scenario==1?DASH_IN_L2:nav_scenario==2?DASH_IN_R2:nav_scenario==4?DASH_IN_UP:(nav_scenario==5 || nav_scenario==8)?DASH_IN_DOWN:nav_scenario==6?DASH_IN_L2:nav_scenario==7?DASH_IN_R2:0;
        else if(f==2)
            in->pressed=DASH_IN_OK;
    }
    if(nested && depth==0 && f==2) {
        depth=1;
        assert(!modalButtons("nested","confirm",NULL,0,(DashColor){0},"OK","Cancel",0,0));
        depth=0;
    }
}
"""
if "static int modalEnabledEdge(" in ui:
    code += function(ui, "static int modalEnabledEdge(")
code += menu + buttons
code += r"""
static void reset(void) {
    anim_time=bg_time=ui_time=0; bg_calls=ui_calls=renders=preview_calls=0;
    section_anim=fade_in=0;
    now_ms=last_frame_ms=1000; frame_dt=0;
    depth=nested=nav_scenario=timed_scenario=offscreen=selection_draws=0;
    selection_y=clip_top=clip_bottom=0;
    memset(frames,0,sizeof(frames)); modal_preview=NULL;
    dashCfg.anim=100; dashCfg.effects=100;
}
static void check(void) {
    assert(renders>0 && bg_calls==renders && ui_calls==renders);
    assert(anim_time>0 && fabsf(anim_time-ui_time)<0.00001f);
    assert(fabsf(anim_time-(last_frame_ms-1000)/1000.0f)<0.00001f);
    assert(section_anim>0 && fade_in>0);
}
int main(void) {
    DashMenuItem items[]={{0,"one",NULL,0},{0,"two",NULL,0}};
    for(int ms=16;ms<=33;ms+=(ms==16?4:13)) {
        reset(); step_ms[0]=step_ms[1]=step_ms[2]=ms;
        assert(dashModalMenu("ordinary",NULL,items,2,0)==-1);
        check(); assert(renders==3 && fabsf(anim_time-ms*3/1000.0f)<0.00001f);
        assert(fabsf(bg_time-anim_time)<0.00001f);
    }
    reset(); step_ms[0]=16;step_ms[1]=20;step_ms[2]=33;
    modal_preview=preview;
    assert(dashModalMenu("preview",NULL,items,2,0)==-1);
    check(); assert(preview_calls==1 && bg_calls==3);
    assert(fabsf(anim_time-0.069f)<0.00001f);
    /* Smooth RTC interpolation consumes this same advancing dashTime(). */
    assert(dashTime()>0 && dashFrameDelta()==0.033f);
    OsdTime ot; osdTime(DASH_FX_FULL,&ot); float second=ot.second;
    now_ms+=20; float rtc_dt=dashVisualUpdate(); osdTime(DASH_FX_FULL,&ot);
    assert(fabsf(ot.second-second-rtc_dt)<0.00001f);
    /* A stalled/dead RTC must not cap orb phase at a fraction of a second. */
    clock_stuck=1; osdTime(DASH_FX_FULL,&ot);
    anim_time+=61.25f; osdTime(DASH_FX_FULL,&ot);
    assert(ot.hour==12 && ot.minute==1 && fabsf(ot.second-1.25f)<0.0001f);
    OsdTime frozen=ot;
    anim_time+=5; osdTime(DASH_FX_OFF,&ot);
    assert(ot.hour==frozen.hour && ot.minute==frozen.minute && ot.second==frozen.second);
    clock_valid=0; anim_time+=1; osdTime(DASH_FX_FULL,&ot);
    assert(ot.minute==1 && fabsf(ot.second-7.25f)<0.0001f);
    clock_valid=1; clock_stuck=0; now_ms=10500; osdTime(DASH_FX_FULL,&ot);
    assert(ot.hour==12 && ot.minute==0 && ot.second==10);
    reset(); nested=1;
    assert(dashModalMenu("parent",NULL,items,2,0)==-1);
    check(); assert(renders==6 && fabsf(anim_time-bg_time)<0.00001f);
    /* Returning to the main loop counts only time since the last modal tick. */
    float before=anim_time; u64 anchor=last_frame_ms;
    now_ms+=20; float dt=dashVisualUpdate();
    assert(fabsf(dt-(now_ms-anchor)/1000.0f)<0.00001f);
    assert(fabsf(anim_time-before-dt)<0.00001f);
    reset(); dashCfg.effects=0; dashCfg.anim=0;
    assert(dashModalMenu("motion off",NULL,items,2,0)==-1);
    check(); assert(bg_time==0 && section_anim==1 && fade_in==1);
    /* Exercise real L2/R2 dispatch against disabled edge entries. */
    DashMenuItem disabled_ends[]={{0,"no top",NULL,1},
                                   {0,"first active",NULL,0},
                                   {0,"no middle",NULL,1},
                                   {0,"last active",NULL,0},
                                   {0,"no bottom",NULL,1}};
    reset(); nav_scenario=1; /* from last active, jump to first enabled */
    assert(dashModalMenu("jump first",NULL,disabled_ends,5,3)==1);
    reset(); nav_scenario=2; /* from first active, jump to last enabled */
    assert(dashModalMenu("jump last",NULL,disabled_ends,5,1)==3);
    reset(); nav_scenario=3; /* disabled initial selection is corrected */
    assert(dashModalMenu("disabled initial",NULL,disabled_ends,5,4)==1);
    DashMenuItem all_disabled[]={{0,"a",NULL,1},{0,"b",NULL,1}};
    reset(); modal_preview=preview;
    assert(dashModalMenu("all disabled",NULL,all_disabled,2,0)==-1);
    assert(renders==0 && modal_preview==NULL); /* no stranded preview callback */
    DashMenuItem long_items[30];
    for(int i=0;i<30;i++)long_items[i]=(DashMenuItem){0,"entry",NULL,i==0 || i==29 || i==15};
    for(int scenario=4;scenario<=7;scenario++) {
        reset();step_ms[0]=step_ms[1]=step_ms[2]=16;nav_scenario=scenario;
        int first=scenario==4 || scenario==7;
        assert(dashModalMenu("long wrap/jump",NULL,long_items,30,first?1:28)==(first?28:1));
        assert(selection_draws>0 && offscreen==0); /* highlight drawn and visible on the first wrapped frame */
    }
    reset();step_ms[0]=step_ms[1]=step_ms[2]=16;nav_scenario=8;
    assert(dashModalMenu("adjacent",NULL,long_items,30,1)==2);
    assert(selection_draws>0 && offscreen==0 && selection_y<clip_top+80); /* adjacent motion still eases */
    reset(); timed_scenario=1; step_ms[0]=step_ms[1]=step_ms[2]=100;
    assert(!modalButtons("video","visible?","",0,(DashColor){0},"Keep","Revert",1,Timer()+5000));
    assert(now_ms==6000 && frames[0]==50); /* no input: revert at five seconds */
    reset(); timed_scenario=1; now_ms=7000;
    assert(!modalButtons("expired","visible?","",0,(DashColor){0},"Keep","Revert",1,7000));
    assert(frames[0]==0); /* expired confirmation cannot accept a late button */
    reset(); now_ms+=2000;
    assert(dashVisualUpdate()==0.1f); /* retain the long-stall clamp */
    now_ms=last_frame_ms-1;
    assert(dashVisualUpdate()==0); /* timer reset cannot unsigned-underflow */
    puts("PASS: ordinary/preview/nested menus, measured timing, RTC interpolation, no double ticks, motion off and stalls");
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-modal-animation-") as tmp:
    harness = Path(tmp) / "modal.c"
    exe = Path(tmp) / "modal.exe"
    harness.write_text(code)
    subprocess.run([compiler, "-std=gnu11", str(harness), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Every scene-owning frame loop must tick, including full-screen tools and
# both worker-wait paths (the loading path ticks inside loadingDraw).
if len(sys.argv) == 1:
    for name in ("dash_ui", "dash_widgets", "dash_settings", "dash_browser",
                 "dash_hdd", "dash_info", "dash_editor", "dash_worker"):
        # Count actual call sites, not prose in C comments. Embedded Files
        # draws through the shared dashboard frame and documents that path.
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", read(f"src/{name}.c"), flags=re.S)
        assert text.count("dashVisualUpdate()") == text.count("dashRenderScene()"), name
        assert "1.0f / 60.0f" not in text and "1.0f / 30.0f" not in text, name
    run = function(shell, "void DashRun(")
    assert run.count("dashVisualUpdate()") == 1
    assert "anim_time +=" not in run and "dashBgUpdate(" not in run and "dashUiUpdate(" not in run
    tick = function(shell, "float dashVisualUpdate(")
    calls = set(re.findall(r"\b(\w+)\s*\(", tick[tick.index("{"):])) - {"if"}
    assert calls <= {"Timer", "dashBgUpdate", "dashUiUpdate", "dashApproach", "dashAnimSpeed"}, calls
    assert "dashVisualUpdate" not in function(shell, "void dashRenderScene(")
    print("PASS: every scene loop ticks once; visual helper excludes input, device polling and IO")

# Menu rows: a long detail must never overlap its label (Settings changes >
# Discard changes), and START hints must draw the START glyph even without the
# pad PNG atlas (the text badge read as a SELECT bar on CRTs).
menu = function(ui, "int dashModalMenu(")
assert "detail ? 90" not in menu
assert "detail_w = avail - label_w - 14.0f;" in menu
assert "dashTextFit(tx, ry + 7, label_w," in menu
key_icon = function(ui, "static int keyIcon(")
assert '"START"' in key_icon and '"L1"' in key_icon and '"R3"' in key_icon
assert "dashIconsPadPng()" not in key_icon
assert "return DI_BTN_START;" in key_icon
assert "const int max_rows = 7;" in menu
assert 76 + 7 * 38 + 44 + 18 <= 448 - 40  # 20px safe margin on each side
print("PASS: menus fit inside 4:3 safe area; controller hints use vector glyphs")
