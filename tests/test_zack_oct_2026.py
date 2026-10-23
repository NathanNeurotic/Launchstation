"""Regression contracts for the October 2026 zackcage6 feedback batch.

These source/build checks complement the PS2 compilation and other host tests;
they cannot replace real interlaced CRT or card-slot hardware validation.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
def read(path):
    return (root / path).read_text()

icons = read("src/dash_icons.c")
ui = read("src/dash_ui.c")
shell = read("src/dash_main.c")
gfx = read("src/dash_gfx.c")
info = read("src/dash_info.c")
devices = read("src/dash_devices.c")
make = read("Makefile")
embed = read("embed.make")
blobs = read("src/embed_blobs.S")
header = read("include/dash_internal.h")

# Eight subtitled menu rows previously left just three pixels at the top.
rows = int(re.search(r"const int max_rows = ([0-9]+);", ui).group(1))
tallest_menu = 76 + rows * 38 + 44 + 18
assert (448 - tallest_menu) / 2 >= 20, (rows, tallest_menu)
assert "dashGfxScissor(" in ui and "dashGfxScissorReset()" in ui

# Pad tiles must work without a fixed navy PNG background, including the
# six L/R buttons used as direct quick-launch glyphs.
for name in ("CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "UP", "DOWN",
             "LEFT", "RIGHT", "START", "SELECT",
             "L1", "L2", "L3", "R1", "R2", "R3"):
    assert f"case DI_BTN_{name}:" in icons, name
assert "dashCfg.icon_backing" in icons
assert "if ((id > DI_PAD_BADGE && id <= DI_BTN_R3) || id == DI_DPAD_UD)" in icons
assert "DashColor badge = DC(18, 22, 30, 230)" in icons
assert "initPadTiles" not in icons and "pad_png_ready" not in icons
assert "dashIconsPadPng" not in ui + icons + header
assert "icon_cross_png.o" not in make
assert "icon_cross_png.s:" not in embed
assert "EMBED_FILE(icon_cross_png" not in blobs
assert "res/icons/cross.png" not in blobs + embed
assert "res/icons/cross.png" in {p.relative_to(root).as_posix() for p in (root / "res/icons").glob("*.png")}
# The original images remain in the repository as a reversible reference.

# Background-only is a temporary, HOME-only presentation state and cannot
# steal SELECT+R3 launch-key combinations.
assert "section == DASH_SEC_HOME && !ql_down" in shell
assert "!(paddata & PAD_SELECT)" in shell
assert "background_only = 0;" in shell
assert "if (background_only)" in shell

# Physical slot identity remains mapped to the port used for actual access.
assert 'snprintf(name, sizeof(name), "mc%d (Slot %d)", port, port + 1);' in devices
assert 'snprintf(mount, sizeof(mount), "mc%d:", port);' in devices
assert '"USB Mass Storage"' in devices
assert '"MASS (USB)"' not in devices   # issue #132 resolved

# Scrolling text and second focus outline are stable at interlaced cadence.
assert "2.0f * floorf(scroll * 0.5f + 0.5f)" in info  # even line pairs (#139)
assert "edge.a = (u8)(65 * strength);" in ui
assert "if (t < 2.0f)" in gfx

print("PASS: modal safe margins, all pad glyphs, reversible asset removal, "
      "R3 hotkey guard, port mapping and 4:3 rendering contracts")
