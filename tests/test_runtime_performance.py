"""Guard the consolidated dashboard hot-path and lazy-USB recovery pass."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/dash_main.c").read_text()
bg = (root / "src/dash_bg.c").read_text()
ps2 = (root / "src/dash_bg_ps2.c").read_text()
geo = (root / "src/dash_bg_geo.c").read_text()
image = (root / "src/dash_image.c").read_text()
init = (root / "src/init.c").read_text()

assert "frame_counter" not in main
assert "next_device_poll_ms" in main and "now + 125" in main
assert "next_disc_poll_ms" in main and "now + 1000" in main
assert "next_remote_poll_ms" in main and "now + 250" in main
assert "idle_ms = dashInputIdleMs();" in main

scan = bg[bg.index("static void drawScanlines"):bg.index("// Entry points")]
assert "dashTexDrawUV(&scanline_tex" in scan
assert "GS_CMODE_REPEAT" in scan
assert "for (y =" not in scan
assert "bg_perf_reduced" in bg and "effectiveFx" in bg

assert "z = 38.0f * powf(1.32f, -s);" in ps2
assert "z *= 1.32f;" in ps2
assert "tw_cells[n].lum =" in ps2
assert "powf(c->h / 96.0f" not in ps2
assert "rotV(&v, cax, sax, cay, say);" in ps2
cubes = geo[geo.index("void dashBgSceneCubes"):geo.index("// Data stream")]
assert cubes.index("cx = dashCosf(ax);") < cubes.index("for (i = 0; i < 8; i++)")

assert "if (state->fullscreen) {\n\t\t\t/* Fullscreen is opaque." in image
assert "dashGfxBeginFrame();" in image
assert "} else {\n\t\t\tdashRenderScene();" in image

settle = init[init.index("static void waitForUsbMassBdmfsSettle(void)"):init.index("static int pathUsesUsbMass")]
assert "DelayThread(1000);" in settle
assert "while (Timer() < ready_time) {" not in settle
rebind = init[init.index("static int refreshPadSessionAfterUsb(void)"):init.index("void loadUsbModules(void)")]
assert "padEnd();" in rebind
assert "setupPad();" in rebind
assert "DelayThread(20000);" in rebind
assert "refreshPadSessionAfterUsb()" in init

print("PASS: hot paths are paced, background math is hoisted, fullscreen avoids overdraw, and lazy USB rebinds pad RPC")
