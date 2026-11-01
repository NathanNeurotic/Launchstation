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
        dashTheme.panel.a = 73;
        dashThemeResetGroup(DASH_TG_STYLE);
        assert(dashTheme.tile_style == DASH_TILE_MEMORY_CARD);
        assert(dashTheme.panel.a == 73); /* Opacity belongs to Colours. */
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
    /* Font family and Regular/Bold style round-trip independently; Roboto
     * and System IDs retain their existing meanings. */
    const char *font_ids[]={"roboto","system","emotion_engine"};
    assert(DASH_FONT_ROBOTO==0 && DASH_FONT_SYSTEM==1 && DASH_FONT_COUNT==3);
    for(int font=0;font<DASH_FONT_COUNT;font++) for(int bold=0;bold<2;bold++) {
        dashTheme.font=font;dashTheme.bold=bold;dashThemeCommit();
        assert(dashThemeStoreWrite(buf,sizeof(buf))<(int)sizeof(buf));
        char expected[80];snprintf(expected,sizeof(expected),"Theme_font = %s",font_ids[font]);
        assert(strstr(buf,expected));
        snprintf(expected,sizeof(expected),"Theme_bold = %d",bold);assert(strstr(buf,expected));
        dashThemeStoreBegin();reload(buf,1);dashThemeStoreEnd();
        assert(dashTheme.font==font && dashTheme.bold==bold);
        assert(dashThemeSerialize(&dashTheme,buf,sizeof(buf),"")<(int)sizeof(buf));
        dashThemeLoadPreset(0,&dashTheme);reload(buf,0);dashThemeValidate(&dashTheme);
        assert(dashTheme.font==font && dashTheme.bold==bold);
        dashThemeParseKey(&dashTheme,"font","invalid");assert(dashTheme.font==font);
    }
    /* The old separate Bold choice takes precedence over its synthetic
     * weight flag regardless of key order, in both file formats. */
    const char *legacy_ids[]={"emotion_engine_bold","3","EMOTION_ENGINE_BOLD"};
    for(int id=0;id<3;id++) for(int order=0;order<2;order++) for(int store=0;store<2;store++) {
        if(store) { dashThemeStoreBegin();dashThemeStoreKey("Theme_custom","1"); }
        else dashThemeLoadPreset(0,&dashTheme);
        for(int key=0;key<2;key++) {
            int is_font=key==order;
            const char *name=store ? (is_font?"Theme_font":"Theme_bold") : (is_font?"font":"bold");
            char value[64];snprintf(value,sizeof(value),"%s",is_font?legacy_ids[id]:"0");
            assert(store?dashThemeStoreKey(name,value):dashThemeParseKey(&dashTheme,name,value));
        }
        if(store) dashThemeStoreEnd();else dashThemeValidate(&dashTheme);
        assert(dashTheme.font==DASH_FONT_EMOTION_ENGINE && dashTheme.bold==1);
        assert(dashThemeSerialize(&dashTheme,buf,sizeof(buf),"")<(int)sizeof(buf));
        assert(strstr(buf,"font = emotion_engine\r\n") && strstr(buf,"bold = 1\r\n"));
        assert(!strstr(buf,"emotion_engine_bold"));
        dashThemeLoadPreset(0,&dashTheme);reload(buf,0);dashThemeValidate(&dashTheme);
        assert(dashTheme.font==DASH_FONT_EMOTION_ENGINE && dashTheme.bold==1);
    }
    dashTheme.font=255;dashThemeValidate(&dashTheme);assert(dashTheme.font==DASH_FONT_ROBOTO);
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
