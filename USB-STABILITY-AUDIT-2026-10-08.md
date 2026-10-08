# USB stability audit, 2026-10-08

Published baseline: `2790d91dbada146a3d41b9634a92a7ac13f9fde0` (PR #80).
Hardware update: the user tested the a50cb4e PR #81 ELF and reported a black
screen. That release is withdrawn. The boot-repair candidate retains the USB,
startup, input and theme-IO corrections but restores legacy boot pad waiting.
Only explicit runtime `reopenPadPorts()` enables a yielding deadline. Visible
boot-check markers identify pad, keyboard, dashboard and audio boundaries.
The deadline change was the only PR #81 change executed during initial pad
setup before the dashboard; its causal role remains a hypothesis until the
repair candidate is tested on the same console.
Historical comparison: `06c32f9f9a8b1b71e3726047bb93bf78539ce540`.
The user reports USB freezes/non-detection across launch devices, usually booting
from memory card. Hardware reproduction remains necessary.

## Runtime USB report and shared BDM repair

The boot-repair candidate reached the File Browser on the user's console, then
froze at Starting MASS (USB). The user observed the analog LED cycle off/on/off.
Connected devices: two USB disks (exFAT and FAT32), exFAT MX4SIO, internal ATA
exFAT and UDPFS. This localizes the next failure to runtime USB activation;
it does not establish the exact blocking RPC.

Compared the separate C++ RiptOPL checkout at
49e4a02ff78b127880ee63d493deb7b85c16471f: USB mass activation does not close/end
the wired pad session. Remove our USB-triggered `reopenPadPorts()` and the unused
duplicate teardown helper. A state-poll deadline never protected blocking
`padPortClose`/`padEnd` RPCs. Also forbid module activation from the file worker.
Stages now name core IO, USBD, BDM, BDMFS, USBMASS_BD and media checking before
blocking work, so a remaining freeze identifies its last stage.

Found numbered `massN:` paths being rewritten to `usbN:` and having USB residency
required. In EXFAT builds, preserve numbered shared-BDM paths through normalization,
directory enumeration and readiness checks. Do not scan/start USB to access an
already numbered slot. The unnumbered legacy `mass:` alias remains USB discovery
for compatibility, and explicit `usbN:` remains USB-specific. Generic numbered
slots do not establish backing transport or media presence; their actual open
must succeed. A missing namespace is not grounds to guess and load USB.

Executable tests exercise all ten numbered slots, missing shared-core readiness,
USB core reuse, repeated activation, failed block-driver load, and worker rejection.
They verify separation of namespace and lifecycle, not physical exFAT/FAT32 IO.
Same-console acceptance with the full listed device set is pending.

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
   one-second yielding state deadlines only during runtime reopening, and reject
   an unsettled state in setup. Preserve boot/legacy readiness waits. The harness
   covers runtime timeout and legacy negotiation without sleeps or a deadline.
3. Extend USB enumeration from five to twenty checks, 200 ms apart (up to four
   seconds of scheduled waiting). This accommodates multi-device setups (such as
   simultaneous exFAT and FAT32 USB drives) where SCSI inquiry, capacity checks
   and partition discovery take longer than two seconds. Successful initial checks
   return immediately.
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
7. Safe DEV9 shutdown before IOP resets: `Reset()` now idles open storage and
   powers down the DEV9 interface (`DDIOC_OFF`) before invoking `SifIopReset()`.
   This prevents hardware hangs in `SifIopSync()` when internal ATA exFAT or
   network adapters (such as UDPFS) are active.

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
