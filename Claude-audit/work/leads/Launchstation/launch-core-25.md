## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/main_title.c:680  function `menuTitleFormat`  (only-prototype-or-export-mentions) | also mentioned: include/main_title.h:6 void menuTitleFormat(char *out, size_t out_size);

## Comment blocks that look like code (cond-commented-code.tsv)
- src/main_info_screens.c:312-338  22 code-like lines

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `NO_IOP_RESET`  tested at 7 site(s) in the repo, mentioned in 0 build file(s); here: src/main_info_screens.c:173
- `WLE_BOOT_DIAGNOSTICS`  tested at 6 site(s) in the repo, mentioned in 0 build file(s); here: src/main_modules.c:32, src/main_modules.c:36, src/main_modules.c:51, src/main_modules.c:55
