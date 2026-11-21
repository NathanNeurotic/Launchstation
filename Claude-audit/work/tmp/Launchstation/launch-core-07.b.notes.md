# launch-core-07.b notes (defects lens)

Done: include/dash_card_tool.h 1-70; src/dash_card_tool.c 1-2903; src/dash_clock_sync.c 1-482 (all read_full).

Kept candidates (checked):
- F1 perf: src/dash_card_tool.c:2534 ctLibToCard returns 1 -> 2782/2813 break -> ctLibScan 2353-2418 re-reads every .psu in full (ctLoadPsu 666-736). Copy to card never changes .psu files. Also 1538-1542 ctSaveMenu export returns 2 -> 2269 break -> rescan all cards (ctAllScan/ctAllRead).
- F2 size: ct_saves 30000, ct_titles 24000, ct_details 24000, ct_items 4048 .bss MEASURED (evidence raw/sizes all-symbols). Only Card contents list; ctDropSave:451 and dashCardNewBlank:1486 use counts only (mcViewListSaves allows max 0 / NULL out, mc_card_view.c:327). Three static sb/root pairs 988,1217,1404.
- F3 bug: ctSafetyCopyOf 266-336 numbering/prune by base name only; on MMCE every image's safety copies go to one mmceN:/WLE-Safety/ (238); same-named images (ctSplitGameId makes <ID>-1.mcd per source in WLE-GameID/<base>/, installed MemoryCards/PS2/<ID>/<ID>-1.mcd) share numbers and prune each other's copies. Base also truncated to 47 chars (277-280).

Dropped: dashMmceShowActive uninit (mmceActiveCard memsets out, mmce_card_switch.c:203); ct_cache sharing (all CardIo use sequential); ctResize dest truncation (MAX_PATH 1025, unrealistic); mcSaveGameId bound ok; mcViewListSaves max 0 ok; clock sync: sockets closed on all paths, select fdset packed, Timer u64; SNTP blocking 2x5 s with overlay (by design, skip).
