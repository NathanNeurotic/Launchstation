## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/filer_actions.c:1393  function `time_manip_custom`  (only-prototype-or-export-mentions) | also mentioned: include/filer_actions.h:14 int time_manip_custom(const char *path, const FILEINFO *file, char *_msg0);; src/filer_actions.c:1910 return time_manip_custom(path, file, message);

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `TMANIP_MORON`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: src/filer_actions.c:1166

## Largely identical files (dupes-file-pairs.tsv)
- src/filer_actions.c repeats itself: 42 cloned normalised lines inside the file (2%)

## Duplicate blocks (dupes-blocks.tsv)
- src/filer_actions.c:1352-1390  ==  Launchstation/src/filer_actions.c:1848-1885  (28 normalised lines)
- src/filer_actions.c:1848-1885  ==  Launchstation/src/filer_actions.c:1352-1390  (28 normalised lines)
- src/filer_actions.c:546-559  ==  Launchstation/src/filer_actions.c:582-595  (14 normalised lines)
- src/filer_actions.c:582-595  ==  Launchstation/src/filer_actions.c:546-559  (14 normalised lines)
