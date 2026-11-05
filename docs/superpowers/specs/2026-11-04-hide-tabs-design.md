# Hide the section tab bar (issue #278)

## Goal

A theme option that removes the top bar (panel strip, section tabs, animated
selector, L1/R1 badges, app icon) from view. L1/R1 still switch sections.
The header clock keeps drawing when the clock is set to Header: it is its own
setting and users should not lose it by hiding the tabs.

## Design

One new `DashTheme` field, wired exactly like `clock_pos`:

- `include/dash_internal.h`: `enum DashTabs { DASH_TABS_SHOWN = 0, DASH_TABS_HIDDEN, DASH_TABS_COUNT }`
  and `u8 tabs` in `DashTheme`.
- `src/dash_themes.c`: `tabs_ids[] = {"shown", "hidden"}`, default
  `DASH_TABS_SHOWN` in the preset defaults, clamp in validate, key
  `{"tabs", K_ENUM, OFS(tabs), tabs_ids, DASH_TABS_COUNT, 0}`, and
  `DASH_TG_STYLE` restore copies it. Presets never hide the bar.
- `src/dash_ui.c` `dashDrawHeader()`: when `dashTheme.tabs == DASH_TABS_HIDDEN`,
  skip everything except the final `dashDrawClock()` call. No re-layout:
  screens keep their current content origin.
- `src/dash_settings.c`: `S_T_TABS` after `S_T_GLOW`; Style page item
  `{ST_CHOICE, S_T_TABS, "Section tabs", "Hidden removes the top bar; L1/R1 still switch sections."}`;
  choice names `{"Shown", "Hidden"}` in the get/set switches; per-item preset
  restore case.
- `tests/test_settings_navigation.py`: add `S_T_TABS` to the expected id list
  (the test walks every page and asserts each id appears).

Nothing else changes. `background_only` (R3) stays as is.

## Out of scope

Quick toggle button, moving content up when the bar is hidden, per-section
tab hiding (the issue title says "disable tabs"; the owner decided on hide).
