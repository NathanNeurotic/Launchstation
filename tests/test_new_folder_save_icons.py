"""New folder... uses a folder-with-plus icon and Save rows use a floppy (B1, suggested by R3Z3N).

Both glyphs live in the atlas margin, which has room for exactly 13 small icons. The test checks
that capacity, renders the new glyphs on the host the way initClipboardIcons bakes them, and
checks every row that should use them."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
hdr = (root / "include/dash_internal.h").read_text(encoding="utf-8")
icons = (root / "src/dash_icons.c").read_text(encoding="utf-8")

# the enum: both new icons sit after the existing margin icons, before DI_COUNT
enum = hdr[hdr.rfind("enum", 0, hdr.index("DI_HOME = 0,")):]
enum = enum[:enum.index("};") + 2]
names = [m.group(1) for m in re.finditer(r"^\s*(DI_[A-Z0-9_]+)", enum, re.M)]
assert names[-3:] == ["DI_NEW_FOLDER", "DI_SAVE", "DI_COUNT"], names[-4:]

# atlas margin capacity, with the constants from dash_icons.c
const = {k: int(v) for k, v in re.findall(r"#define (ICON_TEX_H|APP_PART_SIZE) (\d+)", icons)}
slot = const["APP_PART_SIZE"] + 4
margin = names.index("DI_COUNT") - names.index("DI_CUT")
assert 2 * slot + margin * slot <= const["ICON_TEX_H"], margin
assert "CLIP_ICON_Y + (DI_COUNT - DI_CUT) * APP_PART_SLOT <= ICON_TEX_H" in icons   # compile-time check kept

# render iconSdf on the host exactly as the atlas does (32x32 margin tile)
body = icons[icons.index("typedef struct\n{\n\tfloat x, y;\n} V2;"):icons.index("static inline int clutSwizzle")]
src = r"""#include <math.h>
#include <stdio.h>
typedef unsigned char u8; typedef unsigned int u32;
static float dashClampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
""" + enum + "\n" + body + r"""
static void tile(int id, unsigned char *out) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            V2 p = {(x + 0.5f) / 16.0f - 1.0f, (y + 0.5f) / 16.0f - 1.0f};
            out[y * 32 + x] = (unsigned char)(dashClampf(0.5f - iconSdf(id, p) * 16.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
}
int main(void) {
    int ids[] = {DI_FOLDER, DI_NEW_FOLDER, DI_SAVE, DI_CHECK};
    unsigned char t[32 * 32];
    for (int i = 0; i < 4; i++) { tile(ids[i], t); fwrite(t, 1, sizeof(t), stdout); }
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "icons.c"
    exe = Path(tmp) / "icons"
    c.write_text(src, encoding="utf-8")
    subprocess.run(["gcc", "-O1", "-w", "-fsanitize=address,undefined", "-o", str(exe), str(c), "-lm"], check=True)
    raw = subprocess.run([str(exe)], check=True, capture_output=True).stdout
assert len(raw) == 4 * 1024
folder, new_folder, save, check = (raw[i * 1024:(i + 1) * 1024] for i in range(4))

def ink(t, x0=0, y0=0, x1=32, y1=32):
    return sum(t[y * 32 + x] > 127 for y in range(y0, y1) for x in range(x0, x1))

assert ink(save) > 150 and ink(new_folder) > 150                       # real glyphs, not blank tiles
assert new_folder != folder and save != check                          # distinct from what they replace
# the plus badge: the folder's upper-right corner is empty, the new-folder one is drawn
assert ink(folder, 22, 0, 32, 8) == 0 and ink(new_folder, 22, 0, 32, 8) > 15
# the floppy: a clipped upper-right corner, a filled shutter at the top, an open label below
assert save[2 * 32 + 30] < 64                                           # clipped corner stays empty
assert ink(save, 10, 3, 18, 8) >= 30                                    # shutter is solid
assert ink(save, 12, 19, 20, 25) <= 4                                   # inside the label is hollow

# where they are used
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
editor = (root / "src/dash_editor.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
assert 'ADD(DI_NEW_FOLDER, "New folder...", NULL, !en[NEWDIR], F_NEWDIR);' in browser
assert '{DI_SAVE, "Save", NULL, 0},' in editor and 'ADD(DI_SAVE, "Save", d->dirty ? "unsaved" : NULL, 0, M_SAVE);' in editor
assert '{DI_SAVE, "Save changes", NULL, 0},' in settings
assert '{DI_SAVE, !changed && failed ? "Retry failed saves"' in settings
print("PASS: New folder and Save rows draw their own folder-plus and floppy icons, inside the atlas margin")
