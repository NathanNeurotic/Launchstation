# Notes launch-core-06.b (defects lens)

Done: src/dash_browser.c lines 1-3665 read fully. Verification done; writing findings.

Kept:
- 265-278,384-385 insertion sort on UI thread (listDone via dash_worker.c finish 221-229); qsort already linked; test_browser_sort.py harness lacks stdlib.h
- 1271-1275,1316-1336,1557,1627 name[40]/[48] truncation with RTC fallback stamp; Timer() ms (timer.c:8-11)
- static bss: map lines 11467-11476 (8x0x2000 + 2x0x4000), contrib .bss 106560
- 1587-1610 + 1400-1447 compare after dashLoadingEnd on UI thread; mmce_backup.c 565-577 pre-compare
- 2403,2412 tsChoose i<4 vs 5 items (filer_actions.c 385-390,1406-1415)
- 2224-2261 recoverCardFromPath prepareOp before picker; picker leaves listing in flight (dash_widgets.c 1340-1346)
Dropped: clipReserve (realloc keeps data), opDone without begin (idempotent), folder slash join (pk.dir ends in /)
