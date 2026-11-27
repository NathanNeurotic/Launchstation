# launch-core-23 notes
Done: ALL 11 files read. Now writing findings JSON.
Findings planned (all checked):
1 classic MISC branches in ExecuteMainAction pre-empted by dashRunMisc -> chain main_info_screens.c, config() config.c:1492-1779, Config_Startup/Screen/Network (sizes MEASURED contrib-by-object)
2 ExecuteBrowserFrom dead
3 kelf host test scaffolding + kelf_stricmp
4 des_cbc_crypt/tdes layering, key_count 3 dead, returns unchecked
5 KelfKeyStore duplicate struct
6 copy_printable_prefix dup with main_exploit_installer.c:248-268
7 three Misc name lists lang.c 382-412, 418-433, config.c 801-816
8 main.c dead globals/macros (DiscTypes 340B, CurrTime 8B MEASURED BUILD-EVIDENCE:160), SystemCnf_VMODE
9 main.c ctx fill duplicated
10 Set_Language + Load_External_Language double work
11 small defensive/forwarding leftovers
