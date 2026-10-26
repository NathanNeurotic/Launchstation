# PC cloud relay

This is an optional extension for the existing MrRexD-compatible PC workflow. It does **not** replace the existing Command PS2 / PS2 Drive tools, and it is **not** needed for Google Drive: Launchstation uploads to Google Drive on its own (Tools > Google Drive backups, see [docs/NATIVE_CLOUD.md](../../docs/NATIVE_CLOUD.md)). Use the relay when you want a different cloud provider, an rclone crypt/remote you already have, or prefer to keep every credential on the PC.

The relay uses `rclone.exe` on the PC, talks to Launchstation through the same FTP server used by MrRexD's tools, finds the versioned `WLE-MMCE-Backups` history, and copies new history files to any rclone cloud remote. It adds nothing to the PS2 executable.

## Setup

1. Keep using MrRexD's existing PC bundle. Put this `Cloud Relay` folder beside its `PS2 Drive` folder, or pass `-Rclone` with the path to another rclone executable. If neither is found the script looks for `rclone.exe` on `PATH`.
2. Start **Tools > FTP / Remote access** in Launchstation and leave the dashboard open. The PS2 must be reachable at the address you pass with `-Ps2` (default `192.168.1.111`; use the address shown by Launchstation or in Settings > Network).
3. Configure a cloud remote once:
   `Cloud Relay.bat -Configure`
4. Run a relay:
   `Cloud Relay.bat -CloudRemote "gdrive:PS2/WLE-MMCE-Backups"`

The cloud provider is entirely an rclone choice; Google Drive is only one example. Credentials stay on the PC.

## Backup discovery

Without extra arguments the script searches the PS2 FTP tree (up to eight folder levels) for exactly one `WLE-MMCE-Backups` folder. If there are several histories, or the chosen backup destination is too deeply nested to auto-detect, specify the destination selected in Launchstation:

`Cloud Relay.bat -CloudRemote "gdrive:PS2/WLE-MMCE-Backups" -BackupRoot "ata0:/Backups"`

You may also pass the history folder itself:

`-BackupRoot "ata0:/Backups/WLE-MMCE-Backups"`

Supported PS2 path families match the existing MrRexD companion conventions: `ataN:`, `massN:`, `usbN:`, `mx4sioN:`, `mmceN:` and `mcN:`.

## Safety

The relay intentionally runs **rclone copy**, never **rclone sync**. It also uses `--ignore-existing`, because Launchstation's MMCE history files are versioned and immutable. A relay therefore uploads missing history without deleting or replacing cloud backups.

Use `-DryRun` to inspect what would be copied:

`Cloud Relay.bat -CloudRemote "gdrive:PS2/WLE-MMCE-Backups" -DryRun`

Limits to be aware of:

- The relay copies **every file** it finds under `WLE-MMCE-Backups`. Unlike the native uploader, it does not skip `.PART` files from an interrupted backup and does not validate memory-card geometry or compute checksums. Because of `--ignore-existing`, a partial file that was copied once is never replaced. Run it only while no backup is in progress, and remove stray `.PART` files before relaying.
- Launchstation's FTP server is unauthenticated (anonymous login, plain FTP). Run the relay only on a network you trust. See the security notes on the FTP page of the documentation site.
- The script logs in to the PS2 as `anonymous`.
- The FTP server does not implement `EPSV` or `MLSD`, so the script disables both for rclone.

Optional arguments:

- `-Ps2 192.168.1.111` changes the console address.
- `-BackupRoot <PS2 path>` bypasses auto-detection.
- `-Rclone <path>` selects a specific rclone executable.
- `-RcloneConfig <path>` selects a specific rclone config file.
- `-Configure` opens rclone's normal remote configuration UI.
- `-DryRun` lists what would be copied without changing anything.
