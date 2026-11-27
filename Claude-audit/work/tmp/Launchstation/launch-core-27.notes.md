# launch-core-27 notes
Done: all 10 files read in full. Verification done. Writing findings JSON.

Findings planned: F1 mcSaveFormatExt/Name dead; F2 mmce_backup walkers+setup dup; F3 join_path/dir_exists/mkdir dup across mmce files; F4 WLE_PS2BUILD else dead (mmce_igr, elf.c); F5 nameOk = mcCardNameOk; F6 rd/wr helpers x5; F7 mc_save_formats 951-952; F8 mmce_backup defaults/wrapper; F9 mmce_igr internal dup; F10 prefix_case_equal/mmce prefix -> strncasecmp/mmceCardUnit; F11 mc_sha256 vs mbedtls (2184 B MEASURED).
