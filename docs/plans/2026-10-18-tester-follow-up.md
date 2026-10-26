# October 18 tester follow-up

> **Status as of 2026-10-26: implemented (PR #183, `82082b6`); tester confirmation pending.**
> Explicit *Save current clock*, quiet start-up status (`setStartupDriverStatusQuiet`) and the concise release
> page are in tree (`tests/test_settings_save_prompt.py`, `test_startup_status.py`, `test_release_notes.py`).
> #125 (one-minute rollback after power loss) and #138 (media-dependent boot text) were not re-verified by
> the reporters.


Base: `2836fdaf6214a1b592e245a092285d96210adc41` (includes #179 and #180).
PR #181's draft per-game profiles are separate and are not included here.

## Priorities and evidence

1. #175 still reports cross-storage trouble and a power-off crash. #174 confirms
   audio recovery is improved but reset/stall behavior remains; #173 reports
   background frame drops. These are unresolved physical-device reports. No new
   reset, shorter mount timeout or asynchronous IOP module loading is introduced.
2. #125 reports a one-minute software-clock rollback. Production clock, field
   getter, CNF serializer and parser tests restore the exact saved timestamp
   across repeated simulated cold boots. They do not reproduce an unconditional
   minute subtraction. A minute of runtime after the last save is lost on power
   loss by design, and unpowered elapsed time remains unknowable without RTC or
   external time. The timing of the tester's Save/power-cycle sequence is unknown.
3. #138 still reports media-dependent boot messages. The early-init lifecycle
   flag ends at GS setup, before runtime input modules start. The existing
   drawMsg auto-start hook also did not cover direct filesystem/probe status
   calls and the device retry loops' own loading updates.
4. #182 requests a concise release page; raw branch commit history and the last
   20 merged PRs duplicated information and obscured downloads and changes.

## Changes

- **Clock-only saves:** the existing Settings menu disabled Save when only the
  running software clock had advanced. Keep an explicit **Save current clock**
  action available for a valid active software clock. The action queues the
  current time even without another edit. Leaving unchanged Settings still
  does not trigger a save prompt or automatic writes. Invalid/hardware clock
  modes retain the previous no-change behavior. Wait for pending saves to finish
  before cutting power. The old-source production action test fails and the
  corrected action passes. This is a confirmed persistence UX defect, not proof
  of the tester's exact minute-offset cause.
- **Startup status:** a separate presentation flag hides driver/reboot lines
  after GS setup and through first dashboard/section initialization. Auto-start
  saves/restores this flag. Direct loading progress clears startup driver text
  and counts while keeping the branded loading scene; filesystem probe status
  cannot bypass the policy. Manual/runtime status remains detailed. The original
  early-init flag and DS34 teardown semantics are unchanged. Dedicated diagnostic
  Boot check messages remain controlled by WLE_BOOT_DIAGNOSTICS.
- **Release page:** render downloads and one first-parent change per master
  commit/merge, with full-history and documentation links. Move build verification
  and all four hashes into a collapsed section. Preserve canonical asset names,
  source revision, workflow link, ancestry checks, current-master guards and
  published-byte comparisons. The updater still consumes GitHub's asset digests.

## Validation boundaries

Host tests exercise actual Settings save decisions, real clock CNF round trips,
startup message/probe suppression and restored manual feedback. Release-note
tests cover first-parent merge summaries, escaped labels, stable download URLs,
folded checksums, missing packages and CLI hashing. Existing updater, Settings,
USB probe and scene-loop checks remain required; standard/DS34 builds and CI are
checked separately. This work does not establish real-console frame pacing,
power-off reliability or immediate-power-loss persistence.
