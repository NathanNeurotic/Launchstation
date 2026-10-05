from pathlib import Path

root = Path(__file__).resolve().parents[1]

modules = (root / "src/main_modules.c").read_text()
store = (root / "src/dash_store.c").read_text()
devices = (root / "src/dash_devices.c").read_text()
settings = (root / "src/dash_settings.c").read_text()
main = (root / "src/main.c").read_text()
header = (root / "include/launchelf.h").read_text()

# SELECT remains the legacy "show all devices" boot modifier and now also
# requests a recovery boot before the dashboard persistence layer initializes.
assert "new_pad & PAD_SELECT" in modules
assert "boot_show_all_devices = 1;" in modules
assert "boot_safe_mode = 1;" in modules
assert "u8 boot_safe_mode = 0;" in main
assert "extern u8 boot_safe_mode;" in header

# Safe mode locates the normal settings folder but deliberately does not parse
# DASHBOARD.CNF, so saved auto-start/device/theme choices cannot trap startup.
assert "if (!boot_safe_mode)" in store
safe = store[store.index("void dashStoreInit(void)"):store.index("int dashStoreLoaded(void)")]
assert "dashKvParse(text, settingsKv, &scan_reset);" in safe
assert safe.index("if (!boot_safe_mode)") < safe.index("dashKvParse(text, settingsKv, &scan_reset);")

# Old/corrupt configs with both mutually-exclusive SIO2 stacks are normalized
# before persisted settings are captured.
assert "DASH_DEVA_MMCE | DASH_DEVA_MX4SIO" in store
assert "dashBootDeviceBit() == DASH_DEVA_MX4SIO" in store
assert "dashCfg.dev_auto &= ~DASH_DEVA_MMCE;" in store
assert "dashCfg.dev_auto &= ~DASH_DEVA_MX4SIO;" in store
assert store.index("DASH_DEVA_MMCE | DASH_DEVA_MX4SIO") < store.index("persisted = dashCfg;")

# Runtime auto-start has its own guard so future callers cannot accidentally
# walk MMCE and MX4SIO in one automatic startup pass.
assert "static int safeAutoStartMask(void)" in devices
auto = devices[devices.index("static int safeAutoStartMask(void)"):]
assert "const int mask = safeAutoStartMask();" in auto
probe = devices[devices.index("void dashDevicesAutoProbe(void)"):]
assert "const int mask = safeAutoStartMask();" in probe
assert "if ((mask & DASH_DEVA_MMCE)" in probe
assert "if ((mask & DASH_DEVA_MX4SIO)" in probe

# Settings enforces the same rule for new configurations and refuses to switch
# away from an MMCE/MX4SIO device that is currently the boot device.
assert "bit == DASH_DEVA_MMCE && boot_bit == DASH_DEVA_MX4SIO" in settings
assert "bit == DASH_DEVA_MX4SIO && boot_bit == DASH_DEVA_MMCE" in settings
assert "dashCfg.dev_auto &= ~DASH_DEVA_MX4SIO;" in settings
assert "dashCfg.dev_auto &= ~DASH_DEVA_MMCE;" in settings

print("device auto-start / SELECT safe-mode policy OK")
