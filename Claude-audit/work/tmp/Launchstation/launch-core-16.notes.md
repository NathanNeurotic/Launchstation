# launch-core-16 notes (lens structure)

## Read (all complete, re-read after compaction)
- all 8 files read fully (second pass done for verification)

## Verified candidates -> findings
- classic editor superseded by dash_editor.c; remaining callers filer_browser.c:1554, main_actions.c:231,771, hdd_header_injector.c:826, dash_hdd.c:967; sizes from wLaunchELF-contrib-by-object.tsv (48,084 B); EditorSpecialChars used by dash_widgets.c:54,68
- editor_input.c:372,647 leak (free merged into comment)
- editor_input.c pad/USB duplication
- genFixPath pre-copy dead (filer.c:956 overwrites); fallback pattern repeats repo-wide
- editor_file.c save dup + dead CRLF branch + 5 busy waits
- editor small leftovers: HFONTS (map evidence), always-true conds, commented code
- editor_rules row-find x4
- elf.c WLE_PS2BUILD dead else (+mmce_igr.c:16-24)
- elf.c isElfPayload/isLikelyEncryptedPayload vs classifyExecHeader; 623 isHddPartyPath
- elf.c launchArgSource write-only
- elf.c leftovers: MAX_PATH redefine, unreachable else 775-779/797-801, braces, (void)exec_kind
- ember.c copyElf vs psu_functions.c:127-162
- embed_blobs.S three macros
- dropped: embedded .sys region payload dedupe (risky, small)
