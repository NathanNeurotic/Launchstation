# launch-core-10.b notes (lens defects)

Done: dash_home.c 1-565, dash_icons.c 1-1006, dash_image.c 1-313, dash_info.c 1-446, dash_jpeg.c 1-183, dash_lib.c 1-937 (all read)

Kept (verified):
- F1 dash_home.c:214-220 + dash_main.c:959-969 | LK title overwrites library entry (dashLibResolve returns &dashApps[i]); persisted by dashLaunchApp dash_main.c:647-648 | checked
- F2 dash_info.c:129,147,220,347-348 | 4 static Sheets 6340 B each MEASURED (sizes tsv) | callers not nested (dash_main.c:479-487, dash_settings.c:3352, dash_files.c:566, dash_exploit.c:125)
- F3 dash_jpeg.c:130,140 + dash_png.c:156,169,181,194 | per-pixel u64 div -> __udivdi3 (linked, 536 B) | products < 2^23
- F4 dash_lib.c:492-499,583-601,909-934 | dashApps 512 x 828 B = 423,936 B | only writers cacheKv/ApplyScan/Remove (+ DASH_AUTOTEST demo dash_main.c:1312 unbuilt)
- F5 dash_icons.c:274-752 | iconSdf 30,060 B .text MEASURED at -O2, startup-only | savings INFERRED, Low

Dropped: dashTexRelease double free (no free there), cache flush (both decoders SyncDCache), scan dirs bound (nscan <= 12), ApplyScan rescan snapshot (negligible), home_apps stale (applyScanResult rebuilds), dashAppActions/home_tmp (no use after rebuild), per-frame console text (trivial)
