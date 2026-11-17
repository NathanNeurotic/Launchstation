# launch-core-05 notes (structure lens)

## Done
- src/dash_apps.c 1-802 (read full)
- src/dash_art.c 1-815 (read full)
- src/dash_bg.c 1-602 (read full)
- src/dash_bg_geo.c 1-333 (read full)
- src/dash_bg_ps2.c 1-825 (read full)
ALL READ. Next: verification greps, then findings JSON.

## Candidates
- src/dash_apps.c:94-101 | dashAppsFocusPath unused (lead) | grep whole repo
- src/dash_apps.c:103-106 | dashAppsVisibleCount unused (lead) | grep whole repo
- src/dash_apps.c:12 | local prototype dashHomeRebuild | is it in dash_internal.h?
- src/dash_apps.c:519,541-544,553-554,558-560 | drawGrid dead locals fx fy r fr with void casts | none, read
- src/dash_apps.c:496-499,513 | drawTitleBar (void)a redundant | trivial
- src/dash_apps.c:278-279 vs 454-455 | fav toggle + toast duplicated; also in dash_home? | grep "Added to Favorites"
- src/dash_apps.c:686-691 | carousel idx init convoluted | trivial
- src/dash_art.c:71-74 | dashArtShutdown unused (lead) | grep
- src/dash_art.c:787-790 | dashDrawAppTile unused (lead) | grep
- src/dash_art.c:31 | ART_MAX_CANDIDATES unused macro | grep
- src/dash_art.c:64-69 | dashArtInit memset of zeroed static | callers
- src/dash_art.c:303-306 | appDeviceReady forwards only | 2 callers
- src/dash_art.c:151-163,375 | fileExists probe then decode opens again | check
- src/dash_art.c:409-415 vs 532-539 | GSTEXTURE setup duplicated | grep "Delayed = 1"
- src/dash_art.c:672 | dashDrawTileFocus param always same? | callers
- src/dash_art.c:85,96,469,588,599 | callers of dashArtInvalidateVram, dashArtForget, dashArtTakeDirty, dashThemeTexture, dashThemeTexturesFree
- src/dash_bg.c:74-84 vs 347-348 | dashBgVignette vs crystal fog/vignette same geometry
- src/dash_bg.c:135-138 | bgAnimated one-line wrapper, 1 caller
- src/dash_bg.c:543-547 | dashBgDrawStatic callers
- src/dash_bg.c:193-195 vs 331 | streak ends at 2.2 but fade divides by 2.6: pops out at ~47% alpha
- src/dash_bg.c:432-439, dash_bg_geo.c:257-264 | GSTEXTURE setup pattern copies
- src/dash_bg_geo.c:10 | TAU_F unused in geo
- src/dash_bg_geo.c:12-16 = dash_bg_ps2.c:50-54 | colA identical copies; vs dashColorA
- src/dash_bg_geo.c:18-21 | fracf; ps2 uses (x-(int)x) at 76,424
- src/dash_bg_ps2.c:519 | OrbMat m[9] only 6 used
- src/dash_bg_ps2.c:748-752 | ClockRing builds OrbStyle by hand (menuOrbStyle exists)
- src/dash_bg_ps2.c:803-809 | Clouds: dir=-1 negative fmodf -> x in [span,2span) -> even clouds always offscreen BUG
- src/dash_bg_ps2.c:816-818 | blend toggled per cloud inside loop
