"""Regression checks for the reusable MMCE IGR auto-backup path."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
dash = (root / "src/dash_browser.c").read_text()
backup = (root / "src/mmce_backup.c").read_text()
control = (root / "src/mmce_control.c").read_text()
igr = (root / "src/mmce_igr.c").read_text()
scan = (root / "src/mmce_igr_scan.c").read_text()

# Dashboard must be UI-only; history mechanics belong to the reusable backend.
assert '#include "mmce_backup.h"' in dash
assert "activeHistoryPrune(" not in dash
assert "mmceBackupActiveHistory(" in dash
assert "mmceExportActivePcsx2ToFolder(" in dash

# -igr runs after early boot/device/config initialization but before normal runtime input.
assert "mmceIgrRequested(argc, argv)" in main
assert main.index("mmceIgrRequested(argc, argv)") < main.index("initializeRuntimeInputModules(boot)")
assert "bringUpBootDeviceStack(boot);" in main[main.index("mmceIgrRequested(argc, argv)"):]

# BootCard restore is bounded and only sends 0x30 when card index 0 is active.
restore = control[control.index("int mmceControlRestoreFromBootCard"):control.index("\n}", control.index("int mmceControlRestoreFromBootCard")) + 2]
assert "card = mmceControlGetCard(unit);" in restore
assert "if (card != 0)" in restore
assert "MMCE_CMD_UNMOUNT_BOOTCARD" in restore
assert "mmceControlWaitIdle(unit, 30, 250)" in restore

# A real BootCard switch uses both paths: scan prior changed images and then
# back up the exact restored active image. A failed unmount must not back up
# the still-active BootCard as if it were the game card.
assert "if (restore > 0)" in igr
assert "mmceIgrScanChangedCards(unit, folder, &scan)" in igr
assert "else if (restore < 0)" in igr
failed = igr[igr.index("else if (restore < 0)"):igr.index("continue;", igr.index("else if (restore < 0)")) + len("continue;")]
assert "mmceBackupActiveHistory" not in failed
assert "mmceBackupActiveHistory(unit, folder" in igr

# First scan has fail-safe semantics: no/corrupt state produces zero previous
# records, therefore every discovered card is changed and backed up. Failed
# first-seen cards are omitted from saved state so the next IGR retries them.
assert 'return 0; /* Corrupt/missing state => safe first-run behavior. */' in scan
assert "return now->size != old->size || memcmp(now->mtime" in scan
assert "current_cards[i].path[0] = '\\0';" in scan
assert "mmceBackupCardFileHistory(" in scan
for variant in ('"PS2"', '"COH"', '"SC2"', '"PROT"'):
    assert variant in scan

# Device readiness is boolean in this codebase.
assert "if (!ensurePathDeviceStackReady(root))" in backup
assert "if (ensurePathDeviceStackReady(root) < 0)" not in backup

print("PASS: MMCE IGR uses reusable verified backups, BootCard-safe restore, changed-card fallback, and boot-stack restoration")
