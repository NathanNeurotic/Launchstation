# Rolling release installation, native updates and recovery

## Download and select the right package

[Rolling latest](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest) contains four installable packages: `APP_LAUNCHSTATION.zip`, `APP_LAUNCHSTATION.psu`, `APP_LAUNCHSTATION-DS.zip`, `APP_LAUNCHSTATION-DS.psu`. The DS variant includes the inherited DS34 controller drivers; use standard packages for ordinary PS2 controllers.

A standard install folder has `LAUNCHSTATION.ELF`, `title.cfg`, `WLE-UPDATE-CA.PEM` and `BGM.ADP`. The DS package has `LAUNCHSTATION-DS.ELF` and its own APP folder. Keep customized configuration and sidecar files when upgrading.

## Native updater

A native check verifies ancestry so that older/divergent releases are refused. It selects a matching verified PSU, downloads it over trusted HTTPS and verifies the published SHA-256 digest, validates the packed ELF and stages replacement. It can install **missing only** CA/BGM sidecars from a verified four-file PSU; existing custom sidecars remain untouched. The old app ELF is retained as a `.BAK` recovery copy during replacement. Restart to execute the installed build.

If `.UPDATE` or `.PACKAGE` remains from an interrupted attempt, the updater refuses to overwrite it. Preserve the original ELF and settings, inspect the incomplete staging files and resolve the conflict manually; do not delete them blindly. Old updater versions that expect the retired package format may need one manual new-package install.

## Hardware regression recovery

The [2026-10-08 preserved USB-tested baseline](https://github.com/NathanNeurotic/Launchstation/releases/tag/usb-second-drive-2026-10-08-58d4514) remains available separately from the moving `latest` tag. A passing Actions build is not a guarantee of console acceptance; retain a known-good binary if you need rollback.

The site is generated from `docs-site/`; a documentation-only merge can update the rolling release's commit/build metadata without changing runtime source.

## Verification and support

Record release tag, full commit SHA, package SHA-256, PS2 console model, device topology (especially multiple USB drives), controller type and the first observed failure stage. Avoid risky hot-unplugging or overwriting your last good app/settings backup.
