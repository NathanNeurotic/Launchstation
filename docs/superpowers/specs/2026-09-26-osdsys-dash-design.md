# OSDSYS-ify the dashboard — design spec

Date: 2026-09-26
Branch: `nathan/feature/theme-engine-commits-09252026`

## Goal

Bring the dashboard closer to the real PS2 system-menu experience, in four
workstreams:

1. Procedural OSDSYS-style button icons (no embedded image blobs).
2. More animation: new background scenes + UI micro-animation polish.
3. Dashboard sound effects via a new `audsrv`-based SFX subsystem.
4. CWD-first theme-folder resolution (theme assets live next to the ELF).

Success: the project still builds warning-free with both build systems after
each workstream; every visual change respects the existing
`dashCfg.anim` / `dashCfg.effects` (Off / Subtle / Full) knobs; SFX degrade
gracefully when the audio driver cannot start.

## Reference material

- `agent_delivery_assist/*.png` — 32x32 OSDSYS-style button tiles (face
  buttons, L1-L3/R1-R3, dpad, select, start) and ESRB-style rating strips.
  Used as **art direction only** — everything is redrawn procedurally.
- `agent_delivery_assist/*.adp` — Sony ADP (SPU2 ADPCM) UI sounds:
  `cursor`, `confirm`, `cancel`, `transition`, `message`, `boot`,
  `bd_connect`, `bd_disconnect`. This is exactly the format
  `audsrv_load_adpcm()` consumes — **no conversion needed**.
- `agent_delivery_assist/bgm.ogg` — background music. **Deferred** (needs a
  Vorbis decode + streaming path; follow-up task once SFX is proven).
- `agent_delivery_assist/osdbits-master/` — reverse-engineered OSDSYS
  (opening towers/fog/streaks/cubes, menu rendering, sound RPC). Source of
  the new scene designs.

Out of scope: rating-strip display (no rating UI exists in the dash), BGM
streaming, any use of Sony BIOS data.

---

## Workstream 1 — Procedural OSDSYS button icons

Current state: `src/dash_icons.c` rasterizes 42 SDF icons into a PSMT8
atlas at init; the four pad glyphs are drawn bare inside a panel badge by
`drawButton()` (`src/dash_ui.c:125`); L1/R1/START/SELECT are text pills.

Design:

- **Badge-style atlas tiles.** `iconSdf()` cases for `DI_BTN_CROSS`,
  `DI_BTN_CIRCLE`, `DI_BTN_SQUARE`, `DI_BTN_TRIANGLE` are redrawn as the
  complete OSDSYS tile: dark rounded-square badge (near-black vertical
  gradient), hairline light top border, glyph in the PlayStation colours
  already used today (cross `#86A8FF`, circle `#FF6E7C`, square `#F08CD8`,
  triangle `#4EE0A8`) — matching the reference PNGs. The SDF for the badge
  shape composes with the existing glyph SDFs via the existing `U()`/`S()`
  CSG helpers; per-pixel colour = badge gradient where `d` is the badge,
  glyph colour where `d` is the glyph.
- **New atlas icons** (insert before `DI_COUNT`, `include/dash_internal.h`):
  `DI_BTN_UP`, `DI_BTN_DOWN`, `DI_BTN_LEFT`, `DI_BTN_RIGHT` (dpad chevrons),
  `DI_BTN_START` (play-triangle glyph), `DI_BTN_SELECT` (rounded-rect
  glyph) — all badge-style per `start.png` / `select.png`. The atlas keeps
  7 columns; `ICON_TEX_H` grows from 6 to 7 rows so 48 icons fit.
- **Named buttons stay text** (pure-SDF letterforms are impractical):
  `drawButton()`'s `button < 0` branch is restyled from the pill to the
  same dark rounded-square badge with a hairline border and the key label
  centred (`L1`, `R1`, `L2`, `R2`, `L3`, `R3`, `START`, `SELECT` remain
  font-drawn). Symbol-button branch drops its extra panel badge — the atlas
  tile now *is* the badge.
- No changes to the tint rules: pad glyphs keep their fixed PS colours;
  other icons still follow `dashTheme.icon_tint`.

Files: `src/dash_icons.c`, `src/dash_ui.c`, `include/dash_internal.h`.

## Workstream 2 — Animation

### New background scenes

Following the existing `dashBgSceneX(float t, int fx)` idiom
(`src/dash_bg_ps2.c`), each a few hundred flat/gouraud primitives, driven
by the existing `bg_time` clock and gated by `fx`:

- **Streaks** (`background = streaks`) — vertical light pillars drifting
  across the screen with parallax depth, ADD-blended glow cores with soft
  falloff, inspired by the opening's light streaks.
- **Clouds** (`background = clouds`) — layered translucent fog blobs
  drifting with parallax over a dark gradient, tinted by `bg1`/`bg2`, soft
  additive rims — inspired by the illegal-disc cloud field.

Both register: `DASH_BG_*` enum (`include/dash_internal.h`), id/name
strings (`src/dash_themes.c`), a `case` in `drawScene()`
(`src/dash_bg.c`), `theme.cfg` key acceptance via the enum table, and
docs. One new built-in preset **"Light Show"** (`preset = lightshow`)
uses `streaks` with the OSD indigo palette. Existing scenes/presets are
not modified.

### UI micro-animations (`src/dash_ui.c`, `src/dash_main.c`)

All gated by `dashCfg.anim` (off = instant, reduced = shorter/simpler):

- **Focus pop** — on selection change, the focused card/artwork springs
  ~8% larger and settles over ~180 ms, via a shared helper applied where
  artwork cards draw.
- **Modal & toast entrances** — the existing fade+slide is kept but eased
  (snappier open); the dashboard's gfx stack has no scale transform, so no
  geometric scale is added.
- **Hint bar** — soft slide-in tied to the section-transition clock.
- **Header tab indicator** — springier chase to the active tab (small
  overshoot), replacing the plain exponential `tab_anim_x` chase.
- **Section transition** — existing eased slide kept with a slightly
  longer travel; no per-row stagger (the gfx stack draws each screen as
  one layer).

## Workstream 3 — SFX subsystem

Current state: the project has no audio at all (no `audsrv`, no IRX, no
libs). `audsrv` (EE lib + IOP driver) is installed in the ps2dev packages.

Design:

- **Build wiring.** `audsrv` added to `libs:` (EE) and `embed_irx:`
  (IOP) in `ps2.yaml`; matching `IRX`/`EE_LIBS` additions in the legacy
  `Makefile`/`embed.make`.
- **Payloads.** The 7 UI `.adp` files embedded via one
  `EMBED_FILE(sym, "path")` line each in `src/embed_blobs.S` (ps2build)
  plus a `bin2s` rule per file in `embed.make` (legacy). Payload ≈ 65 KB
  total. Files live at `res/sfx/*.adp` in the repo.
- **New module** `src/dash_sound.c` + `include/dash_sound.h`:
  - `dashSoundInit()` — load/start `audsrv.irx`, `audsrv_init()`,
    `audsrv_adpcm_init()`, `audsrv_load_adpcm()` each sample into static
    `audsrv_adpcm_t` slots. `dashSoundShutdown()` on legacy handoff.
  - `dashSoundPlay(DashSound id)` — one-shot on a round-robin ADPCM
    channel; no-op when sound is off or the driver failed.
  - Cursor throttling: pad-repeat ticks play `cursor` only when the
    focused item actually moved.
- **Hook points:** pad move (cursor), OK/Cancel confirm & back (confirm /
  cancel), section change (transition), toast/dialog pop (message), dash
  boot once (boot), device attach/detach (connect / disconnect).
- **Settings.** New persisted keys in `DASHBOARD.CNF` (`Sound = 1`,
  `Sound_volume = 80`) with a Settings row; default on. If the IRX fails
  to load or a sample fails, every call no-ops and the dash works silent.
- **BGM deferred:** `bgm.ogg` needs libvorbis decode + a PCM stream-feed
  thread; separate follow-up task once SFX is proven on hardware.

## Workstream 4 — CWD-first theme folders

Current state: theme folders live only under `<store_dir>/THEMES/`
(`themesRoot()`, `src/dash_themes.c:930`), where `store_dir` prefers the
dir of the loaded `LAUNCHELF.CNF`. On FMCB that is usually
`mc0:/SYS-CONF/`, so themes colocated with the ELF (e.g. on a USB stick)
are invisible and external assets wait on device handling.

Design (settings location is **not** changed — only theme folders):

- Theme-folder roots become an ordered candidate list:
  `[LaunchElfDir, store_dir]` (each only when on a writable, loaded
  device).
- `dashThemeFolderList()` aggregates theme names from all roots
  (deduped, CWD first). `dashThemeFolderLoad()` tries each root in order.
  `dashThemeFolderExport()` writes to the first writable root (CWD first).
- Image paths inside `theme.cfg` keep resolving against the folder the
  theme was found in (`resolveInFolder`) — so a theme on the ELF's device
  has its images on that same, already-up device. Absolute paths in
  theme.cfg still win.
- `docs/themes.md` updated to document the lookup order.

## Verification

- Each workstream ends with a clean `ps2build build` of `ps2.yaml` and a
  legacy `make`, with no new warnings.
- `.adp` compatibility with `audsrv_load_adpcm` and all runtime behaviour
  must be confirmed on real hardware or PCSX2 by the user; the code
  logs-and-degrades if a sample fails to load.
- Manual checklist for the user: icons in hints/dialogs, both new
  backgrounds in all three motion modes, SFX on each hooked event, theme
  load from a USB-stick `THEMES/` folder with the ELF on the same stick.
