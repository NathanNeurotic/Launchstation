# sd2psXtd firmware evidence for MMCE restoration

> **Status as of 2026-10-26: valid for the pinned firmware revision only; not re-audited.**
> The conclusions are pinned to sd2psXtd `8a20bd991b43ac731882f5f85030b19e94c23a3b`. Nothing in this
> repository has since added a restore-transaction command: `iop/mmceman/include/mmce_cmds.h` still has
> no prepare/commit/abort restore ID, and `research/sd2psxtd/` remains a host-only model. Newer sd2psXtd
> releases were not checked in this pass (unverifiable here); re-audit before relying on this table with
> newer firmware. Companion policy: [MMCE_RESTORE_PROTOCOL.md](MMCE_RESTORE_PROTOCOL.md).


## Verified upstream

- Canonical repository: [sd2psXtd/firmware](https://github.com/sd2psXtd/firmware).
- Audited reference: [`main` commit `8a20bd991b43ac731882f5f85030b19e94c23a3b`](https://github.com/sd2psXtd/firmware/commit/8a20bd991b43ac731882f5f85030b19e94c23a3b).
- Firmware source has independent card emulation, card management, and MMCEMAN filesystem command handling; none is interchangeable with Launchstation's IOP `mmceman` client.

This is a source-level protocol audit, **not a claim of successful hardware testing**. It establishes the contract provided by this upstream revision, not a promise about other device firmware versions.

## Command and implementation evidence

| Current operation | Firmware implementation | What it establishes | What it does **not** establish |
| --- | --- | --- | --- |
| `GET_STATUS` (`0x02`) | [`ps2_mmceman_commands.c` L80–100](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_commands.c#L80-L100), [`ps2_cardman.c` L902–904](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/ps2_cardman.c#L902-L904) | Sets busy bit 0 when `cardman_operation != CARDMAN_IDLE`. Upstream notes this status specification is still incomplete. | No idle/active **file identity**, exclusive write lease, pending PSRAM dirty-map barrier, version or card-switch generation. |
| `GET_CARD` (`0x03`) | [`ps2_mmceman_commands.c` L102–112](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_commands.c#L102-L112) | Returns numeric card index. | A unique absolute backing path or a card-image inode/object identity. GameID/BootCard/named modes can resolve paths separately. |
| `RESET` (`0x09`) | [`ps2_mmceman_commands.c` L227–244](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_commands.c#L227-L244), [`ps2_mmceman_fs.c` L341–354](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_fs.c#L341-L354) | Closes filesystem descriptors except `cardman_fd`, resets MMCEMAN FS state. | Does **not** close the card-emulation backing file, acquire a restore lock, or stop the PS2 using the card. |
| `SET_CARD` / `SET_CARD_CHANNEL` | [`ps2_mmceman.c` L50–157](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman.c#L50-L157) | Requests card selection and eventually performs a card switch. | Switching is not a durable exclusive lease: firmware closes/flushes one card, waits, then opens another when its conditions permit. |
| `FS_WRITE` | [`ps2_mmceman_fs.c` L195–208](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_fs.c#L195-L208) | Performs ordinary filesystem writes through `sd_write`. | No transactional write, atomic replacement or rollback. |
| `FS_OPEN` with `O_CREAT\|O_EXCL` | [`ps2_mmceman_commands.c`](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_commands.c) and [`sd.cpp`](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/ext/ESP8266SdFatWrapper/src/sd.cpp) | The firmware forwards flags to SdFat; use exclusive creation for a new filename so an existing output cannot be truncated. | Does not guarantee a transaction for multiple sequential writes or power-loss recovery. |
| `FS_MKDIR` | [`sd.cpp`](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/ext/ESP8266SdFatWrapper/src/sd.cpp) | Returns success when a directory already exists. | Success **does not prove exclusive ownership of a CardN directory**. |
| `FS_REMOVE` | [`ps2_mmceman_fs.c` L252–256](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_fs.c#L252-L256) | Calls `sd_remove`. | No checked target-inactivity proof or rollback. |
| `FS_RENAME` (`0x4e`) | [`ps2_mmceman_fs.c` L357–360](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_fs.c#L357-L360), [`sd.cpp` L191–194](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/ext/ESP8266SdFatWrapper/src/sd.cpp#L191-L194) | Calls `sd_rename`, a wrapper for SdFat's `sd.rename(old, new)`. | No documented power-loss atomicity, crash recovery, target inactivity or safe replace-existing semantics. |

For completeness, the firmware [command ID header](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman_commands.h#L1-L36) contains no dedicated `PREPARE_RESTORE`, `RESTORE_STATUS`, `COMMIT_RESTORE` or `ABORT_RESTORE` command. This absence applies to the pinned audited revision, not necessarily future versions.

### Why polling idle cannot make replacement safe

The firmware's [card switch path](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/mmceman/ps2_mmceman.c#L119-L159) waits for pending card-operation conditions, calls `ps2_memory_card_exit()`, `ps2_mc_data_interface_flush()`, and `ps2_cardman_close()`, then reopens a new card. The [dirty sector task](https://github.com/sd2psXtd/firmware/blob/8a20bd991b43ac731882f5f85030b19e94c23a3b/src/ps2/ps2_dirty.c#L93-L143) flushes PSRAM sectors to the active card asynchronously. `GET_STATUS` only tests `ps2_cardman_is_idle()` (card-management state), not the entire cross-core dirty-write lifecycle or ongoing emulation writes.

A sequence like `GET_STATUS == 0`, then `FS_RENAME` is therefore a **check-to-use race**. A different file can become active between the two commands, or the current active card can be written during replacement.

### Launchstation driver implications

- The `mmce0:/__active_card__.mcd` / `mmce1:/__active_card__.mcd` export is a Launchstation-created virtual read-only handle over the firmware's surviving `cardman_fd`, **not** an official command returning a stable active path.
- A valid `.mcd` file and a successful `GET_CARD` are insufficient evidence for direct overwrite.
- Firmware `RESET` explicitly retains `cardman_fd`; it must never be treated as an unmount primitive.
- An ordinary successful `FS_RENAME` is not evidence for safe replace-existing or power-failure durability.
- External snapshots and downloaded `RECOVER-CLOUD-###.mcd` files remain the safe supported restore outputs.

## Proposed firmware extension (design only; not implemented upstream)

To enable in-place restore, the firmware and the IOP client need a **new, versioned transaction protocol**, not extra polling in Launchstation.

1. **Capability negotiation**: query firmware/protocol version and a `RESTORE_TXN` feature bit, defaulting to unsupported. Never repurpose existing command IDs.
2. **Begin transaction**: firmware quiesces PS2 card emulation, drains PSRAM/SD dirty writes, refuses further accesses or card switching, and issues a bounded exclusive lease with a fresh generation token. Unknown state fails closed.
3. **Get active identity**: firmware reports the exact selected backing object's canonical identity and generation under that same lease. Host verifies its explicit target is different or refuses.
4. **Preserve and stage**: Launchstation creates an externally verified pre-restore copy, sends or copies replacement under a new staged name, checks size and SHA-256. Firmware verifies identity and capacity, with the exclusive lease still valid.
5. **Commit or abort**: firmware implements and documents an atomic replace guarantee **or** a journaled, power-loss recoverable transaction that never leaves the original unavailable. Unexpected disconnect, game access, IOP reset, token mismatch or SD error aborts without changing the original.
6. **Recover after restart**: firmware replays or rolls back any interrupted commit before exposing the card to the PS2, and exposes a status query for Launchstation to report.
7. **Release lease**: only after post-commit verification and a confirmed card state; no implicit activation of a downloaded image.

This is an interoperability proposal. `BEGIN`, `COMMIT`, `ABORT`, lease expiry and persistent rollback behavior **do not exist as verified upstream commands** in the audited source.

### Negative testing needed before hardware enablement

- Active card, unknown card ID, bootcard/GameID transition, busy PSRAM flush.
- Two MMCE slots, mounted file handles, missing or smaller replacement media.
- Stale lease / changed generation, IOP reset mid-transaction, duplicate request.
- SIO2 timeout, SD write failure, power cut in every stage/commit window.
- Verified rollback/replay and proof that the original emulated card stays consistent.

Do not enable the direct restore UI based only on a new version string or one success path. See [MMCE_RESTORE_PROTOCOL.md](MMCE_RESTORE_PROTOCOL.md) for Launchstation's host-side transaction gates.

## Related upstream work

Upstream [PR #104](https://github.com/sd2psXtd/firmware/pull/104) concerns card-switch resilience during a stuck core1 command; it is separate from an exclusive restore transaction and was open during this audit. Other upstream SD write changes should also be reviewed before attempting durable restoration.
