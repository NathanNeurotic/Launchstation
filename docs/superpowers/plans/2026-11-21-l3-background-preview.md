# L3 Animated-Background Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Make an ordinary L3 press preview animated dashboard backgrounds and their preset background colors for the current session without changing saved settings or existing launch-key chords.

**Architecture:** A pure helper in the theme module selects the next animated scene and maps it to its existing preset palette. The dashboard input loop applies only the scene and its two background colors in memory and shows its existing name, before quick-launch and section dispatch consume input.

**Tech Stack:** C, existing dashboard input/theme APIs, Python host harnesses, ps2build LLVM.

**Spec:** `docs/superpowers/specs/2026-11-21-l3-background-preview-design.md`

## Global Constraints

- Skip Solid, Gradient, and Wallpaper; cycle through every other scene.
- Do not mark settings dirty, call theme commit, or write configuration.
- Preserve boot-held and SELECT+L3 launch keys.
- Work in every dashboard section and background-only mode.

## Review Focus

- Invalid/current non-animated scene values must recover to the first animated scene; test the helper over every enum value and out-of-range input.
- SELECT held on the same frame as L3 must launch rather than preview; test both normal and background-only modes.
- Ordinary L3 must be consumed so no section also handles it; assert cleared input in the dispatch harness.
- Preview must not alter motion/detail or other settings; compare the harness theme state around repeated cycles.
- Boot input must not preview; assert the dispatch guard with `boot_phase` set.

---

### Task 1: Animated scene selection

**Files:**
- Modify: `src/dash_themes.c`
- Modify: `include/dash_internal.h`
- Test: `tests/test_l3_background_preview.py`

**Interfaces:**
- Produces: `int dashThemeNextBackground(int current)`, returning a valid animated `DashBgKind`; `int dashThemeBackgroundPreset(int kind)`, selecting the existing preset that supplies its two background colours.

- [x] **Step 1: Write the failing helper test**

Compile the extracted production helper. Assert every current value from `-1` through `DASH_BG_COUNT` returns an animated kind, Solid/Gradient/Wallpaper recover to `DASH_BG_TUNNEL`, each animated scene advances to the next, and Clouds wraps to Tunnel.

- [x] **Step 2: Run test to verify it fails**

Run: `python3 tests/test_l3_background_preview.py`
Expected: FAIL because `dashThemeNextBackground` is absent.

- [x] **Step 3: Implement the helper**

Declare both helpers in `dash_internal.h`. In `dash_themes.c`, scan forward with wraparound and return the first kind other than `DASH_BG_SOLID`, `DASH_BG_GRADIENT`, and `DASH_BG_IMAGE`. Map each scene to its existing preset palette: Tunnel/Streaks use OSD, Clouds uses Mist, and the remaining scenes use their same-named presets.

- [x] **Step 4: Run test to verify it passes**

Run: `python3 tests/test_l3_background_preview.py`
Expected: helper checks PASS; dispatch checks may remain red until Task 2.

### Task 2: L3 dashboard dispatch

**Files:**
- Modify: `src/dash_main.c`
- Modify: `tests/test_background_only_hotkeys.py`
- Modify: `tests/test_l3_background_preview.py`
- Modify: `.github/workflows/compile.yml`

**Interfaces:**
- Consumes: `int dashThemeNextBackground(int current)` and existing `dashThemeBgName(int kind)`.
- Produces: ordinary L3 previews a scene and is consumed; existing L3 launch-key paths remain intact.

- [x] **Step 1: Write failing dispatch tests**

Extend the existing production input-snippet harness with L3. Assert ordinary L3 advances only `dashTheme.bg`, shows its name, and clears input in Home, another section, and background-only mode. Assert SELECT+L3 still calls its launch key without changing the scene, and boot-phase L3 does nothing. Run the new helper and dispatch test.

- [x] **Step 2: Run dispatch tests to verify they fail**

Run: `python3 tests/test_background_only_hotkeys.py && python3 tests/test_l3_background_preview.py`
Expected: FAIL because ordinary L3 is not handled.

- [x] **Step 3: Implement L3 dispatch**

In `DashRun`, before R3/background-only filtering and quick-launch processing, handle non-boot L3 when SELECT is not held and quick-launch is inactive. Assign only `dashTheme.bg`, load the scene's preset, copy only `bg1` and `bg2`, toast the scene name, flush input, and clear the event.

- [x] **Step 4: Run focused tests**

Run: `python3 tests/test_background_only_hotkeys.py && python3 tests/test_l3_background_preview.py && python3 tests/test_theme_motion.py`
Expected: all PASS. Add the new test to the workflow host-test block.

- [x] **Step 5: Verify target builds and emulator behavior**

Build standard and DS34 packed LLVM ELFs with absolute output directories. Run controller/toolchain checks. Boot the exact standard packed ELF in the isolated PCSX2 harness and confirm ordinary L3 cycles scenes without entering settings or saving.

- [x] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-11-21-l3-background-preview-design.md docs/superpowers/plans/2026-11-21-l3-background-preview.md include/dash_internal.h src/dash_themes.c src/dash_main.c tests/test_l3_background_preview.py tests/test_background_only_hotkeys.py .github/workflows/compile.yml
git commit -m "Add L3 animated background preview"
```
