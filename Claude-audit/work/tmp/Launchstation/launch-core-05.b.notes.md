# launch-core-05.b notes (lens defects)

## Read so far (all complete; re-read in attempt 2 after context loss)
- src/dash_apps.c 1-802 DONE
- src/dash_art.c 1-815 DONE
- src/dash_bg.c 1-602 DONE
- src/dash_bg_geo.c 1-333 DONE
- src/dash_bg_ps2.c 1-825 DONE

## Verified -> findings
1. dash_art.c:397,422 | transient Home apps (dash_home.c:69, dash_lib.c:444-452) set art_dirty -> DASHLIB.CNF rewrite with identical content (dash_lib.c:626 skips transients; dash_main.c:647,1488) | rank 3
2. dash_apps.c:118-136 | showDetails len overflow, worst 700 > 640 -> snprintf size underflow | rank 2
3. dash_apps.c:278-283,454-455 | toggle -1 (list full) shown as removed; dash_home.c:318-320 handles it | rank 2
4. dash_bg_ps2.c:803-809 | dir applied twice; left clouds vanish | rank 2
5. dash_bg.c:194 vs 331 | streak 2.2 vs 2.6 s pop | rank 1
6. dash_art.c:151-163,375 | fileExists redundant open before dashPngLoadFile (dash_store.c:105-119 does same) | rank 1

## Dropped
- selApp NULL deref: norder>0 guarded at 405
- held DashApp* across modal: applyScanResult only from main loop (dash_main.c:1449); storage static/heap
- settings card writes: deferred flush (dash_store.c:659-668)
- osdTime RPC: cached (main_title.c:723-744)
- art cache full: unreachable (<=~30 visible keys vs 40 slots)
- InvalidateVram for theme/brush/scanline: cannot verify gsKit texmanager behaviour
- per-frame stricmp searches: small counts
