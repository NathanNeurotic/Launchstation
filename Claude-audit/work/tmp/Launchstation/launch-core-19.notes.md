# launch-core-19 notes

STATUS: findings file written and validated OK (10 findings).

Done: src/filer_browser.c 1-2528 read (second attempt re-read all). include/filer_copy.h 1-18 read.
Classic browser NON_CNF mode is live: main_actions.c:738,774,886 call getFilePath(tmp, FALSE) (Misc FileBrowser, text editor). Not dead.

Candidates:
- filer_browser.c:2315-2318 | filerPasteInto only prototype launchelf.h:589 | grep
- 1501-1550 vs 2347-2395 vs 2397-2416 | VMC mount/unmount duplicated 3x | write finding
- 1457-1500 vs 2420-2460 | NEWICON vs filerCreateFolderIcon | write finding
- 1223-1229 vs 2517-2528 | LaunchElfDir path fix vs filerLaunchElfFolder | check equivalence
- 958-971, 1122 | USBD_IRX_CNF / USBMASS_IRX_CNF never passed | grep enum users
- 2029-2033, 2080-2104, 2123, 1819, 2232, 944 | commented-out code
- 1741,1745 | recompute same as 1724-1725
- 1591-1592 TEST comments; 2310-2311 stale end-of-file banner
- 2281-2330 | submenu_func_Paste duplicates filerPasteModeInto; filerPsuPasteInto forwarder
- 2166-2238 | printf [PASTE] traces in release; check DPRINTF def
- 640-649,1928-1969,755-777 | swapKeys duplicated sprintf
- 142-148 markMx4sioDestinationAfterWrite single caller; check discardNextMx4sioRootListing
- 126-140 probeDirectory copy; genDopen signature
- 278-326 hdd/dvr display blocks
- filer_copy.h: check each prototype used
