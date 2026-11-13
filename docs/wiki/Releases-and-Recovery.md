# Rolling release installation, native updates and recovery

## Download and select the right package

[Rolling latest](https://github.com/NathanNeurotic/Launchstation/releases/tag/latest) offers these downloads:

| Download | Use |
| --- | --- |
| `APP_LAUNCHSTATION-<build>.zip` | Standard PS2 controller build; extract the app folder. |
| `APP_LAUNCHSTATION.psu` | The same standard app for memory-card installation with **Extract PSU**. |
| `APP_LAUNCHSTATION-DS-<build>.zip` | Adds inherited DS34 drivers for PS3/PS4 DualShock USB/Bluetooth controllers. |
| `APP_LAUNCHSTATION-DS.psu` | The same DS34 app for memory-card installation. |
| `LAUNCHSTATION.ELF`, `LAUNCHSTATION-DS.ELF` | Packed executables without their sidecar files. |
| `BOOT.ELF`, `BOOT2.ELF` | Byte-identical copies of the standard ELF under alternate boot names. |
| `LAUNCHSTATION-<build>.zip` | Optional full kit: standard app, POPStarter/Ember/Neutrino loaders and PC-tool shortcuts. |

Every ZIP name ends with the build number (the commit count). The release title and notes state the current app build number; PSUs used by the in-app updater and loose ELFs keep fixed names. **The kit can be older than the apps:** if its upstream download or validation fails, the app release proceeds and keeps the previous kit attached. Check the kit's filename and `KIT-MANIFEST.json` before assuming it matches the release title.

A standard install folder has `LAUNCHSTATION.ELF`, `title.cfg`, `APPINFO.PBT`, `WLE-UPDATE-CA.PEM`, `BGM.ADP`, `icon.sys`, `list.icn`, `copy.icn` and `del.icn`. The DS package has `LAUNCHSTATION-DS.ELF` and its own `APP_LAUNCHSTATION-DS` folder. ZIP and PSU contents are identical and the ELFs are packed. Keep customized configuration and sidecar files when upgrading.

## How the rolling release is published

`.github/workflows/compile.yml` (Automated-Build) runs on every push to `master`/`main`, on pull requests and on manual dispatch.

1. Host regression tests, the copy-test fixture pack and a documentation-site build.
2. The standard and DS34 ELFs are built with the PS2BUILD LLVM/Clang toolchain; CI checks the controller variants in the linked ELFs, that release ELFs contain only Clang-built objects, the UDPFS SMAP ABI and the embedded loader's low-memory layout.
3. The four app packages and four loose ELFs are created by `scripts/package_apps.py` and verified. The full kit is built separately; its failure does not block the app release.
4. Only for a push (or manual dispatch) on `master`: the workflow skips superseded builds and verifies that `latest` can safely advance. It first publishes and verifies a **build-numbered recovery archive** under the `build-<build>` pre-release tag, checking each asset's GitHub-computed SHA-256 and size against its tested output. If that check fails, `latest` is left untouched. It then uploads assets and any successfully built kit to `latest`, verifies GitHub-computed SHA-256 digests and sizes without creating artificial CI downloads, and moves the tag, title (`Launchstation Rolling <build> - <date> (<commit>)`) and notes. Older rolling ZIP cleanup is attempted after publication but only warns on a listing or deletion failure, so a stale ZIP can remain and keep accruing downloads; the recovery archives are never automatically deleted. An older kit is removed only when a replacement kit was published.
5. Pull requests build and test but never change public downloads.

Release notes come from the `## Rolling` section of [CHANGELOG.md](../../CHANGELOG.md), followed by build and package information. A documentation-only merge therefore also produces a new rolling build without changing runtime source.

**Retry behavior:** If a previous attempt archived a build without its optional full kit (or with an earlier external dependency), a retry keeps that archive immutable and publishes the core app only. The previous kit on `latest` remains available. Release notes explicitly say when the new optional kit was skipped. An archive-tag mismatch or a core app digest mismatch still blocks publication.

**Recovery archive:** Open [build-numbered archives](https://github.com/NathanNeurotic/Launchstation/releases) and select a `build-<build>` pre-release to retrieve files from a previous publication, even after the moving `latest` release has replaced its ZIPs. These archives are not the default update channel. A partial archive from a failed publishing attempt may need inspection and manual repair before retry; the workflow will not silently overwrite a mismatched archive. If a failure interrupts the `latest` update itself (after archive verification, during upload or asset checks), `latest` can temporarily mix builds while its tag and notes still name the previous one. Rerun the workflow so it reuses the verified `build-<build>` archive and republishes every asset. A rerun keeps the original commit SHA, so if `master` has advanced since, the superseded-build guard skips that rerun: publish the current `master` build instead (it creates its own archive) or recover manually. A manual restore must republish the **complete** build from its archive — every asset, not only the affected ones — and then set the tag, title and notes to match that build; confirm each asset's digest and size before announcing it. Replacing assets deletes their asset IDs, so run the download-metrics workflow and wait for it to succeed before touching them, or downloads since the last snapshot are lost from the ledger.

## Download count tracking

The GitHub `latest` release replaces fixed-name ELFs/PSUs and removes older ZIP assets. GitHub counts downloads **per asset ID**, and deleting an asset erases that count from the public release API. Earlier deleted assets cannot be reconstructed.

The [Tracked Downloads ledger](https://github.com/NathanNeurotic/Launchstation/blob/download-metrics/metrics/downloads.json) persists the **highest observed count per asset ID** on a dedicated `download-metrics` branch (not `master`):

- A scheduled workflow samples surviving release assets every three hours and can be run manually.
- Publication takes a **mandatory snapshot before any upload with --clobber**. If the snapshot fails, publication stops rather than knowingly discarding counts.
- The ledger merges concurrent snapshots by asset ID, keeping historical IDs and never decreasing recorded counts.
- The badge is a **tracked ledger total first observed on 2026-11-03**, not a lifetime total and not the live count of the assets currently attached to `latest`: the first snapshot already includes each asset's pre-existing GitHub count, and the ledger keeps historical asset IDs after their assets are deleted. Assets deleted before the first snapshot are irrecoverable, and downloads occurring between the last observation and deletion may be missed.
- Metrics updates do not trigger application builds or modify the native updater's fixed `latest` URLs. Stable releases can be preserved separately without replacing this contract.

If a metrics API outage blocks publishing, restore its access or rerun the snapshot; do not bypass the snapshot just to force a release.

## Native updater

Open **Settings → Storage & devices → MMCE backups → Check for updates** (needs a configured wired network and `WLE-UPDATE-CA.PEM` beside the ELF or in the dashboard settings folder).

- It reads the `latest` release and the SHA-256 digest GitHub publishes for the package matching the running variant (standard or DS).
- It asks GitHub to compare the running commit with the release commit. Only a verified descendant is offered; identical or older builds report "no newer release", and divergent, local or unverifiable builds are blocked with an explanation.
- After confirmation it downloads the PSU into RAM over trusted HTTPS, verifies the digest, checks the packed ELF is a PS2 ELF, and only then writes it straight over the running ELF.
- It can install **missing** `WLE-UPDATE-CA.PEM` and `BGM.ADP` from a verified four-, eight- or nine-file PSU; existing custom sidecars remain untouched. A legacy two-file PSU updates only the ELF.
- Before writing it deletes any `<name>.ELF.BAK` left by an older build and checks that the device has room for the new ELF's growth over the old one (the old ELF's own space is reused, so a memory card needs no spare room). It needs the package plus 4 MB of free memory; without that it stops before touching anything. The space check runs only when the device reports its free space (memory cards and HDD do; USB does not). It never loads another device's drivers to look for space.
- No second file, rename, delete or backup is made. If the write fails part-way the ELF may be damaged: the message gives the step (1 open, 2 write, 3 close) and the error code, and the ELF must be copied to the device again. Restart to execute the installed build.

To roll back, install an older package manually.

## Hardware regression recovery

The [2026-10-08 preserved USB-tested baseline](https://github.com/NathanNeurotic/Launchstation/releases/tag/usb-second-drive-2026-10-08-58d4514) remains available separately from the moving `latest` tag. A passing Actions build is not a guarantee of console acceptance; retain a known-good binary if you need rollback.

The site is generated from `docs-site/` and published to `gh-pages` by `.github/workflows/docs-pages.yml`; the Markdown guides in `docs/wiki/` are mirrored to the GitHub Wiki by `.github/workflows/sync-wiki.yml`.

## Verification and support

Record release tag, full commit SHA, package SHA-256, standard or DS variant, PS2 console model, device topology (especially multiple USB drives), controller type and the first observed failure stage. Avoid risky hot-unplugging or overwriting your last good app/settings backup.
