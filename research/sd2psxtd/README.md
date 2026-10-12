# sd2psXtd restore transaction — reference model only

**Safety status: NOT IMPLEMENTED ON DEVICE.** This C99 state machine is
deliberately isolated in the research directory and is **not** linked into
the PS2 ELF, Launchstation MMCEMAN IRX, or sd2psXtd firmware. Its capability
constant is MMCE_TXN_IMPLEMENTED_ON_DEVICE == 0. Do not advertise or enable
direct MMCE restore on the strength of passing host tests.

The canonical firmware is [sd2psXtd/firmware](https://github.com/sd2psXtd/firmware);
this research follows [main at 8a20bd9](https://github.com/sd2psXtd/firmware/commit/8a20bd991b43ac731882f5f85030b19e94c23a3b).
That repository is not writable by the connected GitHub account. No upstream
firmware branch, PR or actual firmware build is claimed.

## What's new

The adjacent model header and C implementation form a callback-driven
**reference** for a future exclusive lease and durable restore transaction.
Their state transitions can be host-tested before changing real emulation code.

| Phase | Model requires | Real firmware work still missing |
| --- | --- | --- |
| Begin | Exclusive slot lock, drain, exact backing identity + generation and verified external safety backup | Fence both cores, every PS2 MC access and ALL switch sources; drain PSRAM/SD writes and confirm durable flush; persistent object identity |
| Prepare | Verified, separate new image and a durable PREPARED journal record | SD backend needs tear-resistant, versioned records and verified full image/size/digest |
| Commit | Identity still matches under lease; durable COMMITTING journal before any change; recoverable commit and readback; FINISHED journal | Crash-consistent replace-or-rollback (ordinary sd_rename is insufficient evidence) |
| Abort | No target mutation; persistent ABORTED record if PREPARED | Safe cleanup of staged data |
| Reboot | Recover interrupted journal before emulated card opens, then mark FINISHED | Early boot replay/rollback with SD failure and corrupt journal fail-closed handling |

Every callback has mandatory safety prerequisites documented in the header.
The model **does not supply the callbacks**. A callback returning success
without meeting its contract would invalidate any safety claim. No release
occurs after a potentially interrupted commit, identity drift under lease,
or failed recovery.

The model permits two adapter units and requires a matching canonical
identity plus nonzero generation before prepare and commit. A random nonce
cannot substitute for an actual firmware-exclusive fence. Actual serialized
journal records need format version, length, CRC/integrity and power-loss-safe
storage; copying the compiler-dependent in-memory struct directly to SD is
unsafe.

## Code mapping to upstream

- src/ps2/mmceman/ps2_mmceman_commands.c/.h — negotiate new protocol
  capabilities and versioned transaction opcodes after upstream agreement;
  do not reuse 0x02, 0x03, 0x09 or 0x4e.
- src/ps2/mmceman/ps2_mmceman.c — block all card switching during the lease,
  including GameID, physical button, normal MMCEMAN and bootcard routes.
- src/ps2/ps2_cardman.c — expose exact backing object and monotonic generation
  under the lease, not just numerical card index.
- src/ps2/ps2_dirty.c and src/ps2/card_emu/ — drain dirty PSRAM writes, stop
  new emulated I/O and keep card inaccessible during recovery.
- src/ps2/mmceman/ps2_mmceman_fs.c — keep stage and journal separate from
  active cardman_fd, never truncate or overwrite an emulated backing FD.
- ext/ESP8266SdFatWrapper/src/sd.cpp — investigate and implement truly durable
  flush, commit and rollback behavior, including power interruption.
- Firmware early boot — replay any surviving transaction before mounting or
  exposing any memory card to the console.

See [the verified firmware audit](../../docs/MMCE_SD2PSXTD_FIRMWARE_AUDIT.md)
and [the restore safety protocol](../../docs/MMCE_RESTORE_PROTOCOL.md).

## Verification boundaries

The test tests/test_mmce_restore_txn_model.py compiles the reference functions
with strict host C warnings and runs fake fault-injection callbacks for lease
refusal, backup failure, staging errors, identity drift, journal failure,
interrupted commit, reboot recovery and fail-closed behavior.

Fake callbacks offer no SD/FAT, SIO2, PSRAM or card-emulation guarantees.
Passing does NOT prove power-loss durability or safety on a real PS2.
The Launchstation recovery GUI remains NON-DESTRUCTIVE.
