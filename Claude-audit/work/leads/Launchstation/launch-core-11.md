## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/dash_main.c:126  function `dashVisibleSectionName`  (only-prototype-or-export-mentions) | also mentioned: include/dash_internal.h:1057 const char *dashVisibleSectionName(int index);
- src/dash_main.c:200  function `DashIsEnabled`  (only-prototype-or-export-mentions) | also mentioned: include/dash.h:18 int DashIsEnabled(void);

## Comment blocks that look like code (cond-commented-code.tsv)
- src/dash_ps1_card.c:13-15  3 code-like lines

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `DASH_AUTOTEST`  tested at 3 site(s) in the repo, mentioned in 0 build file(s); here: src/dash_main.c:1304
- `DASH_THEME_TOUR`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: src/dash_main.c:1492
- `WLE_BOOT_DIAGNOSTICS`  tested at 6 site(s) in the repo, mentioned in 0 build file(s); here: src/dash_main.c:1354, src/dash_main.c:1368

## Largely identical files (dupes-file-pairs.tsv)
- src/dash_png.c repeats itself: 16 cloned normalised lines inside the file (9%)

## Duplicate blocks (dupes-blocks.tsv)
- src/dash_png.c:158-175  ==  Launchstation/src/dash_png.c:183-200  (16 normalised lines)
- src/dash_png.c:183-200  ==  Launchstation/src/dash_png.c:158-175  (16 normalised lines)
