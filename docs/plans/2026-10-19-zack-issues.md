# Zack's October 19 follow-up

All changes in this checklist belong to one branch and one PR:
`fix/zack-issues-20261019`, based on `82082b6`. Keep follow-up commits here.
The user explicitly excluded #160 (stock PS2 sound effects).

| Issue | Work and evidence | Remaining acceptance |
| --- | --- | --- |
| #187 destructive ELF replacement | Write a separate sibling, close and checksum its readback before parking the original and publishing. Failed publication rolls back; failed rollback retains both recovery files. Never delete the existing destination before copying. Host fault injection covers both MC ports, ordinary filesystem, short reads, corruption, close failure, both rename failures, rollback failure and stale recovery files. | Console repeated ELF replacement and low-space cases. Replacement needs room for both files and rename support; unsupported operations preserve the original. |
| #184 transfer progress | Dedicated filename and remaining-MB lines, smoothly approached progress fill, one visual tick per frame. Native copies bypass legacy metric flattening. Slow MC/VMC operations use 16 KiB chunks; recursive deletion refreshes between entries and advances selected-item progress after completion. Readback verification also draws progress. | Console/theme inspection; a blocked synchronous filesystem RPC still prevents drawing until it returns. |
| #185 Home MASS label | The shared app-device label now reads MASS Storage in Home and app views; the library filter reads MASS. Filesystem paths and device matching stay unchanged. | Console inspection. |
| #182 summarized release changelog | Pending. | Inspect generated release notes. |
| #125 software-clock persistence | Pending. | Unplug/restart on dead-RTC console. Unpowered elapsed time is unavailable without an external time source. |
| #173 MX4SIO loading stalls | Pending. | Present/absent adapters on console. |
| #174 MMCE loading/reset stalls | Pending. | Present/absent adapters and audio on console. |
| #175 MMCE cross-storage/slot 2/power-off | Pending. | Mixed-storage transitions, both MC slots and power-off on console. |

Host tests and builds establish software behavior under the tested conditions.
They do not establish adapter timing, CRT layout or physical-console reliability.
Keep hardware-dependent reports open until Zack verifies their remaining cases.

## Replacement recovery

Ordinary replacement files use unused `LS-COPY-NNN.TMP` and
`LS-OLD-NNN.BAK` sibling names (within the MC filename limit). They are never
reused while present. If power is interrupted during publication, inspect those
files before removing anything: the BAK contains the old file and the TMP the
replacement. Failed cleanup keeps the BAK. Failed rollback keeps both files.
New files and PSU container generation retain their existing paths; ordinary
files extracted from a PSU use the replacement protection too.
