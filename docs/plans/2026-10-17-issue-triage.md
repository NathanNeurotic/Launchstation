# Launchstation issue triage, October 17, 2026

> **Status as of 2026-10-26: historical triage; code items merged, hardware acceptance still open.**
> Merged: #176 (`f29a28f`, storage lifecycle/audio), #177 (`ef02e7a`), #178 (`f4b621f`, UI/software clock),
> #183 (`82082b6`), #181 (`59bb620`, Neutrino profiles). #175/#174/#173/#172 stall reports have source-level
> mitigations only; physical confirmation by the reporter is still pending. #125/#138 likewise.


Baseline: `ec3723f26c43e04cbfa7227e970302684aed4bbc` on master. Reports #167-175 cite that build and SCPH-30001R. Existing PRs #176 and #177 retain separate branches. This PR contains the related UI and clock work.

| Priority | Issue | Evidence and implementation | Acceptance still needed |
| --- | --- | --- | --- |
| 1 | #175 storage interference after MMCE | PR #176 now rejects MMCE/MX4SIO loads if a required reset was refused; retains accurate resident-stack state. Generic missing-device dialog already improved. | Mixed-device physical transition tests; root cause of all reported stalls is not yet proven. |
| 1 | #174 MMCE reset and silent audio | PR #176 shuts down audsrv before reset, restores through generation-aware main-loop maintenance, and reduces startup detection to two 200 ms attempts per port. | Physical absent/present/slow-start MMCE and audio recovery. An incompatible resident SIO2 stack may still require a real reset. |
| 2 | #173 MX4SIO stalls | Pre-RPC frame in #176; refused-reset guard prevents an unsafe cross-stack load. | Timing of module entry and root RPC on hardware. No speculative shorter mount window. |
| 2 | #172 iLink stalls | Pre-RPC frame in #176. Independent BDM transport and existing 3-second returned-probe tolerance retained. | Physical iLink attachment and blocked-call timing. Continuous animation during synchronous module entry remains unresolved. |
| 3 | #168 tiny/flickering shoulder icons | Larger shoulder glyph scale, thicker SDF strokes, remove thin nested shoulder outline. | Interlaced CRT visual acceptance. No claim of measured flicker elimination without display testing. |
| 3 | #171 inconsistent button borders | Fixed rounded-border geometry matching the SDF keycap; independent of theme corner style. | Light/dark/transparent theme visual checks. |
| 3 | #167 folder destination placement | Dedicated path panel above list, separated by a visible gap. Ten file rows keep lower content clear of footer. | Console visual acceptance in embedded and modal browsers. |
| 3 | #170 MASS path shown as USB | Browser breadcrumb displays `massN:` for internal `usbN:` paths. Filesystem paths and device identity remain unchanged. | Both USB units and long nested paths. |
| 3 | #169 exit label overflow | Exit confirmations use `Leave`; descriptive title/body remain intact. | Console confirmation layout. |
| 4 | #125 dead-battery clock | Optional software clock with saved local checkpoint in DASHBOARD.CNF; no RTC read needed for a valid seed, existing Save/Discard controls, switch back to hardware clock. | Power-cycle persistence on the configured writable storage. Unpowered elapsed time cannot be inferred without a working RTC or an external time source. It resumes the last saved checkpoint, not a claim of correct wall time after unplugging. |
| 4 | #138 boot checks | Boot diagnostics remain gated; automatic driver startup uses branded `Starting Launchstation` feedback with driver details suppressed. Actual initialization is preserved. | Reporter confirmation that this meets desired boot presentation. |
| Deferred | #160 stock PS2 effects | User explicitly requested skipping sound asset changes during this task. | No stock sound pack added; audio effort remains on reset recovery and latency evidence. |

## Existing PR #177

The per-launch Neutrino options now use the native dashboard modal when the dashboard is active. Classic mode retains the upstream dialog. Host C checks execute option generation and scripted dashboard selection/Run/Cancel paths. No final parity or real-game compatibility claim until console testing.

## Validation boundaries

Host C tests execute the production clock tick logic, storage transition policy, MMCE ping loop, reset/PAD ordering and Neutrino argument/dialog logic. Standard PS2 builds compile and pack each change. DS34 builds and exact-head CI are checked separately. No physical-console run has been performed in this task. Issues remain open until their individual acceptance gates are met.

## Transition audio latency

Decoded the exact embedded transition ADPCM and inspected PS2SDK audsrv playback
(`ps2dev/ps2sdk/iop/sound/audsrv/src/adpcm.c`). Playback starts one SPU2 voice
at the uploaded payload start; the channel header does not split stereo audio.
The original signal stayed below amplitude 16 until about 76 ms. Removed 110
quiet ADPCM frames (69.85 ms at the stored pitch), beginning the retained stream
on a predictor-zero frame. The decoded waveform after that cut is exactly equal
to the original suffix, verified before writing and retained as a decoded PCM
hash in `test_transition_latency.py`. Header now correctly describes the mono
stream audsrv actually plays. No new effects or stock samples were introduced.
This is an asset-onset improvement, not a measurement of controller-to-speaker
latency or physical-console audio recovery.

## Native Files four-mode parity

The native Files Show menu now includes Game titles without details, preserving
existing mode values 0/1/2 and adding 3. Title loading and rendering share the
same predicate; the details lane uses an independent predicate. Legacy title
operations receive mode 2 for either native title view, so this branch remains
compatible with master before the classic four-mode changes in #177 merge.
Missing titles retain the filename fallback. Compiled predicate tests and
source integration checks cover the four modes; console layout remains pending.
