Unit launch-core-29: all 9 files read fully (popstarter_path.c, popstarter_vcd.c, psu_functions.h/.c, qrcodegen.h/.c, remote_control.c, self_update.h/.c).
Candidates (checked):
- self_update.h:13-17,23-24 | file-input PSU extractors + stage path + selfUpdateWriteProgress unused in production (tests only) | checked
- popstarter_vcd.c:246-257 + popstarter_gameid.c:102-123,190-226 | VCD opened 3x, readVcd* wrappers | checked
- neutrino_gameid.c:20-109 vs popstarter_vcd.c:3-244 | second ISO9660 SYSTEM.CNF reader | checked
- qrcodegen.c asserts, no NDEBUG in ps2.yaml:614-630; __assert_func pulled only by qrcodegen.o | checked
- self_update.c:96-119 find_file_info 2048 x FILEINFO to get one size | checked
- psu_functions.c:84-92/238-245, 333-340/366-372, 276-279/361-364 dup | checked
- psu_functions.h:7-8 clear/pad exported, only internal use; tests text-replace | checked
- remote_control.c double throttle, stat-before-open, dead #else | checked
- popstarter_path.c:299-315 mc0/mc1 dup; elf.c:357-365 isHddPartyPath dup | checked
- self_update.c:58-89 split_parent_leaf two identical branches | checked
- .BAK cleanup: removal commit 8cd3329 only 3 days old -> dropped
