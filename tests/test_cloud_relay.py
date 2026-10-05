"""Static contract checks for the optional MrRexD-compatible PC cloud relay."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
script = (root / "windows/cloud-relay/Cloud Relay.ps1").read_text(encoding="utf-8")
batch = (root / "windows/cloud-relay/Cloud Relay.bat").read_text(encoding="utf-8")
doc = (root / "windows/cloud-relay/README.md").read_text(encoding="utf-8")

assert "WLE-MMCE-Backups" in script
assert "PS2 Drive" in script
assert "rclone.exe" in script
assert "'copy'" in script
assert "--ignore-existing" in script
assert "--dry-run" in script
assert "@('sync'" not in script
assert '"sync"' not in script
assert "'sync'" not in script
assert "disable_epsv=true" in script
assert "disable_mlsd=true" in script
for family in ("mmce", "mc", "ata", "usb", "mx4sio", "mass"):
    assert family in script

assert "Cloud Relay.ps1" in batch
assert "%*" in batch
assert "never **rclone sync**" in doc
assert "Credentials stay on the PC" in doc
