# launch-core-21.b notes (lens defects)

Done (all read in full): src/gui_colors.c 1-86, include/gui_hdd0_format.h 1-9, src/gui_hdd0_format.c 1-75, include/gui_neutrino.h 1-26, src/gui_neutrino.c 1-518, include/gui_sort.h 1-9, src/gui_sort.c 1-95, include/gui_texteditor.h 1-8, src/gui_texteditor.c 1-26, include/hdd_header_injector.h 1-20, src/hdd_header_injector.c 1-1160, include/hdl_rpc.h 1-29, src/hdl_rpc.c 1-83, include/init.h 1-88

Candidates:
- src/gui_hdd0_format.c:46-57 | getHddDVRPParty checks only "dvr_hdd" prefix then indexes fullpath[10] | check callers: can path be shorter than "dvr_hdd0:/"?
- src/gui_hdd0_format.c:22-26,49-53 | strcpy/strcat into fullpath[MAX_PATH] | check MAX_PATH vs caller path lengths
- src/gui_sort.c:74-95 | recursive quicksort, first-element pivot, 2 FILEINFO on stack per frame; file_sort==0 or presorted -> depth n | check FILEINFO size, callers, thread stack, whether callers skip sort when file_sort==0
- src/gui_sort.c:29-30 | *(u64*)&stats._Modify alignment | check FILEINFO layout
- src/gui_neutrino.c:232-261 | profileSave on UI thread: 2 reads + write + 2 reads | low perf
- src/hdd_header_injector.c:91-98,299-319,403-423 | busy-wait hddHeaderDelay | check caller thread, existing sleep helper
- src/hdd_header_injector.c:452-478,1142-1150 | 4096 single-sector RPC reads + 4096 writes incl. APA header sectors 0-7 | check caller thread, ps2hdd devctl limits
- src/hdd_header_injector.c:89,1044-1058 | 32 KB static copy buffer only used in injection | could reuse hdd_header_buffer
- src/hdd_header_injector.c:1082-1095 | unmounts pfs0: without updating filer mount tracking | check mountedParty handling in caller
- src/hdd_header_injector.c:385-388 | -ENOMEM discards whole list when more than max | check caller max
- src/hdl_rpc.c:26-50 | Hdl_Inited=1 even when bind times out; busy nop loop up to seconds | check callers + load of hdl_info.irx
- src/hdl_rpc.c:60-80 | strcpy into 33/64-byte packet fields; SifCallRpc unchecked | check callers lengths and IOP side
