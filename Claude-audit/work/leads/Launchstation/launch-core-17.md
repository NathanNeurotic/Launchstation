## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/filer.c:775  function `setPartyList`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `LIBCDVD_LEGACY`  tested at 4 site(s) in the repo, mentioned in 0 build file(s); here: src/filer.c:604
- `ULE_DEBUG_BUILD`  tested at 5 site(s) in the repo, mentioned in 0 build file(s); here: src/filer.c:156

## Largely identical files (dupes-file-pairs.tsv)
- src/filer.c repeats itself: 77 cloned normalised lines inside the file (4%)

## Duplicate blocks (dupes-blocks.tsv)
- src/filer.c:1149-1164  ==  Launchstation/src/filer.c:1229-1244  (12 normalised lines)
- src/filer.c:1149-1164  ==  Launchstation/src/filer.c:1299-1314  (12 normalised lines)
- src/filer.c:1210-1244  ==  Launchstation/src/filer.c:1281-1314  (28 normalised lines)
- src/filer.c:1229-1244  ==  Launchstation/src/filer.c:1149-1164  (12 normalised lines)
- src/filer.c:1281-1314  ==  Launchstation/src/filer.c:1210-1244  (28 normalised lines)
- src/filer.c:1299-1314  ==  Launchstation/src/filer.c:1149-1164  (12 normalised lines)
- src/filer.c:527-542  ==  Launchstation/src/filer.c:583-598  (12 normalised lines)
- src/filer.c:583-598  ==  Launchstation/src/filer.c:527-542  (12 normalised lines)
- src/filer.c:742-765  ==  Launchstation/src/filer.c:846-868  (13 normalised lines)
- src/filer.c:846-868  ==  Launchstation/src/filer.c:742-765  (13 normalised lines)
