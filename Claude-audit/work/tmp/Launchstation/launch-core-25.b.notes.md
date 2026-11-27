# launch-core-25.b notes (lens defects)

## Done (all 11 files read in full in the second attempt)
- src/main_info_screens.c 1-394
- include/main_modules.h 1-10
- src/main_modules.c 1-71
- include/main_startup.h 1-31
- src/main_startup.c 1-757
- include/main_title.h 1-23
- src/main_title.c 1-753
- src/makeicon.c 1-426
- include/mc_card_export.h 1-54
- src/mc_card_export.c 1-625
- include/mc_card_view.h 1-98

## Kept
- main_startup.c:150-163 strncat duplicate SYSTEM.CNF keys -> overflow (main.c:66-69, caller main_actions.c:621-623)
- makeicon.c:250-255,337-368 tex_buffer leaked every make_icon call (caller filer_browser.c:2455 via dash_browser.c:2389)
- mc_card_export.c:20-22 200,704 B static .bss (BUILD-EVIDENCE.md:83,91)
- main_title.c:156-161 positive minute shift 60 iterations/minute (callers filer_browser.c:1847, dash_ftp.c:34)

## Dropped
- argv sprintf ShowDebugInfo, strchr path2, strcpy CNF_Path, LNG sprintf, IPCONFIG parse, getIpConfig thread, normalizeBootPath bound, seed strcpy, temp probe, RTC retry, missing SAME_PATH in ExportPcsx2/Import (callers change extension), RLE overflow (runs >= 2 invariant holds), per-chunk reopen (documented MMCE rule)

## Status
- writing findings JSON next
