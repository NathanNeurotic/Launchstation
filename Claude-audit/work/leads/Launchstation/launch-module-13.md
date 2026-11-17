## Disabled blocks (cond-if0.tsv)
- iop/vmcman/src/main.c:2913-2920  #if 0 (8 lines)
- iop/vmcman/src/main.c:3360-3364  #if 0 (5 lines)
- iop/vmcman/src/main.c:3372-3375  #if 0 (4 lines)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `BUILDING_DONGLEMAN`  tested at 7 site(s) in the repo, mentioned in 1 build file(s); here: iop/vmcman/src/main.c:106, iop/vmcman/src/main.c:1268
- `BUILDING_VMCMAN`  tested at 93 site(s) in the repo, mentioned in 4 build file(s); here: iop/vmcman/src/main.c:164, iop/vmcman/src/main.c:31, iop/vmcman/src/main.c:39
- `BUILDING_XFROMMAN`  tested at 60 site(s) in the repo, mentioned in 2 build file(s); here: iop/vmcman/src/main.c:31, iop/vmcman/src/main.c:39, iop/vmcman/src/main.c:41
- `BUILDING_XMCMAN`  tested at 10 site(s) in the repo, mentioned in 4 build file(s); here: iop/vmcman/src/main.c:3863, iop/vmcman/src/main.c:455
- `SIO_DEBUG`  tested at 11 site(s) in the repo, mentioned in 1 build file(s); here: iop/vmcman/src/main.c:160
- `_WIN32`  tested at 2 site(s) in the repo, mentioned in 2 build file(s) [builtin-like]; here: iop/vmcman/src/main.c:1147

## Largely identical files (dupes-file-pairs.tsv)
- iop/vmcman/src/main.c repeats itself: 39 cloned normalised lines inside the file (1%)

## Duplicate blocks (dupes-blocks.tsv)
- iop/vmcman/src/main.c:2875-2891  ==  Launchstation/iop/vmcman/src/main.c:2951-2967  (12 normalised lines)
- iop/vmcman/src/main.c:2951-2967  ==  Launchstation/iop/vmcman/src/main.c:2875-2891  (12 normalised lines)
- iop/vmcman/src/main.c:3651-3666  ==  Launchstation/iop/vmcman/src/main.c:3675-3690  (13 normalised lines)
- iop/vmcman/src/main.c:3675-3690  ==  Launchstation/iop/vmcman/src/main.c:3651-3666  (13 normalised lines)
- iop/vmcman/src/main.c:503-527  ==  Launchstation/iop/vmcman/src/main.c:585-609  (14 normalised lines)
- iop/vmcman/src/main.c:585-609  ==  Launchstation/iop/vmcman/src/main.c:503-527  (14 normalised lines)

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- iop/vmcman/src/main.c:15  // TODO: last sdk 3.1.0 has MCMAN module version 0x2,0x30 with significant changes.
