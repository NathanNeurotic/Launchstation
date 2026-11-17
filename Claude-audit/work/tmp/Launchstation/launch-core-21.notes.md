launch-core-21 notes
DONE reading: all 14 files fully (gui_colors.c, gui_hdd0_format.h/.c, gui_neutrino.h/.c, gui_sort.h/.c, gui_texteditor.h/.c, hdd_header_injector.h/.c, hdl_rpc.h/.c, init.h)

Candidates:
- src/gui_neutrino.c:392-406,408-504 | classic Neutrino dialog only reachable when DashIsActive()==0 i.e. IGR after-app pre-DashRun (main.c:221-243), picker restricts ELF (dash_settings.c:2074) | verified; moveNeutrinoSelection 264-280 only used by classic; stale comment 348-350
- src/gui_neutrino.c:93-261 | two-slot versioned profile + FNV checksum + post-write reread both slots; dashWriteFile (dash_store.c:148-175) already reads back | verified
- src/hdl_rpc.c:24,48 + dash_hdd.c:419-437 + init.c:2998 | Hdl_Inited not cleared on IOP reset; stale RPC client -> HdlGetGameInfo after reset | verified path
- src/hdd_header_injector.c:826 + dash_hdd.c:951-967 | fn pointer hook + forwarding wrapper, single caller | verified
- src/hdd_header_injector.c:299-319 vs 403-423 | duplicate wait loops
- src/hdd_header_injector.c:561-624 | Read/WriteRawRange dup (lead)
- src/hdd_header_injector.c:154-159,887-892,903-909 | boot.kelf alt name ad hoc x3, redundant reopen retry
- src/hdd_header_injector.c:698-803 | genFixPath-or-copy pattern x4 in temp helpers
- src/hdd_header_injector.c:89 | 32 KB static copy buffer used only during injection
- defensive NULL checks on static helpers
- src/gui_colors.c:4-13 | hextou32 vs strtoul
- src/gui_hdd0_format.c:15-75 + filer_mount.c:190-207 | party parsing x3
- src/gui_sort.c:5 cmpFile exported only for tests; dash_browser.c:28 unused include
- init.h symbol sweep: running
