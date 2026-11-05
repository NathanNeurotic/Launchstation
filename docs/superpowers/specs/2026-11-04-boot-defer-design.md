# Boot: defer console identity and audio start past the fade-in (#277)

## Problem

From `setupGS()` until the dashboard's first `dashGfxPresent()` the screen is
black. Two pieces of work sit inside that window although nothing on the first
screen needs them:

- `dashConsoleIdentityInit()` (called from `firstInit()`): loads `rom0:ADDDRV`,
  re-initialises CDVD, polls MECHACON (`sceCdMV`) up to 101 times, issues
  S-commands and reads `rom1:DVDVER` — all to fill the one-line console
  summary on Home/Files.
- `dashSoundInit()` + `dashBgmStart()` (called from `DashRun()` before the
  loop): loads libsd and audsrv, initialises audsrv and uploads eight ADPCM
  samples.

Driver loading for storage is already lazy; the IOP reset and USB enumeration
are structural and stay where they are.

## Change

One deferred step in the `DashRun()` loop, run once when `fade_in > 0.95f`
(the same threshold the first-run welcome uses):

1. `dashConsoleIdentityInit()`
2. `dashSoundInit()`; `dashBgmStart()`; boot SFX if this is a boot

Until that step runs `dashSoundMaintain()` is skipped, otherwise it would
start audio on the first frame by itself (`sound_tried_generation` starts at
0 and the IOP reset generation is already 1).

The summary line shows the ROM-only text ("PS2 | region | ROM | DVD:
unavailable") during the fade and fills in afterwards.

## Not changed

- Device Auto-start keeps running from `firstInit()`; it already shows the
  "Starting Launchstation" overlay and users control it per device.
- No new threads: IOP module loads stay on the UI thread.
- No splash screen: the GS is only configured after the IOP reset and config
  load, so a splash could not start earlier than the dashboard itself.
