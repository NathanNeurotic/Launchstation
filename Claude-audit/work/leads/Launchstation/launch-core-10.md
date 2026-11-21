## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/dash_icons.c:859  function `dashIconsShutdown`  (only-prototype-or-export-mentions) | also mentioned: include/dash_internal.h:511 void dashIconsShutdown(void);
- src/dash_icons.c:864  function `dashIconsTexture`  (only-prototype-or-export-mentions) | also mentioned: include/dash_internal.h:514 GSTEXTURE *dashIconsTexture(void);
- src/dash_icons.c:939  function `dashHeaderIcon`  (no-other-mention)
- src/dash_lib.c:501  function `dashLibShutdown`  (only-prototype-or-export-mentions) | also mentioned: include/dash_internal.h:710 void dashLibShutdown(void);
- src/dash_lib.c:517  function `dashLibRemove`  (only-prototype-or-export-mentions) | also mentioned: include/dash_internal.h:731 int dashLibRemove(int index);

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `NO_IOP_RESET`  tested at 7 site(s) in the repo, mentioned in 0 build file(s); here: src/dash_info.c:269, src/dash_info.c:271
- `POWERPC_UART`  tested at 8 site(s) in the repo, mentioned in 0 build file(s); here: src/dash_info.c:269, src/dash_info.c:282
- `SIO_DEBUG`  tested at 11 site(s) in the repo, mentioned in 1 build file(s); here: src/dash_info.c:269
- `UDPTTY`  tested at 8 site(s) in the repo, mentioned in 0 build file(s); here: src/dash_info.c:269, src/dash_info.c:276
- `XFROM`  tested at 39 site(s) in the repo, mentioned in 6 build file(s); here: src/dash_info.c:234
