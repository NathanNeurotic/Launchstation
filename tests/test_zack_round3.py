"""Contracts for zackcage6's round-3 hardware feedback (Oct 13, 2026)."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = lambda f: (root / "src" / f).read_text(encoding="utf-8")
icons, tools, settings = src("dash_icons.c"), src("dash_tools.c"), src("dash_settings.c")
lib, devices, main = src("dash_lib.c"), src("dash_devices.c"), src("dash_main.c")
art, bg = src("dash_art.c"), src("dash_bg_ps2.c")


def case(text, name):
    start = text.index(f"case {name}:")
    return text[start:text.index("break;", start)]


# #152: face-button glyphs fill most of their badge with ~2 px strokes.
assert "dashCfg.icon_backing ? 0.74f : 0.86f" in icons
assert "0.17f" in case(icons, "DI_BTN_CROSS") and "0.16f" in case(icons, "DI_BTN_CIRCLE")
assert "0.15f" in case(icons, "DI_BTN_SQUARE") and "0.15f" in case(icons, "DI_BTN_TRIANGLE")
# #127: the scroll D-pad is solid; hairline outlines flickered on interlaced TVs.
assert "sdBoxOutline" not in case(icons, "DI_DPAD_UD")

# #151: leaving Launchstation uses the red (danger) confirmation, like Power off.
for name in ("PS2 system browser", "OSDSYS", "Reboot IOP", "Power off"):
    line = next(l for l in tools.splitlines() if f'"{name}",' in l and "TA_MISC" in l)
    assert re.search(r", 2, MISC\(", line), name

# #135: the breadcrumb clears the L1/R1 bar (ends at y=62).
assert "dashTextFit(ix, 66, iw - 126, trail" in settings
assert "dashDrawSectionTitle(ix, 82, localizeDashboardText(c->name)" in settings

# #141: HOME names memory cards like the Files tab.
assert '"mc0 (Slot 1)"' in lib and '"mc1 (Slot 2)"' in lib
assert '"Memory Card 1"' not in lib

# #138: boot auto-start hides per-driver messages but keeps the overlay.
auto = devices[devices.index("void dashDevicesAutoStart(void)"):]
auto = auto[:auto.index("\n}\n")]
assert "drawMsgOverride = quietDriverMsg;" in auto and "drawMsgOverride = prev;" in auto

# #137: background-only mode still shows how to bring the UI back.
assert '{-1, "R3", "Show interface"}' in main

# #140: Continue tile focus outlines and icon bob land on whole pixels,
# line thickness survives alternating interlaced fields without 30Hz flicker.
focus = art[art.index("void dashDrawTileFocus("):art.index("static void tileBody(")]
assert "x = floorf(x + .5f);" in focus and "h = floorf(h + .5f);" in focus
assert "ay += floorf(bob + .5f) / h;" in art
assert "vx[i] = floorf(v[i][0] * w + .5f);" in art
assert "vy[i] = floorf(v[i][1] * h + .5f);" in art
assert "dashLine(x + vx[i], y + vy[i], x + vx[j], y + vy[j], 2.0f, top);" in art
draw_art = art[art.index("void dashDrawAppArt("):]
assert "x = floorf(x + .5f);" in draw_art and "h = floorf(h + .5f);" in draw_art

# #136: L1/R1 transition sound uses a rotating channel pool to avoid dropped notes on rapid input.
sound = src("dash_sound.c")
assert "#define SND_TRANS_CH_BASE 14" in sound
assert "#define SND_TRANS_CH_COUNT 4" in sound
assert "ch = SND_TRANS_CH_BASE + transSlot;" in sound
assert "transSlot = (transSlot + 1) % SND_TRANS_CH_COUNT;" in sound
# Fallback must preserve music voice 23; executable coverage lives in
# test_video_sound_preview.py.
assert "audsrv_ch_play_adpcm(-1, &samples[id]);" not in sound

# #133: the current-hour clock rod is gently lit, with no white fill block.
clock = bg[bg.index("void dashBgSceneClockRing("):bg.index("// Light streaks")]
assert "lit ? 56 : 34" in clock and "rf" not in clock

# #130: orb heads follow their trail's tint; no pure-white core.
orbs = bg[bg.index("static void osdOrbs(const OrbStyle *st, float t, int fx)\n{"):bg.index("static OrbStyle menuOrbStyle(")]
assert "DC(255, 255, 255, 255)" not in orbs

# #134: RSOD glass cube edges are feathered.
cube = bg[bg.index("static void glassCube("):bg.index("void dashBgSceneDisc(")]
assert "static const u8 edge[4][2]" in cube

print("PASS: zackcage6 round-3 UI contracts")
