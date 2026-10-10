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

## Console result for 5189e10: USB directory completes, wired pad lost

The user reports usb0:/ files appeared and the application kept animating, but
wired pad input died and the analog light turned off. This candidate fails
console acceptance. Completed listDone releases the USB input gate, so this
report is not explained by the deliberate suppression during directory IO.
Do not reinstate blanket pad close/end/reopen RPC as a speculative recovery.

Add a temporary File Browser status line showing cached PAD state/request for
both ports, IOP reset generation and worker IO state. libpad state/request
queries inspect EE DMA buffers; rendering uses a UI-thread snapshot and issues
no RPC or driver load. Capture one failing-screen photo with the complete device
set. This is instrumentation, not a claimed fix. Reference RiptOPL also vendors
an older usbd_mini rather than our current SDK full usbd, which remains a lead
requiring a controlled comparison. Controller reconnect behavior is unknown;
the user cannot test it now.

## Console result for 110be3f: fails during synchronous USB media probe

The new screenshot stops at Checking USB media..., before the directory and
its pad diagnostic are visible; analog light goes off. The scan calls
fileXioGetStat on usb0:/ and usb1:/ synchronously. Outer retry deadlines cannot
interrupt that RPC. No conclusion about a specific port or IOP fault is proven.

Controlled comparison: switch only the launcher's embedded USBD from the SDK
full driver to the same SDK's usbd_mini variant. Keep USBMASS_BD, BDMFS, media
probe and pad session code unchanged. This compares host-driver variant/resource
footprint, NOT the older version RiptOPL vendors. Loader bootstrap's driver is
unchanged and this comparison should be tested by launching from memory card.
The mini variant may have different USB-class compatibility; do not merge or
call stable without console acceptance. Two directly attached mass-storage
devices are within its configured capacity. No source-harness can reproduce the
observed IOP/pad failure; CI validates build/package only for this comparison.

Local SDK binary inspection: full and mini IRX files are both 26,225 bytes,
with identical text/data/BSS sizes. The difference is runtime pool sizing:
full devices/endpoints/general TD/iso TD/hubs/ports = 32/64/128/128/8/8;
mini = 16/32/64/64/4/4. Callback/HCD priorities remain 36/30 in both.
This comparison reduces configured runtime pools, not executable size.
The generated Ninja BIN2C input was checked to point at usbd_mini.irx.

## Mini host build console pass and missing second USB

User reports 419e3c1 usable but only one USB entry. Which filesystem is visible
is unknown. Confirmed source bug: probeUsbMediaAnimated returns on the first
nonzero mask; activate then bypasses prepareUsbRootBrowse and directly opens the
first cached unit. The second independently enumerating drive can be missed.
Keep the existing 20-pass/200ms bounded window until both supported USB unit
bits are present, accepting a lone drive at expiration. No new frame-time IO.
Regression harness gives unit 0 immediately and unit 1 after one second: fails
419e3c1 and passes corrected source. Console discovery of both formats remains
pending; no conclusion that every missing-drive case has this cause.

## UDPFS first, then two-drive USB: intermittent console freeze

User queues a UDPFS file in clipboard, then starts USB; screenshot stops at
Checking USB media... and analog light goes off. Reverse order does not seem
to trigger it. Hardware fault is possible but not established. Preserve the
released 58d4514 baseline and use separate codex/udpfs-usb-transition candidate.

startSync drains the worker before module/probe calls; no overlapping browser
worker request established. Resource comparison restores SDK-default fileXio
transfer buffer instead of reserving 128KB. Upstream SetRWBufferSize frees the
old buffer before allocation and leaves rwbuf NULL if allocation fails; our
fallback ultimately claims keeping default even though that is not guaranteed.
Remove tuning entirely, matching RiptOPL's absence of this call. This saves IOP
heap relative to 128KB configuration; actual heap exhaustion on this console
is NOT proven. Network throughput may decrease. USB host, discovery and pad
lifecycle remain as baseline. UDPFS-first copy test remains the hardware gate.

## Earlier USBD hypothesis: not isolated or hardware-cleared

Update for unified PR #93: the user reproduced the intermittent freeze with
the successful Actions build at 246782649df36df71726940bd335b5bf847b9a25
(run 38005338377). That build already includes usbd-ra, four bounded USB
probe passes and the libmc drain before deliberate IOP reset. The earlier
root-cause claim below was too strong: the driver substitution has not
eliminated this console failure. No navigation order is a reliable trigger.

### Diagnostic-head console failure (2026-10-09)

The user reproduced the freeze with the diagnostic text visible:
`Checking USB root usb0:/`. Music continues, the wired controller's analog
LED goes out, and all screen animation and the clock stop. Two USB drives,
MX4SIO, ATA/APA, MMCE and Ethernet remain physically connected throughout.
Physical connection does not establish which drivers were activated; do not
infer a simultaneous MMCE/MX4SIO software stack from this inventory.

The pre-call frame completed, but the post-call result frame did not appear.
The immediate suspect is `fileXioGetStat("usb0:/", ...)` or the IOP services
needed to complete it, rather than a retry delay or second-slot probe. BGM
is uploaded looping SPU2 ADPCM; continued music does not prove that IOP
threads, PADMAN or RPC are progressing. The controller symptom also prevents
calling this merely a rendering failure.

Upstream ps2sdk source audit at
`2c670453980fcc3fe46ead6399b730b12c8556eb`: BDMFS `fs_getstat` handles root
paths using mount metadata, before the filesystem lock and `f_stat` disk IO.
This makes a claim of a root-directory sector-read hang premature. The exact
packaged IRX must still be correlated with that source. The packaged IOMANX
disassembly uses the array device table, so an observed source hazard in the
optional linked-list implementation is not grounds to patch this build.
Neither heap exhaustion, USBD stack overflow nor a device-power fault has
been established. Run 38010230432 completed successfully for diagnostic head
`c87bb5633734e5dc9057eed5c340307cb4dc8f8f`; that is build evidence only.

The follow-up adds explicit pre-RPC USB slot status, return codes, and an
explicit status before draining a pending libmc poll. The UI presents the
stage before entering the blocking call; worker callers report status without
drawing. Debug builds also emit paired begin/end records. This is diagnostic
instrumentation, not an RPC timeout or a proven freeze fix. Extra frames can
change timing, so a successful run alone does not clear the regression.

User reports that USB hangs intermittently: opening UDPFS first then USB froze at Checking USB media with analog LED off; doing MMCE first then USB worked fine; USB directly sometimes hung.

The earlier investigation used RiptOPL's configuration as a driver candidate:
On 2024-09-04, upstream ps2sdk rewrote its USB host driver (commit b1f7ff96, \'USBD feature update\'). On real hardware, that rewritten driver intermittently loses controller communication (taking down SIO2/PADMAN, turning off the analog LED) and hangs the IOP during USB transfers. Both ps2sdk\'s full usbd.irx and usbd_mini.irx in current toolchains come from this rewritten codebase. RiptOPL resolved this exact hardware regression by vendoring the pre-rewrite driver (ps2sdk commit 314d87e7 as of 2024-04-01) built with -DMINI_DRIVER and -DUSE_GP_REGISTER=0 (usbd-ra).

We have vendored usbd-ra into iop/usbd-ra and configured ps2.yaml to build it as target usbd_ra, embedding it in both wLaunchELF and wle_igr in place of ps2sdk\'s prepackaged usbd_mini.irx.
