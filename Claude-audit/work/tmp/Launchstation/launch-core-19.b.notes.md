# Notes launch-core-19.b (defects lens)

Done: src/filer_browser.c 1-2528 (all read), include/filer_copy.h 1-18 (read). Verification done; writing findings.

Kept:
- 1 busy spin waitUntilTimer 120-124/256-260 (+ hdd_header_injector.c:91-98), UI thread via dash_browser.c opPaste (prepareOp: worker idle) -> rank 1
- 2 incompatible-stack paste: ynDialog 2167 is a Yes/Cancel modal in dashboard (dash_widgets.c:626-632, 672), then showPasteFailure repeats -> rank 1
- 3 classic-only R1 handlers: sizeC[21] unterminated 2124-2131; DoneIcon strcpy from maybe-uninit tmp1 1498-1499 -> rank 1 (classic browse mode not opened by dashboard, docs/dashboard.md:96-97)

Dropped: filerConfirmExploitModify <0 vs >0 (returns 1/-1 only); Party_vmcIndex (flag only, genFixPath returns 99 for non-HDD); LNG tmp[80] overflows (external LNG only, classic-only, shipped strings short); mx4sio discard on failure (consequence unknown); xfrom unchecked (minor).
