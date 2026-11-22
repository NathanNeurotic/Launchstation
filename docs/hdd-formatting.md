# HDD formatting

HDD Manager offers three destructive whole-drive formats. All formats erase
existing partitions and require the existing two confirmations, after the
format and its capacity summary have been selected.

| Format | Layout |
| --- | --- |
| APA | Existing APA formatter and PFS system partitions |
| exFAT | MBR, one partition starting at 1 MiB, label `PS2`, 32 KiB clusters |
| APA Jail (APA + exFAT) | A2 marker, MBR APA area, 32 MiB ext2 `RECOVERY`, then exFAT `PS2` with 32 KiB clusters |

The APA Jail size selector reserves **additional** space after the standard
APA system partitions. It uses 128 MiB increments and permits zero additional
space. The maximum keeps the APA area within half of the drive. The system
partition sizes follow the existing driver's capacity-dependent size cap;
on most drives they occupy 2 GiB. Creating APA layouts requires at least
8 GiB, so that the driver's cap does not fall below 128 MiB. Plain exFAT
requires at least a 64 MiB volume plus alignment and metadata space.

The formatter uses the ATA driver's existing 32-bit sector capacity. It rejects
the driver's saturated-capacity sentinel for larger drives; this is not a
formatter for disks beyond the current 2 TiB addressing limit. Disks whose
capacity cannot be distinguished from a driver error are also refused.
PSX/DESR internal-drive formatting remains unavailable.

## Driver lifetime

The HDD and ATA driver sources and ABIs are unchanged. The small, separately
loaded `iop/hdd_format` service provides checked raw transfers in 32 KiB chunks.
Before writing, Launchstation stops audio, checks VMC/PFS unmount results,
refuses an active FTP server, and detaches the ATA0 BDM root and its mounted
volumes. The normal runtime IOP reset reloads controllers and the storage stack
after the worker finishes, including after a failed format. This drops stale
APA and FAT caches before the drive is accessed again. Builds that disable
IOP reset cannot use this formatter.

The portable writer in `src/hdd_format.c` validates its plan before writing,
builds exFAT allocation metadata and both boot regions, checks the generated
boot regions and final MBR by reading them back, repairs the hybrid APA header
checksum, and flushes writes. Errors are reported as partial-format failures;
they never produce a success notification.

## APA allocation boundary

`src/hdd_apa_boundary.c` reads and validates the root APA header and MBR ranges.
The shared EE HDD library checks each main/subpartition allocation against the
APA boundary before asking the unmodified driver to allocate it. It follows
the driver's first eligible free-size slot, then its aligned append policy.
Both build systems use that library. Reported APA free space excludes recovery
and exFAT space. Unknown or damaged hybrid maps disable allocations rather
than treating the rest of the physical disk as available APA space.

This protects allocations made through Launchstation. Other applications and
drivers can still allocate beyond the reserved APA area. The
[PSBBN Definitive Project documentation](https://github.com/CosmicScale/PSBBN-Definitive-Project#apa-jail)
describes the same risk of overwriting exFAT when APA allocations exceed their
reserved space.

New partitions retain the **128 MiB minimum**. The boundary reader can inspect
existing 8 MiB partitions; it does not enable their creation. An append after
a tail that is not aligned to 128 MiB is refused because the stock allocator
cannot safely pad it. A suitably aligned existing free block can still be
reused. Supporting new mini partitions would also need a review of the
allocator, PFS overhead and compatibility with other PS2 software.

## Recovery volume

The A2 recovery partition contains a creation-time snapshot of the first
128 MiB (`apa_index.gz`), the cleared final 33 sectors (`gpt_2nd.bin`), and a
`README.txt`. These files do not track later APA changes or back up user files.
The snapshot uses the existing zlib dependency and gzip instead of PSBBN DP's
xz. Decompress it before using raw recovery tools; do not pass it to a tool
that specifically expects `apa_index.xz`.

The layout is based on the
[PSBBN DP installer](https://github.com/CosmicScale/PSBBN-Definitive-Project/blob/main/scripts/PSBBN-Installer.sh).
The filesystem writer follows the
[Microsoft exFAT specification](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification).
Unicode upper-case mappings are the standalone `ff_wtoupper` function and its
tables from ChaN's FatFs, under its permissive notice retained in
`src/hdd_format_upcase.c`; no code-page conversion tables are included.

## Verification

`tests/test_hdd_format.py` formats sparse host disk images and checks partition
bounds, exFAT boot/upcase checksums, metadata allocation, decompressed recovery
data, capacity limits and injected I/O failures. It runs `fsck.exfat` and
`e2fsck` when installed. `tests/test_hdd_format_ui.py` exercises the actual
format action, cancellation at both confirmations, failure handling and cache
reload. No test opens a physical disk. PS2 hardware validation is still needed.


## Online software installer

Native HTTPS builds offer software installation as part of formatting. All
application binaries, icons and system resources are downloaded; none are
bundled in Launchstation. A working network connection and
`WLE-UPDATE-CA.PEM` are required. After the two erase confirmations the HDD is
formatted, then installer files are downloaded to `ata0:/PSBBN_DP_TEMP` for
APA-Jail. Pure APA uses `__common/PSBBN_DP_TEMP` through a temporary PFS mount.
The installer keeps a read-only CA snapshot in EE memory across the IOP reset,
so HTTPS still works when the original CA file was on the formatted HDD. Use the matching release CA
bundle; it includes the Go Daddy G2 root required by Archive.org.

The HTTPS downloader combines arbitrary TLS/HTTP fragments into 16 KiB file
writes from a 64-byte-aligned buffer. Writes start at sector boundaries and use
DMA-aligned source buffers for direct FatFs/ATA transfers; the filesystem handles
the final partial sector. Short writes finish the current sector before continuing.
This avoids the one-to-three-byte shifts caused by unaligned ATA DMA pointers.
Progress counts bytes accepted by storage, including the final buffered flush.

- **Pure APA:** format only, install WLE-R3Z/R3CONFIGURATOR, install OSDMenu MBR
  and HOSDMenu, or install both. Apps share `__common/APPS`, with the HDD-OSD
  visibility attribute `0xc4a7` on APPS and each application folder. No separate
  app partitions or PSBBN/Linux partitions are created.
- **APA Jail:** format only, or install English PSBBN DP, OSDMenu MBR,
  HOSDMenu, WLE-R3Z, R3CONFIGURATOR, OPL, and NHDDL with Neutrino. Apps live on exFAT under `ata0:/APPS`.
  Each has a standard 128 MiB PFS launcher with PATINFO, HDD icons, and PSBBN
  browser metadata. The installer does not enable 8 MiB APA partitions.

APA-Jail software installs create DP's required Linux system/swap/data partitions.
The user chooses Music and Contents sizes (both default to 1 GiB), whether to
install standalone PS2 Linux (default Off), and additional APA space to leave
free for future use (default zero). The required allocation is calculated from
the selected volumes, app launchers and standard APA allocator alignment, then
the future reserve is added. The default PSBBN/media/apps layout requires
**6528 MiB beyond the standard APA system partitions**, before any future reserve.
The APA region is capped at 128 GiB for PSBBN. The existing driver must support a
single 2 GiB APA extent (checked before download/format); smaller-capacity
configurations are refused rather than inventing a subpartition mapping.
PSBBN needs a compatible Sony Network Adapter. Before the erase confirmations,
the installer resolves the DP Git revision and reads its language choices and
English display names from the upstream installer and UI data. It offers only
languages with a published pack in the Archive.org item referenced by that
installer. English is selected initially when available. New language entries
in the same upstream format appear without rebuilding Launchstation; unavailable
packs are omitted. A network or metadata failure stops before formatting.
Downloaded scripts are parsed as data and are never executed.

Music and Contents offer power-of-two sizes from 1 GiB up to the driver's
single-extent limit (at most 32 GiB). Optional PS2 Linux adds a fixed 2 GiB
`__linux.3` root and a user-selected `__linux.10` home partition from 1 GiB up
to the lesser of 16 GiB and the driver's single-extent limit. The limits keep
the PS2 Linux mapping to one validated APA extent per filesystem. Oversized
combinations return to the size choices before any erase confirmation.
The review is a borderless two-column table containing the format, capacity,
software, language, default launcher, media/Linux sizes and future reserve.
It uses 24-pixel rows and scrolls with Up/Down or Left/Right for a visible page
when the current video mode cannot fit every row. The separate final erase
confirmation remains.

Japanese also installs DP's Japanese PSBBN kernel and latest Japanese channel
archive into `__linux.9`. Its download can approach 1 GiB and is included in
overall/file progress and archive preflight. OSDMenu uses DP's corresponding
supported language code (Japanese maps to `jap`; unsupported OSD languages use
English). OPL selects the matching installed language file when available.

Enabling PS2 Linux downloads `PS2Linux.tar.gz` from the same Archive.org item,
checks its metadata size and preflights it along with
the PSBBN and language archives. Both Linux kernels come from the pinned DP
Git revision. Permissions, links and device nodes are preserved; the home
filesystem is prepared for first boot. Holding Circle boots the installed NTSC
Linux kernel, matching DP's default configuration. Linux requires a USB keyboard.
With Linux Off, its archive/kernels and both extra partitions are omitted.

The system archive's Unix permissions, owners, hard links, symlinks and device
nodes are preserved in ext2. The music volume is FAT32, matching DP's Linux
layout; the external-facing APA-Jail partition remains exFAT with label `PS2`.
Boot configuration is generated for the actual installed components and paths.
Pure APA boots HOSDMenu; PSBBN installs boot PSBBN with `-dev9=NICHDD` and offer
HOSDMenu on Cross. Start/Select hotkeys launch the included apps when present.

PSBBN installs ask for a default game launcher: **Open PS2 Loader (OPL)** or
**Neutrino (NHDDL)**. Both launchers are downloaded from the same pinned DP
commit. Their ELFs are installed in `__system/launcher`; the complete Neutrino
runtime (including config files and IRX modules) goes to `ata0:/neutrino` and
OPL's language assets go to `ata0:/LNG`. Matching licenses and version files are
included. The generated OPL configuration enables BDM HDD, apps, and cover art.
These binaries are copied after archive extraction to replace any older copies.

The choice controls the Square boot shortcut, the first HOSDMenu launcher entry,
and DP's standard `PP.LAUNCHER` partition, including its icon, artwork, and PSBBN
Navigator shortcut. NHDDL receives `-mode=ata`. The other launcher remains in the
HOSDMenu menu. The extra launcher uses a standard 128 MiB APA partition and is
included in allocation calculations. Pure APA profiles are unchanged.

### Download and failure behavior

Repository assets come from
[PSBBN DP](https://github.com/CosmicScale/PSBBN-Definitive-Project) at one
resolved Git commit. The installer resolves selected package metadata before
asking to erase and checks the staged assets against the published hashes.
HDD-OSD-only installs
fetch DP's referenced `extras.zip`. PSBBN installations fetch the newest
complete system and selected language versions from DP's
[Archive.org collection](https://archive.org/details/psbbn-definitive-patch-v4.1),
using its published sizes and SHA-1 hashes. Archive decompression, CRCs, paths,
required boot files and capacity bounds are checked after downloading and before
installing the software. Staged file sizes and hashes are checked before
applying the payload. Downloads and verification report file progress; DNS,
connection, TLS and response-header stages are also shown so connection setup
does not appear as an unexplained static download screen. The HTTP client reports connection, TLS and response-header stages while waiting for data.

An ownership marker protects cleanup. Successful installation removes the
temporary files; a failed download or install leaves the folder for inspection.
Because formatting now happens first, download/verification failure leaves a
formatted HDD with an incomplete software installation. The UI reports this
explicitly. Network setup and CA readability are checked before the erase prompts.

The installer activates the downloaded MBR only after files/configuration
succeed, after flushing the payload and verifying its readback. APA-Jail recovery metadata remains the creation-time snapshot.
After that commit succeeds, MBR/HOSDMenu installs also enable the console's
persistent HDD boot setting through the MECHACON configuration service, using
the same set-0 boot flags as FreeMcBoot. All unrelated configuration bits are
preserved and the entire 15-byte block is read back for verification. An already
enabled console needs no write. Format-only and apps-only installs leave the
console setting alone. A configuration read/write/verification failure is
reported explicitly and retains the staged files and diagnostic log; the
installed software and MBR remain on disk.
Installation keeps the ATA filesystem mounted so archives can be read from
the same drive. The private raw service validates the on-disk APA-Jail boundary
and refuses writes into exFAT. Installation transfers use the existing HDD
driver's devctl semaphore; reads/copies/raw writes run sequentially on the EE
worker. The destructive formatter still detaches ATA before formatting. The
HDD and ATA driver sources/ABIs are unchanged. Storage is reset/reloaded after
raw writes, including errors. A failed software install reports the incomplete
installation and retains the download folder; it does not silently reformat or
report success. Cancellation or preparation failure before formatting leaves
the HDD untouched.

### Installer verification

`tests/test_hdd_installer.py` checks bounded metadata/archive parsing, layout
settings, Unix metadata, language overlays, allocation cleanup, music/swap
filesystems, and injected errors. `e2fsck`, `debugfs` and `fsck.fat` validate the
output independently. `tests/test_hdd_installer_apply.py` exercises the production
app/HOSD copy path against a host storage facade, including visibility flags
and PSBBN browser resources. `tests/test_hdd_installer_partitions.py` runs all
default and optional Linux partition requests against the actual HDD driver's
size parser, including its required `1G`/`2G` syntax, custom media/home sizes,
extent validation and injected errors. `tests/test_hdd_installer_choices.py`
checks choice defaults/cancellation, allocation and reserve bounds, optional
asset metadata and the Linux boot hotkey.
`tests/test_hdd_installer_launchers.py` runs production package discovery and
downloads against a local HTTPS facade, checking pinned URLs, recursive runtime
trees, staged file integrity, short I/O, missing files, duplicate/traversal/symlink
metadata, and network/storage failures. Apply tests cover both defaults, archive
replacement, runtime modules/configuration, OPL settings, and launcher resources.
The stage test checks internal folder creation,
the CA snapshot across format and ownership-checked cleanup. `tests/test_hdd_installer_finish.py` verifies software
and flush/readback checks precede MBR activation, with failure cleanup.
`tests/test_hdd_installer_boot.py` checks console configuration preservation,
already-enabled behavior, bounded service failures and readback verification,
and exercises the installation job's ordering and component/failure guards.
The format UI test also exercises format-before-download and failed-install
cache retention. Raw-session tests verify the ATA source remains available,
HDD/PFS serialization and exFAT write bounds; transport tests verify status
callbacks and cleanup. Host tests never open a physical disk. PS2 hardware boot
and network/storage validation are still required.

`tests/test_cloud_download_alignment.py` checks fragment lengths, source alignment,
sector boundaries, buffer tails, short writes and write failures against the
production aligned downloader.


### Temporary verbose PSBBN diagnostics

APA-Jail PSBBN installs show a scrolling installation log throughout
preparation, formatting, downloading, checks and installation. It includes
elapsed time, time since the last activity, progress counts, the last raw-disk
command (including its LBA, sector count and result), and a timestamped log.
An outstanding raw command shows how long it has been waiting.
Up/Down scroll by line, Left/Right by a visible page, and Square returns
to the latest messages. The shared scrollbar tracks the visible position within
the retained log history, including during installation. A review screen stays
open at completion or failure.

Two progress bars stay visible from preparation through completion. The upper
bar is a monotonic, weighted estimate across preparation, formatting, downloading,
checks, installation and final boot/storage setup. Download progress adds bytes
across all selected files; archive checks/extraction add compressed bytes across
the selected system, language and optional Linux archives. Starting a new file
does not reset overall progress. Only a successful final result reaches 100%;
failures retain the last overall value, and IOP resets preserve it in EE memory.

The lower bar tracks the current file or operation independently. Extracted
files show their names and uncompressed write progress. Operations whose size
is unknown show an animated segment rather than a misleading percentage.

The log records the Launchstation build, disk layout and LBAs, Git revision,
downloaded files, partition creation, filesystem setup, archive completion,
MBR activation, recovery and errors. Byte counters and transient DNS/TLS/header
status update the live display without adding history entries or disk checkpoints.
Repeated status counters and sampled archive-entry records are omitted.
It never records certificate contents, credentials,
request headers or response bodies.

After the temporary folder exists, checkpoints append diagnostics to
`ata0:/PSBBN_DP_TEMP/INSTALLER.LOG`. Successful installs remove downloaded
payloads but retain this log and its ownership marker. Failed installs retain
the entire temporary folder. If the drive cannot write the log, the GUI reports
the write error and keeps the latest 256 display lines in RAM. Before a log
file can be created, diagnostics are available in the GUI only.

TLS validation, staged digest checks, transfer/file-size bounds, archive
CRC/format checks and MBR/NVRAM readback checks are all enforced.

`tests/test_hdd_installer_log.py` checks the bounded history, timestamps,
progress/idle snapshots, raw command snapshots, file checkpoints, short writes
and error handling. `tests/test_hdd_installer_log_view.py` checks log navigation
and that the diagnostic layout stays within its panel across display heights.
`tests/test_hdd_installer_progress.py` checks cumulative archive progress with
and without Linux, individual file-write progress, and I/O errors. Log/view tests
check both bars, large byte counters, phase transitions, file resets and the
success-only completion rule.
