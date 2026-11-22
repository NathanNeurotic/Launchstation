# L3 animated-background preview

## Intent

Give ordinary L3 presses a useful dashboard action: preview animated background scenes without editing the user's theme or saved settings.

## Behavior and constraints

- Cycle forward through animated scene kinds, skipping Solid, Gradient, and Wallpaper.
- Change the scene and apply its existing preset palette (`bg1` and `bg2`). Keep tiles, text, icons, layout, wallpaper, motion, detail, brightness, scanlines, and every other setting unchanged.
- Change only in-memory preview state for the current dashboard session; do not mark the theme dirty or write configuration.
- Display the selected scene's existing localized name in an information toast.
- Handle L3 globally in the dashboard, including the background-only view, but not during boot input or while SELECT quick-launch owns the button.
- Preserve existing boot-held and SELECT+L3 launch-key behavior.

## Approach

Add one small theme helper that selects the next animated scene and returns its name. Call it from the dashboard's existing input loop before section-specific dispatch. This reuses the existing renderer and scene names instead of adding a second theme or settings path. Persisting the choice was considered and rejected by the user.

## Verification

Add host regressions for wraparound, skipped non-animated scenes, unchanged settings/save state, and quick-launch precedence. Run focused input/theme tests and both packed LLVM builds; check the shortcut in PCSX2 if the existing harness supports it.
