# launch-core-24.b notes (lens defects)

## Done
- src/main_exploit_installer.c 1-2519 (read fully, re-read in attempt 2)
- src/main_fileops.c, include/main_gameid.h, src/main_gameid.c, src/main_gameid_table.c, include/main_history.h, src/main_history.c, include/main_info_screens.h (all read fully)

## Final candidates
- F1 r3: wipe/create folders before first MG bind (1963-1969, 1642-1669, 1887-1903, 1565-1577, 1595-1604, 1320-1356)
- F2 r2: name[36] keyboard(name,36) dash override 1-byte overflow (962, 979-982; dash_widgets.c 341-350, 663-668; dash_main.c 1358)
- F3 r2: wipe loop hang on >63-char nested path (1097-1103, 1195-1228, 1231-1236)
- F4 r1: repeated probes (1077-1082 vs kelf_signer.c 1014-1018; 1159-1164 + 1664-1666)
- DROP: main_history mcGetInfo unchecked (dash poll consumes changed-card), gameid sceCdRead stack buffer (libcdvd handles unaligned), table layout (~1 KB), uLE_related sprintf (MAX_PATH 1025)
- Next: write JSON, validate
