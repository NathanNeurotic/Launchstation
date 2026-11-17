# launch-core-14.b notes (lens defects)

## Done
- src/dash_tools.c 1-1735 read fully (re-read in attempt 2)
- src/dash_ui.c 1-1556 read fully (attempt 2)
- candidate checks done; writing findings JSON

## Kept
- F1 dash_tools.c:495-503 PrepareMmceStatus awaits only own job before loadMmceModules (IOP reset via init.c:2266-2279 on UI thread) -> rank 3
- F2 dash_tools.c:628..711,1440 redundant refreshes + loop-top UI IO 1363-1378 while job runs -> rank 2
- F3 cloud/MMCE backup blocking IO on UI thread, progress once per file -> rank 3
- F4 cloudGoogleConfigured derivable, CNF read 3x per pass -> rank 1
- F5 672-673 return hides local summary -> rank 1

## Dropped
- result[] uninit (callees always write; boot_path is array; picker never returns empty folder)
- response leak (read_response leaves body NULL on error)
- UpdateJob on stack (await non-cancelable)
- stale resubmit (MAX_PATH 1025 > 256)
- stack arrays (BROWSE_MAX 12, ~9 KB)
- localize cost (binary search)
- DashFileTransferChunk quiet spin (quiet only during autostart)
- DashPause clock units (project-wide assumption)
