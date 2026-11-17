# launch-core-26.b notes (lens defects)

Done (read_full): src/mc_card_view.c 1-543, include/mc_card_write.h 1-117, src/mc_card_write.c 1-1116,
include/mc_ps1_card.h 1-128, src/mc_ps1_card.c 1-445, include/mc_save_export.h 1-99,
src/mc_save_export.c 1-504, include/mc_save_formats.h 1-99. (context: include/mc_card_view.h read)

Checked callers: dash_card_tool.c (ctRead 4x4KB cache, ctWrite lseek+write per call, ctPutSave double verify,
UI thread, no worker), mmce_backup.c (fp cache), dash_ps1_card.c (p1Load temp buffer), mcPsvFileName callers sizes ok.

Kept:
- F1 double read-back: mc_card_write.c:653-662 + dash_card_tool.c:415-426 (rank 2)
- F2 1 KB per write call for save data: mc_card_write.c:413-416, 569-590 (rank 2, INFERRED timing)
- F3 ListSaves/describeSave per-entry chain re-walk: mc_card_view.c:165-178, 288-319, 321-360 (rank 1)

Dropped: readCluster 2x512 reads (cached by ctRead), mcPsvFileName size (callers >= 48), stack depth (static views, 128 KB stacks),
psu calloc before len check (theoretical), mode FILE|DIR (formats sanitize), MCX in-place decrypt (temp buffer).
