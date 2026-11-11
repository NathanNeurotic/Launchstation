#!/usr/bin/env python3
"""Layout contract for Settings > System, the clock popup and the Neutrino popup."""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1]
settings = (root / "src/dash_settings.c").read_text()
neutrino = (root / "src/gui_neutrino.c").read_text()

table = settings[settings.index("static const SetItem system_items[] = {"):]
table = table[:table.index("};")]
labels = re.findall(r'^\s*\{ST_[A-Z]+, [^,]+, "([^"]+)"', table, re.M)
assert labels == [
    "Language",
    "Memory card backup files",
    "Set PS2 Clock",
    "PS2 Temperature Units (50K and later)",
    "Configuration folder",
    "Save launch & tool settings to...",
    "Classic screen settings",
    "Show welcome message again",
    "Check for updates",
    "About Launchstation",
], labels

popup = settings[settings.index("case S_CLOCK: {"):]
popup = popup[:popup.index("case S_SAVE_CLASSIC:")]
for label in ("Set manually...", "Sync from Internet", "Set software clock...", "Use PS2 hardware clock"):
    assert '"%s"' % label in popup, label
actions = re.findall(r"^\s*(S_CLOCK_[A-Z]+),$", popup, re.M)
assert actions == ["S_CLOCK_MANUAL", "S_CLOCK_SYNC", "S_CLOCK_SOFTWARE", "S_CLOCK_HARDWARE"], actions
assert popup.count("#ifdef ETH") == 2
assert 'ST_ACTION, S_CLOCK_SYNC, "Sync PS2 clock from Internet"' in settings
assert "it->id == S_SAVE_AS ? DI_SAVE" in settings and "it->id == S_CLOCK_SYNC) ? DI_CLOCK" in settings

show = neutrino[neutrino.index("static int showDashboardNeutrinoOptions"):]
show = show[:show.index("static void drawNeutrinoHeading")]
assert show.index("dashSoundMaintain();") < show.index('items[0].label = "Run game";')
assert "items[NEUTRINO_RUN]" not in show and "items[NEUTRINO_CANCEL].icon = DI_SAVE;" in show
assert "items, cancel_index + 1, 0) == 0;" in show
assert "const int option = row - 1;" in neutrino and "if (row == 0)" in neutrino
print("system clock menu contract: OK")
