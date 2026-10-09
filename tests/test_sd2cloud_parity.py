from pathlib import Path

root = Path(__file__).resolve().parents[1]
settings = (root/"src/dash_settings.c").read_text()
tools = (root/"src/dash_tools.c").read_text()
header = (root/"include/dash_internal.h").read_text()
workflow = (root/".github/workflows/compile.yml").read_text()

for label in (
    '"Sync all cards"', '"Automatic sync"', '"IGR helper"',
    '"After IGR, open"', '"Language"', '"Backups per card"',
    '"Backup format"', '"Google account"', '"Check for updates"',
    '"About Launchstation"'
):
    assert label in settings

assert 'igr_sync_names[] = {"Off", "Local backup", "Local + Google Drive"}' in settings
assert "dashToolsSyncAllMmceCards();" in settings
assert "dashToolsIgrBackupSetup();" in settings
assert "dashToolsCloudBackup();" in settings
assert "dashToolsCheckUpdates();" in settings
assert "dashToolsPrepareMmceStatus();" in settings
assert "dashToolsMmceStatus()" in settings
assert "dashToolsGoogleStatus()" in settings
assert "dashToolsIgrStatus()" in settings
assert '"Current"' in tools
assert '"Outdated"' in tools
assert '"Not installed"' in tools
assert '"Partial / mixed"' in tools
assert '"All synced"' in tools
assert '"%d pending"' in tools
assert '"Checking..."' in tools
assert "void dashToolsPrepareMmceStatus(void)" in tools
assert 'loadMmceModules()' in tools
assert '"Starting MMCE..."' in tools
assert '"MMCE could not be started"' in tools

assert "void dashToolsSyncAllMmceCards(void)" in tools
assert "mmceBackupAllCards" in tools
assert "cloudGoogleSyncBackupRoot" in tools
assert "/repos/NathanNeurotic/Launchstation/releases/tags/latest" in tools
assert '"WLE-UPDATE-CA.PEM"' in tools
assert "releaseJsonString" in tools

assert "dashToolsSyncAllMmceCards" in header
assert "dashToolsPrepareMmceStatus" in header
assert "dashToolsCheckUpdates" in header
assert (root / "res/WLE-UPDATE-CA.PEM").stat().st_size <= 262144

pem = (root/"res/WLE-UPDATE-CA.PEM").read_text()
assert "BEGIN CERTIFICATE" in pem
assert "END CERTIFICATE" in pem

print("SD2Cloud-visible settings parity contract: OK")
