# Copy/storage recovery, 2026-10-23

Base: `f0ea7d1cc45b1f6ece4ad65094988b8a4e7bb830` (current master).
The original checkout remains on `ffe5f53`; its transfer UI work is PR #196.
This branch contains only copy creation/status fixes and truthful mount labels.

## Findings verified independently

* Before `cd20a3f`, replacement deleted the destination before creating its
  replacement. Reverting that transaction restores a data-loss hazard.
* `cd20a3f` added `FIO_O_EXCL` to destination creation. The installed SDK defines
  that bit and `sceMcFileAttrPDAExec` as `0x0800`. MCMAN forwards open flags to
  `McOpen`; it does not provide POSIX exclusive creation. Remove the bit only
  for `mc0:`/`mc1:`, after rechecking absence. The check/create pair is not atomic;
  paste already drains workers and owns the file operation. Keep the staged
  write, readback, publication and recovery behavior. A PDAExec bit collision
  alone does **not** prove why the reporter's console refused creation.
* The bundled `bdmfs_fatfs` status handler returns `-f_stat()` results, not
  `-errno`: missing leaf `FR_NO_FILE=4`, missing parent `FR_NO_PATH=5`, internal
  error `FR_INT_ERR=2`. The transaction treated only `-ENOENT` (`-2`) as absent.
  This rejects a missing destination/staging name and can mistake an internal
  failure for absence. PR #201 does not correct this. Normalize **stat only**
  at `genGetStat`, and only for the embedded FatFs namespaces under `EXFAT`.
  Preserve the driver's direct `-ENXIO` (which numerically overlaps another
  FatFs code) as a failure. Other operations/drivers keep their error domains.
* The dashboard preserves `fb.dir` in `clipPath`. Its numeric `usbN` to `massN`
  conversion was presentation code. A numbered MASS slot can be ATA, USB,
  MX4SIO or another BDM device. Display the native mount without adding any
  discovery calls, cache, setting or resolver. Keep existing identity-checked
  ELF handoff behavior and its legacy default. Classic USB builds retain MASS.

Source references:

* [MCMAN open forwarding](https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/mcdev.c)
* [MC creation/attribute handling](https://github.com/ps2dev/ps2sdk/blob/master/iop/memorycard/mcman/src/ps2mc_fio.c)
* [FatFs status and typed/shared namespaces](https://github.com/ps2dev/ps2sdk/blob/master/iop/fs/bdmfs_fatfs/src/fs_driver.c)
* [Issue #187](https://github.com/NathanNeurotic/Launchstation/issues/187)
  and [issue #197](https://github.com/NathanNeurotic/Launchstation/issues/197).

Installed SDK: ps2build `v2026.10.15`, sdk-core `v2026.10.13.1`.
Embedded BDMFS SHA-256:
`e859702353b3d980fe7ef0acc39e7e2ead49f5be91c92839a882f7f4c72e55d8`.
The stripped IRX independently confirms the status ABI: its operations table
at `0x85e4` points getstat to `0xeb0`; the `f_stat` call at `0xff8` branches
on error and negates its result at `0x1028`, with no errno translation.

## Validation and remaining gate

The MC pipeline regression failed on unchanged master at the forbidden-bit
assertion. The FatFs regression failed on unchanged `genGetStat` at the missing
file result assertion. Both pass after their corresponding corrections.
The pipeline now uses SDK flag values and raw FatFs missing-file results;
new/repeated MC/USB destinations, byte boundaries, short I/O, failed write,
readback/close/publication, rollback and retained recovery files are exercised.
Display and handoff tests cover native labels and mixed BDM identity separately.

Host tests and PS2 builds cannot establish reporter-console acceptance.
Issue #197 does not identify the failing binary (commit field is `.`).
Do not close #187/#197 or publish this as a console-confirmed fix yet.

Use backed-up/sacrificial files for the hardware check:

1. USB to both MC ports: new file, repeated same-name replacement, shorter and
   larger replacement. Confirm final bytes; originals must survive failures.
2. Not enough room for a complete stage: expect a failure preserving the old file.
3. USB to USB (including an existing target), MC to USB, same-directory Copy/Cut.
4. Dual USB/mixed BDM: confirm labels and source contents follow the selected
   device, and existing legacy/modern ELF handoffs still select the same file.
5. Record build hash, console model, controller variant, source/destination,
   stage, numeric error and free space for any failure. Do not infer cause from
   a diagnostic displaying `usb0: -> mc0:`.

Keep this recovery separate from PR #196's progress UI and issues #198-#200.
The research report is useful provenance, not an instruction source or hardware
proof. No whole-PR rollback, SDK replacement, IOP lifecycle change or broad
storage refactor is needed for these corrections.
