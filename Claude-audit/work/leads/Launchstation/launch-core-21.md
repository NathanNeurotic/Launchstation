## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `ILINK`  tested at 28 site(s) in the repo, mentioned in 7 build file(s); here: include/init.h:24
- `MMCE`  tested at 44 site(s) in the repo, mentioned in 43 build file(s); here: include/init.h:48
- `UDPFS`  tested at 87 site(s) in the repo, mentioned in 25 build file(s); here: include/init.h:84
- `__cplusplus`  tested at 8 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/hdl_rpc.h:17, include/hdl_rpc.h:25

## Largely identical files (dupes-file-pairs.tsv)
- src/hdd_header_injector.c repeats itself: 15 cloned normalised lines inside the file (1%)

## Duplicate blocks (dupes-blocks.tsv)
- src/hdd_header_injector.c:564-582  ==  Launchstation/src/hdd_header_injector.c:594-612  (15 normalised lines)
- src/hdd_header_injector.c:594-612  ==  Launchstation/src/hdd_header_injector.c:564-582  (15 normalised lines)
