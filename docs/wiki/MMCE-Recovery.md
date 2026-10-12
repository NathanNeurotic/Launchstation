# MMCE: safe local and Google Drive recovery

LAUNCHSTATION retains local verified version history for MMCE card images and, optionally, copies history to Google Drive. The primary requirement is that **a recovery attempt must not destroy the active emulated card or valid older snapshots**.

## Local recovery

1. Configure the parent folder of `WLE-MMCE-Backups` in Settings → Storage & devices → MMCE backups.
2. Choose **Recover saved card image...** in Settings or select a completed `.mcd`/`.ps2` snapshot in File Browser.
3. Select a new USB/HDD destination. LAUNCHSTATION validates the image, writes a distinct `.PART`, reads back/verifies it and publishes `RECOVER-###.mcd` only after success.
4. Inspect the newly produced file before making any manual card replacement. The current MMCE card is not replaced or activated.

To preserve an individual MMCE card before a manual recovery, highlight its `.mcd` in `mmce0:/MemoryCards` or `mmce1:/MemoryCards` and choose the targeted history-backup action. Verification and retention must protect the selected source.

## Google Drive history

1. Link the Google account using the native OAuth settings and the Google CA trust bundle; Drive support is optional.
2. In Tools → Google Drive backups, choose **Browse cloud history...**. This is a paged, read-only view of application-created backup history.
3. A snapshot **must include its stored `wle_sha256` digest** to be eligible for recovery. Without it, the entry is only inspectable.
4. Choose a qualified snapshot and a USB/HDD folder. LAUNCHSTATION authenticates the download, enforces the length limit, verifies SHA-256, validates card geometry, converts PCSX2 `.ps2` where applicable and publishes a new `RECOVER-CLOUD-###.mcd` only after verifying the completed copy.
5. The Drive entry, local history and live MMCE card stay unchanged; failures cannot publish a completed recovery file.

## Retention, IGR and failures

Local snapshot export uses `.PART` and verifies before publication. Retention follows successful completion and must not prune a protected restore source. Unchanged cards may skip creating a new version. OPL IGR helper integration backs up changed cards locally before optional cloud upload. A cloud failure does not erase a verified local snapshot.

**Not implemented:** direct overwrite/activation of a live MMCE card from local history or Drive. Do not misread a successfully recovered USB/HDD file as an installed MMCE card.

## Required bug-report fields

Commit SHA, PS2 model, original MMCE unit, selected backup format, destination device/filesystem, exact error, whether the target `.PART` remains, and whether any data changed. Do not send OAuth tokens, game/BIOS data or private backups publicly.
