# launch-module-03 notes
Done reading: mmce_fs.c 1-1482, mmce_sio2.c 1-602, mmcedrv.c 1-413, mmceigr.c 1-168, mmceman.c 1-107, sio2man_hook.c 1-320 (all full)

Candidates:
- mmcedrv.c whole + mmceigr.c whole + exports.tab + mmcedrv_config.h | not in ps2.yaml mmceman target | check repo-wide refs, RiptOPL copies
- mmce_sio2.c:245-311 mmce_sio2_rx_dma only used by mmcedrv.c | grep
- mmce_sio2.c:169-172 mmce_sio2_get_port no callers | grep
- mmce_sio2.c:35 timeout_200ms unused? | grep
- mmce_sio2.c:32-33,85-86 intr handler ptr indirection | trivial
- mmce_sio2.c wait/alarm/clear block x6 ; tx_mixed 483-518 vs 523-557 duplicate branch | count
- mmce_fs.c:625-815 remove/mkdir/rmdir identical except cmd | count
- mmce_fs.c:993-1038 vs 1133-1178 stat decode dup | count
- mmce_fs.c:13 MMCE_FS_WAIT_TIMEOUT unused? | grep
- mmce_fs.c:573 redundant ternary; 945/957 duplicate DPRINTF; 198-215 update_unit return unused, port=2 redundant
- mmce_fs.c:1413-1426 ioctl2 TEMP GET_FD; devctl SETTINGS cases | grep EE users
- mmce_fs.c:32-33 fwd decls maybe in header | check mmce_fs.h
- sio2man_hook.c PORT_NR blocks dead (46-48,60-87,117-123,277-283) | check defines, mx4sio copy
- cross-repo: mmce_sio2.c 100% = RiptOPL; sio2man_hook.c 98% = mx4sio_bd + RiptOPL
