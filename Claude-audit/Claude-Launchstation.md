# Claude audit: Launchstation

Status: IN PROGRESS. 53 of 103 reading passes are in; finding IDs are provisional and will be renumbered until this line says COMPLETE.

- Repository: NathanNeurotic/Launchstation
- Audited branch: master
- Audited commit: 8155d14cb69bffae7c0827cde4ab3eab312b8755
- Audit date: 2026-11-16
- Mode: read-only review. This branch adds only the `Claude-audit/` folder; no source, build or workflow file is changed.

All `path:line` references are pinned to the audited commit above. Each finding was raised by a reader that read the cited files line by line, then re-checked by a second, independent pass that re-read the cited lines and re-ran the reference searches. Findings that did not survive that check were dropped.

## Totals

- Findings in this report: 13 verified, plus 381 not yet verified
- Raised by readers: 395. Dropped as wrong or unsupported on re-check: 1. Corrected on re-check: 8. Merged as duplicates: 0.

| Category | Count |
|---|---:|
| 1 Dead code | 4 |
| 2 Duplication | 3 |
| 3 Overengineering | 0 |
| 4 Size and memory | 0 |
| 5 Performance | 2 |
| 6 Bugs and security | 2 |
| 7 Build/CI/repo clutter | 2 |
| 8 Half-finished or redundant features | 0 |

| Benefit rank | Meaning | Count |
|---|---|---:|
| 5 | major: real crash/data-loss/security bug, or >= 1000 lines / >= 50 KB / >= 100 KB RAM / multi-second freeze | 0 |
| 4 | large: >= 300 lines / >= 10 KB / >= 32 KB RAM, clear reliability or responsiveness gain | 0 |
| 3 | moderate: >= 80 lines / >= 2 KB, minor bug, redundant CI job or card write | 2 |
| 2 | small: >= 15 lines, a few hundred bytes | 7 |
| 1 | trivial: a few lines, cosmetic | 4 |

## Findings (sorted by benefit)

### Benefit rank 3

#### LAUNCHSTATION-001: Brace-matching C function extractor copy-pasted into 110 test files (16 in this unit, three string-aware variants)

- Category: 2 (Duplication)
- Location(s): `Launchstation/tests/test_background_only_hotkeys.py:15-26`; `Launchstation/tests/test_bgm_sidecar.py:11-18`; `Launchstation/tests/test_bookmark_routes.py:10-17`; `Launchstation/tests/test_boot_deferred_init.py:9-16`; `Launchstation/tests/test_browser_sort.py:13-23`; `Launchstation/tests/test_classic_settings_persistence.py:11-14`; `Launchstation/tests/test_clock_field_editor.py:6-10`; `Launchstation/tests/test_clock_save_action.py:7-11`; `Launchstation/tests/test_cloud_history_browser.py:17-46`; `Launchstation/tests/test_cloud_menu.py:21-28`; `Launchstation/tests/test_cloud_recovery.py:19-46`; `Launchstation/tests/test_cloud_upload_ledger.py:19-44`; `Launchstation/tests/test_copy_pipeline.py:22-26`; `Launchstation/tests/test_copy_psu_listing.py:34-41`; `Launchstation/tests/test_copy_short_io.py:16-23`; `Launchstation/tests/test_cursor_sound_ticks.py:26-33`; `Launchstation/tests/test_launch_driver_messages.py:27-34`
- Problem: Each test defines its own function()/extract()/fn() that finds a signature and counts braces to the matching close; Grep finds a top-level definition in 110 files under tests/. The bodies are the same algorithm with cosmetic differences, and three files (cloud_history_browser, cloud_recovery, cloud_upload_ledger) carry a 26-30 line string/char-literal-aware version that the others lack, so a brace inside a string literal breaks some tests and not others.
- Fix: Put one extractor (the string-aware one from test_cloud_history_browser.py) in a small tests/csource.py and import it, as tests already import mc_fixture via sys.path.insert(0, Path(__file__).parent); delete the local copies.
- Justification: Adds one ~30-line module; removes 182 lines in the cited ranges of this unit alone and roughly 8 lines in each of ~94 further files, and gives every test the same literal-aware parsing.
- Benefit: ~110 net lines in this unit (182 cited lines, minus ~30 for the shared module and ~2 import lines in each of 16 files), MEASURED from ranges; roughly 550 more repo-wide (~94 further files x ~6 net lines), INFERRED.
- Effort: M. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-002: Card-image backups (.mcd/.ps2) bypass the UPLOADED.TXT ledger, so every Drive sync opens one TLS session per already-uploaded snapshot

- Category: 5 (Performance)
- Location(s): `Launchstation/src/cloud_google.c:1543-1549`; `Launchstation/src/cloud_google.c:1302-1315`; `Launchstation/src/cloud_google.c:733-738`; `Launchstation/src/cloud_http.c:694-726`; `Launchstation/tests/test_cloud_upload_ledger.py:51-53`
- Problem: sync_tree consults the ledger only when psu is true (1544). Every already-uploaded WLE-*.mcd/.ps2 is therefore hashed in full (1302) and looked up on Drive (1310 -> drive_find_child -> cloudHttpRequest), and each request does DNS, TCP connect, CA parse, DRBG seed and a full TLS handshake with Connection: close (cloud_http.c:701, 715). This runs on the UI thread under dashLoadingBegin (dash_tools.c:360-363, 676-679) and in the IGR boot path (main.c:198); the default history depth is 10 per card (dash_store.c:260).
- Fix: Delete the psu && condition at line 1544 so card images use the same ledger as .psu files (hash, ledger hit skips; miss validates and uploads with the known digest, then records the line). Update the assertion in tests/test_cloud_upload_ledger.py:51-53 and the UPLOADED.TXT paragraph in docs/NATIVE_CLOUD.md. Retention pruning only looks at WLE- names (mmce_backup.c:132), so an UPLOADED.TXT in card folders is harmless.
- Benefit: One HTTPS session (DNS+TCP+TLS handshake+request) saved per old card snapshot per sync, e.g. 2 cards x 10 kept = 20 sessions per sync on the UI/IGR path (INFERRED; handshake time on the EE not measured). The 8 MB re-hash per image remains.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

### Benefit rank 2

#### LAUNCHSTATION-003: Same file and POSIX helpers repeated across the C host harnesses (rd x3, slurp x3, spit x2, gen* stand-ins x2)

- Category: 2 (Duplication)
- Location(s): `Launchstation/tests/mc_card_view_host.c:6-13`; `Launchstation/tests/mc_card_write_host.c:11-18`; `Launchstation/tests/mc_fingerprint_host.c:8-15`; `Launchstation/tests/mc_ps1_card_host.c:15-40`; `Launchstation/tests/mc_save_export_host.c:14-39`; `Launchstation/tests/mc_save_formats_host.c:9-29`; `Launchstation/tests/dash_card_tool_host.c:87-88`; `Launchstation/tests/dash_card_tool_host.c:96-100`; `Launchstation/tests/dash_card_tool_host.c:115-135`; `Launchstation/tests/dash_card_tool_host.c:203-208`; `Launchstation/tests/mmce_save_versions_host.c:34-35`; `Launchstation/tests/mmce_save_versions_host.c:51-55`; `Launchstation/tests/mmce_save_versions_host.c:68-88`; `Launchstation/tests/mmce_save_versions_host.c:99-104`
- Problem: rd() is byte-identical in three harnesses; slurp()/spit() are identical in mc_ps1_card_host.c and mc_save_export_host.c, with a NULL-returning slurp variant in mc_save_formats_host.c; genLseek/genRead, genClose, genDclose/fileXioDread and genCmpFileExt are the same in dash_card_tool_host.c and mmce_save_versions_host.c (fileXioDread differs only in hisize). The path-mapping and fault-injection stand-ins differ and are not part of this finding.
- Fix: Move rd, slurp, spit and the identical gen* stand-ins into one static-inline header in tests/ (for example host_io.h) included by these harnesses, and delete the copies.
- Justification: Adds one header of ~60 lines; removes ~115 duplicated lines, so the harnesses cannot drift (mc_save_formats already behaves differently on read errors).
- Benefit: ~65 net lines removed (MEASURED from ranges: 24 rd + 52 slurp/spit + 68 gen* lines deleted, minus a ~72-line header and ~8 include lines); no product change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-004: tests/test_boot_deferred_init.py is never run: not listed in compile.yml, and it only searches dash_main.c text

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/tests/test_boot_deferred_init.py`; `Launchstation/tests/test_dashboard_console_summary.py:36`
- Problem: compile.yml runs each test by name (lines 50-266) and test_boot_deferred_init.py is not among them; the only other mention of the file in the repo is a comment in test_dashboard_console_summary.py:36. The file itself compiles nothing: every check is a substring or regex over src/dash_main.c (lines 24-47), so it guards spelling, not behaviour, and nobody runs it.
- Fix: Delete tests/test_boot_deferred_init.py and drop the pointer to it from the comment at tests/test_dashboard_console_summary.py:36.
- Benefit: 49 lines removed (whole file) and one stale cross-reference; no CI change because it never ran (MEASURED: grep of compile.yml).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-005: test_copy_pipeline.py and test_copy_psu_listing.py repeat the same ~40-line C define/typedef prefix and ~20 stubs

- Category: 2 (Duplication)
- Location(s): `Launchstation/tests/test_copy_pipeline.py:41-83`; `Launchstation/tests/test_copy_pipeline.py:109-135`; `Launchstation/tests/test_copy_psu_listing.py:57-102`; `Launchstation/tests/test_copy_psu_listing.py:144-184`
- Problem: Both harnesses for src/filer_copy.c open with the same typedefs and #defines (MAX_PATH through DPRINTF: PM_*, TRANSFER_*, MC_ATTR_*, FIO_*), differing only in DVRP and the FIO_S_ISDIR/ISREG forms, and repeat stubs such as Timer, getIopResetGeneration, waitUntilTimer, drawMsg, readpad, DashIsActive, nonDialog, isPcCardCfPath/Online, ensureMemoryCardPortAccessible, makeHostPath, filerDirReadComplete, genFixPath and genGetStat. test_copy_psu_listing.py:170 even says 'As in test_copy_pipeline.py'. A new constant used by filer_copy.c has to be added to both.
- Fix: Move the common defines and the identical stubs into one C fragment (a tests/ header or a Python string in a shared module) that both harness strings start with; keep the per-test stubs local.
- Justification: Adds one shared fragment of about the size of one copy; removes the second copy and the need to edit two files when filer_copy.c gains a dependency.
- Benefit: ~45 lines removed (MEASURED: ~42 matching define/typedef lines plus ~15 identical one-line stubs, minus include/import lines); INFERRED less churn on filer_copy.c changes.
- Effort: M. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-006: test_cloud_tls_paths.py lines 22-65 test the host compiler's -ffile-prefix-map, not scripts/build_cloud_tls.sh

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/tests/test_cloud_tls_paths.py:22-65`
- Problem: After checking the script text (16-20), the test writes its own one-line C file, compiles it with and without -ffile-prefix-map and compares objects; the shell escaping it exercises at line 59 is a Python re-implementation, not the script's. It therefore proves that gcc/clang implement the flag, which is toolchain behaviour, while the repo's own wiring is covered only by the text checks above it. The docstring itself calls it 'a portable host compiler smoke test, not a PS2 ELF reproducibility proof'.
- Fix: Delete lines 22-65 and keep the text checks at 16-20 (or delete the file if those are judged not worth a CI step).
- Benefit: 44 lines removed and five host compiles per CI run (MEASURED: 2 iterations x 2 compiles at 36-40 plus one at 60-63).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-007: Tests assert that compile.yml runs themselves, which cannot fail in CI

- Category: 1 (Dead code)
- Location(s): `Launchstation/tests/test_cloud_upload_ledger.py:15-16`; `Launchstation/tests/test_dash_card_tool.py:23-24`; `Launchstation/tests/test_mc_card_write.py:26-27`; `Launchstation/tests/test_mc_save_export.py:23-24`; `Launchstation/tests/test_mc_fingerprint.py:23-24`; `Launchstation/tests/test_mc_save_formats.py:20-21`; `Launchstation/tests/test_mc_ps1_card.py:20-21`; `Launchstation/tests/test_mmce_save_versions.py:21`; `Launchstation/tests/test_mmce_save_versions.py:27`; `Launchstation/tests/test_dash_pause.py:22`; `Launchstation/tests/test_loader_linkfile.py:10`; `Launchstation/tests/test_loader_linkfile.py:23`; `Launchstation/tests/test_mmce_dread_returns.py:16-17`; `Launchstation/tests/test_mmce_exclusive_new_files.py:14`; `Launchstation/tests/test_mmce_exclusive_new_files.py:43`; `Launchstation/tests/test_mmce_fingerprint_backup.py:21-22`; `Launchstation/tests/test_mmce_fs_restore_guard.py:14`; `Launchstation/tests/test_mmce_fs_restore_guard.py:38`; `Launchstation/tests/test_mmce_handle_release.py:17-18`; `Launchstation/tests/test_mmce_selectable_install.py:13`; `Launchstation/tests/test_mmce_selectable_install.py:70`; `Launchstation/tests/test_ps2ips_rpc.py:48`; `Launchstation/tests/test_rolling_release_workflow.py:15`
- Problem: Eighteen tests assert that the string 'python3 tests/<this file>.py' appears in compile.yml. When CI runs the test the line is necessarily there, so the assert can only fire when someone runs the file by hand after removing it from the workflow. In 15 of these files the compile.yml text is read only for this assert.
- Fix: Delete the self-referencing asserts, and the compile.yml read in the 15 files where nothing else uses it.
- Benefit: ~33 lines removed and one file read per run in 15 tests (MEASURED: 18 assert lines plus 15 read lines; grep -cw workflow is 2 in those 15 files).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-008: cloudGoogleRecoverSnapshot copies a .mcd download to a stage file (8 MB write, 24 MB read) yet never checks the written file against the digest

- Category: 5 (Performance)
- Location(s): `Launchstation/src/cloud_google.c:1051-1076`; `Launchstation/src/cloud_http.c:863-866`; `Launchstation/src/mc_card_export.c:315-337`; `Launchstation/src/cloud_google.c:1158-1169`
- Problem: The SHA-256 is computed on bytes as they arrive (cloud_http.c:863), not on the file written. For a .mcd, mcCardBackupNoEcc(download, stage) (1062) then writes an 8 MB copy and reads source and copy back (mc_card_export.c:315-337, each 64 KB chunk reopening the file), which only proves stage equals download; a bad write of the download is never caught. The .psu path (1166) already publishes its download by rename and has the same on-disk gap.
- Fix: After the download, hash the written file with hash_local_file (move it above the recovery functions or forward-declare it) and compare it with snapshot->sha256 (and version->sha256 for .psu). For .mcd rename download to result directly, as cloudGoogleRecoverSaveVersion does; keep mcCardImportPcsx2 only for .ps2. Update tests/test_cloud_recovery.py: the source assertions at 58-59 and the harness stubs and counts that assume a .mcd.PART stage (111-112, 129-131, 186), adding a hash_local_file stub.
- Justification: The added hash read replaces the .mcd copy-and-compare, so it is less I/O, and it verifies the bytes that are actually published instead of only stage==download.
- Benefit: Per .mcd recovery: 8 MB write and 16 MB read less and ~512 fewer open/close calls (INFERRED: 8 MB / 64 KB = 128 chunks x 2 files x 2 passes); published file checked against Drive's digest. About 0 net lines.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-009: cloudGoogleSyncBackupRoot fetches the Drive access token once and never refreshes it; a sync longer than the token lifetime aborts with HTTP 401

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/cloud_google.c:1626-1633`; `Launchstation/src/cloud_google.c:389-400`; `Launchstation/src/cloud_google.c:739-743`; `Launchstation/src/cloud_google.c:1561-1567`
- Problem: The access token is obtained once (1626) and its expires_in is ignored (389-400); every later Drive call reuses it. Google access tokens last about one hour, and the first 401 makes drive_find_child fail (739-743), which aborts the whole recursive sync (1518-1524, 1563-1567). A first sync of a large history reads each 8 MB image twice (hash at 1302, upload at cloud_http.c:805-815) plus TLS work, so it can pass the one-hour mark.
- Fix: Remember Timer() when refresh_access_token succeeds and, at the top of each sync_tree entry, call refresh_access_token again when the token is older than about 50 minutes.
- Justification: Without it a big first sync cannot finish in one run and fails after an hour of work with an opaque 'Drive list returned HTTP 401'. A timestamp check is about 5 lines, needs no retry logic, and touches only the long-running sync.
- Benefit: Large first syncs complete in one run (INFERRED: at ~1 MB/s USB reads, two reads per 8 MB image is ~16 s, so roughly 225 images per hour before the token expires).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

### Benefit rank 1

#### LAUNCHSTATION-010: Optional git-ref argv mode in test_copy_short_io.py and test_cursor_sound_ticks.py that CI never uses

- Category: 1 (Dead code)
- Location(s): `Launchstation/tests/test_copy_short_io.py:13-14`; `Launchstation/tests/test_copy_short_io.py:95`; `Launchstation/tests/test_cursor_sound_ticks.py:8`; `Launchstation/tests/test_cursor_sound_ticks.py:20-23`; `Launchstation/tests/test_launch_driver_messages.py:10`; `Launchstation/tests/test_launch_driver_messages.py:21-24`
- Problem: These tests read sources through 'git show <argv[1]>:path' when given an argument, so that the checks can run against an older revision. Only the tests' own docstrings document this (test_cursor_sound_ticks.py:8, test_launch_driver_messages.py:10). compile.yml runs all three with no argument (lines 67, 228, 227). In test_copy_short_io.py the mode also silently skips the seek checks at 95-98.
- Fix: Delete the argv branch and read the working-tree file directly; git worktree or checkout already gives the same bisect ability.
- Benefit: ~8 lines removed across three files and one less code path per test (MEASURED from ranges).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-011: Redundant code in test_dash_card_tool.py: unused _whole, repeated dash_browser.c reads and F/G reassignments

- Category: 1 (Dead code)
- Location(s): `Launchstation/tests/test_dash_card_tool.py:275`; `Launchstation/tests/test_dash_card_tool.py:433`; `Launchstation/tests/test_dash_card_tool.py:1411`; `Launchstation/tests/test_dash_card_tool.py:449`; `Launchstation/tests/test_dash_card_tool.py:458`
- Problem: _whole (275) is never read, yet it runs the saves() helper, and with it the card lister subprocess, twice. Lines 433 and 1411 re-read src/dash_browser.c into _br, although line 25 already holds the same text in browser and nothing reassigns it. Lines 449 and 458 reassign F and G to the values set at 280 and 287.
- Fix: Delete test_dash_card_tool.py:275, 449 and 458. Use browser instead of _br at 434-436 and 1412, and delete lines 433 and 1411.
- Benefit: 5 lines removed and two lister subprocess runs per test run (MEASURED: line 275 calls saves(B) twice).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-012: Asserts that long-removed helper names stay absent (test_copy_pipeline.py, test_copy_short_io.py)

- Category: 1 (Dead code)
- Location(s): `Launchstation/tests/test_copy_pipeline.py:18-20`; `Launchstation/tests/test_copy_short_io.py:54`
- Problem: test_copy_pipeline.py checks that copyPrepareTarget, copyLookupPath, copyVerifyTarget, copyPublishTarget, copyOpenTarget and three strings are not in filer_copy.c; test_copy_short_io.py checks the same for copyReadChunk/copyWriteChunk. None of these names exists anywhere else in the repo, so the checks only guard against re-adding a name; they test code that no longer exists and say nothing about behaviour, which the rest of both files already exercises.
- Fix: Delete the three lines in test_copy_pipeline.py and line 54 in test_copy_short_io.py.
- Benefit: 4 lines removed (MEASURED from ranges); no coverage lost because the compiled harnesses in both files test the current copy path.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-013: Drive history paging rejects page tokens over 240 characters although next_page_token keeps 255, and longer tokens are truncated silently

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/cloud_google.c:890-902`; `Launchstation/src/cloud_google.c:913-914`; `Launchstation/include/cloud_google.h:56`; `Launchstation/src/dash_tools.c:1273`
- Problem: json_string stores nextPageToken into a 256-byte field (913-914, cloud_google.h:56) and truncates longer values without telling the caller. dash_tools passes it back (1273), and the request then refuses any token longer than 240 characters (892-895). A token of 241 characters or more shows 'Next page', but that page always fails with 'Invalid cloud-history pagination token'. Folders with more than 12 entries are affected. Whether Google issues tokens this long was not verified.
- Fix: Delete the 240-character check at 892-895: append_url already bounds the URL, and the 2304-byte path has room (base path at most ~352 bytes + 11 + 3 x 255 = 1128). If Google tokens can be longer than 255, enlarge next_page_token and dash_tools' token[256] together.
- Benefit: Paging works for every token that fits the buffer; 4 lines removed (INFERRED path arithmetic from the format string at 884-887 and drive_safe_id ids < 192 chars).
- Effort: S. Risk: Low. Confidence: Low (INFERRED)

### Not yet verified (raised by a reader, second check still pending)

#### LAUNCHSTATION-014: Classic Configure menu (config(), configSaveTargetPrompt, Config_Screen/Startup/Network/Advanced) is superseded by dashboard SETTINGS

- Category: 8 (Half-finished or redundant features)
- Location(s): `Launchstation/src/config.c:36`; `Launchstation/src/config.c:38-84`; `Launchstation/src/config.c:719-787`; `Launchstation/src/config.c:1129-1207`; `Launchstation/src/config.c:1367-1779`; `Launchstation/src/config_network.c:59-359`; `Launchstation/src/config_screen.c`; `Launchstation/src/config_startup.c`; `Launchstation/src/config_advanced.c`; `Launchstation/src/main_actions.c:784-788`; `Launchstation/include/config_private.h:52-53`; `Launchstation/include/config_private.h:69`; `Launchstation/include/config_private.h:81-84`; `Launchstation/include/launchelf.h:552-553`; `Launchstation/ps2.yaml:514-517`
- Problem: dashRunMisc sends MISC/Configure to the dashboard SETTINGS section ('every classic configuration option lives in SETTINGS now', dash_main.c:456-459) and dashRunMiscCommand returns early for it (dash_main.c:398-399); docs/R3Z-CONFIG-PARITY.md maps every CNF key to a dashboard setting id. The only remaining entry to the classic menu is main_actions.c:784-788 (config()), reached only if a raw MISC/Configure path arrives through ExecuteMainAction from a library entry, the IGR after-app path or the classic file picker. config() is the only caller of Config_Screen/Startup/Network/Advanced, configSaveTargetPrompt and saveConfigToPath; configFormatLabelValue(Aligned) are used only by these screens.
- Fix: Make the main_actions.c:784 branch do what dashRunMisc does (switch to the dashboard SETTINGS section) or drop it, then delete config(), configSaveTargetPrompt, configFormatPromptLabel, configFormatSavePromptLine, the CONFIG_MAIN enum, tmpsetting, saveConfigToPath, configFormatLabelValue/Aligned, the files config_screen.c, config_startup.c, config_advanced.c, and everything in config_network.c except configWriteNetworkSnapshot (move it into config.c and drop the file), with their prototypes and ps2.yaml entries. Keep configSnapshotSaveTargetPath, configSelectInitialSaveTarget and the path helpers the dashboard uses.
- Benefit: ~1,960 lines. MEASURED .text (symbol table): Config_Startup 4356 + Config_Screen 4068 + Config_Advanced 3660 + config 3632 + Config_Network 3060 + configSaveTargetPrompt 2412 + saveConfigToPath 956 + helpers 1004 = 23,148 B unpacked, ~370 B .rodata. Packed effect not measured.
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-015: loadIcon is never called: TexIcon, FOLDER/WARNING and the whole of src/icon.c are dead with it

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/draw.c:206-227`; `Launchstation/src/draw.c:15`; `Launchstation/include/launchelf.h:466-470`; `Launchstation/include/launchelf.h:473`; `Launchstation/include/launchelf.h:505`; `Launchstation/src/icon.c`; `Launchstation/ps2.yaml:559`
- Problem: loadIcon appears only at its definition (draw.c:206) and prototype (launchelf.h:505). It is the only user of TexIcon (draw.c:15, launchelf.h:473) and of icon_folder/icon_warning, the two 1024-byte arrays that make up all 2055 lines of src/icon.c (compiled via ps2.yaml:559). The FOLDER/WARNING index macros (launchelf.h:466-467) are used nowhere. loadIcon would also free() static arrays if it ever ran.
- Fix: Delete loadIcon, TexIcon, the FOLDER/WARNING macros, the icon_folder/icon_warning/TexIcon externs and the loadIcon prototype; delete src/icon.c and its ps2.yaml entry.
- Benefit: ~2085 source lines removed (2055 of them a byte table). MEASURED: linker already discards .text.loadIcon (308 B) and icon.o .data (2048 B); TexIcon 80 B .bss stays in the shipped ELF (no -fdata-sections) and goes away.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-016: loadIcon is never called: TexIcon, src/icon.c (icon_folder/icon_warning) and FOLDER/WARNING are a dead chain

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/draw.c:205-227`; `Launchstation/src/draw.c:15`; `Launchstation/src/icon.c`; `Launchstation/ps2.yaml:559`; `Launchstation/include/launchelf.h:466-470`; `Launchstation/include/launchelf.h:473`; `Launchstation/include/launchelf.h:505`
- Problem: loadIcon has no caller (only its definition draw.c:206 and prototype launchelf.h:505); it is the only user of TexIcon, and TexIcon/icon_folder/icon_warning (2 x 1024-byte tables filling the 2055-line src/icon.c) are referenced nowhere else. The FOLDER and WARNING index macros are unused. loadIcon would also free() static arrays if it ever ran.
- Fix: Delete loadIcon, TexIcon, the FOLDER/WARNING macros and the three externs and prototype in launchelf.h, delete src/icon.c and remove it from the wLaunchELF source list in ps2.yaml.
- Benefit: ~2085 source lines removed (2055 of them a byte table). MEASURED: icon.o contributes 0 bytes to the linked ELF and .text.loadIcon (308 B) is already gc'd, but TexIcon keeps 80 B of .bss (xref OBJECT size 80); one fewer compile unit.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-017: Classic TextEditor (editor*.c) is superseded by dash_editor.c; route the remaining callers to dashTextEditor and delete it

- Category: 8 (Half-finished or redundant features)
- Location(s): `Launchstation/src/editor_file.c:1-324`; `Launchstation/src/editor_input.c:1-705`; `Launchstation/src/editor_menu.c:1-229`; `Launchstation/src/editor_rules.c:1-123`; `Launchstation/src/editor.c:112-160`; `Launchstation/src/main_actions.c:225-254`; `Launchstation/src/main_actions.c:770-783`; `Launchstation/src/filer_browser.c:1551-1554`; `Launchstation/src/hdd_header_injector.c:826`; `Launchstation/src/dash_hdd.c:965-967`; `Launchstation/src/dash_main.c:470-473`
- Problem: dash_editor.c:3 says it replaces the classic TextEditor() wherever the dashboard opens text; every dashboard entry (dash_main.c:470-473, dash_browser.c:767,3003, dash_tools.c:1622, dash_files.c:324, dash_settings.c:3495, dash_hdd.c:953) uses dashTextEditor. The classic editor (editor.c plus the four files of this unit) is left only behind classic paths: executeBrowserSelection and the Misc_TextEditor branch of ExecuteMainAction (main_actions.c:231,771, reachable e.g. through Execute(igr_after_path) main.c:240), the classic filer menu (filer_browser.c:1554) and the hddHeaderTextEditor default, which dash_hdd.c:967 even restores to TextEditor after each use.
- Fix: Point main_actions.c:231 and :771, filer_browser.c:1554 and the hddHeaderTextEditor default (hdd_header_injector.c:826, dash_hdd.c:967) at dashTextEditor (it already returns the same TEXTEDITOR_RESULT_* codes, see dash_main.c:515), move EditorSpecialChars/EditorSpecialCharCount (used by dash_widgets.c:54,68) next to dash_widgets.c, then delete editor.c, editor_file.c, editor_input.c, editor_menu.c, editor_rules.c and most of include/editor_private.h, and drop them from ps2.yaml:553-557. tests/test_editor_keyboard.py reads src/editor_input.c and must follow the table.
- Benefit: MEASURED (raw/link/wLaunchELF-contrib-by-object.tsv): editor.o 28,860 + editor_input.o 12,536 + editor_file.o 3,308 + editor_menu.o 2,048 + editor_rules.o 1,332 = 48,084 bytes of ELF/RAM (18,868 of it .bss); ~2,000 source lines (688+705+324+229+123 minus the moved table).
- Effort: M. Risk: Med. Confidence: Low (INFERRED)

#### LAUNCHSTATION-018: Classic MISC screens in ExecuteMainAction are pre-empted by dashRunMisc; config(), Config_Startup/Screen/Network and main_info_screens.c hang off them

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main_actions.c:734-740`; `Launchstation/src/main_actions.c:766-804`; `Launchstation/src/main_actions.c:219-254`; `Launchstation/src/main_actions.c:90-106`; `Launchstation/src/main_info_screens.c:11-390`; `Launchstation/include/main_info_screens.h:1-9`; `Launchstation/src/config.c:1492-1779`; `Launchstation/src/config_startup.c:30-361`; `Launchstation/src/config_screen.c:50-358`; `Launchstation/src/config_network.c:141-359`; `Launchstation/src/dash_main.c:451-495`
- Problem: Since the classic menu was removed (8155d14), MISC paths reach ExecuteMainAction only via runMiscCommand, and dashRunMisc (dash_main.c:451-494) handles FileBrowser, HddManager, TextEditor, Configure, Exploit_Installer, About, Build_Info, Debug_Info and ShowFont natively first. dashLaunchApp, IGR and remote callers pass ELF/device paths only. So these branches, executeBrowserSelection, isMiscAboutWlePath and their sole callees (config(), Show_About_uLE, Show_build_info, ShowDebugInfo, ShowFont and the classic Config_* screens) are unreachable.
- Fix: Delete the MISC branches for FileBrowser, HddManager, TextEditor, Configure, ShowFont, Debug_Info, About, Build_Info and Exploit_Installer from ExecuteMainAction, then executeBrowserSelection, getTextEditorReturnPath and isMiscAboutWlePath, main_info_screens.c/.h, config() and Config_Startup/Config_Screen/Config_Network (keep configWriteNetworkSnapshot/configSaveNetworkToPath). With the Exploit_Installer branch gone, exploitInstallerUi is always set by dash_exploit.c, so the NULL/classic paths in main_exploit_installer.c can follow.
- Benefit: ~1500 source lines (INFERRED from cited ranges). MEASURED in evidence/Launchstation/raw/link/wLaunchELF-contrib-by-object.tsv: main_info_screens.o 5170 B, config_startup.o 4516 B, config_screen.o 4148 B, most of config_network.o 3952 B, plus config()'s share of config.o: >= 15 KB ELF.
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-019: src/dash_files.c is unreachable: the DEVICES section can never be selected, so dashDevicesScreen and everything behind it is dead

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_files.c`; `Launchstation/src/dash_main.c:43-44`; `Launchstation/src/dash_main.c:206-213`; `Launchstation/src/dash_main.c:1324-1336`; `Launchstation/src/dash_main.c:1450`; `Launchstation/include/dash_internal.h:1022`; `Launchstation/include/dash_internal.h:1133`; `Launchstation/src/dash_settings.c:1071-1073`; `Launchstation/src/dash_settings.c:1776-1777`; `Launchstation/ps2.yaml:597`
- Problem: dashDevicesScreen (dash_files.c:731) is reached only through screens[DASH_SEC_DEVICES] in dash_main.c:44, but `section` is only assigned at dash_main.c:235 (after 208-213 map DEVICES to FILES) and 1324-1336 (same mapping, else a visible_sections entry), so it is never DASH_SEC_DEVICES. All other code in dash_files.c (files*/dev*/storage* screens, openDevice, startDevice) is static and reached only from that screen; dashFilesPoll, the one external entry, returns at once because pend.active is set only by the dead startDevice. files_mode/S_FILES_MODE only steer these dead screens.
- Fix: Delete src/dash_files.c and its ps2.yaml:597 entry, the dashFilesPoll() call (dash_main.c:1450) and prototype, the dashDevicesScreen extern, and put NULL (or &dashFilesScreen) in the DEVICES slot of screens[]. Optionally drop the then-meaningless S_FILES_MODE cases and files_mode field (keep reading the CNF key as a no-op). Update or delete the host tests that grep dash_files.c (listed in evidence).
- Benefit: ~734 source lines removed. dash_files.o keeps 13188 B .text + 112 B .rodata in the ELF and 30520 B .bss in RAM (MEASURED: evidence raw/link/wLaunchELF-contrib-by-object.tsv); gc cannot drop it because screens[] references dashDevicesScreen.
- Effort: M. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-020: FILEOP_TRACE debug tracing is never compiled: ULE_DEBUG_BUILD is defined by no build

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_fileops.c:5-12`; `Launchstation/src/filer_fileops.c:42-184`; `Launchstation/src/filer_fileops.c:227-292`; `Launchstation/src/filer_fileops.c:499-791`; `Launchstation/src/filer_copy.c:22-29`; `Launchstation/src/filer_copy.c:412-420`; `Launchstation/src/filer_copy.c:477-485`; `Launchstation/src/filer_copy.c:513-521`; `Launchstation/src/filer_copy.c:785-892`; `Launchstation/src/filer_copy.c:1092-1169`; `Launchstation/src/filer_copy.c:1334-1447`; `Launchstation/src/filer_copy.c:1566-1627`; `Launchstation/src/filer_copy.c:1703-1708`; `Launchstation/src/filer_mount.c:6-13`; `Launchstation/src/filer_mount.c:40-106`; `Launchstation/src/filer.c:156-163`; `Launchstation/include/launchelf.h:11-12`
- Problem: Every #if FILEOP_TRACE block (24 in filer_copy.c, 33 in filer_fileops.c incl. a 143-line fd-tracking table, 6 in filer_mount.c) depends on ULE_DEBUG_BUILD, which neither ps2.yaml, configure_ds34.py, scripts nor workflows define, so FILEOP_TRACE is always 0. filer.c:156-163 even defines FILEOP_TRACE and never tests it.
- Fix: Delete the four ULE_DEBUG_BUILD/FILEOP_TRACE preambles, every #if FILEOP_TRACE block (keeping the #else side where there is one, e.g. filer_fileops.c:571-573 and 702-704), and the trace_* locals in copy(); drop the ULE_DEBUG_BUILD suffix branch in launchelf.h. Update tests/test_copy_short_io.py:58, which slices copy() on '#if FILEOP_TRACE'.
- Benefit: ~507 source lines (MEASURED by counting the #if FILEOP_TRACE blocks: 141 in filer_copy.c, 316 in filer_fileops.c, 18 in filer_mount.c, plus 4 x 8-line preambles); no ELF change since the preprocessor already drops it (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-021: Classic info screens in main_info_screens.c are superseded by dash_info.c and reachable only through alias edge cases

- Category: 8 (Half-finished or redundant features)
- Location(s): `Launchstation/src/main_info_screens.c`; `Launchstation/include/main_info_screens.h`; `Launchstation/src/main_actions.c:789-801`; `Launchstation/src/main_actions.c:90-106`; `Launchstation/src/dash_main.c:478-493`; `Launchstation/src/dash_info.c:145-195`; `Launchstation/ps2.yaml:494`
- Problem: Show_About_uLE, Show_build_info, ShowDebugInfo and ShowFont (all of main_info_screens.c) have native replacements dashShowAbout/dashShowBuildInfo/dashShowSystemInfo/dashShowFontViewer, and dashRunMisc matches the four MISC paths before anything reaches ExecuteMainAction. The classic branches at main_actions.c:790-801 are left for paths dashRunMisc does not match exactly: the legacy 'About uLE' alias accepted by isMiscAboutWlePath, or a MISC path typed by hand as the IGR-after path (main.c:240). The file also carries a 27-line commented-out gsKit test block (312-338) and a leftover 'show font twice' test pass (382-385).
- Fix: Make dashRunMisc (or the four ExecuteMainAction branches) call the dash_info.c screens, accepting the legacy About alias there too. Then delete src/main_info_screens.c, include/main_info_screens.h, its ps2.yaml entry, the four classic branches and isMiscAboutWlePath, and update the stubs in tests/test_launch_driver_messages.py:206-210.
- Benefit: ~400 source lines removed. ELF: the four functions are 2108+820+1416+800 = 5144 B of .text (MEASURED, evidence/Launchstation/raw/sizes/wLaunchELF-unstripped-all-symbols-by-size.tsv), plus their strings (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-022: Active MMCE card history helpers re-implement mmce_backup.c (stamp, join, newest, compare, prune) with 32 KB of extra buffers

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_browser.c:1257-1466`; `Launchstation/src/mmce_backup.c:45-83`; `Launchstation/src/mmce_backup.c:130-164`; `Launchstation/src/mmce_backup.c:376-395`; `Launchstation/src/mmce_backup.c:437-517`; `Launchstation/src/dash_card_tool.c:1373-1376`; `Launchstation/src/main_history.c:22-25`; `Launchstation/src/main_title.c:46-49`
- Problem: dashBcdToInt+activeBackupStamp (1257-1288) are a line-for-line copy of mmce_backup.c bcd_to_int+backup_stamp (45-71); joinBackupPath (1304-1314) copies join_path (73-83); activeHistoryNewestOther copies newest_history (136-164) with a different name prefix; activeHistoryFilesEqual copies files_equal_serial (376-395) with its own 2 x 16 KB static buffers; activeHistoryScan+activeHistoryPrune redo prune_history (437-484); activeStampedName redoes unique_history_path (486-517). activeHistoryName is a one-line forwarder with one caller (1569). The 4-line BCD helper also exists in dash_card_tool.c, main_history.c and main_title.c.
- Fix: Export backup_stamp, join_path and files_equal_serial from mmce_backup.c and give history_name_match/newest_history/prune_history/unique_history_path a name-prefix parameter ("WLE-" vs "MMCE<n>-ActiveCard-"); delete dash_browser.c 1257-1466 except a few lines of glue. Keep one BCD helper (exported from mmce_backup.c or main_title.c) for the other copies.
- Benefit: ~150 lines removed (INFERRED: 210 lines at 1257-1466 minus ~25 lines of glue and parameter changes) and 32,768 bytes .bss (MEASURED: activeHistoryFilesEqual.buf_a and buf_b, 16,384 each, BUILD-EVIDENCE.md top-symbol list)
- Effort: M. Risk: High. Confidence: Med (READ)

#### LAUNCHSTATION-023: Eight static int[MAX_ENTRY] scratch arrays (64 KB .bss) where one shared array is enough

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_browser.c:822`; `Launchstation/src/dash_browser.c:978`; `Launchstation/src/dash_browser.c:1054`; `Launchstation/src/dash_browser.c:1079`; `Launchstation/src/dash_browser.c:1172`; `Launchstation/src/dash_browser.c:2698`; `Launchstation/src/dash_browser.c:2795-2796`; `Launchstation/src/dash_browser.c:3044-3046`
- Problem: opCopy, opExtractPsuTo, opImportSavesTo, opDelete, opSize and actionsMenu (three times: sv_idx, psu_idx and a second sv_idx shadowing the first inside case F_IMPORT_SAVES) each own a static int[MAX_ENTRY] (MAX_ENTRY = 2048, 8 KB each). All run on the UI thread, none is re-entered while another is in use, and actionsMenu only uses its arrays for counts before dispatching, so one array would do. The F_IMPORT_SAVES case also recomputes saveFileTargets that actionsMenu already computed at 2810.
- Fix: Replace the eight function-local static arrays with one file-scope static int[MAX_ENTRY] used by targets() callers; in case F_IMPORT_SAVES reuse it instead of declaring another sv_idx.
- Benefit: 57,344 bytes of .bss freed (MEASURED: 8 symbols of 8192 bytes each in evidence/Launchstation/raw/sizes/wLaunchELF-unstripped-all-symbols-by-size.tsv: opCopy.idx, opExtractPsuTo.idx, opImportSavesTo.idx, opDelete.idx, opSize.idx, actionsMenu.sv_idx, actionsMenu.psu_idx, actionsMenu.sv_idx.228; 7 x 8192 saved)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-024: hddParties/dvrParties keep 135 KB of partition names in .bss only to fill the HDD and DVR root listings

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/filer.c:130-134`; `Launchstation/src/filer.c:762-770`; `Launchstation/src/filer.c:865-873`; `Launchstation/src/filer.c:1194-1197`; `Launchstation/src/filer.c:1267-1270`
- Problem: hddParties[2][1400][33] (92,400 B) and dvrParties[1400][33] (46,200 B) are filled by setPartyListForDevice/setDVRPPartyList and read only by the root branches of readHDD and readHDDDVRP, which copy the names into info[]. Every other user (genFixPath 1063/1093, filerDirReadNeedsPrep 802, filerPrepareDirRead 829, invalidatePartitionCaches 880-887) only tests the counts, so the name tables are a cache of one listing held for the whole run.
- Fix: Let the root branches of readHDD/readHDDDVRP write the partition names straight into info[] from the same hddN:/dvr_hdd0: Dread loop (give setPartyListForDevice/setDVRPPartyList an optional info pointer, NULL = count only), and delete both name tables, keeping hddNparties/ndvrpparties as counts. tests/test_listing_complete.py mirrors these tables in its stubs and needs the same change.
- Benefit: 138,600 bytes of .bss removed (MEASURED: BUILD-EVIDENCE.md:83 lists hddParties 92400 and dvrParties 46200); the root listing then re-reads the partition table each time, a few APA RPCs.
- Effort: M. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-025: get_CNF_string loops forever on a byte >= 0x80 at a line start or inside a comment line (UTF-8 BOM, non-ASCII comment): boot hang

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/config.c:117-128`; `Launchstation/src/config.c:950`; `Launchstation/src/main_boot.c:63-69`; `Launchstation/src/main_startup.c:178-181`
- Problem: With signed char, a byte >= 0x80 fails both '*tp > 0' tests: the skip loop at 118 stops on it, line 123 sends it to the comment branch, and the comment loop at 125 stops on it without advancing, so 'goto start_line' repeats forever. A LAUNCHSTATION.CNF saved by a PC editor with a UTF-8 BOM (EF BB BF) or with an accented character in a '#'/';' comment hangs loadConfig, which main_boot.c:64 runs at every boot before the UI exists. readSystemCnf parses disc SYSTEM.CNF with the same function.
- Fix: In the comment-skip loop at line 125 test '*tp != 0' instead of '*tp > 0' so the rest of the line is skipped; a high byte at a line start then goes through the comment branch and is skipped too. No other line needs to change.
- Benefit: Removes a boot-time infinite loop triggered by an ordinary PC-edited config file; one-token change (INFERRED: signed char is the MIPS ABI default, no -funsigned-char in ps2.yaml)
- Effort: S. Risk: Low. Confidence: High (INFERRED)

#### LAUNCHSTATION-026: wle_igr hdd_target_path reloads dev9/bdm/bdmfs/ata_bd/ps2hdd/ps2fs on every one of its 24 retries

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/igr/wle_igr.c:122-134`; `Launchstation/igr/wle_igr.c:151-154`; `Launchstation/iop/ps2hdd_osd/hdd.c:359-378`; `Launchstation/iop/ata_bd/src/ps2atad.c:331-342`; `Launchstation/iop/ata_bd/src/ps2atad.c:374-383`
- Problem: For an hdd0:/hdd1: target, resolve_app_path calls hdd_target_path on each of up to 24 attempts and every call SifExecModuleBuffer's the whole HDD driver set again. ps2hdd_osd re-runs DelDrv/AddDrv and returns RESIDENT_END each time (hdd.c:359-378), so every retry leaves another resident ps2hdd. A second ata_bd registers DEV9 interrupt, DMA and power-off callbacks and calls sceAtaInit (bdm_connect_bd) before RegisterLibraryEntries fails and it exits NO_RESIDENT_END, so its freed image stays wired into dev9 and bdm. Trigger: a configured HDD path whose partition or ELF is missing, or a mount that fails once.
- Fix: Load the HDD driver set once: move the six module loads out of hdd_target_path into main's prefix chain (extend the existing ata branch to hdd0:/hdd1: and start ps2hdd/ps2fs there), so the retry loop only splits the path and mounts pfs0:. bdmfs_fatfs is not needed for a PFS target and can be left out of that branch.
- Benefit: Fixes repeated driver loads in the IGR path: up to 23 extra resident ps2hdd copies (27,549-byte IRX each, BUILD-EVIDENCE.md:174, plus cache) and dangling DEV9/bdm callbacks from freed ata_bd images, likely an IOP hang instead of the ExecOSD fallback (INFERRED). Net 0 lines.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-027: HDL info RPC stays 'ready' across IOP resets and after a failed load: HDD manager calls SifCallRpc on a stale or unbound client

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/init.c:2563-2574`; `Launchstation/src/init.c:2998`; `Launchstation/src/hdl_rpc.c:26-50`; `Launchstation/src/hdl_rpc.c:57-62`; `Launchstation/src/dash_hdd.c:421-437`
- Problem: Hdl_Inited (hdl_rpc.c:24,48) is set once and never cleared: clearIopModuleState only clears have_hdl_info (init.c:2998). After any IOP reset (stack switch MMCE/MX4SIO/UDPFS, mc1 with MX4SIO, DEV9 family change), dash_hdd hdlReady returns 1 without reloading hdl_info.irx and HdlGetGameInfo/HdlRenameGame issue blocking SifCallRpc through a client bound to the previous IOP session. Also loadHdlInfoModule ignores ID/ret and the bind result; when the module did not start, Hdl_Info_BindRpc spins 0x1000 binds x 0x10000 nop loops and still sets Hdl_Inited=1, so the call goes to a NULL server.
- Fix: Clear Hdl_Inited in clearIopModuleState next to have_hdl_info; in loadHdlInfoModule skip the bind when ID < 0 or ret == 1; in Hdl_Info_BindRpc set Hdl_Inited only when client.server != 0; key dash_hdd's one-shot 'tried' to getIopResetGeneration() so the module is reloaded once per IOP session.
- Justification: The checks replace an unconditional 'ready' latch that lets a blocking RPC reach a dead or absent IOP server (console freeze, INFERRED). Each is one condition on existing state; no new mechanism is added.
- Benefit: Removes a likely hard freeze when the HDD manager is reopened after any IOP reset, and a ~3-5 s UI-thread spin when hdl_info fails (INFERRED: 2^28 loop iterations of 4 nops at 294.9 MHz plus 4096 bind round trips).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-028: dash_sound.c keeps full EE copies of BGM.ADP and custom effects for the whole run only as audsrv sample IDs

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_sound.c:40`; `Launchstation/src/dash_sound.c:465-472`; `Launchstation/src/dash_sound.c:54`; `Launchstation/src/dash_sound.c:146-149`; `Launchstation/src/dash_sound.c:296-301`
- Problem: After audsrv_load_adpcm has sent the sample to the IOP/SPU2, bgm_data (size+64 bytes) and each accepted custom_data[i] stay allocated until shutdown; the file's own comments (lines 40, 54) say audsrv only uses the buffer address as the sample ID. The copy is never reused either: dashSoundShutdown (296-301), run before every IOP reset (init.c:2193), frees it and dashBgmStart reads the file again. The shipped kit always installs BGM.ADP (scripts/package_apps.py:37,44), so every boot with music on holds it.
- Fix: After a successful audsrv_load_adpcm, shrink the buffer in place to a few bytes (realloc to 16 bytes; newlib shrinks without moving) so the address stays allocated and unique as the ID and the rest returns to the heap. Do the same for each accepted custom effect in uploadSamples. Do not free the buffers outright: a reused address would collide with a live sample ID.
- Justification: Two realloc calls (about 4 lines) release memory that has no use after upload: 284,720 bytes for the shipped music on every boot, and up to 1 MB + 2 MB with user files. Nothing else holds or reads these buffers.
- Benefit: 284,720 B EE heap for the shipped BGM.ADP (MEASURED: res/music.sad 284,656 B in inventory, +64 pad from line 429); up to 1 MB + 8 x 256 KB with user sidecars (INFERRED from BGM_MAX_BYTES and SFX_MAX_BYTES). Rank 4, not 5, because the fix relies on INFERRED audsrv/newlib behaviour.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-029: DashApp is 828 bytes and dashApps reserves DASH_MAX_APPS (512) of them for the whole run, plus a second full array per scan

- Category: 4 (Size and memory)
- Location(s): `Launchstation/include/dash_internal.h:39`; `Launchstation/include/dash_internal.h:690-702`; `Launchstation/src/dash_lib.c:492-499`; `Launchstation/src/dash_lib.c:889-891`; `Launchstation/src/dash_lib.c:928-929`
- Problem: dashLibInit mallocs sizeof(DashApp) * DASH_MAX_APPS once and keeps it, whatever the library size; every scan mallocs a second full-size fresh array and then memcpy's it into the first. A library of 50 apps uses about 41 KB of the 414 KB that stays reserved.
- Fix: Size the array to the library: allocate for the loaded count plus a small headroom, and let dashLibApplyScan adopt the scan's fresh array shrunk with realloc instead of copying it into a second permanent buffer. dash_main.c:718-719 rebuilds the Apps and Home views right after the apply; check first which dialogs hold DashApp pointers across a scan (dash_lib.c:764-765).
- Justification: Adds a realloc and a capacity check in the cache loader. The concrete problem is about 0.4 MB of EE RAM held for the whole session for empty slots; the change is a few lines and no new layer.
- Benefit: sizeof(DashApp) = 256+64+160+40+24+24+256+1+1+2 = 828 B; 828 x 512 = 423,936 B reserved permanently. With 50 apps about 380 KB of EE RAM is returned (INFERRED; depends on library size). Scan peak also drops by the same amount.
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-030: 29 dashboard/config functions declared in dash_internal.h, dash.h and config_private.h have no caller

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/dash_internal.h:257-262`; `Launchstation/include/dash_internal.h:276-277`; `Launchstation/include/dash_internal.h:284`; `Launchstation/include/dash_internal.h:298`; `Launchstation/include/dash_internal.h:334`; `Launchstation/include/dash_internal.h:404`; `Launchstation/include/dash_internal.h:421-422`; `Launchstation/include/dash_internal.h:511-514`; `Launchstation/include/dash_internal.h:635`; `Launchstation/include/dash_internal.h:643-644`; `Launchstation/include/dash_internal.h:710`; `Launchstation/include/dash_internal.h:731`; `Launchstation/include/dash_internal.h:741`; `Launchstation/include/dash_internal.h:758`; `Launchstation/include/dash_internal.h:769`; `Launchstation/include/dash_internal.h:809`; `Launchstation/include/dash_internal.h:884`; `Launchstation/include/dash_internal.h:1057`; `Launchstation/include/dash_internal.h:1112`; `Launchstation/include/dash_internal.h:1130`; `Launchstation/include/dash_internal.h:1136-1139`; `Launchstation/include/dash.h:16-18`; `Launchstation/include/config_private.h:54`; `Launchstation/include/config_private.h:70`; `Launchstation/include/config_private.h:77`; `Launchstation/src/dash_apps.c:94-106`; `Launchstation/src/dash_art.c:71-74`; `Launchstation/src/dash_art.c:787-790`; `Launchstation/src/dash_bg.c:543-547`; `Launchstation/src/dash_ui.c:176-179`; `Launchstation/src/dash_ui.c:611-633`; `Launchstation/src/dash_browser.c:3631-3662`; `Launchstation/src/dash_font.c:309-314`; `Launchstation/src/dash_font.c:838-860`; `Launchstation/src/dash_gfx.c:362-371`; `Launchstation/src/dash_icons.c:859-867`; `Launchstation/src/dash_lib.c:501-503`; `Launchstation/src/dash_lib.c:517-526`; `Launchstation/src/dash_devices.c:187-190`; `Launchstation/src/dash_store.c:466-469`; `Launchstation/src/dash_store.c:699-702`; `Launchstation/src/dash_themes.c:141-144`; `Launchstation/src/dash_themes.c:469-472`; `Launchstation/src/dash_themes.c:631-634`; `Launchstation/src/dash_themes.c:1122-1195`; `Launchstation/src/dash_themes.c:1281-1284`; `Launchstation/src/dash_main.c:126-129`; `Launchstation/src/dash_main.c:200-204`; `Launchstation/src/config.c:86-102`; `Launchstation/src/config.c:1342-1345`; `Launchstation/src/config_network.c:70-73`
- Problem: Each name appears only at its prototype and definition:dashThemePresetIndex, dashThemeMarkCustom, dashThemeFolderLoad, dashThemeFolderExport, dashThemeBgId, dashThemeMarkDirty, dashGfxShutdown, dashFontShutdown, dashTextWrapScrollTimed, dashIconsShutdown, dashIconsTexture, dashStoreLoaded, dashStorePersisted, dashLibShutdown, dashLibRemove, dashArtShutdown, dashDrawAppTile, dashBgDrawStatic, dashMcPresent, dashDrawFocusFrame, dashVisibleSectionName, dashFileBrowser, dashOkLabelKey, dashAppsVisibleCount, dashAppsFocusPath, DashIsEnabled, configFormatSavePathValue, configDefaultSaveTargetPath, configSaveNetworkToPath. The linker already discards all 29 (wLaunchELF-gc-discarded-first-party.tsv). The five *Shutdown functions are dead because DashRun never returns (dash_main.c:1396).
- Fix: Delete the 29 definitions and their prototypes (and the orphaned comment on dash_internal.h:643). Drop the source-text asserts that mention dashFileBrowser (tests/test_files_tab_lifecycle.py:24, tests/test_r3z_browser_contract.py:87), dashMcPresent (tests/test_device_input.py:394-395) and dashStoreLoaded (tests/test_device_autostart_safe_mode.py:23). Then re-check whether static helpers used only by these (for example in dash_themes.c around 1122-1195) become unused too.
- Benefit: ~310 source lines (279 definition lines measured by brace span + ~31 prototype lines). 5,108 bytes of .text already gc-discarded (MEASURED, gc tsv sum), so no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-031: libhdd: hddGetFilesystemList, hddGetInfo, hddRemoveFilesystem, hddExpandFilesystem and the state that only they read are unused

- Category: 1 (Dead code)
- Location(s): `Launchstation/ee_libs/hdd/libhdd.c:135-260`; `Launchstation/ee_libs/hdd/libhdd.c:264-288`; `Launchstation/ee_libs/hdd/libhdd.c:459-569`; `Launchstation/ee_libs/hdd/libhdd.c:43`; `Launchstation/ee_libs/hdd/libhdd.c:73`; `Launchstation/ee_libs/hdd/include/libhdd.h:21-25`; `Launchstation/ee_libs/hdd/include/libhdd.h:31-33`; `Launchstation/ee_libs/hdd/include/libhdd.h:35-36`; `Launchstation/ee_libs/hdd/include/libhdd.h:39-65`; `Launchstation/ee_libs/hdd/include/libhdd.h:74-78`; `Launchstation/ee_libs/hdd/include/libhdd.h:84-89`
- Problem: Launchstation calls only hddCheckPresent, hddCheckFormatted, hddFormat and hddMakeFilesystem (src/dash_hdd.c:152,155,267,285). The other four exported functions have no caller, and hddExpandFilesystem (503-564) is a copy of the sub-partition loop in hddMakeFilesystem (380-442). hddSize and hddFree, and the dread loop in hddUpdateInfo that computes hddFree, feed only hddGetInfo. Only hddMaxPartitionSize is read by live code. The header's t_hddFilesystem/t_hddInfo types, the PFS_MT_ROBUST, FS_TYPE_EXT2/EXT2_SWAP/REISER/CFS, FS_COMMON_PREFIX, ATTR_SUB_PARTITION and FS_TYPE_EMPTY macros, and the deprecated hddPowerOff-style aliases (plus their libpwroff.h include) are used by nothing else.
- Fix: Delete the four functions, _OMIT_SYSTEM_PARTITION, hddSize/hddFree and the partition dread loop in hddUpdateInfo, leaving it as the HDIOC_MAXSECTOR query. Delete the unused types, macros, aliases and prototypes from libhdd.h. Keep FS_GROUP_*, FS_TYPE_PFS and ATTR_MAIN_PARTITION (used by dash_hdd.c and filer.c:742,846). Optionally drop the hddUpdateInfo calls in hddCheckPresent/hddCheckFormatted/hddFormat, which do not use the result. Upstream-derived (ps2sdk ee/rpc/hdd), already a private copy per ps2.yaml:420-427.
- Benefit: ~270 lines removed. 1,608 bytes of .text already gc-discarded (MEASURED: 784+680+72+72 in gc tsv). Trimming hddUpdateInfo saves its live dread loop code and one full APA directory scan per first probe/format/create (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-032: Classic-UI fallback drawing code in main_exploit_installer.c is dead in the dashboard build (exploitInstallerUi is always set)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main_exploit_installer.c:430-447`; `Launchstation/src/main_exploit_installer.c:664-687`; `Launchstation/src/main_exploit_installer.c:729-737`; `Launchstation/src/main_exploit_installer.c:751-789`; `Launchstation/src/main_exploit_installer.c:804-858`; `Launchstation/src/main_exploit_installer.c:973-974`; `Launchstation/src/main_exploit_installer.c:1022-1023`; `Launchstation/src/main_exploit_installer.c:2327-2347`; `Launchstation/src/main_actions.c:802-804`; `Launchstation/src/dash_exploit.c:159-161`
- Problem: Every dashboard route to the installer (dashRunMisc at dash_main.c:474-476, reached from tools, home, browser and launch keys; dash_main.c:398-399 refuses to send it to ExecuteMainAction) goes through dashExploitInstaller, which sets exploitInstallerUi with all five callbacks non-NULL (dash_exploit.c:148,159). The classic menu loop, OK dialog (a copy of gui.c ynDialog layout), progress box, info page, console box, button helpers and getFilePath pickers only run when exploitInstallerUi is NULL, i.e. via main_actions.c:803, which only a hand-edited MISC path in a non-dashboard ExecuteMainAction caller could reach.
- Fix: Delete the classic fallback branches (draw_console_info_box, the classic half of draw_install_progress, is_ok_button/is_cancel_button, the classic loops of choose_from_menu, ok_result_dialog and show_exploit_installer_info, and the getFilePath fallbacks) and call the exploitInstallerUi callbacks directly. Make main_actions.c:802-804 call dashExploitInstaller() so the UI table is always installed.
- Benefit: ~170 source lines removed; main_exploit_installer.o .text is 23,592 bytes (MEASURED, BUILD-EVIDENCE.md:88); the fallback is reachable through a runtime NULL test so gc cannot drop it, estimated ~2 KB .text saved (INFERRED from line share)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-033: mc_card_export.c has five near-identical copy-then-verify loops (backup, active-MMCE backup, PCSX2 export x2, PCSX2 import)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_card_export.c:239-296`; `Launchstation/src/mc_card_export.c:298-351`; `Launchstation/src/mc_card_export.c:377-441`; `Launchstation/src/mc_card_export.c:443-498`; `Launchstation/src/mc_card_export.c:556-610`
- Problem: Each function has the same argument checks, a chunked write pass, a chunked read-back verify pass with memcmp, progress calls and remove-on-failure tail. They differ only in three points: the source is read through a kept-open fd (ActiveMmce variants) or reopened per chunk; the block transform (none, convert_block, strip_pcsx2_block); and whether the destination is created exclusively (only_new). The ActiveMmce pair duplicates the path pair line for line except for read_exact_fd vs read_exact_at.
- Fix: Fold them into one static engine taking (keep_source_open, transform, only_new). Keep the nine public names as one-line wrappers so the callers and tests do not change.
- Benefit: ~150 lines removed (288 lines across the five bodies to ~110 plus wrappers, INFERRED). Their code is 732+736+912+820+1032 = 4232 B (MEASURED, sizes tsv); about 2 KB saved after merging (INFERRED).
- Effort: M. Risk: High. Confidence: High (READ)

#### LAUNCHSTATION-034: copy() builds the classic text progress report and speed/ETA stats every chunk, but the dashboard progress always replaces it

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_copy.c:1454-1540`; `Launchstation/src/filer_copy.c:359-398`; `Launchstation/src/filer_copy.c:775-781`; `Launchstation/src/filer_copy.c:1438-1441`; `Launchstation/src/filer_copy.c:31`; `Launchstation/src/filer_copy.c:1589`; `Launchstation/src/filer.c:53`; `Launchstation/src/filer.c:208-209`; `Launchstation/include/filer_shared.h:46`; `Launchstation/include/filer_shared.h:61-62`; `Launchstation/src/filer_browser.c:2159-2160`
- Problem: Each chunk computes speed/ETA (1454-1480) and formats a ~15-sprintf/strcat report into progress (1482-1531), used only when DashFileProgress returns 0 (1534-1540). DashFileProgress returns 0 only when the dashboard is inactive or on the worker thread (dash_ui.c:1431); copy() is only called from subfunc_Paste (filer_browser.c:2208), reached from the dashboard browser on the UI thread (dash_browser.c:892-894) while DashRun, which never returns, keeps dash_active=1.
- Fix: Delete the speed/ETA computation, the progress text, calcTransferSpeed/calcRemainingTime/smoothTransferSpeed, TRANSFER_ETA_UPDATE_MS, the related locals, PasteProgress_f, written_size and PasteTime; keep the DashFileProgress call and use its result only to choose the worker chunk path. Update the helper-name lists in tests/test_copy_pipeline.py:221, test_copy_psu_listing.py:282, test_listing_callers.py:246. The five LNG strings (Remain_Size, Current_Speed, Remain_Time, Written_Total, Average_Speed) then become unused in Lang/*.LNG and Lang/DASHBOARD.inc.
- Benefit: ~135 source lines; removes ~15 sprintf/strcat and 3 divisions per chunk on the UI thread; a few hundred bytes of .text (INFERRED, not measured).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-035: kelf_signer.c host-test scaffolding (KELF_SIGNER_HOST_TEST, KELF_SIGNER_SELFTEST) and kelf_stricmp are unused

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/kelf_signer.c:1-33`; `Launchstation/src/kelf_signer.c:35`; `Launchstation/src/kelf_signer.c:1097-1153`; `Launchstation/src/kelf_signer.c:268-291`
- Problem: No build file, test or script defines KELF_SIGNER_HOST_TEST or KELF_SIGNER_SELFTEST, and no test mentions kelf_signer, so the POSIX shim (lines 1-33) and the main() self-test (1097-1153) are never compiled. kelf_stricmp only exists so the host build works without the PS2 stricmp, which the rest of src uses everywhere.
- Fix: Delete the #ifdef KELF_SIGNER_HOST_TEST block and its #else/#endif (keep #include "launchelf.h"), delete the KELF_SIGNER_SELFTEST main(), and replace kelf_stricmp with stricmp.
- Benefit: ~112 source lines removed. The #ifdef blocks are not compiled, so no ELF change; replacing kelf_stricmp saves roughly 100 bytes of .text (INFERRED, kelf_signer.o is 13252 B per wLaunchELF-contrib-by-object.tsv)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-036: editor_input.c: pad and USB-keyboard handlers copy the same mark, clipboard, return-key, char-insert and apply-operation blocks

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/editor_input.c:217-227`; `Launchstation/src/editor_input.c:238-250`; `Launchstation/src/editor_input.c:253-292`; `Launchstation/src/editor_input.c:299-306`; `Launchstation/src/editor_input.c:333-352`; `Launchstation/src/editor_input.c:369-426`; `Launchstation/src/editor_input.c:482-492`; `Launchstation/src/editor_input.c:512-524`; `Launchstation/src/editor_input.c:527-566`; `Launchstation/src/editor_input.c:571-578`; `Launchstation/src/editor_input.c:585-595`; `Launchstation/src/editor_input.c:606-639`; `Launchstation/src/editor_input.c:644-696`
- Problem: editorVirtualKeyboardEntry and editorKeyboardEntry repeat the same code: mark-range normalise (7 copies of ~7 lines), copy-to-clipboard malloc+loop (4 copies), MARK toggle (2x13 lines), PASTE setup (2x8), RETURN ins1..ins5 table (2x19 lines), the buffer-apply switch (369-426 vs 644-696, ~50 lines) and the single-char insert setup, which editorPrepareSingleCharInsert (153-167) already implements but 628-639 re-inlines.
- Fix: If the classic editor stays: call editorPrepareSingleCharInsert at 628-639, and move the shared blocks into a few static helpers (normalise mark, copy mark to clipboard, set return ins values, apply Operation with the inserted char as parameter) called from both handlers.
- Justification: The same blocks occur 2 to 7 times in one file; each helper replaces at least two copies, so the file shrinks.
- Benefit: ~110 source lines removed (INFERRED from the cited ranges: apply block ~50, return table ~19, mark/clip copies ~30, char insert ~11); a few hundred bytes of .text (INFERRED). Moot if finding 1 is done.
- Effort: M. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-037: Classic Neutrino options dialog is only reachable before DashRun (hand-edited IGR after-app path)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/gui_neutrino.c:264-280`; `Launchstation/src/gui_neutrino.c:348-350`; `Launchstation/src/gui_neutrino.c:392-406`; `Launchstation/src/gui_neutrino.c:410-420`; `Launchstation/src/gui_neutrino.c:433-503`; `Launchstation/src/main.c:221-243`
- Problem: showNeutrinoLaunchOptions uses the dashboard modal whenever DashIsActive(). dash_active is set at DashRun entry (dash_main.c:1349) and cleared only at exec cleanup (dash_main.c:316); DashRun is the only shell (main.c:293-307). The only launch before DashRun is runWleIgrAfter (main.c:221-243), whose app path comes from a picker limited to ELF (dash_settings.c:2074), so the classic pad loop, its draw helpers and moveNeutrinoSelection run only if a user hand-edits DASHBOARD.CNF to point the IGR after-app at an ISO. The comment at 348-350 still says the classic shell uses it.
- Fix: Delete moveNeutrinoSelection, drawNeutrinoHeading, drawNeutrinoOption and the classic loop; when DashIsActive() is false return TRUE with the zeroed (default) options, which matches the existing no-profile launch-only defaults. Keep the enum values so NEUTRINO_CANCEL row indexes used by the dashboard menu do not shift, and update the source-text anchors in tests/test_r3z_neutrino_options.py:37 and tests/test_system_clock_menu.py:37.
- Benefit: ~110 source lines removed; the static functions and the loop are live code today, so the ELF shrinks by their size (INFERRED, roughly 1-2 KB of .text plus format strings).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-038: SYSTEM.CNF temp-file editing reimplements dashWriteFile/dashLoadTextFile and probes up to 100 temp names

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/hdd_header_injector.c:14-15`; `Launchstation/src/hdd_header_injector.c:689-805`; `Launchstation/src/hdd_header_injector.c:847-861`; `Launchstation/src/dash_store.c:105-175`
- Problem: hddHeaderWriteTempFile + configEnsureSysconfDir (850) is what dashWriteFile does (dash_store.c:154-162, plus read-back verify), and hddHeaderReadTempFile is a bounded whole-file read like dashLoadTextFile. Four helpers repeat the same 'genFixPath or copy the raw path' fallback (698-701, 722-725, 761-764, 800-803), and hddHeaderBuildTempPath opens up to 100 WLEHCnn.TMP names on the card although the edit runs synchronously on the UI thread with the worker idle (dash_hdd.c:960-966).
- Fix: Use one fixed temp name built with configBuildSysconfPath, write it with dashWriteFile, read it back with dashLoadTextFile(path, APA_HEADER_SYSTEM_CNF_MAX_SIZE, &len), remove it with genRemove. Delete hddHeaderWriteTempFile, hddHeaderReadTempFile, hddHeaderTempFileExists, hddHeaderBuildTempPath, hddHeaderRemoveTempFile and the TEMP_FILE_FORMAT/TRIES macros.
- Benefit: ~100 lines removed (117 lines of helpers replaced by ~10 calls); fewer memory-card opens per edit (no name probing). INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-039: init.c keeps #else branches for EXFAT and HOMEBREW_SIO2MAN, which every build defines

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/init.c:83-85`; `Launchstation/src/init.c:336-338`; `Launchstation/src/init.c:418-431`; `Launchstation/src/init.c:720-722`; `Launchstation/src/init.c:1794-1804`; `Launchstation/src/init.c:1861-1870`; `Launchstation/src/init.c:1882-1892`; `Launchstation/src/init.c:1931-1936`; `Launchstation/src/init.c:2089-2099`; `Launchstation/src/init.c:2403-2409`; `Launchstation/src/init.c:2443-2448`; `Launchstation/src/init.c:2559-2561`; `Launchstation/src/init.c:1236-1242`; `Launchstation/src/init.c:1259-1265`; `Launchstation/src/init.c:1313-1317`; `Launchstation/src/init.c:1905-1914`; `Launchstation/src/init.c:1702-1707`; `Launchstation/ps2.yaml:623-629`
- Problem: ps2.yaml:623 and :629 always define HOMEBREW_SIO2MAN and EXFAT, and scripts/configure_ds34.py (the only other build config) copies them unchanged. So the non-EXFAT paths (classic usb_mass driver, busy-loop delay(), mass:/massN: Ember remount names, MX4SIO/iLink fallbacks, loadPcCardCfModules stub) and the rom0:SIO2MAN/PADMAN + MC_TYPE_MC paths are never compiled. The non-EXFAT branch could not even link: usb_mass_irx is not in the ps2.yaml embed list (only usbmass_bd_fork is).
- Fix: Delete the #ifndef/#else branches and the #ifdef EXFAT / #ifdef HOMEBREW_SIO2MAN guards in init.c, keeping the defined-path code (and simplify pathUsesUsbMass line 1706 to the bare mass: test once the numbered-slot early return is unconditional). Other files that test these macros belong to other units. Tests that grep for '#ifdef EXFAT' (tests/test_ember_two_usb_handoff.py:112-129 and similar) need the matching edit.
- Benefit: ~100 source lines removed (about 62 lines of dead #else bodies plus ~40 guard lines, counted from the cited ranges); no ELF change because the preprocessor already drops them (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-040: update_psu.c: file-based and ELF-writing extract paths (selfUpdateExtractPsu, ...Mem, ...Sidecar, selfUpdateWriteProgress) are test-only

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/update_psu.c:18`; `Launchstation/src/update_psu.c:29-35`; `Launchstation/src/update_psu.c:41-42`; `Launchstation/src/update_psu.c:49-59`; `Launchstation/src/update_psu.c:102-134`; `Launchstation/src/update_psu.c:174-179`; `Launchstation/src/update_psu.c:183-208`; `Launchstation/src/update_psu.c:335-354`; `Launchstation/include/self_update.h:13-17`; `Launchstation/include/self_update.h:23-24`; `Launchstation/src/self_update.c:203`
- Problem: The updater only calls selfUpdateLocatePsuElf (self_update.c:306, stage NULL) and selfUpdateExtractPsuSidecarMem (:327, :333). selfUpdateExtractPsu, selfUpdateExtractPsuMem and selfUpdateExtractPsuSidecar are called only by tests/test_app_packages.py:69-73, and test_self_update.py:29 even asserts selfUpdateExtractPsu( is gone from self_update.c. So the fd input path (in_open, genRead/genLseek branches, PsuIn.fd), the stage-writing branch of extract_elf with its write loop and cleanup, and selfUpdateWriteProgress (only read inside that branch, set at self_update.c:203) never run.
- Fix: Delete selfUpdateExtractPsu, selfUpdateExtractPsuMem, selfUpdateExtractPsuSidecar, in_open, the fd branches of read_exact/in_seek and PsuIn.fd; make extract_elf locate-only (drop stage, output, buffer, the write loop and the remove at 176-179); delete selfUpdateWriteProgress, its extern and the assignment at self_update.c:203. Point tests/test_app_packages.py at selfUpdateLocatePsuElf and selfUpdateExtractPsuSidecarMem.
- Benefit: ~95 source lines. MEASURED: the 3 public functions (752 B) are already gc-discarded (evidence raw/experiment-fdata/link/wLaunchELF-gc-discarded-first-party.tsv:95-97); the live extract_elf write branch, fd branches and pointer go too, INFERRED a few hundred bytes of .text.
- Effort: M. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-041: File-backed PSU extractors, the extract-to-stage path and selfUpdateWriteProgress are unused by the shipped updater

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/self_update.h:13-17`; `Launchstation/include/self_update.h:23-24`; `Launchstation/src/self_update.c:203`; `Launchstation/src/update_psu.c:183-208`; `Launchstation/src/update_psu.c:335-354`; `Launchstation/src/update_psu.c:49-59`; `Launchstation/src/update_psu.c:102-134`; `Launchstation/src/update_psu.c:18`; `Launchstation/src/update_psu.c:224`
- Problem: selfUpdateInstall only calls selfUpdateLocatePsuElf and selfUpdateExtractPsuSidecarMem (self_update.c:306,327,333). selfUpdateExtractPsu, selfUpdateExtractPsuMem and selfUpdateExtractPsuSidecar have no caller in any .c/.h; only tests/test_app_packages.py:69-73 calls them. With those gone, in_open, the fd branches of read_exact/in_seek and the stage!=NULL write loop of extract_elf are unreachable, and selfUpdateWriteProgress (set at self_update.c:203) is only invoked inside that dead write loop (update_psu.c:125).
- Fix: Delete selfUpdateExtractPsu, selfUpdateExtractPsuMem, selfUpdateExtractPsuSidecar and their prototypes, in_open and the fd field/branches of PsuIn, the stage-writing branch of extract_elf (keep locate-only), selfUpdateWriteProgress and its assignment at self_update.c:203. Point tests/test_app_packages.py at selfUpdateLocatePsuElf and selfUpdateExtractPsuSidecarMem.
- Benefit: ~90 source lines removed (26+20+11+33 in update_psu.c, 7 header lines, fd branches). The three public functions are already dropped by --gc-sections; the static stage path and fd branches are not: a few hundred bytes of .text (INFERRED).
- Effort: M. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-042: mc_sha256.c duplicates the Mbed TLS SHA-256 that is linked into the same ELF

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/mc_sha256.c:1-94`; `Launchstation/include/mc_sha256.h:1-24`; `Launchstation/src/mc_save_export.c:7-12`; `Launchstation/ps2.yaml:540`
- Problem: The shipped wLaunchELF links both mcSha256Init/Update/Final and mbedtls_sha256_starts/update/finish (Mbed TLS is always built in: WLE_NATIVE_CLOUD and mbedcrypto are unconditional, and mc_save_export.c already includes mbedtls headers). The header's reason, builds without the TLS stack, no longer exists; the only remaining reason is that host tests compile mc_sha256.c without Mbed TLS.
- Fix: On the target, make McSha256 a typedef of mbedtls_sha256_context and the three functions thin static inline calls to mbedtls_sha256_starts(.., 0)/update/finish in mc_sha256.h, and stop compiling src/mc_sha256.c in ps2.yaml. Host tests either link the host Mbed TLS or keep the current portable file as a test-only source under tests/.
- Benefit: MEASURED from sizes/wLaunchELF-unstripped-all-symbols-by-size.tsv: mcSha256Update 1260 + mcSha256Final 564 + mcSha256Init 72 + iv 32 bytes, plus the 256-byte K table (INFERRED: 64 x u32) = about 2.2 KB of ELF.
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-043: tex_drawChar builds each glyph through a 16x32 temp image with run detection; it compiles to 10.5 KB

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/makeicon.c:110-159`; `Launchstation/src/makeicon.c:165`; `Launchstation/src/makeicon.c:223-225`
- Problem: tex_drawChar zeroes a 16x32 u16 temp array, finds vertical runs of set font bits, sets every other pixel, ORs the gaps in by rows then by columns, and copies the whole block out. The result is a 2x2 pixel block for every set font bit. The measured function size is 10548 bytes (loops unrolled). The colour parameter is never used (always 0xFFFF), and tex_printXY's return value is never read ('not needed for anything').
- Fix: Replace the body with a double loop over the 8x16 font bits that writes a 2x2 block of 0xFFFF straight into tex_buffer for each set bit; the buffer is already zeroed in make_icon. Drop the unused colour parameter and make tex_drawChar/tex_printXY static and void.
- Benefit: ~40 lines removed; tex_drawChar is 10548 B of .text (MEASURED, sizes/wLaunchELF-unstripped-all-symbols-by-size.tsv); a direct 2x2 write is well under 1 KB, so about 9-10 KB of ELF saved (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-044: prepareExploitSignerIop reboots the IOP without the teardown resetRuntimeDeviceState requires (FTP close, PAD close, mc poll drain, sound)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/init.c:821-882`; `Launchstation/src/init.c:2172-2206`; `Launchstation/src/init.c:3048-3098`; `Launchstation/src/main_exploit_installer.c:1053-1057`
- Problem: The exploit installer's signer reboot (init.c:838-874) only stops SMB/PCCF, closes the keyboard and unmounts. It skips what the file's own reset path documents as required: ftpSessionShutdown refusal (2172-2177; an FTP upload to the card is cut by the reboot), filerRootMcPollIdle (2182-2186; pending mcGetInfo leaves the pad dark), padPortClose/padEnd before setupPad (2197-2205; 'UI alive, controller dead'), dashSoundShutdown (2189-2193). The installer then needs pad input and writes to the card.
- Fix: Run the signer reboot through the same path as every other reset: let Reset() take an optional IOPRP buffer (NULL = SifIopReset) and have prepareExploitSignerIop call resetRuntimeDeviceState with it, then check have_exploit_signer_iop/generation for a refused reset. This deletes the duplicated reboot/reload sequence at 845-874.
- Benefit: Closes a card-corruption window (FTP write cut mid-file) and an intermittent dead-controller state after the signer reboot; ~25 duplicated reset lines removed (INFERRED from 845-874 vs 3053-3093).
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-045: copy() allocates a second 1 MiB buffer (buff2) that never overlaps any I/O

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/filer_copy.c:1427-1436`; `Launchstation/src/filer_copy.c:1557`; `Launchstation/src/filer_copy.c:1591-1592`; `Launchstation/src/filer_copy.c:764`; `Launchstation/src/filer_copy.c:773`; `Launchstation/src/filer_copy.c:1698`; `Launchstation/src/dash_ui.c:1452-1466`
- Problem: For a file of at least 1 MiB on the fast-device path, copy() allocates buff2 (another COPY_BUFFER_FAST_DEFAULT, 1 MiB) and alternates the two buffers per chunk. Each chunk is one read and then one write of the same buffer (copyChunkIo, filer_copy.c:67-76), and DashFileTransferChunk waits until the worker job is done (dash_ui.c:1463-1464) or the chunk runs inline (line 1563). No read ever overlaps a write, so the second buffer only doubles the peak heap.
- Fix: Delete buff2, buffCount, buffIndex and copyBuff, and pass buff to the chunk.
- Benefit: 1 MiB less peak heap during every fast-path copy of a file >= 1 MiB (0x100000, filer_copy.c:32; INFERRED); about 14 lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-046: fbSortFiles insertion-sorts 388-byte FILEINFO structs on the UI thread: O(n^2) for folders up to 2048 entries

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_browser.c:265-278`; `Launchstation/src/dash_browser.c:384-385`; `Launchstation/src/dash_worker.c:221-229`; `Launchstation/tests/test_browser_sort.py:26-31`
- Problem: listDone (a worker completion callback, run on the UI thread by dashWorkerPump -> finish, dash_worker.c:221-229) sorts the whole listing with an insertion sort that moves whole FILEINFO structs (char[256] + title[66] + 64-byte sceMcTblGetDir, about 388 B). Average cost is n*n/4 moves: at the MAX_ENTRY cap of 2048 that is about 1.05 M struct copies (about 400 MB of memory traffic) plus as many fbCompare calls (Type sort, the default, runs several extension compares per call), all before the next frame. Large USB/HDD folders therefore stall the UI when opened, refreshed or re-sorted.
- Fix: Replace the body of fbSortFiles with a qsort call (qsort is already linked: dash_card_tool.c:1766, dash_lib.c:599, dash_widgets.c:950) through a two-line wrapper that calls fbCompare. fbCompare already ends in a name tiebreak, so the order is unchanged except for names that differ only in case. Add #include <stdlib.h> to the host harness string in tests/test_browser_sort.py, which compiles the extracted function with -Werror.
- Benefit: INFERRED: worst case drops from ~1.05 M moves of 388 B to ~n*log2(n) = ~22.5 k swaps at n=2048; ~0.1-0.25 s per open at n=500 and multi-second stalls near the cap go away. ~8 lines removed.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-047: Power-button handler is lost when load_ps2dev9 reboots the IOP after the caller armed it, and is never armed in USB-only sessions

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/init.c:505-511`; `Launchstation/src/init.c:2637-2654`; `Launchstation/src/init.c:3001`; `Launchstation/src/init.c:3024`; `Launchstation/src/init.c:921-922`; `Launchstation/src/init.c:967-969`; `Launchstation/src/init.c:1024-1026`; `Launchstation/src/init.c:2551-2552`; `Launchstation/src/init.c:2677-2678`; `Launchstation/src/init.c:2701-2702`; `Launchstation/src/init.c:2723-2724`; `Launchstation/src/init.c:2614-2615`
- Problem: Each DEV9 loader calls setupPowerOff() and then reaches load_ps2dev9, which reboots the IOP when the Legacy Ethernet setting was toggled (dash_settings.c:1905 changes it live). Reset clears have_poweroff and done_setupPowerOff (3001, 3024) and nothing re-arms them, so the new HDD/ATA/network stack runs without poweroff.irx: a power-button press skips PFS CLOSEALL, HDD idle and DEV9 off. USB, MMCE, MX4SIO and iLink loaders never call setupPowerOff, so the USB STOP UNIT at 2614-2615 never runs from the button in those sessions.
- Fix: Call setupPowerOff() once inside load_ps2dev9 after the variant-switch reset and before DEV9 is executed, and delete the seven caller calls at 921, 967, 1024, 2551, 2677, 2701, 2723 (all reach load_ps2dev9). Add the same single call in loadUsbModules if USB HDD STOP UNIT on the button is wanted.
- Justification: Moving the call to the one place every DEV9 path passes after any reset fixes the ordering bug and removes six call sites; the optional USB call is one line for the documented emergency-park problem.
- Benefit: Power-button shutdown again flushes HDD/PFS after a DEV9 family switch and stops USB disks; ~6 call lines removed (READ: 7 removed, 1 added).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-048: dash_browser.o keeps ~88 KB of one-shot scratch in .bss: eight static int[2048] index arrays and two 16 KB compare buffers

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_browser.c:822`; `Launchstation/src/dash_browser.c:978`; `Launchstation/src/dash_browser.c:1054`; `Launchstation/src/dash_browser.c:1079`; `Launchstation/src/dash_browser.c:1172`; `Launchstation/src/dash_browser.c:2698`; `Launchstation/src/dash_browser.c:2795`; `Launchstation/src/dash_browser.c:3045`; `Launchstation/src/dash_browser.c:1402-1403`
- Problem: Each of opCopy, opExtractPsuTo, opImportSavesTo, opDelete, opSize and actionsMenu (three: sv_idx, psu_idx and a second sv_idx that shadows the first inside case F_IMPORT_SAVES) has its own static int[MAX_ENTRY] for targets(); all run on the UI thread one after another and never hold two of them live at once. activeHistoryFilesEqual adds two static 16 KB buffers used only by the rare active-card dedupe. Together 96 KB of the object's 104 KB .bss, reserved for the whole run.
- Fix: Use one file-scope static int index array for every targets()/psuTargets()/saveFileTargets()/ps1SaveTargets() caller and delete the eight local statics (this also removes the shadowed sv_idx). In activeHistoryFilesEqual malloc one 32 KB block for the two halves at entry and free it at done.
- Justification: The malloc needs one NULL check (returning -1, which callers already treat as 'could not compare'); it replaces 32 KB of permanent .bss used only by one rarely chosen menu action.
- Benefit: MEASURED map: 8 x 0x2000 + 2 x 0x4000 = 96 KB; sharing saves 7 x 8192 = 56 KB and malloc saves 32 KB -> 88 KB .bss (of 106560 B). Rated 3 not 4: RAM headroom only, the browser already mallocs ~1.5 MB per listing.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-049: dash_info.c keeps four separate static 6.3 KB Sheet buffers (one per info screen)

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_info.c:22-34`; `Launchstation/src/dash_info.c:129`; `Launchstation/src/dash_info.c:147`; `Launchstation/src/dash_info.c:220`; `Launchstation/src/dash_info.c:347-348`
- Problem: dashShowTextLines, dashShowAbout, dashShowBuildInfo and dashShowSystemInfo each declare their own static Sheet (48 lines x 132 B + count = 6340 B), so 25,360 B of .bss is reserved although only one modal sheet can be on screen at a time. dashShowSystemInfo also keeps a static SysInfo copy of cached_console_info that it only reads.
- Fix: Use one file-scope static Sheet for all four functions (each already resets s.n = 0 before filling it) and read cached_console_info directly instead of copying it into the static job.
- Benefit: 19,020 B of .bss (MEASURED: wLaunchELF-unstripped-all-symbols-by-size.tsv lists dashShowTextLines.s, dashShowAbout.s, dashShowBuildInfo.s, dashShowSystemInfo.s at 6340 B each; 3 x 6340 saved) plus 92 B for dashShowSystemInfo.job.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-050: Hdl_Inited survives IOP reboots, so HdlGetGameInfo can call a stale hdl_info RPC client

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/hdl_rpc.c:24-50`; `Launchstation/src/init.c:2563-2574`; `Launchstation/src/init.c:2998`; `Launchstation/src/dash_hdd.c:419-449`
- Problem: clearIopModuleState (called by Reset, init.c:3066) clears have_hdl_info (init.c:2998) but nothing clears hdl_rpc.c's Hdl_Inited. After the HDD manager has loaded hdl_info once, any IOP reboot (stack switch, rebootIopAndReloadCoreStack) leaves Hdl_Inited=1, so dash_hdd.c hdlReady returns 1 without reloading the module and loadGameInfo calls HdlGetGameInfo -> SifCallRpc on a client bound to a server that no longer exists. Hdl_Info_BindRpc also sets Hdl_Inited=1 when its retry loop times out, and init.c:2571 ignores the result. Three flags (have_hdl_info, Hdl_Inited, the static 'tried' in hdlReady) track one state.
- Fix: Make one flag the truth: set Hdl_Inited only when client.server != 0, clear it in clearIopModuleState next to have_hdl_info (or drop Hdl_Inited and let loadHdlInfoModule return the bind result), and delete the static 'tried' in hdlReady since loadHdlInfoModule is already idempotent through have_hdl_info.
- Justification: Adds one reset line (or a return value); it fixes a reachable stale-RPC call after an IOP reboot and removes the redundant 'tried' flag, so the line count stays about even.
- Benefit: Removes a possible EE hang or IOP crash on HDD manager use after an IOP reboot (consequence of calling a dead RPC server is INFERRED); ~4 lines net removed.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-051: hdd_header_copy_buffer keeps 32 KB of .bss for a rare header-injection copy while a 2 MB heap buffer was just freed

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/hdd_header_injector.c:18`; `Launchstation/src/hdd_header_injector.c:89`; `Launchstation/src/hdd_header_injector.c:1044-1058`; `Launchstation/src/hdd_header_injector.c:1142-1155`
- Problem: The static 32768-byte copy buffer is used only by hddHeaderCopyOptionalPfsFile during InjectHddPartitionHeaderFromSource. That function memaligns the 2 MB hdd_header_buffer and frees it (1150) immediately before calling hddHeaderCopyOptionalPfsFiles (1155), so the 32 KB stays reserved for the whole session for no reason.
- Fix: Delete the static buffer and HDD_HEADER_COPY_BUFFER_SIZE; free hdd_header_buffer after the PFS copy instead of before it and use its first 32 KB (or all of it) as the copy buffer.
- Benefit: 32,768 bytes of EE .bss freed (MEASURED: BUILD-EVIDENCE.md:91 lists hdd_header_injector.o .bss 33,920; the copy buffer is 32,768 of it); no extra allocation; ~2 lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-052: Artwork results for transient Home entries mark DASHLIB.CNF dirty, causing byte-identical library cache rewrites (idle and before every launch)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_art.c:392-425`; `Launchstation/src/dash_home.c:60-72`; `Launchstation/src/dash_lib.c:444-452`; `Launchstation/src/dash_lib.c:624-627`; `Launchstation/src/dash_main.c:646-648`; `Launchstation/src/dash_main.c:1488-1489`
- Problem: Home resolves recents/pins/favorites that are not in the library into home_tmp[] transients that start at DASH_ART_UNKNOWN on every homeRebuild (dash_home.c:69, dash_lib.c:444-452). When their art job ends, artDone sets art_dirty on UNKNOWN->NONE or ->FOUND (dash_art.c:397,422). That schedules a full DASHLIB.CNF rewrite after 6 s idle and before each launch (dash_main.c:647-648,1488-1489), but dashLibBuildCache skips TRANSIENT entries (dash_lib.c:626-627), so the file written is byte-identical. Transients without art have no slot, so every Home rebuild re-probes them and repeats the write.
- Fix: In artDone, set art_dirty only when the app is not DASH_APP_TRANSIENT (the two assignments at dash_art.c:397 and :422).
- Justification: Adds one flag test to two existing conditions. It removes a whole-file memory-card rewrite of an unchanged file per Home rebuild and from the launch path; no simpler change stops it because the dirty flag is the only trigger.
- Benefit: One redundant DASHLIB.CNF write (whole library text, roughly 10-30 KB for 100 apps, INFERRED) avoided per Home rebuild with an art-less transient and before such launches; less worker time and card wear.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-053: Classic editor leaks the whole text buffer on every inserted character: free() is swallowed into a comment

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/editor_input.c:369-375`; `Launchstation/src/editor_input.c:644-650`
- Problem: In both insert paths the line that should free the old buffer reads as one comment ('//memset(...); // 256 ... free(TextBuffer[Active_Window]);'), so the next line overwrites TextBuffer[Active_Window] with a new malloc and the old block (TextSize+ins1+256 bytes) is lost on every typed char, Return, Tab or Paste. Typing 100 characters into a 100 KB file leaks ~10 MB; when malloc then returns NULL, strcpy at :374/:649 writes through NULL. The delete path (:387, :664) shows the intended separate free().
- Fix: Put free(TextBuffer[Active_Window]) back on its own line in both places (or, simpler, realloc the buffer once instead of the TMP copy dance); if the classic editor is deleted (finding 1) this goes with it.
- Benefit: Fixes an unbounded heap leak per keystroke in the classic editor (INFERRED arithmetic: TextSize+ins1+256 bytes per insert).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-054: HDD Manager reports 0 MB on drives over 1 TiB (2 TB HDDs): HDIOC_TOTALSECTOR read as signed int

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_hdd.c:158-161`; `Launchstation/src/dash_hdd.c:348-351`; `Launchstation/src/dash_hdd.c:621-625`; `Launchstation/src/dash_hdd.c:881`
- Problem: scanRun stores HDIOC_TOTALSECTOR in an int and uses it only when > 0. The driver returns the u32 totalLBA through an int (iop/ps2hdd_osd/hdd_fio.c:1055,1087), so any drive above 2^31 sectors (1.5/2 TB, the APA maximum) comes back negative and size_mb becomes 0. Then free_mb is 0, Create says 'HDD full', the create-missing header path gets max 0 so pickSize returns -1, and the usage bar is hidden (hddScene needs size_mb > 0).
- Fix: Read the value as u32, as libhdd already does (ee_libs/hdd/libhdd.c:269): u32 sectors = (u32)fileXioDevctl(...); size_mb = sectors / SECTORS_PER_MB. present/formatted are already checked before this line, so the error case is not reached.
- Benefit: Partition creation and usage display work on 1.5-2 TB HDDs (common for HDL collections); one-line change, no size change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-055: Rename game fails for HDL titles longer than 39 characters: old title truncated into HddJob.a[40]

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_hdd.c:126`; `Launchstation/src/dash_hdd.c:716-717`; `Launchstation/src/dash_hdd.c:280-281`
- Problem: actRenameGame copies p->game.Name (up to 64 characters, include/hdl_rpc.h:12) into j->a, which is MAX_PART_NAME + 8 = 40 bytes, so dashStrCopy keeps 39 characters. The IOP looks the game up with strcmp(OldName, game->name) on the full title (iop/hdl_info/hdd.c:178), finds nothing and returns -3, and the user gets 'Game not renamed' for every title of 40+ characters.
- Fix: Size HddJob.a for the longest string it carries (64 + 1, the HDL title limit, iop/hdl_info/hdl.h:4) instead of MAX_PART_NAME + 8.
- Benefit: Fixes a functional bug in Rename game; +24 bytes per malloc'd HddJob, no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-056: SMB/NFS editor passes sizeof(buf) as dashModalKeyboard max_len: 1-byte stack overwrite, and edit[96] truncates the 127-char share path

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_settings.c:688-700`; `Launchstation/src/dash_settings.c:3376-3381`; `Launchstation/src/dash_settings.c:3393-3396`; `Launchstation/src/dash_settings.c:3405-3411`; `Launchstation/src/dash_widgets.c:341-350`
- Problem: dashModalKeyboard's max_len is a string length: kbInsert (dash_widgets.c:345) inserts while strlen < max_len, so the result can hold max_len chars plus the NUL. smbedEditField (693), the port field (3380-3381) and NFS uid/gid (3411) pass sizeof(buf), so a full-length entry writes the NUL one byte past edit[96], buf[8] or buf[12] on the stack. Other callers (3359-3360) pass sizeof-1. edit[96] is also smaller than smbed.folder[128] (398): a share/export path is cut to 95 chars at 692 though 3395 promises 127.
- Fix: Pass sizeof(edit) - 1 and sizeof(buf) - 1 at the three dashModalKeyboard calls, and declare edit as 128 bytes (the size of smbed.folder, the largest field edited there).
- Benefit: Closes a 1-byte stack overwrite reached by typing 8 chars in Port, 12 in UID/GID or 96 in a share field; share paths of 96-127 chars become enterable. 0 lines; +32 bytes of stack in smbedEditField (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-057: Save library and All saves re-read everything after actions that change nothing (ctLibToCard returns 1, ctSaveMenu Export returns 2)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_card_tool.c:2534`; `Launchstation/src/dash_card_tool.c:2681-2683`; `Launchstation/src/dash_card_tool.c:2708`; `Launchstation/src/dash_card_tool.c:2737-2739`; `Launchstation/src/dash_card_tool.c:2781-2783`; `Launchstation/src/dash_card_tool.c:2813-2816`; `Launchstation/src/dash_card_tool.c:2399-2418`; `Launchstation/src/dash_card_tool.c:1538-1542`; `Launchstation/src/dash_card_tool.c:2207-2210`; `Launchstation/src/dash_card_tool.c:2269-2270`
- Problem: ctLibToCard returns 1 ('may have changed') after copying .psu files to a card image, although it writes only to that image and its WLE-Safety/ copy, never to a library .psu. All three callers (marked-saves menu, Apply set, single-file menu) then break out and ctLibScan reopens, mallocs and fully parses every .psu again (up to 500, each up to 8.5 MB, ctLoadPsu 685-696) on the UI thread. In All saves, a single-save Export via ctSaveMenu returns 2 and the caller rescans every card image (up to 64), while the batch Export correctly returns 0 (2048-2050).
- Fix: Return 0 from ctLibToCard (the library is unchanged, so the list and marks can stay). In dashCardAllSaves rescan only after Copy, Move or Delete, e.g. let ctSaveMenu return -1 after ctExportPsu (dashCardTool ignores the return value).
- Benefit: Removes one full re-read of the library per copy: e.g. 100 saves x 300 KB = 30 MB at about 1 MB/s USB is about 30 s of loading screen (INFERRED). All saves: one rescan of up to 64 images per single export avoided.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-058: Text keyboard writes one NUL past the buffer when callers pass the buffer size as max_len (classicKeyboard, three SMB/NFS settings fields)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_widgets.c:341-350`; `Launchstation/src/dash_widgets.c:663-668`; `Launchstation/src/main_exploit_installer.c:962`; `Launchstation/src/main_exploit_installer.c:981`; `Launchstation/src/dash_settings.c:690-693`; `Launchstation/src/dash_settings.c:3377-3381`; `Launchstation/src/dash_settings.c:3408-3411`
- Problem: kbInsert lets the text grow to max_len characters and then moves the terminator to buf[max_len], so the buffer must hold max_len+1 bytes. classicKeyboard forwards the classic keyboard() max unchanged; the classic keyboard capped text at min(max,33), so choose_custom_output_path sized name[36] for keyboard(name, 36), and from the dashboard (override installed for the whole dashboard, dash_exploit.c:159-160) typing to 36/36 writes name[36]. dash_settings.c passes sizeof(edit[96]), sizeof(buf[8]) for the SMB/NFS port and sizeof(buf[12]) for the NFS UID/GID: typing 8 port digits writes buf[8] on the stack.
- Fix: Pass max - 1 from classicKeyboard to dashModalKeyboard, and pass sizeof(...) - 1 at dash_settings.c:693, 3381 and 3411, as every other dashModalKeyboard caller already does.
- Benefit: Closes a reachable one-byte stack overwrite in four keyboard prompts (exploit installer output name, SMB/NFS share fields, port, UID/GID); four one-token edits, no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-059: Listing a missing folder or empty device root waits the whole 2-3 s retry budget every time (readGENERICWithFirstOpenRetry, ATA, MX4SIO)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/filer.c:345-381`; `Launchstation/src/filer.c:1960-1993`; `Launchstation/src/filer.c:2009-2041`; `Launchstation/src/filer.c:2090-2091`; `Launchstation/src/filer.c:2141-2188`; `Launchstation/src/filer.c:2206`; `Launchstation/src/filer.c:2216`; `Launchstation/src/dash_themes.c:965-968`; `Launchstation/src/dash_themes.c:993-995`; `Launchstation/src/dash_store.c:29-33`
- Problem: Every usb/mass (2000 ms), iLink (3000 ms) and CF (2000 ms) listing goes through readGENERICWithFirstOpenRetry, and listDir repeats the loop for ATA and MX4SIO (750 ms, 2000 ms at a root). The retry is not limited to a first open: a missing sub-folder fails Dopen on every pass, and an empty root opens but is_root forbids the early break, so both run the full budget, polling with 100 ms busy spins. Example: booted from USB without a THEMES folder, the awaited 'Looking for themes' job (dash_settings.c:2913-2922) blocks 2 s per USB theme root; the library scan's default mass:/APPS/ and mass:/BOOT/ cost 2 s each when missing.
- Fix: Retry only on the first open of a device unit per IOP session, keyed the same way as waitUsbTargetReady (filer.c:913-944), so later listings of a missing folder or empty root return at once; replace the three 100 ms spin loops with DelayThread(100000).
- Justification: The per-unit mask check adds about four lines but removes repeated multi-second modal stalls on common layouts (missing THEMES/APPS/BOOT folders, empty sticks); it reuses the mask pattern already in this file rather than adding a mechanism, and replacing the spins removes six lines.
- Benefit: Up to 2 s (iLink 3 s, ATA/MX4SIO 0.75 s) per listing of a missing folder or empty root; 2-4 s off the theme list when booted from USB (INFERRED from the loop budgets).
- Effort: M. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-060: JPEG and PNG box downscale do a 64-bit division per source pixel (software __udivdi3 on the R5900)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_jpeg.c:129-146`; `Launchstation/src/dash_png.c:155-176`; `Launchstation/src/dash_png.c:180-201`
- Problem: The inner loops compute dx = (u64)x * dw / sw for every source pixel. The R5900 has no 64-bit divide, so each one is a call to compiler-rt __udivdi3, which is linked into the ELF. The viewer decodes on the UI thread (dash_image.c:108-119) and art and theme images decode on the worker (dash_art.c:343, 513), so a large image (up to 2048x2048 PNG, or a JPEG after the 1/8 prescale) costs millions of library divides.
- Fix: Step dx with an accumulator instead of dividing (add dw per pixel, advance dx when it reaches sw; dw <= sw so at most one step), or at least drop the u64 casts in these per-pixel and per-row lines: the products stay below 2^23 (x < 8192, dw <= 640), so 32-bit hardware DIVU is exact.
- Benefit: INFERRED: about 4.2M __udivdi3 calls for a 2048x2048 PNG (roughly 50-100 cycles each, 0.7-1.4 s at 295 MHz); the accumulator removes them, which shortens the frozen viewer open and art/theme jobs. No source lines added with the cast variant.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-061: dashApps always reserves 512 x 828 B (414 KB) of heap for the library, whatever its size

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_lib.c:492-499`; `Launchstation/src/dash_lib.c:583-601`; `Launchstation/src/dash_lib.c:909-934`
- Problem: dashLibInit mallocs DASH_MAX_APPS (512) DashApp entries of 828 B each and keeps them for the whole session; dashLibApplyScan then memcpys the scan result (itself a full 512-entry buffer) into that array and frees the result. A typical library of a few dozen apps uses a small part of the 423,936 B block.
- Fix: After dashLibLoadCache has parsed the cache, shrink dashApps with realloc to max(dashAppCount, 1). In dashLibApplyScan, free the old array and adopt j->sc.fresh (realloc'd to nfresh) instead of memcpy, then clear j->sc.fresh before scanFree.
- Benefit: INFERRED: (512 - n) x 828 B of heap returned, e.g. about 382 KB with 50 apps; the 414 KB memcpy on every applied scan goes away. Peak heap during a scan is unchanged (the scan still allocates its 512-entry buffer).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-062: dashToolsPrepareMmceStatus can reboot the IOP on the UI thread while another worker job is running

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_tools.c:495-503`; `Launchstation/src/dash_tools.c:624-631`; `Launchstation/src/init.c:2369-2378`; `Launchstation/src/init.c:2266-2279`; `Launchstation/src/init.c:2163-2171`; `Launchstation/src/dash_worker.c:15-17`; `Launchstation/src/dash_settings.c:934-935`
- Problem: Opening the MMCE backups page with a backup path set and MMCE not loaded waits only for its own status job (dash_tools.c:496), then calls loadMmceModules (502). If MX4SIO owns the SIO2 stack, loadMmceModules -> switchStorageDriverStack -> resetRuntimeDeviceState reboots the IOP (init.c:2272-2274); resets are refused only on the worker thread (init.c:2168) and the park hook ignores UI-thread resets (dash_worker.c:155-156). A queued or running save/art/listing job then loses the IOP mid-RPC. dash_worker.c:15-17 requires waiting for the whole queue first, and dashToolsSyncAllMmceCards does exactly that (624) before the same load (631).
- Fix: Drop the mmce_status_job condition so line 496 always calls dashWorkerAwaitAll; it returns immediately when nothing is queued (dash_worker.c:342-343).
- Benefit: Closes an IOP reboot under a running worker job (hung or failed RPC, possibly a DASHBOARD.CNF write left truncated after its O_TRUNC open, dash_store.c:157-160; INFERRED). No lines added.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-063: qrcodegen.c is built with its 53 assert() calls live (no NDEBUG), pulling __assert_func into the ELF

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/qrcodegen.c:24`; `Launchstation/src/qrcodegen.c:209-211`; `Launchstation/src/qrcodegen.c:772-791`; `Launchstation/ps2.yaml:614-630`
- Problem: The wLaunchELF target defines no NDEBUG (ps2.yaml:614-630), so every assert in the vendored QR library is compiled, including the ones in getModuleBounded/setModuleBounded that -O2 inlines into the module loops (qrcodegen_encodeSegmentsAdvanced is 9508 bytes). qrcodegen.o is the only object that pulls libc assert.o (__assert_func), which in turn pulls fiprintf. qrcodegen.c is the only file under src/ or ee_libs/ that uses assert.
- Fix: Add NDEBUG to the wLaunchELF defines in ps2.yaml (no other src/ or ee_libs/ file uses assert), or put #define NDEBUG before #include <assert.h> in qrcodegen.c. Leave the library code itself unchanged.
- Benefit: MEASURED: qrcodegen.o .text 19,492 B, libc assert.o 100 B pulled only by it. INFERRED: removing ~53 assert sites (compare/branch/4-arg call, plus expression strings), several duplicated by inlining, saves an estimated 2-5 KB.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-064: Exploit install deletes or creates the update folders before the first MagicGate bind, so a bind failure loses the old install

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/main_exploit_installer.c:1963-1969`; `Launchstation/src/main_exploit_installer.c:1642-1669`; `Launchstation/src/main_exploit_installer.c:1887-1903`; `Launchstation/src/main_exploit_installer.c:1565-1577`; `Launchstation/src/main_exploit_installer.c:1595-1604`; `Launchstation/src/main_exploit_installer.c:1320-1356`
- Problem: install_selected_ps2_updates calls prepare_needed_folders (1967), which deletes the existing B?EXEC-SYSTEM folders when the user chose Delete (1650-1654) and creates empty folders (1666), before any payload is bound to the card. The first SecrDownloadFile bind comes later, in install_system_xlf_targets (1901) or bind_custom_payload_to_memory_card (1575, 1602). If that bind fails (bind_payload_to_memory_card returns -ENODEV/-EINVAL/-EIO, 1327-1352), the install returns with the old exploit folders gone and empty folders without icon.sys left on the card.
- Fix: Bind first, touch the card second. In the stock path sign the needed payloads into memory with sign_payload_to_memory before prepare_needed_folders (the custom path already pre-signs with prepare_custom_signed_payloads). In the custom path move the two bind_custom_payload_to_memory_card calls ahead of prepare_needed_folders. Binding works on an in-RAM buffer and does not use the card's file system, so only the order changes.
- Justification: Fixes a real data-loss path on the memory card. Nothing new is added; the stock path only holds its signed buffers (under 42 KB) until the writes finish instead of signing one at a time.
- Benefit: A failed MagicGate bind no longer deletes a working exploit install or leaves empty folders (shown as corrupted data by the browser, INFERRED). RAM: under 42 KB held briefly (SYSTEM.XLF 13896 + XSYSTEM 13896 + 2 x ~7 KB KERNEL, MEASURED from exploits/).
- Effort: M. Risk: High. Confidence: Med (INFERRED)

#### LAUNCHSTATION-065: configSysconfFileExistsOnPort probes mc1: without the mc1AccessRefused guard; the HDD header editor can trigger an IOP reset that evicts MX4SIO

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/config.c:229-243`; `Launchstation/src/config.c:258-259`; `Launchstation/src/config.c:311-314`; `Launchstation/src/hdd_header_injector.c:782-786`; `Launchstation/src/dash_hdd.c:956-966`; `Launchstation/src/filer_fileops.c:14-17`; `Launchstation/src/init.c:2238-2262`
- Problem: configGetPreferredSysconfPort always opens mcN:/SYS-CONF/<file> on both ports. genOpen of an mc1: path calls ensureMemoryCardPortAccessible(1), which resets the IOP when the MX4SIO stack owns slot 2. On the worker the reset is refused, but actSystemCnf runs EditHddPartitionHeaderSystemCnf on the UI thread, and its temp-path search calls configBuildSysconfPath, so editing an HDD partition SYSTEM.CNF with MX4SIO loaded resets the IOP mid-edit. init.c:2250-2254 states implicit config discovery must never do this; getIpConfig and configStampMcFolderIcon already guard mc1.
- Fix: In configSysconfFileExistsOnPort return 0 for port 1 when mc1AccessRefused(), like main_startup.c:104 and config.c:412 do.
- Justification: Adds one condition to an existing early return. It is the guard the codebase already uses at main_startup.c:104 and config.c:412 for the same rule, and it stops an unrequested IOP reset (driver teardown) during an HDD header edit.
- Benefit: Prevents an unrequested IOP reset and MX4SIO eviction on the UI thread; effect of that reset on the later raw-sector header write is INFERRED
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-066: Classic colour editor (colorEditor) loses every change: settingsDraw's colorField call resets classic_col each frame

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_settings.c:1442-1453`; `Launchstation/src/dash_settings.c:1492-1495`; `Launchstation/src/dash_settings.c:1536-1552`; `Launchstation/src/dash_settings.c:1574`; `Launchstation/src/dash_settings.c:3984-3986`; `Launchstation/src/dash_main.c:1210`
- Problem: colorEditor edits classic_col[i] through the pointer colorField returns, then calls dashRenderScene() every frame (1574). That runs settingsDraw (dash_main.c:1210), whose ST_COLOR rows call colorField again (3986), and its classic branch (1445-1452) rewrites classic_col[i] from the unchanged setting->color. Each Left/Right/swatch step is undone before the next frame, so on OK c equals orig and classicColorStore (1551-1552) never runs: the 8 classic colour rows (788-795) cannot be changed.
- Fix: In colorEditor, edit a local DashColor copy for classic colours (copy *c into a local and point c at it when classic is set) and pass that value to classicColorStore on OK instead of reading classic_col. Theme colours keep editing dashTheme directly. Adjust tests/test_classic_settings_persistence.py if classicColorStore takes the colour as an argument.
- Justification: Adds about two lines (the local copy and the pointer switch). settingsDraw has to keep refreshing classic_col to show the current values, so the editor needs its own copy; this is smaller than tracking which row is being edited.
- Benefit: Makes the 8 classic colour settings editable from Settings again; adds about 2 lines (INFERRED from the cited code path, not run on hardware).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-067: dashLaunchApp rewrites DASHAPPS.CNF before every launch even when the Recent list did not change (or Recent is off)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_main.c:649-655`; `Launchstation/src/dash_store.c:853-860`; `Launchstation/src/dash_main.c:263-264`
- Problem: dashLaunchApp calls dashStorePushRecent unconditionally, and dashStorePushRecent marks the lists dirty even when the path is already recent[0]. settleBackgroundWork (dashStoreFlushWait) then writes the whole DASHAPPS.CNF and reads it back (dashWriteFile, dash_store.c:148-175) and the launch waits for it. Relaunching the top app, the common case, costs a store-dir (often memory card) write each time. It also records Recent when dashCfg.recent_count is 0 (Recent off), which dashLaunchHook (:263) honours.
- Fix: In dashStorePushRecent return before dashStoreSaveLists when idx is 0 (already first). In dashLaunchApp call dashStorePushRecent only when dashCfg.recent_count > 0, as dashLaunchHook does.
- Justification: Adds one early return and one condition. Without them every relaunch of the most recent app waits on a full DASHAPPS.CNF write plus read-back on the store device (mc0:/SYS-CONF or beside the ELF). Two conditions are the smallest way to skip that write.
- Benefit: One DASHAPPS.CNF write + verify read and the launch wait for it saved per launch of the top Recent app or with Recent set to 0 (INFERRED from the call path; time not measured).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-068: opBackupActiveMmce compares the new backup with the previous one on the UI thread after the progress overlay is closed

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_browser.c:1587-1610`; `Launchstation/src/dash_browser.c:1400-1447`; `Launchstation/src/mmce_backup.c:565-577`
- Problem: After mcCardBackupActiveMmce has written and verified the image, dashLoadingEnd() is called (1589) and only then activeHistoryFilesEqual reads the whole new backup and the previous history image in 16 KB reads (2 x 8 MB for a standard card), with no dashLoadingStatus/frame in the loop. On a USB destination this is several seconds with the screen frozen on the finished overlay and input ignored; when the card is unchanged the just-written 8 MB file is then deleted. The selected-card path in mmce_backup.c:565-577 compares the source with the newest history before writing anything.
- Fix: Smallest: move dashLoadingEnd() below the compare and call dashLoadingStatus("Comparing with previous backup", done, total) once per chunk in activeHistoryFilesEqual so frames keep drawing. Optional, saves the write: compare the source with the newest history first, as mmce_backup.c does; needs hardware testing because mcCardBackupActiveMmce keeps the synthetic source open on purpose (mc_card_export.c:250-253).
- Justification: Adds about two lines (the status call and its counters); removes a multi-second unresponsive period on the UI thread, which the target rules forbid.
- Benefit: INFERRED: ~16 MB read with no frame per repeat backup (seconds on USB 1.1) becomes a progress bar; the optional reorder also saves an 8 MB write + 16 MB verify read when the card is unchanged.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-069: mcDir: a 128 KB libmc directory table is reserved in .bss for the whole run but used only during readMC/readXFROM

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/filer.c:486-488`; `Launchstation/src/filer.c:507-541`; `Launchstation/src/filer.c:580-596`
- Problem: static sceMcTblGetDir mcDir[MAX_ENTRY + 1] (2049 x 64 B = 131,136 B) receives mcGetDir/xfromGetDir results and is copied entry by entry into the caller's FILEINFO array in the same function; nothing reads it afterwards, yet it stays reserved for the whole session, including while copy, cloud TLS or image code needs heap.
- Fix: Allocate the table with memalign(64, limit * sizeof(sceMcTblGetDir)) at the start of readMC/readXFROM, free it after the copy loop (a failed allocation is handled like a failed read), and delete the static table; update tests/test_listing_complete.py:248 and tests/test_mc_detect_retry.py:16, which match the declaration text.
- Justification: Adds an allocate/free pair (about six lines) to return 128 KB of permanently reserved RAM to the heap outside memory-card listings; memalign keeps the 64-byte alignment the SIF transfer needs. Peak memory during an MC listing is unchanged, which is why this is rated below the RAM threshold.
- Benefit: 131,136 bytes of .bss no longer reserved outside MC/XFROM listings (MEASURED: BUILD-EVIDENCE.md:83, 131136 .bss mcDir); peak during a listing unchanged.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-070: Card contents keeps 82 KB of list buffers (ct_saves, ct_titles, ct_details, ct_items) in .bss for the whole run

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_card_tool.c:45-48`; `Launchstation/src/dash_card_tool.c:451`; `Launchstation/src/dash_card_tool.c:1486`; `Launchstation/src/dash_card_tool.c:1577-1608`; `Launchstation/src/dash_card_tool.c:988`; `Launchstation/src/dash_card_tool.c:1217`; `Launchstation/src/dash_card_tool.c:1404`
- Problem: ct_saves (30,000 B), ct_titles (24,000 B), ct_details (24,000 B) and ct_items (4,048 B) are static, but ct_titles/ct_details/ct_items are used only inside dashCardTool (1577-1622) and ct_saves only while a card dialog runs; ctDropSave (451) and dashCardNewBlank (1486) fill all 250 entries just to read the count. The sibling views already malloc their lists per call (2195-2206, 2729-2736). ctResize, ctSplitGameId and dashCardNewBlank each hold their own static sb[512]/root[1024] pair although they never run together.
- Fix: Allocate the title/detail/item arrays (and the saves list) in dashCardTool on entry and free them on return, as dashCardAllSaves does; call mcViewListSaves with max 0 and NULL in ctDropSave and dashCardNewBlank (allowed, mc_card_view.c:327); give ctResize/ctSplitGameId/ctGameCard a heap list; keep one file-scope sb/root pair.
- Justification: Replaces static arrays with per-call allocation. The concrete problem is 82 KB of EE RAM reserved for the whole run for a rarely opened dialog; the two sibling views in the same file already work this way, so it adds only the malloc/free lines (about +10 net).
- Benefit: 82,048 B of .bss returned to the heap while no card dialog is open, plus 3,072 B from sharing sb/root (MEASURED, raw/sizes/wLaunchELF-unstripped-all-symbols-by-size.tsv); peak use while the dialog is open is unchanged.
- Effort: M. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-071: Google Drive link/sync/browse/recover and 'back up all MMCE cards' block the UI thread; frame redrawn only once per file

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_tools.c:228-230`; `Launchstation/src/dash_tools.c:302-304`; `Launchstation/src/dash_tools.c:360-363`; `Launchstation/src/dash_tools.c:638-651`; `Launchstation/src/dash_tools.c:676-679`; `Launchstation/src/dash_tools.c:1218-1221`; `Launchstation/src/dash_tools.c:1305-1308`; `Launchstation/src/dash_tools.c:1328-1331`; `Launchstation/src/dash_tools.c:883-921`; `Launchstation/src/cloud_google.c:1525-1567`; `Launchstation/src/cloud_http.c:805-815`; `Launchstation/src/mmce_backup.c:773-781`
- Problem: These paths call blocking card and HTTPS work directly on the UI thread behind dashLoadingBegin. The overlay is redrawn only when a progress callback fires: once per file before hashing, validating and uploading it (cloud_google.c:1529-1561) and once per card before a full-verify backup (mmce_backup.c:774-781); uploads (cloud_http.c:805-815) and Drive recovery downloads report nothing, and each cloudLink poll is a full TLS round trip (304). The screen shows a frozen frame with a stopped spinner and ignores input for the whole of each step. dashToolsCheckUpdates already runs the same kind of HTTPS on the worker through updateRun (883-921) so frames keep presenting.
- Fix: Route these calls through the existing worker path used by updateRun (dashWorkerSubmit plus dashWorkerAwait(id, title, 0)). Prepare the network and device stacks on the UI thread first (ensureDashboardIpNetworkReady, ensurePathDeviceStackReady, loadMmceModules), as dashToolsCheckUpdates does at 947, because IOP resets are refused on the worker (init.c:2168). dashLoadingStatus already forwards worker-side progress to dashWorkerStatus (dash_ui.c:1408-1411), so the existing callbacks keep working.
- Justification: Each call site needs a small argument struct and run function (about 20 lines in total, INFERRED), but it removes multi-second frozen frames from user-started backups, uploads and downloads, and it reuses the worker and await code that already exists instead of adding a new mechanism.
- Benefit: Frames and the spinner keep running during uploads, downloads and card backups. Frozen time per 8 MB card INFERRED at several seconds (SHA-256 plus validation read plus TLS upload on the EE). About -20 lines net (INFERRED).
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-072: Development-only #ifdef blocks that no build defines: DASH_AUTOTEST, DASH_THEME_TOUR, WLE_BOOT_DIAGNOSTICS

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_main.c:1304-1323`; `Launchstation/src/dash_main.c:1492-1506`; `Launchstation/src/dash_main.c:1354-1356`; `Launchstation/src/dash_main.c:1368-1370`; `Launchstation/src/pad.c:284-307`; `Launchstation/src/pad.c:313-325`; `Launchstation/include/dash_autotest.h`; `Launchstation/src/main_modules.c:32-38`; `Launchstation/src/main_modules.c:51-57`
- Problem: None of the three macros is set by ps2.yaml or configure_ds34.py; they guard a demo library and button-replay script (DASH_AUTOTEST), a theme slideshow (DASH_THEME_TOUR) and six 'Boot check N' drawMsg prints (WLE_BOOT_DIAGNOSTICS). docs/dashboard.md:601-604 and docs/themes.md:436 describe the first two as dev aids to enable by hand.
- Fix: Delete the blocks and include/dash_autotest.h, plus the doc paragraphs, unless the owner still uses these emulator aids; the boot-check prints at least are leftover debug output.
- Benefit: ~115 source lines removed; no ELF change since the code is never compiled (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-073: 18 swapKeys if/else pairs differ only in the button glyph digit

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/config.c:1728-1767`; `Launchstation/src/config_advanced.c:286-315`; `Launchstation/src/config_startup.c:224-233`; `Launchstation/src/config_startup.c:307-350`; `Launchstation/src/config_network.c:319-348`; `Launchstation/src/config_screen.c:317-347`
- Problem: Every tooltip is written twice, once with '\xFF1'/'\xFF0' and once with them swapped, depending on swapKeys. gui_neutrino.c:497 already shows the shorter form (swapKeys ? '1' : '0' passed to one format). Moot if finding 1 is applied.
- Fix: Compute the OK and cancel glyph characters once per draw and use a single sprintf with %c for each tooltip.
- Benefit: ~90 lines removed (18 pairs x ~6 lines, minus ~2 lines per file for the glyph variables, INFERRED); small .rodata saving from fewer format strings.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-074: Feature #ifdefs in dash_devices.c/dash_files.c test macros every build always defines

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_devices.c:16-18`; `Launchstation/src/dash_devices.c:44-48`; `Launchstation/src/dash_devices.c:212-218`; `Launchstation/src/dash_devices.c:389-439`; `Launchstation/src/dash_devices.c:446-484`; `Launchstation/src/dash_devices.c:533-548`; `Launchstation/src/dash_devices.c:608-666`; `Launchstation/src/dash_devices.c:681-684`; `Launchstation/src/dash_devices.c:704-718`; `Launchstation/src/dash_devices.c:744-764`; `Launchstation/src/dash_devices.c:811-848`; `Launchstation/src/dash_devices.c:865-884`; `Launchstation/src/dash_devices.c:899-904`; `Launchstation/src/dash_files.c:43-52`; `Launchstation/src/dash_files.c:450-452`; `Launchstation/ps2.yaml:614-630`
- Problem: MMCE, MX4SIO, ILINK, XFROM, DVRP, EXFAT, UDPFS, SMB and ETH are unconditionally defined for the only EE target (ps2.yaml:618-629), and the DS34 variant only adds DS34 (scripts/configure_ds34.py:36), so 82 directive lines in dash_devices.c and 4 in dash_files.c guard code that is always compiled. Unlike the code elsewhere, dashBootDeviceBit (lines 909-916) already uses ata/mmce/mx4sio/ilink without guards, so the files are not even consistent.
- Fix: Delete the #ifdef/#endif lines for these macros in both files (keep the guarded code). If the owner wants the macros gone repo-wide, do it in one pass with the other dash_* files. Host tests that paste these functions (tests/test_device_input.py:210-213 defines only MMCE, MX4SIO, EXFAT, UDPFS) need the extra stubs for SMB/ILINK paths.
- Benefit: 86 preprocessor lines removed (MEASURED: Grep count of ^#(if|ifdef|ifndef|else|endif) = 82 in dash_devices.c, 4 in dash_files.c); no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-075: DASH_AUTOTEST development hook (include/dash_autotest.h and its users) is never built and its script is stale

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/dash_autotest.h:1-27`; `Launchstation/src/pad.c:284-307`; `Launchstation/src/pad.c:313-325`; `Launchstation/src/dash_main.c:1304-1323`; `Launchstation/docs/dashboard.md:601-602`
- Problem: No build defines DASH_AUTOTEST (not in ps2.yaml or configure_ds34.py). The header is included only under that macro at pad.c:289, and dash_main.c:1304-1323 injects a fake demo library under the same macro. The script targets screens that no longer exist as tabs: an APPS grid and a DEVICES tab, while dash_main.c:1325-1330 now folds Apps into Home and Devices into Files.
- Fix: Delete include/dash_autotest.h, the two #ifdef DASH_AUTOTEST blocks in pad.c, the demo-library block in dash_main.c and the paragraph in docs/dashboard.md (and the matching docs-site developers text).
- Benefit: ~85 source lines removed; no ELF change (INFERRED: never compiled).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-076: LIBCDVD_LEGACY is never defined: cdvd_macro.h legacy branch, include/snippets/readCD.h and two LCDVD no-op macros are dead

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/cdvd_macro.h:1-16`; `Launchstation/include/snippets/readCD.h`; `Launchstation/include/launchelf.h:66-78`; `Launchstation/include/launchelf.h:88`; `Launchstation/src/filer.c:604`; `Launchstation/src/filer.c:656-658`; `Launchstation/src/filer.c:962-963`; `Launchstation/src/init.c:1334`; `Launchstation/src/init.c:1458-1459`; `Launchstation/src/main_actions.c:812-813`; `Launchstation/ps2.yaml:617`
- Problem: No build file or source defines LIBCDVD_LEGACY. ps2.yaml:617 defines LIBCDVD_LATEST instead, and nothing tests that. So the CDVD_* branch of cdvd_macro.h, the cdvd_rpc.h branch in launchelf.h and the 51-line legacy readCD() in include/snippets/readCD.h (included only under the #else at filer.c:656-658) never compile. In the live branch LCDVD_FLUSHCACHE() and LCDVD_INIT() expand to nothing, so the calls at filer.c:962, init.c:1334, init.c:1458 and main_actions.c:812 are no-ops. The two remaining macros only rename sceCdStop/sceCdDiskReady.
- Fix: Delete include/cdvd_macro.h and include/snippets/readCD.h. Call sceCdStop()/sceCdDiskReady(0) directly at the four LCDVD_STOP/LCDVD_DISKREADY sites and delete the LCDVD_FLUSHCACHE/LCDVD_INIT lines. Drop the LIBCDVD_LEGACY #ifdef/#else lines in launchelf.h and filer.c, and the unused LIBCDVD_LATEST define. Update the stub macros in tests/test_launch_driver_messages.py:213-214.
- Benefit: ~80 lines removed (16 + 51 + ~13 conditional/no-op lines); no ELF change (INFERRED: the dead branches are preprocessed out).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-077: dashThemeFolderLoad and dashThemeFolderExport are dead copies of the worker-split FolderRead/FolderText/FolderWrite

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_themes.c:1122-1159`; `Launchstation/src/dash_themes.c:1161-1195`; `Launchstation/include/dash_internal.h:276-277`; `Launchstation/src/dash_themes.c:1046-1120`
- Problem: dashThemeFolderLoad repeats dashThemeFolderRead line for line (plus apply), and dashThemeFolderExport repeats dashThemeFolderText + dashThemeFolderWrite. Neither has a caller: the only repo matches are their definitions, the prototypes at dash_internal.h:276-277 and old plan/spec docs. dash_settings.c uses only the split versions (2895, 2897, 2952).
- Fix: Delete dashThemeFolderLoad, dashThemeFolderExport and their two prototypes. Optionally drop the stale names from docs/superpowers specs/plans.
- Benefit: ~77 source lines removed. No ELF change: both are already discarded by --gc-sections (MEASURED: evidence/Launchstation/raw/link-raw/wLaunchELF.gc-sections.log:556-557; 956 + 384 bytes of .text never linked).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-078: neutrino_gameid.c re-implements the ISO9660 SYSTEM.CNF reader that popstarter_vcd.c already provides

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/neutrino_gameid.c:20-109`; `Launchstation/src/popstarter_vcd.c:3-9`; `Launchstation/src/popstarter_vcd.c:63-103`; `Launchstation/src/popstarter_vcd.c:131-149`; `Launchstation/src/popstarter_vcd.c:182-244`
- Problem: neutrino_gameid.c has its own isoReadLe32 (=readIsoLe32), readIsoBytes (the same read-until-done loop as readPs1IsoSector:76-80), isoTextMatches (=isoNameMatches) and readIsoSystemCnf (PVD search, root directory walk, SYSTEM.CNF lookup), the same algorithm as readPs1IsoPrimaryVolumeDescriptor + readPs1SystemCnf, which already accept a cooked 2048-byte ISO through PS1_IMAGE_READER {fd, 0, 2048}. neutrino_gameid.c already includes popstarter_internal.h.
- Fix: Replace readIsoSystemCnf and its three helpers with readPs1SystemCnf on a PS1_IMAGE_READER {fd, 0, 2048}. Move the stricter checks neutrino needs (file-size bound, directory-record sanity) into readPs1SystemCnf so both callers get them, and keep parseIsoGameID.
- Benefit: ~75 lines removed from neutrino_gameid.c (lines 20-109 minus a 3-line call), a few lines added to the shared reader; one ISO directory parser to maintain (INFERRED).
- Effort: M. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-079: Commented-out WriteFont_C and stale banners in draw.c and launchelf.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/draw.c:339-399`; `Launchstation/include/launchelf.h:507-508`; `Launchstation/src/main_info_screens.c:302-303`; `Launchstation/include/launchelf.h:38`; `Launchstation/include/launchelf.h:747`
- Problem: WriteFont_C is a 54-line commented-out font dumper with a commented prototype and a commented call; draw.c:339-341 is a banner for transcpy_sjis, which now lives in dash_font.c. launchelf.h:38 is a commented SIO_DEBUG define, and launchelf.h:747 names a nonexistent chkesr_rpc.c (the file is src/chkesr.c).
- Fix: Delete the commented WriteFont_C block, its commented prototype and call, the orphan transcpy_sjis banner and the commented SIO_DEBUG line; fix the chkesr comment or drop it.
- Benefit: ~66 comment lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-080: reopenPadPorts is never called in either pad variant; pad_reopen_active and its waitPadReady deadline branch are dead with it

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/pad.c:427-452`; `Launchstation/src/pad.c:15`; `Launchstation/src/pad.c:355`; `Launchstation/src/pad.c:360-364`; `Launchstation/src/pad_ds34.c:486-515`; `Launchstation/include/launchelf.h:527`
- Problem: reopenPadPorts is defined in pad.c and pad_ds34.c and declared in launchelf.h, but nothing calls it (USB-STABILITY-AUDIT-2026-10-08.md:37 already records the removal of its caller). pad_reopen_active is set only inside it, so the deadline/DelayThread branch of waitPadReady can never run.
- Fix: Delete both reopenPadPorts definitions and the prototype, delete pad_reopen_active and the deadline branch in waitPadReady, and drop the matching cases in tests/test_device_input.py and the stub in tests/test_usb_storage_lifecycle.py.
- Benefit: ~63 source lines removed. MEASURED: .text.reopenPadPorts (252 B) already gc'd (wLaunchELF-gc-discarded-first-party.tsv:69); waitPadReady loses a never-taken branch (a few bytes, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-081: LIBCDVD_LEGACY is never defined: include/snippets/readCD.h and the legacy CDVD branches are dead

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/snippets/readCD.h`; `Launchstation/src/filer.c:604`; `Launchstation/src/filer.c:656-658`; `Launchstation/include/launchelf.h:66-68`; `Launchstation/include/launchelf.h:78`; `Launchstation/include/cdvd_macro.h:4-9`
- Problem: No build defines LIBCDVD_LEGACY (ps2.yaml defines LIBCDVD_LATEST, which nothing tests). The legacy readCD() in snippets/readCD.h is only included from filer.c's #else branch, and the legacy cdvd_rpc.h include and CDVD_* macro set are likewise never selected.
- Fix: Delete include/snippets/readCD.h (and the now empty snippets folder), the #ifndef/#else/#endif around readCD in filer.c, the LIBCDVD_LEGACY include branch in launchelf.h and the legacy half of cdvd_macro.h; drop the unused LIBCDVD_LATEST define from ps2.yaml.
- Benefit: ~62 source lines and one file removed; no ELF change (preprocessed out) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-082: LIBCDVD_LEGACY readCD path and include/snippets/readCD.h are never compiled

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer.c:604`; `Launchstation/src/filer.c:656-658`; `Launchstation/include/snippets/readCD.h`; `Launchstation/include/launchelf.h:66-68`; `Launchstation/include/cdvd_macro.h:4-9`
- Problem: No build defines LIBCDVD_LEGACY (ps2.yaml defines LIBCDVD_LATEST at 617; configure_ds34.py adds no CDVD define), so the #else branch at filer.c:656-658 that includes include/snippets/readCD.h (51 lines, the old CDVD_GetDir reader) is never built. snippets/readCD.h has no other includer.
- Fix: Delete the #ifndef/#else/#include/#endif lines around readCD in filer.c, delete include/snippets/readCD.h, and drop the LIBCDVD_LEGACY branches in launchelf.h:66-68 and cdvd_macro.h:4-9.
- Benefit: ~62 source lines and one header file removed; no ELF change (the code is never compiled).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-083: load_ps2atad_stack, have_ps2atad and the ps2atad_irx embed are dead (function marked unused, no caller)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/init.c:748-802`; `Launchstation/src/init.c:386`; `Launchstation/src/init.c:93`; `Launchstation/src/init.c:215`; `Launchstation/src/init.c:549`; `Launchstation/src/init.c:3034`; `Launchstation/ps2.yaml:683`
- Problem: load_ps2atad_stack is declared and defined with __attribute__((unused)) and nothing calls it; have_ps2atad is set only inside it, so the test at line 549 and the reset at 3034 only ever see 0. Its ps2atad_irx import is the only user of the 'atad:ps2atad' embed, which the linker already garbage-collects. The function also carries a second copy of the hddarg/pfsarg tables from load_ps2hdd_stack (lines 752-769 vs 687-705, the duplicate-block lead).
- Fix: Delete load_ps2atad_stack and its prototype, the ps2atad_irx import, the have_ps2atad flag and its two uses, and the 'atad:ps2atad' line in the ps2.yaml embed list.
- Benefit: ~62 lines removed; no shipped-size change: BUILD-EVIDENCE.md says ps2atad_irx (11,477-byte payload) is already garbage-collected (MEASURED); one fewer embed step per build
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-084: Commented-out WriteFont_C font dumper in draw.c and its leftovers

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/draw.c:343-399`; `Launchstation/include/launchelf.h:507-508`; `Launchstation/src/main_info_screens.c:303`
- Problem: WriteFont_C (a developer tool that dumps font_uLE as C source) exists only inside a /* */ block in draw.c, with a commented prototype in launchelf.h and a commented call in main_info_screens.c. No build can enable it without editing three files.
- Fix: Delete the commented block, its banner, the commented prototype and the commented call.
- Benefit: ~60 comment lines removed; no binary change (INFERRED: code is commented out).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-085: DVRP partition list and folder reader are copies of the HDD versions (setDVRPPartyList, readHDDDVRP)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer.c:726-773`; `Launchstation/src/filer.c:834-876`; `Launchstation/src/filer.c:1210-1246`; `Launchstation/src/filer.c:1281-1316`
- Problem: setDVRPPartyList (834-876) repeats the Dopen/Dread partition loop of setPartyListForDevice (737-772) line for line except the target array and counter. The Dopen/Dread/fill loop of readHDDDVRP (1281-1316) is identical to readHDD (1210-1246) apart from one comment line; both start after mounting the partition and fill info the same way.
- Fix: Give setPartyListForDevice's loop a static helper that takes the device name, the parties array and the count pointer, and call it for hdd0/hdd1 and dvr_hdd0. Move the Dopen..return i part of readHDD into a static helper taking the mounted dir and call it from both readHDD and readHDDDVRP.
- Justification: Two helpers replace four near-identical blocks; the DVRP copies are compiled into the shipped ELF (DVRP is on, ps2.yaml:619), so this is a net source reduction of about 60 lines.
- Benefit: ~60 source lines removed (two 36-line copies and two ~30-line copies collapse to one each, plus helper headers); ELF effect INFERRED small (a few hundred bytes of .text).
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-086: Modal dashFileBrowser() and browserScene() are never called; modal-only branches remain in the tab code

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_browser.c:86-87`; `Launchstation/src/dash_browser.c:3258-3263`; `Launchstation/src/dash_browser.c:3286-3298`; `Launchstation/src/dash_browser.c:3390-3391`; `Launchstation/src/dash_browser.c:3405-3414`; `Launchstation/src/dash_browser.c:3481-3487`; `Launchstation/src/dash_browser.c:3501-3505`; `Launchstation/src/dash_browser.c:3519-3525`; `Launchstation/src/dash_browser.c:3550-3553`; `Launchstation/src/dash_browser.c:3628-3662`; `Launchstation/include/dash_internal.h:1112`
- Problem: dashFileBrowser has no caller (only its definition and the prototype at dash_internal.h:1112) and the linker discards it and browserScene. Since browserDrawBody is then only reached through browserTabDraw with fb_tab_active == 1, every !fb_tab_active branch is unreachable, and browserProcessInput is only called with running == NULL, so every running branch is dead. The comment at 86-87 still says the modal browser serves Tools / launch-key callers.
- Fix: Delete dashFileBrowser, browserScene and the prototype; drop the running parameter (fold browserProcessInput into browserTabInput) and the fb_tab_active ternaries/conditions in browserDrawBody; fix the comment at 86-87. tests/test_files_tab_lifecycle.py:28-32 assert on the modal code and must be updated.
- Benefit: ~60 lines removed; dashFileBrowser (496 B) and browserScene (60 B) are already discarded by --gc-sections (MEASURED, raw/link/wLaunchELF-gc-discarded-first-party.tsv), the dead branches in live functions are a few hundred bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-087: dash_icons.c: dashHeaderIcon, dashIconsShutdown, dashIconsTexture are never called; the header-icon atlas parts exist only for dashHeaderIcon

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_icons.c:924-954`; `Launchstation/src/dash_icons.c:759-776`; `Launchstation/src/dash_icons.c:835`; `Launchstation/src/dash_icons.c:859-867`; `Launchstation/src/dash_icons.c:17-22`; `Launchstation/include/dash_internal.h:511`; `Launchstation/include/dash_internal.h:514`
- Problem: dashHeaderIcon (939) has no caller and no prototype; dashIconsShutdown (859) and dashIconsTexture (864) appear only in their prototypes. drawHeaderIconPart (925) is called only by dashHeaderIcon, and initHeaderIconParts (761, called at 835) rasterises the two 32px header parts that only drawHeaderIconPart samples, so that atlas space and startup work serve dead code.
- Fix: Delete dashHeaderIcon, drawHeaderIconPart, initHeaderIconParts and its call, dashIconsShutdown, dashIconsTexture and the two prototypes. Optionally set CLIP_ICON_Y to 0 so the margin icons start at the top of the freed column, and update the comment at 17-18.
- Benefit: ~60 lines removed. dashHeaderIcon 600 B, dashIconsShutdown 32 B, dashIconsTexture 28 B are already gc-discarded (MEASURED, link/wLaunchELF-gc-discarded-first-party.tsv); initHeaderIconParts is static and still linked into dashIconsInit (INFERRED a few hundred bytes) plus 2048 SDF evaluations at startup.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-088: mmce_backup.c: three copies of the MemoryCards tree walker and three copies of the history-root setup

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mmce_backup.c:686-717`; `Launchstation/src/mmce_backup.c:742-786`; `Launchstation/src/mmce_backup.c:920-963`; `Launchstation/src/mmce_backup.c:795-819`; `Launchstation/src/mmce_backup.c:972-996`; `Launchstation/src/mmce_backup.c:885-891`
- Problem: process_tree and scan_tree are the same 44-line recursive walker (skip . and .., join_path, recurse on dirs, count and report progress on *.MCD, strip source_root) differing only in the leaf call and the stats type; count_candidates repeats the same loop again. mmceBackupAllCards and mmceBackupScanCards share 25 identical lines of argument checks and history_root construction (only ensure_dir_tree differs), and mmceBackupCardPath rebuilds history_root the same way.
- Fix: Keep one walker that takes a leaf callback and a context (the counting pass becomes a callback that increments), and one static helper that validates options and builds <dest>/WLE-MMCE-Backups/mmceN; delete scan_tree and the repeated setup blocks. Update tests/test_mmce_backup.py, which slices the source text by function names.
- Benefit: about 60 source lines net (44-line scan_tree and ~20 lines of count_candidates removed, ~22 duplicated setup lines folded, ~25 lines of new walker/helper glue); small code size gain INFERRED
- Effort: M. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-089: Eight SetIds with no Settings row (S_LEGACY_THEME, S_ENABLED, S_FILES_MODE, S_LAUNCHKEYS, S_IGR_*, S_SMB_SHARES, S_T_CUSTOMIZE) and their cases

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_settings.c:44`; `Launchstation/src/dash_settings.c:52`; `Launchstation/src/dash_settings.c:59`; `Launchstation/src/dash_settings.c:79`; `Launchstation/src/dash_settings.c:118-120`; `Launchstation/src/dash_settings.c:133`; `Launchstation/src/dash_settings.c:159`; `Launchstation/src/dash_settings.c:1018-1023`; `Launchstation/src/dash_settings.c:1071-1075`; `Launchstation/src/dash_settings.c:1169-1176`; `Launchstation/src/dash_settings.c:1697-1703`; `Launchstation/src/dash_settings.c:1776-1778`; `Launchstation/src/dash_settings.c:1843-1855`; `Launchstation/src/dash_settings.c:3092-3094`; `Launchstation/src/dash_settings.c:3300-3302`; `Launchstation/tests/test_igr_return_policy.py:24-25`; `Launchstation/tests/test_native_cloud.py:112-119`
- Problem: choiceInfo, setChoice, runAction and runThemeAction are static and are only called with ids from the SetItem tables (lines 234-873) or from the constant lists at 3550-3557 and 3693-3695. None of these eight ids appears in any table or list, so their cases never run. dash_store.c:309 and :325 also force enabled/legacy_theme to 1, so the S_ENABLED/S_LEGACY_THEME setters are obsolete; files_mode is toggled from dash_files.c:693 instead. The cases for S_LEGACY_THEME, S_ENABLED and S_FILES_MODE are dead.
- Fix: Delete the eight enum entries, the stale 'Legacy/internal split flags' comment, and the matching cases in choiceInfo, setChoice, runThemeAction and runAction. Update the two source-text tests: drop S_IGR_AUTO_BACKUP from the list in test_igr_return_policy.py, and point test_native_cloud.py at the live S_IGR_SYNC_MODE branch (1830-1834), which already checks cloudGoogleIsLinked().
- Benefit: ~56 source lines removed; switch cases in static functions are compiled in, so a few hundred bytes of .text are saved (INFERRED, not measured)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-090: SifIopRebootBufferEncrypted is unused, and the always-on F_* section guards and extinfo types in the vendored iopreboot copy are dead

- Category: 1 (Dead code)
- Location(s): `Launchstation/ee_libs/iopreboot/SifIopRebootBuffer.c:50-64`; `Launchstation/ee_libs/iopreboot/SifIopRebootBuffer.c:114`; `Launchstation/ee_libs/iopreboot/SifIopRebootBuffer.c:120-159`; `Launchstation/ee_libs/iopreboot/SifIopRebootBuffer.c:246`; `Launchstation/ps2.yaml:414-417`
- Problem: The only caller of the library is SifIopRebootBuffer at src/init.c:847. SifIopRebootBufferEncrypted (124-156) and SifLoadModuleEncryptedSpecial (120-121) have no caller and are gc-discarded (280 bytes). ps2.yaml:414-417 always defines all three F_* macros for this single translation unit, so the #ifdef guards are always true. extinfo_t and enum EXTINFO_TYPE (50-62) are referenced only in comments.
- Fix: Delete SifIopRebootBufferEncrypted, the encrypted load macro, extinfo_t/EXTINFO_TYPE, the three #ifdef F_* / #endif pairs and the defines block in ps2.yaml, and update the header comment at lines 4-7. Vendored from ps2sdk; this only trims the private copy.
- Benefit: ~55 lines removed (source and ps2.yaml); 280 bytes .text already gc-discarded (MEASURED, gc tsv), so no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-091: buildBootBlockPath is 53 lines of manual bounded concatenation equivalent to one snprintf

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_startup.c:465-520`; `Launchstation/src/main_startup.c:589`; `Launchstation/src/main_startup.c:28`
- Problem: buildBootBlockPath appends prefix, partition, an optional '/' and path_part with four copies of the same room/chunk/memcpy step and NULL defaults. Its only caller (589) passes non-NULL arguments into a MAX_PATH buffer; snprintf with "%s%s%s%s" and the conditional slash gives the same truncating result.
- Fix: Replace the call with snprintf(LaunchElfDir, sizeof(LaunchElfDir), "%s%s%s%s", hdd_prefix, temp, path_part[1] == '/' ? "" : "/", path_part + 1) and delete the function and its forward declaration.
- Benefit: ~55 lines removed; buildBootBlockPath is 300 B of .text (MEASURED, sizes tsv), replaced by one snprintf call (~40 B, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-092: main_title.c token template MENU_TITLE_FORMAT is a fixed string, so menuTitleUses* and the care_time/care_date plumbing are always true

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_title.c:11`; `Launchstation/src/main_title.c:51-79`; `Launchstation/src/main_title.c:348-382`; `Launchstation/src/main_title.c:591-678`
- Problem: MENU_TITLE_FORMAT is the compile-time constant "$TIME $DATE $TEMP", so hasToken, menuTitleUsesTime/Date/Clock/Temp/DynamicTokens always return 1. Every branch on them in menuTitleUpdateAsync, menuTitleFormatClock and menuTitleFormatTemperature is constant, and updateClock/refreshClockText/seedClockFromPs2 always get care_time = care_date = 1. appendChar/appendText only rebuild what one snprintf does.
- Fix: Delete MENU_TITLE_FORMAT, hasToken and the five menuTitleUses* helpers and their branches; drop the care_time/care_date parameters (refreshClockText returns whether either string changed); write menuTitleFormatClock and menuTitleFormatTemperature as single snprintf calls and delete appendChar/appendText (after finding 6 removes menuTitleFormat). Adjust tests/test_software_clock.py:75, which calls updateClock(tick, force, 1, 1).
- Benefit: ~55 lines removed (INFERRED from the cited ranges); the helpers are static and probably folded by -O2, so the ELF effect is small (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-093: main_title.c: menuTitleFormat and menuTitleGetClockText have no caller; clock prototypes are declared twice

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main_title.c:680-720`; `Launchstation/src/main_title.c:746-753`; `Launchstation/include/main_title.h:6`; `Launchstation/include/dash_internal.h:1161-1167`; `Launchstation/include/main_title.h:9`; `Launchstation/include/main_title.h:18-19`
- Problem: menuTitleFormat and menuTitleGetClockText are referenced only by their definitions and prototypes; the comment at 717-720 describes GetClockText, not the function below it. dash_internal.h:1161-1167 redeclares menuTitleGetTemperatureRaw, menuTitleFormatClockTime and menuTitleFormatClockDate, which main_title.h already declares, and is the only home of menuTitleGetClockFields.
- Fix: Delete menuTitleFormat, menuTitleGetClockText, their prototypes and the stale comment. Move the menuTitleGetClockFields prototype into main_title.h, delete the dash_internal.h block, and include main_title.h in dash_ui.c and dash_bg_ps2.c.
- Benefit: ~55 lines removed; no ELF change: both functions are already discarded by --gc-sections (660 B and 220 B, MEASURED in evidence/Launchstation/raw/link/wLaunchELF-gc-discarded-all.tsv).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-094: Cached card-image reader ctRead/ctOpen is a copy of mmce_backup.c fp_read/card_fingerprint

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_card_tool.c:35-43`; `Launchstation/src/dash_card_tool.c:50-94`; `Launchstation/src/dash_card_tool.c:119-145`; `Launchstation/src/mmce_backup.c:31-42`; `Launchstation/src/mmce_backup.c:204-247`; `Launchstation/src/mmce_backup.c:252-278`
- Problem: ctRead (45 lines) and mmce_backup.c fp_read (44 lines) are the same 4-way, 4 KB block cache line for line, differing only in names (CardIo/FpReader, CT_WAYS/MMCE_FP_WAYS, ct_cache/fp_cache); the struct and the open sequence (tags to -1, genOpen, size via lseek, fill McViewSource, mcViewOpen) are also repeated in ctOpen and card_fingerprint. Each copy keeps its own 16 KB static cache.
- Fix: Keep one reader (struct, read callback, open/close) in one file and export it; have card_fingerprint call it with read-only access. Delete the other copy and its defines.
- Justification: Replaces a duplicated 45-line cache plus open sequence with one shared copy; the export is a single declaration, so the net change is a removal.
- Benefit: ~55 lines removed (INFERRED from the cited ranges). Sharing the 16 KB cache as well would save 16 KB .bss (4*4096, INFERRED), but only if no fingerprint runs while a card-tool image is open; not verified.
- Effort: M. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-095: ctLibToCard re-implements dashCardImportMany (port < 0) for library .psu files

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_card_tool.c:2465-2535`; `Launchstation/src/dash_card_tool.c:1839-1933`
- Problem: ctLibToCard picks a card image, runs ctStacks/ctWritable, confirms, then loops ctLoadPsu + ctPutSave with one shared safety copy, counts done/skipped/failed/left, and builds the same summary text (2518-2533 matches 1917-1932) as dashCardImportMany does for a dir plus names[]. dashCardImportMany also refuses PS1 and read-only PCSX2 images up front, which ctLibToCard does not.
- Fix: Build a names[] array of the picked lib->list[i].rel strings and call dashCardImportMany(lib->folder, names, n, -1); delete ctLibToCard's body. If the set name in the confirm title matters, pass it as an optional title argument.
- Benefit: ~55 lines removed (71-line function replaced by ~15 lines building names[]; INFERRED). One import path to test instead of two.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-096: Neutrino game profiles use two versioned slots, a checksum and a second full re-read after dashWriteFile already verifies

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/gui_neutrino.c:93-95`; `Launchstation/src/gui_neutrino.c:113-133`; `Launchstation/src/gui_neutrino.c:167-261`; `Launchstation/src/dash_store.c:148-175`
- Problem: Each ~70-byte per-game options record is kept as NEUT-<id>-A.CNF and -B.CNF with a revision number and FNV checksum; a save reads both slots, writes the older one, then profileReadLatest re-reads and re-parses both slots to verify. dashWriteFile already re-opens and byte-compares the written file (dash_store.c:163-174), and losing a profile on an interrupted write only means the game launches with default options.
- Fix: Store one NEUT-<id>.CNF written with dashWriteFile and read with dashLoadTextFile; delete profileChecksum, profileReadLatest, the slot/revision logic and the post-write re-read. Keep the strict sscanf/range validation. Old A/B files are simply ignored (or read the -A name once if migration matters). Update tests/test_neutrino_profiles.py.
- Benefit: ~50 lines removed; one memory-card file per profiled game instead of two (each MC file costs a directory entry and at least one 1 KB cluster, INFERRED), and a save does 1 write + 1 verify read instead of 2 reads + 1 write + 3 reads.
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-097: libhdd.c: dead DEBUG printf blocks and PFS_FRAGMENT branch inside live hddMakeFilesystem

- Category: 1 (Dead code)
- Location(s): `Launchstation/ee_libs/hdd/libhdd.c:33-44`; `Launchstation/ee_libs/hdd/libhdd.c:344-351`; `Launchstation/ee_libs/hdd/libhdd.c:359-378`; `Launchstation/ee_libs/hdd/libhdd.c:386-400`; `Launchstation/ee_libs/hdd/libhdd.c:412-437`; `Launchstation/ee_libs/hdd/libhdd.c:448-450`
- Problem: DEBUG is only a commented-out #define (line 44) and the hdd_ee target sets no defines (ps2.yaml:423-427), so the 13 #ifdef DEBUG blocks in hddMakeFilesystem never compile. Without them, the retVal>=0 and else branches at 426-437 are empty. PFS_FRAGMENT is the constant 0 (line 35), so the three-element pfsFormatArg branch (37-39) and PFS_FRAGMENT_OPT are dead.
- Fix: Delete the #ifdef DEBUG blocks, the commented DEBUG define, the empty else-if/else branches, and the PFS_FRAGMENT/PFS_FRAGMENT_OPT #if, keeping the one-element pfsFormatArg.
- Benefit: ~50 lines removed from a live function; no ELF change because the blocks are already preprocessed out (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-098: mc_card_export.c duplicates its own I/O and header helpers (write_exact_at/_new, read_exact_at/read_exact_fd, source_info_fd/pcsx2_source_info)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_card_export.c:109-161`; `Launchstation/src/mc_card_export.c:87-107`; `Launchstation/src/mc_card_export.c:224-237`; `Launchstation/src/mc_card_export.c:173-203`; `Launchstation/src/mc_card_export.c:500-535`
- Problem: write_exact_at_new (137-161) is write_exact_at (109-130) with different open flags and an 'owned' flag, re-pasted with 4-space indents. read_exact_at (87-107) is open + the loop of read_exact_fd (224-237) + close. pcsx2_source_info (500-535) repeats source_info_fd's magic check, superblock field reads and geometry check (173-203); only the bytes-per-page used for the expected size differs (528 vs 512).
- Fix: Give write_exact_at the flags/owned parameters and delete write_exact_at_new; implement read_exact_at as open + read_exact_fd + close; make one header parser that takes the page stride and returns raw and encoded sizes.
- Benefit: ~50 lines removed (INFERRED from the cited ranges); a few hundred bytes of .text (INFERRED).
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-099: cloudHttpDownloadFdInternal and read_response repeat the same 3-5 line cleanup at about 35 exit points

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/cloud_http.c:994-1266`; `Launchstation/src/cloud_http.c:573-683`
- Problem: In cloudHttpDownloadFdInternal every error exit repeats its own cleanup: within lines 953-1277 there are 21 tls_free(&tls), 24 free(io_buf), 17 free(raw) and 10 mbedtls_sha256_free(&sha) statements spread over about 24 exits. read_response repeats free(raw) 13 times over 11 exits. The repetition makes each new check cost 4-6 lines and invites a missed free.
- Fix: Initialise raw/io_buf to NULL and keep a flag for an opened TLS session and a started SHA context, then make each error path set_error and goto one fail label that frees what is set; do the same with one fail label in read_response.
- Benefit: About 72 cleanup lines in the download loop collapse to ~24 gotos plus an ~8 line label, and ~10 lines go from read_response: ~50 lines net (INFERRED from the counts above). No size claim measured.
- Effort: M. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-100: Little-endian rd16/rd32/wr16/wr32 copied into five mc_*.c files, mode-bit macros into two

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_save_formats.c:11-42`; `Launchstation/src/mc_card_write.c:11-36`; `Launchstation/src/mc_card_view.c:31-35`; `Launchstation/src/mc_ps1_card.c:17-31`; `Launchstation/src/mc_save_export.c:14-24`
- Problem: The same static byte-order helpers (17 lines in mc_save_formats.c and mc_card_write.c, 15 in mc_ps1_card.c, 11 in mc_save_export.c, 5 in mc_card_view.c) are redefined in each file, and mc_save_formats.c repeats ENT/M_EXISTS/M_DIR/M_FILE from mc_card_write.c.
- Fix: Put the four helpers once as static inline functions (and the shared ENT/M_* card constants) in a small common header such as mc_card_write.h, which mc_save_formats.c already includes, and delete the per-file copies.
- Justification: Adds one header block of about 17 lines but removes about 65 duplicated lines across five files; no new layer.
- Benefit: about 48 source lines net; no ELF change (static helpers are inlined either way, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-101: Little-endian rd16/rd32/wr16/wr32 helpers copied into five mc_*.c files

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_card_view.c:31-35`; `Launchstation/src/mc_card_write.c:20-36`; `Launchstation/src/mc_ps1_card.c:17-31`; `Launchstation/src/mc_save_export.c:14-24`; `Launchstation/src/mc_save_formats.c:26-42`; `Launchstation/src/mc_save_export.c:196`; `Launchstation/src/mc_save_export.c:206`
- Problem: The same static byte-order helpers are defined five times (65 lines in all); mc_save_export.c additionally open-codes rd16 of the entry mode at lines 196 and 206. mc_card_write.h, mc_save_export.h and mc_save_formats.h all include mc_card_view.h, so one definition there reaches four of the five files.
- Fix: Define rd16/rd32/wr16/wr32 once as static inline functions in include/mc_card_view.h (mc_ps1_card.c includes it or keeps its own copy), delete the per-file copies, and use rd16 at mc_save_export.c:196 and 206.
- Justification: Adds one ~17-line header block but deletes four to five identical copies (48-65 lines); net code shrinks and the helpers cannot drift apart.
- Benefit: ~48 lines net removed (65 copied lines minus one 17-line shared copy). No ELF change expected: the statics are already inlined (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-102: Classic R1 'Mount vmcN' branch re-implements filerMountVmc, and filerMountVmc re-implements filerUnmountVmc

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_browser.c:1501-1550`; `Launchstation/src/filer_browser.c:2347-2395`; `Launchstation/src/filer_browser.c:2397-2416`
- Problem: The MOUNTVMC0/1 branch of the classic menu (1501-1550) does the same steps as filerMountVmc (2347-2395, whose comment says 'same steps as the classic menu'): unmount the old vmcN, genFixPath, makeHostPath for host/udpfs, append the name, load_vmcman, fileXioMount, record vmc_PartyIndex/Party_vmcIndex. The unmount block is written three times (1504-1512, 2355-2363, 2404-2414). Lines 1543-1544 are no-ops: that branch only runs with cnfmode==NON_CNF (else at 1293), and ext is never changed in that mode.
- Fix: Make the classic branch call filerMountVmc(path, &files[browser_sel], ret - MOUNTVMC0, msg1, sizeof(msg1)); on failure show msg1 with ynDialog, on success set path to 'vmcN:/' and browser_cd. Replace the unmount block inside filerMountVmc with a call to filerUnmountVmc(slot). Delete lines 1543-1544.
- Benefit: ~45 source lines removed (50-line branch becomes ~8, 9-line unmount copy becomes 1); a few hundred bytes of .text (INFERRED, both copies are live and not garbage-collected)
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-103: Dead globals and macros in main.c: DiscTypes table, CurrTime, SystemCnf_VMODE, dbgprintf/dbginit_scr

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main.c:82-106`; `Launchstation/src/main.c:36`; `Launchstation/include/launchelf.h:652`; `Launchstation/src/main.c:69`; `Launchstation/include/launchelf.h:374`; `Launchstation/src/main_startup.c:158-159`; `Launchstation/src/main_startup.c:176`; `Launchstation/src/main.c:16-27`
- Problem: DiscType/DiscTypes and CurrTime have no reference outside their definitions (CurrTime also has an extern); SystemCnf_VMODE is written by scanSystemCnf/readSystemCnf but never read (its own comment says 'As yet unused'); the dbgprintf/dbginit_scr macros are defined in main.c and never used there.
- Fix: Delete DiscType, DiscTypes, CurrTime (and its extern), SystemCnf_VMODE (definition, extern, the VMODE branch and the reset line in main_startup.c), and the DEBUG macro block in main.c.
- Benefit: MEASURED in BUILD-EVIDENCE.md:160: DiscTypes 340 B .data and CurrTime 8 B .bss stay in the ELF (no -fdata-sections); SystemCnf_VMODE 10 B .bss (INFERRED); ~45 source lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-104: Debug-build macros IOPTRAP, UDPTTY, POWERPC_UART (cannot link), SIO_DEBUG and NO_IOP_RESET are set by no build

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/init.c:51-58`; `Launchstation/src/init.c:73-75`; `Launchstation/src/init.c:1224-1227`; `Launchstation/src/init.c:3079-3091`; `Launchstation/src/init.c:1244-1251`; `Launchstation/src/init.c:823-825`; `Launchstation/src/init.c:2061-2064`; `Launchstation/src/init.c:3052-3062`; `Launchstation/src/dash_info.c:269-285`; `Launchstation/src/main_info_screens.c:173-201`
- Problem: No ps2.yaml target or script defines any of these. IOPTRAP, UDPTTY and POWERPC_UART load ioptrap_irx, udptty_irx and ppctty_irx, which exist nowhere in the repo or embed list, so turning them on fails to link; UDPTTY plus POWERPC_UART together would also redeclare 'int i, d' inside Reset(). SIO_DEBUG and NO_IOP_RESET still compile but are unmaintained leftovers: NO_IOP_RESET removes the IOP reset that every stack switch in this file depends on. The two SIO_DEBUG blocks at 1244-1251 are back to back.
- Fix: Delete the IOPTRAP, UDPTTY and POWERPC_UART blocks (imports and load code) and the matching lines in the info screens. Either delete SIO_DEBUG and NO_IOP_RESET too, or if they are kept as a debug option, merge the two SIO_DEBUG blocks into one.
- Benefit: ~25 lines removed in init.c plus ~25 in the two info screens; no ELF change (preprocessor-only code, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-105: UDPTTY, POWERPC_UART, IOPTRAP and COMMON_PRINTF debug variants cannot be built: their IRX blobs exist nowhere

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/launchelf.h:7-10`; `Launchstation/include/launchelf.h:160-162`; `Launchstation/src/init.c:51-58`; `Launchstation/src/init.c:73-75`; `Launchstation/src/init.c:1224-1227`; `Launchstation/src/init.c:3079-3091`; `Launchstation/src/main_info_screens.c:183-187`; `Launchstation/src/main_info_screens.c:195-199`; `Launchstation/src/dash_info.c:276-278`; `Launchstation/src/dash_info.c:282-284`
- Problem: No build file defines UDPTTY, POWERPC_UART, IOPTRAP or COMMON_PRINTF, and defining one cannot link: init.c imports udptty_irx/ppctty_irx/ioptrap_irx via IMPORT_BIN2C but no udptty, ppctty or ioptrap module is embedded or present anywhere in the repo. COMMON_PRINTF appears only in its own #elif. With both UDPTTY and POWERPC_UART, init.c:3080/3086 would also redeclare i, d.
- Fix: Delete the UDPTTY, POWERPC_UART, IOPTRAP and COMMON_PRINTF branches (version suffix, DPRINTF #elif, IMPORT_BIN2C lines, module loads, build-info lines). Keep SIO_DEBUG, NO_IOP_RESET and ULE_DEBUG_BUILD, which need no missing blobs. Keep launchelf.h:36, which tests/test_project_branding.py checks.
- Benefit: ~45 source lines and 4 dead build variants removed; no shipped-ELF change (code is preprocessed out) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-106: copy() repeats the per-device stat/attribute restoration for folders and files, each with an empty if-else chain

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_copy.c:1225-1287`; `Launchstation/src/filer_copy.c:1639-1691`
- Problem: The folder closure (1225-1276) and file closure (1639-1691) are the same mc/vmc/xfrom/fileXio dispatch differing only in MC_ATTR_norm_folder vs MC_ATTR_norm_file, an mctype_PSx==2 check and an mmce skip (missing from the folder copy). Both use a 5-branch if/else-if chain whose bodies are empty just to skip fileXioChStat; the PSU_RESTORE closure 1277-1287 repeats the same mc/vmc/xfrom dispatch a third time.
- Fix: Make one static function that applies copied stats to 'out' given 'in', the stats and the normal attribute value, with the skip list as a single condition (host, udpfs, smb, mmce, mass, mx4sio, ata, cf), and call it from both closures; reuse it for the PSU_RESTORE folder fix-up.
- Benefit: ~45 source lines (INFERRED: two ~52-line blocks plus an 11-line block become one ~30-line function and three calls); also gives folders the same mmce skip as files.
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-107: join_path, dir_exists and mkdir-or-exists are re-implemented in mmce_backup.c, mmce_igr.c and mmce_save_versions.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mmce_backup.c:73-96`; `Launchstation/src/mmce_igr.c:34-44`; `Launchstation/src/mmce_igr.c:305-315`; `Launchstation/src/mmce_igr.c:403-411`; `Launchstation/src/mmce_igr.c:439-445`; `Launchstation/src/mmce_save_versions.c:27-52`
- Problem: join_path in mmce_igr.c:34-44 is a byte-identical copy of mmce_backup.c:73-83, and svJoin in mmce_save_versions.c:27-34 is the same logic again; svDirExists (mmce_save_versions.c:36-47) is identical to dir_exists (mmce_backup.c:85-96). mmce_igr.c open-codes 'genMkdir, else probe with genDopen' three times, which is what svMkdir and ensure_dir_tree already do.
- Fix: Make join_path, dir_exists and a mkdir-if-missing helper non-static in one file (for example next to wleExists in main_fileops.c) and use them from all three files; delete the copies and the three inline probe blocks.
- Benefit: about 45 source lines net (19 lines of join copies, 12 of dir_exists, ~24 of inline mkdir probes, minus ~5 lines of prototypes); negligible ELF change (static helpers, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-108: prepareTransferDeviceStacks check and warning block repeated 9 times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_browser.c:1509-1517`; `Launchstation/src/dash_browser.c:1577-1585`; `Launchstation/src/dash_browser.c:1648-1656`; `Launchstation/src/dash_browser.c:1707-1715`; `Launchstation/src/dash_browser.c:1773-1781`; `Launchstation/src/dash_browser.c:1932-1940`; `Launchstation/src/dash_browser.c:2101-2109`; `Launchstation/src/dash_browser.c:2254-2261`; `Launchstation/src/dash_browser.c:2341-2349`
- Problem: Every card operation calls prepareTransferDeviceStacks and then shows a modal with one of two near-identical texts (incompatible stacks / device not started), each written out again with slightly different wording, 8-9 lines per site.
- Fix: Add one static helper that runs prepareTransferDeviceStacks(source, dest), shows the modal with a caller-supplied title and the standard two texts, and returns 0/1; replace the nine blocks with one-line calls.
- Justification: The same 8-9 line block appears nine times in this file; a ~12-line helper replaces ~72 lines, a net removal of ~45 lines and one set of message strings.
- Benefit: ~45 lines removed net (INFERRED: 9 x 8 lines minus 9 call lines minus ~12-line helper) and a few hundred bytes of duplicate strings (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-109: printXY_sjis repeats the Shift-JIS to ELISA index mapping and symbol table that dash_font.c also has

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/draw_text.c:220-292`; `Launchstation/src/dash_font.c:46-68`; `Launchstation/src/dash_font.c:121-125`
- Problem: draw_text.c:266-292 converts SJIS to EUC and walks font404 to get the ELISA glyph index; dash_font.c:46-68 (elisaGlyphIndex, commented 'Same ... mapping as the classic renderer') does the same arithmetic. The five PlayStation-button codes (0x819B, 0x817E, 0x81A0, 0x81A2, 0x81A1) are hand-written as five copy-paste switch cases in draw_text.c:222-260 and again as an if-chain in dash_font.c:121-125.
- Fix: Make elisaGlyphIndex non-static (or move it next to font404 in draw_text.c) and call it from printXY_sjis (glyph * 8 is the byte offset); replace the five switch cases with one small code-to-glyph table shared by both files.
- Benefit: ~45 lines removed (INFERRED: 39 lines of switch cases become ~6, 27 lines of mapping become one call); a few hundred bytes of .text (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-110: preloadCNFFd streamed-read fallback (128 KB buffer) for devices that fail lseek

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/config.c:451-453`; `Launchstation/src/config.c:465-511`
- Problem: After a failed SEEK_END/SEEK_SET (or a failed small malloc) the file is reopened and read in 4 KB chunks into a fresh 128 KB+1 buffer with a probe for oversize. No device in the repo is shown to need it (the comment only says 'some stacks may'); a malloc failure for a small CNF falls into a path that allocates more. Every other loader in this file (configWriteNetworkSnapshot, configStampMcFolderIcon) simply relies on genLseek.
- Fix: Delete the fallback_stream path and return NULL when lseek or malloc fails, as configWriteNetworkSnapshot does.
- Benefit: ~45 lines removed; a few hundred bytes of preloadCNFFd's 532 B .text (MEASURED symbol size; saving INFERRED).
- Effort: S. Risk: Med. Confidence: Low (INFERRED)

#### LAUNCHSTATION-111: emberUsbProbePath only builds a string for a stripped DPRINTF; its checks repeat emberUsbSourceIsUsb

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/init.c:1831-1871`; `Launchstation/src/init.c:2048`; `Launchstation/src/init.c:2053`; `Launchstation/src/init.c:2065`
- Problem: prepareEmberUsbHandoffIop calls emberUsbProbePath to fill probe[MAX_PATH], whose only reader is the DPRINTF at 2065 (empty in shipped builds). Its path validation (usb:, usbN:, mass:, massN:) is already done by emberUsbSourceIsUsb(ember_path) on the next line, which is stricter (it also checks the BDM driver name).
- Fix: Delete emberUsbProbePath, the probe buffer and its call; log ember_path in the DPRINTF instead.
- Benefit: ~43 lines removed and one MAX_PATH stack buffer less in prepareEmberUsbHandoffIop (INFERRED from the cited lines)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-112: Commented-out debug code and stale comments in filer_browser.c

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_browser.c:2029-2033`; `Launchstation/src/filer_browser.c:2072`; `Launchstation/src/filer_browser.c:2080-2104`; `Launchstation/src/filer_browser.c:2123`; `Launchstation/src/filer_browser.c:2134`; `Launchstation/src/filer_browser.c:1819`; `Launchstation/src/filer_browser.c:2232`; `Launchstation/src/filer_browser.c:944`; `Launchstation/src/filer_browser.c:1591-1592`; `Launchstation/src/filer_browser.c:2307-2311`
- Problem: submenu_func_GetSize carries a commented-out variable block (2029-2033) and a 23-line commented-out printf/fileXioGetStat attribute dump (2080-2104) with its 'Comment out this section' markers; single commented-out lines remain at 1819, 2123, 2232 and 944; 'TEST' comments at 1591-1592; and an 'End of file: filer_browser.c' banner at 2307-2311 is followed by 200 more lines of code.
- Fix: Delete the commented-out code and the stale markers/banners.
- Benefit: ~40 comment lines removed; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-113: Four unused functions: configFormatSavePathValue, configDefaultSaveTargetPath, saveConfig, configSaveNetworkToPath

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/config.c:86-102`; `Launchstation/include/config_private.h:54`; `Launchstation/src/config.c:1338-1345`; `Launchstation/include/config_private.h:70`; `Launchstation/src/config.c:788-791`; `Launchstation/src/config_network.c:70-73`; `Launchstation/include/config_private.h:75-77`
- Problem: Repo-wide grep finds only the definition and prototype of each name: configFormatSavePathValue (config.c:86, config_private.h:54), configDefaultSaveTargetPath (config.c:1342, config_private.h:70; the dashboard calls configSnapshotSaveTargetPath directly), saveConfig (config.c:788, no prototype), configSaveNetworkToPath (config_network.c:70, a one-line forward to saveNetworkSettings, prototype config_private.h:77).
- Fix: Delete the four functions, their prototypes and the comment block above configDefaultSaveTargetPath and configSaveNetworkToPath.
- Benefit: ~40 lines removed. No ELF change: all four are in link/wLaunchELF-gc-discarded-first-party.tsv (320 + 44 + 28 + 32 B already discarded by --gc-sections, MEASURED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-114: Non-EXFAT #else branches in filer.c are dead: EXFAT is always defined

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer.c:299-306`; `Launchstation/src/filer.c:311`; `Launchstation/src/filer.c:336`; `Launchstation/src/filer.c:983`; `Launchstation/src/filer.c:994-999`; `Launchstation/src/filer.c:1009-1011`; `Launchstation/src/filer.c:1336-1340`; `Launchstation/src/filer.c:1375-1379`; `Launchstation/src/filer.c:1962`; `Launchstation/src/filer.c:1970-1976`; `Launchstation/src/filer.c:1997`; `Launchstation/src/filer.c:2010-2014`; `Launchstation/src/filer.c:2042`; `Launchstation/src/filer.c:2327`; `Launchstation/src/filer.c:2340`
- Problem: ps2.yaml:629 defines EXFAT for the only EE target and configure_ds34.py keeps the define list, so every #else (non-EXFAT, old mass: driver) branch in filer.c is never compiled: the mass:/massN: mapping at 301-305, the old mass path patch at 994-998, the mass0:/ probe path at 1338-1339 and 1377-1378, the old mass listing at 1970-1975 and the ATA fallback at 2010-2011.
- Fix: Delete the #else bodies and the surrounding #ifdef EXFAT/#endif lines in filer.c, keeping the EXFAT code unconditional. The same macro is tested at about 60 other sites (init.c 28, dash_devices.c 8, ...) that can be cleaned the same way.
- Benefit: ~40 source lines removed from filer.c (about 20 dead body lines plus 20 directive lines); no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-115: dashDevicesStart is synchronous but callers use a callback plus deferred pend/pick state to get its result

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_devices.c:738-774`; `Launchstation/include/dash_internal.h:802-805`; `Launchstation/src/dash_files.c:64-137`; `Launchstation/include/dash_internal.h:1133`; `Launchstation/src/dash_main.c:1450`; `Launchstation/src/dash_widgets.c:960-966`; `Launchstation/src/dash_widgets.c:1117-1126`; `Launchstation/src/dash_widgets.c:1206-1210`
- Problem: dashDevicesStart runs startSync/probeMount on the UI thread and calls cb before it returns (dash_devices.c:765-773); `user` is NULL at every call site and both callbacks (pendProbeDone, pickStartDone) ignore mount and user and only latch ok. dash_files.c then keeps a pend struct, an 'Another device is still starting' guard, a per-frame dashFilesPoll from dash_main.c and a section-changed branch, although the result is already known when the call returns; dash_widgets.c does the same with pk.start_done/start_ok.
- Fix: Drop DashProbeDone, cb and user; return the probe result directly (keep -1 for backed out and 1 for refused, e.g. return 2/0 for usable/not found or move refused to -2). Handle the result inline in startDevice and in the picker, and delete pend, pendProbeDone, dashFilesPoll (and its call and prototype), pickStartDone and the start_done/start_ok fields. Update tests/test_udpfs_retry.py:22 and test_device_input.py, which assert on the old call text.
- Benefit: ~40 lines across dash_devices.c, dash_files.c, dash_widgets.c, dash_main.c, dash_internal.h (INFERRED from cited ranges); one function call per frame removed
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-116: listDir ATA branch hand-rolls the retry loop that readGENERICWithFirstOpenRetry already implements

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer.c:1994-2043`; `Launchstation/src/filer.c:345-381`
- Problem: The ATA case of listDir (1994-2043) reads once, then polls fileXioDopen with a 100 ms busy backoff for 2000 ms (root) or 750 ms and re-reads, stopping on entries or on a non-root path. readGENERICWithFirstOpenRetry (345-381), already used for usb, mass, ilink and cf, does the same loop with the same root test semantics.
- Fix: Replace the ATA body with loadAtaModules(), canonicalizeAtaPath() and n = readGENERICWithFirstOpenRetry(read_path, info, max, root ? 2000 : 750) (or a flat 2000 as the other BDM devices use).
- Benefit: ~40 source lines removed; a few hundred bytes of .text INFERRED.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-117: Format-new-image, copy-saves and verify-list sequence written three times (ctResize, ctGameCard, dashCardNewBlank)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_card_tool.c:998-1028`; `Launchstation/src/dash_card_tool.c:1069-1107`; `Launchstation/src/dash_card_tool.c:1152-1191`; `Launchstation/src/dash_card_tool.c:1227-1251`; `Launchstation/src/dash_card_tool.c:1468-1477`
- Problem: ctResize 1069-1107 and ctGameCard 1152-1191 both: genOpen O_EXCL, mcCardFormat via ctAppend, close, then per save ctReadSave + ctOpen rw + mcCardWriteSave + ctClose, then reopen and require mcViewListSaves to count exactly the copied saves. dashCardNewBlank repeats the open/format/close part. The source prelude (ctOpen, mcCardReadIdentity, mcViewListSaves, refuse more than 250 saves with the same text, copy names) is also duplicated between ctResize 998-1028 and ctSplitGameId 1227-1251.
- Fix: Let ctResize call ctGameCard with a NULL id meaning every save (ctGameCard already takes the list and filters by id), and have dashCardNewBlank call the same format step with n = 0; merge the two source preludes into one helper that returns the identity and the name list.
- Benefit: ~40 lines removed (INFERRED from the repeated ranges).
- Effort: M. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-118: mmce_igr.c write_verified duplicates ctWriteFileAs

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mmce_igr.c:179-219`; `Launchstation/src/dash_card_tool.c:552-593`
- Problem: write_verified (41 lines) writes a buffer with O_TRUNC, reads it back in chunks, compares and removes the file on mismatch; ctWriteFileAs does exactly this (with fresh=0) and is already exported in its O_EXCL form as dashCardWriteVerified.
- Fix: Give dashCardWriteVerified the fresh flag (or export ctWriteFileAs) and call it from mmce_igr.c; delete write_verified.
- Benefit: ~38 lines removed (INFERRED). No RAM change: mmce_igr's 32 KB verify_buf is still used by inspect_helper_path (mmce_igr.c:248-256).
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-119: Classic R1 'New icon' branch duplicates filerCreateFolderIcon (incl. the DoneIcon goto dummy)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_browser.c:1457-1500`; `Launchstation/src/filer_browser.c:2420-2460`
- Problem: The NEWICON branch (1457-1500) does genFixPath, icon.sys exists check with overwrite prompt, make_iconsys, genFixPath again, icon.icn exists check with overwrite prompt, make_icon: the same sequence as filerCreateFolderIcon (2420-2460), which dash_browser.c:2389 uses. The classic copy also needs a goto label with a dummy strcpy 'to make goto DoneIcon legal for gcc' (1498-1499) and ignores make_iconsys/make_icon errors.
- Fix: In the classic branch, ask for the title and the icon text with keyboard() first, then call filerCreateFolderIcon(path, title, text, msg0, sizeof(msg0)) and set browser_cd. Delete the rest of the branch and the DoneIcon label.
- Benefit: ~35 source lines removed; small .text saving (INFERRED, both copies are compiled and reachable)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-120: Two near-duplicate function pairs in hdd_header_injector.c: the source wait loops and the raw-range read/write

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/hdd_header_injector.c:299-319`; `Launchstation/src/hdd_header_injector.c:403-423`; `Launchstation/src/hdd_header_injector.c:561-589`; `Launchstation/src/hdd_header_injector.c:591-624`
- Problem: hddHeaderWaitForSourceRoot and hddHeaderWaitForSource are the same 21-line timed loop except for the probe call and the message. hddHeaderReadRawRange and hddHeaderWriteRawRange share the same bounds checks and sector walk (15 normalised lines per the duplicate report); the write side only adds a memcpy direction and a write-back.
- Fix: Make one wait loop that takes the partition name (NULL meaning the __Headers root) and one raw-range function with a write flag.
- Benefit: ~35 lines removed; static functions, small .text saving (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-121: main_startup.c small redundancies: one-entry ROMVER path loop, second ROMVER prefix parser, redundant #if, unused counter

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_startup.c:216-245`; `Launchstation/src/main_startup.c:289-292`; `Launchstation/src/main_startup.c:726-757`; `Launchstation/src/main_startup.c:597-606`; `Launchstation/src/main_startup.c:150-181`
- Problem: uLE_InitializeRegion loops over a one-element romver_paths array and strncpy's the literal into path_buf before opening it. getRomverPrefixNumber re-parses the first four ROMVER digits as decimal, although initializeBootExecPath already parses them as hex into ROMVersion; for digit-only prefixes 'decimal >= 220' equals 'ROMVersion >= 0x220'. The hide flag is set in two places (292 and 755). The outer #if defined(ETH) || defined(UDPFS) at 597/606 only wraps two inner #ifdefs. readSystemCnf counts var_cnt and scanSystemCnf returns a value, and neither is read.
- Fix: Open "rom0:ROMVER" directly; compute hide_hdd_ata_devices only in shouldHideHddAtaDevices from strtoul(prefix, NULL, 16) >= 0x220 and delete getRomverPrefixNumber and line 292; drop the outer #if; drop var_cnt and make scanSystemCnf void.
- Benefit: ~35 lines removed (INFERRED from the cited ranges); negligible ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-122: mmce_igr.c re-implements its own install/inspect plumbing several times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mmce_igr.c:395-421`; `Launchstation/src/mmce_igr.c:423-450`; `Launchstation/src/mmce_igr.c:474-484`; `Launchstation/src/mmce_igr.c:273-279`; `Launchstation/src/mmce_igr.c:293-301`; `Launchstation/src/mmce_igr.c:333-340`; `Launchstation/src/mmce_igr.c:571-574`; `Launchstation/src/mmce_igr.c:668-671`; `Launchstation/src/mmce_igr.c:774-777`; `Launchstation/src/mmce_igr.c:727-730`; `Launchstation/src/mmce_igr.c:746-749`; `Launchstation/src/mmce_igr.c:786-789`
- Problem: install_direct_helper rebuilds the target path that direct_helper_target computes, and its mc branch is install_helper_on_mc_slot for slot boot[2]-'0'; the split/pick-free-slot/filerMountVmc preamble is written out in all three BootCard functions; the 'reset count, collect_boot_images for mmce0 and mmce1' loop appears three times; the CURRENT/OUTDATED/MISSING/failed counting chain appears three times in wleIgrStatus.
- Fix: Have install_direct_helper call install_helper_on_mc_slot for mc paths and direct_helper_target for USB; factor the mount preamble, the two-unit collect loop and the status counting into one small static function each.
- Benefit: about 35 source lines net; small code size reduction INFERRED
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-123: read_file in cloud_http.c re-implements dashLoadTextFile

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/cloud_http.c:272-307`; `Launchstation/src/cloud_http.c:400`; `Launchstation/src/dash_store.c:105-146`
- Problem: read_file opens a file, rejects size <= 0 or > 256 KB, reads it all into a malloc'd buffer and NUL-terminates it. dashLoadTextFile (dash_store.c:105) does the same with a size cap argument and is already used for the same CA file type in cloud_google.c:535 (256 * 1024). read_file has one caller, tls_open at 400.
- Fix: Delete read_file and call dashLoadTextFile(ca_path, 256 * 1024, &ca_len) in tls_open, with an include of dash_internal.h (or its prototype). Update the read_file stub in tests/test_https_tls.py:89.
- Benefit: 36 lines removed, 1 added; a few hundred bytes of .text (INFERRED, not measured).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-124: ember.c copyElf duplicates psu_functions.c psu_backup_copy_bytes (copy file, delete partial output)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/ember.c:209-246`; `Launchstation/src/psu_functions.c:127-162`
- Problem: Both static functions open a source, create/truncate a destination, copy in a heap buffer until EOF, check each write, close both and genRemove the destination on failure (~30 of their lines do the same thing). They differ only in buffer size (32 KB vs PSU_COPY_BUFFER), memalign vs malloc, openPathForRead vs genOpen for the source and the return convention.
- Fix: Keep one of them as a shared non-static helper (for example psu_backup_copy_bytes, returning 0/negative) and call it from ensureLocalEmber; delete copyElf.
- Justification: Two real copies exist; sharing one removes ~35 lines net.
- Benefit: ~35 source lines and a few hundred bytes of .text (INFERRED; ember.o total is 4,092 bytes MEASURED in wLaunchELF-contrib-by-object.tsv).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-125: Timestamp apply-and-report loop copied in time_manip_custom_run and time_manip_automatic_run

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_actions.c:1352-1390`; `Launchstation/src/filer_actions.c:1848-1885`
- Problem: Both functions end with the same 38-line block: count changed folders, draw progress, write each changed folder with setMemoryCardFolderTimestamp or setHddCommonFolderTimestamp, then build the same three result messages and return codes. The two copies differ only by one blank line.
- Fix: Move the block into one static function taking path, folder_count, reserve_tuna_date and message, and call it from both places.
- Benefit: ~36 lines removed (38-line block kept once plus a 4-line function header, two 1-line calls); a few hundred bytes of .text (INFERRED: code is duplicated, not inlined from a shared function)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-126: Commented-out CodeBreaker partition filter, two copies

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer.c:745-761`; `Launchstation/src/filer.c:849-864`
- Problem: The same 16-17 line block of commented-out code (a CodeBreaker .PCB and __ partition name filter) sits in setPartyListForDevice and in setDVRPPartyList. It has been disabled since the uLE era and nothing refers to it.
- Fix: Delete both commented blocks.
- Benefit: 33 source lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-127: Unused: dashAppsFocusPath, dashAppsVisibleCount, dashArtShutdown, dashDrawAppTile, dashBgDrawStatic, ART_MAX_CANDIDATES

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_apps.c:94-106`; `Launchstation/src/dash_art.c:31`; `Launchstation/src/dash_art.c:71-74`; `Launchstation/src/dash_art.c:787-790`; `Launchstation/src/dash_bg.c:543-547`; `Launchstation/include/dash_internal.h:741`; `Launchstation/include/dash_internal.h:758`; `Launchstation/include/dash_internal.h:769`; `Launchstation/include/dash_internal.h:1136-1139`
- Problem: Repo-wide grep finds only the definitions and prototypes for dashAppsFocusPath, dashAppsVisibleCount, dashArtShutdown, dashDrawAppTile and dashBgDrawStatic, and only the #define for ART_MAX_CANDIDATES. The link map confirms the linker discards all five functions; the dashBgDrawStatic prototype comment ("legacy screens / FX off") describes callers that no longer exist.
- Fix: Delete the five functions, their prototypes in dash_internal.h and the ART_MAX_CANDIDATES define.
- Benefit: ~33 source lines removed. No ELF change: MEASURED, evidence/Launchstation/raw/link/wLaunchELF-gc-discarded-first-party.tsv lists all five .text sections as discarded (256+12+132+52+28 = 480 bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-128: releaseJsonString and the asset size parse re-implement cloud_google.c json_string/json_int

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_tools.c:715-745`; `Launchstation/src/dash_tools.c:779-785`; `Launchstation/src/cloud_google.c:61-121`
- Problem: dash_tools.c releaseJsonString (needle "key", strstr, skip to ':', whitespace, quoted value copy) is the same algorithm as the static json_string in cloud_google.c, which also handles escapes; the inline "size" parse in releaseAssetDigest (skip spaces and ':' then atoi) is json_int's body. Both files are linked into the same ELF and the dash_tools.c code is only built with ETH && WLE_NATIVE_CLOUD, where cloud_google.c is also compiled.
- Fix: Make cloud_google.c json_string (and json_int) non-static with a prototype in cloud_google.h, delete releaseJsonString and call the shared one; replace the inline size parse with json_int on the asset slice. Results are equivalent for the fields used (status, target_commitish, digest, name), which are then validated anyway.
- Benefit: ~33 source lines removed and one copy of the parser in .text (a few hundred bytes, INFERRED from the 31-line function at -Os)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-129: The 15 MISC command names are listed three times (lang.c rename chain, lang.c rebuild, config.c initConfig)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/lang.c:382-412`; `Launchstation/src/lang.c:418-433`; `Launchstation/src/config.c:801-816`
- Problem: updateLocalizedMiscPaths has a 15-arm else-if chain pairing each setting->Misc_* field with its LANG id, then 16 sprintf lines building the same fields again; initConfig repeats the 16 sprintf lines with LNG_DEF (and config.c:814 uses LNG instead of LNG_DEF for Build_Info, an inconsistency).
- Fix: Add one static table of {offsetof(SETTING, Misc_x), LANG_x} and loop over it in all three places (dash_tools.c already addresses these fields by misc_offset). Keep the extra "Exploit Installer" legacy alias as one special case.
- Justification: The same 15-entry list is written out three times (about 63 lines); one table plus three short loops is about 30 lines, so the change removes code, and a new MISC command then needs one table line instead of three edits.
- Benefit: ~33 source lines removed net (INFERRED: 63 cited lines -> ~30); some .text from the unrolled sprintf calls (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-130: Defensive NULL/empty checks and redundant NUL stores in static hdd_header_injector.c helpers

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/hdd_header_injector.c:106-107`; `Launchstation/src/hdd_header_injector.c:199-200`; `Launchstation/src/hdd_header_injector.c:211-212`; `Launchstation/src/hdd_header_injector.c:233-234`; `Launchstation/src/hdd_header_injector.c:273`; `Launchstation/src/hdd_header_injector.c:391`; `Launchstation/src/hdd_header_injector.c:429-430`; `Launchstation/src/hdd_header_injector.c:442-443`; `Launchstation/src/hdd_header_injector.c:486-487`; `Launchstation/src/hdd_header_injector.c:529-530`; `Launchstation/src/hdd_header_injector.c:549-550`; `Launchstation/src/hdd_header_injector.c:567-568`; `Launchstation/src/hdd_header_injector.c:597-598`; `Launchstation/src/hdd_header_injector.c:631-632`; `Launchstation/src/hdd_header_injector.c:659-660`; `Launchstation/src/hdd_header_injector.c:955-956`; `Launchstation/src/hdd_header_injector.c:1126`
- Problem: Every listed function is static and every caller passes a stack array, a static buffer or an argument the public entry already validated (350, 1112-1115), so the NULL checks cannot fire. The source_dir[size-1]='\0' stores follow snprintf, which always terminates.
- Fix: Delete these checks and stores; keep the validation in the three public entry points.
- Benefit: ~32 lines removed; a few dozen bytes of .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-131: Hide HDD option and its name table/normalizer are duplicated in Config_Startup and Config_Advanced

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/config_startup.c:23`; `Launchstation/src/config_startup.c:30-52`; `Launchstation/src/config_startup.c:140-141`; `Launchstation/src/config_startup.c:200-201`; `Launchstation/src/config_startup.c:292-294`; `Launchstation/src/config_advanced.c:16`; `Launchstation/src/config_advanced.c:27-49`; `Launchstation/src/config_advanced.c:121-122`; `Launchstation/src/config_advanced.c:153-154`; `Launchstation/src/config_advanced.c:262-265`
- Problem: The same setting->Hide_Hdd is editable from two classic sub-menus, and each file carries an identical static normalizeHideHddMode, hide_hdd_mode_names[7] table and getHideHddModeDisplayName (23 identical lines each). Moot if finding 1 is applied.
- Fix: If the classic menus are kept: remove the Hide HDD row from Config_Startup (enum entry, both key branches, draw lines, tooltip and spacing term) together with its copy of the table and helpers.
- Benefit: ~32 lines removed; one 7-pointer table (28 B) and two small statics fewer in .rodata/.text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-132: dashTextWrapScrollTimed is never called; the textWrap draw flag exists only for it

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_font.c:837-860`; `Launchstation/src/dash_font.c:785`; `Launchstation/src/dash_font.c:811-813`; `Launchstation/src/dash_font.c:832-835`; `Launchstation/include/dash_internal.h:421-422`
- Problem: dashTextWrapScrollTimed appears only at its definition and its prototype; the linker discards it. Its measurement pass is the only caller of textWrap with draw=0, so once it is gone the draw parameter and the empty if(!draw) branch are dead and textWrap is a private copy of dashTextWrap.
- Fix: Delete dashTextWrapScrollTimed and its prototype, drop the draw parameter and the if(!draw) branch, and rename textWrap to dashTextWrap (removing the one-line forwarder).
- Benefit: ~31 lines removed; no ELF change because --gc-sections already drops the 804-byte .text.dashTextWrapScrollTimed (MEASURED: evidence/Launchstation/raw/link/wLaunchELF-gc-discarded-first-party.tsv)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-133: CT32 GSTEXTURE setup re-typed five times while dash_gfx.c already has setupTex (static)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_gfx.c:196-207`; `Launchstation/src/dash_art.c:409-415`; `Launchstation/src/dash_art.c:532-539`; `Launchstation/src/dash_bg.c:432-440`; `Launchstation/src/dash_bg_geo.c:257-265`; `Launchstation/src/dash_image.c:130-137`
- Problem: Each copy writes the same memset/Width/Height/PSM=CT32/Mem/Filter/Delayed=1/gsKit_setup_tbw (and in two cases SyncDCache) sequence that setupTex in dash_gfx.c:196-207 implements, but setupTex is static and hard-codes GS_FILTER_LINEAR, so the other files repeat it (the scanline copy needs NEAREST).
- Fix: Make setupTex non-static as dashTexSetup(tex, w, h, mem, filter), declare it in dash_internal.h and replace the five copies with one call each.
- Justification: Exports an existing function instead of adding a new one; replaces five 7-9 line blocks with single calls, a net reduction of about 30 lines. The SyncDCache it adds to the art and image paths is harmless (the pixels must be flushed before the GS DMA upload anyway).
- Benefit: ~30 source lines removed, a few hundred bytes of .text (INFERRED: five inlined field-store sequences become calls).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-134: Copy before genFixPath is a dead store (genFixPath overwrites it); the fix-or-raw-path fallback is hand-written in 10 places

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/editor_file.c:80-85`; `Launchstation/src/editor_file.c:219-224`; `Launchstation/src/editor_file.c:285-290`; `Launchstation/src/elf.c:70-94`; `Launchstation/src/dash_editor.c:315-316`; `Launchstation/src/hdd_header_injector.c:698-701`; `Launchstation/src/hdd_header_injector.c:722-725`; `Launchstation/src/hdd_header_injector.c:761-764`; `Launchstation/src/hdd_header_injector.c:800-803`; `Launchstation/src/main_exploit_installer.c:1513-1516`; `Launchstation/src/filer.c:953-956`
- Problem: genFixPath always starts with strcpy(gen_path, uLE_path) (filer.c:956), so the strncpy done just before it in editor_file.c:80-81, 219-220, 285-286 and elf.c:90 is overwritten every time. The 'if genFixPath fails, copy the raw path' fallback is then repeated by hand at every listed site (elf.c even has two helpers, launchArgsCopyPath and launchArgsMakeOpenPath, only for it).
- Fix: Delete the pre-copies. Optionally make genFixPath itself copy the raw path on failure (or add one genFixPathOrRaw in filer.c) and drop the per-site fallbacks and elf.c's launchArgsCopyPath/launchArgsMakeOpenPath.
- Justification: One shared fallback replaces ten 2-4 line copies and two elf.c helpers, so the net is fewer lines.
- Benefit: ~8 lines of dead stores; ~30 lines with the shared fallback (INFERRED from the cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-135: Hide_Paths and Show_Titles settings have no consumer; only load/save/toggle code remains

- Category: 8 (Half-finished or redundant features)
- Location(s): `Launchstation/src/config.c:606`; `Launchstation/src/config.c:620`; `Launchstation/src/config.c:638`; `Launchstation/src/config.c:652`; `Launchstation/src/config.c:844`; `Launchstation/src/config.c:870`; `Launchstation/src/config.c:1008-1009`; `Launchstation/src/config.c:1043-1044`; `Launchstation/src/config.c:1573-1576`; `Launchstation/src/config.c:1681-1691`; `Launchstation/include/launchelf.h:314`; `Launchstation/include/launchelf.h:332`; `Launchstation/src/dash_settings.c:152-153`; `Launchstation/src/dash_settings.c:785-786`; `Launchstation/src/dash_settings.c:1234-1239`; `Launchstation/src/dash_settings.c:1952-1957`
- Problem: setting->Hide_Paths and setting->Show_Titles are only parsed, defaulted, serialized and toggled (classic Configure menu and dashboard items whose help text says they affect 'the classic launch menu'); nothing reads them to change output. The repo's own parity table says 'none (read only by the removed classic main menu)' (docs/R3Z-CONFIG-PARITY.md:35-36).
- Fix: Delete the two fields, their CNF read/write lines, defaults, the classic menu rows and the two dashboard items S_SHOW_TITLES/S_HIDE_PATHS; unknown keys in old CNF files are already ignored by loadConfig. Update tests/test_classic_settings_persistence.py:80 and the parity table.
- Benefit: ~30 lines removed, two CNF lines fewer per save, two dead dashboard rows gone (INFERRED, line count of cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-136: Same path-preparation preamble copied into six gen* wrappers in filer_fileops.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_fileops.c:231-243`; `Launchstation/src/filer_fileops.c:267-282`; `Launchstation/src/filer_fileops.c:365-375`; `Launchstation/src/filer_fileops.c:504-514`; `Launchstation/src/filer_fileops.c:541-552`; `Launchstation/src/filer_fileops.c:624-637`
- Problem: genRmdir, genRemove, genGetStat, genMkdir, genOpen and genDopen each repeat: check cf online, copy the path into a local MAX_PATH buffer, makeHostPath, ensurePathMemoryCardAccessible, normalizePcCardCfPath, genLimObjName (genDopen omits the last). genRmdir, genRemove and genDopen use a second MAX_PATH buffer for makeHostPath although genGetStat/genMkdir/genOpen show it works in place.
- Fix: Add one static helper that fills a caller buffer from a path with these steps and returns -ENODEV when the CF card is offline (with a flag to skip genLimObjName for genDopen), and call it from the six wrappers; drop the extra mapped_path buffers.
- Justification: The same 8-12 lines appear six times; one ~14-line helper plus six 2-line calls removes code and one MAX_PATH stack buffer in three wrappers.
- Benefit: ~30 source lines (INFERRED: 6 x ~9 lines replaced by 6 x 2 plus a 14-line helper); one fewer MAX_PATH stack buffer in genRmdir/genRemove/genDopen.
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-137: Screen-settings reset block copied three times (config_screen.c, dash_settings.c x2) plus initConfig

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/config_screen.c:166-185`; `Launchstation/src/config_screen.c:66-70`; `Launchstation/src/dash_settings.c:2770-2783`; `Launchstation/src/dash_settings.c:2791-2804`; `Launchstation/src/config.c:845-862`
- Problem: The same 13 assignments (8 DEF_COLOR colours, TV_mode, screen_x/y, Menu_Frame, Popup_Opaque) followed by updateScreenMode appear in config_screen.c:166-179, dash_settings.c:2770-2783 and 2791-2804, and the same values again inside initConfig. config_screen.c also repeats its rgb[] unpack loop (66-70 and 181-185).
- Fix: Add one configResetScreenDefaults() in config.c holding the assignments, call it from initConfig, resetScreen and resetDisplayThemeDefaults (and Config_Screen if it is kept, re-running the existing unpack loop).
- Justification: The identical 13-line block exists in 3 places plus initConfig; one ~15-line function and 4 calls replace ~50 lines, and the defaults can no longer drift apart.
- Benefit: ~30 net lines removed (3 x 13 + 12 in initConfig + 5 unpack, minus a 15-line helper and 4 calls; INFERRED); ~100-200 B .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-138: Small defensive and forwarding leftovers in main_boot.c, main_console_info.c, main_actions.c and main.c

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_boot.c:12-13`; `Launchstation/src/main_boot.c:39-42`; `Launchstation/src/main_boot.c:54-59`; `Launchstation/src/main_boot.c:93-94`; `Launchstation/src/main_console_info.c:64-68`; `Launchstation/src/main_console_info.c:178-179`; `Launchstation/src/main_console_info.c:120-124`; `Launchstation/src/main_actions.c:78-81`; `Launchstation/include/launchelf.h:418`; `Launchstation/src/main_actions.c:600-601`; `Launchstation/src/main_actions.c:720`; `Launchstation/src/main_actions.c:727-728`; `Launchstation/src/main.c:118-129`
- Problem: Single-caller code guards against inputs its only caller never passes: captureBootArguments and performEarlyBootInitialization check NULL/0 arguments and keep a local CNF buffer although main.c:276-277 always passes CNF and &CNF_error. get_model_fallback_name only writes "Unknown" and is called right after the model already is "Unknown" (178-179, a no-op); initialize_console_rom1_driver and CleanUpForEmberExec (one caller, ember.c:362) only forward. main_actions.c has 'if (fd < 0)' where fd < 0 is certain (600), a commented label (720) and 'x = x;' (728). wleIgrRequested scans argv twice with a no-op continue.
- Fix: Delete the unneeded checks and the local CNF fallback; replace get_model_fallback_name and the wrappers with their one-line bodies at the call sites; drop the no-op lines; merge the two argv loops in wleIgrRequested.
- Benefit: ~30 source lines removed; a MAX_NAME stack buffer gone from performEarlyBootInitialization (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-139: Small repeated idioms inside dash_card_tool.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_card_tool.c:275-280`; `Launchstation/src/dash_card_tool.c:1043-1048`; `Launchstation/src/dash_card_tool.c:1275-1280`; `Launchstation/src/dash_card_tool.c:1433-1440`; `Launchstation/src/dash_card_tool.c:2187-2194`; `Launchstation/src/dash_card_tool.c:2723-2728`; `Launchstation/src/dash_card_tool.c:1050-1055`; `Launchstation/src/dash_card_tool.c:1443-1450`; `Launchstation/src/dash_card_tool.c:987`; `Launchstation/src/dash_card_tool.c:1403`; `Launchstation/src/dash_card_tool.c:1511`; `Launchstation/src/dash_card_tool.c:1579`; `Launchstation/src/dash_card_tool.c:1592-1595`; `Launchstation/src/dash_card_tool.c:1757-1760`; `Launchstation/src/dash_card_tool.c:2407-2410`; `Launchstation/src/dash_card_tool.c:1369-1370`
- Problem: File stem without extension is cut three times (275-280, 1043-1048, 1275-1280); a trailing '/' is appended to a folder three times (1433-1440, 2187-2194, 2723-2728); the -N free-name loop twice (1050-1055, 1443-1450); the size list is declared twice (987 is the first 7 entries of 1403); the read-only string twice (1511, 1579); the icon.sys-title-or-folder-name choice three times; and ctSplitGameId copies a string literal into msg[160] only to pass it on (1369).
- Fix: Add two static helpers (stem of a name, folder with trailing slash), use one sizes[] table with a count, one read-only string, one title helper, and pass the literal at 1369 directly (dropping msg from ctSplitGameId).
- Justification: Each helper replaces three in-file copies, so the file gets shorter.
- Benefit: ~30 lines removed (INFERRED from the cited ranges); static code, small .text change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-140: Two-way swapKeys sprintf pairs for tooltips differ only in the button glyph

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_browser.c:640-649`; `Launchstation/src/filer_browser.c:755-777`; `Launchstation/src/filer_browser.c:1928-1941`; `Launchstation/src/filer_browser.c:1954-1969`
- Problem: Four tooltips are written twice, once per swapKeys value; the two strings differ only in whether the OK glyph is '\xFF0' or '\xFF1' (and the cancel/mark glyph the other way). gui_neutrino.c:497 already does the same thing with one format and swapKeys ? '1' : '0'.
- Fix: Write each tooltip once with %c glyph arguments computed from swapKeys, as gui_neutrino.c:497 does.
- Benefit: ~30 source lines removed; four format strings fewer in .rodata (INFERRED, ~250 bytes)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-141: VCD game-ID lookup opens the VCD twice more after validateVcd through single-use readVcd* wrappers

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/popstarter_vcd.c:246-257`; `Launchstation/src/popstarter_internal.h:53-54`; `Launchstation/src/popstarter_gameid.c:102-123`; `Launchstation/src/popstarter_gameid.c:190-226`; `Launchstation/src/popstarter.c:77-81`
- Problem: popstarter.c:77 opens the VCD in validateVcd, then buildPopstarterVcdGameID opens it again in buildVcdSystemCnfGameID and a third time in buildVcdPvdTimestampGameID, each via openPathForRead (which for hdd:pfs paths calls loadHddModules and mountParty, popstarter_path.c:265-267). readPs1SystemCnf already reads the PVD, so the PVD is read twice. readVcdIsoPrimaryVolumeDescriptor and readVcdSystemCnf exist only to build a PS1_IMAGE_READER for these two single callers.
- Fix: In buildPopstarterVcdGameID open the VCD once, build one PS1_IMAGE_READER {fd, POPSTARTER_VCD_IMAGE_OFFSET, POPSTARTER_VCD_RAW_SECTOR_SIZE}, try readPs1SystemCnf + parseSystemCnfBootGameID, then readPs1IsoPrimaryVolumeDescriptor + timestamp, close, then the name fallbacks. Delete the two readVcd* wrappers, their prototypes and the two static open-per-step helpers.
- Benefit: ~30 lines removed (12 wrapper lines, 2 prototypes, the two open helpers collapse into one); one fewer open/mount and one fewer PVD sector read per VCD launch (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-142: cloudGoogleRecoverSnapshot and cloudGoogleRecoverSaveVersion copy the same claim/session/download/digest sequence

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/cloud_google.c:1004-1054`; `Launchstation/src/cloud_google.c:1114-1161`; `Launchstation/src/cloud_google.c:1082-1089`; `Launchstation/src/cloud_google.c:1177-1181`
- Problem: Both functions run the same steps with the same error strings: claim a RECOVER-CLOUD name, check result length, start the storage device, load the session, start the network, refresh the token, build the alt=media URL, re-check the names, open the .DOWNLOAD.PART file, call cloudHttpDownloadGoogleFd, close it and compare the SHA-256. The only differences are the extension, the size cap (CLOUD_RECOVERY_MAX_BYTES vs MMCE_SAVE_VERSION_MAX) and one word in an error message. About 45 lines appear twice.
- Fix: Move the shared sequence into one static function that takes the item, dest_dir, extension and max_bytes, fills result/stage/download, and returns after the digest check. Each public function keeps only its format checks and publish step.
- Benefit: ~45 duplicated lines become one copy plus a ~10 line signature and two calls: ~30 lines net (INFERRED). .text size change not measured.
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-143: configSaveTargetPrompt computes default_sel that is never read and formats every line twice; save-path selection repeated

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/config.c:1374`; `Launchstation/src/config.c:1386-1414`; `Launchstation/src/config.c:1419-1426`; `Launchstation/src/config.c:1591-1598`; `Launchstation/src/config_network.c:236-245`; `Launchstation/src/config.c:1258-1268`
- Problem: default_sel is set in the loop at 1403-1414 but the selection comes from configSelectInitialSaveTarget at 1444; lines[] is filled with an unbounded width at 1388-1399 and then overwritten at 1419-1426. Both callers re-implement the target-to-path mapping that the static configGetSaveTargetPath (1258-1268) already does. Moot if finding 1 is applied.
- Fix: Delete default_sel and its loop; compute max_chars first and format each line once; let callers use configGetSaveTargetPath (make it non-static) instead of their own if/else chains.
- Benefit: ~30 lines removed (INFERRED from cited ranges); a little .text.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-144: dash_clock_sync.c keeps two copies of BCD, day-count and RTC write/verify code

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_clock_sync.c:251-280`; `Launchstation/src/dash_clock_sync.c:294-312`; `Launchstation/src/dash_clock_sync.c:377-385`; `Launchstation/src/dash_clock_sync.c:401-412`; `Launchstation/src/dash_clock_sync.c:438-474`
- Problem: manualToBcd/manualFromBcd (377-385) are byte-identical to toBcd/fromBcd (251-259), which sit inside #ifdef ETH; manualSerialSeconds (401-412, loop over years) recomputes what civilDays does; dashClockSetManual's set/write/read-back/decode/compare (438-474) repeats writePs2Clock + clockLocalSeconds (282-312, 261-280).
- Fix: Move toBcd, fromBcd, civilDays and clockLocalSeconds above the #ifdef ETH, delete the manual* copies and manualSerialSeconds, and verify the manual write by comparing clockLocalSeconds(&check) with the seconds of the raw_* RTC values (one shared write-and-verify helper for both paths). Update tests/test_clock_field_editor.py, which extracts manualLeap, only if it is touched.
- Benefit: ~30 lines removed (INFERRED from the cited ranges); static functions, so a few hundred bytes of .text at most.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-145: dash_files.c list mode keeps a dead FE_DEVICE path and an unused 4.5 KB fdevs[] array

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_files.c:164`; `Launchstation/src/dash_files.c:181-182`; `Launchstation/src/dash_files.c:212`; `Launchstation/src/dash_files.c:288-291`; `Launchstation/src/dash_files.c:319`; `Launchstation/src/dash_files.c:325-326`; `Launchstation/src/dash_files.c:332-342`; `Launchstation/src/dash_files.c:374-378`; `Launchstation/src/dash_files.c:392-403`
- Problem: filesRebuild never adds an FE_DEVICE entry (it only sets nfdevs = 0 at line 212, 'Devices are the alternate mode'), so fdevs[] is never filled and every FE_DEVICE branch in filesActivate, the row dot and the detail panel is unreachable. The 1-second periodic filesRebuild in filesUpdate (comment: 'device states change') and the 'Refresh devices' menu item only re-read LastDir/PathPad, which already rebuild on enter and after every activation.
- Fix: Delete FE_DEVICE, fdevs, nfdevs and the three FE_DEVICE branches (keep the else body of the detail panel), delete the timer part of filesUpdate (keep the fsel_anim line) and the 'Refresh devices' menu item.
- Benefit: ~30 lines; 4512 bytes of .bss RAM (INFERRED: sizeof(DashDevice) = 4+4+32+24+24+48+48+4 = 188 bytes x DASH_MAX_DEVICES 24); one needless list rebuild per second removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-146: dash_sound.c internal duplication: twin blob switches, repeated volume loop, BGM state reset and boot-dir expression

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_sound.c:82-108`; `Launchstation/src/dash_sound.c:214-217`; `Launchstation/src/dash_sound.c:326-327`; `Launchstation/src/dash_sound.c:243-248`; `Launchstation/src/dash_sound.c:295-300`; `Launchstation/src/dash_sound.c:138`; `Launchstation/src/dash_sound.c:261`; `Launchstation/src/dash_sound.c:410`; `Launchstation/src/dash_sound.c:508`; `Launchstation/src/dash_sound.c:166`; `Launchstation/src/dash_sound.c:12`
- Problem: blob_data and blob_size are two 13-line switches over the same ids; the SFX volume loop in dashSoundInit equals dashSoundApplyVolume; the six-line BGM state reset (bgm_loaded, bgm_tried, bgm_channel, buffer, free) is copied in ReloadEffects and Shutdown; 'LaunchElfBootDir[0] ? LaunchElfBootDir : LaunchElfDir' is written four times; 'sound_ready && soundAlive()' repeats soundAlive's own test; iopcontrol.h is included but no SifIop* call is made.
- Fix: Replace the two switches with one static table of {data, &size} indexed by id; set sound_ready/generation first and call dashSoundApplyVolume instead of the init loop; move the BGM reset into one static helper; add one static bootDir() helper; test only soundAlive() at 166; drop the iopcontrol.h include.
- Benefit: ~30 source lines removed; small .text saving from one table instead of two switch functions (INFERRED, well under 1 KB).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-147: editor.c: NEW and OPEN menu branches are copies, and the open-window count loop is written five times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/editor.c:270-316`; `Launchstation/src/editor.c:330-334`
- Problem: The ret == NEW and ret == OPEN branches (270-292 and 293-316) are identical except for editorNew vs editorOpenFile and one drawMsg. The loop 'Num_Window = 0; for i < 10 if Window[i][OPENED] Num_Window++' appears at 271-275, 288-292, 295-299, 312-316 and 330-334, and the first-free-window search twice (277-282, 301-306).
- Fix: Add one static countOpenWindows() and merge NEW/OPEN into one branch that picks editorNew or editorOpenFile.
- Justification: The helper replaces five copies of the same loop and the merge removes a 23-line duplicate branch; net source shrinks.
- Benefit: ~30 lines removed (INFERRED from cited ranges); small .text saving (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-148: kelf_signer.c 3DES layer: des_cbc_crypt only ever runs one block with a zero IV, key_count 3 is never used, error returns are never checked

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/kelf_signer.c:396-423`; `Launchstation/src/kelf_signer.c:425-481`
- Problem: tdes_cbc_crypt calls des_cbc_crypt with size 8 and a zero IV for each stage of each block, so the CBC loop inside des_cbc_crypt is unused and subkeys are recomputed every 8 bytes. All 14 callers pass key_count 1 or 2, so the key_count == 3 branches (452-455, 470-473) are dead, and no caller checks the -EINVAL return (all sizes are constant multiples of 8).
- Fix: Delete des_cbc_crypt; in tdes_cbc_crypt build the one or two subkey schedules once and call des_crypt_block directly per stage; drop the key_count 3 branches and make the function void.
- Benefit: ~30 source lines removed and fewer des_make_subkeys calls per signing (INFERRED); a few hundred bytes of .text at most (INFERRED)
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-149: dashOkLabelKey and dashDrawFocusFrame are never called

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_ui.c:176-179`; `Launchstation/src/dash_ui.c:611-633`; `Launchstation/include/dash_internal.h:884`; `Launchstation/include/dash_internal.h:1130`
- Problem: The only matches for dashOkLabelKey and dashDrawFocusFrame in the whole repo (src, include, tests, docs) are their definitions in dash_ui.c and their prototypes in dash_internal.h.
- Fix: Delete both functions and both prototypes.
- Benefit: ~29 lines removed; no ELF change: BUILD-EVIDENCE.md lists dashDrawFocusFrame (524 bytes) among --gc-sections discards (MEASURED), dashOkLabelKey is likewise unreferenced
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-150: Config_Advanced repeats the whole option-toggle chain for the cancel and OK buttons

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/config_advanced.c:119-150`; `Launchstation/src/config_advanced.c:151-182`
- Problem: Lines 123-150 and 155-182 are the same 28-line if/else chain; the only difference is the Hide HDD direction (-1 vs +1). Moot if finding 1 is applied.
- Fix: Merge the two button branches into one that sets a direction (+1/-1) from the button pressed and runs the chain once.
- Benefit: ~28 lines removed; a few hundred bytes of .text (INFERRED: one copy of 12 compare/toggle cases).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-151: Four virtual keyboard char getters return the same value; getVirtualKeyboardEditorDisplayChar is unused

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/virtual_keyboard.c:193-205`; `Launchstation/src/virtual_keyboard.c:216-219`; `Launchstation/src/virtual_keyboard.c:371-378`; `Launchstation/include/launchelf.h:696-698`; `Launchstation/include/launchelf.h:703`; `Launchstation/src/dash_widgets.c:19-22`; `Launchstation/src/dash_widgets.c:222-224`; `Launchstation/src/dash_widgets.c:315-316`
- Problem: getVirtualKeyboardLayoutDisplayChar and getVirtualKeyboardLayoutChar have identical bodies, getVirtualKeyboardFilenameChar only forwards to LayoutChar, and getVirtualKeyboardEditorDisplayChar (no caller; equal to EditorChar) is the lead. dash_widgets.c then chooses between FilenameChar and LayoutChar/DisplayChar in two ternaries that yield the same char, and kbFilenameChar only forwards to isVirtualKeyboardFilenameChar.
- Fix: Keep getVirtualKeyboardLayoutChar; delete LayoutDisplayChar, FilenameChar and EditorDisplayChar with their prototypes; replace callers (editor.c:514, gui.c:356, dash_widgets.c:83/223/315) with LayoutChar and drop the two ternaries and kbFilenameChar. Update tests/test_editor_keyboard.py:494.
- Benefit: ~28 source lines. MEASURED: EditorDisplayChar (380 B) already gc-discarded (gc-discarded-first-party.tsv:99); the rest INFERRED a few tens of bytes.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-152: Small simplifications in filer_actions.c timestamp and path helpers

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_actions.c:1322-1341`; `Launchstation/src/filer_actions.c:1627-1631`; `Launchstation/src/filer_actions.c:1530-1536`; `Launchstation/src/filer_actions.c:1502-1505`; `Launchstation/src/filer_actions.c:153-162`; `Launchstation/src/filer_actions.c:2176-2181`
- Problem: (a) time_manip_custom_run casts _Modify to PS2TIME and copies six fields into an sceMcStDateTime of the same layout, with Resv2=0 duplicated in both branches. (b) In buildSasTimestampPayload every category other than 1 and 9 except exact 'SYS' has '_' at index 3, so the 11 strncmp calls equal effective[3]=='_'. (c) setSequentialTimestamp repeats setTunaFolderTimestamp then sets Sec=58. (d) compareTimestampFolderNames only forwards to stricmp for one caller. (e) filerBuildFullPath has one caller with a fixed buffer; its NULL/size guard and forced terminator after snprintf cannot matter. (f) Rename computes the same separator expression twice.
- Fix: (a) Copy the struct, set Resv2=0 and reset to 2000-01-01 only when out of range. (b) Test effective[3]=='_'. (c) Call setTunaFolderTimestamp then set Sec=58. (d) Call stricmp directly. (e) Drop the guard and terminator line. (f) Compute the separator once.
- Benefit: ~28 lines removed in total ((a) ~12, (b) 3, (c) 5, (d) 4, (e) 3, (f) 2); negligible ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-153: Three hand-rolled case-insensitive prefix compares duplicate strncasecmp (dash_lib.c prefixMatchNoCase and two others)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_lib.c:741-760`; `Launchstation/src/main_exploit_installer.c:313-328`; `Launchstation/src/popstarter_path.c:171-182`
- Problem: prefixMatchNoCase (dash_lib.c), starts_with_model (main_exploit_installer.c) and prefixCaseCmp (popstarter_path.c) each loop with wle_ascii_tolower to test a case-insensitive prefix. That is exactly strncasecmp(s, prefix, strlen(prefix)) (or the given length), which newlib provides and the repo already uses (dash_main.c:403, dash_themes.c:910, mmce_card_switch.c:68).
- Fix: Replace the bodies with strncasecmp: in dash_lib.c call !strncasecmp(path, dirs[i], strlen(dirs[i])) inside dirPrefixMatch and delete prefixMatchNoCase; make starts_with_model and prefixCaseCmp one-line strncasecmp calls (or inline them at their callers).
- Benefit: ~30 lines removed across three files (INFERRED from cited ranges); a few hundred bytes of duplicated loop code, offset by strncasecmp already being linked (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-154: dashCfg.enabled and dashCfg.legacy_theme are always 1: unreachable settings cases, dead DashIsEnabled, redundant keep_enabled

- Category: 8 (Half-finished or redundant features)
- Location(s): `Launchstation/src/dash_store.c:239`; `Launchstation/src/dash_store.c:247`; `Launchstation/src/dash_store.c:308-309`; `Launchstation/src/dash_store.c:324-325`; `Launchstation/src/dash_store.c:516`; `Launchstation/src/dash_store.c:524`; `Launchstation/src/dash_store.c:548-550`; `Launchstation/include/dash_internal.h:572`; `Launchstation/include/dash_internal.h:580`; `Launchstation/src/dash_settings.c:44`; `Launchstation/src/dash_settings.c:52`; `Launchstation/src/dash_settings.c:1018-1023`; `Launchstation/src/dash_settings.c:1697-1703`; `Launchstation/src/dash_settings.c:2753-2759`; `Launchstation/src/dash_main.c:200-204`; `Launchstation/include/dash.h:18`
- Problem: Defaults set both fields to 1 and the parser forces 1 whatever the file says ('the dashboard is the only shell now', 'no unstyled classic look'). The only writes of other values are in the S_ENABLED / S_LEGACY_THEME switch cases, but neither id appears in any settings item table (grep: enum, two cases each), so they are unreachable. The only reader of enabled is DashIsEnabled, which has no caller; legacy_theme is read only in the unreachable case and written to DASHBOARD.CNF.
- Fix: Delete both fields, the S_ENABLED/S_LEGACY_THEME enum values and their four switch cases, DashIsEnabled and its prototype, the keep_enabled save/restore, and the Dash_Enabled/Dash_LegacyTheme parse branches and output lines (dashKvParse already skips unknown keys in old files).
- Benefit: ~28 source lines removed, two fewer keys written per DASHBOARD.CNF save. DashIsEnabled already gc-discarded (MEASURED: wLaunchELF.gc-sections.log:438); remaining code is a few hundred bytes of .text/.rodata at most (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-155: mcSaveFormatExt and mcSaveFormatName are never called

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/mc_save_formats.h:60-62`; `Launchstation/src/mc_save_formats.c:878-902`
- Problem: Grep over the whole repo finds the two names only at their definitions (mc_save_formats.c:878, 891) and prototypes (mc_save_formats.h:61-62); no source, host test or Python test calls them, and the link garbage-collects both (56 bytes each).
- Fix: Delete both functions and their prototypes and comment.
- Benefit: ~28 lines removed. Code already gc'd (MEASURED: gc-discarded-first-party.tsv lines 67-68); their string literals stay in .rodata without -fdata-sections (INFERRED ~150 bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-156: mmceImportCandidate duplicates recoveryTargetPath and re-checks what its single caller guarantees

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_browser.c:1813-1836`; `Launchstation/src/dash_browser.c:1842-1865`; `Launchstation/src/dash_browser.c:1923-1925`; `Launchstation/src/dash_browser.c:1999-2001`
- Problem: mmceImportCandidate is recoveryTargetPath without the USB/HDD prefix test: the same RECOVER-%03d.mcd + .PART free-name loop. It also rebuilds the folder string its only caller already built at 1923, and both it and mmceSelectableCandidate check unit range and NULL/size arguments that their single callers (stack arrays, unit from a 2-entry choice) cannot violate.
- Fix: Move the USB/HDD prefix test from recoveryTargetPath into recoverCardFromPath, call recoveryTargetPath(folder, ...) from opImportCardToMmce and delete mmceImportCandidate; drop the impossible argument checks in mmceSelectableCandidate. tests/test_mmce_native_import.py exercises mmceImportCandidate and must follow.
- Benefit: ~28 lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-157: Leftover 'temporary hardware diagnostic' PAD status overlay on USB folders

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_browser.c:56-58`; `Launchstation/src/dash_browser.c:3394-3402`; `Launchstation/src/dash_browser.c:3457-3469`; `Launchstation/src/dash_browser.c:3560`; `Launchstation/src/dash_browser.c:3653`
- Problem: browserDrawBody prints raw pad DMA state, request state, IOP reset generation and worker-busy flag under every USB listing; the comment calls it a temporary diagnostic for an already completed USB input-loss report (commit 110be3f). browserSnapshotPad polls padGetState/padGetReqState every frame in the Files tab only to feed it, and fb.pad_* fields exist only for it. No test references it.
- Fix: Delete the overlay block, browserSnapshotPad and its two calls, and the pad_state/pad_request/pad_generation/pad_snapshot_valid fields.
- Benefit: ~27 lines removed, one per-frame pad state read pair and one snprintf per frame on USB folders gone; small .text saving (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-158: convertCustomTimestampToLocalTime and convertCustomTimestampFromLocalTime are the same function

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_actions.c:480-538`
- Problem: The two 29-line functions are identical except for the converter they call (menuTitleConvertTimestampToLocalTime vs ...FromLocalTime, which have the same signature in include/main_title.h:20-21).
- Fix: Keep one static function that takes the converter as a parameter (or pass a direction flag), and call it from the five existing call sites.
- Benefit: ~27 lines removed; small .text saving (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-159: HDD path-to-partition parsing exists three times (getHddParty, getHddDVRPParty, filer_mount getHddPartyFromPath)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/gui_hdd0_format.c:15-75`; `Launchstation/src/filer_mount.c:190-207`
- Problem: getHddParty and getHddDVRPParty repeat the same fullpath build and partition split (22-30 vs 49-57) differing only in the prefix length (6 vs 10) and output prefix. filer_mount.c getHddPartyFromPath re-implements the hddN: check of getHddDeviceFromPath and the party split of getHddParty(path, NULL, party, NULL) for clipboardUsesHddParty.
- Fix: Replace getHddPartyFromPath with a call to getHddParty(clipPath, NULL, clipParty, NULL) plus an empty-name check, and share the fullpath/split block of the two gui_hdd0_format.c functions through one static helper taking the prefix length.
- Benefit: ~25 lines removed (18-line filer_mount copy plus ~8 shared lines); small .text saving (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-160: Same dialog message split/measure/centre block in ynDialog, nonDialog and main_exploit_installer ok_result_dialog

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/gui.c:20-40`; `Launchstation/src/gui.c:111-131`; `Launchstation/src/main_exploit_installer.c:804-825`
- Problem: Three copies of the same ~19 lines: split the message at newlines, measure the widest line with printXY, clamp to 96 and compute dh/dw/dx/dy (nonDialog uses 16*n, the others FONT_HEIGHT*(n+1)).
- Fix: Move the split-and-measure loop into one helper in gui.c (returning the line count and width) and call it from the three dialogs.
- Justification: Three identical copies; a ~12-line helper and three 1-2 line calls remove code.
- Benefit: ~25 source lines (INFERRED: 3 x ~13 duplicated lines replaced by a 12-line helper and 3 calls).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-161: Small in-file duplicates in main_exploit_installer.c (signing call, card-write tail, file-exists test, folder_assets_to_write)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/main_exploit_installer.c:1543-1551`; `Launchstation/src/main_exploit_installer.c:1432-1441`; `Launchstation/src/main_exploit_installer.c:1403-1405`; `Launchstation/src/main_exploit_installer.c:1910-1912`; `Launchstation/src/main_exploit_installer.c:1493-1500`; `Launchstation/src/main_exploit_installer.c:1785-1797`; `Launchstation/src/main_fileops.c:7-16`; `Launchstation/src/main_exploit_installer.c:1648-1664`; `Launchstation/src/main_exploit_installer.c:1764-1783`
- Problem: export_custom_payload repeats the keyset drawMsg and KelfSignElfToMemory call of build_custom_payload_to_memory (header_id resolves the same for PS2). write_signed_payload and install_system_xlf_targets inline the build_mc_path/drawMsg/write_blob body of write_custom_payload_buffer. mc_boot_elf_exists re-implements wleExists. prepare_needed_folders recomputes folder_assets_to_write with the result build_folder_assets_to_write already gives.
- Fix: Call build_custom_payload_to_memory(custom, KELF_SIGN_SYSTEM_PS2, ...) from export_custom_payload; use write_custom_payload_buffer at 1403 and 1910; replace mc_boot_elf_exists with snprintf + wleExists; call build_folder_assets_to_write in prepare_needed_folders before the wipe.
- Benefit: ~25 lines removed (INFERRED from cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-162: Theme idle-save chain is dead: dashThemeMarkDirty has no caller, so dashThemeSavePending and dashStoreSaveTheme never act

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_themes.c:1200`; `Launchstation/src/dash_themes.c:1279-1292`; `Launchstation/src/dash_store.c:694-697`; `Launchstation/include/dash_internal.h:298-299`; `Launchstation/include/dash_internal.h:641`; `Launchstation/src/dash_main.c:325`; `Launchstation/src/dash_main.c:1485`
- Problem: theme_dirty is set to 1 only in dashThemeMarkDirty, which nothing calls, so dashThemeSavePending (called every UI frame at dash_main.c:1485 and before hand-off at :325) always returns at its first test, and its call to dashStoreSaveTheme (only caller) never runs. Theme edits are saved by settingsCommit via dashStoreSaveSettings (dash_settings.c:2421-2423, 2502-2504), so this path is a leftover.
- Fix: Delete theme_dirty, dashThemeMarkDirty, dashThemeSavePending, dashStoreSaveTheme, their three prototypes and the two calls in dash_main.c (settleBackgroundWork keeps only the dashStoreFlushWait call).
- Benefit: ~25 source lines removed; one call per UI frame removed. dashThemeMarkDirty is already gc-discarded (MEASURED: wLaunchELF.gc-sections.log:558); dashThemeSavePending and dashStoreSaveTheme stay linked today, a few dozen bytes (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-163: libhdd hddUpdateInfo walks every APA partition entry only to compute hddFree, which nothing reads

- Category: 5 (Performance)
- Location(s): `Launchstation/ee_libs/hdd/libhdd.c:262-293`; `Launchstation/ee_libs/hdd/libhdd.c:75-102`; `Launchstation/ee_libs/hdd/libhdd.c:253-260`; `Launchstation/src/dash_hdd.c:151-155`
- Problem: hddUpdateInfo issues one fileXioDread per partition entry (main and sub) to compute hddFree, read only by hddGetInfo, which has no caller. hddCheckPresent triggers it on the HDD Manager's first scan, so that scan walks the directory twice (scanRun walks it again at dash_hdd.c:167-209) and sends HDIOC_STATUS three times. hddMakeFilesystem and hddFormat repeat the walk after each create/format, and dash_hdd rescans anyway.
- Fix: Delete the directory walk and hddSize/hddFree from hddUpdateInfo (keep the HDIOC_MAXSECTOR devctl that hddMakeFilesystem needs) together with the uncalled hddGetInfo. In scanRun, derive present/formatted from the HDIOC_STATUS value it already holds (present: 0..2, formatted: 0), not from the two libhdd calls.
- Benefit: Removes one full APA directory walk (one Dread RPC plus header read per partition entry, hundreds on a game HDD) from the first HDD Manager scan and from every create/format, and 2 HDIOC_STATUS RPCs per scan. Worker thread, behind the loading overlay (INFERRED timing). ~25 lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-164: ps2ips RPC socket shim (externs, hostent/sockaddr views, htons) declared in both dash_clock_sync.c and cloud_http.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_clock_sync.c:24-47`; `Launchstation/src/dash_clock_sync.c:95-98`; `Launchstation/src/cloud_http.c:37-71`
- Problem: The seven ps2ipc_* extern prototypes, the SntpHostEnt/CloudHostEnt and SntpSockaddrIn/CloudSockaddrIn structs and sntpHtons/cloud_htons are the same declarations under different names; cloud_http.c additionally declares ps2ipc_ioctl and ps2ipc_getpeername.
- Fix: Put the shared prototypes, the two structs and the byte swap in one small header (or in cloud_http.h) and include it from both files; delete the private copies.
- Justification: A new header of ~25 lines replaces ~55 duplicated lines in two files; the copies must otherwise be kept in step with the PS2IP RPC ABI by hand.
- Benefit: ~25 lines removed net (INFERRED: 28 + 35 duplicated lines minus a ~28-line header); no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-165: switchPsxHddDriverStack does nothing in release builds (only early returns and DPRINTF)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/init.c:2329-2347`; `Launchstation/src/init.c:387`; `Launchstation/src/init.c:2666-2668`; `Launchstation/src/init.c:2719`
- Problem: The body of switchPsxHddDriverStack only tests flags and returns, or calls DPRINTF, which launchelf.h:164 defines as empty in shipped builds. It changes no state and loads nothing, so its two calls in loadHddModules and loadDVRPHddModules are no-ops. Its name suggests a stack switch that was removed.
- Fix: Delete the function, its prototype and both calls (with the #ifdef DVRP wrapper around the call in loadHddModules).
- Benefit: ~25 lines removed; the static function is probably already inlined to nothing, so no measurable ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-166: Size-estimate error plumbing for embedded blobs that cannot be missing (add_size_kb and repeated blob checks)

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_exploit_installer.c:591-631`; `Launchstation/src/main_exploit_installer.c:1834-1852`; `Launchstation/src/main_exploit_installer.c:1366-1369`
- Problem: All payloads and icon.sys files are .incbin blobs with build-time sizes (embed_blobs.S:53-63) and get_payload/get_icon_sys always set data/body consistently for every enum value used, yet payload_size_kb, folder_asset_file_size_kb and sign_payload_to_memory re-check for NULL/zero/inconsistent blobs, and add_size_kb exists only to forward these impossible negative values through four call sites.
- Fix: Have payload_size_kb and folder_asset_file_size_kb return the KB count directly, delete add_size_kb and the four error checks in estimate_install_file_kb, and drop the repeated blob checks in sign_payload_to_memory.
- Benefit: ~25 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-167: normalizeCustomLocalTimestamp repeats normalizeCustomFolderTimestamp; stepCustomFolderTimestamp repeats decrementSasTimestampDate

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_actions.c:540-559`; `Launchstation/src/filer_actions.c:578-596`; `Launchstation/src/filer_actions.c:649-659`; `Launchstation/src/filer_actions.c:1683-1696`
- Problem: normalizeCustomLocalTimestamp is the month/day/hour/minute/second clamp of normalizeCustomFolderTimestamp (14 identical lines) with only a different year bound (max 2100, no minimum). The day/month/year borrow at the end of stepCustomFolderTimestamp (649-659) is the body of decrementSasTimestampDate (1685-1695).
- Fix: Give the folder normaliser year bounds as parameters (or clamp the year in the caller) and delete normalizeCustomLocalTimestamp; move decrementSasTimestampDate above stepCustomFolderTimestamp and call it there.
- Benefit: ~24 lines removed (19-line function minus 2 for parameters, 11-line borrow replaced by 1 call); negligible ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-168: Classic screen-defaults block copied in resetScreen, resetDisplayThemeDefaults and config_screen.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_settings.c:2770-2783`; `Launchstation/src/dash_settings.c:2791-2804`; `Launchstation/src/config_screen.c:166-179`; `Launchstation/src/config.c:845-855`
- Problem: The same 14 statements (eight DEF_COLORn colours, TV_mode Auto, screen_x/y = 0, Menu_Frame, Popup_Opaque, updateScreenMode) appear three times. Both dash_settings.c copies and config_screen.c (compiled, ps2.yaml:514) match line for line. config.c's defaults function sets the same fields in a slightly different order.
- Fix: Keep one copy as a non-static function next to the defaults in config.c (or as a static function in dash_settings.c if only the two local copies are merged). Then make resetScreen, resetDisplayThemeDefaults and the config_screen.c DEFAULT branch call it.
- Justification: The helper replaces three identical 14-line blocks: 42 lines become about 16 (one function) plus 3 calls, a net removal of about 23 lines, and the reset defaults can no longer drift apart.
- Benefit: ~23 lines removed net if all three copies share it (~11 if only the two in dash_settings.c); a few hundred bytes of .text (INFERRED: three inlined copies of 13 stores and a call)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-169: Redundant code paths in dash_main.c: unreachable MISC check, duplicated firstInit branch, always-1 return, one-line poll wrappers

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_main.c:398-399`; `Launchstation/src/dash_main.c:1292-1300`; `Launchstation/src/dash_main.c:513-537`; `Launchstation/src/dash_apps.c:196-197`; `Launchstation/include/dash_internal.h:1122`; `Launchstation/src/dash_main.c:796-810`
- Problem: runMiscCommand's Configure/Exploit check (398-399) cannot match: its only caller dashRunMisc returns for both at 456-460 and 474-477. In firstInit `dashAppCount >= 0` is always true and both branches call dashDevicesAutoStart. dashOpenTextEditor always returns 1, so the check at dash_apps.c:196 never fires and the prototype comment ('0 = the user backed out') is wrong. pollPcGameReceiver/pollRemotePcCommand are 6-line wrappers adding the same HOME test, each called once (1459-1460).
- Fix: Delete 398-399. In firstInit call dashDevicesAutoStart() once and then dashRescanLibrary(0) when dashLibLoadCache() < 0 || dashCfg.autoscan. Make dashOpenTextEditor void and drop the dash_apps.c:196 test. Replace the two wrappers with one `if (section == DASH_SEC_HOME)` around the two Impl calls at 1459-1460.
- Benefit: ~22 lines removed, no behaviour change; a few dozen bytes of .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-170: dash_png.c: box-filter accumulate and row-flush code written twice/three times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_png.c:155-177`; `Launchstation/src/dash_png.c:180-202`; `Launchstation/src/dash_png.c:205-209`
- Problem: The interlaced and non-interlaced branches contain the same 21-line per-row loop (flush destination row on dy change, then accumulate the source row); they differ only in where the row comes from (memcpy from `full` vs png_read_row). The 4-line flush body appears a third time for the final row.
- Fix: Use one row loop: take the row from `full` (point px at it, no memcpy) when the image is interlaced, else png_read_row into `row`; put the flush in one small static function used inside the loop and after it.
- Benefit: ~22 lines removed and one copy of the loop code (a few hundred bytes of .text, INFERRED); also drops a w*4-byte memcpy per row for interlaced images.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-171: folder_for_asset switch and six NULL checks that cannot fire; folder names stored twice

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_exploit_installer.c:467-481`; `Launchstation/src/main_exploit_installer.c:114-125`; `Launchstation/src/main_exploit_installer.c:1257-1258`; `Launchstation/src/main_exploit_installer.c:1281-1282`; `Launchstation/src/main_exploit_installer.c:1662-1663`; `Launchstation/src/main_exploit_installer.c:1748-1749`; `Launchstation/src/main_exploit_installer.c:1779-1780`; `Launchstation/src/main_exploit_installer.c:1932-1933`
- Problem: folder_for_asset is a 15-line switch mapping the four FOLDER_ASSET values to fixed names, and every caller loops i < FOLDER_ASSET_COUNT, so its NULL return and the six 'if (folder == NULL) continue' checks can never happen. The same four names are also repeated as string fields in all ten ps2_targets rows, next to the asset index that already identifies them.
- Fix: Replace the switch with a static const char *const array indexed by asset, delete the six NULL checks, and drop the folder field from ps2_targets in favour of the array lookup by .asset.
- Benefit: ~22 lines removed; ten pointer fields (40 bytes) of ps2_targets dropped (INFERRED: 10 x 4-byte pointers)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-172: editor_file.c: editorSave/editorSaveAs repeat the write block with a never-true CRLF branch; 1.5 s busy-wait loop pasted 5 times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/editor_file.c:226-238`; `Launchstation/src/editor_file.c:292-305`; `Launchstation/src/editor_file.c:58-60`; `Launchstation/src/editor_file.c:181-183`; `Launchstation/src/editor_file.c:207-209`; `Launchstation/src/editor_file.c:249-251`; `Launchstation/src/editor_file.c:318-320`
- Problem: The open/write/close/unmountParty block is duplicated between editorSave and editorSaveAs. Its test TextBuffer[Win][TextSize[Win]] == '\n' reads the terminator slot (the buffer is zeroed at load, 107/119, and every edit re-terminates with strcpy/strcat at TextSize), so the TextSize-1 branch at 229-230 and 295-296 never runs. The 'WaitTime = Timer(); while (Timer() < WaitTime + 1500);' result delay is copied five times in this file.
- Fix: Move the write block into one static helper used by both functions, write TextSize bytes unconditionally, and replace the five delay loops with one static helper (or a single delay at the common exit).
- Justification: The helpers each replace two to five identical copies, so the file gets shorter.
- Benefit: ~22 source lines removed (INFERRED: 10 from the write block, 2x2 dead branch, 8 from the delay copies).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-173: dashGfxShutdown and dashFontShutdown have no callers

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_gfx.c:362-371`; `Launchstation/src/dash_font.c:307-314`; `Launchstation/include/dash_internal.h:334`; `Launchstation/include/dash_internal.h:404`
- Problem: Both functions appear only at their definitions and prototypes; the dashboard never shuts down (DashRun does not return) and the linker discards both. The forward declaration of facesShutdown at dash_font.c:307 exists only so dashFontShutdown can call it; facesInit, its other user, is defined after facesShutdown.
- Fix: Delete dashGfxShutdown, dashFontShutdown, their two prototypes and the facesShutdown forward declaration.
- Benefit: ~21 lines removed; no ELF change because --gc-sections already drops .text.dashGfxShutdown (180 B) and .text.dashFontShutdown (120 B) (MEASURED: raw/link/wLaunchELF-gc-discarded-first-party.tsv, raw/link-raw/wLaunchELF.gc-sections.log)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-174: Five exported dash_ui.c functions only forward to a static twin

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_ui.c:241-244`; `Launchstation/src/dash_ui.c:862-880`
- Problem: dashButtonWidth, dashModalBackdrop, dashModalPanel, dashModalHintRow and dashDrawButton each just call the static buttonWidth, modalBackdrop, modalPanel, modalHintRow and drawButton with the same arguments. Each operation exists under two names.
- Fix: Remove static from the five originals and give them the public names (or keep the public names and delete the static ones, renaming the in-file calls). Update the source-slicing host tests that look the static names up (tests/test_editor_keyboard.py:240, test_file_operation_overlay.py:63,84, test_modal_animation.py:101, test_hint_bar_fit.py:32-33, test_hint_order.py:36-37, test_pad_icon_order.py:23).
- Benefit: ~20 source lines removed; ELF effect a few tail-call stubs (tens of bytes, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-175: Small dead or redundant bits in cloud_google.c: unused macro, forwarder, constant parameter, duplicate filter, dead fd test, needless copy

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/cloud_google.c:31`; `Launchstation/src/cloud_google.c:205-212`; `Launchstation/src/cloud_google.c:243-246`; `Launchstation/src/cloud_google.c:684-695`; `Launchstation/src/cloud_google.c:826-830`; `Launchstation/src/cloud_google.c:1368-1373`; `Launchstation/src/cloud_google.c:1083-1084`; `Launchstation/src/cloud_google.c:1614-1624`
- Problem: (a) CLOUD_JSON_MAX (31) is never used. (b) builtin_client_available (205-212) has one caller, the public cloudGoogleHasBuiltinClient (243-246), which only forwards to it. (c) drive_request's content_type parameter is "application/json" at all three calls (736, 904, 1196), so the NULL fallback at 695 never runs. (d) drive_browse_parse open-codes the WLE-/MCD/PS2/PSU test (826-830) that is_backup_file (1368-1373) implements. (e) In cloudGoogleRecoverSnapshot fd is always -1 at every goto fail (set at 1048 before the first goto), so 1083-1084 are dead. (f) cloudGoogleSyncBackupRoot copies local_root, already a writable char array, into probe just to pass it to genDopen (1615-1617).
- Fix: Delete CLOUD_JSON_MAX; put the #ifdef body into cloudGoogleHasBuiltinClient and delete builtin_client_available; drop the content_type parameter and hard-code the header; move is_backup_file above drive_browse_parse and call it; delete the fd >= 0 close at the fail label; pass local_root to genDopen directly and drop probe.
- Benefit: ~20 lines removed in total. Static helpers are already inlined or dropped, so no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-176: Two identical alloc/run/free wrappers and repeated path checks around the timestamp tools; time_manip_custom exported for one internal caller

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_actions.c:1393-1402`; `Launchstation/src/filer_actions.c:1888-1897`; `Launchstation/src/filer_actions.c:1296-1297`; `Launchstation/src/filer_actions.c:1817-1818`; `Launchstation/src/filer_actions.c:1899-1915`; `Launchstation/include/filer_actions.h:14`
- Problem: time_manip_custom and time_manip_automatic are the same wrapper (timestampListsAlloc, run, timestampListsFree) around their _run functions, and their only caller is time_manip_organize, which already checks filerCanOrganizeFolderTimestamps; both _run functions check it again. time_manip_custom has a public prototype in filer_actions.h but no caller outside filer_actions.c:1910.
- Fix: In time_manip_organize allocate the lists once for the non-Tuna modes, call the run functions directly and free afterwards; delete both wrappers, the two repeated filerCanOrganizeFolderTimestamps checks and the time_manip_custom prototype.
- Benefit: ~20 lines removed; no measurable ELF change (INFERRED: small wrappers)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-177: Unused functions and globals declared in these headers: filerPasteInto, getVirtualKeyboardEditorDisplayChar, CurrTime, mcfreeSpace, HFONTS

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_browser.c:2315-2319`; `Launchstation/include/launchelf.h:589`; `Launchstation/src/virtual_keyboard.c:371-379`; `Launchstation/include/launchelf.h:703`; `Launchstation/src/main.c:36`; `Launchstation/include/launchelf.h:652`; `Launchstation/src/filer.c:113`; `Launchstation/include/filer_shared.h:54`; `Launchstation/src/editor_input.c:6-7`; `Launchstation/include/editor_private.h:92`
- Problem: Each name appears only at its definition and its header declaration: filerPasteInto (callers use filerPasteModeInto), getVirtualKeyboardEditorDisplayChar, the u64 CurrTime, int mcfreeSpace (a test only asserts its old use is absent), and const int HFONTS (gui.c has its own local HFONTS enum).
- Fix: Delete the two functions, the three variables and their header declarations (keep WFONTS in editor_input.c).
- Benefit: ~20 lines removed. MEASURED: both functions already gc'd (40 B + 380 B); CurrTime 8 B and mcfreeSpace 4 B .bss and HFONTS 4 B .rodata stay linked because wLaunchELF has no -fdata-sections (xref sizes; BUILD-EVIDENCE.md:107).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-178: autoStartNeeded repeats dashDevicesAutoProbe's per-family mask/loaded tests

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_devices.c:698-720`; `Launchstation/src/dash_devices.c:863-886`
- Problem: autoStartNeeded and the pass body of dashDevicesAutoProbe write the same six '(mask & DASH_DEVA_X) && !isPathDeviceStackLoaded("x:")' tests for HDD, ATA, MMCE, MX4SIO, iLink and UDPFS, one returning a boolean and the other calling probeMount on the same mount strings; a new family must be added in both places.
- Fix: Put the six {bit, mount} pairs in one static const table and loop over it in both functions (USB stays a separate last step in AutoProbe and a separate test in autoStartNeeded).
- Justification: The table is data that replaces about 30 lines of repeated conditions with two short loops; it removes code and keeps the two lists from drifting.
- Benefit: ~20 lines (INFERRED: 22 + 22 condition lines become a 7-line table and two ~6-line loops); a few hundred bytes of .text at most (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-179: boot.kelf/BOOT.KELF fallback is hand-coded three times and the header file is stat-opened, sized and re-opened

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/hdd_header_injector.c:116-166`; `Launchstation/src/hdd_header_injector.c:873-917`
- Problem: hddHeaderSourceHasFile (154-159) and hddHeaderReadFileIntoHeader (887-892) each special-case the string "boot.kelf" to retry "BOOT.KELF", and ReadFileIntoHeader retries it a third time after genOpen fails (903-909) although the path already holds the name whose size was just read. hddHeaderGetFileSize opens the file only to lseek, then ReadFileIntoHeader opens it again. The PFS table already has an alternate_source_name field for the same need (48, 81).
- Fix: Add an alternate name to HddHeaderFileSpec ("BOOT.KELF" for boot.kelf), resolve it with the existing hddHeaderOpenOptionalSourceFile, take the size with lseek on that fd and read from it. Delete the three strcmp("boot.kelf") branches and the separate open in hddHeaderGetFileSize where it is used for reading.
- Benefit: ~20 lines removed; one open per header file instead of two or three (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-180: copy_printable_prefix is copied verbatim from main_console_info.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/main_exploit_installer.c:248-268`; `Launchstation/src/main_console_info.c:7-27`; `Launchstation/include/main_console_info.h:9-12`
- Problem: The 21-line static copy_printable_prefix in main_exploit_installer.c is byte-for-byte the same as the static one in main_console_info.c; the installer already includes main_console_info.h.
- Fix: Make the main_console_info.c copy non-static, add its prototype to main_console_info.h and delete the installer copy.
- Benefit: ~20 lines removed; one copy of a small function (tens of bytes, INFERRED) dropped from .text
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-181: copy_printable_prefix is copied verbatim into main_exploit_installer.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/main_console_info.c:7-27`; `Launchstation/src/main_exploit_installer.c:248-268`; `Launchstation/include/main_console_info.h:9-12`
- Problem: The two static copy_printable_prefix functions are identical (21 lines each). main_exploit_installer.c already includes main_console_info.h and uses its GetConsoleModelName.
- Fix: Make the main_console_info.c copy non-static, declare it in main_console_info.h, and delete the copy in main_exploit_installer.c.
- Benefit: 20 source lines removed; one copy of the function in the ELF (about 100 bytes, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-182: dash_icons.c: 64px-grid vs 32px-margin icon geometry is copy-pasted in the rasteriser and in dashIcon/dashIconMany

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_icons.c:879-890`; `Launchstation/src/dash_icons.c:986-997`; `Launchstation/src/dash_icons.c:779-800`; `Launchstation/src/dash_icons.c:814-833`
- Problem: dashIcon and dashIconMany repeat the same 12 lines that pick slot size, atlas origin (marginSlot or grid) and gutter pad by id >= DI_CUT. initClipboardIcons and the loop in dashIconsInit are the same SDF-to-alpha rasteriser, differing only in tile size, origin and the DI_PAD_BADGE light.
- Fix: Add one static helper that returns u0, v0, slot and pad for an id and size, used by both draw functions; rasterise all ids in one loop that picks size and origin by id >= DI_CUT, and delete initClipboardIcons.
- Justification: The helper replaces two identical 12-line blocks and one of two rasteriser loops, so it removes code overall (net ~20 lines).
- Benefit: ~20 lines net removed (INFERRED: 2x12 lines replaced by a ~10-line helper, ~10 lines saved by merging the two raster loops); a few hundred bytes of duplicated code (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-183: filerPasteInto has no caller; submenu_func_Paste/submenu_func_psuPaste duplicate the exported paste entry points

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_browser.c:2313-2318`; `Launchstation/include/launchelf.h:589`; `Launchstation/src/filer_browser.c:2281-2306`; `Launchstation/src/filer_browser.c:2321-2330`; `Launchstation/src/filer_browser.c:975-976`
- Problem: filerPasteInto (2315) appears only in its definition and the prototype launchelf.h:589. submenu_func_Paste (2281-2288) is filerPasteModeInto (2321-2325) with the mode taken from new_pad & PAD_SQUARE, and filerPsuPasteInto (2327-2330) only forwards to submenu_func_psuPaste (2292-2306).
- Fix: Delete filerPasteInto and its prototype. Delete submenu_func_Paste and call filerPasteModeInto(msg0, path, new_pad & PAD_SQUARE) at line 1428. Rename submenu_func_psuPaste to filerPsuPasteInto (dropping the forwarder) and drop the two forward declarations at 975-976.
- Benefit: ~20 source lines removed; filerPasteInto's 40-byte section is already dropped by --gc-sections (MEASURED: evidence/Launchstation/raw/link/wLaunchELF-gc-discarded-all.tsv), the wrappers are likely inlined (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-184: splitSmbUrl and splitNfsUrl repeat the same host/port parsing

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/smb_cnf.c:188-213`; `Launchstation/src/smb_cnf.c:240-265`
- Problem: The colon search, digit loop, 65535/0 checks, server copy and isIpv4 test are identical in both functions (26 lines each); they differ only in the default port (445 vs 2049).
- Fix: Move the block into one static helper taking sh, host, slash and the default port and returning the problem string; call it from both.
- Justification: Same logic in 2 places; the helper replaces two 26-line copies with one copy plus two one-line calls, net fewer lines.
- Benefit: ~20 source lines; INFERRED ~150 B .text (one copy of the loop).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-185: Days-in-month table and leap-year rule implemented three times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_clock_sync.c:387-399`; `Launchstation/src/filer_actions.c:457-469`; `Launchstation/src/main_title.c:81-92`
- Problem: dashClockDaysInMonth is exported (dash_internal.h:1128) yet filer_actions.c has isCustomDateLeapYear/getCustomDateDaysInMonth and main_title.c has daysInMonth with the same table and rule.
- Fix: Call dashClockDaysInMonth from filer_actions.c and main_title.c and delete their copies (main_title's 31 for an invalid month needs its callers checked; they pass a normalized month).
- Benefit: ~20 lines removed (INFERRED from the cited ranges).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-186: FAT chain step and FAT-cluster lookup written twice (nextOf vs chainNext, fatGet vs mcViewFatLocate)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_card_write.c:61-75`; `Launchstation/src/mc_card_view.c:99-118`; `Launchstation/src/mc_card_view.c:73-97`; `Launchstation/src/mc_card_view.c:501-527`
- Problem: nextOf in mc_card_write.c repeats chainNext in mc_card_view.c check for check (in-use bit, 0xFFFFFFFF end, mask, alloc_usable bound). fatGet and mcViewFatLocate both carry the same indirect-FAT lookup (ind_index/dbl, ifc_list bound, loadCluster32 of the ifc cluster, ifc_cache index), and mcViewFatGet re-checks the bound that its callers already ensure.
- Fix: Export chainNext (for example as mcViewChainNext) and delete nextOf; implement fatGet as mcViewFatLocate followed by the FAT cluster load, so the indirect lookup exists once.
- Benefit: ~20 lines removed (15-line nextOf, ~12 duplicated lookup lines, minus a 5-line export). Code size: a few hundred bytes of .text at most (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-187: Keyboard row search and next-key walk written twice (layout grid and editor grid)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/virtual_keyboard.c:272-296`; `Launchstation/src/virtual_keyboard.c:380-404`; `Launchstation/src/virtual_keyboard.c:316-322`; `Launchstation/src/virtual_keyboard.c:442-448`
- Problem: findSelectableInRow and getVirtualKeyboardEditorNextInRow are the same outward search (25 lines each) differing only in the column count (VKEY_LAYOUT_COLS vs VKEY_EDITOR_COLS) and the key test (isVirtualKeyboardLayoutKey vs isVirtualKeyboardEditorKey); the dx loops of the two NextKey functions differ the same way.
- Fix: Keep one row search that takes the column count and a 0/1 grid selector (layout or editor key test) and call it from both NextKey functions; the editor's special dy handling stays.
- Justification: Same loop in 2 places; one parameterised copy removes about 20 lines net without a new layer.
- Benefit: ~20 source lines; INFERRED ~100 B .text.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-188: elf.c isElfPayload/isLikelyEncryptedPayload re-implement classifyExecHeader; getMbrOpenPath re-inlines isHddPartyPath

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/elf.c:588-604`; `Launchstation/src/elf.c:693-695`; `Launchstation/src/elf.c:300-326`; `Launchstation/src/elf.c:623`; `Launchstation/src/elf.c:357-360`
- Problem: isElfPayload and isLikelyEncryptedPayload repeat the MIPS-ELF check and the 'byte0==0x01, byte2==0x00' KELF heuristic of classifyExecHeader in the same file. The test '!isElfPayload && encrypted' at 695 is redundant: a buffer starting with 0x01 can never start with 0x7F 'ELF'. getMbrOpenPath line 623 spells out isHddPartyPath (357-360) instead of calling it.
- Fix: Delete isElfPayload and isLikelyEncryptedPayload and use classifyExecHeader(payload, payload_size) == 2 at 694-695; call isHddPartyPath at 623.
- Benefit: ~19 source lines removed (INFERRED from 588-604 plus 693-695); some bytes of .text in the XFROM build.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-189: Cloud history: save-version and card-snapshot recovery are two near-identical blocks

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_tools.c:1293-1340`
- Problem: In cloudBrowseHistory the .psu branch (1293-1317) and the .mcd snapshot branch (1318-1340) run the same sequence: dashPickPath folder, dashModalConfirm, dashWorkerAwaitAll, clear buffers, loading begin, call a recover function with the identical (item, folder, result, size, error, size) signature, loading end, success/failure message. Only the strings and the function differ.
- Fix: Pick the five strings and the recover function pointer (cloudGoogleRecoverSaveVersion or cloudGoogleRecoverSnapshot) from genCmpFileExt(item->name, "PSU") once, then run one copy of the sequence.
- Benefit: ~18 source lines removed (INFERRED from the two 24-line blocks); small .text reduction
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-190: find_file_info allocates and lists a whole directory (2048 FILEINFO) just to read the running ELF's size

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/self_update.c:96-119`; `Launchstation/src/self_update.c:341-342`
- Problem: To learn the old ELF size for the free-space check, find_file_info memaligns sizeof(FILEINFO)*MAX_ENTRY (about 388 B x 2048 = ~776 KB) and runs getDir on the app folder, while the multi-MB update package is already held in RAM. If that allocation or the listing fails, find_file_info returns 0 and the free-space check at :341 is skipped silently.
- Fix: Replace find_file_info with an open + genLseek(fd, 0, SEEK_END) + close on the elf path (the pattern validateVcd uses), and use that size in the check.
- Benefit: ~18 lines removed; avoids a transient ~776 KB heap allocation and a full directory listing during the update (INFERRED: FILEINFO = 256 name + 66 title + 64 sceMcTblGetDir; MAX_ENTRY 2048).
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-191: read_response parses Content-Length and Transfer-Encoding up to three times and checks the header size twice

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/cloud_http.c:591-614`; `Launchstation/src/cloud_http.c:638-671`
- Problem: Once the header end is in the buffer, every later read iteration parses Content-Length and Transfer-Encoding again (595-599) and checks header_len and the body cap (602-611). After the loop, the header_len check is repeated (638-642), Content-Length is parsed again for the cap/truncation check (643-655) and a third time at 667, and Transfer-Encoding again at 658, although content_length and chunked already hold those values.
- Fix: Parse the headers and do the two size checks once, the first time the header end is found; after the loop use content_length and chunked, and delete the repeated parses and the second header_len check.
- Benefit: ~18 lines removed and no header rescans per TLS record of a large body (READ). The time saved is small (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-192: Shift-JIS to ELISA index mapping duplicated between dash_font.c and draw_text.c printXY_sjis

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_font.c:46-68`; `Launchstation/src/draw_text.c:266-294`
- Problem: elisaGlyphIndex (comment: 'Same Shift-JIS -> packed ELISA index mapping as the classic renderer') repeats the SJIS->EUC conversion, the font404 gap-table walk and the 6877-glyph bound that printXY_sjis does inline (0x2121+0x8080 == 0xA1A1, 0xFF-0xA1 == 94, n<=55008 bytes == index<ELISA_GLYPHS). About 20 lines are the same logic.
- Fix: Make elisaGlyphIndex non-static (declare it next to font404) and have printXY_sjis call it, using n = index * 8 and keeping its own '_' fallback for -1.
- Benefit: ~18 lines removed and one copy of the conversion code (INFERRED: a few hundred bytes of .text)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-193: ExecuteBrowserFrom has no caller

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main_actions.c:873-888`; `Launchstation/include/main_actions.h:27`
- Problem: ExecuteBrowserFrom is defined and declared but nothing calls it; the dashboard opens the browser with dashOpenBrowser (dash_main.c:497).
- Fix: Delete the function, its banner comment and its prototype.
- Benefit: 17 lines removed; -ffunction-sections + --gc-sections already drop it from the ELF (BUILD-EVIDENCE.md:108), so no size change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-194: A mounted VMC appears twice in the File Browser root (dashDevicesList row plus dash_browser's own row)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_devices.c:485-493`; `Launchstation/src/dash_browser.c:485-501`
- Problem: dashDevicesList already adds a READY "Virtual MC N" row (browse vmcN:/) for each mounted image. The File Browser root calls dashDevicesList and then appends a second "Memory card image N" row for the same vmcN:/ (dash_browser.c:486-501), so every mounted VMC is listed twice. The folder picker (dash_widgets.c:977) and the Devices grid rely on the dashDevicesList row alone.
- Fix: Delete the extra VMC block in dash_browser.c:486-501; the rows from dashDevicesList remain.
- Benefit: Removes a duplicate root entry and ~16 lines (INFERRED from the cited range).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-195: Four explicit ensureDashboardIpNetworkReady calls duplicate the one oauth_post makes immediately after; sync starts Ethernet before storage

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/cloud_google.c:854-857`; `Launchstation/src/cloud_google.c:1022-1025`; `Launchstation/src/cloud_google.c:1130-1133`; `Launchstation/src/cloud_google.c:1598-1601`; `Launchstation/src/cloud_google.c:361-364`
- Problem: Each of these four blocks is followed directly by refresh_access_token (858, 1026, 1134, 1626), whose oauth_post calls ensureDashboardIpNetworkReady again at 361. Each call repeats showLoadingModulesMsg, getIpConfig, load_ps2ip and a ps2ip_init RPC rebind (init.c:2884-2914). In cloudGoogleSyncBackupRoot the extra call at 1598 also runs before ensurePathDeviceStackReady at 1602, the reverse of the storage-first order the comment at 1014-1015 asks for in recovery.
- Fix: Delete the four explicit blocks and rely on the call in oauth_post. In sync this also moves network start after ensurePathDeviceStackReady, matching the recovery order.
- Benefit: 16 lines removed and one redundant network-ready pass (loading message, IP config refresh, PS2IPS rebind) per browse, recovery and sync (READ). The sync order change is a consistency gain; whether the old order ever failed is INFERRED.
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-196: dash_lib.c: dashLibShutdown (empty) and dashLibRemove are never called; DashProgressFn typedef unused

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_lib.c:501-503`; `Launchstation/src/dash_lib.c:517-526`; `Launchstation/include/dash_internal.h:707`; `Launchstation/include/dash_internal.h:710`; `Launchstation/include/dash_internal.h:731`
- Problem: dashLibShutdown has an empty body and dashLibRemove has no caller; both names appear only in their definitions and prototypes. The DashProgressFn typedef next to them has no user anywhere.
- Fix: Delete both functions, their prototypes and the DashProgressFn typedef.
- Benefit: ~16 lines removed; .text.dashLibShutdown 8 B and .text.dashLibRemove 232 B are already gc-discarded, so no ELF change (MEASURED, link/wLaunchELF-gc-discarded-first-party.tsv).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-197: mc_save_export.c zipCrc re-implements zlib crc32, which is already linked

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_save_export.c:373-388`; `Launchstation/src/mc_save_export.c:483`
- Problem: zipCrc is a private nibble-table CRC-32 (the zip/zlib polynomial). zlib's crc32() is already in the shipped ELF: the link pulls libz.a(crc32.o), 6,908 bytes, for libpng. The host test already links -lz.
- Fix: Delete zipCrc and call crc32(0, f->data, f->size) from <zlib.h> at line 483.
- Benefit: 16 lines removed; 64-byte table zipCrc.t (MEASURED, all-symbols tsv) plus the inlined loop (INFERRED ~40 bytes) leave the ELF; zlib's table-driven CRC is also faster on the EE for large saves.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-198: dash_ps1_card.c has three hand-written whole-file read loops

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_ps1_card.c:49-67`; `Launchstation/src/dash_ps1_card.c:84-92`; `Launchstation/src/dash_ps1_card.c:202-237`
- Problem: p1Load (open, size check, malloc, chunked read loop) and p1ReadFile (open, size check, malloc, read loop) are the same routine with different size limits, and both re-implement p1ReadAll, which already loops genRead until n bytes arrive.
- Fix: Give p1ReadFile min/max size parameters and use p1ReadAll for its loop; have p1Load call p1ReadFile(path, PS1_CARD_SIZE, PS1_CARD_FILE_MAX, ...) and keep only the container parsing.
- Benefit: ~16 lines removed (INFERRED); p1Load's 32 KB chunking is dropped, as p1ReadFile and dashLoadTextFile already read whole files in one genRead.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-199: Dead 'dashboard enabled' option: DashIsEnabled() has no caller and S_ENABLED is in no settings row

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_main.c:200-204`; `Launchstation/include/dash.h:18`; `Launchstation/src/dash_settings.c:52`; `Launchstation/src/dash_settings.c:1021-1023`; `Launchstation/src/dash_settings.c:1701-1703`; `Launchstation/src/dash_settings.c:2753-2759`; `Launchstation/src/dash_store.c:308-309`; `Launchstation/include/dash_internal.h:572`
- Problem: DashIsEnabled is referenced only by its prototype (dash.h:18); the gc map already discards it. dashCfg.enabled is forced to 1 on load ('the dashboard is the only shell now', dash_store.c:309) and S_ENABLED appears only in its enum line and two switch cases, so no settings row can show or change it; the keep_enabled save/restore in dash_settings.c:2753-2759 protects a value that is always 1.
- Fix: Delete DashIsEnabled and its prototype, the S_ENABLED enum value and its two cases, and the keep_enabled lines. Either keep writing 'Dash_Enabled = 1' as a literal for old builds that read the file, or drop the field and the key.
- Benefit: ~15 lines removed; DashIsEnabled code is already gc-discarded (36 B, MEASURED wLaunchELF-gc-discarded-first-party.tsv:40), so ELF change is a few bytes of switch code (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-200: dash_files.c: fentries sized for 64 rows (max 35 used) and fdevs[24] is never filled: ~14 KB of .bss

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_files.c:178-182`; `Launchstation/src/dash_files.c:212`; `Launchstation/src/dash_files.c:288-291`; `Launchstation/src/dash_files.c:374-378`; `Launchstation/src/dash_files.c:392-403`
- Problem: filesRebuild adds at most 4 fixed rows, one bookmark header and MAX_PATH_PAD (30) bookmarks = 35 rows, but fentries has FILES_MAX 64 slots of 332 bytes. fdevs (24 DashDevice of 188 bytes) is only read under index < nfdevs, and nfdevs is only ever set to 0 (line 212); no FE_DEVICE row is ever added.
- Fix: Set FILES_MAX to 35 (4 + 1 + MAX_PATH_PAD); delete fdevs, nfdevs and the FE_DEVICE branches in filesActivate and filesDraw.
- Benefit: fentries 21,248 B .bss MEASURED (BUILD-EVIDENCE.md top symbols) -> 11,620 B, saves 9,628 B; fdevs 24*188 = 4,512 B INFERRED; ~14.1 KB .bss and ~15 lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-201: extract_sidecar walks every PSU record twice with the same validation code

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/update_psu.c:253-286`; `Launchstation/src/update_psu.c:84-96`
- Problem: extract_sidecar runs a full validation pass (255-269) and then a second pass (274-286) that repeats the same seek/read/length/padding checks line for line before looking for the record. In production self_update.c:306 has already validated the same RAM package with selfUpdateLocatePsuElf, whose loop (update_psu.c:84-96) performs the same record checks.
- Fix: Make it one pass: in the single loop, remember the position and length of the record named name, check position == size after the loop, then seek to the remembered record and copy it. Drop the second loop.
- Benefit: ~15 source lines and one walk over the package removed; INFERRED ~100-200 B of .text.
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-202: Unconditional printf [PASTE] traces in subfunc_Paste ship in release builds

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_browser.c:2163`; `Launchstation/src/filer_browser.c:2166`; `Launchstation/src/filer_browser.c:2174`; `Launchstation/src/filer_browser.c:2180`; `Launchstation/src/filer_browser.c:2184-2186`; `Launchstation/src/filer_browser.c:2203-2206`; `Launchstation/src/filer_browser.c:2210-2211`; `Launchstation/src/filer_browser.c:2238-2239`
- Problem: subfunc_Paste prints seven '[PASTE] ...' traces with plain printf, plus a trace_vmc_paste flag that exists only for two of them. The repo's DPRINTF macro is stripped in consumer builds (include/launchelf.h:164), but these printf calls are compiled into every build and run on every paste (per item when a vmc path is involved).
- Fix: Delete the traces and trace_vmc_paste, or change them to DPRINTF so they vanish from release builds.
- Benefit: ~15 lines removed; ~450 bytes of format strings plus 7 call sequences (~700 bytes .rodata/.text, INFERRED from the literal lengths)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-203: artRun opens every candidate artwork file twice: fileExists probe before dashPngLoadFile

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_art.c:151-163`; `Launchstation/src/dash_art.c:375`
- Problem: fileExists does genFixPath + genOpen + genClose; tryDecode then calls dashPngLoadFile -> dashLoadTextFile (src/dash_store.c:105-119), which does the same genFixPath + genOpen and returns NULL cleanly when the file is missing. The probe adds nothing except an extra open/close for every candidate that exists, on the worker, against memory card / USB / HDD paths.
- Fix: Delete fileExists and call tryDecode(j, cand) directly at line 375.
- Benefit: 14 lines removed; one fewer open/close per found artwork file on the worker thread (INFERRED: each MC open is a slow IOP round trip).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-204: loadUsbModules re-implements loadBdmCoreModules inline

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/init.c:1765-1780`; `Launchstation/src/init.c:2465-2481`
- Problem: Lines 1765-1780 load bdm_irx then bdmfs_fatfs_irx with the same have_bdm/have_bdmfs latching as loadBdmCoreModules (2465-2481), which loadMx4sioModules, loadIlinkModules, loadPcCardCfModules and loadAtaBlockDriver already use. The only difference is two extra 'USB BDM'/'USB BDMFS' loading messages.
- Fix: Replace the inline block with 'if (!loadBdmCoreModules()) loaded_ok = 0;' and drop the two extra messages ('USB core IO' is already shown at 1750).
- Benefit: ~13 lines removed; a few dozen bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-205: Imported save is read back twice: mcCardWriteSave verifies, then ctPutSave reopens and reads the whole save again

- Category: 5 (Performance)
- Location(s): `Launchstation/src/mc_card_write.c:653-662`; `Launchstation/src/dash_card_tool.c:415-426`
- Problem: mcCardWriteSave step 6 already reopens the view (fresh mcViewOpen), reads the whole save into a second McSaveData and compares it with sameSave. ctPutSave then closes, reopens the image read-only and does the same full mcCardReadSave + mcCardSameSave again. Every single-save import (5 ctPutSave callers) therefore reads, mallocs and compares the full save data twice, synchronously on the UI thread under the loading overlay.
- Fix: Delete the second read-back in ctPutSave (dash_card_tool.c:415-426) and return the mcCardWriteSave result once ctClose succeeded. Keep the verify inside mcCardWriteSave, which the batch paths (dash_card_tool.c:1088 and 1170) rely on.
- Benefit: One full read of the save (all its data clusters through fileXio) plus one full-size malloc/compare removed per import; ~11 lines removed (INFERRED: count of dash_card_tool.c:415-426).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-206: Fixed sleep(1) on the UI thread after every ATA_BD load (HDD/ATA boot, first hdd0:/ata0: access, after each IOP reset)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/init.c:2500-2504`; `Launchstation/src/init.c:2520-2526`; `Launchstation/src/dash_devices.c:604-611`; `Launchstation/src/main_startup.c:651-658`
- Problem: loadAtaBlockDriver sleeps 1 s after ATA_BD starts. In EXFAT builds both loadAtaModules and loadHddModules (load_ps2hdd_stack(1)) go through it, from the dashboard device probe and the HDD/ATA boot path on the UI thread, so the screen stops for a full second once per IOP session. The ata0: probe that follows already waits up to 2000 ms for the device (dash_devices.c:611), and RiptOPL loads ATAD and its HDD modules back to back with no pause (src/sources/hddsupport.cpp:654-658).
- Fix: Delete pauseAfterAtaBlockDriverLoad and its call; rely on the driver's own device init and the existing bounded probe. Verify HDD/ATA detection on hardware.
- Benefit: 1000 ms less UI freeze on HDD/ATA boot and on first HDD/ATA access per IOP session (INFERRED from sleep(1)); ~7 lines removed.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-207: SMB3 share kind (kind 3, VERSION = 3) is a marker that is lost when the share is reopened in the editor

- Category: 8 (Half-finished or redundant features)
- Location(s): `Launchstation/src/smb_cnf.c:483`; `Launchstation/src/smb_cnf.c:502-507`; `Launchstation/src/smb_cnf.c:143-144`; `Launchstation/include/smb_shares.h:81-86`; `Launchstation/src/dash_settings.c:535`; `Launchstation/src/dash_settings.c:1025-1029`; `Launchstation/src/dash_settings.c:1860-1862`
- Problem: The editor offers SMB2/SMB3/SMB1 and writes VERSION = 3 for SMB3 (smbCnfBuildBlock kind 3), but the parser maps 3 to version 2 (smb_cnf.c:143), no code acts on the mark (both use connectSmb2), and the editor reloads kind = sh->version (dash_settings.c:535). Reopening an SMB3 share shows SMB2, and saving it again drops the VERSION line.
- Fix: Delete the SMB3 choice: editor list becomes SMB2/SMB1, smbCnfBuildBlock accepts kinds 1 and 2 only, header comment updated. Keep the parser accepting VERSION = 3 as SMB2 for existing files. Adjust tests/test_smb_cnf_edit.py calls with kind 3.
- Benefit: ~6 source lines and one editor option that does nothing; no measurable size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-208: elf.c launchArgSource is written but never read (1 KB .bss and a dead source parameter chain)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/elf.c:35`; `Launchstation/src/elf.c:41`; `Launchstation/src/elf.c:144-154`; `Launchstation/src/elf.c:160`; `Launchstation/src/elf.c:183`
- Problem: launchArgSource[MAX_PATH] is only cleared (41) and filled (151-154); no code reads it. The source argument of launchArgsFinishParse and launchArgsReadFromFd exists only to fill it, and the source parameter of LaunchArgsLoadFromBuffer feeds nothing else.
- Fix: Delete launchArgSource, its clear and copy, and the source parameter of the two static helpers; optionally drop source from LaunchArgsLoadFromBuffer and its six callers.
- Benefit: 1,025 bytes .bss (INFERRED: char[MAX_PATH], MAX_PATH = 1025 in launchelf.h:204); ~6 lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-209: copy() re-runs prepareTransferDeviceStacks at every recursion level although subfunc_Paste already did it

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_copy.c:804-811`; `Launchstation/src/filer_browser.c:2164-2176`; `Launchstation/src/filer_device.c:161-165`
- Problem: subfunc_Paste prepares both stacks once (filer_browser.c:2164) and copy() prepares them again for every file and folder (804). Each call re-walks all path checks and ensurePathDeviceStackReady; for host: that is initHOST() (filer.c:1720-1734), which opens host:elflist.txt and dopens host: over the network for every copied entry.
- Fix: Call prepareTransferDeviceStacks in copy() only when recurses == 0 (or rely on subfunc_Paste's call and delete it from copy()); update tests/test_transfer_stack_preflight.py:176-177, which assert the call is in copy().
- Benefit: ~2 host RPCs saved per copied entry on host:, fewer string compares elsewhere; ~6 lines (INFERRED, not measured).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-210: Launch-key launch writes LK_Title into the shared library entry (dash_home.c activateChip, dash_main.c runLaunchKey)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_home.c:214-222`; `Launchstation/src/dash_main.c:959-969`; `Launchstation/src/dash_lib.c:471-482`
- Problem: dashLibResolve returns &dashApps[i] when the launch-key path is already in the library, and both callers then copy LK_Title into a->title. The library entry is renamed in RAM: Home and Applications show the launch-key title after a failed or cancelled launch, and dashLaunchApp saves DASHLIB.CNF when art is dirty (dash_main.c:647-648), so the wrong title is persisted until a rescan of that device rebuilds it.
- Fix: When dashLibResolve returns a library entry, copy it into the local tmp before overriding the title (tmp = *a; a = &tmp) in runLaunchKey; dashLaunchApp already works on its own copy. Let activateChip call runLaunchKey (make it non-static) instead of repeating the same branch, so the fix is made once.
- Justification: Adds one struct copy per launch-key start; it is the smallest way to stop a launch from renaming a library entry, and sharing runLaunchKey removes the duplicate branch in dash_home.c, so the net line count goes down.
- Benefit: Fixes a user-visible rename of library entries that can be persisted to DASHLIB.CNF; about 5 lines removed net (duplicate branch replaced by one call). INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-211: Cloud menu re-reads config/token files on the UI thread right after queuing MMCE status jobs (redundant in-function refreshes)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_tools.c:1363-1378`; `Launchstation/src/dash_tools.c:1435-1445`; `Launchstation/src/dash_tools.c:628`; `Launchstation/src/dash_tools.c:634`; `Launchstation/src/dash_tools.c:662`; `Launchstation/src/dash_tools.c:687`; `Launchstation/src/dash_tools.c:698`; `Launchstation/src/dash_tools.c:711`; `Launchstation/src/dash_tools.c:403-426`; `Launchstation/src/dash_tools.c:610-612`; `Launchstation/src/dash_worker.c:6-10`; `Launchstation/src/dash_settings.c:3303-3305`; `Launchstation/src/dash_settings.c:3343-3345`
- Problem: dashToolsSyncAllMmceCards calls dashToolsRefreshMmceStatus on every exit (628, 634, 662, 687, 698, 711) and cloud-menu case 5 calls it at 1440; each queues mmceStatusRun (MMCE scans plus cloudGoogleIsLinked token-file reads on the worker, 403-426). Control returns to the dashToolsCloudBackup loop top, which at once reads WLE-CLOUD.CNF, the CA and token files on the UI thread (1368-1378) while that job, and any settings save queued at 612 or 1464, is still running, against the rule in dash_worker.c:7-10. Both callers already refresh after these functions return (dash_settings.c:3305, 3345).
- Fix: Delete the seven in-function dashToolsRefreshMmceStatus calls (628, 634, 662, 687, 698, 711, 1440) and start each dashToolsCloudBackup loop pass with dashWorkerAwaitAll, returning if the user backs out.
- Justification: The added await (2 lines) closes the remaining overlap with the settings save that chooseMmceBackupRoot and the disconnect path queue just before the loop reads files; it returns immediately when nothing is queued. The change is still -5 lines net.
- Benefit: -5 lines net; one MMCE status scan fewer per in-menu action; the UI no longer does file IO while a job it queued runs. Practical damage of the overlap INFERRED (fileXio RPCs are serialised by the SDK).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-212: wle_igr.c: the HDD driver stack is reloaded on every one of up to 24 path-resolution retries

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/igr/wle_igr.c:122-136`; `Launchstation/igr/wle_igr.c:151-154`; `Launchstation/igr/wle_igr.c:228-255`
- Problem: resolve_app_path calls hdd_target_path on each attempt (151-154, WLE_IGR_TRIES = 24, 250 ms apart). Each call runs SifExecModuleBuffer again for ps2dev9, bdm, bdmfs_fatfs, ata_bd, ps2hdd and ps2fs (122-134), then calls fileXioInit (136). If the first mount fails because the drive is still spinning up, every retry loads six more module copies instead of retrying only the mount. Unlike the other device families, main() (228-255) has no hdd branch that loads the drivers once.
- Fix: Load the HDD modules once in main(): add hdd0/hdd1 to the ata branch and start ps2hdd/ps2fs with their arguments there. Reduce hdd_target_path to the path split and the pfs0: mount. This also removes the second copy of the ata_bd/bdm load sequence.
- Benefit: Saves up to 23 x 6 redundant IOP module loads and the IOP memory and time they take on the HDD IGR path (INFERRED). About 5 fewer lines.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-213: configStampMcFolderIcon writes the same 14990-byte icon three times (list/copy/del.icn) to the user's memory card

- Category: 5 (Performance)
- Location(s): `Launchstation/src/config.c:429-432`; `Launchstation/src/embed_blobs.S:63-67`; `Launchstation/res/memory-card/icon.sys`; `Launchstation/src/makeicon.c:408-410`
- Problem: After a successful settings save, the folder stamp writes mc_list_icn under list.icn, copy.icn and del.icn because the shipped icon.sys names three different files, though embed_blobs.S:65-66 says they are byte-identical. That is two extra 14990-byte files and two extra file creates on a slow 8 MB card for every stamped folder. The repo's own makeicon.c writes one icon name into all three icon.sys slots, so one file is enough.
- Fix: Put "list.icn" in the copy and del name slots of res/memory-card/icon.sys (offsets 324 and 388), write only list.icn in configStampMcFolderIcon, and drop copy.icn/del.icn from res/memory-card and the release packages.
- Benefit: About 29980 bytes less memory-card space and 2 fewer card file writes per stamped folder (MEASURED: list/copy/del.icn are 14990 B each in inventory); ~3 source lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-214: readCD rejects and spins down a DVD-Video/ESR or audio disc that finishes detection during its 500 ms wait

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer.c:611-623`; `Launchstation/src/init.c:1359-1373`; `Launchstation/src/init.c:1388-1407`
- Problem: When the disc is still detecting at entry (611), the loop waits for uLE_cdDiscValid, which accepts SCECdCDDA and SCECdDVDV; the range check that follows (619) treats every type above SCECdPS2DVD as invalid, calls uLE_cdStop (stops the drive, init.c:1404) and returns an empty listing. If detection had already finished before entry the check is skipped and the same disc lists. Trigger: insert an ESR-patched disc (reported as DVDV, init.c:1396-1401) or a DVD-Video and open cdfs: right away; the first listing is empty and the drive spins down.
- Fix: After the wait, return 0 only when the last uLE_cdDiscValid() call failed; delete the range check and the uLE_cdStop call.
- Benefit: First cdfs listing after inserting a DVD-Video/ESR disc works; about 3 lines removed (INFERRED: the 4-line check becomes one test of the loop's last result).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-215: Cancelling the name prompt of Paste-with-rename still pastes under the original name and replaces a same-named file

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_copy.c:820-823`; `Launchstation/src/filer_copy.c:1379-1383`; `Launchstation/src/dash_widgets.c:663-668`; `Launchstation/src/filer_browser.c:2321-2324`
- Problem: With PasteMode PM_RENAME, copy() asks for a new name; when keyboard() returns -1 (cancel, which classicKeyboard returns when the dashboard keyboard is dismissed) it copies the original name back and carries on. For a file the destination is then removed and recreated without a prompt (genRemove and O_TRUNC at 1382-1383), so cancelling the rename the user chose to avoid a name clash overwrites the existing destination file, and a Move completes under the original name.
- Fix: When keyboard() returns a negative value, return COPY_KEPT_DESTINATION (nothing written, nothing for a Move to delete) instead of reverting to the original name; keep the revert only for an empty name.
- Justification: Replaces the fallback branch with an early return; today a cancelled prompt silently overwrites user data.
- Benefit: A cancelled rename no longer overwrites a destination file (READ); no size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-216: ELISA Shift-JIS atlas allocates 512x1024 (512 KB) but only 864 rows hold glyphs

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_font.c:39`; `Launchstation/src/dash_font.c:78-92`; `Launchstation/src/dash_font.c:101`
- Problem: ELISA_GLYPHS is 6877; at 64 glyphs per 8-pixel row that is 108 rows = 864 texel rows, yet elisaAtlasReady memaligns, clears and uploads 512*1024 bytes and sets Height 1024. The buffer is never freed (dashFontShutdown has no caller), so after the first Japanese save title the extra rows are held in EE RAM and in the VRAM pool for the rest of the run.
- Fix: Use a height of 864 (rows = (ELISA_GLYPHS + 63) / 64 * 8) for the memalign, memset, SyncDCache and Height; the font atlas in the same file already uses a non-power-of-two height (FONT_TEX_H 340).
- Benefit: 512*(1024-864) = 81,920 B EE RAM and about 64 KB of VRAM pool (512 vs 448 KB in 8 KB T8 pages) once any SJIS title has been shown (INFERRED)
- Effort: S. Risk: Low. Confidence: High (INFERRED)

#### LAUNCHSTATION-217: RunLoaderElfInternal: sprintf into the 256-byte stack bootpath overflows on long HDD/DVR PFS paths

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/elf.c:730`; `Launchstation/src/elf.c:775-779`; `Launchstation/src/elf.c:797-801`; `Launchstation/src/main_actions.c:402-405`
- Problem: bootpath is char[256] on the stack but sprintf writes party + ":pfs:" + the PFS path into it with no bound (elf.c:776,778,798,800). For an HDD launch main_actions.c:402-404 builds party "hddN:PART" and filename "pfs0:/rest" from a path of up to MAX_PATH (1025), so bootpath receives strlen(path)+4 bytes: any hdd0:/ ELF path of 252 or more characters overflows the stack and can corrupt locals such as handoff_path, which is dereferenced right after (elf.c:782).
- Fix: Declare bootpath as char[MAX_PATH] (exec_target and exec_arg0 already are) and use snprintf(bootpath, sizeof(bootpath), ...) at the four sites.
- Benefit: Closes a stack overflow on the ELF launch path; stack use +769 bytes (INFERRED: 1025-256), no code size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-218: choose_custom_output_path passes keyboard(name, 36) for char name[36]; the dashboard keyboard writes one byte past the array

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/main_exploit_installer.c:962`; `Launchstation/src/main_exploit_installer.c:979-982`; `Launchstation/src/dash_widgets.c:341-350`; `Launchstation/src/dash_widgets.c:663-668`; `Launchstation/src/dash_main.c:1358`
- Problem: keyboard()'s max is a character count, so the buffer needs max+1 bytes (classic gui.c:249 inserts while len < max; dashboard kbInsert dash_widgets.c:345 inserts while len < max_len). name is char[36] and is passed with max 36. DashRun installs the dashboard keyboard override (dash_main.c:1358), which accepts 36 characters, and the terminating NUL is written at name[36], past the stack array. Reached when a dongle console (COH/Python) exports a signed custom ELF (2398-2399 -> export_custom_payload 1539 -> choose_custom_output_path).
- Fix: Pass sizeof(name) - 1 to keyboard().
- Benefit: Removes a 1-byte stack overflow (INFERRED: 36 characters + NUL = 37 bytes into char[36]). No size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-219: dashBgSceneClouds applies the drift direction twice: no cloud moves left and even-indexed clouds leave the screen for good (cloud 0 is never shown)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_bg_ps2.c:800-809`
- Problem: x = fmodf(dir * t * sp + i * 211, span) followed by x = span - x when dir < 0. While the argument is positive, span - (i*211 - t*sp) grows with t, so 'left' clouds also drift right. Once t*sp > i*211 the argument is negative, fmodf returns a negative value, and x - 180 stays in [820, 1820), off the 640-wide screen. Cloud 0 is gone at t > 0, and after about 211 s of scene time only the 4 odd clouds remain (2 of 5 in SUBTLE). Reachable by choosing the 'Drift clouds' background (dash_themes.c:114,119).
- Fix: Drop dir from the fmodf argument (fmodf(t * sp + i * 211.0f, span)); the existing mirror then gives the intended leftward drift.
- Benefit: Scene shows all 9 (FULL) or 5 (SUBTLE) clouds drifting both ways as designed; one-token change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-220: dashBgSceneClouds: clouds with dir = -1 are drawn off screen after the first minute

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_bg_ps2.c:803-809`
- Problem: For even i, x = fmodf(-t*sp + i*211, span) becomes negative once t*sp > i*211 (immediately for i = 0, within ~53 s for i = 8); fmodf keeps the sign, so x = span - x lies in [span, 2*span) and after -180 the centre is at 820..1820 on a 640 px screen. Five of nine clouds (three of five in SUBTLE) then sit off screen and also move right instead of left. The scene is selectable as "clouds" (src/dash_themes.c:114, dispatched at src/dash_bg.c:524-525).
- Fix: Take the modulo of the positive term, x = fmodf(t * sp + i * 211.0f, span), and keep the existing mirror (span - x) for dir < 0.
- Benefit: Fixes a visible scene defect (more than half the clouds missing); 0 lines changed in count.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-221: dash_ps1_card.c keeps 8 KB of tool-only state in .bss for the whole run

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_ps1_card.c:30-36`; `Launchstation/src/dash_ps1_card.c:719-737`
- Problem: p1_saves, p1_titles, p1_details, p1_items and p1_head are static arrays used only while the PS1 card tool is open. The three card buffers are already allocated in p1Hold and freed in p1Drop, but these 8 KB stay resident from boot to exit.
- Fix: Allocate these arrays in the same p1Hold memalign block as the card buffers (one larger allocation) and drop them with it in p1Drop.
- Benefit: 8092 B of .bss returned to the heap while the tool is closed (MEASURED: evidence/Launchstation/raw/link/wLaunchELF-contrib-by-object.tsv:74, dash_ps1_card.o .bss 8092).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-222: loadConfig copies CNF values with strcpy/sprintf into fixed SETTING fields without a length bound

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/config.c:962`; `Launchstation/src/config.c:972-1004`; `Launchstation/src/config.c:1039-1042`; `Launchstation/src/config.c:1047-1048`; `Launchstation/src/config.c:1058-1067`; `Launchstation/include/launchelf.h:281-307`
- Problem: Values come straight from LAUNCHSTATION.CNF (up to the file size; the primary read path in preloadCNFFd has no cap) and are strcpy'd into MAX_PATH (1025) fields or sprintf'd into the 64-byte Misc fields. A hand-edited line longer than the field overwrites the following SETTING members at boot; a NEUTRINO_ELF value over about 1267 characters runs past the end of the malloc'd SETTING and corrupts the heap. Titles, PathPad and the SMB login in the same loop are already bounded.
- Fix: Replace each strcpy(field, value) and sprintf(field, "%s%s", ...) here with snprintf(field, sizeof(field), ...), as the neighbouring LK_Title/PathPad/SMB branches already bound their copies. Same line count.
- Benefit: Closes a heap/struct overflow reachable from a user-editable boot config at no size cost; INFERRED arithmetic: neutrino_file starts at offset 26916 of a ~28184-byte SETTING
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-223: makeicon: tex_compresRLE copies its result into a second malloc, and the texture buffer is never freed (leak per icon)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/makeicon.c:236-256`; `Launchstation/src/makeicon.c:337-368`
- Problem: tex_compresRLE mallocs a 32 KB output buffer, then mallocs a second buffer of the compressed size, copies into it and frees the first. make_icon returns on every path (success 364, errors 344/368) without freeing tex_buffer, so each folder icon created through filerCreateFolderIcon (filer_browser.c:2455) leaks the compressed texture; the next call overwrites the pointer. None of the three mallocs is checked (memset/write on NULL at 338/246).
- Fix: In tex_compresRLE free the old tex_buffer and keep new_tex as tex_buffer (no second malloc or memcpy). Free tex_buffer (and set it to NULL) before every return in make_icon, and return -1 if a malloc fails.
- Justification: The leak is real on every icon creation, and the NULL checks guard the only allocations in this path; the change is a few lines and removes one malloc and a copy.
- Benefit: Fixes a leak of the RLE texture (a few KB, INFERRED from 16384 u16 pixels of mostly black runs) per icon written; removes one transient malloc and memcpy per icon.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-224: showDetails: len += snprintf can exceed body[640], then sizeof(body)-len underflows and the Metadata line is written past the stack buffer

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_apps.c:118-136`
- Problem: Each append adds snprintf's would-be length. Worst case: Location+File 320 (dev 47, path 255), Author 48, Version 33, Category 34, Artwork 265 (art 255) = 700 > 640. Line 134 then calls snprintf(body + 700, (size_t)(640 - 700), ...), a huge size, writing up to 289 bytes past body on the UI-thread stack. Trigger: Details on an app with path+art lengths of about 450-600 chars (deep USB/HDD folder or an absolute cover= in title.cfg) and filled author/version/category.
- Fix: Enlarge body to 1024 bytes: every field is bounded (worst case 700 + 288 Metadata line + NUL = 989), so no append can pass the end and the arithmetic cannot underflow.
- Justification: Changes one constant; costs 384 bytes of UI-thread stack while the Details dialog is built. Smaller than adding a clamp after each of the six appends.
- Benefit: Removes a stack buffer overflow (crash) on rare long paths; no code added (INFERRED: arithmetic from DashApp field sizes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-225: Active MMCE backup/export name buffers (40 and 48 bytes) truncate the no-RTC stamp: names lose .mcd/.ps2 and later backups are refused

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_browser.c:1271-1275`; `Launchstation/src/dash_browser.c:1316-1336`; `Launchstation/src/dash_browser.c:1290-1302`; `Launchstation/src/dash_browser.c:1557`; `Launchstation/src/dash_browser.c:1569-1573`; `Launchstation/src/dash_browser.c:1627`; `Launchstation/src/dash_browser.c:1640-1644`
- Problem: When sceCdReadClock fails or stat != 0, the stamp is 00000000-000000-S plus 10 digits of Timer()/1000 (27 chars). MMCE0-ActiveCard-<stamp>.mcd is then 48 chars but name[40] keeps 39: the extension and the last 5 digits are cut, and because Timer() counts ms since boot the kept digits are 00000 for the first 27.7 hours. The first backup is written with no .mcd, so activeHistoryMatch never sees it (no dedupe, no pruning); every later attempt truncates all 100 suffixes to the same existing name and fails with 'Could not create a unique backup history name'. The PCSX2 export (name[48]) loses the final '2' and writes a .ps file.
- Fix: Declare both name buffers as char name[MAX_NAME] (dash_browser.c:1557 and 1627). Nothing else changes.
- Benefit: Fixes blocked active-card backups and wrongly named exports on consoles without a valid RTC reading; no lines added (INFERRED how often the RTC read fails; the code at 1272-1273 says it must still work).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-226: Classic TextEditor: per-keystroke buffer leak, unbounded row table, mark-delete underflow and unchecked save writes (editor appears unreachable)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/editor_input.c:369-375`; `Launchstation/src/editor_input.c:644-650`; `Launchstation/src/editor_rules.c:26-27`; `Launchstation/src/editor.c:11`; `Launchstation/src/editor_input.c:217-226`; `Launchstation/src/editor_input.c:278-291`; `Launchstation/src/editor_input.c:482-491`; `Launchstation/src/editor_input.c:552-565`; `Launchstation/src/editor_input.c:585-594`; `Launchstation/src/editor_file.c:228-234`; `Launchstation/src/editor_file.c:294-300`
- Problem: Each inserted char mallocs a new text buffer but the free of the old one sits inside a comment (editor_input.c:372,647), leaking TextSize+256 bytes per key until malloc returns NULL and strcpy crashes. editor_rules.c:26 writes Editor_nRowsWidth[Editor_nRowsNum] unbounded; the array has 2048 entries (editor.c:11) and 512 KB files are accepted. Deleting a mark made right-to-left uses del1=-MARK_SIZE from the cursor at the low end, writing before the buffer and strcat-overflowing it. Saves ignore genWrite after O_TRUNC and report success. INFERRED unreachable: the dashboard sends MISC/TextEditor to dash_editor.c.
- Fix: Delete the classic editor together with the dead-code finding for it (dash_editor.c replaced it; dash_main.c:470-472). Only if it is kept: un-comment the free at editor_input.c:372 and :647, stop adding rows at MAX_ENTRY in editor_rules.c:26, compute mark deletes from Mark[MARK_IN] instead of Editor_Cur, and treat a short genWrite as a failed save.
- Justification: Only for the keep-it fallback: each check closes one cited leak or overwrite with a one-line bound or comparison; deletion adds nothing.
- Benefit: If deleted: lines counted by the dead-code finding, 0 here. If kept: removes a whole-buffer leak per keystroke, two heap/.bss overwrites and a silent truncation of the saved file (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-227: First-run welcome dialog opens during the auto-launch countdown, which then cannot be cancelled

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_main.c:1527-1536`; `Launchstation/src/dash_main.c:1119-1126`
- Problem: The intro check uses boot_phase != 1, so it fires while boot_phase is 2 (auto-launch countdown). dashModalMessage blocks and takes all input, so 'Press any button to stay' (bootUpdate needs in->any) cannot cancel, while bootDraw keeps showing the countdown underneath. If the user reads past the deadline, the auto key launches on the next frame after the dialog closes. Trigger: from_boot with LK_Path[SETTING_LK_AUTO] set and intro_done 0, i.e. first boot with an existing auto-launch config, or after 'show welcome again' (dash_settings.c:3295-3298).
- Fix: Show the welcome only when boot_phase is 0 (change the condition to !boot_phase), so it appears after the countdown is cancelled, or not at all when the auto launch happens.
- Benefit: Auto-launch countdown stays cancellable on the first boot (minor bug fixed, no size change).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-228: copy() re-prepares both device stacks for every nested file and folder; on host: each call costs 2-3 PC round trips

- Category: 5 (Performance)
- Location(s): `Launchstation/src/filer_copy.c:804-811`; `Launchstation/src/filer_device.c:161-165`; `Launchstation/src/filer.c:1720-1737`; `Launchstation/src/filer_browser.c:2164`; `Launchstation/src/filer_actions.c:306`; `Launchstation/src/filer_actions.c:1951`
- Problem: subfunc_Paste prepares the source and destination stacks once (filer_browser.c:2164), yet copy() calls prepareTransferDeviceStacks again at every recursion level, once per copied file and folder. For host: that runs initHOST(), which opens host:elflist.txt and, when it is absent, opens and closes the host: directory: 2-3 blocking network requests per entry on the UI thread, and a transient failure there aborts the copy midway. getFileSize() and delete() call ensurePathDeviceStackReady per entry the same way.
- Fix: Call prepareTransferDeviceStacks in copy() only when recurses == 0 (once per clipboard item); nested levels use the same two devices. getFileSize() and delete() can leave the check to their top-level callers.
- Benefit: Removes 2-3 host: requests per nested entry: a 1000-file host: copy saves about 2000-3000 requests, seconds of UI-thread time (INFERRED at ~1 ms per LAN round trip); other devices only save flag checks.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-229: dashDevicesStart keeps using the caller's DashDevice after dashWorkerAwaitAll, which can rebuild the File Browser device array under it

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_devices.c:738-773`; `Launchstation/src/dash_devices.c:723-735`; `Launchstation/src/dash_devices.c:107-112`; `Launchstation/src/dash_browser.c:676-694`; `Launchstation/src/dash_browser.c:3573-3583`; `Launchstation/src/dash_main.c:1242-1246`
- Problem: The File Browser root passes d = &fb.devs[fb.sel] to dashDevicesStart (dash_browser.c:679,694). startSync(dev->mount) first calls dashWorkerAwaitAll, whose pump runs completion callbacks on the UI thread; usbMaskRescanDone (when a late second USB drive appears) and dashNetConfigWarmDone both call dashBrowserRefreshRoot, which re-runs openDir("") and overwrites fb.devs in place. probeMount then reads the mount string from the overwritten slot, so a row shifted by the new USB row is probed instead of the one the user chose (for example MX4SIO instead of iLink, which also switches the SIO2 stack). dash_browser.c:689 already notes that openDir rebuilds fb.devs, but only the browse path is copied.
- Fix: At the top of dashDevicesStart copy *dev into a local DashDevice and use the copy for the title, the UDPFS check, startSync and the callback.
- Justification: Adds one local copy (188 bytes of stack) and changes four dev-> uses; it closes a real wrong-device probe that the caller's existing path copy does not cover, and is smaller than making every caller pass a copy.
- Benefit: Fixes a wrong-driver load and possible unwanted MMCE/MX4SIO stack switch in a boot/after-misc-command timing window; net 0 lines (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-230: dashPngDecode does a 64-bit division per source pixel to map columns and rows

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_png.c:156`; `Launchstation/src/dash_png.c:169`; `Launchstation/src/dash_png.c:181`; `Launchstation/src/dash_png.c:194`
- Problem: dx = (u64)x * dw / w is computed for every source pixel (and dy per row). The R5900 has no 64-bit divide, so each one is a __udivdi3 call (the builtin is linked into the ELF). A 2048x2048 PNG means about 4.2 million calls. That time lands on the worker (art, theme) or on the UI thread behind the loading overlay (image viewer, dash_image.c:111). The operands never need 64 bits: x, y < 2048 and dw <= w, dh <= h <= 2048, so the products stay below 2^22.
- Fix: Use 32-bit unsigned arithmetic for dx and dy (drop the u64 casts). Optionally hoist dx into a per-column table computed once per image.
- Benefit: Removes one 64-bit division call per source pixel: about 4.2M calls for a 2048x2048 image, 1M for 1024x1024 (INFERRED; per-call cycle cost not measured).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-231: dashTextFit and textWrap re-measure text quadratically on every frame

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_font.c:699-711`; `Launchstation/src/dash_font.c:798-807`
- Problem: When a label does not fit, dashTextFit tries each cut from the full length downward and for every cut re-runs glyphPrefixLength and dashTextWidthN over the whole prefix (O(n^2)); textWrap calls dashTextWidthN from the line start for every byte (O(L^2) per line). Both run on the UI thread every frame: one dashTextFit per non-selected file-browser row (dash_browser.c:3363), list rows in settings/tools/apps, and dashTextWrap bodies in modals (dash_ui.c:1218). A 100-byte name fitting 40 characters costs about 60 iterations of ~140 glyph steps.
- Fix: Measure once going forward: in dashTextFit accumulate glyph advances and stop at the last glyph boundary where width + ellipsis fits; in textWrap keep a running line width and add only the newly completed glyph's advance per step.
- Benefit: Per-call cost from O(n^2) to O(n); roughly 10k glyph steps saved per overflowing row per frame (INFERRED from the loop structure, not timed)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-232: delete() and getFileSize() allocate a full 2048-entry listing (~0.9 MB) per recursion level, so deep trees fail with -ENOMEM partway

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_actions.c:1981-2004`; `Launchstation/src/filer_actions.c:309-333`
- Problem: Each folder level of delete() memaligns (MAX_ENTRY+1) FILEINFO and getFileSize() MAX_ENTRY FILEINFO, kept for the whole subtree (the comments at 310 and 1981 say about 900 KB). The heap is at most ~25 MB, so a folder chain about 20-28 levels deep runs out: delete returns -ENOMEM after removing everything it reached first, and every retry stops at the same depth, so such a tree can never be deleted from the console. Reached from the dashboard Delete (dash_browser.c:1100) and Get Size (dash_browser.c:1183).
- Fix: Let delete() list a small batch per level (for example 64 entries, ~28 KB) by passing a capacity to getDirComplete/listDir. delete() already re-lists a folder until it is empty (the do/while on more at 1988-2004, with the files[MAX_ENTRY] progress sentinel moved to files[capacity]), so only the allocation size and the sentinel index change. getFileSize() cannot re-list, so it can keep the full listing; it already reports 'At least ... error -12'.
- Justification: Adds only a capacity argument to the existing listing call. It removes a real depth limit on delete and cuts the per-level heap from ~0.9 MB to ~28 KB, reusing the re-list loop delete() already has.
- Benefit: Deletes trees of any depth; peak heap per level ~0.9 MB -> ~28 KB (INFERRED: 2048 x FILEINFO, 'about 900 KB' per comment at 310/1981; heap <= 25.46 MB per BUILD-EVIDENCE.md section 7, itself INFERRED).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-233: readHOST elflist.txt parser drops the last entry of a list without a trailing newline and can overrun FILEINFO.name

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer.c:1780-1804`; `Launchstation/src/filer.c:1687-1697`; `Launchstation/include/launchelf.h:562`
- Problem: The parser ends the buffer with a synthetic newline (1782) and returns hostcount - 1 (1804), which only cancels the empty entry made when the file itself ends with a newline (an empty line opens as the folder host:). A list without a trailing newline loses its last ELF, every blank line adds a nameless folder, and a list with no valid line returns -1. Lines are buffered up to MAX_PATH-1 characters (1799) and makeFslPath copies up to 1024 bytes into info[].name, which is 256 bytes, so a line over 255 characters that opens overruns the entry's title and stats.
- Fix: Skip empty lines, return hostcount, and cap a buffered line at sizeof(info->name) - 1 characters (ps2host truncates paths to 255 anyway, iop/ps2host/hostlink.h:68).
- Benefit: Correct elflist.txt listings (last ELF no longer hidden, no blank entries) and no name overrun; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-234: scanRun queries HDIOC_STATUS three times and, on the first scan, libhdd walks the whole APA table before scanRun walks it again

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_hdd.c:151-156`; `Launchstation/ee_libs/hdd/libhdd.c:75-102`; `Launchstation/ee_libs/hdd/libhdd.c:262-293`
- Problem: scanRun reads HDIOC_STATUS into j->status, then calls hddCheckPresent and hddCheckFormatted, which each issue the same devctl again; on the first call of the run hddStatusCurrent is 0, so hddUpdateInfo also Dopen/Dreads every APA header (one disk read per partition segment, the driver caches only a few headers, iop/ps2hdd_osd/hdd.c:218) right before scanRun's own identical walk (172-208). Runs on the worker behind 'Reading the HDD'.
- Fix: Derive both flags from j->status as libhdd does: present = status >= 0 && status < 3, formatted = status == 0, and drop the two libhdd calls. hddMakeFilesystem/hddFormat still call hddUpdateInfo themselves when needed (libhdd.c:306-307, 116-117).
- Benefit: Two devctl RPCs per scan and one full header walk on the first open per run; with hundreds of HDL partitions that is likely seconds of extra wait (INFERRED, not measured)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-235: wipe_mc_folder_contents loops forever when a nested sub-folder path is longer than 63 characters

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/main_exploit_installer.c:1097-1103`; `Launchstation/src/main_exploit_installer.c:1195-1228`; `Launchstation/src/main_exploit_installer.c:1231-1236`
- Problem: For a sub-folder entry wipe_mc_folder_contents calls wipe_mc_folder (1217), which first calls mc_folder_exists. That helper builds 'mcN:/<path>' in a 64-byte buffer and returns 0 ('missing') when snprintf truncates (1102-1103), so wipe_mc_folder returns 0 without deleting (1235-1236). The loop handles only the first entry per pass (break at 1223), rescans (1199-1201), gets the same sub-folder again and never ends. Trigger: Delete update folders on a card where an update folder holds a two-level sub-folder tree longer than 63 characters. It runs on the dashboard UI thread (dash_exploit.c:160), so the console hangs.
- Fix: In the sub-folder branch call wipe_mc_folder_contents(port, child) and then delete_mc_path(port, child) directly: mcGetDir has just listed the entry, so the existence check and its 64-byte path are not needed there.
- Benefit: Removes a possible permanent UI hang during the exploit install, and saves one fileXioDopen per sub-folder (INFERRED). Net lines about 0.
- Effort: S. Risk: High. Confidence: Med (READ)

#### LAUNCHSTATION-236: iconSdf is 30 KB of -O2 code that runs only once at start-up

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_icons.c:274-752`
- Problem: The rasterizer switch iconSdf makes up 30,060 of the 37,192 B .text of dash_icons.o; it has about 234 calls to small SDF helpers (sdSeg 104, sdBox 44, sdCircle 29, sdTriP 19, sdBoxOutline 17, sdRing 15), and the main target is compiled at -O2, so the helpers are expected to be inlined at each call site. The code runs only inside dashIconsInit, once per boot.
- Fix: Mark iconSdf (and dashIconsInit) __attribute__((cold)), which both GCC and Clang compile for size, or mark sdTri/sdBox/sdSeg noinline. Keep it only if a build shows a real .text drop.
- Justification: Adds one attribute; it targets a measured 30 KB start-up-only function, and optimizing it for size is the cheapest way to shrink it without rewriting the icon set. The cost is slightly slower rasterization at boot.
- Benefit: iconSdf 30,060 B .text MEASURED (sizes tsv); the saving is INFERRED (estimated several KB of ELF and RAM) and was not built; the boot-time cost is also not measured.
- Effort: S. Risk: Low. Confidence: Low (INFERRED)

#### LAUNCHSTATION-237: runLaunchKey writes the launch-key title into the shared library entry

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_main.c:965-969`; `Launchstation/src/dash_lib.c:471-476`; `Launchstation/src/dash_lib.c:628-629`
- Problem: dashLibResolve returns &dashApps[i] when the ELF is in the library, and runLaunchKey then copies setting->LK_Title over a->title. After a boot or SELECT+button launch through a titled key, the library entry keeps the key's title (shown on Home/Applications, out of title sort order) if the launch fails. If dashLaunchApp saves the cache (dash_main.c:647-648, art dirty), dashLibBuildCache writes that title to DASHLIB.CNF and it survives reboots until a rescan.
- Fix: Apply the title to a copy: when dashLibResolve returned a library entry, memcpy it into tmp first and set tmp.title, then launch &tmp (dashLaunchApp already works on its own copy).
- Justification: Adds one memcpy. It is the smallest change that keeps a per-key label from overwriting the library title in RAM and in DASHLIB.CNF.
- Benefit: Library titles and DASHLIB.CNF no longer pick up launch-key labels (minor persistent display bug fixed).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-238: dashPickPath memset resets pk.gen, so a listing job left over from the previous picker session is applied to the new one

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_widgets.c:1097-1100`; `Launchstation/src/dash_widgets.c:922-952`; `Launchstation/src/dash_widgets.c:1340-1344`
- Problem: pickListDone applies a listing when pk.active and j->gen == pk.gen. Exiting the picker with a listing still in flight (Triangle while a slow device is read) bumps gen, but the next dashPickPath memsets pk, restarting gen at 0, so the old job's gen can equal the new session's gen. The worker runs jobs FIFO, so the old listing completes first: pk.entries are filled with the previous folder's names under the new pk.dir and pk.loading is cleared until the new job finishes; choosing an entry then returns pk.dir + a name from another folder (dash_widgets.c:1225-1236).
- Fix: Keep the generation across sessions: save pk.gen before the memset at line 1099 and restore it after, so the counter never restarts.
- Justification: Two-line change; it makes the existing stale-listing guard (comment at line 1340) hold across picker sessions instead of only within one.
- Benefit: Removes a wrong-path selection window after reopening the picker while a previous listing is still queued or running (INFERRED, timing-dependent).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-239: genFixPath sprintf into party[MAX_NAME] overflows the stack for an hdd0:/ or dvr_hdd0:/ path whose partition part exceeds about 250 characters

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer.c:948`; `Launchstation/src/filer.c:1051-1062`; `Launchstation/src/filer.c:1079-1090`; `Launchstation/src/lang.c:472`; `Launchstation/src/config.c:1047-1048`; `Launchstation/src/config.c:1058-1059`
- Problem: loc_path (MAX_PATH) takes everything after 'hddN:/', is cut at the next '/', and sprintf(party, "%s%s", hdd_device, loc_path) (1062) or "dvr_hdd0:%s" (1090) writes it into the 256-byte stack buffer party without a bound. APA names are at most 32 characters, so only hand-edited or corrupted text reaches it, for example LAUNCHSTATION.CNF LANG_FILE or CNF_Path (MAX_PATH fields copied by config.c:1048,1059) passed to genFixPath at boot by lang.c:472 and main_modules.c:13.
- Fix: Use snprintf(party, sizeof(party), ...) at both sites and return -1 (bad path) when the result does not fit.
- Justification: Adds one length comparison per site; it closes a stack overflow reachable from config text, and a partition name that long cannot exist, so failing the path is correct.
- Benefit: Removes a boot-time stack overflow on a malformed config path; about 2 lines added (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-240: recoverCardFromPath prepares device stacks after the folder picker without waiting for the picker's in-flight listing job

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_browser.c:2224-2261`; `Launchstation/src/dash_widgets.c:1340-1346`; `Launchstation/src/dash_browser.c:1566-1574`; `Launchstation/src/dash_browser.c:1691-1704`; `Launchstation/src/dash_browser.c:1768-1770`
- Problem: Every other op calls prepareOp() (wait for the worker) after dashPickPath, but recoverCardFromPath calls it before (2224) and then, after the picker, does UI-thread IO (recoveryTargetPath: up to 999 x 2 wleExists) and prepareTransferDeviceStacks, which can load drivers or reload stacks. dashPickPath can return while its own folder listing is still running on the worker (dash_widgets.c:1340: 'a listing still in flight ... frees itself'), e.g. when 'Use this folder' is chosen right after opening a slow SMB/HDD folder, breaking the worker rule in dash_worker.c:7-17. opBackupActiveMmce, opExportActiveMmcePcsx2, opBackupCard and opConvertCard similarly stat the destination after the picker but before prepareOp.
- Fix: In recoverCardFromPath call prepareOp() again right after dashPickPath returns; in the four other ops move the existing prepareOp() call above the wleExists/name checks that follow the picker.
- Justification: One added call in recoverCardFromPath; it closes the one path in this file that can start or reload IOP drivers while a worker listing may still be reading.
- Benefit: Removes a rare race between a worker folder read and driver/stack preparation on the UI thread; +2 lines, the other four are reorders.
- Effort: S. Risk: Low. Confidence: Low (INFERRED)

#### LAUNCHSTATION-241: dashPngDecode leaks the interlaced full-image buffer and row table when libpng fails mid-image

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_png.c:77-84`; `Launchstation/src/dash_png.c:139-154`
- Problem: For interlaced images the decoder mallocs full (w*h*4, up to 256*256*4 = 256 KB) and rows (h pointers) as plain locals. A corrupt or truncated interlaced PNG makes png_read_image call png_error, pngErr longjmps to the handler at :77-84, which frees only row, acc, acc_n and dst. full and rows are lost. Covers, theme images and files opened in the image viewer come from user devices, and each failed open leaks again.
- Fix: Declare full and rows with the other volatile pointers at the top of the function and free them in the setjmp handler, setting them to NULL after their normal frees.
- Justification: Adds two frees and two declarations. Without them each corrupt interlaced PNG leaks up to 257 KB of EE heap per decode attempt (image viewer re-opens, slideshow over a folder). Freeing in the existing handler is the smallest fix.
- Benefit: Up to 256 KB + 8 KB (rows) of EE heap no longer leaked per failed interlaced decode (INFERRED: w*h <= 65536 enforced at :96, 4 bytes per pixel; h <= 2048 pointers).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-242: dash_png.c leaks the interlaced full-image buffer and row table when libpng longjmps inside png_read_image

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_png.c:77-84`; `Launchstation/src/dash_png.c:141-154`
- Problem: full (up to 256*256*4 = 256 KB) and rows are block-local, non-volatile and not freed by the setjmp handler, which frees only row/acc/acc_n/dst. A truncated or CRC-damaged interlaced PNG up to 256x256 used as cover/artwork makes pngReadMem or libpng call png_error during png_read_image, and both buffers leak on every such decode (dash_art.c:343/513, dash_image.c:111).
- Fix: Declare full and rows at function scope as volatile pointers initialised to NULL and free them in the setjmp handler (and set them NULL after the normal frees). Fold this into the loop merge of finding 4.
- Justification: Adds about 3 lines; it is the smallest change that stops a leak of up to ~257 KB of EE heap per damaged interlaced image, which accumulates across art loads.
- Benefit: Stops a leak of w*h*4 + h*4 bytes (<= 257 KB, INFERRED from the 256*256 cap at line 96) per failed interlaced decode.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-243: Safety copies are numbered and pruned by base name only, so same-named images share one sequence and delete each other's copies

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_card_tool.c:237-238`; `Launchstation/src/dash_card_tool.c:275-280`; `Launchstation/src/dash_card_tool.c:290-299`; `Launchstation/src/dash_card_tool.c:311-334`; `Launchstation/src/dash_card_tool.c:1282`; `Launchstation/src/dash_card_tool.c:1312-1313`
- Problem: ctSafetyCopyOf names copies <base>-NNNN.mcd, where base is the image name without extension cut to 47 chars, and the prune keeps 10 across all files matching that base. Images 'X.mcd' and 'X.bin' (or X.vmc/.mc2) in one folder share a sequence on any device. On MMCE all images use one folder, mmceN:/WLE-Safety/, so same-named images in different folders collide too; ctSplitGameId itself makes WLE-GameID/<source>/<ID>/<ID>-1.mcd for each source card. Ten writes to one image delete every safety copy of the other, and the names do not say which card a copy is from.
- Fix: Make the base unique per image: keep the full file name including its extension (drop the dot strip at 275-276), and on MMCE also add a short hash of the full image path. Then numbering and pruning see only that image's copies.
- Justification: Adds a few lines (a path hash for MMCE). The concrete problem is that the 'keep the newest 10 safety copies of this card' guarantee breaks for same-named images: older verified copies of another card are deleted. A per-image base fixes it with no new folder layout.
- Benefit: Stops one card's writes from pruning another card's safety copies; about 3 net lines added (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-244: PrepareMbrLaunchPayload reads xosdmain into fixed RAM at 0x01000000 that Launchstation's own heap and stack may occupy

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/elf.c:12`; `Launchstation/src/elf.c:669-691`; `Launchstation/src/elf.c:698-711`; `Launchstation/src/main_actions.c:383-390`
- Problem: The payload is read straight into RAM from 0x01000000 up to the end of EE RAM (elf.c:669-687) before the launch is committed. The app heap runs from _end (0x797bd4) to the stack with _heap_size=-1, so any heap block (live, or free with bin pointers) above 16 MB is overwritten; the failure paths after the read (secrman load/SecrInit/decrypt, elf.c:698-711; CleanUpForKeepIopExec cancel, main_actions.c:389) return to the dashboard with that memory corrupted. A payload larger than about 16 MB - 128 KB would also overwrite the main-thread stack at the top of RAM during the read loop.
- Fix: Before reading, return -1 when the heap break (sbrk(0)) is above MBR_PAYLOAD_LOAD_ADDR, and cap payload_size64 so the read ends below the main-thread stack instead of at GetMemorySize().
- Justification: The fixed address has to stay (loader.c preserves that window and the target ELF loads low), so the only way to avoid clobbering live heap and the stack is to refuse when the heap already reaches the window; two comparisons on an existing error path, no new code path.
- Benefit: Prevents heap/stack corruption on PSX xosdmain launches that fail or are cancelled after the read (INFERRED: needs more than ~8.4 MB of heap in use). About +3 lines.
- Effort: S. Risk: Med. Confidence: Low (INFERRED)

#### LAUNCHSTATION-245: Apps screen treats dashStoreToggleFavorite/TogglePinned failure (-1, list full) as a removal and shows 'Removed from Favorites'/'Unpinned'

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_apps.c:277-284`; `Launchstation/src/dash_apps.c:452-455`; `Launchstation/src/dash_store.c:807-833`
- Problem: Both toggles return -1 when 48 entries already exist (DASH_MAX_FAV) and change nothing. dash_apps.c tests only r > 0, so -1 shows 'Removed from Favorites' or 'Unpinned from Home'. Home handles the same return correctly with 'Favorites full' (dash_home.c:318-320). Trigger: 48 favorites, press Square (or Options > Add to Favorites / Pin to Home) on a non-favorite app.
- Fix: At the three call sites, show 'Favorites full' (or 'Pins full') when r < 0, as dash_home.c:319-320 does.
- Justification: Adds one branch per site; the current message tells the user an app was removed when nothing changed.
- Benefit: Correct feedback when the list is full; about 4 lines added.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-246: copy() and filerGetGameTitle() join paths into 1025-byte stack buffers with sprintf/strcat and no length check

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_copy.c:849`; `Launchstation/src/filer_copy.c:873`; `Launchstation/src/filer_copy.c:927`; `Launchstation/src/filer_copy.c:1117-1118`; `Launchstation/src/filer_copy.c:942-954`; `Launchstation/src/filer_copy.c:972-979`; `Launchstation/src/filer_copy.c:594-597`; `Launchstation/src/filer_copy.c:633-634`; `Launchstation/src/filer_copy.c:642-643`
- Problem: Each recursion level builds in, out and tmp (char[MAX_PATH], 1025) as the parent path plus a listed name with sprintf, and filerGetGameTitle builds dir and tmpdir with strcpy/strcat (path + name + '/' + name for the PS1 probe). Names come from listings (FAT, exFAT and SMB allow 255 chars; FILEINFO.name is 256) and nothing bounds the total, so a source tree whose path passes about 770 chars (four nested 190-char folder names) overflows copy()'s stack buffers on the UI thread, and long names overflow tmpdir on the worker when the browser lists titles. delete() and getFileSize() bound the same joins with snprintf and return -ENAMETOOLONG.
- Fix: At copy() entry, after newfile is set, return copyDiagFail(-ENAMETOOLONG) when strlen(inPath) + strlen(file.name) + 2 or strlen(outPath) + strlen(newfile.name) + 2 (plus room for the PSU name suffix in PSU backup mode) exceeds MAX_PATH. In filerGetGameTitle build dir and tmpdir with snprintf and return -1 when they do not fit.
- Justification: A stack overflow (crash or silent corruption, EE threads have no stack protector) on input a user can have: deep trees with long names. One bound check at copy() entry covers all five joins, the same pattern delete()/getFileSize() already use.
- Benefit: Closes a stack overflow on long paths; about 4 lines added (READ).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-247: mcCardWriteSave writes save data one 1 KB cluster per lseek+write RPC pair, copying each through a bounce buffer

- Category: 5 (Performance)
- Location(s): `Launchstation/src/mc_card_write.c:408-416`; `Launchstation/src/mc_card_write.c:581-588`; `Launchstation/src/dash_card_tool.c:96-117`
- Problem: Each data cluster is memset+memcpy'd into buf and written with its own writeAt call; the card tool's write callback ctWrite does genLseek (fileXioLseek64) plus genWrite (fileXioWrite) per call. A 1 MB save costs 1024 writes = 2048 EE-to-IOP RPCs on the UI thread. writerAlloc hands out the lowest free clusters in ascending order (mc_card_write.c:374-386), so a chain is usually a few runs of consecutive clusters that could each be one write.
- Fix: In step 1, write each run of consecutive chain clusters with one writeAt straight from fl->data (no memset/memcpy), and send only the final partial cluster through buf with its zero padding.
- Justification: Adds about 4 lines to the write loop. The concrete problem is per-cluster RPC pairs and a 1 KB bounce copy for every cluster of every imported save; grouping consecutive clusters is the smallest change that cuts the call count by the run length without a new buffer or cache.
- Benefit: Write calls per save drop from one per KB to one per contiguous run (often 1-3 per file); removes the per-cluster memcpy. Time saved INFERRED, not measured (roughly 2 RPCs per KB of save data).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-248: Recursive copy() keeps a full 2048-entry listing (896 KB) at every folder level

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/filer_copy.c:915`; `Launchstation/src/filer_copy.c:1163-1164`; `Launchstation/src/filer_copy.c:1179`; `Launchstation/src/filer_actions.c:311`; `Launchstation/src/filer_actions.c:1984`; `Launchstation/include/launchelf.h:205`; `Launchstation/include/launchelf.h:560-565`
- Problem: Each folder level memaligns MAX_ENTRY FILEINFOs (2048 x 448 B = 896 KB) for its listing and holds it while all its subfolders are copied, so a tree d levels deep holds d x 896 KB of listings although a typical folder has a few dozen entries. copy() allows 15 nested levels (MAX_RECURSE 16), about 13 MB. getFileSize() and delete() repeat the same per-level allocation.
- Fix: List into one MAX_ENTRY scratch buffer shared by all levels (getDirComplete writes it only before the children run and again after they return), then memalign only nfiles entries for the level and copy them; do the second listing at line 1179 into the scratch and compare it with the level's copy. getFileSize() can use the same scratch.
- Justification: A 5-level copy holds about 4.4 MB of mostly empty listings out of a heap of about 25 MB (evidence/Launchstation/BUILD-EVIDENCE.md:207), on top of up to 2 MiB of copy buffers, and deeper trees approach 13 MB and can fail with -ENOMEM. One shared scratch keeps the peak at 896 KB plus the entries really listed, for about 6 more lines.
- Benefit: Peak heap of a d-level copy drops from d x 917,504 B to 917,504 B plus the used entries (INFERRED: FILEINFO 256 + 66 + 64-byte-aligned 64-byte stats = 448 B, x MAX_ENTRY 2048).
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-249: Settings reads files on the UI thread without dashWorkerAwaitAll (S_LANGUAGE language reload, SMB2.CNF reads in the share editor)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_settings.c:1917-1919`; `Launchstation/src/dash_settings.c:565-567`; `Launchstation/src/dash_settings.c:3482-3484`; `Launchstation/src/dash_settings.c:496`; `Launchstation/src/dash_settings.c:2122-2125`; `Launchstation/src/dash_worker.c:6-10`
- Problem: dash_worker.c:6-10 says the UI thread does no IO while a job runs, and pathApplied guards Load_External_Language with dashWorkerAwaitAll (2122-2125, 'these read files on the UI thread'). The S_LANGUAGE choice calls the same function (1919) with no guard; it opens and reads setting->lang_file when one is set (lang.c:469-486). The share editor reads SMB2.CNF with dashLoadTextFile (566-567, 3483-3484), and smbSharesSnapshot (496) can re-read LAUNCHSTATION.CNF or mc0/mc1 SYS-CONF (smb_shares.c:81-121, 216-219), with no worker check, while a queued job such as an earlier Save (cfgSaveRun, 2286-2298) may be using the same card.
- Fix: Before each read, call dashWorkerAwaitAll and stop when it returns 0, as pathApplied does: in the S_LANGUAGE case, in smbedWriteFile's base == NULL branch, before the S_SMBED_TEXT read, and before smbedLoadPick at 3786.
- Justification: Each guard adds about 2 lines. It is the documented mechanism (dash_worker.c:15-17) already used at 2124 and 2559 for the same kind of read, and it stops libmc/fileXio being used from two threads at once, which the worker rules forbid.
- Benefit: Removes a race between UI-thread card reads and worker writes during Settings edits; about +8 lines (4 guards x 2). Harm on hardware INFERRED, not reproduced.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-250: BGM.ADP and custom effect files are read on the UI thread at boot and after every IOP reset

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_sound.c:407-450`; `Launchstation/src/dash_sound.c:143-149`; `Launchstation/src/dash_sound.c:507-516`; `Launchstation/src/dash_main.c:1482-1484`; `Launchstation/src/dash_main.c:1517-1521`
- Problem: readAdpSidecar opens and reads the whole file synchronously (up to 1 MB music, plus eight effect files when Custom sounds is on). Its callers run in the dashboard main loop: the boot init (dash_main.c:1520-1521), dashSoundMaintain every frame after an IOP reset (1484), and after misc commands and failed launches (356-358, 676-678). The shipped BGM.ADP is 284,656 B and sits beside the ELF, often on a memory card, so no frame is drawn while it is read. The worker exists so the cursor never waits for IO (dash_worker.c:3-4).
- Fix: Read the sidecar files in a dashWorkerSubmit job and do the audsrv uploads in its completion callback, which already runs on the UI thread. The boot-time call can stay where it is; the gain is mainly the re-reads after IOP resets and with Custom sounds on.
- Justification: The stall is real: a file read of about 278 KB per IOP reset runs on the render thread. Using the existing worker queue (one submit, one run function, one done callback, about 20 lines) is the smallest change that keeps rendering going. No new mechanism is added.
- Benefit: Removes a frame stall of roughly 0.3-1 s per sound re-init when BGM.ADP is on a memory card or USB 1.1 (INFERRED from 284,656 B at a few hundred KB/s; not measured).
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### LAUNCHSTATION-251: Orphan 'endfunc' banners and headings for functions that moved out of draw.c and editor.c

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/draw.c:228-231`; `Launchstation/src/draw.c:322-341`; `Launchstation/src/editor.c:41-50`; `Launchstation/src/editor.c:103-111`; `Launchstation/src/dash_widgets.c:362-364`
- Problem: draw.c keeps empty banners for loadFont, drawChar, drawChar2, printXY, printXY_sjis and transcpy_sjis, which now live in draw_text.c. editor.c keeps 'ends menu', 'endfunc Virt_KeyBoard_Entry', 'endfunc KeyBoard_Entry' and 'ends Window_Selector' banners for code split into editor_menu.c/editor_input.c. dash_widgets.c:362-363 is a heading with no code under it.
- Fix: Delete these comment-only blocks.
- Benefit: ~46 comment lines removed; no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-252: Stale banner comments in filer.c describe code that moved or never existed

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/filer.c:48-52`; `Launchstation/src/filer.c:383-405`; `Launchstation/src/filer.c:889-904`; `Launchstation/src/filer.c:1581-1583`; `Launchstation/src/filer.c:2490-2494`
- Problem: 383-403 holds a commented-out debug buffer, an #ifdef DVRP block containing only comment rules, and an unmountAll description ending in 'ends unmountAll' although unmountAll lives in filer_mount.c:155. 889-903 lists gen* functions (including a nonexistent genInit) and says 'endfunc genLimObjName', which is in filer_fileops.c:186. 1581-1582 says 'endfunc addRootUsbDeviceEntries' after pollRootMemoryCardDevices. 2490-2494 says 'End of file: filer.c' with 85 lines of code after it. 48-52 are blank.
- Fix: Delete these comment blocks.
- Benefit: ~45 source lines of misleading comments removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-253: wle_stricmp and wle_strnlen reimplement newlib strcasecmp and strnlen

- Category: 2 (Duplication)
- Location(s): `Launchstation/include/launchelf.h:108-147`; `Launchstation/src/ember.c:206`
- Problem: launchelf.h carries static inline copies of ASCII case-insensitive compare and strnlen, aliased by macro to stricmp/strnlen. The EE build already calls newlib's strcasecmp/strncasecmp directly (dash_card_tool.c:1715, cloud_http.c:509), so the library versions are linked and available; strnlen has only two callers.
- Fix: Replace the helpers with '#define stricmp strcasecmp' (include strings.h) and use newlib strnlen; change ember.c:206 to strcasecmp.
- Benefit: ~36 lines removed from the umbrella header included by every EE file; negligible ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-254: genOpen retries every failed open with the other dev:/path spelling

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_fileops.c:575-610`
- Problem: After any failed fileXioOpen, genOpen rewrites dev:/path to dev:path (or back) and opens again, so every expected failure (probing for a missing file, O_CREAT in a missing folder, verifyUdpfsCopy) costs a second IOP or network round trip. No caller or comment names a device that needs the alternate spelling; the code dates from the fork's initial commit 944a2fc.
- Fix: Delete the fallback after checking on hardware which device, if any, still needs it; if one does, limit the retry to that device prefix.
- Benefit: ~36 source lines; one fewer IOP RPC per failed open (INFERRED).
- Effort: M. Risk: Med. Confidence: Low (INFERRED)

#### LAUNCHSTATION-255: mcSaveFormatExt and mcSaveFormatName are never called

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/mc_save_formats.c:878-902`; `Launchstation/include/mc_save_formats.h:60-62`
- Problem: Neither function has a caller: the only matches for the names in the repo are the definitions and the prototypes (no src, test or script use).
- Fix: Delete both functions and their prototypes.
- Benefit: 28 source lines. MEASURED: --gc-sections already drops their 2 x 56 bytes of .text (raw/link/wLaunchELF-gc-discarded-all.tsv); their switch tables (2 x 24 B) and label strings stay in .rodata because the build has no -fdata-sections (INFERRED, ~150 B).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-256: Small dead items in dash_themes.c: dashThemeBgId, dashThemePresetIndex, dashThemeMarkCustom, write-only store_seen and FolderCtx.folder_path

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_themes.c:141-144`; `Launchstation/include/dash_internal.h:284`; `Launchstation/src/dash_themes.c:469-472`; `Launchstation/include/dash_internal.h:257`; `Launchstation/src/dash_themes.c:631-634`; `Launchstation/include/dash_internal.h:262`; `Launchstation/src/dash_themes.c:44`; `Launchstation/src/dash_themes.c:904`; `Launchstation/src/dash_themes.c:913`; `Launchstation/src/dash_themes.c:1017-1021`; `Launchstation/src/dash_themes.c:1036-1042`; `Launchstation/src/dash_themes.c:1066`; `Launchstation/src/dash_themes.c:402`
- Problem: dashThemeBgId, dashThemePresetIndex and dashThemeMarkCustom have no caller (definition and prototype only). store_seen is assigned at 904/913 and never read. FolderCtx.folder_path is assigned but folderKv reads only ctx->t, so the struct is a needless wrapper around DashTheme*. P_CRT sets bold = 1, which base() already set (line 217).
- Fix: Delete the three functions and prototypes, store_seen and its two assignments, the FolderCtx struct (pass the DashTheme pointer straight to dashKvParse as user data), and the redundant bold line in P_CRT.
- Benefit: ~25 source lines removed. The three functions are already gc-discarded (MEASURED: wLaunchELF.gc-sections.log:553-555), so no ELF change from them.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-257: Small path-helper duplication: mc0/mc1 open blocks, elf.c copy of isHddPartyPath/isHddDevicePath, split_parent_leaf branches

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/popstarter_path.c:290-318`; `Launchstation/src/popstarter_path.c:20-27`; `Launchstation/src/popstarter_path.c:124-130`; `Launchstation/src/elf.c:357-365`; `Launchstation/src/self_update.c:68-88`
- Problem: openPathForRead repeats the open + copy-opened_path block three times (genFixPath, mc0:, mc1:) and the final opened_path copy once more; elf.c:357-365 re-defines static isHddPartyPath (identical to popstarter_path.c:124-130) and isHddBrowserPath (equal to isHddDevicePath, popstarter_path.c:20-27); split_parent_leaf has two identical branches that differ only in the separator found by strrchr.
- Fix: In openPathForRead loop over the candidate paths with one open/copy block. Delete elf.c's two statics and use the popstarter_path.c functions via a prototype. In split_parent_leaf pick sep = strrchr('/') or else strrchr(':') and keep one copy of the branch.
- Benefit: ~25 lines removed (about 10 in openPathForRead, 9 in elf.c, 10 in split_parent_leaf, minus a few for the loop); no ELF change worth stating (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-258: Small redundancies in dash_store.c: twin toggle functions, zero stores after memset, one-line buildSettings wrapper, always-0 int returns

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_store.c:807-833`; `Launchstation/src/dash_store.c:248-267`; `Launchstation/src/dash_store.c:564-567`; `Launchstation/src/dash_store.c:640`; `Launchstation/src/dash_store.c:683-692`; `Launchstation/src/dash_store.c:716-720`
- Problem: dashStoreToggleFavorite and dashStoreTogglePinned are the same 13 lines with fav/pin swapped. dashStoreDefaults memsets dashCfg and then stores 0 or '' into ten fields again (248, 251, 254, 255, 259, 261, 262, 264, 266, 267). buildSettings only forwards to dashStoreSettingsText(&persisted) for its one caller (640). dashStoreSaveSettings and dashStoreSaveLists always return 0 and no caller uses the value.
- Fix: Make one static toggle(list, &n, path) used by both public toggles; drop the zero stores (keep the explanatory comments on the remaining lines if wanted); call dashStoreSettingsText(&persisted, ...) directly in submitSave; make the two save functions void.
- Benefit: ~25 source lines removed; a few dozen bytes of .text (INFERRED: one function body instead of two).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-259: kelf_signer.c KelfKeyStore duplicates KelfParsedKeys and is filled by a block of memcpy calls

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/kelf_signer.c:92-107`; `Launchstation/src/kelf_signer.c:710-729`
- Problem: parse_key_store already holds every key in merged.value[KEY_*] with presence flags, then copies each field one by one into a second struct, KelfKeyStore, with the same contents.
- Fix: Delete KelfKeyStore and pass the merged KelfParsedKeys to the signing helpers, reading value[KEY_SIG_MASTER] etc.; has_override becomes present[KEY_OVERRIDE_KBIT].
- Benefit: ~25 source lines removed; about 140 bytes less stack per signing call (INFERRED: sizeof KelfKeyStore 8+8+16+8+16+8+8+16+8+8+16+16+4 = 140)
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-260: embed_blobs.S: three embed macros differ only in the .incbin arguments

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/embed_blobs.S:16-50`
- Problem: EMBED_FILE, EMBED_SLICE and EMBED_TAIL are the same 11-line macro body; only the .incbin operand list differs (file / file,off,len / file,off).
- Fix: Keep one macro that takes the symbol and a variadic .incbin operand list (EMBED(sym, ...) with .incbin __VA_ARGS__) and use it for all 26 entries.
- Benefit: ~22 source lines; no ELF change (INFERRED: same directives emitted).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-261: BCD-to-int helper defined in six EE source files

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_clock_sync.c:256-259`; `Launchstation/src/dash_card_tool.c:1373-1376`; `Launchstation/src/dash_browser.c:1257-1260`; `Launchstation/src/main_history.c:22-25`; `Launchstation/src/mmce_backup.c:45-48`; `Launchstation/src/main_title.c:46-49`
- Problem: fromBcd, ctBcd, dashBcdToInt, bcdToInt (twice) and bcd_to_int are the same one-line function for decoding sceCdCLOCK fields, each followed by the same six-field decode of the RTC.
- Fix: Declare one static inline bcd decoder in a shared header (main_title.h, which already owns RTC conversion) and delete the six copies.
- Benefit: ~20 lines removed (6 copies of 4 lines minus one 3-line inline; INFERRED); no binary change expected.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-262: Small redundancies in filer_copy.c, filer_mount.c and filer_device.c

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_copy.c:1697-1702`; `Launchstation/src/filer_copy.c:1425`; `Launchstation/src/filer_copy.c:1361`; `Launchstation/src/filer_copy.c:1395`; `Launchstation/src/filer_copy.c:1112`; `Launchstation/src/filer_copy.c:85-89`; `Launchstation/src/filer_copy.c:119-120`; `Launchstation/src/filer_mount.c:109-111`; `Launchstation/src/filer_mount.c:182-186`; `Launchstation/src/filer_device.c:13-15`; `Launchstation/include/launchelf.h:571`
- Problem: copyDiagFail(ret) runs twice on the normal exit (1697, 1702), and the copy_file_exit_mem_err label (used only at 1425, where buff and buff2 are NULL) is unnecessary; copy_diag.total is set at 1361 and again at 1395; iox_stat.attr = 0 at 1112 is a dead store; the 85-89 comment says LaunchELF is single-threaded though chunks now run on the worker, and 119-120 is an orphan comment for psu_restore_buff (declared at 272); unmountParty re-tests party_ix < MOUNT_LIMIT after rejecting it at 94; unmountAll clears mountedDVRPParty[i][0] behind a needless test; filer_device.c re-declares host_error that filer_internal.h:12 already declares; latestDVRPMount is declared in both launchelf.h:571 and filer_internal.h:8.
- Fix: Delete the duplicate call, label and goto target (use copy_file_exit), the second total assignment, the dead store, the stale/orphan comments, the redundant tests and the duplicate extern declarations.
- Benefit: ~20 source lines; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-263: mmce_igr.c prefix_case_equal and mmce0:/mmce1: prefix tests re-implement strncasecmp and mmceCardUnit

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mmce_igr.c:46-64`; `Launchstation/src/mmce_igr.c:76`; `Launchstation/src/mmce_igr.c:117-118`; `Launchstation/src/mmce_igr.c:559`; `Launchstation/src/mmce_igr.c:660`; `Launchstation/src/mmce_igr.c:760`; `Launchstation/src/mmce_card_switch.c:43-48`
- Problem: prefix_case_equal (19 lines, one caller at line 76) is strncasecmp(name, "BootCard", 8) == 0, and strncasecmp is already used in the same build (mmce_card_switch.c:68). The pair strncmp(boot, "mmce0:", 6) && strncmp(boot, "mmce1:", 6) is written three times, and mmceCardUnit already answers exactly that.
- Fix: Delete prefix_case_equal and call strncasecmp at line 76; replace the three mmce prefix tests (and the one in portable_mmce_path) with mmceCardUnit(boot) < 0, including mmce_card_switch.h.
- Benefit: about 20 source lines; a few dozen bytes of code INFERRED
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-264: Unreachable repeated conditions and redundant tests in main_exploit_installer.c

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main_exploit_installer.c:2111-2116`; `Launchstation/src/main_exploit_installer.c:539-540`; `Launchstation/src/main_exploit_installer.c:2243-2244`; `Launchstation/src/main_exploit_installer.c:409-410`; `Launchstation/src/main_exploit_installer.c:395-397`; `Launchstation/src/main_exploit_installer.c:372`; `Launchstation/src/main_exploit_installer.c:378-379`
- Problem: build_magicgate_result_message tests console_is_PSX twice; the second block (with a hard-coded English message) can never run. get_psx_desr_payload repeats the dtl_signing_console test at 539-540 after already returning for it at 535-536. build_install_plan's EXPLOIT_MODE_MG_TEST case is unreachable (MG test is handled at 2424 before the call). get_custom_payload_header 409-410 and get_custom_payload_keyset 395-397 return the same value as their fall-through. starts_with_model("COH") already covers "COH-H", and model_contains("PYTHON") covers "PYTHON 1"/"PYTHON1".
- Fix: Delete the second console_is_PSX block, the repeated dtl test, the MG_TEST case, the two redundant if branches and the three subsumed model tests.
- Benefit: ~18 lines removed; a dead 170-byte English string literal and a few compares leave .rodata/.text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-265: dash_info.c: 'Debug features' block tests UDPTTY/SIO_DEBUG/POWERPC_UART/NO_IOP_RESET, which no build defines

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_info.c:269-285`
- Problem: The block is compiled only when one of four macros is defined. None is set by ps2.yaml or scripts/configure_ds34.py; SIO_DEBUG appears only as a commented-out define (include/launchelf.h:38). The same macro family is tested elsewhere (init.c, launchelf.h:5-10), so it is a manual debug-build switch, not a shipped feature.
- Fix: Delete the block, or delete it together with the rest of the UDPTTY/SIO_DEBUG/POWERPC_UART/NO_IOP_RESET support if the owner retires those debug builds (that wider cleanup belongs to the units owning init.c and launchelf.h).
- Benefit: 17 lines removed; no ELF change, the block is never compiled (INFERRED from the build defines).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-266: File-type extension lists and size formatting written several times in dash_browser.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_browser.c:125-127`; `Launchstation/src/dash_browser.c:213-216`; `Launchstation/src/dash_browser.c:773-775`; `Launchstation/src/dash_browser.c:2778-2780`; `Launchstation/src/dash_browser.c:3181-3195`; `Launchstation/src/dash_browser.c:957-965`; `Launchstation/src/dash_browser.c:174-177`; `Launchstation/src/dash_browser.c:1199-1204`; `Launchstation/src/dash_browser.c:3197-3219`
- Problem: fileIcon re-tests ELF/KELF/XLF (also in fbFileType 213 and activate 773) and PNG/JPG/JPEG (isImageFile 125-127); the ELF/XLF/KELF/VCD/ISO/CUE launchable list is spelled out in activate and again in actionsMenu 2778-2780. fmtSize recomputes fbFileBytes (174-177) and repeats fmtBytes' GB/MB branches; opSize has a third GB/MB formatter. ps1SaveTargets loops with a hard-coded 6 instead of the array size.
- Fix: Have fileIcon use fbFileType and isImageFile; use one static isLaunchableExt() in activate and actionsMenu; make fmtSize call fbFileBytes and fmtBytes; use the array size in ps1SaveTargets. tests/test_zack_launch_sizing.py and test_png_viewer.py grep these functions.
- Justification: isLaunchableExt replaces two copies of the same six-extension test; it removes lines overall.
- Benefit: ~15 lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-267: Small leftovers in loadConfig/preloadCNF: unused var_cnt and tst, empty legacy-key branches, overlapping mc-port helpers

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/config.c:885`; `Launchstation/src/config.c:902`; `Launchstation/src/config.c:950`; `Launchstation/src/config.c:517-521`; `Launchstation/src/config.c:1011-1013`; `Launchstation/src/config.c:1023-1025`; `Launchstation/src/config.c:949-953`; `Launchstation/src/config.c:1104-1105`; `Launchstation/src/config.c:181-212`
- Problem: var_cnt is incremented but never read; tst is assigned and never read (loadConfig and preloadCNF); the Menu_Pages and Menu_Title branches do nothing, and unknown keys are already ignored by the final else; CNF_version feeds only a DPRINTF that is empty in shipped builds (launchelf.h:164); getExplicitMcPortFromPath repeats the first half of getMcPortFromPath.
- Fix: Drop var_cnt, tst and the two empty branches; drop CNF_version handling or keep only the key skip; have getMcPortFromPath call getExplicitMcPortFromPath before its mc: case.
- Benefit: ~15 lines removed; no measurable ELF change (INFERRED, compiler already drops unused locals).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-268: Small redundancies in filer_browser.c: mx4sio wrapper, recomputed rows, LaunchElfDir path fix, unused width term

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_browser.c:142-148`; `Launchstation/src/filer_browser.c:2268`; `Launchstation/src/filer_browser.c:1741`; `Launchstation/src/filer_browser.c:1745`; `Launchstation/src/filer_browser.c:1223-1229`; `Launchstation/src/filer_browser.c:2517-2528`; `Launchstation/src/filer_browser.c:822-823`
- Problem: markMx4sioDestinationAfterWrite (142-148, one caller at 2268) repeats the NULL and 'mx4sio' checks that discardNextMx4sioRootListing already makes through isMx4sioRootPath (filer.c:1482-1495). Lines 1741 and 1745 recompute font_height and rows exactly as 1724-1725 did with nothing changed in between. The R3 folder branch (1223-1229) re-implements filerLaunchElfFolder (2517-2528). In BrowserModePopup, the width term for LNG(Timestamp) alone (822-823) can never exceed the Timestamp+Old_to_New term at 816.
- Fix: Call discardNextMx4sioRootListing(path) at 2268 and delete the wrapper; delete lines 1741 and 1745; replace 1223-1229 with filerLaunchElfFolder(path, sizeof(path)); delete 822-823.
- Benefit: ~15 source lines removed; negligible binary change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-269: Unused ST_INFO item type, unused choiceInfo 'single' out-parameter and write-only TargetsJob.finished

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_settings.c:28`; `Launchstation/src/dash_settings.c:3962`; `Launchstation/src/dash_settings.c:3965-3972`; `Launchstation/src/dash_settings.c:1004-1007`; `Launchstation/src/dash_settings.c:3812-3813`; `Launchstation/src/dash_settings.c:3997-3998`; `Launchstation/src/dash_settings.c:2650`; `Launchstation/src/dash_settings.c:2665`; `Launchstation/tests/test_settings_navigation.py:111`
- Problem: No SetItem uses ST_INFO, so the draw branch that prints ip/netmask (3965-3972) and its icon case never run. choiceInfo always sets *single = NULL, and neither caller reads it. targetsDone sets j->finished, but nothing reads that field: saveClassicAs uses dashWorkerAwait's result and j->abandoned.
- Fix: Delete ST_INFO, its icon case and the draw branch. Drop the 'single' parameter from choiceInfo and its two callers, plus the stub signature in tests/test_settings_navigation.py. Delete the finished field and the assignment.
- Benefit: ~15 source lines removed; tiny .text change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-270: cnfmode values USBD_IRX_CNF and USBMASS_IRX_CNF are never passed to getFilePath; stale banner describes them

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_browser.c:963-971`; `Launchstation/src/filer_browser.c:1122`; `Launchstation/include/launchelf.h:181`; `Launchstation/include/launchelf.h:187`; `Launchstation/src/filer.c:168`; `Launchstation/src/filer.c:174`; `Launchstation/src/filer.c:184`; `Launchstation/src/filer.c:190`; `Launchstation/src/filer.c:2279`
- Problem: No getFilePath call uses USBD_IRX_CNF or USBMASS_IRX_CNF (callers use USBKBD_IRX_CNF, KBDMAP_FILE_CNF, CNF_PATH_CNF, LANG_CNF, FONT_CNF, LK_ELF_CNF, TEXT_CNF, ELF_FILE_CNF, SAVE_CNF, DIR_CNF, FALSE/TRUE). The comment banner at 963-971 gives examples with setting->usbd_file and setting->usbmass_file, fields that no longer exist.
- Fix: Delete the two enum values, their entries in cnfmode_extU/cnfmode_extL, their tests at filer_browser.c:1122 and filer.c:2279, and the banner lines 963-971.
- Benefit: ~15 source lines removed; 8 bytes of .data for the two table rows (INFERRED: 2 rows x 4 bytes in two tables)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-271: dash_home.c: identical UP/DOWN branches, NULL checks on the const dashAppsScreen callbacks, and geometry computed twice

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_home.c:267-278`; `Launchstation/src/dash_home.c:171-172`; `Launchstation/src/dash_home.c:262-263`; `Launchstation/src/dash_home.c:349-350`; `Launchstation/src/dash_home.c:427-428`; `Launchstation/src/dash_home.c:539-540`; `Launchstation/src/dash_home.c:150-155`; `Launchstation/src/dash_home.c:359-364`; `Launchstation/src/dash_home.c:489-491`; `Launchstation/src/dash_home.c:505-508`
- Problem: The DASH_IN_UP and DASH_IN_DOWN branches of homeInput are identical. dashAppsScreen is a const struct with all five callbacks set (dash_apps.c:799), and dash_files.c:685-687 calls such screens without checks, so the five NULL tests are dead. The Continue-row camera clamp (150-155, 359-364) and the chip width clamp (489-491, 505-508) are each written twice.
- Fix: Test (DASH_IN_UP | DASH_IN_DOWN) in one branch; call dashAppsScreen callbacks directly; compute the camera target in one small static function and the chip width once per chip.
- Benefit: ~15 lines removed (INFERRED from the cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-272: dash_internal.h/dash.h/draw_private.h: unused macros and enum, duplicate prototypes, stale comments

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/dash_internal.h:23-24`; `Launchstation/include/dash_internal.h:40`; `Launchstation/include/dash_internal.h:285`; `Launchstation/include/dash_internal.h:553-561`; `Launchstation/include/dash_internal.h:990`; `Launchstation/include/dash_internal.h:1143`; `Launchstation/include/dash.h:11-13`; `Launchstation/include/dash.h:35`; `Launchstation/include/draw_private.h:7`
- Problem: DASH_SAFE_T and DASH_SAFE_B are never used. DASH_SELECT_EXTRA_W is used only by a test that copies it (tests/test_editor_keyboard.py:49), not by source. enum DashAnim (556-561) and DASH_FX_COUNT are never referenced. dashWorkerIoBusy is prototyped in both dash.h:35 and dash_internal.h:990 (dash_internal.h includes dash.h), and gsScreenGeneration is declared in both draw_private.h:7 and dash_internal.h:1143. The comment at dash_internal.h:285 belongs to dashThemeSetFocusApp (300), not to the enum below it. dash.h:11-13 says DashRun returns to the classic menu, but DashRun never returns (dash_main.c:1396).
- Fix: Delete the unused macros, enum DashAnim and DASH_FX_COUNT (keeping the test in step), the second dashWorkerIoBusy prototype and one gsScreenGeneration extern. Move or delete the stray comment and correct the DashRun comment.
- Benefit: ~15 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-273: editor_rules.c: the same row-find loop is written four times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/editor_rules.c:36-41`; `Launchstation/src/editor_rules.c:48-53`; `Launchstation/src/editor_rules.c:59-64`; `Launchstation/src/editor_rules.c:81-86`
- Problem: Home, End, Line/Page Up and Line/Page Down each run the identical loop that accumulates Editor_nRowsWidth until it passes Editor_Cur+1 to find the cursor row.
- Fix: Compute the cursor row index and its cumulative end once before the four branches (or in one static helper) and reuse it.
- Justification: One computation replaces four identical loops, net fewer lines.
- Benefit: ~15 source lines (INFERRED from the four 6-line ranges); negligible .text.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-274: hdd/dvr_hdd blocks of formatBrowserPathForDisplay are the same code twice

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_browser.c:278-302`; `Launchstation/src/filer_browser.c:304-325`
- Problem: The HDD block (278-302) and the DVRP block (304-325) split 'dev:/partition/sub' into partition and subpath with identical code and print '<dev><partition>:pfs:<sub>'; they differ only in the prefix length (6 vs 10) and the device text.
- Fix: Compute the prefix length and device text first (hddN: or dvr_hdd0:) and run one copy of the split-and-print code.
- Benefit: ~15 source lines removed; small .text saving (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-275: mc0/mc1 to mc: rewrite open-coded four times; dash_settings.c already has mcAlias

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/config.c:1567-1571`; `Launchstation/src/config_startup.c:177-181`; `Launchstation/src/config_startup.c:186-190`; `Launchstation/src/config_startup.c:195-199`; `Launchstation/src/dash_settings.c:2098-2106`
- Problem: The same 'if path starts with mc0 or mc1, snprintf "mc%.*s" and strcpy back' block appears four times in the classic screens, while dash_settings.c:2098 has a static mcAlias doing the same. Moot if finding 1 is applied.
- Fix: If the classic screens stay, call one shared mcAlias (moved next to configResolveMcAliasPath in config.c) instead of the four copies.
- Benefit: ~15 lines removed (4 x 5 lines minus 4 calls; INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-276: mc_save_formats.c nameOk duplicates the exported mcCardNameOk

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_save_formats.c:44-58`; `Launchstation/src/mc_card_write.c:38-54`
- Problem: nameOk(name, n) applies the same rule as mcCardNameOk (its own comment says so); every call passes n = 32 or 64, and since names of 32 or more bytes are rejected either way the result equals mcCardNameOk(name), which mc_card_write.h (already included) exports.
- Fix: Delete nameOk and call mcCardNameOk(name) in startSave and addFile.
- Benefit: 15 source lines; tens of bytes of code INFERRED
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-277: remoteCommandPoll stacks a second 3 s throttle under the dashboard's 500 ms one, stats before opening, and has a never-built #else stub

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/remote_control.c:9-12`; `Launchstation/src/remote_control.c:49-54`; `Launchstation/src/remote_control.c:39`; `Launchstation/src/remote_control.c:101-107`; `Launchstation/src/dash_main.c:1458-1462`; `Launchstation/src/pc_game_receiver.c:323-327`
- Problem: dash_main.c:1458-1461 already calls the poll every 500 ms, then remoteCommandPoll keeps its own next_remote_poll and only acts every 3000 ms, so the outer gate has no effect on it (pc_game_receiver.c:323-325 does the same). fileXioGetStat at :53 only precedes fileXioOpen, which fails the same way when the file is missing (one extra IOP RPC per poll). ETH and EXFAT are always defined (ps2.yaml:624,629), so the #else stub at :101-107 is never compiled.
- Fix: Keep one throttle: drop next_remote_poll/REMOTE_COMMAND_INTERVAL_MS here (and next_poll in pc_game_receiver.c) and set the dash_main.c gate to the wanted interval, or the reverse. Delete the fileXioGetStat call and the #if/#else wrapper.
- Benefit: ~15 lines removed across the two pollers; one fewer fileXio RPC per poll (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-278: Classic editor leftovers: unused HFONTS, always-true conditions, commented-out code

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/editor_input.c:6-7`; `Launchstation/include/editor_private.h:92`; `Launchstation/src/editor_menu.c:54`; `Launchstation/src/editor_menu.c:61`; `Launchstation/src/editor_menu.c:110`; `Launchstation/src/editor_menu.c:193`; `Launchstation/src/editor_menu.c:199`; `Launchstation/src/editor_file.c:191`; `Launchstation/src/editor_file.c:278-281`; `Launchstation/src/editor_input.c:386`; `Launchstation/src/editor_input.c:390`; `Launchstation/src/editor_input.c:421`; `Launchstation/src/editor_input.c:663`; `Launchstation/src/editor_input.c:667`; `Launchstation/src/editor_input.c:691`
- Problem: HFONTS (editor_input.c:7, extern editor_private.h:92) has no user; gui.c:149 has its own local HFONTS. In editorMenu, Menu_Sel < NUM_MENU (54, 61, 110) is always true because NEW/OPEN/EXIT are never disabled; in editorSelectWindow the else-if Window[i][OPENED] (193) is always true there, and Window_Sel <= 10 (199) always holds (0..9). Lines 191, 278-281 and the memset comment lines in editor_input.c are dead commented-out code.
- Fix: Delete HFONTS and its extern, drop the always-true conditions (plain else at 193), and delete the commented-out lines.
- Benefit: 4 bytes .rodata MEASURED (BUILD-EVIDENCE.md:160 lists editor_input.o HFONTS as unreferenced but kept); ~14 source lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-279: Dead accessors in dash_store.c: dashStorePersisted, dashStoreLoaded and the write-only store_loaded flag

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_store.c:699-702`; `Launchstation/include/dash_internal.h:644`; `Launchstation/src/dash_store.c:466-469`; `Launchstation/include/dash_internal.h:635`; `Launchstation/src/dash_store.c:21`; `Launchstation/src/dash_store.c:433`; `Launchstation/src/dash_store.c:609-610`
- Problem: dashStorePersisted and dashStoreLoaded have no caller in any source; store_loaded is read only by dashStoreLoaded, so its writes at 433 and 609-610 are dead too.
- Fix: Delete both functions, their prototypes, store_loaded and its writes. tests/test_device_autostart_safe_mode.py:23 uses the text 'int dashStoreLoaded(void)' as the end marker of a slice; point it at the next function (dashStoreSettingsText or the Saving banner).
- Benefit: ~14 source lines removed. Both functions already gc-discarded (MEASURED: wLaunchELF.gc-sections.log:504-505).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-280: Device branches in delete() and newdir() identical to the generic else branch

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer_actions.c:2040-2041`; `Launchstation/src/filer_actions.c:2218-2222`; `Launchstation/src/filer_actions.c:2242-2248`
- Problem: In delete() the vmc file branch (genRemove(dir)) equals the final else. In newdir() the vmc branch and the ETH/UDPFS host/udpfs branch both do strcpy/strcat/genLimObjName/filerMkdirNoOverwrite, exactly like the final else (2249-2253). None of these prefixes is caught by an earlier branch, so removing them changes nothing.
- Fix: Delete the vmc branch at 2040-2041, and the vmc and host/udpfs branches (with their #if/#endif) in newdir.
- Benefit: ~14 lines removed; a few dozen bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-281: Unused section-name helpers, write-only prev_section and a duplicate extern in dash_main.c

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_main.c:126-129`; `Launchstation/src/dash_main.c:51-54`; `Launchstation/src/dash_main.c:46`; `Launchstation/src/dash_main.c:25`; `Launchstation/src/dash_main.c:234`; `Launchstation/src/dash_main.c:914`; `Launchstation/include/dash_internal.h:1040`; `Launchstation/include/dash_internal.h:1057`
- Problem: dashVisibleSectionName has no caller; dashSectionName is called only by it, and section_names only by dashSectionName (both functions are already gc-discarded). prev_section is assigned at 234 and never read. `extern u32 paddata;` is declared twice in the same file (267 and 914).
- Fix: Delete dashVisibleSectionName, dashSectionName, section_names and their two prototypes, delete prev_section and its assignment, and delete the second paddata extern. test_unified_dashboard_navigation.py:36 uses the text 'const char *dashSectionName' as a slice anchor and needs another anchor.
- Benefit: ~14 lines removed; functions already gc-discarded (216+52 B MEASURED, wLaunchELF-gc-discarded-first-party.tsv:37,39); section_names stays in .rodata without -fdata-sections: ~24 B pointers + ~37 B strings (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-282: artRun probes each candidate with fileExists before dashPngLoadFile, which already does the same genFixPath+genOpen and fails on a missing file

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_art.c:151-163`; `Launchstation/src/dash_art.c:375-376`
- Problem: fileExists opens and closes the file, then tryDecode -> dashPngLoadFile -> dashLoadTextFile runs genFixPath and genOpen on the same path and returns NULL when it is missing (dash_store.c:115-119). For missing candidates both cost one failed open; for the found file the worker does an extra open/close, which is slow on memory cards and network devices.
- Fix: Delete fileExists and call tryDecode(j, cand) directly at line 375.
- Benefit: One open+close less per discovered artwork file on the worker thread; 14 lines removed (READ).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-283: canonicalizeAtaPath and canonicalizePcCardCfPath differ only in the prefix

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer.c:684-716`
- Problem: Both functions map 'pfx:' to 'pfx0:...' and 'pfxN:' to 'pfxN:/' with identical structure; only the prefix 'ata'/'cf' and its length differ.
- Fix: Merge into one static function taking the prefix (length from strlen), with the two call sites (1012, 1021, 2006, 2211) passing "ata" or "cf".
- Justification: Replaces two 16-line twins with one; net saving about 14 lines.
- Benefit: ~14 source lines net; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-284: draw_gs.c: TV mode to GS mode mapping written twice, and updateScreenMode recomputes what applyGSParams sets

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/draw_gs.c:104-121`; `Launchstation/src/draw_gs.c:156-165`
- Problem: setupGS and updateScreenMode each contain a switch mapping TV_mode_* to GS_MODE_*. updateScreenMode then recomputes height/interlace/field from the GS mode with ternaries that restate the per-mode values already encoded in applyGSParams.
- Fix: Use one static tvModeToGsMode() in both functions. tests/test_display_feedback.py:132 asserts the source contains 'case TV_mode_576P:' twice and must be updated with it.
- Justification: One mapping function replaces two copies; net lines go down.
- Benefit: ~14 lines removed (INFERRED from cited ranges); no measurable size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-285: psu_functions.c fills the PSU root header, the mcT_header copy and the 1 KB pad size twice each

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/psu_functions.c:84-92`; `Launchstation/src/psu_functions.c:238-245`; `Launchstation/src/psu_functions.c:333-340`; `Launchstation/src/psu_functions.c:366-372`; `Launchstation/src/psu_functions.c:276-279`; `Launchstation/src/psu_functions.c:361-364`
- Problem: The folder root header (attr, size, cTime, mTime, name, unknown_1_u16, unknown_2_u64) is built identically in psu_backup_create_root_image and psu_backup_finalize_root_image; the psu_header -> mcT_header copy is the same 7 lines in psu_restore_open_root_image and psu_restore_read_file_entry; the pad-size if/else appears twice and psu_restore_check_entries already uses the one-expression form (:402).
- Fix: Add one static fill_root_header(psu_header*, const mcT_header*, int count) and one static copy_to_mc_header(mcT_header*, FILEINFO*, const psu_header*) used by both callers; write the pad size as (0x400 - (size & 0x3FF)) & 0x3FF in both places.
- Justification: Each helper replaces two identical 7-8 line blocks with one, so the file shrinks.
- Benefit: ~14 lines removed; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-286: save_refresh_token re-reads and re-parses the staged token although dashWriteFile already verified it byte for byte

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/cloud_google.c:333-344`; `Launchstation/src/dash_store.c:163-174`
- Problem: dashWriteFile writes the file and then reads it back and compares every byte, returning -1 on any difference (dash_store.c:163-174). save_refresh_token then reads the same WLE-CLOUD.TOK.NEW a second time, parses it and compares the token (333-344). A byte-identical file always parses to the same token, so this check can never fail when dashWriteFile succeeded. It only costs one more 8 KB allocation and a memory-card read per link.
- Fix: Delete the read-back block 333-344 (and the now unused verify, saved, saved_len locals). The wider TOK.NEW staging scheme could also go, since losing the token only means linking again, but that is optional.
- Benefit: ~14 lines removed and one card read per Google link (READ). Dropping the whole staging scheme would remove roughly 30 more lines (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-287: cloudHttpDownloadFd has no caller (self-updater moved to cloudHttpDownloadMem)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/cloud_http.c:1279-1287`; `Launchstation/include/cloud_http.h:30-36`
- Problem: The only C mentions of cloudHttpDownloadFd are its definition, its prototype and a comment at cloud_http.h:42. self_update.c:204 calls cloudHttpDownloadMem, and Drive recovery calls cloudHttpDownloadGoogleFd. The linker already drops it: the gc log lists .text.cloudHttpDownloadFd (80 bytes) as removed.
- Fix: Delete cloudHttpDownloadFd and its prototype. Move the behaviour comment (redirects, max_bytes, digest) onto cloudHttpDownloadMem/cloudHttpDownloadGoogleFd, and change the assertions in tests/test_self_update.py:13-14 that only check the name is present.
- Benefit: ~13 lines removed. No ELF change: MEASURED, evidence/Launchstation/raw/link-raw/wLaunchELF.gc-sections.log:60 shows the 80-byte section already discarded.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-288: elf.c small leftovers: MAX_PATH redefinition, unreachable else branches after a repeated test, stray braces, stale (void)exec_kind

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/elf.c:11`; `Launchstation/src/elf.c:763-771`; `Launchstation/src/elf.c:775-779`; `Launchstation/src/elf.c:797-801`; `Launchstation/src/elf.c:819`
- Problem: elf.c:11 #defines MAX_PATH 1025 after including launchelf.h, which already has the enum MAX_PATH = 1025 (launchelf.h:204). The outer if at 762 already requires !strncmp(filename, "pfs0:", 5), so the same test at 775 is always true and 777-779 is unreachable; likewise 787 requires the dvr_pfs_name prefix, making 799-801 unreachable. The extra block braces at 763/771 serve nothing, and (void)exec_kind at 819 is stale since exec_kind is used at 749.
- Fix: Delete the #define, keep only the true branches of the repeated tests, drop the extra braces and the (void) cast.
- Benefit: ~13 source lines; no ELF change expected (INFERRED: the compiler already folds the repeated tests).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-289: ps1SaveFromFile raw branch re-implements takeBlocks

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_ps1_card.c:342-360`; `Launchstation/src/mc_ps1_card.c:296-311`
- Problem: The PS1F_RAW branch builds the name and then repeats takeBlocks inline: the size checks, ps1NameOk, malloc, memcpy, blocks and strcpy. The other three formats call takeBlocks.
- Fix: After building name, set r to PS1_ERR_NAME when the stem is longer than 20, else r = takeBlocks(buf, len, name, out).
- Benefit: ~13 lines removed. Only change: when both the size and the name are bad the result becomes PS1_ERR_INVALID instead of PS1_ERR_NAME.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-290: Hex Editor hexDigit duplicates dash_themes.c hexNibble and hexParse re-implements strtol

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_editor.c:1221-1242`; `Launchstation/src/dash_themes.c:646-655`
- Problem: hexDigit (dash_editor.c:1221-1227) and hexNibble (dash_themes.c:646-655) are the same hex-digit decoder in two static copies; hexParse (1230-1242) hand-parses an optional 0x prefix and up to 7 hex digits, which strtol(s, &end, 16) already does (strtol is linked: dash_settings.c, ember_gameid.c).
- Fix: Replace hexParse with strtol plus an end-pointer/empty check; keep one hex-digit helper (export one copy, or keep the editor's 7-line version and use it in dash_themes.c).
- Benefit: ~12 lines (INFERRED: hexParse 13 -> ~4, one 7-10 line decoder dropped, +1 prototype)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-291: Public wrappers that only forward to statics: mcViewReadChain, mcViewWalkDir, mcCardSameSave

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/mc_card_view.c:529-537`; `Launchstation/src/mc_card_write.c:820-823`
- Problem: mcViewReadChain and mcViewWalkDir only call the static chainRead and walkDir with the same arguments; mcCardSameSave only adds a NULL check in front of the static sameSave (mc_card_write.c:481). Each pair is two names for one function.
- Fix: Give the static functions the public names (drop static) and delete the wrappers; fold the NULL check of mcCardSameSave into sameSave.
- Benefit: ~12 lines removed; no ELF change (INFERRED: the forwarders are tail calls or inlined).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-292: Small leftovers: duplicate prototype, unused include, unreachable SAME_PATH branch, redundant copies and branches

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_browser.c:28`; `Launchstation/src/dash_browser.c:31`; `Launchstation/src/dash_browser.c:137-139`; `Launchstation/src/dash_browser.c:1139`; `Launchstation/src/dash_browser.c:1727-1729`; `Launchstation/src/dash_browser.c:2283-2284`; `Launchstation/src/dash_browser.c:2678-2681`
- Problem: IsTextEditorFileType is re-declared at 31 although launchelf.h:404 (via dash_internal.h) declares it; gui_sort.h (cmpFile, sort) is included but nothing from it is used; nextImageIndex wraps by hand instead of using wrapIndex; opRename copies the name into want_sel although openDir(fb.dir, name) overwrites it; the MC_CARD_EXPORT_SAME_PATH branch is unreachable because 1697 already rejected the same strcmp-equal paths; recoverCardFromPath copies the non-const stage[] into clean[] before genRemove; both bookmark branches at 2678-2681 append '/' (one test on '/' suffices).
- Fix: Delete line 31, the gui_sort.h include, line 1139, the SAME_PATH branch, the clean[] copy (pass stage) and the second bookmark branch; use wrapIndex in nextImageIndex.
- Benefit: ~12 lines removed, no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-293: Unused cosdgf/sindgf/WLE_DEG_TO_RADF compatibility macros in launchelf.h

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/launchelf.h:95-106`
- Problem: The floatlib compatibility macros cosdgf, sindgf and WLE_DEG_TO_RADF are referenced only by each other inside this block.
- Fix: Delete lines 95-106.
- Benefit: 12 lines removed; no ELF change (macros).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-294: Write-only or constant state in filer.c: mcfreeSpace, host_ready, host_use_Bsl, root_mc_poll_format, readHOST Win_path stores

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer.c:113`; `Launchstation/include/filer_shared.h:54`; `Launchstation/src/filer.c:197`; `Launchstation/src/filer.c:1708`; `Launchstation/src/filer.c:1736`; `Launchstation/include/filer_internal.h:13-14`; `Launchstation/src/filer.c:200`; `Launchstation/src/filer.c:1660-1661`; `Launchstation/src/filer.c:269`; `Launchstation/src/filer.c:1570`; `Launchstation/src/filer.c:1811`; `Launchstation/src/filer.c:1818`
- Problem: mcfreeSpace is defined and declared extern but never used. host_ready is only ever assigned (1708, 1736), never read. host_use_Bsl is initialised to 1 and never written, so the branch at 1660-1661 is unreachable. root_mc_poll_format only receives mcGetInfo's format output and is never read. In readHOST, host_path is copied from Win_path (1811) only to build Win_path again per entry (1818), and that Win_path is never read.
- Fix: Delete mcfreeSpace and its extern; delete host_ready, its two writes and extern; delete host_use_Bsl, its extern and the always-false early return; pass NULL as the format argument at 1570 and delete root_mc_poll_format; delete the two dead stores in readHOST.
- Benefit: ~12 source lines; mcfreeSpace is 4 bytes of .bss still in the ELF (MEASURED: BUILD-EVIDENCE.md section 5, objects lack -fdata-sections); the rest INFERRED a few dozen bytes of .text/.bss.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-295: config_private.h: the CUSTOM_COLORS #ifdef selects two identical colour sets

- Category: 3 (Overengineering)
- Location(s): `Launchstation/include/config_private.h:10-28`; `Launchstation/ps2.yaml:630`; `Launchstation/ps2.yaml:9`
- Problem: Both branches define DEF_COLOR1..DEF_COLOR8 with exactly the same GS_SETREG_RGBA values (lines 11-18 and 20-27). CUSTOM_COLORS is tested nowhere else in the repo, so the define in ps2.yaml:630 has no effect.
- Fix: Keep one set of DEF_COLOR lines, delete the #ifdef/#else/#endif and the other copy, and remove CUSTOM_COLORS from ps2.yaml:630 and the comment on line 9.
- Benefit: ~12 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-296: dashModalMenu resets the one-shot callbacks three times and duplicates the Up/Down skip loop

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_ui.c:945-963`; `Launchstation/src/dash_ui.c:972-989`
- Problem: modal_preview, modal_marks and modal_edit are already copied into locals at 931-934, yet the same three NULL assignments are written out at 946-948, 954-956 and 961-963. The Up and Down handlers are the same do/while skip-disabled loop with the direction flipped.
- Fix: Clear the three globals once right after copying them (before the early returns) and drop the other two copies; fold Up/Down into one step with d = -1/+1.
- Benefit: ~12 source lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-297: dash_files.c storage* wrappers branch on files_mode five times instead of choosing the screen once

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_files.c:682-727`
- Problem: storageEnter, storageInput, storageUpdate, storageDraw and storageHints each repeat 'if (dashCfg.files_mode) dashDevicesGridScreen.X(); else dashFilesListScreen.X();'.
- Fix: Add one static function returning files_mode ? &dashDevicesGridScreen : &dashFilesListScreen and make each wrapper a one-line call through it.
- Justification: One 4-line selector replaces five duplicated if/else pairs; net reduction of about 12 lines.
- Benefit: ~12 lines (INFERRED from range)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-298: main_modules.c: unused boot_device parameter, dead WaitTime stores, one-line font wrapper, stale comments

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/main_modules.c:18-27`; `Launchstation/src/main_modules.c:35`; `Launchstation/src/main_modules.c:47`; `Launchstation/src/main_modules.c:58`; `Launchstation/src/main_modules.c:64`; `Launchstation/include/main_modules.h:6`; `Launchstation/src/main_startup.c:714-720`; `Launchstation/include/main_startup.h:26`
- Problem: initializeRuntimeInputModules ignores boot_device ((void)boot_device at 25). It sets WaitTime at 27 and 58, but every reader (editor_file.c:58,181,207,249,318) sets WaitTime itself right before reading it. loadConfiguredBootFont (main_startup.c:714) only calls loadFont(setting->font_file) and has one caller (main_modules.c:64). The comments at 35 ('early setupPad above') and 47 ('main menu') describe code that is gone.
- Fix: Drop the parameter (and the argument at main.c:279), delete both WaitTime stores, call loadFont(setting->font_file) directly and delete loadConfiguredBootFont and its prototype, and delete the two stale comments.
- Benefit: ~12 lines removed; trivial code size (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-299: mmce_backup.c: unused NULL-options defaults and a forwarding wrapper

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/mmce_backup.c:791-796`; `Launchstation/src/mmce_backup.c:839-846`; `Launchstation/src/mmce_backup.c:968-973`; `Launchstation/src/mmce_backup.c:280-283`
- Problem: All five callers of mmceBackupAllCards, mmceBackupCardPath and mmceBackupScanCards pass &options, so the three static defaults structs and 'if (options == NULL) options = &defaults' are never used. mmceCardFingerprint only forwards to the static card_fingerprint.
- Fix: Delete the three defaults structs and NULL fallbacks (treat NULL as invalid with the existing argument check); rename card_fingerprint to mmceCardFingerprint and delete the wrapper.
- Benefit: about 12 source lines; a few dozen bytes of .rodata/.text INFERRED
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-300: readMC and readXFROM end with the same entry-copy loop

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer.c:527-542`; `Launchstation/src/filer.c:583-598`
- Problem: After their GetDir call both readers run the same 16 lines: map a negative result to dirOpenResult, then copy mcDir entries into info skipping . and .. up to max. They already share the static mcDir table (488).
- Fix: Move the 16 lines into one static function taking ret, info and max, and return it from both readers.
- Justification: Removes one of two identical 16-line blocks; the helper is about 16 lines, so the net saving is about 12 lines and one place to fix the shared table logic.
- Benefit: ~12 source lines net; ELF INFERRED neutral to ~100 bytes.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-301: waitUntilTimer/hddHeaderDelay spin on Timer() with an empty loop body instead of sleeping

- Category: 5 (Performance)
- Location(s): `Launchstation/src/filer_browser.c:120-124`; `Launchstation/src/filer_browser.c:256-260`; `Launchstation/src/hdd_header_injector.c:91-98`
- Problem: waitForClipboardSourceDevice (filer_browser.c:240-264) runs after an IOP reset since the copy. Between probes it busy-waits up to 1 s per probe, 6 s in total, in an empty while loop. It runs on the UI thread: dash_browser.c opPaste -> filerPasteModeInto -> subfunc_Paste:2179. The EE runs at 100% and no lower-priority thread can run. hddHeaderDelay is the same empty spin, used between source polls at hdd_header_injector.c:315 and :419.
- Fix: Delete both helpers. Between probes call DelayThread(SOURCE_DEVICE_WAIT_INTERVAL_MS * 1000), and DelayThread(HDD_HEADER_SOURCE_POLL_MS * 1000) in the injector. The loops already check the deadline, so the next_check clamp at 257-259 can go too.
- Benefit: About 12 lines removed (two 4-8 line helpers plus the clamp). The CPU is free during waits of up to 6 s. The UI looks the same, because the thread is blocked either way (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-302: dash_ps1_card.c repeats the read-and-parse-save sequence and the folder-of-path idiom

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_ps1_card.c:241-259`; `Launchstation/src/dash_ps1_card.c:417-432`; `Launchstation/src/dash_ps1_card.c:471-479`; `Launchstation/src/dash_ps1_card.c:279-280`; `Launchstation/src/dash_ps1_card.c:399-400`; `Launchstation/src/dash_ps1_card.c:439-440`; `Launchstation/src/dash_ps1_card.c:582-583`; `Launchstation/src/dash_ps1_card.c:423`; `Launchstation/src/dash_ps1_card.c:459`
- Problem: p1ReadSave and dashPs1SaveConvert both do p1ReadFile, the same two-string error choice, ps1SaveFromFile, free and an error modal, differing only in the title ('Not imported'/'Not converted') and the name hint; ConvertMany repeats the error strings a third time. 'copy path, cut at p1Base' is written four times and the on-card test twice.
- Fix: Give p1ReadSave a hint and a title argument and call it from dashPs1SaveConvert; keep the two error strings in one place; add a 2-line p1Folder(path, out) for the four copies.
- Benefit: ~12 lines removed net (INFERRED); one copy of two message strings in .rodata.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-303: main.c fills the same MainExecuteContext twice (Execute and the DashRun block)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/main.c:252-263`; `Launchstation/src/main.c:295-306`
- Problem: Execute() and main() each set the same ten fields from the same globals, line for line.
- Fix: Keep one file-scope MainExecuteContext filled once in main() after performEarlyBootInitialization (rough_region and ROMVER_data are set by then), and pass it both to ExecuteMainAction from Execute() and to DashRun.
- Benefit: ~12 source lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-304: Auto-start device id-to-bit mapping written twice (six choiceInfo cases and a setChoice ternary)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_settings.c:1130-1152`; `Launchstation/src/dash_settings.c:1738-1743`
- Problem: choiceInfo has seven cases that differ only in the DASH_DEVA_* constant (S_DEVA_UDPFS leaves out the boot-device test, which is harmless because dashBootDeviceBit never returns UDPFS, dash_devices.c:895-918). setChoice maps the same ids to bits again with a 6-way ternary.
- Fix: Add one small static id-to-bit function, use it in setChoice in place of the ternary, and collapse the choiceInfo cases into one case group: cur = (dev_auto & bit) || dashBootDeviceBit() == bit.
- Justification: The helper replaces two copies of the same mapping: 23 + 6 lines become about 8 + 6 + 1, a net removal of about 11 lines, and a device added later needs one edit instead of two.
- Benefit: ~11 lines removed net (INFERRED from cited ranges); negligible size change
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-305: Dead locals and a redundant prototype in dash_apps.c (drawGrid fx/fy/r/fr, drawTitleBar (void)a, local dashHomeRebuild prototype)

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_apps.c:12`; `Launchstation/src/dash_apps.c:513`; `Launchstation/src/dash_apps.c:519`; `Launchstation/src/dash_apps.c:541-544`; `Launchstation/src/dash_apps.c:553-560`
- Problem: In drawGrid, fx/fy are assigned only to be (void)-cast, and r/fr are computed only to be (void)-cast; drawTitleBar casts a to void although it is used at line 499. dashHomeRebuild is already declared in include/dash_internal.h:733, which dash_apps.c includes at line 10.
- Fix: Delete fx, fy, r, fr, their assignments and the three (void) casts in drawGrid, the (void)a in drawTitleBar and the local prototype at line 12.
- Benefit: ~11 lines removed; no ELF change (INFERRED: the compiler already drops unused locals).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-306: No-cloud #else branches in S_IGR_SYNC_MODE handling are never compiled

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_settings.c:1160-1168`; `Launchstation/src/dash_settings.c:1830-1840`
- Problem: Both #else paths are only for builds without ETH and WLE_NATIVE_CLOUD. ps2.yaml:624-625 always defines both for the wLaunchELF target, the DS34 variant is generated from the same ps2.yaml, and the host test that compiles this file defines them too (tests/test_settings_navigation.py:31). No build compiles the #else code.
- Fix: Delete the two #else branches and their #if/#endif lines, keeping the cloud code. A repo-wide decision to drop the WLE_NATIVE_CLOUD switch would remove the other guards in this file (17, 378, 775, 1172, 1846, 3342, 3857) as well.
- Benefit: ~11 source lines removed; no ELF change, because the code is not compiled now (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-307: No-op indirections: scan_USB_mass forwarder and makeHostPath UDPFS goto to the next line

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer.c:1402-1408`; `Launchstation/src/filer.c:1670-1675`
- Problem: scan_USB_mass only calls scanUsbMassDevices(0); its three callers are all in filer.c (1974, 1989, 2278) and no header declares it. In makeHostPath the UDPFS block jumps with goto to the label on the following line, which is reached anyway.
- Fix: Delete scan_USB_mass and call scanUsbMassDevices(0) at its three call sites. Delete the #ifdef UDPFS goto block and the copy_passthrough label.
- Benefit: ~11 source lines; no ELF change (scan_USB_mass is already inlined and its 28-byte copy discarded, MEASURED in wLaunchELF-gc-discarded-first-party.tsv).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-308: Redundant statements: second LK_Flag update in pathApplied, no-op keep_enabled/keep_view in editScanPaths, presetPreview forwarder

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_settings.c:2111-2114`; `Launchstation/src/dash_settings.c:2134-2137`; `Launchstation/src/dash_settings.c:2753-2760`; `Launchstation/src/dash_settings.c:2819-2822`; `Launchstation/src/dash_settings.c:2840`
- Problem: pathApplied sets LK_Flag for S_ESR_PATH/S_OSDSYS_PATH at 2111-2114, and 2134-2137 sets the same flags again with nothing in between that returns early for those ids. In editScanPaths 'Restore defaults', saved is a full copy of the old dashCfg with only scan/nscan replaced, so dashCfg = saved already keeps enabled and view; keep_enabled/keep_view do nothing. presetPreview only forwards to dashThemeSetPreset(int), which already has the void (*)(int) type that dashModalSetPreview takes (dash_internal.h:951, :260).
- Fix: Delete lines 2134-2137. Delete keep_enabled/keep_view and their two restore assignments. Delete presetPreview and pass dashThemeSetPreset to dashModalSetPreview.
- Benefit: ~11 lines removed; no behaviour change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-309: clear_psu_header/pad_psu_header are exported one-line memset wrappers used only inside psu_functions.c

- Category: 3 (Overengineering)
- Location(s): `Launchstation/include/psu_functions.h:7-8`; `Launchstation/src/psu_functions.c:7-15`
- Problem: Both functions only memset a psu_header; their only callers are in psu_functions.c (:84,96,238,267,289). The header exports them and tests/test_copy_psu_listing.py:276 and tests/test_listing_callers.py:239 rewrite them to static to paste the file into a host TU.
- Fix: Delete both functions and their prototypes and use memset(&psu_head, 0 or 0xFF, sizeof psu_head) at the five call sites; drop the matching replace() calls in the two tests.
- Benefit: ~11 lines removed (9 in .c, 2 in .h) plus 2 test lines; no ELF change, gc-sections/inlining already handle them (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-310: dash_image.c / dash_jpeg.c: defaults and NULL checks for states the single caller never produces

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_image.c:90`; `Launchstation/src/dash_image.c:96-104`; `Launchstation/src/dash_jpeg.c:171-173`
- Problem: dashShowImageViewer has one caller (dash_browser.c:752), which passes a fullPath()-built non-empty path and &iv with slide_seconds = 5 (dash_browser.c:744-745); the viewer itself keeps slide_seconds in 1..60. So the empty-path return, the local_state fallback and the slide_seconds<1 reset are dead. dashJpegLoadFile re-checks out and clears *out, which dashJpegDecode does again at 56-58.
- Fix: Delete the NULL/empty path check, local_state and the state NULL/slide_seconds fallback in dashShowImageViewer, and the out check in dashJpegLoadFile.
- Benefit: ~11 lines removed (INFERRED); a few dozen bytes of code.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-311: init.c clutter: prototypes repeated from init.h and commented-out code

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/init.c:365-369`; `Launchstation/src/init.c:392`; `Launchstation/src/init.c:1255`; `Launchstation/src/init.c:1258`; `Launchstation/src/init.c:1369-1370`; `Launchstation/src/init.c:3095`
- Problem: loadUsbModules, ensureCoreIoStackReady, setupPowerOff, startKbd, Reset and closeAllAndPoweroff are declared again at 365-369 and 392 although init.h (included at line 2) declares them at init.h:10,19,27,29,30,31. Lines 1255, 1258, 1369-1370 and 3095 are commented-out code (rom0 MCMAN/MCSERV loads, ESR case labels, setupPad).
- Fix: Delete the six repeated prototypes and the five commented-out lines.
- Benefit: ~11 lines removed; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-312: Dead non-PS2BUILD #else branches for embedded ELF symbols in mmce_igr.c and elf.c

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/mmce_igr.c:16-24`; `Launchstation/src/elf.c:15-24`
- Problem: WLE_PS2BUILD is defined by the only build (ps2.yaml:615, kept by scripts/configure_ds34.py:36 for DS34); the legacy Makefile that used the plain wle_igr_elf/loader_elf names is retired. Neither file is compiled by a host test, so the #else branches can never be built.
- Fix: Delete the #ifdef/#else/#endif and keep only the *_irx extern declarations (and in elf.c the two #define aliases, or rename the uses).
- Benefit: about 10 source lines; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-313: GUI colour CNF parser hextou32 duplicates strtoul(value, NULL, 16)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/gui_colors.c:4-13`
- Problem: hextou32 is a hand-written upper-case-only hex parser used by the eight GUI_Col_N_ABGR keys; strtoul is already linked (main_startup.c:291, cloud_http.c:538).
- Fix: Delete hextou32 and use (u32)strtoul(value, NULL, 16); this also accepts lower-case hex. Adjust tests/test_classic_settings_persistence.py:55 which extracts the function by name.
- Benefit: ~10 lines removed; tens of bytes of .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-314: Loader '-la=' flags N and D and DEV9_CLEANUP_HDD are never produced

- Category: 3 (Overengineering)
- Location(s): `Launchstation/loader/loader.c:28-32`; `Launchstation/loader/loader.c:264-265`; `Launchstation/loader/loader.c:396`; `Launchstation/loader/loader.c:415-420`
- Problem: The only producers of the loader flag string are src/elf.c:577 ('-la=E' or '-la=ER') and src/elf.c:823 ('-la=A' plus optional R and H). N (HDD-only cleanup) and D (explicit none) are never emitted, so cleanup is always either NONE or ALL and the three-value enum plus the DEV9_CLEANUP_ALL test only serve unused modes.
- Fix: Delete the N and D cases and the DEV9_CLEANUP_HDD value; make the cleanup a plain flag set by H that always powers off DEV9 after the HDD idle.
- Benefit: ~10 lines removed and a few dozen bytes from the loader ELF that must fit 0x84000-0xa0000 (INFERRED, not measured).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-315: Non-ETH stub of dashClockSyncInternet is compiled by no build

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_clock_sync.c:10`; `Launchstation/src/dash_clock_sync.c:363-372`
- Problem: ETH is always defined for the only EE target (ps2.yaml:624) and the DS34 variant does not remove it, so the #else stub is unreachable.
- Fix: Delete the #ifdef ETH / #else stub / #endif, or leave it only if the project decides to keep ETH optional across its ~30 guarded files. tests/test_clock_sync.py cuts the source at '\n#else' and must get a new end marker.
- Benefit: ~10 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-316: PS1 save validity check written three times (validSave/ps1CardWrite, ps1SaveToMcs, ps1Check)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/mc_ps1_card.c:231-242`; `Launchstation/src/mc_ps1_card.c:378-381`; `Launchstation/src/mc_save_export.c:115-122`
- Problem: The same test (s, s->data, 1 <= blocks <= 15, then ps1NameOk giving PS1_ERR_NAME) appears in validSave plus the re-evaluation at ps1CardWrite:242, in ps1SaveToMcs, and as ps1Check in mc_save_export.c. ps1CardWrite even repeats the validSave expression to choose between INVALID and NAME.
- Fix: Keep one function in mc_ps1_card.c that returns PS1_ERR_INVALID or PS1_ERR_NAME (ps1Check's shape, without the out/len clearing, which callers can do), declare it in mc_ps1_card.h, and use it in ps1CardWrite, ps1SaveToMcs, ps1SaveToPsv and ps1SaveToPsx; delete validSave and ps1Check.
- Benefit: ~10 lines removed; one rule for what a valid PS1 save is.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-317: Repeated device-prefix tests and option table: mc/vmc test x10, USB/HDD test x4, mc0/mc1 test x3, slots[] x2

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_browser.c:2794`; `Launchstation/src/dash_browser.c:2816-2824`; `Launchstation/src/dash_browser.c:2864-2865`; `Launchstation/src/dash_browser.c:2870`; `Launchstation/src/dash_browser.c:2879`; `Launchstation/src/dash_browser.c:1817-1819`; `Launchstation/src/dash_browser.c:1912-1913`; `Launchstation/src/dash_browser.c:2067-2068`; `Launchstation/src/dash_browser.c:2332-2333`; `Launchstation/src/dash_browser.c:299`; `Launchstation/src/dash_browser.c:364`; `Launchstation/src/dash_browser.c:3437`; `Launchstation/src/dash_browser.c:1894-1896`; `Launchstation/src/dash_browser.c:2045-2047`
- Problem: actionsMenu evaluates strncmp(fb.dir,"mc",2) && strncmp(fb.dir,"vmc",3) eight times on the same fb.dir, including two consecutive identical ifs at 2821 and 2823. The mass/usb/ata/hdd prefix test is written out four times, the mc0:/mc1: test three times, and the two-entry MMCE slots[] table twice.
- Fix: In actionsMenu compute the off-card flag once and merge the ifs at 2821/2823; add one static isExternalStore(path) for the four USB/HDD tests; move slots[] to file scope.
- Justification: isExternalStore replaces four 2-3 line conditions with calls; the flag and file-scope table remove repeated code without adding behaviour.
- Benefit: ~10 lines removed and simpler conditions (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-318: Single-caller forwarding wrappers ctSafetyCopy and ctWriteFile

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_card_tool.c:338-341`; `Launchstation/src/dash_card_tool.c:585-588`
- Problem: ctSafetyCopy only calls ctSafetyCopyOf(image, NULL, 0, ...) and has one caller (384); ctWriteFile only calls ctWriteFileAs(..., 0) and has two callers (642, 2599).
- Fix: Delete both wrappers and call ctSafetyCopyOf/ctWriteFileAs directly at the three call sites.
- Benefit: ~10 lines removed (two 4-line functions and their blank lines; INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-319: Small copy-paste in dash_devices.c: dashDevicesInit repeats ResetBaseline; PC CARD CF row written as 8 add() calls

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_devices.c:117-138`; `Launchstation/src/dash_devices.c:408-438`
- Problem: dashDevicesInit (dash_main.c:1283) repeats the three baseline assignments of dashDevicesResetBaseline (dash_main.c:362). The PC CARD CF block calls add() eight times with the same icon, name, 'cf0:' and 'cf0:/', differing only in state and status text.
- Fix: Have dashDevicesInit call dashDevicesResetBaseline. In the CF block pick state and status in the if/else chain and make one add() call afterwards (then fill the capacity detail when READY).
- Benefit: ~10 lines (INFERRED: 3 in Init, ~7 in the CF block)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-320: Small leftovers in the background code: once-only guard, one-line wrapper, oversized OrbMat, per-cloud blend toggles

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_bg.c:51`; `Launchstation/src/dash_bg.c:103-104`; `Launchstation/src/dash_bg.c:126`; `Launchstation/src/dash_bg.c:135-138`; `Launchstation/src/dash_bg_ps2.c:517-520`; `Launchstation/src/dash_bg_ps2.c:816-818`
- Problem: dashBgInit is called once from firstInit (src/dash_main.c:1282), so bg_ready is never seen set; bgAnimated is a one-line wrapper with one caller (line 143); OrbMat has m[9] but only indices 0,2,3,5,6,8 are ever written or read, in a static array of 33; dashBgSceneClouds switches the blend mode to ADD and back for every cloud instead of once around the loop.
- Fix: Delete bg_ready and its checks; inline dashCfg.effects > 0 at line 143 and delete bgAnimated; shrink OrbMat to six floats (renumber the six uses); move the two dashGfxBlend calls outside the cloud loop.
- Benefit: ~10 lines removed; 396 bytes .bss (INFERRED: 33 mats x 3 floats x 4 bytes); 16 fewer blend-state changes per frame in Clouds FULL.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-321: Symbols used only in their own file are exported through these headers

- Category: 3 (Overengineering)
- Location(s): `Launchstation/include/launchelf.h:448`; `Launchstation/include/launchelf.h:477`; `Launchstation/include/launchelf.h:520`; `Launchstation/include/launchelf.h:550`; `Launchstation/include/launchelf.h:553`; `Launchstation/include/launchelf.h:578`; `Launchstation/include/editor_private.h:101`; `Launchstation/include/filer_shared.h:105`; `Launchstation/include/filer_internal.h:13-14`
- Problem: LaunchArgsCopyToArgv (elf.c), Menu_title_y (draw.c), joy_value (pad.c / pad_ds34.c), initConfig and saveConfigToPath (config.c), PathPad_menu (filer_browser.c), getEditorSpecialKeyboardNextKey (editor_input.c), readSMB, host_ready and host_use_Bsl (filer.c) are referenced only inside the file that defines them, yet are declared extern in shared headers.
- Fix: Drop the header declarations and make the definitions static (adding a forward declaration only where the use precedes the definition).
- Benefit: ~10 header lines removed; static lets the compiler inline or drop them. Size effect not measured (INFERRED small).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-322: WLE_PS2BUILD is always defined: the #else branches for the retired Makefile build are dead

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/elf.c:15-24`; `Launchstation/src/mmce_igr.c:16-24`; `Launchstation/ps2.yaml:615`; `Launchstation/scripts/configure_ds34.py:36`
- Problem: ps2.yaml is the only build (legacy Makefile retired) and defines WLE_PS2BUILD (ps2.yaml:615); the DS34 variant keeps it (configure_ds34.py:36). The two #else branches declaring bin2s-style loader_elf/wle_igr_elf symbols can no longer be compiled.
- Fix: Keep the PS2BUILD declarations, delete the #ifdef/#else/#endif and the else branches; then the WLE_PS2BUILD define in ps2.yaml and configure_ds34.py can go too (configure_ds34 would anchor its DS34 insertion on another line).
- Benefit: ~10 source lines; no ELF change (INFERRED: preprocessor only).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-323: dash_home.c: row_anim, col_anim[1] and cam[1] are animated or set but never read

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_home.c:43-45`; `Launchstation/src/dash_home.c:176`; `Launchstation/src/dash_home.c:354-367`
- Problem: row_anim is written at 354 and never read; col_anim[1] is written at 176 and 357 but only col_anim[0] is read (152, 458); cam[1] is never used. homeUpdate loops r over 0..1 with a slot of 0 for row 1 only to update these unused values.
- Fix: Delete row_anim and line 354, turn col_anim and cam into scalars, delete line 176, and replace the loop in homeUpdate with the row-0 body. Update tests/test_home_focus.py:65, which declares col_anim[2], cam[2].
- Benefit: ~10 lines removed and one approach call per frame saved (INFERRED); 12 B .bss (row_anim, col_anim[1], cam[1]).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-324: opExtractPsuTo repeats opCopy's clipboard fill and the 'clipboard was cleared' toast three times

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_browser.c:833-842`; `Launchstation/src/dash_browser.c:992-995`; `Launchstation/src/dash_browser.c:1007-1012`; `Launchstation/src/dash_browser.c:1029-1047`
- Problem: Both functions reserve the clipboard with the same error modal and then set clipPath, clipFiles, nclipFiles, browser_cut and clipIopResetGeneration with identical lines; opExtractPsuTo then emits the same clipboard_lost toast on three exit paths.
- Fix: Move the reserve-and-fill into one static helper used by both, and restructure the end of opExtractPsuTo so the clipboard_lost toast is emitted once.
- Justification: The helper replaces two copies of the same ~9 lines and removes code overall.
- Benefit: ~10 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-325: Boot launch-key / auto-launch countdown logic written twice in bootBegin and bootUpdate

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_main.c:1075-1088`; `Launchstation/src/dash_main.c:1106-1118`
- Problem: The 'held launch key runs, else start the AUTO countdown' block appears in bootBegin's Init_Delay==0 branch and again in bootUpdate when the delay expires.
- Fix: In bootBegin always enter boot_phase 1 with boot_phase_end = Timer() + Init_Delay*1000 and drop the else branch; bootUpdate then handles both cases on the next frame.
- Benefit: ~10 lines removed (INFERRED); with Init_Delay 0 the held key is read one frame later.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### LAUNCHSTATION-326: isHddRootPath/isHddBrowserPath defined separately in four files

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_browser.c:18-26`; `Launchstation/src/filer_copy.c:349-352`; `Launchstation/src/filer.c:674-677`; `Launchstation/src/elf.c:362-365`
- Problem: filer_browser.c and filer_copy.c both define a static isHddRootPath with the same 'hddN:/' test (filer_copy.c inlines it in one expression); filer.c defines another through getHddDeviceFromPath; filer_browser.c and elf.c each define isHddBrowserPath.
- Fix: Keep one exported isHddBrowserPath/isHddRootPath pair (e.g. in filer_shared.h next to the other path helpers) and delete the static copies.
- Benefit: ~10 source lines removed; no meaningful binary change (INFERRED, static copies are tiny)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-327: safeAutoStartMask re-checks MMCE/MX4SIO exclusivity already enforced on load and on toggle

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_devices.c:685-694`; `Launchstation/src/dash_store.c:443-454`; `Launchstation/src/dash_settings.c:1746-1758`
- Problem: dashCfg.dev_auto is only written in dash_store.c (default 0, parse at :358 followed by the same MMCE/MX4SIO normalisation at :448-454) and in dash_settings.c:1755-1764, which clears the other bit or refuses the toggle; whole-struct restores (dash_settings.c:2573, 2758) copy already-normalised snapshots. The 'defense in depth' copy in safeAutoStartMask can never see both bits set.
- Fix: Delete lines 685-694 of dash_devices.c and keep only the iLink masking in safeAutoStartMask.
- Benefit: 10 lines (INFERRED from range)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-328: settingsDraw repeats the value-column drawing block for choice rows and number/path rows

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_settings.c:3999-4014`; `Launchstation/src/dash_settings.c:4038-4053`; `Launchstation/src/dash_settings.c:3829`
- Problem: Both blocks compute maxw/vw/vx the same way, draw the same left/right arrow icons when focused, and then choose dashTextScroll or dashTextFit. Only the string and colour differ. The choice block also evaluates the same 'S_LANGUAGE or S_T_PRESET keeps the raw name, else localize' expression three times (4001, 4011, 4013), and settingsInput repeats it at 3829.
- Fix: Compute the display string once into a local in the choice branch. Move the shared width/arrow/scroll-or-fit code into one static draw function taking the text, colour and an arrows flag, and call it from both branches.
- Justification: Two 14-line copies become one ~14-line function plus two short calls, a net removal of about 10 lines, and the two kinds of row keep identical layout.
- Benefit: ~10 lines removed net (INFERRED from cited ranges); negligible size change
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-329: Exploit install repeats file and folder probes: custom ELF and PS2KEYS.dat checked again before signing, mc_folder_exists twice per folder

- Category: 5 (Performance)
- Location(s): `Launchstation/src/main_exploit_installer.c:1077-1082`; `Launchstation/src/kelf_signer.c:1014-1018`; `Launchstation/src/main_exploit_installer.c:1159-1164`; `Launchstation/src/main_exploit_installer.c:1664-1666`
- Problem: reload_custom_payload_source parses PS2KEYS.dat again (KelfSignValidateKeys) and opens the ELF again to measure it (KelfSignGetOutputSize, 1077-1082), right before prepare_custom_signed_payloads calls KelfSignElfToMemory, which parses the keys and reads the whole ELF anyway (kelf_signer.c:1014-1018) and reports its own failure. prepare_needed_folders calls mc_folder_exists (1664) and then ensure_mc_folder, which calls mc_folder_exists on the same folder again (1163-1164).
- Fix: Delete lines 1077-1082 and the signed_size variable in reload_custom_payload_source; keep the two ensurePathDeviceStackReady calls. Delete the check at 1163-1164 in ensure_mc_folder and call ensure_mc_folder only when folder_assets_to_write[i] was set (folder missing); mcMkDir's -4 result is already accepted.
- Benefit: One keys-file parse and one ELF open/seek/tail read less per custom install; one memory-card directory open less per needed folder (up to 4). INFERRED, tens of ms. About 9 lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-330: PS2TIME in filer_shared.h duplicates ps2time in psu_types.h

- Category: 2 (Duplication)
- Location(s): `Launchstation/include/filer_shared.h:6-15`; `Launchstation/include/psu_types.h:6-15`
- Problem: Both headers declare the same 8-byte memory-card timestamp (u8 unused/sec/min/hour/day/month, u16 year) under two names; PS2TIME is used in filer_actions.c and filer_browser.c, ps2time in the PSU headers.
- Fix: Keep one typedef (psu_types.h's ps2time), include psu_types.h from filer_shared.h and rename the PS2TIME uses, or make PS2TIME a one-line typedef of ps2time.
- Benefit: ~9 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-331: hddHeaderTextEditor global function pointer and editorForHeader wrapper for a single caller

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/hdd_header_injector.c:825-826`; `Launchstation/src/hdd_header_injector.c:858`; `Launchstation/include/hdd_header_injector.h:16`; `Launchstation/src/dash_hdd.c:951-967`
- Problem: The only caller of EditHddPartitionHeaderSystemCnf (dash_hdd.c:966) sets hddHeaderTextEditor to editorForHeader, which only forwards to dashTextEditor, and resets it to TextEditor afterwards; the TextEditor default is never used.
- Fix: Call dashTextEditor directly from EditHddPartitionHeaderSystemCnf (or pass the editor as a parameter); delete the global, its extern, editorForHeader and the two assignments.
- Benefit: ~9 lines removed; one global and one indirect call gone.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-332: p1Base duplicates ctBaseName from dash_card_tool.c; lkLabel repeats the same basename code

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_ps1_card.c:38-45`; `Launchstation/src/dash_card_tool.c:185-192`; `Launchstation/src/dash_main.c:1029-1032`
- Problem: p1Base and ctBaseName are byte-for-byte the same 8-line function (basename after last '/' or ':'); lkLabel open-codes it again. dash_ps1_card.c already includes dash_card_tool.h.
- Fix: Make ctBaseName non-static with a prototype in dash_card_tool.h (or a dash_internal.h helper), delete p1Base and use it in dash_ps1_card.c and lkLabel.
- Benefit: ~9 lines removed net (8 + 3 removed, 1 prototype and include added), one copy of a tiny function (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-333: prepareExploitSignerIop repeats its post-reboot re-init sequence in both branches

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/init.c:847-876`
- Problem: The failure branch (849-859) and the success path (864-874) run the same seven steps: SifInitRpc, SifLoadFileInit, initsbv_patches, clearIopModuleState, iop_reset_generation++, ensureCoreIoStackReady, DS34 load, setupPad. The success path only adds SifIopSync and clearing the PCCF ownership first.
- Fix: Do the SifIopSync and PCCF clear only when ret > 0, then run the shared re-init once, then return 0 if ret <= 0, else load SECRSIF as now.
- Benefit: ~9 lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Med. Confidence: High (READ)

#### LAUNCHSTATION-334: FILEOP_TRACE definition block in filer.c is unused there

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer.c:156-163`; `Launchstation/src/filer_copy.c:22-29`; `Launchstation/src/filer_mount.c:6-13`; `Launchstation/src/filer_fileops.c:5-12`
- Problem: filer.c defines FILEOP_TRACE from ULE_DEBUG_BUILD but never tests it (no #if FILEOP_TRACE in filer.c). The same 8-line block is pasted in filer_copy.c, filer_mount.c and filer_fileops.c, and no build defines ULE_DEBUG_BUILD, so FILEOP_TRACE is 0 in every build.
- Fix: Delete the block in filer.c. Optionally keep a single copy of the block in a header the other three files already share instead of three pasted copies.
- Benefit: 8 lines removed from filer.c (up to 24 more if the other copies are merged); no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-335: dashPickPath: pickParent ignores its size argument and the 'go to parent' block is written twice

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_widgets.c:1066-1085`; `Launchstation/src/dash_widgets.c:1164-1170`; `Launchstation/src/dash_widgets.c:1218-1223`
- Problem: pickParent takes int size only to cast it to void (line 1084). Both callers (Back at 1164-1170, the Up row at 1218-1223) do the same four steps: copy pk.dir to a local buffer, pickParent, pickOpen, sel = 0.
- Fix: Drop the size parameter and fold the copy + pickOpen into pickParent (pickUp), so both call sites become one call plus sel = 0.
- Benefit: ~8 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-336: DI_RECENT icon is rasterised into the atlas but never drawn

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_icons.c:726-732`; `Launchstation/include/dash_internal.h:486`
- Problem: DI_RECENT appears only in its enum entry and its iconSdf case; no code passes it (directly or by DI_x + n arithmetic) to dashIcon, dashIconMany or any menu/hint table. It was added with three other icons in commit 26c06e6; the others are used.
- Fix: Delete the DI_RECENT enum entry and its iconSdf case.
- Benefit: ~8 lines removed; one 64x64 SDF tile no longer rasterised at boot and a few hundred bytes of iconSdf code (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-337: McView fields cardtype, cardflags and version are written but never read

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/mc_card_view.h:41-43`; `Launchstation/src/mc_card_view.c:229-230`; `Launchstation/src/mc_card_view.c:246-247`
- Problem: mcViewOpen copies the superblock version string and the cardtype/cardflags bytes into McView, but no EE source or host test reads them. has_ecc (mc_card_view.h:40) is read only by the host test printout tests/mc_card_view_host.c:37.
- Fix: Delete the three fields and the four assignments.
- Benefit: ~7 lines removed and 15 bytes per McView (INFERRED: char[13] + 2 x u8).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-338: McViewSave.title_break is written in two places and never read; icon.sys header parse duplicated

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_card_tool.c:2317-2325`; `Launchstation/src/mc_card_view.c:305-316`; `Launchstation/include/mc_card_view.h:56`
- Problem: title_break is set by ctPsuInfo (2320, 2323-2324) and by mc_card_view.c (311, 314-315); no production code reads it, only the host test print at tests/mc_card_view_host.c:44. ctPsuInfo's icon.sys check and title copy repeat mc_card_view.c's.
- Fix: Delete the title_break field and the lines that set it (and its print in the host test); optionally let ctPsuInfo call one shared icon.sys header parser from mc_card_view.c.
- Benefit: ~7 lines removed, 2 bytes per McViewSave (ct_saves holds 250, plus the 1000-entry All saves list; INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-339: Needless indirections: install_for_plan forwarding wrapper and main_history evictedEntry temp copy

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/main_exploit_installer.c:2277-2280`; `Launchstation/src/main_exploit_installer.c:2491`; `Launchstation/src/main_history.c:142-146`
- Problem: install_for_plan only unpacks plan into install_selected_ps2_updates and has one caller (2491). processHistoryList copies the evicted entry into a local only to pass it as a const pointer to appendEvictedHistoryEntry; the original is not modified until the memset after the call.
- Fix: Delete install_for_plan and call install_selected_ps2_updates directly; pass newEntry to appendEvictedHistoryEntry and delete evictedEntry and the memcpy.
- Benefit: ~7 lines removed; 24-byte stack copy avoided (INFERRED: sizeof(OSDHistoryEntry) = 16+4+2+pad)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-340: Unreachable and duplicate branches in dash_font.c glyph handling

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_font.c:316`; `Launchstation/src/dash_font.c:492-495`; `Launchstation/src/dash_font.c:497-498`; `Launchstation/src/dash_font.c:622-623`
- Problem: glyphAdv returns the same value for c >= ELISA_CODE and c >= 0x100 in two separate branches. Its c < FONT_FIRST remap and the same remap in drawRun can never fire: both get codes only from nextGlyphs, which already maps c < 0x20 to ' ' (line 361) and otherwise yields '?', 0x100+ pad codes or ELISA codes. The forward declaration of facesInit at line 316 is unused because facesInit is defined at 398 before its first use at 562.
- Fix: Merge the two glyphAdv branches into one 'c >= 0x100' test, delete the two c < FONT_FIRST remaps and the facesInit forward declaration.
- Benefit: ~7 lines removed; negligible ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-341: colA copied in dash_bg_geo.c and dash_bg_ps2.c; TAU_F defined but unused in dash_bg_geo.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_bg_geo.c:10-16`; `Launchstation/src/dash_bg_ps2.c:48-54`
- Problem: The 5-line static colA (set alpha with clamp) is byte-identical in both files, and both define TAU_F; dash_bg_geo.c never uses TAU_F (lines 1-333 read).
- Fix: Delete TAU_F from dash_bg_geo.c. Move colA once into dash_internal.h as a static inline next to dashColorA (line 63) and delete both file copies.
- Benefit: ~7 lines removed; no ELF change (INFERRED: static helper inlined either way).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-342: ctBaseName is a copy of dash_ps1_card.c p1Base

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_card_tool.c:185-192`; `Launchstation/src/dash_ps1_card.c:38-45`
- Problem: Both return the path component after the last '/' or ':' with identical bodies; dash_card_tool.h is already the header shared between the two files.
- Fix: Export one of them through dash_card_tool.h and delete the other.
- Benefit: ~7 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-343: dashSinf/dashCosf are out-of-line forwarders to sinf/cosf

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_gfx.c:63-71`; `Launchstation/include/dash_internal.h:322-323`
- Problem: dashSinf and dashCosf only return sinf(x) and cosf(x). Because they are defined in dash_gfx.c, the ~90 call sites in the background, art and UI files pay an extra cross-file call per evaluation in per-frame animation code.
- Fix: Replace the two prototypes with static inline definitions (or #define to sinf/cosf) in dash_internal.h and delete the two functions from dash_gfx.c; callers stay unchanged.
- Benefit: ~7 net lines removed; removes one call frame per sin/cos in per-frame background code (INFERRED, not measured)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-344: dash_devices.c exports with no outside user: dashMcPresent (test-only), dashDiscStatus, dashDevicesAutoProbe

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_devices.c:187-190`; `Launchstation/include/dash_internal.h:809`; `Launchstation/src/dash_devices.c:254`; `Launchstation/include/dash_internal.h:808`; `Launchstation/src/dash_devices.c:856`; `Launchstation/include/dash_internal.h:797`
- Problem: dashMcPresent has no production caller (only tests/test_device_input.py:394-395); dashDiscStatus is only called at dash_devices.c:443 and dashDevicesAutoProbe only at dash_devices.c:796, yet both are exported through dash_internal.h.
- Fix: Delete dashMcPresent and its prototype (drop the two test asserts); make dashDiscStatus and dashDevicesAutoProbe static and delete their prototypes.
- Benefit: ~7 lines; no ELF change (dashMcPresent is already discarded by -ffunction-sections + --gc-sections, BUILD-EVIDENCE.md:107-108)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-345: smb2Connect_out_t is never written by smb2man; the EE passes a dummy out buffer

- Category: 3 (Overengineering)
- Location(s): `Launchstation/include/smb2man.h:22-25`; `Launchstation/src/smb_shares.c:227`; `Launchstation/src/smb_shares.c:231`; `Launchstation/src/smb_shares.c:238`
- Problem: The header says ctx is 'not written: kept so the EE's call is unchanged'; smb2man_connect_devctl (iop/smb2man/smb2man.c:1491, called at 1552) takes only the input struct. The EE still declares, zeroes and passes the out struct, while the NFS connect beside it passes NULL, 0 (smb_shares.c:256).
- Fix: Delete smb2Connect_out_t and pass NULL, 0 as the out buffer, as NFS_DEVCTL_CONNECT does; update tests/smb2man_live/drive.c:58 the same way.
- Benefit: ~7 lines removed; one 4-byte devctl out buffer less per SMB2 connect (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-346: time_manip_automatic_run re-implements sasFolderTimestamp inline

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/filer_actions.c:1840-1845`; `Launchstation/src/filer_actions.c:1804-1805`; `Launchstation/src/filer_actions.c:1815`; `Launchstation/src/filer_actions.c:1729-1738`
- Problem: The SAS loop body at 1841-1844 is exactly sasFolderTimestamp (1734-1737): build effective name, category, payload, setSasTimestamp. The effective/payload buffers and category local exist only for this copy.
- Fix: Call sasFolderTimestamp(name, &stats._Modify) in the loop and delete the three locals.
- Benefit: ~7 lines removed; also drops ~2 x (MAX_NAME+8) bytes of stack in that frame (INFERRED from the declarations)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-347: Classic browser R1 handlers: GetSize prints an unterminated sizeC buffer, and NEWICON's DoneIcon copies a possibly uninitialised tmp1

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_browser.c:2124-2131`; `Launchstation/src/filer_browser.c:1462-1464`; `Launchstation/src/filer_browser.c:1498-1499`
- Problem: GetSize: sizeC[21] is filled backwards from sizeC[20] (sizeP starts at &sizeC[21]), so no NUL follows the last digit inside the array. sprintf('%s', sizeP) reads stack bytes past the array, so garbage digits can be appended to the message. NEWICON: if the first keyboard() is cancelled (1463-1464), DoneIcon runs strcpy(tmp, tmp1). On the first R1 action of a browser session, tmp1 has not been written, so this reads uninitialised stack memory. Both paths are reachable only from the classic browse mode (cnfmode 0), which docs/dashboard.md:96-97 says no dashboard entry opens.
- Fix: If the classic browser is kept, replace the sizeC block (2124-2131) with a single ' mcTsz=%llu' sprintf, as lines 1841 and 2068 already do, and replace the dummy strcpy at 1499 with an empty statement after the DoneIcon label. If the classic browser is deleted as dead code, both go with it.
- Benefit: About 6 lines removed. Fixes an out-of-bounds read and an uninitialised read, both latent because the code is practically unreachable (READ; reachability INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-348: McView version, cardtype and cardflags are filled in but never read; has_ecc and title_break are read only by a test

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/mc_card_view.h:40-43`; `Launchstation/include/mc_card_view.h:56`; `Launchstation/src/mc_card_view.c:216`; `Launchstation/src/mc_card_view.c:229-230`; `Launchstation/src/mc_card_view.c:246-247`; `Launchstation/src/mc_card_view.c:311-315`
- Problem: mcViewOpen copies the superblock version string, card type and flags into McView, but nothing in the repo reads v->version, v->cardtype or v->cardflags (mcCardFormat and mcCardReadIdentity take the identity from the raw superblock). has_ecc duplicates page_stride == 528 and is read only by tests/mc_card_view_host.c:37; McViewSave.title_break is read only by that test (line 44) and also set in dash_card_tool.c:2320-2324.
- Fix: Delete the version, cardtype and cardflags fields and their assignments. Optionally also drop has_ecc (the test can print page_stride) and title_break with its two fillers, adjusting the host test output.
- Benefit: ~6 lines removed (up to ~14 with has_ecc/title_break); 15 bytes per McView (INFERRED: char[13] + 2 x uint8_t).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-349: dashHomeRebuild is a forwarding wrapper placed after the end-of-file banner, with a duplicate prototype in dash_apps.c

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_home.c:558-565`; `Launchstation/src/dash_apps.c:12`
- Problem: dashHomeRebuild only calls the static homeRebuild and sits below the 'End of file' banner. dash_apps.c:12 re-declares it although dash_internal.h:733 already does and dash_apps.c includes that header.
- Fix: Rename homeRebuild to dashHomeRebuild (non-static), delete the wrapper and the duplicate prototype in dash_apps.c, and keep the banner at the real end. tests/test_home_focus.py:80 extracts 'static void homeRebuild(' and must follow the rename.
- Benefit: ~6 lines removed; no ELF change (INFERRED: the wrapper is a tail call).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-350: isWritablePath in dash_themes.c duplicates isWritableDevice in dash_store.c

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/dash_themes.c:949-954`; `Launchstation/src/dash_store.c:180-187`
- Problem: Both functions test the same eight device prefixes (mc, mass, usb, hdd, mmce, mx4sio, ilink, ata) with the same strncmp chain; they differ only in the NULL/empty guard of the dash_store.c copy.
- Fix: Make isWritableDevice non-static (prototype in dash_internal.h), use it in themeRoots and delete isWritablePath.
- Benefit: ~6 source lines removed net; one copy of the strncmp chain fewer in .text (INFERRED, under 100 bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-351: #ifdef TMANIP_MORON branch is never compiled and the TMANIP define is never tested

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer_actions.c:1166-1170`; `Launchstation/ps2.yaml:628`
- Problem: No build file or source defines TMANIP_MORON, so the HACK_FOLDER branch in setMemoryCardFolderTimestamp is dead. ps2.yaml defines TMANIP, but no source tests TMANIP (the only TMANIP-prefixed test is TMANIP_MORON).
- Fix: Delete the #ifdef/#else/#endif and the HACK_FOLDER line, keeping folder_name = file->name; drop the TMANIP define from ps2.yaml (docs mention it as a feature name only).
- Benefit: 4 source lines plus one define removed; no ELF change (INFERRED: preprocessor only)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-352: Dead bits in dash_editor.c: constant-empty status ternary, needless details[] buffer, avoidable forward declaration

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_editor.c:921-923`; `Launchstation/src/dash_editor.c:721-728`; `Launchstation/src/dash_editor.c:410`
- Problem: editorScene formats '%s' with 'ndocs > 1 ? "" : ""', which is always empty. switchDocument snprintf's 'unsaved' or '' into a 128-byte details[8][16] stack array only to turn '' back into NULL. docNew is forward-declared because it is defined after its only earlier user docOpenPath.
- Fix: Drop the trailing %s and its argument; set items[i].detail = docs[i].dirty ? "unsaved" : NULL and delete details[]; move docNew above docOpenPath and delete the forward declaration.
- Benefit: ~5 lines; 128 bytes of stack (INFERRED: char[8][16])
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-353: Set_Language repeats the table copy and MISC path rebuild that Load_External_Language does right after it

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/lang.c:442-449`; `Launchstation/src/dash_settings.c:1918-1919`; `Launchstation/src/dash_settings.c:2561-2562`
- Problem: Every dashboard caller runs Set_Language then Load_External_Language. Both release the external buffer, memcpy the whole Lang_String table and run updateLocalizedMiscPaths, so each language change does that work twice. The only standalone Set_Language callers are in the classic Config_Startup screen (config_startup.c:113,146), see finding 1.
- Fix: Reduce Set_Language to storing the normalised language, or have the callers set setting->language and call Load_External_Language only.
- Benefit: ~5 source lines removed and one redundant table copy plus MISC rename pass per language change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-354: setPartyList has no caller

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/filer.c:775-778`
- Problem: setPartyList only forwards to setPartyListForDevice("hdd0:") and nothing calls it; every user calls setPartyListForDevice directly.
- Fix: Delete setPartyList.
- Benefit: 5 source lines; no ELF change: the 32-byte section is already discarded by --gc-sections (MEASURED: raw/experiment-fdata/link/wLaunchELF-gc-discarded-first-party.tsv).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-355: Functions used only in their own file are exported (filer.c readers, filer_actions.h timestamp helpers)

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/filer.c:220-223`; `Launchstation/include/filer_shared.h:105`; `Launchstation/include/filer_actions.h:9`; `Launchstation/include/filer_actions.h:14`
- Problem: readMC, readXFROM, readCD, readVMC, readHDD, readHDDDVRP, readGENERIC, readHOST, readUDPFS, readSMB and setDVRPPartyList are non-static but used only in filer.c; readSMB is still declared in filer_shared.h. filer.c also re-declares dashWorkerOnThread locally (222) although dash_internal.h:997 declares it. filerCanSetCustomTimestamp and time_manip_custom are declared in filer_actions.h but used only inside filer_actions.c.
- Fix: Make these functions static, delete the readSMB prototype from filer_shared.h and the two declarations from filer_actions.h; turn the readGENERIC forward declaration into a static one.
- Benefit: ~4 header/prototype lines; lets the compiler inline single-call readers (ELF effect not measured).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-356: Tools table: TA_BROWSER_AT and Tool.path duplicate TA_BROWSER

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_tools.c:27`; `Launchstation/src/dash_tools.c:46`; `Launchstation/src/dash_tools.c:56-58`; `Launchstation/src/dash_tools.c:1615-1617`
- Problem: Tool.path is NULL for every entry except the three TA_BROWSER_AT rows, where it is ""; runTool passes t->path ? t->path : "" to dashOpenBrowser, which is exactly what TA_BROWSER does with dashOpenBrowser(""). The extra action and the field never carry information.
- Fix: Change the three rows to TA_BROWSER, delete TA_BROWSER_AT, its case and the path member (and the trailing NULL/"" initializer of all 28 rows).
- Benefit: ~4 lines plus 28 initializers shortened; 112 bytes of .rodata (28 entries x 4-byte pointer, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-357: Unused parameter and return value in dash_ui.c header/clock code

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_ui.c:520-530`; `Launchstation/include/dash_internal.h:879`; `Launchstation/src/dash_main.c:1205`; `Launchstation/src/dash_ui.c:428-464`
- Problem: dashDrawHeader takes section_anim and discards it with (void)section_anim; its only caller dash_main.c:1205 passes it. clockText returns 0/1 but both callers (dash_ui.c:473 and :501) ignore the result.
- Fix: Drop the section_anim parameter (prototype, definition, the one call and the (void) line) and make clockText void. tests/test_files_tab_lifecycle.py:35 asserts the call text and needs the same edit.
- Benefit: ~4 lines; no measurable size change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-358: visibleWork() in dash_worker.c only forwards to dashWorkerQueued(DASH_JOB_VISIBLE)

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_worker.c:351-354`
- Problem: visibleWork is a one-line static wrapper with one caller (dash_worker.c:358).
- Fix: Delete visibleWork and call dashWorkerQueued(DASH_JOB_VISIBLE) at line 358.
- Benefit: 4 lines removed; no binary change (inlined already, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-359: mcMkDir-and-accept-exists helper written twice (ensure_mc_folder and ensureHistoryDirectory)

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/main_exploit_installer.c:1166-1170`; `Launchstation/src/main_history.c:60-64`
- Problem: Both functions run mcSync, mcMkDir(port, 0, dir), mcSync and map 0/-4 to success with the same five lines.
- Fix: Make ensure_mc_folder's mkdir part a shared non-static helper (without its exists probe) and call it from ensureHistoryDirectory.
- Benefit: ~4 lines removed net (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-360: Duplicate extern declarations of LastDir, elisaFnt and latestDVRPMount

- Category: 2 (Duplication)
- Location(s): `Launchstation/include/filer_shared.h:51`; `Launchstation/include/filer_shared.h:92`; `Launchstation/include/filer_internal.h:8`; `Launchstation/include/launchelf.h:349`; `Launchstation/include/launchelf.h:570-572`; `Launchstation/include/launchelf.h:576`
- Problem: filer_shared.h includes launchelf.h and then redeclares LastDir and elisaFnt, and filer_internal.h redeclares latestDVRPMount, all already declared in launchelf.h.
- Fix: Delete the three redundant declarations in filer_shared.h and filer_internal.h.
- Benefit: 3 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-361: Small SMB leftovers: SMB_CNF_MAX_SIZE defined twice, stale header comments, dead store

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/smb_shares.c:20`; `Launchstation/include/smb_shares.h:129`; `Launchstation/include/smb_shares.h:84-85`; `Launchstation/include/smb_shares.h:140`; `Launchstation/src/smb_cnf.c:602`
- Problem: smb_shares.c:20 redefines SMB_CNF_MAX_SIZE already defined by the header it includes (line 11, under SMB). smb_shares.h:84-85 says a 445 port is left out of the URL, but smbCnfBuildBlock always writes it (smb_cnf.c:528-529). smb_shares.h:140 carries a second comment that belongs to the section functions. smb_cnf.c:602 sets i = 0 just before i = text_len.
- Fix: Delete smb_shares.c:20 and smb_cnf.c:602; correct the 445 sentence; drop the misplaced comment on line 140.
- Benefit: ~3 lines; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-362: SmbShare.error is written but never read

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/smb_shares.h:63`; `Launchstation/src/smb_shares.c:385`; `Launchstation/src/smb_shares.c:467`
- Problem: The field is set from the connect return and cleared on forget; status text uses sh->problem instead. No reader of .error/->error on a share exists in src, include or tests.
- Fix: Delete the field and its two assignments.
- Benefit: 3 lines; 4 B per SmbShare, 32 B per SmbConfig (static smb_cfg plus snapshot copies) INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-363: dash_smbd.c #else stub of DashSmbServer is never referenced, and its int return is ignored

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_smbd.c:81-82`; `Launchstation/src/dash_tools.c:1635-1637`; `Launchstation/include/dash.h:25`
- Problem: The only caller of DashSmbServer is itself inside #ifdef ETH (dash_tools.c:1635-1637), so the non-ETH stub can never be called in any build (ETH is always set, ps2.yaml:624, and configure_ds34.py does not remove it). The caller also discards the 0/1 return value.
- Fix: Delete the #else stub (and its #else), and make DashSmbServer return void (definition and the dash.h:25 prototype).
- Benefit: ~3 source lines removed; no ELF change (INFERRED: the stub is not compiled with ETH set).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-364: mcPsvSign and ps1VmpSign are exported but used only inside mc_save_export.c

- Category: 1 (Dead code)
- Location(s): `Launchstation/include/mc_save_export.h:95-97`; `Launchstation/src/mc_save_export.c:90-102`
- Problem: No file outside mc_save_export.c (sources or host tests) calls mcPsvSign or ps1VmpSign; their only callers are ps1SaveToPsv (:145), mcSaveToPsv (:237) and ps1CardSeal (:347). The link discards their out-of-line copies (108 bytes each) because every use is inlined.
- Fix: Make both static and delete their prototypes and comment from mc_save_export.h.
- Benefit: 3 header lines removed and a smaller public interface; no ELF change (MEASURED: both already gc-discarded, gc-discarded-first-party.tsv lines 64-65).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-365: Stale file banner and a misplaced function comment in dash_card_tool.c

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/dash_card_tool.c:3-5`; `Launchstation/src/dash_card_tool.c:1935-1938`
- Problem: The banner says the file handles single-save copy/move/delete/export/import for .mcd/.ps2/.bin, but it also holds resize, Game ID split, blank cards, convert, All saves, the Save library and the MMCE switch dialogs. Lines 1935-1938 describe ctAllBatch (defined at 2008) but sit directly above ctAllExport's own comment.
- Fix: Shorten the banner to name the file's scope, and move the 1935-1938 comment to ctAllBatch (merging it with 2006-2007).
- Benefit: Accurate comments; ~2 lines net (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-366: dashShowSystemInfo copies cached_console_info into a second static SysInfo

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/dash_info.c:348`; `Launchstation/src/dash_info.c:355`; `Launchstation/src/dash_info.c:359-371`
- Problem: static SysInfo job is filled by a plain copy of cached_console_info and then only read; the name is left over from when the identity was produced by a worker job.
- Fix: Delete job and the copy, and read cached_console_info directly at 359-371.
- Benefit: ~2 lines and 92 B .bss removed (INFERRED: SysInfo = 25 + 64 + 2 bytes, padded).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-367: dash_tools.c includes mmce_igr.h twice and re-declares dashToolsRefreshMmceStatus

- Category: 7 (Build/CI/repo clutter)
- Location(s): `Launchstation/src/dash_tools.c:10-12`; `Launchstation/src/dash_tools.c:428`
- Problem: #include "mmce_igr.h" appears on lines 10 and 12; the forward declaration of dashToolsRefreshMmceStatus at 428 repeats the prototype already in dash_internal.h:1031, which the file includes at line 8.
- Fix: Delete line 12 and line 428.
- Benefit: 2 lines; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-368: loadFont closes a failed descriptor and seeks to 0 twice

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/draw_text.c:74-84`
- Problem: When genOpen fails (fd < 0) loadFont still calls genClose(fd) on the negative value (line 76). Line 79 seeks to 0 on a freshly opened file right before seeking to the end, and line 84 seeks to 0 again.
- Fix: Delete the genClose in the fd < 0 branch and the first genLseek(fd, 0, SEEK_SET).
- Benefit: 2 lines and two IO calls removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-369: mcSaveFormatRead rewrites *format with an expression that always equals f

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/mc_save_formats.c:951-952`
- Problem: *format is already set to f at line 938, and the conditional at line 952 evaluates to f in every branch (f, or MCSF_UNKNOWN only when f is MCSF_UNKNOWN), so the final if changes nothing.
- Fix: Delete lines 951-952.
- Benefit: 2 source lines; a few instructions
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-370: smbSharesConnect retry_failed parameter is always 1

- Category: 3 (Overengineering)
- Location(s): `Launchstation/src/smb_shares.c:363-373`; `Launchstation/include/smb_shares.h:130`
- Problem: All five callers pass retry_failed = 1 (dash_devices.c:648, 653; filer.c:826, 1922; init.c:988), so the if (retry_failed) guard is always entered; only the rate limit can clear it.
- Fix: Drop the parameter; keep the rate-limit test as a local flag.
- Benefit: ~2 lines and one argument at 5 call sites; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-371: Folder-date tools redo whole-list work: date editor re-sorts every frame with struct copies, A-Z/Z-A dating is O(N^2 x 60)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/filer_actions.c:775-791`; `Launchstation/src/filer_actions.c:817-837`; `Launchstation/src/filer_actions.c:1080-1083`; `Launchstation/src/dash_browser.c:2473`; `Launchstation/src/filer_actions.c:1526-1539`; `Launchstation/src/filer_actions.c:1835-1838`
- Problem: The dashboard date editor calls filerDateEditorSync every frame (dash_browser.c:2473, UI thread); it runs two stricmp scans and the insertion sort, which copies each FILEINFO into a temporary and back even when nothing moves (782 and 788). setSequentialTimestamp steps one second at a time, position x 60 calls per folder, so A-Z/Z-A ordering of N folders costs 30 x N x (N-1) step calls on the UI thread before any progress is drawn (1853). With the usual few dozen MC-root folders both costs are small; they grow with folder count (N=500: ~7.5 M step calls).
- Fix: In tsEdit call filerDateEditorSync only when an input changed the selection or a date. In sortCustomDateEditorFolders skip an element already in order before copying it. In setSequentialTimestamp subtract position minutes from 23:59:58 arithmetically (as setSasTimestamp does with decrementSasTimestampDate) instead of the per-second loop.
- Benefit: Removes per-frame O(N) struct copies (~0.9 KB per folder per frame) and makes A-Z/Z-A dating O(N) instead of O(N^2 x 60) step calls (INFERRED from the loops; no timing measured).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-372: genFixPath strips the '/' after the device with an overlapping strcpy at four sites (undefined behaviour)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer.c:975-977`; `Launchstation/src/filer.c:989-991`; `Launchstation/src/filer.c:1014-1016`; `Launchstation/src/filer.c:1023-1025`
- Problem: Each site does strcpy(gen_path + (sep - gen_path), sep + 1) with sep inside gen_path, i.e. copies a string onto itself shifted by one byte; overlapping strcpy is undefined in C. It works today only because the copy runs forward byte by byte for these misaligned pointers (INFERRED from newlib's strcpy). The mx4sio site (1031-1032) copies from uLE_path into gen_path and is not affected.
- Fix: Replace the four identical three-line blocks with one small static helper that finds the separator and shifts the tail with memmove, called from the four branches.
- Justification: The helper replaces four copies of the same three lines (net about 2 lines fewer) and makes the shift well defined.
- Benefit: Defined behaviour for every usb/mass/ata/cf path; about 2 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-373: configSnapshotSaveTargetPath re-probes both memory cards for the SYS-CONF target it built a moment earlier

- Category: 5 (Performance)
- Location(s): `Launchstation/src/config.c:1353`; `Launchstation/src/config.c:1362-1363`; `Launchstation/src/config.c:336-338`
- Problem: configBuildSaveTargetsFrom (1353) already resolves the override and cwd paths (configResolveMcAliasPath) and builds the SYS-CONF path with configGetPreferredSysconfPort. configRefreshSaveTargetForWrite at 1363 then repeats the same call with the same arguments for a SYSCONF target (two more card opens, maybe CheckMC) and is a no-op for the other targets, whose paths no longer start with 'mc:'. It runs on every dashboard settings save (cfgSaveRun, worker thread).
- Fix: Delete the configRefreshSaveTargetForWrite call at line 1363; the path copied at 1362 is already resolved.
- Benefit: Two memory-card opens (plus a possible mcGetInfo pair) fewer per dashboard CNF save on the worker (INFERRED); 1 line removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-374: dashToolsCloudBackup reads WLE-CLOUD.CNF three times per menu pass; cloudGoogleConfigured result is already known

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_tools.c:1361-1378`; `Launchstation/src/cloud_google.c:258-283`; `Launchstation/src/cloud_google.c:403-407`
- Problem: Each pass of the menu loop calls cloudGoogleGetClientConfig (reads WLE-CLOUD.CNF and stats the CA), cloudGoogleConfigured (load_session: reads the CNF again and stats the CA again) and cloudGoogleIsLinked (reads the CNF a third time plus the token files). cloudGoogleConfigured fails only when the resolved client id is empty (cloud_google.c:274-279), which is exactly client_id[0] || builtin from values the loop already holds (1361, 1368-1370).
- Fix: Replace lines 1375-1376 with configured = client_id[0] || builtin, dropping the cloudGoogleConfigured call.
- Benefit: One file read and one stat fewer on the config device (usually mc0) every time the menu is redrawn after an action; -1 line (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-375: dash_hdd.c: unused hm.scanning field and redundant hm.changed write

- Category: 1 (Dead code)
- Location(s): `Launchstation/src/dash_hdd.c:101`; `Launchstation/src/dash_hdd.c:902`
- Problem: The scanning member of the hm state struct is never read or written. In injectMatching, hm.changed = 1 after a successful create is redundant: created is incremented on the line before, and afterChange() at line 927 (run whenever created > 0) sets hm.changed = 1 itself.
- Fix: Delete the scanning member and the hm.changed = 1 line at 902.
- Benefit: ~1 line removed, 4 bytes of .bss (INFERRED); cosmetic
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-376: subfunc_Paste shows an 'incompatible drivers' error twice, the first time as a Yes/Cancel confirmation

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_browser.c:2165-2171`
- Problem: When prepareTransferDeviceStacks returns TRANSFER_STACK_INCOMPATIBLE, subfunc_Paste first calls ynDialog with the error text and then puts the same text in mess. Examples: mx4sio<->mc1, udpfs<->smb, udpfs<->host, PC-card CF with DEV9 (filer_device.c:184-199). In the dashboard, ynDialog is classicYesNo, a 'Please confirm' modal with Yes/Cancel (dash_widgets.c:626-632, installed at :672 and dash_main.c:1358). After that, opPaste sees fb.msg set and shows a second 'Paste failed' modal (dash_browser.c:898-899, showPasteFailure).
- Fix: Delete the ynDialog call at 2167. The message in mess is already shown: by showPasteFailure in the dashboard, and in the header in the classic browser.
- Benefit: 1 line removed. One modal per failed paste instead of a misleading Yes/Cancel prompt followed by the real error (READ).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-377: Backing out of the settings-save wait in dashToolsSyncAllMmceCards hides the finished local backup result

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_tools.c:672-673`; `Launchstation/src/dash_tools.c:703-710`; `Launchstation/src/dash_store.c:670-674`
- Problem: The local card backup has already run (638-651) when the cloud branch calls dashStoreFlushWait (672) and returns on failure (673). It fails only when the user presses Back on the cancelable wait overlay (dash_store.c:670-674, dash_worker.c:313-316), and the function then exits without the local summary (703-710) that every other path shows, so the user never sees how many cards were backed up or failed.
- Fix: On a 0 return skip only the Drive upload and fall through to the local-only summary, for example by entering the cloud block only when cloudGoogleIsLinked() and dashStoreFlushWait(...) both succeed.
- Benefit: The local backup result is always reported; no size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-378: Crystal light streak is cut off at 47% brightness: lifetime 2.2 s but fade computed over 2.6 s

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_bg.c:193-195`; `Launchstation/src/dash_bg.c:330-333`
- Problem: dashBgUpdate ends the streak when streak_t > 2.2, but sceneCrystal fades it as sin(streak_t / 2.6 * pi); at 2.2 the alpha is still sin(0.846 pi) = 0.47 and the centre is at x = 680, so a 240 px wide glow visibly pops out instead of fading.
- Fix: Use one constant for both (end the streak at 2.6, or divide by 2.2).
- Benefit: Removes a visible pop in the Crystal scene; 0 lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-379: PS1 PVD timestamp table and Retro GEM packet drawing duplicated in POPSLoader retrogem.cpp

- Category: 2 (Duplication)
- Location(s): `Launchstation/src/main_gameid_table.c:9-136`; `Launchstation/src/main_gameid.c:13-105`; `POPSLoader/src/retrogem.cpp:55-181`; `POPSLoader/src/retrogem.cpp:469-523`
- Problem: The 124-entry OSDMenu-derived timestamp-to-title-ID table (dupes-blocks reports 123 identical normalised lines) and the Retro GEM packet build/CRC/sprite loop exist in both repos. The table is upstream-derived data.
- Fix: No deletion is possible inside one repo. Keep one copy as the reference (Launchstation's, which names its OSDMenu source) and update both together; do not let them drift.
- Benefit: Cross-repo note only; 0 lines removed in Launchstation
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-380: Text Editor docInsert deletes the selection before checking the 512 KB limit, so a refused paste loses the selected text

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_editor.c:173-183`
- Problem: docInsert calls docDelete on the selection first and only then docReserve(n); when len - selection + n exceeds ED_MAX_SIZE (paste of a long clip over a selection in a file near 512 KB) the toast says the text is too large, but the selection is already gone and the document is marked dirty.
- Fix: Check the limit before deleting: call docReserve with n minus the selection length (it returns 1 for a non-positive extra) ahead of docDelete, and return 0 without touching the document when it fails.
- Benefit: Fixes a minor edit-loss case; net ~0 lines (reorders the existing check) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-381: filerGetGameTitle leaves two bytes of a rejected PS1 header in the title, and copy() uses them for the .psu name

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_copy.c:653-655`; `Launchstation/src/filer_copy.c:678-681`; `Launchstation/src/filer_copy.c:948-954`; `Launchstation/src/filer_copy.c:1050-1051`; `Launchstation/src/dash_browser.c:321-322`; `Launchstation/src/filer.c:2477-2479`
- Problem: The PS1 probe reads the first 2 bytes of the candidate save straight into out and returns -1 when they are not SC, leaving those bytes (and stale bytes after them) in out. The browser callers clear the title on failure, but copy() runs transcpy_sjis on the title whatever the result: a PSU backup with titles shown (or PSU_HugeNames) of a folder that holds a same-named, 8 KB-multiple file that is no PS1 save is named after those bytes, and the folder-conflict prompt shows them as the save title.
- Fix: Read the 2-byte magic into a local buffer (or clear out[0] at finish when ret < 0) so a failed lookup always leaves an empty title.
- Benefit: Correct .psu name and prompt in that case; no size change (READ).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-382: loadExternalModule reports a failed embedded load as success (raw module ID used as a flag)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/init.c:1543-1555`; `Launchstation/src/init.c:1719-1722`; `Launchstation/src/init.c:2117-2118`
- Problem: def_OK holds the return of SifExecModuleBuffer, which is negative on failure, so 'if (def_OK) return 1' also returns 1 when the embedded ps2kbd/DS34 module failed to load (and the start result in dummy is ignored). have_ps2kbd or have_ds34 is then latched for the IOP session, startKbd marks the keyboard open, and no retry happens; the header comment at 1523-1531 promises 0 in that case.
- Fix: Set def_OK to (ID >= 0 && dummy != 1) instead of the raw ID.
- Benefit: Correct 'not loaded' result for the keyboard/DS34 fallback; one-line change, no size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-383: loadFont accepts a truncated .fnt file and reports success with a partly stale FontBuffer

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/draw_text.c:79-96`
- Problem: loadFont only rejects files larger than 4700 bytes; it never checks a minimum size or the genRead results. A file shorter than 100 bytes is checked against uninitialised FontHeader bytes, and a file with a valid header but shorter than 1018 + 3584 = 4602 bytes leaves the tail of FontBuffer from the previous font while returning 1. Reachable from Settings when the user picks a font file (dash_settings.c:2128) and at startup (main_startup.c:716).
- Fix: Reject sizes below 4602 in the same size test at line 80 (accept only 4602..4700).
- Justification: One extra comparison in an existing condition; it turns a truncated copy of a font file into the existing default-font fallback instead of mixed glyphs.
- Benefit: Avoids corrupted glyphs from a truncated font file; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-384: mcViewListSaves and describeSave re-walk the directory chain from its first cluster for every entry (quadratic)

- Category: 5 (Performance)
- Location(s): `Launchstation/src/mc_card_view.c:165-178`; `Launchstation/src/mc_card_view.c:295-297`; `Launchstation/src/mc_card_view.c:334-341`
- Problem: Both loops call dirEntry(v, first, i), which uses chainRead from the first cluster, so entry i costs i/2 chainNext FAT lookups plus one full cluster read; a root of L entries costs about L*L/4 FAT lookups and L cluster reads instead of L/2. The file already has walkDir (lines 383-409), written for exactly this ('instead of re-walking the chain per entry'), used by the fingerprint and rootFind but not by the listing, which the card tool runs on the UI thread after every open and operation.
- Fix: Drive the root loop of mcViewListSaves and the loop of describeSave with walkDir callbacks, as rootFind (mc_card_write.c:139-151) does.
- Benefit: INFERRED: root of 600 entries (large 64 MB card) ~90,000 cached FAT lookups and 600 cluster reads become ~300 of each; on typical 8 MB cards (under ~150 entries) the gain is milliseconds. About the same line count.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-385: tsChoose never shows its fifth detail string (Tuna Date)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_browser.c:2403-2412`; `Launchstation/src/filer_actions.c:1406-1415`
- Problem: details[] has five entries, the fifth being "set selected MC folder to 2099", but the loop uses i < 4 ? details[i] : NULL, so the *Tuna Date row (passed as the fifth label when allow_tuna is set, filer_actions.c:1406-1415) never gets its detail and the string is dead.
- Fix: Use the array size instead of 4 (or delete the fifth string if the row should stay bare).
- Benefit: Trivial UI fix or a few bytes of dead .rodata (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-386: tsChoose shows the detail text only for the first four folder-date choices; the *Tuna Date row never gets its detail

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_browser.c:2403-2413`; `Launchstation/src/filer_actions.c:1406-1415`
- Problem: details[] has five strings, including 'set selected MC folder to 2099' for the fifth choice, but the loop assigns items[i].detail only for i < 4. selectTimestampOrganizationMode passes five items (TIMESTAMP_ORGANIZE_COUNT = 5, filer_actions.c:385-390) whenever the Tuna option is allowed, so that row is shown without its explanation.
- Fix: Bound the detail lookup by the array size (5) instead of the literal 4.
- Benefit: Cosmetic correctness: the fifth detail string becomes visible; no size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### LAUNCHSTATION-387: Crystal light streak ends at 2.2 s but is drawn on a 2.6 s path, so it vanishes mid-screen at about 47 % brightness

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/dash_bg.c:192-196`; `Launchstation/src/dash_bg.c:330-336`
- Problem: dashBgUpdate resets streak_t once it passes 2.2 s, but sceneCrystal computes p = streak_t / 2.6. The last drawn frame has p = 0.846: centre x = 680, a 240 px radius glow covering x 440-640, alpha 110*sin(0.846*pi) = 51 additive. It disappears in one frame instead of fading out off-screen. Visible in Crystal with effects at 100 %.
- Fix: Use the same duration in both places (reset at 2.6f).
- Benefit: Removes a visible pop in the Crystal scene; one constant changed.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-388: FTP status loop sleeps 100 ms per frame, so the screen runs at about 8 fps and short OK/Back taps are lost

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_ftp.c:90-120`
- Problem: Each pass of DashFtpServer's UI-thread loop does a status RPC, draws, presents (vsync) and then DelayThread(100000). Input is edge-detected from one pad sample per pass (dash_ui.c:52-59, pad.c:62), so a press and release inside one ~117 ms pass never produces in.pressed; the user has to press again or hold. It is the only 100 ms sleep in the dash_* screens.
- Fix: Drop the DelayThread (dashGfxPresent already waits for vsync) and query ftpSessionQuery only every few frames, e.g. every 6th pass, so the RPC rate stays about 10 Hz.
- Benefit: Input sampled every frame instead of every ~117 ms and smooth animation on the FTP screen; about the same line count (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-389: Unbounded strcpy/strcat/sprintf path joins into MAX_PATH buffers in newdir, make_title_cfg and setHddCommonFolderTimestamp

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_actions.c:2204`; `Launchstation/src/filer_actions.c:2214`; `Launchstation/src/filer_actions.c:2219-2220`; `Launchstation/src/filer_actions.c:2225`; `Launchstation/src/filer_actions.c:2244-2245`; `Launchstation/src/filer_actions.c:2250-2251`; `Launchstation/src/filer_actions.c:1926-1927`; `Launchstation/src/filer_actions.c:1208`
- Problem: These join a caller path (up to MAX_PATH-1 = 1024 chars) and a name (up to 255) into a 1025-byte stack buffer with no length check, while the rest of the file (delete 1972, getFileSize 314/352, Rename 2176-2182) checks snprintf and returns -ENAMETOOLONG. A copy into a destination whose path plus folder name exceeds 1024 bytes (newdir from filer_copy.c:1048/1081) or a title.cfg in a folder deeper than 1015 chars overruns the stack. Needs very long paths, so practical risk is low.
- Fix: Replace each strcpy+strcat (or sprintf) pair with one snprintf into the buffer and return -ENAMETOOLONG (make_title_cfg: set the error message) when the result does not fit, as delete() already does at 1972.
- Justification: Adds a length test per join, but each strcpy+strcat pair becomes a single snprintf, so line count does not grow; it closes a stack overflow on over-long paths using the pattern already used elsewhere in the file.
- Benefit: Removes 8 unbounded writes into 1025-byte stack buffers; about 4 lines saved by merging strcpy+strcat pairs (INFERRED from cited lines).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-390: filerGetGameTitle's .psu scan turns a large entry size into a backward seek and can loop up to 2^31 times

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_copy.c:565`; `Launchstation/src/filer_copy.c:613-626`
- Problem: psu_pad_pos is an int computed from the u32 entry size read from the file, so a size of 0x80000000 or more becomes a negative SEEK_CUR offset that moves back to an earlier header, and the loop is bounded only by the first header's count (also from the file, up to 0x7FFFFFFF). A malformed .psu keeps the title scan re-reading the same headers, stalling the browser listing job on the worker (dash_browser.c:321) or the classic listing (filer.c:2477) whenever titles are shown.
- Fix: Declare psu_pad_pos as u32 so the seek always goes forward; a bogus size then seeks past the end and the next header read fails.
- Benefit: Removes a hang on malformed .psu files; no size change (READ; the trigger needs a crafted or corrupt file).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-391: Devices grid refresh issues a PC CARD CF devctl RPC from the UI thread every 0.5 s, even while a worker job runs

- Category: 5 (Performance)
- Location(s): `Launchstation/src/dash_devices.c:415-431`; `Launchstation/src/dash_files.c:589-593`; `Launchstation/src/init.c:3177-3195`
- Problem: devUpdate calls devRefresh every 0.5 s, and dashDevicesList calls getPcCardCfCapacitySectors (a fileXioDevctl SIF RPC) whenever the PCCF driver is loaded. This is UI-thread IO without the dashWorkerIoBusy() gate that dash_main.c:1472 applies to the other device polls (dash_worker.c:7-10 rule), so the frame waits on the shared fileXio RPC while a worker copy or listing is in flight. Only consoles with the CF driver loaded (early Japanese models) are affected.
- Fix: In dashDevicesList skip the capacity devctl while dashWorkerIoBusy() is true (show the PROBE_PCCF result already recorded by probeMount instead).
- Justification: One added condition removes a periodic blocking RPC from the render thread; it follows the existing gate used for the other device polls and is smaller than caching the capacity.
- Benefit: Removes up to 2 RPCs/s from the UI thread on the Devices screen; avoids frame stalls during background IO on CF-equipped consoles (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### LAUNCHSTATION-392: getExternalFilePath dereferences NULL for an hdd0:/PART or dvr_hdd0:/PART path without a file part (boot-time crash from a hand-edited CNF)

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/init.c:1425-1435`; `Launchstation/src/init.c:1443-1452`; `Launchstation/src/config.c:1039-1042`; `Launchstation/src/main_modules.c:54`
- Problem: For 'hdd0:/NAME' the code builds party='hdd0:NAME', then p=strchr(party,'/') is NULL, so sprintf prints '(null)' and *p=0 stores to address 0; the DVR branch is identical. USBKBD_FILE and KBDMAP_FILE are copied from the CNF unchecked (config.c:1039-1042) and startKbd runs on every boot, safe mode included (main_modules.c:54), so such a value crashes each start until the CNF is fixed elsewhere. Paths picked in the file browser always contain the file part.
- Fix: When p is NULL, fall back to genFixPath(argPath, filePath) and return (one check per branch).
- Justification: One NULL test per branch closes a store to address 0 reached from a config value at boot; nothing simpler prevents it.
- Benefit: Removes a boot crash loop on a malformed keyboard path (INFERRED consequence of a NULL store on the EE).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-393: make_title_cfg ignores the genWrite result, so a failed or short write leaves a truncated title.cfg with no message

- Category: 6 (Bugs and security)
- Location(s): `Launchstation/src/filer_actions.c:1928-1934`
- Problem: genWrite's return is discarded and _msg0 is only set when the open fails. On a full memory card or USB stick the file is created (O_TRUNC) but the write can fail or be short, leaving an empty or cut title.cfg that OPL then reads, and the user sees no error: both callers only show a message when _msg0 is set (dash_browser.c:1219, filer_browser.c:1589).
- Fix: Compare genWrite's result with the buffer length and set an error message in _msg0 when it differs.
- Justification: Adds one comparison and one message; the problem is a silent corrupt title.cfg on a full or failing device, and checking the one write is the smallest way to report it.
- Benefit: Failed title.cfg writes are reported instead of silently leaving a truncated file (no size change; ~2 lines added).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### LAUNCHSTATION-394: HDD scan allocates 1400 HddPart entries (about 246 KB) and keeps them while the HDD Manager is open

- Category: 4 (Size and memory)
- Location(s): `Launchstation/src/dash_hdd.c:162-166`; `Launchstation/src/dash_hdd.c:373-376`
- Problem: scanRun mallocs sizeof(HddPart) * MAX_PARTITIONS (180 B * 1400, launchelf.h:206) and scan() keeps the whole block as hm.parts even when the disk has a few dozen partitions; during a rescan (after every create/remove/rename) the old and the new 246 KB blocks are alive together.
- Fix: After the Dread loop, shrink j->parts to j->n entries with realloc when j->n > 0 (keep the original block if realloc fails).
- Justification: Adds about three lines; it frees roughly (1400 - n) * 180 bytes of EE heap for as long as the HDD Manager is open, the simplest way short of changing the scan to two passes.
- Benefit: 180 B * 1400 = 252,000 B allocated; with 100 partitions about 234 KB returned to the heap while the screen is open (INFERRED: struct size from field layout)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

## Stale branches and tags (list only)

Listed for information; nothing here is a recommendation to delete without checking. Snapshot of the remote at audit time: 113 branches besides the default, 147 tags. "Open PR" marks a branch that is the head of an open pull request.

### Branches fully merged into master (14)

| Branch | Last commit | Note |
|---|---|---|
| `docs/add-0bsd-license` | 2026-11-02 |  |
| `codex/docs-ui-showcase` | 2026-11-03 |  |
| `claude/move-manifest-followup` | 2026-11-04 |  |
| `codex/selective-ftp-wins` | 2026-11-04 |  |
| `docs/launchstation-readme-badges-download-count` | 2026-11-04 |  |
| `fix/blaze-mcm-folder` | 2026-11-04 |  |
| `fix/release-archive-preflight` | 2026-11-04 |  |
| `fix/rolling-download-metrics` | 2026-11-04 |  |
| `chore/attribution-policy` | 2026-11-05 |  |
| `feat/appinfo-pbt` | 2026-11-05 |  |
| `feat/release-followups` | 2026-11-05 |  |
| `fix/277-boot-speed` | 2026-11-05 |  |
| `maintenance/docs-translations` | 2026-11-05 |  |
| `codex/fix-smb-browser` | 2026-11-06 |  |

### Unmerged branches with no commit in the 30 days before the audit (1)

| Branch | Last commit | Ahead | Behind | Note |
|---|---|---:|---:|---|
| `codex/mmce-local-restore` | 2026-10-06 | 2 | 862 |  |

### Tags (147)

| Tag family | Count | First | Last | Examples |
|---|---:|---|---|---|
| `build*` | 95 | 2026-11-04 | 2026-11-16 | `build-1013`, `build-1019`, `build-1030` |
| `preview*` | 37 | 2026-09-26 | 2026-10-05 | `preview-chatgpt-fix-bgm-adpcm-music-volume`, `preview-osdsys-dash-commits-09262026`, `preview-dash-native-ui-09272026` |
| `usb*` | 6 | 2026-10-08 | 2026-10-08 | `usb-boot-repair-2026-10-08-2dee735`, `usb-mini-comparison-2026-10-08-419e3c1`, `usb-pad-diagnostic-2026-10-08-110be3f` |
| `language*` | 1 | 2026-09-25 | 2026-09-25 | `language-files` |
| `boot*` | 1 | 2026-10-08 | 2026-10-08 | `boot-recovery-2026-10-08-2790d91` |
| `ds34*` | 1 | 2026-10-08 | 2026-10-08 | `ds34-2026-10-08-2761e62` |
| `image*` | 1 | 2026-10-08 | 2026-10-08 | `image-colors-2026-10-08-09546fe` |
| `udpfs*` | 1 | 2026-10-08 | 2026-10-08 | `udpfs-usb-buffer-2026-10-08-63e0f16` |
| `usbd*` | 1 | 2026-10-08 | 2026-10-08 | `usbd-ra-vendor-2026-10-08-256325d` |
| `app*` | 1 | 2026-10-09 | 2026-10-09 | `app-packages-2026-10-08-51e1e0c` |
| `beta*` | 1 | 2026-10-25 | 2026-10-25 | `beta-1` |
| `latest*` | 1 | 2026-11-16 | 2026-11-16 | `latest` |

## Files not read line by line (and why)

Vendored or generated content, reviewed structurally:

- generated bitmap font data arrays: 4 files, 27149 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- generated icon and title-database data arrays: 2 files, 2942 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- language files: 10 files, 7157 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.

Not read as text:

- 123 files, 17.3 MB: binary file (covered by the asset/clutter inventory, not read as text). Mostly under `docs-site` (67), `res` (30), `exploits` (13), `iop` (5), `__Headers` (4), `ship` (2), `docs` (1), `tests` (1).
- 1 files, 0.1 MB: site stylesheet (static web asset, not product code). Mostly under `docs-site` (1).

Not read yet (reading still in progress): 751 files in 31 chunks.
