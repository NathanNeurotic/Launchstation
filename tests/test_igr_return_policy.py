"""Guard automatic IGR backup and post-IGR routing policy."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
hdr = (root / "include/dash_internal.h").read_text()
store = (root / "src/dash_store.c").read_text()
settings = (root / "src/dash_settings.c").read_text()
main = (root / "src/main.c").read_text()

for token in (
    "DASH_IGR_AFTER_DASHBOARD",
    "DASH_IGR_AFTER_OSDSYS",
    "DASH_IGR_AFTER_APP",
    "igr_auto_backup",
    "igr_after_path",
):
    assert token in hdr

assert "dashCfg.igr_auto_backup = 1;" in store
assert "dashCfg.igr_after = DASH_IGR_AFTER_DASHBOARD;" in store
for key in ("Dash_IGRAutoBackup", "Dash_IGRAfter", "Dash_IGRAfterPath"):
    assert key in store

for control in ("S_IGR_AUTO_BACKUP", "S_IGR_AFTER", "S_IGR_AFTER_APP"):
    assert control in settings
assert '"After IGR, open"' in settings
assert '"Automatic IGR backup"' in settings
assert "dashLibFind(ps.field)" in settings, "selected app should display its library title"
assert "dashTextFit" in settings, "long values must remain width-bounded"

backup = main[main.index("static int runWleIgrBackup("):main.index("static void runWleIgrAfter(")]
assert "if (!wleIgrRequested(argc, argv, &backup_root))" in backup
assert "if (!dashCfg.igr_auto_backup)" in backup
assert "mmceBackupAllCards" in backup

after = main[main.index("static void runWleIgrAfter("):main.index("//---------------------------------------------------------------------------\n//executable code")]
assert "if (!requested)" in after, "normal boots must never use IGR return routing"
assert "DASH_IGR_AFTER_OSDSYS" in after and "Execute(setting->Misc_OSDSYS)" in after
assert "DASH_IGR_AFTER_APP" in after
assert "ensurePathDeviceStackReady(dashCfg.igr_after_path)" in after
assert "Execute(dashCfg.igr_after_path)" in after

call = main.index("igr_requested = runWleIgrBackup(argc, argv);")
route = main.index("runWleIgrAfter(igr_requested);", call)
dash = main.index("DashRun(&dash_ctx, 1);", route)
assert call < route < dash, "IGR backup and routing must complete before dashboard entry"

print("PASS: IGR backup can be disabled independently and post-IGR routing is persisted, IGR-only, and device-aware")
