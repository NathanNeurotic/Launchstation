"""Controller hint glyphs: START must look like START, SELECT like SELECT,
and hints may only name real PS2 buttons.

Regressions: "Menu" (START) once drew the SELECT bar, and the Applications
screen showed a "BACK" badge, which is not a PS2 button.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
icons = (root / "src/dash_icons.c").read_text(encoding="utf-8")
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")


def case(name):
    start = icons.index(f"case {name}:")
    return icons[start:icons.index("break;", start)]


# START is the right-pointing wedge, SELECT the flat rounded bar.
assert "sdTriP(" in case("DI_BTN_START") and "sdBoxOutline(" not in case("DI_BTN_START")
assert "sdBoxOutline(" in case("DI_BTN_SELECT") and "sdTriP(" not in case("DI_BTN_SELECT")
key_icon = ui[ui.index("static int keyIcon("):ui.index("static float buttonWidth(")]
assert 'if (!strcmp(key, "START"))\n\t\treturn DI_BTN_START;' in key_icon
assert 'if (!strcmp(key, "SELECT"))\n\t\treturn DI_BTN_SELECT;' in key_icon
print("PASS: START/SELECT hint keys map to their own glyphs")

# Hint key badges must name real PS2 controller buttons ("BACK" is not one;
# the back action uses dashBackIcon(), i.e. Circle or Cross).
allowed = {"START", "SELECT", "L1", "L2", "L3", "R1", "R2", "R3", "L1/R1", "L2/R2"}
for path in sorted((root / "src").glob("*.c")):
    for key in re.findall(r'\.key = "([^"]+)"', path.read_text(encoding="utf-8", errors="replace")):
        assert key in allowed, f"{path.name}: hint key {key!r} is not a PS2 button"
print("PASS: hint key badges name real PS2 buttons")

# Controller glyphs snap to whole GS pixels; the texture pool rejects
# textures it can never fit instead of looping in gsKit's evictor.
gfx = (root / "src" / "dash_gfx.c").read_text(encoding="utf-8")
icons = (root / "src" / "dash_icons.c").read_text(encoding="utf-8")
assert "void dashSpriteBatchAddAligned(" in gfx and "floorf(SX(x1) + 0.5f) - 0.5f" in gfx
assert "dashSpriteBatchAddAligned(x + go, y + go," in icons
bind = gfx[gfx.index("int dashTexBind("):]
assert bind.index("size > available") < bind.index("gsKit_TexManager_bind")
gs = (root / "src" / "draw_gs.c").read_text(encoding="utf-8")
reset = gs[gs.index("fadeScreenToBlack();"):gs.index("setGS_flag = 1;")]
assert reset.index("gsKit_queue_exec(gsGlobal);") < reset.index("dmaKit_wait_fast();") < reset.index("gsKit_finish();")
print("PASS: aligned pad glyphs, bounded texture binds, GS drained before mode reset")
