## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/mc_save_formats.c:878  function `mcSaveFormatExt`  (only-prototype-or-export-mentions) | also mentioned: include/mc_save_formats.h:61 const char *mcSaveFormatExt(int format);
- src/mc_save_formats.c:891  function `mcSaveFormatName`  (only-prototype-or-export-mentions) | also mentioned: include/mc_save_formats.h:62 const char *mcSaveFormatName(int format);

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `WLE_PS2BUILD`  tested at 2 site(s) in the repo, mentioned in 2 build file(s); here: src/mmce_igr.c:16

## Largely identical files (dupes-file-pairs.tsv)
- src/mmce_backup.c repeats itself: 39 cloned normalised lines inside the file (5%)

## Duplicate blocks (dupes-blocks.tsv)
- src/mmce_backup.c:745-770  ==  Launchstation/src/mmce_backup.c:923-948  (16 normalised lines)
- src/mmce_backup.c:791-819  ==  Launchstation/src/mmce_backup.c:968-996  (23 normalised lines)
- src/mmce_backup.c:923-948  ==  Launchstation/src/mmce_backup.c:745-770  (16 normalised lines)
- src/mmce_backup.c:968-996  ==  Launchstation/src/mmce_backup.c:791-819  (23 normalised lines)
