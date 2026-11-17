# launch-core-10 notes (lens structure)

## Done
- src/dash_home.c 1-565 (read full)
- src/dash_icons.c 1-1006 (read full)
- src/dash_image.c 1-313 (read full)
- src/dash_info.c 1-446 (read full)
- src/dash_jpeg.c 1-183 (read full)
- src/dash_lib.c 1-937 (read full)  -> ALL READ; now verifying
- dash_lib cands: 501-503 dashLibShutdown empty; 517-526 dashLibRemove; callers of IndexOf/Resolve/MakeTransient/MetadataPath/LoadArgs; splitPath file/base unused in MetadataPath 363-369; isElfName 658-661 wrapper; prefixMatchNoCase 741-750 vs strnicmp; DeviceLabel/DeviceIcon 167-254 dup with Files tab?; hashHue djb2 dup?; known_names dup?

## Verified -> writing JSON: icons dead chain; lib dead (+DashProgressFn h:707); home anim state; home redundant branches; dashHomeRebuild wrapper; DI_RECENT; icons tier dup; prefix cmp x3 (exploit_installer:313, popstarter_path:171); info debug block; image/jpeg defensive; info job copy
## Dropped: DI ids other (VIEW_CAROUSEL via arithmetic, PAD_BADGE internal), lk_names dup, gsColor dup (differs), feature ifdefs, DeviceLabel dup w/ dash_devices
## Candidates
- src/dash_home.c:45,354 | row_anim written only, never read | grep row_anim
- src/dash_home.c:43,176,357 | col_anim[1] never read; cam[1] never used; loop r<2 with slot var | confirm reads
- src/dash_home.c:267-278 | UP and DOWN branches identical | merge
- src/dash_home.c:150-155 vs 359-364 | camera clamp computed twice | small
- src/dash_home.c:489-491 vs 505-508 | chip width computed twice | small
- src/dash_home.c:562-565 | dashHomeRebuild wrapper only forwards to static homeRebuild | grep callers
- src/dash_home.c:171,262,349,427,539 | NULL checks on const dashAppsScreen callbacks | check dash_apps.c definition
- src/dash_home.c:47-48 | lk_names table maybe duplicated elsewhere | grep "Triangle", "L1"
- src/dash_icons.c:859-867 | dashIconsShutdown, dashIconsTexture unused | grep
- src/dash_icons.c:939-954 + 925-937 + 761-776 | dashHeaderIcon unused -> drawHeaderIconPart, initHeaderIconParts, atlas part slots dead | grep; dashTexDrawQuadUV other users?
- src/dash_icons.c:15 | SELECT_HALF_W unused macro | grep
- src/dash_icons.c:879-890 vs 986-997 | slot/uv/pad computation duplicated in dashIcon and dashIconMany | count
- src/dash_icons.c:765-775, 790-798, 818-832 | three rasteriser loops | small merge
- src/dash_icons.c:296-298,453-455,490-491 | plus badge repeated 3x; folder body 291-293 = 312-314 | small
- src/dash_icons.c iconSdf | DI_ ids never drawn anywhere (check each enum name repo-wide)
- src/dash_image.c:76-80 clampPan single caller wrapper | trivial
- src/dash_image.c:98-104 state NULL fallback: check callers pass NULL ever
- src/dash_image.c:123 snprintf heading; fine
- src/dash_info.c:224-268 nine #ifdef/else feature blocks -> could be feature(&s,"x", IS_DEFINED) via macro; small
- src/dash_info.c:269-285 debug block for macros no build defines (UDPTTY, POWERPC_UART, NO_IOP_RESET; SIO_DEBUG in 1 build file?) | check
- src/dash_info.c:348,355 static SysInfo job copy of cached_console_info -> use directly
- src/dash_info.c:387-390 gsColor helper: dup elsewhere?
- src/dash_info.c:326-343 dashConsoleSummaryText callers?
- src/dash_jpeg.c:43 dashJpegDecode exported? only caller in-file?; DashJpegLoadFile uses dashLoadTextFile (fine)
- src/dash_jpeg.c:56-57 out NULL checks; 171 dup check
