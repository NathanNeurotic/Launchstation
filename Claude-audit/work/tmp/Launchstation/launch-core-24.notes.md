unit launch-core-24 notes
DONE: all 8 files read full (installer 1-2519, main_fileops, main_gameid.h/.c, main_gameid_table, main_history.h/.c, main_info_screens.h)

findings decided:
1 classic UI fallback in installer (exploitInstallerUi always set by dash_exploit.c:159); only other entry main_actions.c:803, dash routes MISC via dashRunMisc -> rank3 INFERRED
2 redundant/unreachable conditions 2111-2116, 539-540, 2243-2244, 409-410, 395-397, 372, 378-379
3 copy_printable_prefix dup main_console_info.c:7-27
4 folder_for_asset switch + impossible NULL checks
5 add_size_kb / impossible negative size plumbing
6 in-file dups (export sign, write tail, mc_boot_elf_exists vs wleExists, folder_assets_to_write recompute)
7 install_for_plan wrapper + main_history evictedEntry copy
8 ensure_mc_folder vs ensureHistoryDirectory
9 cross-repo POPSLoader retrogem.cpp table + packet draw
NEXT: write JSON, validate
