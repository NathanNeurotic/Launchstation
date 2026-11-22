"""L3 background preview: scene selection and ordinary-input dispatch contracts."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
themes = (root / "src/dash_themes.c").read_text()
header = (root / "include/dash_internal.h").read_text()


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
#include <stdio.h>
"""
code += header[header.index("enum DashBgKind"):header.index("enum DashHighlight")]
code += themes[themes.index("enum {\n\tP_OSD"):themes.index("/* Preset for a fresh install")]
code += function(themes, "int dashThemeNextBackground(")
code += function(themes, "int dashThemeBackgroundPreset(")
code += r"""
int main(void) {
    static const int palettes[DASH_BG_COUNT] = {
        P_BLACK, P_BIOSBLUE, P_OSD, P_MIST, P_TOWERS, P_DISC, P_MINIMAL,
        P_CLOCKRING, P_MCGRID, P_BOOTLINES, P_CUBES, P_STREAM, P_MCBLUE,
        P_BRUSHED, P_PSX, P_CRYSTAL, P_OSD, P_MIST, P_CRYSTAL
    };
    for (int current = -1; current <= DASH_BG_COUNT; current++) {
        int next = dashThemeNextBackground(current);
        assert(next >= DASH_BG_TUNNEL && next <= DASH_BG_CLOUDS);
    }
    assert(dashThemeNextBackground(DASH_BG_SOLID) == DASH_BG_TUNNEL);
    assert(dashThemeNextBackground(DASH_BG_GRADIENT) == DASH_BG_TUNNEL);
    assert(dashThemeNextBackground(DASH_BG_IMAGE) == DASH_BG_TUNNEL);
    for (int current = DASH_BG_TUNNEL; current <= DASH_BG_CLOUDS; current++) {
        int expected = current == DASH_BG_CLOUDS ? DASH_BG_TUNNEL : current + 1;
        assert(dashThemeNextBackground(current) == expected);
    }
    for (int kind = 0; kind < DASH_BG_COUNT; kind++)
        assert(dashThemeBackgroundPreset(kind) == palettes[kind]);
    puts("PASS: L3 preview cycles animated scenes and routes each to its preset palette");
    return 0;
}
"""
compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
assert compiler, "a host C compiler is needed"
with tempfile.TemporaryDirectory(prefix="l3-background-") as tmp:
    source = Path(tmp) / "preview.c"
    exe = Path(tmp) / "preview"
    source.write_text(code)
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
