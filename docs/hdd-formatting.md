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
`WLE-UPDATE-CA.PEM` are required. Choose a USB or MMCE folder for temporary
files and leave that device connected throughout installation. Use the matching
release CA bundle; it includes the Go Daddy G2 root required by Archive.org.

- **Pure APA:** format only, install WLE-R3Z/R3CONFIGURATOR, install OSDMenu MBR
  and HOSDMenu, or install both. Apps share `__common/APPS`, with the HDD-OSD
  visibility attribute `0xc4a7` on APPS and each application folder. No separate
  app partitions or PSBBN/Linux partitions are created.
- **APA Jail:** format only, or install English PSBBN DP, OSDMenu MBR,
  HOSDMenu, WLE-R3Z and R3CONFIGURATOR. Apps live on exFAT under `ata0:/APPS`.
  Each has a standard 128 MiB PFS launcher with PATINFO, HDD icons, and PSBBN
  browser metadata. The installer does not enable 8 MiB APA partitions.

APA-Jail software installs create DP's Linux system/swap/data partitions,
a 1 GiB music volume, and a 1 GiB PFS contents partition. At least **6400 MiB
of additional APA space** is required; the default remains 8192 MiB. The APA
region is capped at 128 GiB for PSBBN. The existing driver must support a
single 2 GiB APA extent (checked before download/format); smaller-capacity
configurations are refused rather than inventing a subpartition mapping.
PSBBN needs a compatible Sony Network Adapter. This first version uses English
and fixed music/contents sizes.

The system archive's Unix permissions, owners, hard links, symlinks and device
nodes are preserved in ext2. The music volume is FAT32, matching DP's Linux
layout; the external-facing APA-Jail partition remains exFAT with label `PS2`.
Boot configuration is generated for the actual installed components and paths.
Pure APA boots HOSDMenu; PSBBN installs boot PSBBN with `-dev9=NICHDD` and offer
HOSDMenu on Cross. Start/Select hotkeys launch the included apps when present.

### Download and failure behavior

Repository assets come from
[PSBBN DP](https://github.com/CosmicScale/PSBBN-Definitive-Project) at one
resolved Git commit; their Git blob hashes are verified. HDD-OSD-only installs
fetch DP's referenced `extras.zip`. PSBBN installations fetch the newest
complete system and English language versions from DP's
[Archive.org collection](https://archive.org/details/psbbn-definitive-patch-v4.1),
using its published sizes and SHA-1 hashes. Archive decompression, CRCs, paths,
required boot files and capacity bounds are checked **before the two erase
confirmations**. All staged hashes are checked again after the formatting IOP
reset. The temporary folder has a contents-based ownership marker so it can
be located after USB renumbering. Missing or duplicate markers stop installation
and cleanup rather than selecting a different drive.

The installer activates the downloaded MBR only after files/configuration
succeed, after flushing the payload and verifying its readback. APA-Jail
recovery metadata is refreshed after creating the software's partitions.
Storage is reset/reloaded after raw writes, including errors. A failed software
install reports the incomplete installation and retains the download folder;
it does not silently reformat or report success. Failures before formatting
leave the HDD untouched.

### Installer verification

`tests/test_hdd_installer.py` checks bounded metadata/archive parsing, layout
settings, Unix metadata, language overlays, allocation cleanup, music/swap
filesystems, and injected errors. `e2fsck`, `debugfs` and `fsck.fat` validate the
output independently. `tests/test_hdd_installer_apply.py` exercises the production
app/HOSD copy path against a host storage facade, including visibility flags
and PSBBN browser resources. The stage test checks USB renumbering and
ownership-checked cleanup. `tests/test_hdd_installer_finish.py` verifies software
and flush/readback checks precede MBR activation, with failure cleanup.
The format UI test also exercises download-before-erase and failed-install
cache retention. Host tests never open a physical disk. PS2 hardware boot and
network/storage validation are still required.
