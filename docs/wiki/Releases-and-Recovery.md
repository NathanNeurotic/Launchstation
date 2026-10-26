# Rolling release installation, native updates and recovery

## Download and select the right package

[Rolling latest](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest) contains four installable packages: `APP_LAUNCHSTATION.zip`, `APP_LAUNCHSTATION.psu`, `APP_LAUNCHSTATION-DS.zip`, `APP_LAUNCHSTATION-DS.psu`. The DS variant includes the inherited DS34 (PS3/PS4 USB/Bluetooth) controller drivers; use standard packages for ordinary PS2 controllers.

A standard install folder has `LAUNCHSTATION.ELF`, `title.cfg`, `WLE-UPDATE-CA.PEM` and `BGM.ADP`. The DS package has `LAUNCHSTATION-DS.ELF` and its own `APP_LAUNCHSTATION-DS` folder. ZIP and PSU contents are identical and the ELFs are packed. Keep customized configuration and sidecar files when upgrading.

## How the rolling release is published

`.github/workflows/compile.yml` (Automated-Build) runs on every push to `master`/`main`, on pull requests and on manual dispatch.

1. Host regression tests, the copy-test fixture pack and a documentation-site build.
2. The standard and DS34 ELFs are built with the PS2BUILD LLVM/Clang toolchain; CI checks the controller variants in the linked ELFs, that release ELFs contain only Clang-built objects, the UDPFS SMAP ABI and the embedded loader's low-memory layout.
3. The four packages are created by `scripts/package_apps.py` and re-verified.
4. Only for a push (or manual dispatch) on `master`: the workflow skips builds that `master` has already moved past, refuses to move the `latest` tag unless the previous tagged commit is an ancestor of the new one, uploads the four assets, downloads them again and compares bytes, and then moves the tag, title ("Launchstation Rolling - date (commit)") and release notes, marking it GitHub's Latest, not a pre-release.
5. Pull requests build and test but never change public downloads.

Release notes come from the `## Rolling` section of [CHANGELOG.md](../../CHANGELOG.md), followed by build and package information. A documentation-only merge therefore also produces a new rolling build without changing runtime source.

## Native updater

Open **Settings → Storage & devices → MMCE backups → Check for updates** (needs a configured wired network and `WLE-UPDATE-CA.PEM` beside the ELF or in the dashboard settings folder).

- It reads the `latest` release and the SHA-256 digest GitHub publishes for the package matching the running variant (standard or DS).
- It asks GitHub to compare the running commit with the release commit. Only a verified descendant is offered; identical or older builds report "no newer release", and divergent, local or unverifiable builds are blocked with an explanation.
- After confirmation it downloads the PSU over trusted HTTPS, verifies the digest, extracts the packed ELF and validates it as a PS2 ELF, then stages the replacement.
- It can install **missing** `WLE-UPDATE-CA.PEM` and `BGM.ADP` from a verified four-file PSU; existing custom sidecars remain untouched. A legacy two-file PSU updates only the ELF.
- The old ELF becomes `<name>.ELF.BAK` before the replacement is renamed into place; if that final rename fails the original is restored. Restart to execute the installed build.
- It supports ELFs started from `mc`, `mass`/`usb`, `mmce`, `mx4sio`, `ilink`, `ata` and `hdd` paths.

If `.UPDATE` or `.PACKAGE` remains from an interrupted attempt, the updater refuses to overwrite it and changes nothing. Preserve the original ELF and settings, inspect the incomplete staging files and resolve the conflict manually; do not delete them blindly. Old updater versions that expect the retired package format may need one manual new-package install.

To roll back, copy `LAUNCHSTATION.ELF.BAK` (or the DS equivalent) back over the ELF, or install an older package manually.

## Hardware regression recovery

The [2026-10-08 preserved USB-tested baseline](https://github.com/NathanNeurotic/Launchstation/releases/tag/usb-second-drive-2026-10-08-58d4514) remains available separately from the moving `latest` tag. A passing Actions build is not a guarantee of console acceptance; retain a known-good binary if you need rollback.

The site is generated from `docs-site/` and published to `gh-pages` by `.github/workflows/docs-pages.yml`; the Markdown guides in `docs/wiki/` are mirrored to the GitHub Wiki by `.github/workflows/sync-wiki.yml`.

## Verification and support

Record release tag, full commit SHA, package SHA-256, standard or DS variant, PS2 console model, device topology (especially multiple USB drives), controller type and the first observed failure stage. Avoid risky hot-unplugging or overwriting your last good app/settings backup.
