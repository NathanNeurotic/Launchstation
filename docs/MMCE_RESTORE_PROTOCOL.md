# Direct MMCE image restoration — safety protocol

## Current status

**Not enabled.** Launchstation can already save verified local/card-history snapshots and download verified cloud snapshots to **new USB/HDD `.mcd` files**, without touching active MMCE images. This remains the supported recovery path.

The present MMCEMAN/firmware interface cannot certify that an arbitrary `mmce0:/MemoryCards/...` or `mmce1:/MemoryCards/...` target is inactive and atomically replaceable. Do not infer that from a successful pathname lookup, `GET_CARD`, an idle `GET_STATUS` flag, or `RESET`.

## Verified firmware source

This plan is now grounded in the canonical [sd2psXtd firmware source audit](MMCE_SD2PSXTD_FIRMWARE_AUDIT.md), pinned to upstream commit `8a20bd991b43ac731882f5f85030b19e94c23a3b`. That audit distinguishes actual wire commands and host/firmware behaviors from proposed future transaction features. Existing `GET_STATUS`, `GET_CARD`, and `FS_RENAME` must not be promoted to restore guarantees without firmware changes.

## What the existing MMCE commands actually establish

- `GET_STATUS` supplies firmware status flags; the current active-card export checks its busy bit before probing. **Idle does not certify a selected backing filename or exclude later activity.**
- `GET_CARD` supplies numeric card selection state, **not an authenticated absolute backing path with a frozen generation**.
- `RESET` resets the firmware filesystem descriptor table (apart from its cardman backing descriptor). It is **not** a lock or a safe way to unmount a game-active card for replacement.
- `__active_card__.mcd` is a synthetic **read-only** handle identified through the firmware's surviving backing descriptor. It is specifically **not** a writable restore destination or proof that another file is inactive.
- `FS_RENAME` exists, but its atomicity, crash durability and card-emulation coordination are unproven here. Simply renaming a staged file over an existing active card is not a safe commit protocol.

This module now reserves the synthetic active-card pathname against mutation and refuses paths that overflow the firmware's 8-bit on-wire length. These are defensive checks, not the missing proof of in-place restoration safety.

## Host-tested reference for a future firmware transaction

A non-production C99 [transaction reference model](../research/sd2psxtd/README.md) now exercises exclusive-lease ownership, exact backing identity + generation checks, a verified external safety-backup requirement, prepare/commit journaling and fail-closed cold-boot recovery. It deliberately contains **no implementation of SD durability, SIO2 fencing or card emulation quiescence** and is not linked into the shipping executable or upstream firmware. All real callbacks and power-loss validation remain required. Passing host-model tests does not enable an in-place restore GUI.

## Required transaction before enabling direct MMCE restore

| Phase | Required guarantee | Failure policy |
| --- | --- | --- |
| 1. Select | User names a verified local `.mcd` recovery image and an explicit destination card | No implicit latest-card or GameID assumptions |
| 2. Preserve | A second verified safety snapshot of the **exact** target is durably stored outside MMCE, including original identity | Missing/unreadable backup blocks restore |
| 3. Quiesce | Firmware confirms that game/card emulation has stopped writing, with exclusive ownership retained throughout commit | Unknown, busy, switched or timed-out state blocks restore |
| 4. Identify | Firmware resolves the exact target filesystem object to an active/inactive identity and version, independently for each MMCE unit | Numeric card/channel IDs alone are insufficient |
| 5. Stage | Fully write/verify replacement under an unused **different** filename, leaving target intact | Never truncate or replace target during staging |
| 6. Commit | Adapter provides a documented atomic replacement **or** an explicit crash-recoverable journal/rollback protocol, protected by the same exclusive lease | No overwrite without guaranteed recoverability |
| 7. Revalidate | Reopen and hash/validate committed contents, confirm the expected card identity and record completion | Preserve external safety copy and recovery journal on any error |

Power interruption, SIO2 timeout, IOP reset, card switching, PS2 reboot and removal of storage during each phase must be modeled and hardware-verified. The on-card backup cannot substitute for the required **external** safety copy.

## Implementation policy

1. Keep direct in-place restore **disabled** until the firmware exposes and documents quiesce + exact object identity + durable commit semantics.
2. Do not add a timed guess (sleep, retry or a `GET_STATUS == 0` shortcut), copy data into a selected `.mcd` with truncate, or treat ordinary FS rename as atomic without evidence.
3. Preserve normal read/backup, card selection, device discovery, GameID and dual-USB behavior.
4. Continue supporting verified, independent recovery images on USB/HDD for manual inspection and offline installation.
5. Put any future destructive implementation in a dedicated PR with negative/fault-injection tests before hardware work. Roll it out separately from the GUI and USB reliability changes.

## Source locations

- Firmware command IDs and responses: `iop/mmceman/include/mmce_cmds.h`, `iop/mmceman/src/mmce_cmds.c`.
- Synthetic active-card read path, filesystem forwarding and rename: `iop/mmceman/src/mmce_fs.c`.
- Targeted and retained history: `src/mmce_backup.c`.
- Verified external recovery: `src/dash_browser.c`, `src/cloud_google.c`.
- Defensive mutation guards: `iop/mmceman/include/mmce_restore_guard.h`.

The MMCE filesystem and card selection behavior are based on the sd2psXtd MMCEMAN/MMCEDRV interface; consult the corresponding firmware implementation before adding new commands.
