# Claude audit: Launchstation

Status: IN PROGRESS. 28 of 103 reading passes are in; finding IDs are provisional and will be renumbered until this line says COMPLETE.

- Repository: NathanNeurotic/Launchstation
- Audited branch: master
- Audited commit: 8155d14cb69bffae7c0827cde4ab3eab312b8755
- Audit date: 2026-11-16
- Mode: read-only review. This branch adds only the `Claude-audit/` folder; no source, build or workflow file is changed.

All `path:line` references are pinned to the audited commit above. Each finding was raised by a reader that read the cited files line by line, then re-checked by a second, independent pass that re-read the cited lines and re-ran the reference searches. Findings that did not survive that check were dropped.

## Totals

- Findings in this report: 13. 193 more raised findings are waiting for their second check and are not shown yet.
- Raised by readers: 207. Dropped as wrong or unsupported on re-check: 1. Corrected on re-check: 8. Merged as duplicates: 0.

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

Not read yet (reading still in progress): 799 files in 42 chunks.
