# launch-module-03.b notes (lens defects)

Done: iop/mmceman/src/mmce_fs.c 1-1482

Candidates:
- mmce_fs.c:287 | (flags&3)-1 -> 0xFF packed flags when flags&3==0 (create/trunc/excl/append all set) | check EE callers of fileXioOpen on mmce paths with flags 0 / O_RDONLY
- mmce_fs.c:879-880 | dopen accepts any fd != 0xff (open checks < 16); garbled fd >= 250 stored | low, maybe drop
- mmce_fs.c:1072 | getstat u8 wrbuf[0xFF] on stack, only 4 bytes used | check stack of fileXio thread; rank 1
- mmce_fs.c:1343,1356,1397,1402 | devctl derefs *(u32*)arg without arglen check; GET_GAMEID no buflen | check EE callers; likely drop
- mmce_fs.c:198-215 | update_unit outside lock; last_unit stale if someone else calls mmce_sio2_set_port | grep set_port callers
- mmce_fs.c:554-563 | write bytes_written not clamped to size (read is) | low
- mmce_fs.c:1469-1477 | register return 1 success/0 fail | check caller mmceman.c
