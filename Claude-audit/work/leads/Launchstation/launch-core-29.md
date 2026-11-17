## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- include/qrcodegen.h:144  macro `qrcodegen_BUFFER_LEN_MAX`  (no-other-mention)
- src/qrcodegen.c:878  function `qrcodegen_makeBytes`  (only-prototype-or-export-mentions) | also mentioned: include/qrcodegen.h:340 struct qrcodegen_Segment qrcodegen_makeBytes(const uint8_t data[], size_t len, uint8_t buf[]);
- src/qrcodegen.c:961  function `qrcodegen_makeEci`  (only-prototype-or-export-mentions) | also mentioned: include/qrcodegen.h:361 struct qrcodegen_Segment qrcodegen_makeEci(long assignVal, uint8_t buf[]);

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `QRCODEGEN_TEST`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: src/qrcodegen.c:30
- `__cplusplus`  tested at 8 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: include/qrcodegen.h:31, include/qrcodegen.h:383
