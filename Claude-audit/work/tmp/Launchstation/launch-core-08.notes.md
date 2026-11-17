# launch-core-08 notes

## Done
- src/dash_devices.c 1-921 (read full, re-read in attempt 2)
- src/dash_editor.c 1-1494 (read full)
- src/dash_exploit.c 1-169 (read full) - no findings (classic ShowExploitInstaller path main_actions.c:803 still reachable, vtable not single-impl)

## Candidates (verified status)
- dash_devices.c:117-138 | Init repeats ResetBaseline 3 lines | Init caller dash_main.c:1283, Reset dash_main.c:362 -> KEEP rank1
- dash_devices.c:187-190 + dash_internal.h:809 | dashMcPresent only used by tests/test_device_input.py:394-395 -> KEEP dead
- massN test x8 repo-wide | helper would not reduce lines -> DROP
- dash_devices.c:408-437 | PCCF 8 add() calls same name/mount/browse -> KEEP rank1
- dash_devices.c:698-720 vs 856-890 | autoStartNeeded duplicates AutoProbe conditions -> KEEP rank2 (table)
- dash_devices.c:738-774 | user param always NULL (files.c:135, widgets.c:1209, browser.c:694 NULL cb) -> check callbacks ignore user
- dash_devices.c:685-694 | MMCE/MX4SIO exclusivity redundant: dash_store.c:443-454 normalizes on load, dash_settings.c:1746-1758 enforces on toggle -> KEEP rank1
- quietDriverMsg vs dashMsgHook: behaviour differs (clears status) -> DROP
- ifdefs always defined (ps2.yaml:614-630) -> KEEP, count in all 4 files
- dashDevicesAutoProbe / dashDiscStatus exported but only used in file (dash_internal.h:797,808) -> rank1
- dash_editor.c:921-923 dead ternary `ndocs > 1 ? "" : ""`; 721-728 details[] array needless; 410 forward decl -> group rank1
- dash_editor.c:1221-1242 hexDigit dup of dash_themes.c:646-655 hexNibble; hexParse reimplements strtol -> rank1
- dash_editor.c:998-1001 dashTextEditor wrapper: 4 callers, keep (drop)
