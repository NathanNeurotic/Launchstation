from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / "src/dash_ui.c").read_text(encoding="utf-8")
browser = (root / "src/dash_browser.c").read_text(encoding="utf-8")
files = (root / "src/dash_files.c").read_text(encoding="utf-8")
devices = (root / "src/dash_devices.c").read_text(encoding="utf-8")
settings = (root / "src/dash_settings.c").read_text(encoding="utf-8")
editor = (root / "src/dash_editor.c").read_text(encoding="utf-8")
classic = (root / "src/filer_browser.c").read_text(encoding="utf-8")

# 54/74: seconds stay; layout changes instead of deleting precision.
assert "menuTitleFormatClockTime(time_text, sizeof(time_text), hour, minute, second, use_12h)" in classic
assert "(details_column + 9) * FONT_WIDTH" in classic
assert "explicit size/date-time gap" in browser

# 55/75: sort keys and direction are dashboard controls.
for label in ('"Name"', '"Size"', '"Date-Time"', '"Kind"'):
    assert label in browser
assert "fb_sort_desc" in browser
assert '"Sort direction"' in browser

# 56: the header selector no longer receives a high-frequency sinusoidal kick.
header = ui[ui.index("void dashDrawHeader"):ui.index("void dashDrawSectionTitle")]
assert "dashSinf(dashTime() * 16.0f)" not in header
assert "fabsf(target_x - tab_anim_x) < 0.25f" in header

# 76: scan scope/defaults are explained in the actual Settings UI.
assert "Only these folders are scanned" in settings
assert "mc:/APPS, mc:/BOOT, mass:/APPS and mass:/BOOT" in settings
assert "this folder + one subfolder" in settings

# 77/78: an unprobed APA/iLink device can be selected to invoke lazy loading.
selectable = files[files.index("static int devSelectable"):files.index("static void devSelectFirst")]
assert "DASH_DEV_PROBE" in selectable
assert '"Internal HDD (APA)"' in devices
assert '"iLink / FireWire"' in devices

# 81/82: path context/page movement are visible and end navigation remains.
assert "M_GOTO_END" in editor
assert '"L2/R2", "Page"' in editor
assert '"L1/R1", "Line"' in editor
assert '"Line start/end"' not in editor
assert "dashTextScroll(ED_X + 30, 42" in editor

print("Zack/FitthFox 35-82 accepted-delta contract: OK")
