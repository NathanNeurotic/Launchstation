# launch-core-04.b notes (lens defects)

## Done
- src/config.c 1-1782 (read fully, re-read in second attempt)
- src/config_advanced.c 1-329
- src/config_network.c 1-362
- src/config_screen.c 1-361
- src/config_startup.c 1-364

## Candidates -> decisions
- config.c:117-128 get_CNF_string hang on byte >= 0x80 at line start/comment (BOM) | KEEP rank 4; callers loadConfig (boot, main_boot.c:64) + readSystemCnf (main_startup.c:178)
- config.c:962,972-1004,1039-1067 unbounded strcpy/sprintf from CNF into SETTING | KEEP rank 2
- config.c:429-432 icon written 3x (same 14990 B); makeicon.c:408-410 uses one name | KEEP rank 2
- config.c:229-243,258-259 mc1 probe without mc1AccessRefused -> IOP reset on UI thread via hdd header editor (dash_hdd.c:956-966) | KEEP rank 3
- config.c:1363 redundant re-probe in configSnapshotSaveTargetPath | KEEP rank 1
- config.c:1674 strcat, config_network BuildOctets uninit: classic config() reachability unclear (dash redirects Misc_Configure dash_main.c:456) | DROP
- config.c:814 LNG vs LNG_DEF: loadConfig only at boot after Init_Default_Language | DROP
- PathPad negative index: needs 64-bit long, unverified | DROP
- preloadCNFFd size cap, malloc unchecked, Misc offset: theoretical | DROP
- worker IOP reset via configEnsureSysconfDir: refused on worker (init.c:2168) | DROP
