# USB stability audit, 2026-10-08

Published baseline: `2790d91dbada146a3d41b9634a92a7ac13f9fde0` (PR #80).
Historical comparison: `06c32f9f9a8b1b71e3726047bb93bf78539ce540`.
The user reports USB freezes/non-detection across launch devices, usually booting
from memory card. Hardware reproduction remains necessary.

## Reproduced source failures and candidate fixes

1. Explicit USB activation reset the whole IOP after only 800 ms of failed
   enumeration. Repeated automatic failures could do the same. Missing media
   is insufficient evidence of a poisoned stack. The host harness demonstrates
   that an empty USB probe destroys unrelated resident services on the published
   baseline. Remove both automatic reset branches, including the module-load
   failure branch: a USB probe now reports failure without resetting all services.
2. Runtime USB activation invokes `reopenPadPorts()`. Its two outer attempts call
   `setupPad()`, whose state waits previously had no deadline or yield. A
   permanently busy pad prevents even the first attempt from returning. Add
   one-second yielding state deadlines and reject an unsettled state in setup.
   Both single-port and any-port waits are covered by the executable harness.
3. Extend USB enumeration from five to eleven checks, 200 ms apart (up to two
   seconds of scheduled waiting). The harness detects media arriving at 1.8 s
   without an IOP reset. Absent media takes longer to report failure, but avoids
   the much more disruptive reset/reload cycle. Successful initial checks return
   immediately.
4. Startup loaded USB before other device families. A later stack reset could
   remove already loaded devices; MX4SIO activation could clear USB discovery.
   Run USB discovery last and replay the startup pass once if its IOP generation
   changed. The harness reproduces a later network-loader reset and MX4SIO cache
   invalidation, and checks earlier storage plus USB are available afterward.
5. Dashboard input polling and flushing called `waitAnyPadReady()` even though
   the read routine already skips unsettled ports. Remove these waits from the
   dashboard frame path; busy controllers no longer stall drawing. Retain bounded
   waits for explicit setup/legacy callers. The harness checks busy-port polling
   and flushing use no state waits, alongside existing repeat/held-action tests.
6. Loading overlays draw the normal scene, whose uncached theme textures could
   immediately submit worker file IO after driver startup had drained the worker.
   During a loading overlay, allow cached textures but defer new theme-image IO.
   A production-function harness verifies no job is submitted while loading and
   that loading resumes afterward.

Run `python tests/test_device_input.py 2790d91` to exercise the same assertions
against the published baseline: absent-media, dashboard input, startup ordering
and theme-IO assertions fail, and the stuck pad harness times out. Run without a
revision to test the candidate.

## Broader audit findings and limits

- Compared the previous-day source history, including USB/pad recovery, polling,
  graphics performance changes and audio handoffs. Compared USB loader/discovery
  behavior with `saildot4k/wLaunchELF_R3Z`. The reference has no dashboard-driven
  whole-IOP reset for absent USB media. Preserve existing module order and the
  hardware-motivated pad rebind pending console testing.
- The dashboard's periodic USB poll reads a cache; it does not perform fresh
  discovery. Startup now refreshes after cache invalidation and replays a changed
  IOP generation. Fresh discovery for idle hotplug remains separate; explicit
  activation/browsing refreshes discovery. Continuous synchronous rescans would
  add blocking storage RPCs to the frame loop and are not introduced here.
- The shared `massN:` namespace is not a reliable USB identity. Keep USB-specific
  paths separate from generic BDM mount ordering when diagnosing hardware.
- Deadlines bound the pad state polling loops. They cannot interrupt an IOP RPC
  that never returns (`padPortClose`, module startup, or `fileXioGetStat`). USB
  enumeration waiting is bounded; individual blocking RPC duration is not.
- CI installs/updates the live ps2build SDK. An older source rebuild is therefore
  not guaranteed to reproduce an older released binary or embedded drivers.
- Preserve the abandoned media/cloud worktree and unrelated local changes.

## Console acceptance gate

Test this candidate with the same console, USB device, cable and configuration:
memory-card boot with USB absent, present at boot, inserted after boot, explicit
activation, repeated browsing/copying, and return from applications. Verify wired
input remains responsive and other enabled storage/network devices remain
available after a failed USB probe. Repeat the failing scenario with an older
known-good binary if available. Host tests/build/CI do not establish console
stability; keep this candidate separate from `latest` until that evidence exists.
