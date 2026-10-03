# Dashboard report follow-up

This tracks the supplied `wlaunchelf issues.txt` report. Reports are leads,
not proof of a cause. The changes below address defects demonstrated by
source inspection and host regressions. Neither a host test nor a compiled
ELF proves the console symptoms are resolved.

## Implemented in this follow-up

* Background music is an optional `BGM.ADP` beside the boot ELF. Both build
  paths exclude the embedded track; CI/release packaging includes the loose
  asset. Missing/invalid music does not disable interface sounds. Reads are
  deferred during worker IO or inactive boot drivers, attempted once per
  audio session, and retried on an explicit music off/on or IOP reset.
* Disc boot paths preserve the real read-only boot folder for sidecars while
  retaining the writable configuration fallback.
* Controller reads clear failed/disconnected samples and initialize both
  return values. No-repeat reads reset repeat timing when held keys change.
  Analog input no longer replaces physical button state on alternating
  samples. Dashboard action buttons require a fresh press; navigation keeps
  its existing delayed repeat. USB keyboard/autotest input remains supported.
* Optional-device cards no longer equate loaded drivers with attached
  storage. Explicit root probes establish readiness, with separate MMCE slot
  results and a two-second retry window for ATA/MX4SIO mount settling.
  Failed probes also hide stale cached apps. Probe results expire on IOP
  reset. Root opening does not prove a disk's filesystem is fully healthy.
* Unknown/busy/absent device cards are dimmed. Unfocused cards no longer
  receive a constant selection-colour tint. Free-space labels use MB/GB;
  small file sizes still use B/KB. The device-list capacity accommodates
  both MMCE slots and all other existing cards.
* Back from a Settings subpage plays Cancel. Confirming the Cancel choice
  in a dialog plays Cancel rather than Confirm.
* OSD orb/clock-background phase no longer freezes at 0.999 seconds when
  RTC fields stop ticking. It continues on the shared visual clock, rolls
  minutes/hours correctly and re-anchors to a valid RTC on recovery. This
  is visual fallback, not a replacement persistent wall clock.

## Report disposition

| Report | Current disposition / next work |
|---|---|
| Combine Home, Apps and Files under Devices | Design proposal, not a proven defect. Preserve library views, artwork, recents, favorites, bookmarks and launch keys when simplifying navigation. Not changed here. |
| Background stops in notifications/errors/browser | PR #17 already merged shared visual updates for native tools/modals. Existing regression retained. Console frame rate and blocking driver calls still need measurement; do not call all animation lag fixed. |
| Files contains two browsers | Source has a doorway screen plus the full browser, not two independent filesystem engines. Simplify the doorway/navigation as part of the layout work. |
| Fatal crash when starting exFAT HDD | Unresolved. Improved readiness is not a driver/crash fix. Need console model, exact ELF revision/hash, boot storage, disk layout and the last startup/IOP log or reproducible failing step. |
| ELF should approach 287 KB | Music externalized without removing features. Further reductions require a symbol/asset budget and explicit tradeoffs; a historical minimal build is not an equivalent feature profile. |
| Inconsistent sounds for the same action | Corrected the demonstrated Cancel-dialog and Settings Back cases. Audit remaining surfaces and measure audible latency on hardware. |
| Rename Home to Devices | Same layout decision as the consolidation request; avoid duplicate Devices labels. |
| Own clock for dead RTC battery | Visual phase fallback implemented and regression-tested. Accurate footer date/time across power loss still needs a manual/session-clock policy and persistence UX. |
| Grey-out broken | Driver residency no longer makes optional storage Ready; unknown/failed cards are dimmed. Hardware check remains. |
| Absent MX4SIO/MMCE/UDPFS/HDD appears detected | Root checks plus per-slot MMCE results implemented. Optional-family results are snapshots, not continuous hotplug monitoring. Recheck after attachment/server changes. |
| MB rather than KB for storage space | Implemented for storage free space, including sub-MB values. Individual file sizes retain appropriate units. |
| Unselected devices have selection light | Removed the constant accent tint. Focus movement still intentionally interpolates between adjacent cards. Compare on the affected display. |
| More readable description font | Needs comparison captures at the affected output mode and a readability pass on size/weight/contrast; no blind font replacement. |
| UI too sharp on 4:3 | 4:3 is an aspect ratio, not evidence of a particular artifact. Identify CRT/interlaced versus progressive/scaled output; review safe area, contrast and one-line details. |
| Missing Back sound | Settings subpage Back now plays Cancel. Other tool/section exits still need a semantic sound audit. |
| Flickering towers/cubes/shapes | Requires same-scene captures/frame timing across interlaced and progressive output. Distinguish temporal aliasing, thin primitives, alpha layers and GS cost before changing smoothing. |
| Separate orbit-light orbs | Visual preference/reproduction needed. Preserve the existing intended OSD clock choreography unless deliberately redesigned. |
| Orbs at sides of clock ring | Layout proposal; compare the intended reference and actual output before moving the group. |
| Triangle causes runaway input; suggests IOP reset | Source/host fixes cover alternating analog state, stale failed reads and action autorepeat. An IOP reset is not assumed to be the cause or remedy. Console reproduction remains. |
| Settings interfere with other LaunchELFs in mc0 | Shared `LAUNCHELF.CNF`/`IPCONFIG.DAT` paths remain. Plan a private namespace with read-only legacy import, preserving custom save paths and migration behavior. No silent rename here. |
| Sound/response slow | Input-state defects addressed. Measure input-to-visual and input-to-audio separately on hardware; these tests do not establish audible latency. |

## Reproducible validation

Run the eight `tests/test_*.py` files invoked by `.github/workflows/compile.yml`,
`actionlint .github/workflows/compile.yml`, `git diff --check`, and
`ps2build build`. Run `scripts/check_loader_layout.sh` against the produced
unstripped embedded loader with the installed EE `nm` tool.

`test_device_input.py` executes production controller/device code with mocked
hardware; its same assertions fail against `origin/master` (merged #17).
`test_bgm_sidecar.py` executes production sidecar loading/lifetime, boot-path
preparation and stack-residency guards, covering missing/short/invalid music,
partial reads, inactive/busy devices, toggle retry, buffer lifetime, IOP reset,
USB/HDD/disc paths and read-only disc configuration fallback.

## Console acceptance pass

1. Boot with no BGM file, then with the supplied file beside the ELF. Confirm
   the dashboard works either way, interface effects remain independent,
   and enabled music loops. Try a renamed Ogg/invalid file; it must stay silent.
2. Open/close dialogs and Settings repeatedly while holding Triangle/Confirm,
   including a tilted stick. Navigation should retain its initial delay;
   held actions must not toggle repeatedly. Test both controller ports and a
   disconnected/reconnected controller, plus USB keyboard input.
3. On a setup lacking optional storage, manually detect each family. Drivers
   alone must not show Ready. Test MMCE in each port separately, slow ATA/MX4
   mounting, an unavailable UDPFS server and a responding server. No root
   probes should occur merely while drawing the cards.
4. Switch incompatible MMCE/MX4 stacks (IOP reset), then return to boot storage.
   Check controller state, device readiness and sound/music recovery without
   stale RPC calls. Recheck optional devices after insertion/removal.
5. Retry the exFAT HDD crash separately with a recorded ELF hash and startup
   trace. Do not infer it is fixed from step 3 or a successful build.
6. Compare text, highlights, fine geometry and orb movement on the affected
   4:3 output. Record interlaced/progressive mode and motion settings.
