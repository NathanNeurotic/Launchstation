"""Exercise production config migration and clock phase without PS2 hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / "include/dash_internal.h").read_text()
store = (root / "src/dash_store.c").read_text()
ps2 = (root / "src/dash_bg_ps2.c").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
typedef uint64_t u64;
#define stricmp strcasecmp
#define DASH_MAX_SCAN 16
#define DASH_PATH_MAX 1024
#define DASH_MAX_RECENT 16
static float dashClampf(float x,float a,float b) { return x<a?a:x>b?b:x; }
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static int dashThemeStoreKey(const char *k,char *v) { return 0; }
"""
code += header[header.index("enum DashSection"):header.index("extern DashSettings dashCfg;")]
code += "DashSettings dashCfg;\n"
code += function(store, "static int clampInt(")
code += function(store, "static int settingsKv(")
code += function(ps2, "static float clockPhase(")
code += r"""
int main(void) {
    int reset=0;
    char value[16];
    for (int old=0;old<3;old++) {
        sprintf(value,"%d",old);
        settingsKv("Dash_Effects",value,&reset);
        settingsKv("Dash_Animations",value,&reset);
        assert(dashCfg.effects==old*50 && dashCfg.anim==old*50);
    }
    for (int pct=0;pct<=200;pct+=10) {
        sprintf(value,"%d",pct);
        settingsKv("Dash_MotionPercent",value,&reset);
        settingsKv("Dash_AnimationPercent",value,&reset);
        assert(dashCfg.effects==pct && dashCfg.anim==pct);
    }
    settingsKv("Dash_MotionPercent","201",&reset);
    settingsKv("Dash_AnimationPercent","-1",&reset);
    assert(dashCfg.effects==100 && dashCfg.anim==100);
    float prev=clockPhase(58,10000);
    for (int ms=16;ms<120000;ms+=16) {
        /* Delayed RTC observations, including repeated minute rollovers. */
        int second=(58+(ms>100 ? (ms-100)/1000 : 0))%60;
        float phase=clockPhase(second,10000+ms);
        float delta=fmodf(phase-prev+60,60);
        assert(delta>0 && delta<0.03f);
        prev=phase;
    }
    float resumed=clockPhase(25,200000);
    assert(fabsf(resumed-25)<0.001f);
    puts("PASS: legacy settings, 0-200% values, invalid bounds, fluid RTC rollover and resume");
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-motion-") as tmp:
    source = Path(tmp) / "motion.c"
    exe = Path(tmp) / "motion.exe"
    source.write_text(code)
    subprocess.run([compiler, "-std=c11", str(source), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)
