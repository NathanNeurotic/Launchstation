## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- ee_libs/hdd/include/libhdd.h:23  macro `PFS_MT_ROBUST`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:31  macro `FS_TYPE_EXT2_SWAP`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:32  macro `FS_TYPE_EXT2`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:33  macro `FS_TYPE_REISER`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:35  macro `FS_TYPE_CFS`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:87  macro `hddPreparePoweroff`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:88  macro `hddSetUserPoweroffCallback`  (no-other-mention)
- ee_libs/hdd/include/libhdd.h:89  macro `hddPowerOff`  (no-other-mention)
- ee_libs/hdd/libhdd.c:135  function `hddGetFilesystemList`  (only-prototype-or-export-mentions) | also mentioned: ee_libs/hdd/include/libhdd.h:74 extern int hddGetFilesystemList(t_hddFilesystem hddFs[], int maxEntries);
- ee_libs/hdd/libhdd.c:253  function `hddGetInfo`  (only-prototype-or-export-mentions) | also mentioned: ee_libs/hdd/include/libhdd.h:75 extern void hddGetInfo(t_hddInfo *info);
- ee_libs/hdd/libhdd.c:459  function `hddRemoveFilesystem`  (only-prototype-or-export-mentions) | also mentioned: ee_libs/hdd/include/libhdd.h:77 extern int hddRemoveFilesystem(t_hddFilesystem *fs);
- ee_libs/hdd/libhdd.c:473  function `hddExpandFilesystem`  (only-prototype-or-export-mentions) | also mentioned: ee_libs/hdd/include/libhdd.h:78 extern int hddExpandFilesystem(t_hddFilesystem *fs, int extraMB);
- ee_libs/iopreboot/SifIopRebootBuffer.c:124  function `SifIopRebootBufferEncrypted`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:114  function `ps2ipc_accept`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:144  function `ps2ipc_bind`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:219  function `ps2ipc_listen`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:385  function `ps2ipc_sendto`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:459  function `ps2ipc_ps2ip_setconfig`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:483  function `ps2ipc_ps2ip_getconfig`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:616  function `ps2ipc_getsockname`  (no-other-mention)
- ee_libs/tcpips/ps2ipc.c:670  function `ps2ipc_getsockopt`  (no-other-mention)
- include/dash_internal.h:23  macro `DASH_SAFE_T`  (no-other-mention)
- include/dash_internal.h:24  macro `DASH_SAFE_B`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `CUSTOM_COLORS`  tested at 1 site(s) in the repo, mentioned in 1 build file(s); here: include/config_private.h:10
- `F_SifIopRebootBufferEncrypted`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: ee_libs/iopreboot/SifIopRebootBuffer.c:123
- `F_SifIopRebootBuffer`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: ee_libs/iopreboot/SifIopRebootBuffer.c:159
- `F__iopcontrol_special_internals`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: ee_libs/iopreboot/SifIopRebootBuffer.c:64
- `LIBCDVD_LEGACY`  tested at 4 site(s) in the repo, mentioned in 0 build file(s); here: include/cdvd_macro.h:4
- `WLE_NATIVE_CLOUD`  tested at 25 site(s) in the repo, mentioned in 3 build file(s); here: include/dash_internal.h:1034
- `__cplusplus`  tested at 8 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: ee_libs/hdd/include/libhdd.h:67, ee_libs/hdd/include/libhdd.h:80

## Largely identical files (dupes-file-pairs.tsv)
- ee_libs/hdd/libhdd.c repeats itself: 29 cloned normalised lines inside the file (9%)
- ee_libs/tcpips/ps2ipc.c repeats itself: 12 cloned normalised lines inside the file (2%)

## Duplicate blocks (dupes-blocks.tsv)
- ee_libs/hdd/libhdd.c:380-442  ==  Launchstation/ee_libs/hdd/libhdd.c:503-564  (29 normalised lines)
- ee_libs/hdd/libhdd.c:503-564  ==  Launchstation/ee_libs/hdd/libhdd.c:380-442  (29 normalised lines)
- ee_libs/tcpips/ps2ipc.c:267-282  ==  Launchstation/ee_libs/tcpips/ps2ipc.c:301-316  (12 normalised lines)
- ee_libs/tcpips/ps2ipc.c:301-316  ==  Launchstation/ee_libs/tcpips/ps2ipc.c:267-282  (12 normalised lines)
