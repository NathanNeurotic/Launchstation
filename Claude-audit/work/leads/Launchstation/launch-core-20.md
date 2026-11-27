## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `ULE_DEBUG_BUILD`  tested at 5 site(s) in the repo, mentioned in 0 build file(s); here: src/filer_copy.c:22, src/filer_fileops.c:5, src/filer_mount.c:6

## Largely identical files (dupes-file-pairs.tsv)
- src/gui.c is 4% the same as Launchstation/src/main_exploit_installer.c (15 cloned normalised lines)
- src/gui.c repeats itself: 12 cloned normalised lines inside the file (3%)

## Duplicate blocks (dupes-blocks.tsv)
- src/gui.c:111-126  ==  Launchstation/src/gui.c:20-35  (12 normalised lines)
- src/gui.c:20-35  ==  Launchstation/src/gui.c:111-126  (12 normalised lines)
- src/gui.c:22-40  ==  Launchstation/src/main_exploit_installer.c:807-825  (15 normalised lines)
