# Storage driver fixes — one-PR implementation handoff

**Canonical plan:** [docs/plans/2026-10-17-storage-driver-lifecycle-172-175.md](docs/plans/2026-10-17-storage-driver-lifecycle-172-175.md)

**Tracking PR:** [#176](https://github.com/NathanNeurotic/Launchstation/pull/176) (draft, planning only). **Do not create a second branch or PR.**

**Issues:** [#172](https://github.com/NathanNeurotic/Launchstation/issues/172) iLink, [#173](https://github.com/NathanNeurotic/Launchstation/issues/173) MX4SIO, [#174](https://github.com/NathanNeurotic/Launchstation/issues/174) MMCE + audio, [#175](https://github.com/NathanNeurotic/Launchstation/issues/175) storage interference + dialog.

## Decision summary

- **MMCE:** two ping attempts *per port* using current 200 ms transaction timeout initially. A 100 ms timeout is an *optional hardware-gated optimization*, not an established safe universal default. With two empty ports, nominal alarm-wait budgets are ~800 ms (2×200×2) or ~400 ms (2×100×2) respectively; total operation time is greater and must be measured.
- **Never demote resident stack mode just because media is absent.** MMCEMAN retains hooks after it loads. Safe unload (if proven) or guarded IOP transition is necessary to change incompatible resident stacks; avoid speculative resets for browsing an absent device.
- **Animation:** show status before/after synchronous calls and while returned retry loops permit; do not move IOP module initialization to the background worker or pretend it can animate during a blocked call.
- **Audio:** generation-safe shutdown while the old IOP service still exists; restore through existing deferred maintenance after safe recovery.
- **UX:** no raw mount IDs in generic missing-device modal.
- **Tests:** next agent should add `tests/test_zack_storage_drivers.py` and extend reset/PAD, device, audio, UI and MMCE host tests. Verify that the newly built MMCEMAN IRX is actually embedded and test on real MMCE/MX4SIO/iLink devices.

The detailed implementation sequence, safety gates and test matrix are in the **canonical document linked above**. This root entry is an index to prevent multiple versions of the plan from drifting.

No bug fixes, completed background test results, or hardware verification are asserted by this handoff file.
