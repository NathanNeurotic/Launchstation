"""Host-check production theme persistence/defaults; no GS or console proof."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / "include/dash_internal.h").read_text()
theme = (root / "src/dash_themes.c").read_text()
gfx = (root / "src/dash_gfx.c").read_text()
art = (root / "src/dash_art.c").read_text()


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


stub = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
typedef unsigned char u8;
#define DASH_PATH_MAX 1024
#define stricmp strcasecmp
static float dashClampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
static void dashStrCopy(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
"""
color_start = header.rfind("typedef struct", 0, header.index("} DashColor;"))
stub += header[color_start:header.index("DashColor dashColorLerp")]
stub += header[header.index("enum DashBgKind"):header.index("extern DashTheme dashTheme;")]
group_start = header.index("enum DashThemeGroup")
stub += header[group_start:header.index("};", group_start) + 2]
stub += function(gfx, "DashColor dashColorLerp(")
stub += theme[theme.index("DashTheme dashTheme;"):theme.index("// Apply\n")].rsplit("//--------------------------------------------------------------", 1)[0]
stub += "void dashThemeApply(void) {}\nvoid dashThemeInit(void) {}\n"
stub += "void dashThemeCommit(void) { dashThemeValidate(&dashTheme); theme_custom = 1; }\n"
stub += theme[theme.index("static int hexNibble"):theme.index("// Theme folders\n")].rsplit("//--------------------------------------------------------------", 1)[0]
stub += function(theme, "void dashThemeResetGroup(")
stub += function(theme, "void dashThemeRestore(")
stub += function(art, "static int tileVertices(")
checks = r"""
static void reload(char *buf, int store) {
    char *line = strtok(buf, "\r\n");
    while (line) {
        char *eq = strstr(line, " = ");
        assert(eq);
        *eq = 0;
        if (store) dashThemeStoreKey(line, eq + 3);
        else dashThemeParseKey(&dashTheme, line, eq + 3);
        line = strtok(NULL, "\r\n");
    }
}
int main(void) {
    char buf[16384];
    DashTheme saved;
    for (int p = 0; p < dashThemePresetCount(); p++) {
        dashThemeLoadPreset(p, &dashTheme);
        assert(dashTheme.tile_style == DASH_TILE_MEMORY_CARD);
    }
    /* Old custom config, with no tile key, gets the new default. */
    dashThemeStoreBegin();
    dashThemeStoreKey("Theme_custom", "1");
    dashThemeStoreKey("Theme_corners", "square");
    dashThemeStoreEnd();
    assert(dashTheme.tile_style == DASH_TILE_MEMORY_CARD);
    assert(dashTheme.corner == DASH_CORNER_SQUARE);
    for (int style = 0; style < DASH_TILE_COUNT; style++) {
        dashTheme.tile_style = style;
        dashThemeCommit();
        saved = dashTheme;
        assert(dashThemeStoreWrite(buf, sizeof(buf)) < (int)sizeof(buf));
        assert(strstr(buf, "Theme_tile_style = "));
        dashThemeStoreBegin(); reload(buf, 1); dashThemeStoreEnd();
        assert(dashTheme.tile_style == style);
        assert(dashTheme.corner == saved.corner);
        assert(dashThemeSerialize(&dashTheme, buf, sizeof(buf), "") < (int)sizeof(buf));
        dashThemeLoadPreset(0, &dashTheme); reload(buf, 0);
        assert(dashTheme.tile_style == style);
        dashThemeParseKey(&dashTheme, "tile_style", "invalid");
        dashThemeParseKey(&dashTheme, "tile_style", "255");
        assert(dashTheme.tile_style == style);
        dashThemeResetGroup(DASH_TG_STYLE);
        assert(dashTheme.tile_style == DASH_TILE_MEMORY_CARD);
        dashThemeRestore(&saved, 0, 1);
        assert(dashTheme.tile_style == style);
        if (style == DASH_TILE_MEMORY_CARD || style == DASH_TILE_CHAMFERED || style == DASH_TILE_CRYSTAL) {
            float x[8],y[8]; int n=tileVertices(84,84,x,y);
            for (int i=0;i<n;i++) {
                int j=(i+1)%n,k=(i+2)%n;
                assert(x[i]>=0 && x[i]<=84 && y[i]>=0 && y[i]<=84);
                assert((x[j]-x[i])*(y[k]-y[j])-(y[j]-y[i])*(x[k]-x[j]) > 0);
            }
        }
    }
    dashTheme.tile_style = 255; dashThemeValidate(&dashTheme);
    assert(dashTheme.tile_style == DASH_TILE_MEMORY_CARD);
    puts("PASS: all preset defaults, old config, every style round-trip, invalid values, reset/restore, convex silhouettes");
}
"""
compiler = shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("Host gcc or clang required")
with tempfile.TemporaryDirectory(prefix="wle-tile-") as tmp:
    c = Path(tmp) / "tiles.c"
    exe = Path(tmp) / "tiles.exe"
    c.write_text(stub + checks)
    subprocess.run([compiler, "-std=c11", str(c), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)
