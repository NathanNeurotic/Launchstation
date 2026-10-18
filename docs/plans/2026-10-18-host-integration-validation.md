# October 18 host and target-build integration checkpoint

This branch combines the three open PRs for validation. It is not master, a
release, or a statement of physical-console acceptance.

## Inputs and exact build source

| PR | Validated input |
| --- | --- |
| #176 storage/audio lifecycle | `d5118919717dcfe5ecf0695ed9902b152cbf1c33` |
| #177 R3Z launcher/browser parity | `3dc0c4d7bb2d4d16be37eb9c278d40b60c08fe71` |
| #178 native UI, clock, audio onset | `276578728c7a29dbe6d20ac67b37ba2258d989c9` |

The combined source built and packaged is
`43e81868ed9021a9526da1789c8b08d4fe66ccf3` on `codex/oct17-integration`.
All three merge operations were clean. This document was added afterward.

## Verification performed

- The combined workflow's original 92 host test scripts passed, including the
  Git Bash release-workflow syntax check. After the final audio fix and expanded
  Game ID fixtures were incorporated, the production sound recovery and Game ID
  tests passed again on the combined source. The audio fix also passed existing
  BGM, pre-reset/PAD, storage-transition and MMCE presence tests on #176.
- Standard and DS34 builds passed with PS2BUILD `v2026.10.13`, including builds
  after the final audio change. Existing unrelated compiler warnings remain;
  this is not a warning-free build claim.
- `check_ds34_build.py` verified the standard ELF excludes DS34 and the optional
  build includes polling, USB/BT clients and embedded controller drivers.
- `check_loader_layout.sh` passed: image `0x84000..0x9def8`, stack address
  `0x9def8`, stack size `0x2108`, within the `0xa0000` limit. Windows Git Bash
  required unrestricted execution because sandboxed MSYS initialization failed.
- Both raw linked ELFs contain the exact newly built 19,509-byte MMCEMAN IRX,
  SHA-256 `72e936822d17021e2173b8afab2d1a9aa2626f0a431562741548479266aa1d42`.
  Both also contain the exact 20,496-byte transition ADPCM asset.
- `package_apps.py` generated and verified both ZIP and PSU variants, including
  ELF, title.cfg, updater CA and BGM.ADP. The archives record the combined source
  revision above. They are local validation artifacts, not published releases.
- The documentation site built 35 pages and 262 search entries.

## New failures covered without hardware

The production sound initializer used to report failure after all SFX uploads
failed despite a healthy audsrv service. That blocked independently loaded BGM
and prevented shutdown from recognizing the live RPC client. #176 now retains
the healthy service and keeps rejected effects silent. The regression failed
before the fix and passed afterward. Module/RPC failures, once-per-generation
retry policy, parked/busy deferral and stale-client teardown are also exercised.

The expanded #177 fixture tests malformed root/CNF extents and lengths,
truncated/missing images, fragmented/failed IO, simultaneous source-card
exclusions, rejected ping/SET_GAMEID, failed status, and permanently busy status.
Busy polling is verified to stop at 15 iterations; this is a logical bound,
not a measured transport or console latency guarantee.

## Exact-head upstream CI

- #176: [run 39094702758](https://github.com/NathanNeurotic/Launchstation/actions/runs/39094702758), passed at `d511891`.
- #177: [run 39094787933](https://github.com/NathanNeurotic/Launchstation/actions/runs/39094787933), passed at `3dc0c4d`.
- #178: [run 39093922223](https://github.com/NathanNeurotic/Launchstation/actions/runs/39093922223), passed at `2765787`.

## Remaining gates

Real MMCE cold-start/reconnect, mixed storage transitions, audio/PAD recovery,
Neutrino game launches and card switching, RetroGem output, and CRT/theme layout
remain unverified. Synchronous module/RPC calls may still pause animation.
Stock sound replacement remains deferred. No issues are closed on the strength
of these host results, and no merge to master or release was performed.
