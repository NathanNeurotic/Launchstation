# launch-core-18.b notes (lens defects)

Done: src/filer_actions.c lines 1-2259 read fully. Candidates checked.

Kept:
- 298-334, 1944-2005 | delete/getFileSize ~0.9 MB listing per recursion level -> depth limit, delete partial on ENOMEM | checked: callers dash_browser.c:1100,1183; heap estimate BUILD-EVIDENCE sec 7
- 1208, 1926-1927, 2204, 2214, 2219-2220, 2225, 2244-2245, 2250-2251 | unbounded path+name joins into MAX_PATH | rank 1, needs >~770-char paths
- 1928-1934 | make_title_cfg genWrite unchecked, silent | rank 1
- 775-791, 817-837 (+dash_browser.c:2473), 1526-1539, 1837-1838 | per-frame resort with struct copies; O(N^2*60) step loop | rank 1

Dropped:
- delete 2022-2025 rebuild drops makeHostPath: genRmdir does makeHostPath itself (filer_fileops.c:234)
- newdir getHddParty unchecked: only fails for non hddN:/ paths
- adjustCustomDateField month 0: convertTimestampMinutes validates (main_title.c:265)
- IOCTL_BUF_SIZE: ps2sdk 1024 (test defines 1024) > 256 check
- Tuna forcing on HDD: intent ambiguous (sasFolderPath also special-cases Tuna everywhere)
- static indexes[MAX_ENTRY] 8 KB: trivial
