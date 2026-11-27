# launch-core-17.b notes (lens defects)

Done: src/filer.c 1-2578 (read in full), include/filer_actions.h 1-52 (read in full). Checks done; writing findings.

Kept:
- F1 retry helper + ATA/MX4SIO loops wait full budget on missing folder / empty root (themes list, lib scan proof)
- F2 hddParties/dvrParties 138,600 B bss (MEASURED BUILD-EVIDENCE:83), names only used by root listing
- F3 mcDir 131,136 B bss (MEASURED), only used during readMC/readXFROM
- F4 readHOST elflist hostcount-1 + makeFslPath into 256-byte name
- F5 readCD wait path rejects DVDV/CDDA and stops drive
- F6 genFixPath sprintf party[MAX_NAME] overflow (config LANG_FILE/CNF_Path)
- F7 overlapping strcpy at 977/991/1016/1025

Dropped: root MC poll concurrency (guarded by dashWorkerIoBusy), readHDD root max (1400 < 2046), info[max] writes (arrays MAX_ENTRY), blank MISC spacer (intentional, filer_browser.c:978), double USB scan (plausible purpose), -2 unformatted card absent (consistent with browse).
