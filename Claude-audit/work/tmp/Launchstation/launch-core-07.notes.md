# launch-core-07 notes

Done so far: include/dash_card_tool.h 1-70 (full); src/dash_card_tool.c 1-2903 (full)

Candidates (to verify):
- dash_card_tool.c:338-341 ctSafetyCopy, 585-588 ctWriteFile | forwarding wrappers | count callers
- dash_card_tool.c:275-280, 1043-1048, 1275-1280 | base name without extension (dot/len/base[48]) repeated 3x | check further copies below 2199
- dash_card_tool.c:501-502, 622-623, 828-829, 921-922, 1040-1041 | "folder of image" 2-line pattern | small
- dash_card_tool.c:367-368, 415-416, 746-747, 2310-2311 | save name from entry+0x40 | check helper in mc_card_write.h
- dash_card_tool.c:1063-1107 (ctResize) vs 1152-1191 (ctGameCard) vs 1468-1477 (NewBlank) | format new image + write saves loop + verify list duplicated | net saving?
- dash_card_tool.c:987 vs 1403 sizes[] table twice
- dash_card_tool.c:1011-1016 vs 1237-1242 | same 250-saves refusal text
- dash_card_tool.c:282-284, 1065-1067, 1301-1303, 1463-1465 | mkdir of folder without trailing slash
- dash_card_tool.c:1433-1441 vs 2187-2194 vs 2723-2728 | ensure trailing slash on folder; check dash helper
- dash_card_tool.c:752-759, 1050-1055, 1443-1449 | free-name loop i<100
- dash_card_tool.c:798-860 vs 866-911 ConvertSave vs ConvertMany | could single be Many(n=1)? PS1 psv path differs
- dash_card_tool.c:1935-1938 | orphaned comment (describes ctAllBatch, sits above ctAllExport)
- dash_card_tool.c:1002 | freec,total fetched in ctResize and unused (only validity)
- dash_card_tool.c:1369 | snprintf msg "%s" of literal, could pass directly
- dash_card_tool.c:1592-1598 vs 1757-1762 | title/detail formatting duplicate
- dash_card_tool.c:1511,1579 | same "PCSX2 .ps2 cards are read-only here" string
- batch result summary pattern (first/failed/skipped/left) 1917-1932, 2139-2171, 1998-2003, 907-910, 2518-2533
- ctPsuInfo 2305-2327: icon.sys title parse; compare mcView/mc_view equivalent (likely duplicates mc_view save info)
- ctLibSaveSet 2592-2603: whole-file read loop; check for existing read-file helper (ctLoadPsu? genReadFile?)
- 2498: MCV_ERR_IO vs MCW_ codes mixed (check)
- ct_titles/ct_details/ct_items static arrays: size lens, other reader
