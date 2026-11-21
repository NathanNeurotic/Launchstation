# launch-core-11.b notes (lens defects)

Done: src/dash_main.c 1-1542, src/dash_png.c 1-241, src/dash_ps1_card.c 1-759 (all read_full). Writing findings.

Findings planned:
1 dash_main.c:649-655 + dash_store.c:853-860 | Recent push writes DASHAPPS.CNF before each launch even when unchanged / Recent=0 | rank 3
2 dash_main.c:965-969 | runLaunchKey writes LK title into library entry | rank 2
3 dash_main.c:1527-1536 | welcome modal opens during auto-launch countdown (boot_phase 2) | rank 2
4 dash_png.c:77-84,139-153 | interlaced full/rows leak on longjmp | rank 2
5 dash_png.c:156,169,181,194 | u64 division per pixel | rank 2 INFERRED
6 dash_ps1_card.c:30-36 | 8092 B .bss MEASURED held whole run | rank 2
Dropped: argp truncation, cdStop poll, acc_n overflow, ps1CardKind over-read (reads <=11 bytes), p1 reentrancy (commit check refuses), main_msg copy (pointer shared)
