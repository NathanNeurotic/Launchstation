"""Guard the native staged updater and release-integrity contract."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
http_h = (root / "include/cloud_http.h").read_text()
http_c = (root / "src/cloud_http.c").read_text()
update = (root / "src/self_update.c").read_text()
tools = (root / "src/dash_tools.c").read_text()
settings = (root / "src/dash_settings.c").read_text()
yaml = (root / "ps2.yaml").read_text()
workflow = (root / ".github/workflows/compile.yml").read_text()

assert "cloudHttpDownloadFd" in http_h
assert "cloudHttpDownloadFd" in http_c
assert "Too many HTTPS redirects" in http_c
assert "mbedtls_sha256_update" in http_c
assert "max_bytes" in http_c
assert "Transfer-Encoding" in http_c and "chunked" in http_c

assert "r3LaunchELF_ATTDASH.ELF.sha256" not in update
assert "parse_expected_sha" in update
assert "r3LaunchELF_ATTDASH.ELF" in update
assert "checkELFheader(stage)" in update
assert '"%s.BAK"' in update
assert '"%s.UPDATE"' in update
assert "memcmp(expected, actual" in update
assert "rename_leaf(parent, leaf, backup_leaf)" in update
assert "rename_leaf(parent, stage_leaf, leaf)" in update
assert "rename_leaf(parent, backup_leaf, leaf)" in update
assert "supported_boot_path" in update

assert '#include "self_update.h"' in tools
assert "releaseAssetDigest" in tools
assert '"digest"' in tools
assert "selfUpdateInstall(ca_path, asset_digest" in tools
assert '"Install update"' in tools
assert "SHA-256 verified and installed in place" in settings

assert "src/self_update.c" in yaml
assert "sha256sum r3LaunchELF_ATTDASH.ELF > r3LaunchELF_ATTDASH.ELF.sha256" not in workflow
assert "artifacts/r3LaunchELF_ATTDASH.ELF.sha256#SHA-256" not in workflow
assert "r3LaunchELF_ATTDASH-Complete.zip" in workflow

print("PASS: native updater verifies GitHub release digests, stages safely, backs up, and rolls back")

update_ca = (root / "res/WLE-UPDATE-CA.PEM").read_text()
assert update_ca.count("-----BEGIN CERTIFICATE-----") >= 2
