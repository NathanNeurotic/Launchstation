## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- iop/vmcman/src/mcman-internal.h:69  macro `MODVER`  (no-other-mention)

## Disabled blocks (cond-if0.tsv)
- iop/vmcman/src/ps1mc_fio.c:175-182  #if 0 (8 lines)
- iop/vmcman/src/ps1mc_fio.c:189-193  #if 0 (5 lines)
- iop/vmcman/src/ps1mc_fio.c:227-234  #if 0 (8 lines)
- iop/vmcman/src/ps1mc_fio.c:525-529  #if 0 (5 lines)

## Comment blocks that look like code (cond-commented-code.tsv)
- iop/vmcman/src/mcdev.c:232-251  13 code-like lines

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `BUILDING_DONGLEMAN`  tested at 7 site(s) in the repo, mentioned in 1 build file(s); here: iop/vmcman/src/mcman-internal.h:371, iop/vmcman/src/mcman-internal.h:66, iop/vmcman/src/mcsio2.c:351, iop/vmcman/src/mcsio2.c:928
- `BUILDING_XMCMAN`  tested at 10 site(s) in the repo, mentioned in 4 build file(s); here: iop/vmcman/src/mcman-internal.h:48, iop/vmcman/src/mcsio2.c:278, iop/vmcman/src/ps1mc_fio.c:484
- `SIO2MAN_V2`  tested at 2 site(s) in the repo, mentioned in 3 build file(s); here: iop/vmcman/src/mcman-internal.h:49
- `SIO_DEBUG`  tested at 11 site(s) in the repo, mentioned in 1 build file(s); here: iop/vmcman/src/mcman-internal.h:79

## Largely identical files (dupes-file-pairs.tsv)
- iop/vmcman/src/mcman-internal.h is 9% the same as RiptOPL/modules/vmc/genvmc/genvmc.c (28 cloned normalised lines)
- iop/vmcman/src/mcman-internal.h is 9% the same as RiptOPL/tools/pc/genvmc/src/genvmc.c (28 cloned normalised lines)
- iop/vmcman/src/mcsio2.c repeats itself: 20 cloned normalised lines inside the file (2%)

## Duplicate blocks (dupes-blocks.tsv)
- iop/vmcman/src/mcman-internal.h:300-327  ==  RiptOPL/modules/vmc/genvmc/genvmc.c:92-119  (28 normalised lines)
- iop/vmcman/src/mcman-internal.h:300-327  ==  RiptOPL/tools/pc/genvmc/src/genvmc.c:40-67  (28 normalised lines)
- iop/vmcman/src/mcsio2.c:1037-1067  ==  Launchstation/iop/vmcman/src/mcsio2.c:1103-1133  (20 normalised lines)
- iop/vmcman/src/mcsio2.c:1103-1133  ==  Launchstation/iop/vmcman/src/mcsio2.c:1037-1067  (20 normalised lines)

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- iop/vmcman/src/mciomanx_backing.c:289  // FIXME: implement
- iop/vmcman/src/mcman-internal.h:158  // FIXME: The r/w/x flag order is opposite from iomanX
