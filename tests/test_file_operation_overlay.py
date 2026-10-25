"""Execute production overlay drawing and progress updates with a host draw sink.

This checks labels, layout, completion and scene ticking, not console rendering.
An optional output directory receives an HTML draw-call preview.
"""
from pathlib import Path
import html
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/dash_ui.c").read_text()
gfx = (root / "src/dash_gfx.c").read_text()
def function(text, signature):
    start = text.index(signature)
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos] + "\n"

delete_op = function((root / "src/dash_browser.c").read_text(), "static void opDelete(")
assert delete_op.index("dashLoadingFrame(f->name, done, n)") < delete_op.index("ret = delete (fb.dir, f)")

code = r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
typedef int DashColor;
#define DASH_VW 640
#define DASH_VH 448
#define DASH_FS_BODY 0.8f
#define DASH_FS_SMALL 0.66f
#define DASH_FS_LABEL 0.92f
#define DASH_C_ACCENT 1
#define DASH_C_TEXT 2
#define DASH_C_TEXT2 3
#define DASH_C_TEXT3 4
#define DASH_BLEND_ADD 1
#define DASH_BLEND_NORMAL 0
static int active=1, worker, scenes, ticks, presents, spinners, texts, bars, emit, preview;
static u64 tick;
static float px,py,pw,ph,fill,track,bar_y;
static char lines[3][256];
static float widths[3], text_bottom;
static int DashIsActive(void) { return active; }
static int dashWorkerOnThread(void) { return worker; }
static int isStartupDriverStatusQuiet(void) { return 0; }
static u64 Timer(void) { return tick; }
static void dashInputFlush(void) {}
static void dashWorkerStatus(const char *s,int done,int total) { (void)s;(void)done;(void)total; }
static void dashStrCopy(char *out,const char *in,int n) { snprintf(out,n,"%s",in); }
static float dashClampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
static float dashVisualUpdate(void) { ticks++; return 0.016f; }
static void dashRenderScene(void) { scenes++; texts=bars=spinners=0;fill=track=0;text_bottom=0; }
static void dashGfxPresent(void) { presents++; }
static void modalPanel(float x,float y,float w,float h,float alpha) {
 (void)alpha;px=x;py=y;pw=w;ph=h;
 assert(x>=32 && y>=32 && x+w<=608 && y+h<=416);
 if(emit)printf("BOX|%.2f|%.2f|%.2f|%.2f\n",x,y,w,h);
}
static void dashDrawSpinner(float x,float y,float radius,DashColor c) {
 (void)x;(void)y;(void)radius;(void)c;spinners++;
}
static void dashTextFit(float x,float y,float w,const char *s,float fs,DashColor c) {
 (void)c; assert(texts<3);widths[texts]=w;snprintf(lines[texts++],256,"%s",s);
 assert(y>=text_bottom);text_bottom=y+32*fs;
 assert(x>=px+20 && x+w<=px+pw-20 && y>=py+20 && y+32*fs<=py+ph-20);
 if(emit)printf("TEXT|%.2f|%.2f|%.2f|%.2f|%s\n",x,y,w,fs,s);
}
static void dashPanel(float x,float y,float w,float h,float radius,DashColor c) {
 (void)radius;(void)c;
 assert(x>=px && x+w<=px+pw && y>=py && y+h<=py+ph && w>=0);
 if(!bars) { assert(y>=text_bottom+12);track=w;bar_y=y; } else { fill=w;assert(w<=track); }
 bars++;
 if(emit)printf("BAR|%.2f|%.2f|%.2f|%.2f|%d\n",x,y,w,h,bars);
}
static void modalHintRow(float x,float y,int a,const char *b,int c,const char *d) {
 (void)x;(void)a;(void)b;(void)c;(void)d;assert(y>=bar_y+20);
}
#define modalBackdrop(...) ((void)0)
#define dashGfxPushAlpha(...) ((void)0)
#define dashGfxPopAlpha(...) ((void)0)
#define dashGfxBlend(...) ((void)0)
#define dashBackIcon() 0
#define dashColorA(c,a) (c)
"""
code += r"""
#define DI_WARNING 1
static struct { char msg[1024]; } fb;
static char failure_title[32], failure_body[1024], failure_detail[1024];
static void dashModalMessage(int icon,const char *title,const char *body,const char *detail) {
 assert(icon==DI_WARNING);snprintf(failure_title,sizeof(failure_title),"%s",title);
 snprintf(failure_body,sizeof(failure_body),"%s",body);
 snprintf(failure_detail,sizeof(failure_detail),"%s",detail?detail:"");
}
"""
code += function((root/"src/dash_browser.c").read_text(), "static void showPasteFailure(")
code += function(gfx, "float dashApproach(")
a=source.index("static int loading_active;")
b=source.index("int dashLoadingActive(",a)
code += source[a:b]
for sig in ["static void loadingDraw(", "static void loadingBegin(", "void dashLoadingBegin(",
            "void dashFileLoadingBegin(", "static void loadingProgress(", "int DashFileProgress(",
            "void DashFileDeleteProgress(", "void dashLoadingFrame(", "void dashLoadingStatus(", "void dashLoadingEnd("]:
    code += function(source,sig)
code += r"""
static void capture(const char *name) {
 texts=bars=spinners=0;fill=track=0;
 emit=preview;
 if(emit)printf("CASE|%s\n",name);
 loadingDraw();
 emit=0;
 assert(scenes==ticks && ticks==presents);
}
int main(int argc,char **argv) {
 (void)argv; preview=argc>1;
 strcpy(fb.msg,"Writing destination (-28)\nNot enough free space.\nBOOT.ELF\nusb0: -> mc0:\n0/1024 B\nIOP 1>1; fixture-head");
 showPasteFailure(0);
 assert(!strcmp(failure_title,"Paste failed"));
 assert(!strcmp(failure_body,"Writing destination (-28)\nNot enough free space."));
 assert(strstr(failure_detail,"BOOT.ELF") && strstr(failure_detail,"usb0: -> mc0:") && strstr(failure_detail,"fixture-head"));
 assert(!strchr(failure_detail,'\n'));
 strcpy(fb.msg,"Invalid PSU file");showPasteFailure(1);
 assert(!strcmp(failure_title,"PSU failed") && !strcmp(failure_body,"Invalid PSU file") && !failure_detail[0]);
 dashFileLoadingBegin("Pasting file");
 /* The filename must be published immediately, before the first blocking IO. */
 int before=presents;
 assert(DashFileProgress("Pasting file","BOOT.ELF",64ULL<<20,64ULL<<20));
 assert(presents==before+1);
 capture("Copy starting");
 assert(texts==3 && !strcmp(lines[0],"Pasting file"));
 assert(!strcmp(lines[1],"BOOT.ELF") && widths[1]==432);
 assert(!strcmp(lines[2],"Remaining size: 64.00 MB") && !spinners && fill==0);
 tick+=20;DashFileProgress("Pasting file","BOOT.ELF",32ULL<<20,64ULL<<20);
 float first=loading_fraction;capture("Copy halfway");
 assert(loading_fraction>first && loading_fraction<0.5f && fill>0 && fill<track);
 tick+=20;DashFileProgress("Pasting file","BOOT.ELF",0,64ULL<<20);
 capture("Copy complete");assert(fill==track && loading_fraction==1);
 /* A new same-named file is a new operation even without a phase change. */
 tick+=1;DashFileProgress("Pasting file","BOOT.ELF",64ULL<<20,64ULL<<20);
 assert(loading_fraction==0);
 char long_name[256];memset(long_name,'X',255);long_name[255]=0;
 DashFileProgress("Verifying file",long_name,1024,1024);capture("Long filename");
 assert(texts==3 && !spinners && !strcmp(lines[1],loading_status) && strlen(lines[1])==sizeof(loading_status)-1 && widths[1]==432);
 dashFileLoadingBegin("Deleting file");
 dashLoadingFrame("folder",0,2);tick+=20;DashFileDeleteProgress("nested.bin");
 capture("Deleting selected items");
 assert(texts==3 && !strcmp(lines[0],"Deleting file") && !strcmp(lines[1],"nested.bin"));
 assert(!strcmp(lines[2],"Completed items: 0 / 2") && !spinners);
 dashLoadingFrame("folder",1,2);capture("Deletion halfway");assert(fill>0 && fill<track);
 dashLoadingFrame("second.bin",2,2);capture("Deletion complete");assert(fill==track);
 loading_cancel=1;capture("Cancelable transfer");
 dashLoadingEnd();assert(!loading_active && !loading_file_operation);
 /* One-shot device status must never inherit the file-operation layout. */
 loading_file_operation=1;tick+=40;dashLoadingStatus("USB",0,0);
 assert(!loading_active && !loading_file_operation && spinners==1);
 dashLoadingBegin("Starting device");capture("Generic device loading");
 assert(!loading_file_operation && spinners==1 && texts==2);
 int old=presents;active=0;assert(!DashFileProgress("Pasting file","classic",1,1));
 active=1;worker=1;assert(!DashFileProgress("Pasting file","worker",1,1));
 DashFileDeleteProgress("worker");assert(old==presents);
 puts("PASS: compact copy/delete rendering, immediate filename, full completion, bounded fill and one scene tick");
 return 0;
}
"""
cc=shutil.which("gcc") or shutil.which("cc")
assert cc
with tempfile.TemporaryDirectory(prefix="file-overlay-") as folder:
    path=Path(folder);(path/"test.c").write_text("#define localizeDashboardText(s) (s)\n#define localizeDashboardFormat(s) (s)\n" + code)
    exe=path/"test.exe"
    subprocess.run([cc,"-std=c99","-Wall","-Wextra","-Werror",str(path/"test.c"),"-lm","-o",str(exe)],check=True)
    result=subprocess.run([str(exe),*( ["preview"] if len(sys.argv)>1 else [])],capture_output=True,text=True,check=False)
    assert result.returncode==0,result.stdout+result.stderr
print(result.stdout.splitlines()[-1])
if len(sys.argv)>1:
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    cards=[];svg=[];title=""
    def finish():
        if svg: cards.append("<figure><figcaption>"+html.escape(title)+"</figcaption><svg viewBox='0 0 640 448'>"+"".join(svg)+"</svg></figure>")
    for line in result.stdout.splitlines():
        parts=line.split("|")
        if parts[0]=="CASE":
            finish();title=parts[1];svg=["<rect width='640' height='448' fill='#101829'/>"]
        elif parts[0]=="BOX":
            x,y,w,h=parts[1:];svg.append(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='16' fill='#243149'/>")
        elif parts[0]=="TEXT":
            x,y,w,fs,text=parts[1:];size=32*float(fs)
            svg.append(f"<svg x='{x}' y='{y}' width='{w}' height='{size+4}'><text y='{size*0.8}' fill='#f0f3ff' font-size='{size}' font-family='sans-serif'>{html.escape(text)}</text></svg>")
        elif parts[0]=="BAR":
            x,y,w,h,n=parts[1:];color="#8bbcff" if n=="2" else "#53617a"
            svg.append(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='4' fill='{color}'/>")
    finish()
    (out/"index.html").write_text("<!doctype html><meta charset='utf-8'><title>Transfer overlay host draw capture</title><style>body{font:16px sans-serif;background:#0d1320;color:#eee;margin:24px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(400px,1fr));gap:16px}figure{margin:0}svg{width:100%}figcaption{padding:12px}</style><h1>Transfer overlay: host draw-call capture</h1><p>Production drawing coordinates and labels, with mocked fonts/colors/background. This is not a console screenshot or hardware acceptance.</p><main>"+"".join(cards)+"</main>")
    print("Preview:",out/"index.html")
