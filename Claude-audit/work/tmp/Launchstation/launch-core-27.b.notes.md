# launch-core-27.b notes (lens defects)

Done: src/mc_save_formats.c 1-954, include/mc_sha256.h, src/mc_sha256.c, include/mmce_backup.h, src/mmce_backup.c 1-1002 (all full)

Candidates:
- src/mc_save_formats.c:242-244 | mcSaveFromMcm returns fail() without free(raw) when startSave fails (name hint fails nameOk, e.g. 32-char or non-ASCII base name; calloc fail) -> inflated buffer leaked | caller dash_card_tool.c:706-718 passes file base name truncated to 32: verified
- mc_save_formats.c:952 no-op ternary (both branches f) -> other lens (dead code), skip
- mmce_backup.c:565-621 | PCSX2 format: no compare before export; unchanged card is exported (8.25 MB write + verify read) then compared and deleted; files_equal_pcsx2_history (399-435, used by scan_one 672-676) could pre-compare | verified mc_card_export.c:443-498 write+verify passes; callers verify=1 at dash_browser.c:1523, dash_tools.c:619
- mmce_backup.c:136-164,437-484 + 486-517 | name ordering: "-NN" suffix sorts below base name ('-'<'.'), S-fallback names (Timer since boot) not chronological -> newest_history picks wrong copy | trigger needs same-second stamp or clock fail; rank 2, Med
- mmce_backup.c:17-18 compare_a/b 128 KB static .bss (MEASURED BUILD-EVIDENCE:83,91), only used during backup compare; sv_io, export_in/out/verify_out similar in other files | check mmce_igr.c buffers too
- dropped: AllCards allows dest on MMCE (policy), case-insensitive MemoryCards prefix (rare), join_path truncation at 553 (only leads to failure)
