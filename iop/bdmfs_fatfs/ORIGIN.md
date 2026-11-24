# FAT/exFAT driver provenance

Source: ps2build/sdk-core tag `v2026.11.11` (commit `053450c`), `core/bdmfs_fatfs/src`, the
driver Launchstation embedded before this copy: the PS2SDK filesystem layer over FatFs R0.16
(ChaN, its own licence in `src/fatfs/ff.c`). Copyright notices are kept; licence AFL-2.0
(LICENSE). The first commit of this folder is that source byte for byte.

Built by the `bdmfs_fatfs_fork` target in ps2.yaml and embedded in Launchstation under the SDK
name `bdmfs_fatfs`. It serves every FAT32 and exFAT volume: `mass:`/`usb:`, `mx4sio:`, `ilink:`,
`ata:` and `cf:`. The wLE IGR helper keeps the SDK binary; it copies no files.

Change: one request, `BDMFS_IOCTL2_RESERVE` (`include/bdmfs_reserve.h`), that gives a new, empty
file one unbroken run of clusters of its final size.

- `src/fatfs/include/ffconf.h`: `FF_USE_EXPAND 1`, which builds FatFs's own `f_expand`.
- `src/reserve.c` (new): `fs_reserve_contiguous`, a call to `f_expand(file, size, 1)`.
- `src/fs_driver.c`, `src/include/fs_driver.h`: the `ioctl2` case, under the filesystem lock,
  for a handle open for writing only.

Why: FatFs's ordinary allocator takes the next free cluster after the last one it handed out,
one cluster at a time, and never looks for a run long enough for the file. On a volume with
holes left by deleted files, a large file is spread over those holes. Neutrino reads game
images through a table of at most 64 runs and refuses a file in more pieces. The file manager
(`src/filer_copy.c`) and the PC game receiver now ask for the run before the first write. When
the volume has no free run that long the request answers 0, the file is still empty, and it is
written as before.

The directory entry keeps size 0 until the file is closed, as with ordinary writes, so a copy cut
short by power loss is not left looking complete.

`tests/test_contiguous_copy.py` runs these FatFs sources on the host on FAT32 and exFAT volumes
with holes: 82 and 81 runs without the request, 1 with it. That is a host test of the allocator.
It is not a console, card or i.LINK test.

Remove this copy when the SDK driver offers contiguous allocation itself.
