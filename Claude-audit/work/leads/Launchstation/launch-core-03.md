## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- src/cloud_http.c:104  function `mbedtls_hardware_poll`  (no-other-mention)
- src/cloud_http.c:83  function `mbedtls_ms_time`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `WLE_GOOGLE_CLIENT_ID`  tested at 2 site(s) in the repo, mentioned in 3 build file(s); here: src/cloud_google.c:207, src/cloud_google.c:235
- `WLE_GOOGLE_CLIENT_SECRET`  tested at 1 site(s) in the repo, mentioned in 3 build file(s); here: src/cloud_google.c:238
- `__has_include`  tested at 7 site(s) in the repo, mentioned in 0 build file(s) [builtin-like]; here: src/cloud_google.c:22
- `h`  tested at 4 site(s) in the repo, mentioned in 0 build file(s); here: src/cloud_google.c:23
- `wle_cloud_client`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: src/cloud_google.c:23

## Largely identical files (dupes-file-pairs.tsv)
- src/cloud_google.c repeats itself: 18 cloned normalised lines inside the file (1%)

## Duplicate blocks (dupes-blocks.tsv)
- src/cloud_google.c:1005-1028  ==  Launchstation/src/cloud_google.c:1115-1136  (18 normalised lines)
- src/cloud_google.c:1115-1136  ==  Launchstation/src/cloud_google.c:1005-1028  (18 normalised lines)
