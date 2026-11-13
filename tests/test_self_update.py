"""Guard the native in-place updater and release-integrity contract."""
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
assert 'selfUpdateExtractPsuSidecarMem(ram, package_size, "WLE-UPDATE-CA.PEM", update_ca)' in update
assert 'selfUpdateExtractPsuSidecarMem(ram, package_size, "BGM.ADP", bgm)' in update
assert "need_ca = !wleExists(update_ca)" in update
assert "need_bgm = !wleExists(bgm)" in update
# One path: the package is downloaded into RAM, checked, and written over the running ELF.
# No second file, rename, delete, staging or backup of the ELF.
for gone in ("Rename(", "rename_leaf", "download_file(", ".PACKAGE", ".UPDATE", "stage", "selfUpdateExtractPsu(", "checkELFheader"):
    assert gone not in update, gone
assert 'FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC' in update  # mcman truncates only in its O_CREAT branch
assert update.index("memcmp(expected, actual") < update.index("selfUpdateLocatePsuElf(ram") < update.index("looks_like_elf(ram + elf_pos") < update.index("overwrite_elf(elf, ram + elf_pos")
assert update.index('selfUpdateExtractPsuSidecarMem(ram, package_size, "BGM.ADP", bgm)') < update.index("overwrite_elf(elf, ram + elf_pos")
assert '"%s%s.BAK"' in update  # only to remove one an older build left
assert "memcmp(expected, actual" in update
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

print("PASS: native updater verifies GitHub release digests and overwrites the ELF in place")

update_ca = (root / "res/WLE-UPDATE-CA.PEM").read_text()
assert update_ca.count("-----BEGIN CERTIFICATE-----") >= 2

# Controller variants select matching package and ELF names.
header = (root / "include/self_update.h").read_text()
assert '#ifdef DS34' in header
assert '"APP_LAUNCHSTATION-DS"' in header and '"APP_LAUNCHSTATION"' in header
assert '"LAUNCHSTATION-DS.ELF"' in header and '"LAUNCHSTATION.ELF"' in header
assert 'releaseAssetDigest(response.body, SELF_UPDATE_ASSET' in tools
