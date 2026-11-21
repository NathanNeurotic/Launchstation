# launch-core-08.b notes (lens defects)

## Done
- src/dash_devices.c 1-921 (read full, re-read in attempt 2)
- src/dash_editor.c 1-1494 (read full)
- src/dash_exploit.c 1-169 (read full, no findings)
- src/dash_files.c 1-734 (read full)

## Verified candidates -> findings
- F1 dash_devices.c:738-773 + dash_browser.c:679-694 | dashDevicesStart reads caller's dev after dashWorkerAwaitAll pumps usbMaskRescanDone/dashNetConfigWarmDone -> dashBrowserRefreshRoot rebuilds fb.devs -> wrong device probed | verified (dash_worker.c:221-229,298-310,340-344; dash_browser.c:475-521,3573-3582; dash_main.c:1242-1246)
- F2 dash_devices.c:485-493 + dash_browser.c:486-501 | VMC rows listed twice in File Browser root | verified no dedup
- F3 dash_files.c:178-182 | fentries 64 slots for max 35 (21,248 B MEASURED) + fdevs never filled (4,512 B)
- F4 dash_editor.c:173-183 | docInsert deletes selection before 512 KB check
- F5 dash_devices.c:415-418 + dash_files.c:589-593 | PCCF devctl RPC each 0.5 s on UI thread, ignores dashWorkerIoBusy

## Dropped
- dashDevicesStart returns 1 on refused UDPFS reset: all 3 callers treat !=0 as failure.
- autoStartNeeded re-probe: only on boot/misc return/settings, cost unproven.
- static smb/nfs status buffers 256 B: trivial.
- dashDevicesPoll guarded by !dashWorkerIoBusy at dash_main.c:1472.
- edIoRun genFixPath mc1 IOP reset on worker: refused by iopResetBlocked (init.c:2168).
- dashWorkerAwait abandon race: await returns 0 only while job not done; safe.
- storageInput SELECT save: dashStoreSaveSettings is deferred (dash_store.c:659-692).
- text save O_TRUNC in place: doc stays in RAM, by design.
