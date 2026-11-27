# launch-core-20.b notes (lens defects)

Done: src/filer_copy.c 1-1721, src/filer_device.c 1-231, src/filer_fileops.c 1-808, src/filer_mount.c 1-238,
include/ftp_session.h 1-12, src/ftp_session.c 1-92, src/gui.c 1-406, include/gui_colors.h 1-9. ALL READ.

Candidates (checked):
- filer_copy.c:1427-1436,1557,1591 | buff2 second 1MB buffer, chunk IO synchronous (dash_ui.c:1452-1466) | CONFIRMED
- filer_copy.c:915,928,1179 | MAX_ENTRY listing (896 KB) per folder level held during recursion | CONFIRMED (also filer_actions.c:311,1984)
- filer_copy.c:849,873,927,1117-1118,594-643,942-979 | unbounded path joins into MAX_PATH buffers; delete/getFileSize check | CONFIRMED
- filer_copy.c:820-823 + 1379-1383 | rename-paste cancel copies under original name, overwrites dest file | CONFIRMED
- filer_copy.c:804 | prepareTransferDeviceStacks per entry -> initHOST network RPCs (filer.c:1720-1737) | CONFIRMED
- filer_copy.c:653-655 | PS1 probe leaves 2 bytes in title on failure; copy() callers ignore ret | CONFIRMED
- filer_copy.c:613-626 | PSU title scan: u32 size -> negative int seek -> loop | CONFIRMED
- dropped: gui.c ynDialog msg[512] strcpy (classic path only with override off; reachability unclear)
- dropped: genOpen alt-path fallback doubling failed opens (purpose unknown; other lens)
- dropped: genLseek 32-bit fallback on host >4GB (PC-side truncation unverifiable; Move blocked by dummy getstat)
- ftp_session.c: LsFtpStatus 256 B, aligned 64: no defect
- filer_mount.c: no defect found (unmountDVRPParty negative index guarded by caller elf.c:787)
