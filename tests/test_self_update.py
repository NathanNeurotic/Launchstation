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
assert "SELF_UPDATE_ASSET" in update
assert "selfUpdateExtractPsu(package, stage, package_size)" in update
assert 'selfUpdateExtractPsuSidecar(package, "WLE-UPDATE-CA.PEM"' in update
assert 'selfUpdateExtractPsuSidecar(package, "BGM.ADP"' in update
assert "need_ca = !wleExists(update_ca)" in update
assert "need_bgm = !wleExists(bgm)" in update
assert "wleExists(update_ca_stage)" in update and "wleExists(bgm_stage)" in update
assert "wleExists(package) || wleExists(stage)" in update
assert update.index("wleExists(package) || wleExists(stage)") < update.index("download_file(UPDATE_ELF_URL")
assert update.index("checkELFheader(stage)") < update.index('selfUpdateExtractPsuSidecar(package, "WLE-UPDATE-CA.PEM"') < update.index("Rename(parent, &staged_info, leaf)")
assert 'rename_leaf(parent, "WLE-UPDATE-CA.PEM.UPDATE", "WLE-UPDATE-CA.PEM")' in update
assert 'rename_leaf(parent, "BGM.ADP.UPDATE", "BGM.ADP")' in update
assert "checkELFheader(stage)" in update
assert '"%s.BAK"' in update
assert '"%s.UPDATE"' in update
assert "memcmp(expected, actual" in update
assert "rename_leaf(parent, leaf, backup_leaf)" not in update and "rename_leaf(parent, backup_leaf, leaf)" not in update
assert "Rename(parent, &staged_info, leaf)" in update and update.index("find_file_info(parent, stage_leaf, &staged_info)") < update.index("genRemove(remove_path) < 0 || wleExists(remove_path)")
assert "supported_boot_path" in update

assert '#include "self_update.h"' in tools
assert "releaseAssetDigest" in tools
assert '"digest"' in tools
assert "selfUpdateInstall(j->ca_path, j->digest, j->size" in tools
assert "dashWorkerAwait(id, title, 0)" in tools  # the screen keeps drawing
# Progress is reported for a body that came with the headers, and per read.
assert http_c.count("cloudHttpProgress(written, chunked ? -1 : content_length)") == 2
assert '"Install update"' in tools
assert "SHA-256 verified and installed in place" in settings

assert "src/self_update.c" in yaml
assert "sha256sum Launchstation.ELF > Launchstation.ELF.sha256" not in workflow
assert "artifacts/Launchstation.ELF.sha256#SHA-256" not in workflow
assert "scripts/package_apps.py" in workflow

print("PASS: native updater verifies GitHub release digests, stages safely, keeps no .BAK")

update_ca = (root / "res/WLE-UPDATE-CA.PEM").read_text()
assert update_ca.count("-----BEGIN CERTIFICATE-----") >= 2

# Controller variants select matching package and ELF names.
header = (root / "include/self_update.h").read_text()
assert '#ifdef DS34' in header
assert '"APP_LAUNCHSTATION-DS"' in header and '"APP_LAUNCHSTATION"' in header
assert '"LAUNCHSTATION-DS.ELF"' in header and '"LAUNCHSTATION.ELF"' in header
assert 'releaseAssetDigest(response.body, SELF_UPDATE_ASSET' in tools
assert update.index("memcmp(expected, actual") < update.index("selfUpdateExtractPsu(package, stage, package_size)") < update.index("checkELFheader(stage)") < update.index("Rename(parent, &staged_info, leaf)")
