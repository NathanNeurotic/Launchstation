# launch-core-17 notes

Done: src/filer.c 1-2578 (all), include/filer_actions.h 1-52 (all). Candidates verified; writing findings JSON.

Kept: setPartyList dead; FILEOP_TRACE block unused in filer.c; LIBCDVD_LEGACY path + snippets/readCD.h; EXFAT #else branches; DVRP copies of HDD party/reader code; commented CB filter x2; ATA retry loop dup of readGENERICWithFirstOpenRetry; readMC/readXFROM tail dup; canonicalize twins; write-only/constant globals (mcfreeSpace, host_ready, host_use_Bsl, root_mc_poll_format, readHOST Win_path stores); stale banners; scan_USB_mass wrapper + makeHostPath no-op goto; exported file-local functions.
Dropped: globals list at 53-56/112-118/208-211 (all used elsewhere), genFixPath slash strip helper (no net win), USB_mass_max_drives (written in init.c).
