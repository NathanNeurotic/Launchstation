# launch-core-12.b notes (lens defects)

Done: src/dash_settings.c 1-4103 read fully. Verification finished.

Confirmed (to report):
- 1442-1453, 1492-1574, 3984-3986 | classic colour edit wiped each frame: colorEditor -> dashRenderScene (dash_main.c:1210) -> settingsDraw -> colorField refreshes classic_col from setting->color | verified, no scene override pushed by settings
- 688-700, 3376-3381, 3405-3411 | dashModalKeyboard max_len = sizeof(buf): kbInsert (dash_widgets.c:341-350) allows strlen == max_len -> 1-byte stack overwrite; edit[96] < folder[128] | verified; SMB and ETH on by default (brief, ps2.yaml:624,627)
- 1917-1919, 565-567, 3482-3484, 496/3786 | UI-thread file reads without dashWorkerAwaitAll; pathApplied 2122-2125 guards the same Load_External_Language | verified dashLoadTextFile has no guard (dash_store.c:105-139); harm INFERRED

Dropped:
- themeIo / saveClassicAs UAF: dashWorkerAwait returns 1 only after done callback ran; no UAF
- S_LK enum collision: values fit
- SMBD user/pass strcpy: edit[32] with sizeof-1, fields are 32
- preset pnames overflow: 18 presets < 32
- legacy theme revert, editScanPaths defaults, revert screen mode, refreshDirty cost, folder list cap, cfgSaveRun cancel, ip fields, consoleHasIlink, classic alpha 0: no defect shown or not worth reporting
- S_CLOCK_SYNC UI-thread SNTP: shows loading frames per attempt; not reported
