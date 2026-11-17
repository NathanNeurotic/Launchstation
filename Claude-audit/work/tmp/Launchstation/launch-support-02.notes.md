Done: all 46 files read in full (test_dash_card_tool.py 1-1421 done). Now verifying.

Candidates:
- tests/mc_*_host.c slurp/spit/rd dup: mc_ps1_card_host.c:15-40, mc_save_export_host.c:14-39, mc_save_formats_host.c:9-29; rd in mc_card_view_host.c:6-13, mc_card_write_host.c:11-18, mc_fingerprint_host.c:8-15
- dash_card_tool_host.c:70-135,189-227 vs mmce_save_versions_host.c:20-105 POSIX gen* stubs dup
- pccf_ata_card.h | who includes
- function()/extract() brace extractor copied in python tests | count repo-wide
- pure source-text tests: test_boot_deferred_init.py (whole), test_cloud_relay.py (whole), test_cloud_tls_paths.py 16-20 + 22-65
- self-referential workflow asserts: test_cloud_upload_ledger 15-16, test_dash_card_tool 23-24
- removed-name asserts: test_copy_pipeline 16-20, test_copy_short_io 54
- dead stubs/vars: test_clock_save_action:34, test_copy_short_io:31, test_dash_card_tool:275
- dash_browser.c re-read: test_dash_card_tool 433, 1411 (browser at 25)
- git-ref argv modes: test_copy_short_io 13-14, test_cursor_sound_ticks 8,20-23 | check compile.yml
- test_copy_pipeline 41-135 vs test_copy_psu_listing 57-184 shared prefix
