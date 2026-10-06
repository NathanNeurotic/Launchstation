from pathlib import Path
root=Path(__file__).resolve().parents[1]
hdr=(root/"include/dash_internal.h").read_text()
store=(root/"src/dash_store.c").read_text()
settings=(root/"src/dash_settings.c").read_text()
main=(root/"src/dash_main.c").read_text()
ui=(root/"src/dash_ui.c").read_text()
files=(root/"src/dash_files.c").read_text()
devices=(root/"src/dash_devices.c").read_text()
home=(root/"src/dash_home.c").read_text()

for field in ("files_mode","home_shortcuts","icon_backing","show_unavailable_devices"):
    assert field in hdr
for key in ("Dash_FilesMode","Dash_HomeShortcuts","Dash_IconBacking","Dash_ShowUnavailableDevices"):
    assert key in store

assert "visible_sections[] = {DASH_SEC_HOME, DASH_SEC_APPS, DASH_SEC_FILES, DASH_SEC_TOOLS, DASH_SEC_SETTINGS}" in main
assert "dashAdjacentSection(section, -1)" in main and "dashAdjacentSection(section, 1)" in main
assert "dashVisibleSectionCount()" in ui and "tab_w * vcount" in ui

assert '"File Browser", "Open the canonical wLaunchELF File Browser"' in files
assert "dashCfg.files_mode = !dashCfg.files_mode" in files
assert "const DashScreen dashFilesScreen = {storageEnter" in files
assert "state != DASH_DEV_ABSENT" in files
assert "USB / MASS" in devices
assert "Not connected" in devices
assert "out[i].state == DASH_DEV_READY" in devices

assert "Panel transparency" in settings
assert "Control icon backings" in settings
assert "Home quick-launch shortcuts" in settings
assert "Show unavailable devices" in settings
assert "dashCfg.icon_backing" in ui and "dashCfg.icon_backing" in home
assert "dashCfg.home_shortcuts" in home
print("zack structural feedback contract: OK")

clock = (root / "src/dash_clock_sync.c").read_text()
init = (root / "src/init.c").read_text()
yaml = (root / "ps2.yaml").read_text()
make = (root / "Makefile").read_text()

assert "Sync PS2 clock from Internet" in settings
assert "dashClockSyncInternet" in settings
assert "time.google.com" in clock and "time.cloudflare.com" in clock and "pool.ntp.org" in clock
assert "diff <= 10" in clock
assert "sceCdWriteClock" in clock and "sceCdReadClock" in clock
assert "9ULL * 3600ULL" in clock
assert "toBcd" in clock
assert "ensureDashboardIpNetworkReady" in init
assert "src/dash_clock_sync.c" in yaml
assert "ps2ips, cglue" in yaml
assert "dash_clock_sync.o" in make
print("clock sync contract: OK")
