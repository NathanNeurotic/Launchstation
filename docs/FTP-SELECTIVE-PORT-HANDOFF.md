# Selective FTP port: status

Selective port of the FTP fixes from saildot4k/Launchstation `ftp-fixes` (reviewed at `7b2c2cde8d9ed858a65c283f16028457d64059c1`), adapted to keep our own features: all supported devices and aliases, PS2Link, remote commands, staged/resumable ISO receive, and the FTP server staying resident after Back. His removals (ps2netfs, MMCE/MX4SIO exclusion, device family restriction) and his exclusive-FTP lifecycle (IOP reboot on entry, FTP-only screen) were not taken.

Host-tested only. NOT TESTED on a real PS2 or emulator.

## What the port does

- Virtual `/hdd/<unit>/<partition>/` paths for every operation, with virtual CWD/parent navigation.
- Reference-counted PFS mounts. Physical PFS slots 0..3 belong to the EE browser/VMC, 4..7 to FTP; FTP's logical `/pfs/0..3` maps onto 4..7 (`FS_PFS_BASE`), including the `/pfs` root unit probe.
- Compatible ATA/HDD/USB/iLINK drivers are prepared before networking; the PC CARD CompactFlash bus guard and the console HDD/ATA gate (`-no-hdd`) are honored.
- IOP `FtpControl` status/cooperative-stop service and an EE `ftp_session.c` client using NOWAIT RPC with bounded waits.
- FTP panel with Stop and Back/Keep running. Remote commands are polled from the panel regardless of section. A network startup failure shows a modal instead of returning silently.
- Idle Stop is bounded: `select` uses a 100 ms timeout and checks the stopping flag. If the control thread does not answer, shutdown gives up after a bounded wait (about 1 s of status polling, each RPC itself capped at 1 s) with "FTP control did not respond. The IOP was not reset over open files."
- Background memory-card polling pauses while the FTP server is resident and resumes after shutdown.
- SITE mount management was removed (replies 500).

## Host coverage

`tests/test_ftp_server.py`, `tests/test_ftp_network.py` and `tests/test_ftp_session.py` run in CI (`.github/workflows/compile.yml`), plus `tests/test_root_mc_poll.py` for the polling gate. The server fixture stores physical PFS units and covers partition-style HDD getstat, cooperative STOP during a transfer, idle Stop, authenticated status reporting, LIST 550 on a failed open, and mount release on a failed opendir.

## Remaining before calling it console-proven

- No console or emulator run.
- Native LLVM ps2build not run locally; CI GCC ps2build is the compile evidence.
- IOP heap not measured. Source-derived estimate: PFS `-m 4` to `-m 8` adds four `pfs_mount_t` (324 bytes each, about 1.3 KB); the FTP control thread adds a 4 KiB stack. `-n 40` and `-o 10` are unchanged.
- A full trace of every IOP reset, app handoff and poweroff path against a resident FTP server is still recommended.
