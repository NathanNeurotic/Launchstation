# launch-core-11 notes (structure lens)

Done reading: src/dash_main.c 1-1542 (full), src/dash_png.c 1-241 (full), src/dash_ps1_card.c 1-759 (full). Re-read in attempt 2.

## Verified (attempt 2) -> findings
- F1 dash_files.c whole file dead: section never DEVICES (dash_main 208-213, 1324-1336, visible_sections) -> screens[3] unreachable; dashFilesPoll no-op (pend only set by dead startDevice). dash_files.o 13188 text, 30520 bss MEASURED (contrib-by-object). files_mode / S_FILES_MODE follow.
- F2 DashIsEnabled + S_ENABLED dead option chain (no settings row uses S_ENABLED; enabled forced 1 at dash_store 309)
- F3 dashVisibleSectionName/dashSectionName/section_names, prev_section write-only, extern paddata twice (gc-discarded per gc file)
- F4 dash_png duplicate box filter loops + flush 3x
- F5 dash_png full/rows leak on longjmp
- F6 small redundant paths in dash_main: 398-399, 1292-1300, dashOpenTextEditor always 1 (dash_apps.c:196), poll wrappers 798-810
- F7 bootBegin/bootUpdate launch-key logic twice
- F8 p1Base == ctBaseName (dash_card_tool.c:185) + lkLabel
- F9 three read loops in dash_ps1_card (p1Load, p1ReadFile, p1ReadAll)
- F10 dev-only ifdef blocks DASH_AUTOTEST/DASH_THEME_TOUR/WLE_BOOT_DIAGNOSTICS
- F11 read+parse+message repeated; folder-of-path 4x
Dropped: dash_inited/else (DashRun re-entered from main_actions.c:731), afterMiscCommand vs launch restore (differs), dashOpenBrowser vs Navigate (behaviour differs), lead 13-15 (prose).
