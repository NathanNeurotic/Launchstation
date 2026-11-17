# launch-core-29.b notes (lens defects)

Done: src/popstarter_path.c 1-399, src/popstarter_vcd.c 1-257, include/psu_functions.h 1-32, src/psu_functions.c 1-423

Candidates:
- src/popstarter_path.c:339-343 | __.POPS partition: arg "uLE:" + name where name keeps leading '/' -> "uLE:/X.elf" | check callers/tests to see expected form
- src/popstarter_path.c:247-276 | openHddPfsPathForRead mounts party, never unmounts | check mountParty cache semantics
- src/psu_functions.c:407-423 | mc stats apply ignores mcSetFileInfo/mcSync results, always 0 | low, probably drop
- src/psu_functions.c:237,248 | finalize seeks unchecked | probably drop
- psu restore/backup logic otherwise looks sound (check_entries validates names/sizes before restore loop filer_copy.c:1024,1125)
