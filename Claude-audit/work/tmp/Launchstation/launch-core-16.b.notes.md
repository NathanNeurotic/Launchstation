# launch-core-16.b notes (lens defects)

## Done
- All 8 files read in full (editor files re-read in attempt 2 for verification).
- findings/Launchstation/launch-core-16.b.json written, validator OK (3 findings).

## Final findings
1. elf.c:730,776-800 bootpath[256] sprintf overflow on long HDD/DVR paths.
2. elf.c:669-711 MBR payload read to fixed 0x01000000 may clobber heap/stack (INFERRED).
3. Classic editor defects (leak per keystroke, row table overflow, mark-delete underflow, unchecked save) - editor INFERRED unreachable.

## Dropped
- embed_blobs.S: all symbols referenced; exploits/list.icn differs from res/memory-card/list.icn; XLF bodies after 128 bytes identical.
- elf.c early-return stale launch args: dashLaunchApp clears args after a failed launch (dash_main.c:672).
- ember.c / ember_gameid.c: nothing found.
- editor_menu.c tmp[64], busy waits, SaveAs path bound: unreachable classic code, not reported separately.
