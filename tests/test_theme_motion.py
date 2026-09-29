"""Exercise the production motion / animation settings parsing without PS2 hardware.

Dash_Effects / Dash_Animations used to hold 0/1/2 (off, reduced, full); they
now hold 0..200 percent. Old files must load as 0/50/100 %, new values in
10 % steps must round-trip, and out-of-range values fall back to 100 %.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / "include/dash_internal.h").read_text()
store = (root / "src/dash_store.c").read_text()


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
typedef uint64_t u64;
#define stricmp strcasecmp
#define DASH_MAX_SCAN 16
#define DASH_PATH_MAX 1024
#define DASH_MAX_RECENT 16
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static int dashThemeStoreKey(const char *k,char *v) { (void)k; (void)v; return 0; }
"""
code += header[header.index("enum DashSection"):header.index("extern DashSettings dashCfg;")]
code += "DashSettings dashCfg;\n"
code += function(store, "static int clampInt(")
code += function(store, "static int percentSetting(")
code += function(store, "static int settingsKv(")
code += r"""
static void load(const char *effects, const char *anim) {
    int reset = 0;
    settingsKv("Dash_Effects", (char *)effects, &reset);
    settingsKv("Dash_Animations", (char *)anim, &reset);
}

int main(void) {
    char value[16];
    /* old enum files: off / reduced / full */
    load("0", "0"); assert(dashCfg.effects == 0 && dashCfg.anim == 0);
    load("1", "1"); assert(dashCfg.effects == 50 && dashCfg.anim == 50);
    load("2", "2"); assert(dashCfg.effects == 100 && dashCfg.anim == 100);
    /* percentages in the settings' 10 % steps */
    for (int pct = 10; pct <= 200; pct += 10) {
        sprintf(value, "%d", pct);
        load(value, value);
        assert(dashCfg.effects == pct && dashCfg.anim == pct);
    }
    /* out of range: the 100 % default */
    load("201", "-1"); assert(dashCfg.effects == 100 && dashCfg.anim == 100);
    load("", "");      assert(dashCfg.effects == 100 && dashCfg.anim == 100);
    puts("PASS: legacy 0/1/2 values, 10-200% steps, invalid values");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-motion-") as tmp:
    source = Path(tmp) / "motion.c"
    exe = Path(tmp) / "motion.exe"
    source.write_text(code)
    subprocess.run([compiler, "-std=c11", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
