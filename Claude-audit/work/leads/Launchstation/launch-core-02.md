## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- include/launchelf.h:467  macro `WARNING`  (no-other-mention)
- loader/loader.c:34  function `_libcglue_init`  (no-other-mention)
- loader/loader.c:38  function `_libcglue_deinit`  (no-other-mention)
- loader/loader.c:42  function `_libcglue_args_parse`  (no-other-mention)
- loader/loader.c:48  function `_libcglue_timezone_update`  (no-other-mention)
- loader/loader.c:52  function `_libcglue_rtc_update`  (no-other-mention)
- loader/loader.c:84  function `_ps2sdk_fileXio_deinit`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `COMMON_PRINTF`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:160
- `DVRP`  tested at 49 site(s) in the repo, mentioned in 7 build file(s); here: include/filer_internal.h:6, include/launchelf.h:396, include/launchelf.h:570, include/launchelf.h:633
- `LIBCDVD_LEGACY`  tested at 4 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:66, include/snippets/readCD.h:1
- `L`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: include/ftp_protocol.h:25
- `MMCE`  tested at 44 site(s) in the repo, mentioned in 43 build file(s); here: include/launchelf.h:426
- `MX4SIO`  tested at 47 site(s) in the repo, mentioned in 21 build file(s); here: include/launchelf.h:378, include/launchelf.h:429
- `POWERPC_UART`  tested at 8 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:160, include/launchelf.h:7
- `SIO_DEBUG`  tested at 11 site(s) in the repo, mentioned in 1 build file(s); here: include/launchelf.h:157, include/launchelf.h:5
- `UDPFS`  tested at 87 site(s) in the repo, mentioned in 25 build file(s); here: include/filer_internal.h:11, include/filer_shared.h:100, include/filer_shared.h:97, include/launchelf.h:392, include/launchelf.h:583
- `UDPTTY`  tested at 8 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:160, include/launchelf.h:9
- `ULE_DEBUG_BUILD`  tested at 5 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:11
- `XFROM`  tested at 39 site(s) in the repo, mentioned in 6 build file(s); here: include/launchelf.h:423, include/launchelf.h:449
- `__STDC_VERSION__`  tested at 1 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/ftp_protocol.h:25
- `__has_include`  tested at 7 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/launchelf.h:20, include/launchelf.h:21, include/launchelf.h:69, include/launchelf.h:70, include/launchelf.h:72
- `cdvd_rpc`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:72
- `githash`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:21
- `h`  tested at 4 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:21, include/launchelf.h:70, include/launchelf.h:72
- `libcdvd`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:70
- `rpc`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: include/launchelf.h:70

## Largely identical files (dupes-file-pairs.tsv)
- include/nfsman.h is 100% the same as RiptOPL/modules/network/nfsman/nfsman.h (17 cloned normalised lines)
- loader/loader.c is 15% the same as RiptOPL/elfldr/loader.c (39 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- include/nfsman.h:1-30  ==  RiptOPL/modules/network/nfsman/nfsman.h:1-31  (17 normalised lines)
- loader/loader.c:104-125  ==  RiptOPL/elfldr/loader.c:93-111  (14 normalised lines)
- loader/loader.c:117-155  ==  RiptOPL/elfldr/loader.c:104-138  (25 normalised lines)
