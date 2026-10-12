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
