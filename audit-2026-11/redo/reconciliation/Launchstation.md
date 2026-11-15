# Launchstation extraction

## Verdicts
| ID | Title | Effort | Risk | Verdict |
|---|---|---|---|---|
| RE-LAUNCH-001 | libsmb2 randomness on the IOP is a 15-bit LCG seeded from boot seconds, a constant pid, and a heap pointer | M | Medium | VERIFIED |
| RE-LAUNCH-002 | vmcman overwrites derived geometry with raw superblock bytes; cluster reads then write past 1024-byte cache entries | S | Low | VERIFIED |
| RE-LAUNCH-003 | SMB1 NetShareEnum walks the share list on fully server-controlled offsets, count, and string lengths | S | Low | VERIFIED |
| RE-LAUNCH-004 | Anonymous FTP is the unauthenticated transport for a remote ELF-launch command inbox | M | Low | VERIFIED-DRIFT |
| RE-LAUNCH-005 | Self-update overwrites the running ELF in place with no recoverable copy — and the CHANGELOG tells users a `.BAK` exists during the swap | M | Medium | VERIFIED |
| RE-LAUNCH-006 | CNF loader copies unbounded file values into fixed SETTING fields with strcpy/sprintf | S | Low | VERIFIED |
| RE-LAUNCH-007 | Config save-text builder can overrun its heap buffer (unbounded sprintf chain measured with %n into a size estimate) | M | Low | REFUTED |
| RE-LAUNCH-008 | smbd truncates 64-bit SMB file offsets to signed 32-bit before seeking on the card | S | Low | VERIFIED |
| RE-LAUNCH-009 | CI-wired test `test_copy_psu_listing.py` fails deterministically at the pin | M | Low | VERIFIED |
| RE-LAUNCH-010 | The SMB server ships with a published default login, stored in clear text and printed on the status screen | S | Low | VERIFIED |
| RE-LAUNCH-011 | No authentication throttling anywhere in the SMB server, against that same default login | S | Low | VERIFIED |
| RE-LAUNCH-012 | `GET_XPARAM_DB_SIZE()` is identically 1, so the 292-entry game-parameter database is searched over exactly one entry | S | Low | VERIFIED |
| RE-LAUNCH-013 | vmcman's FAT-chain walk indexes the 32-entry ifc_list with an unbounded cluster number | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-014 | hdl_info copies game name/startup with strcpy from disk data and from the EE rename packet | S | Low | VERIFIED |
| RE-LAUNCH-015 | ps2ips_rpc trusts the EE packet's `malign` value in send/sendto; negative lengths pass through recv | S | Low | VERIFIED |
| RE-LAUNCH-016 | usbmass_bd uses the device-reported block length for a stack alloca and unchecked transfer lengths | S | Low | VERIFIED |
| RE-LAUNCH-017 | Unbounded sprintf/strcat path assembly in the copy/delete/MC-read path cluster | S | Low | VERIFIED |
| RE-LAUNCH-018 | scanSystemCnf uses strncat as if it were a bounded copy — duplicate SYSTEM.CNF keys overflow | S | Low | VERIFIED |
| RE-LAUNCH-019 | smbd file handles and auth state survive logoff and tree disconnect; handles bind to the connection, not the session | M | Medium | VERIFIED-DRIFT |
| RE-LAUNCH-020 | `SMBD_DEVCTL_START` parses the credential blob past its stated length, and reports success for credentials it did not take | S | Low | WEAKENED |
| RE-LAUNCH-021 | SNTP clock sync uses a predictable transmit timestamp, and the RTC it sets is what TLS certificate validation trusts | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-022 | TLS entropy callback always reports success, including when its only OS source failed and every other input is at its cold-boot value | S | Medium | VERIFIED-DRIFT |
| RE-LAUNCH-023 | mmceman copies the card-supplied game ID with strcpy into the caller's devctl buffer | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-024 | SMB1 QueryPathInformation dereferences the server's DataOffset without validation | S | Low | VERIFIED |
| RE-LAUNCH-025 | hdl_info sums sub-partition lengths using the disk-controlled `nsub` past the 64-entry array | S | Low | VERIFIED |
| RE-LAUNCH-026 | mmceman set-game-ID narrows the length to u8 and strcpys into a 255-byte stack buffer | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-027 | Classic dialog helpers strcpy unbounded caller text into fixed stack buffers | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-028 | editorSaveAs appends the typed filename with strcpy at the end of a possibly-full path buffer | S | Low | VERIFIED |
| RE-LAUNCH-029 | smbd WRITE accepts lengths above the negotiated maximum the server itself advertised | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-030 | smbd CREATE does not enforce the card's 31-byte leaf limit that RENAME carefully enforces | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-031 | smbd file-id validation uses only 4 of the 16 file-id bytes, and the generation counter wraps at 16 bits | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-032 | smbd server identity is a compile-time constant and session ids restart from a fixed counter on every serve | S | Low | VERIFIED |
| RE-LAUNCH-033 | hdl_info APA chain walk follows disk `next` pointers with only a 10,000-count hack against cycles | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-034 | mmceman directory read stores the card-supplied filename without ensuring NUL termination | S | Low | VERIFIED |
| RE-LAUNCH-035 | vmcman VMC-backing slot validation is off by one and admits negative slots (latent) | S | Low | VERIFIED |
| RE-LAUNCH-036 | cloud_http maintains two divergent HTTP response parsers; chunked detection is case-sensitive and Content-Length goes through atoi | M | Low | WEAKENED |
| RE-LAUNCH-037 | Custom OAuth client override contradicts its own "complete pair" rule: an ID alone silently discards the built-in secret | S | Low | VERIFIED |
| RE-LAUNCH-038 | Long-lived credentials are stored in clear text in ordinary config files on removable media | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-039 | smbd SET_INFO classes report success without performing the operation | S | Low | VERIFIED |
| RE-LAUNCH-040 | smbd LOCK succeeds without locking, and CREATE ignores share-access and oplock semantics | S | Medium | VERIFIED-DRIFT |
| RE-LAUNCH-041 | smbd QUERY_DIRECTORY never validates the information class: unsupported classes return a successful empty listing, and FileId is always zero | S | Low | VERIFIED |
| RE-LAUNCH-042 | Vendored usmb2 contains an unhardened server-length memcpy in readdir, kept out of the IOP build only by a feature define | S | Low | VERIFIED |
| RE-LAUNCH-043 | Classic main-menu loop in main() is unreachable — DashRun never returns | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-044 | Classic config/info/editor screens are shadowed by dashboard interception — a second, parallel settings/UI system is carried but not presented | L | Medium | VERIFIED-DRIFT |
| RE-LAUNCH-045 | Two test files are orphaned from CI — and architecture-maintenance.md claims every test runs there | S | Low | VERIFIED |
| RE-LAUNCH-046 | All 211 CI tests run as one sequential step: first failure masks the rest | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-047 | Updater menu path in README and both wiki guides points to a page that does not contain it | S | Low | VERIFIED |
| RE-LAUNCH-048 | Automatic IGR sync default stated as Off in two texts; code default is Local backup | S | Low | VERIFIED |
| RE-LAUNCH-049 | CNF rename to LAUNCHSTATION.CNF not propagated through dashboard.md and the LAUNCHELF.CNF template header | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-050 | Hand-rolled JSON string extraction is duplicated, and both copies mishandle escapes | M | Medium | VERIFIED |
| RE-LAUNCH-051 | The same C-function brace-extractor helper is copy-pasted into 103 test files | M | Low | VERIFIED-DRIFT |
| RE-LAUNCH-052 | Every directory listing allocates ~900 KB of FILEINFO; recursive size/delete allocate another ~900 KB per level | L | Medium | VERIFIED-DRIFT |
| RE-LAUNCH-053 | Two identical 2049-entry sceMcTblGetDir static buffers are kept in BSS for two readers that never run concurrently | S | Low | VERIFIED |
| RE-LAUNCH-054 | Two animated GIFs duplicate their MP4s at 8–9× the size and dominate the published site and offline ZIP | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-055 | 1,639,271 B of generated font C has no source font in the repo and no regeneration/comparison gate | S | Low | VERIFIED |
| RE-LAUNCH-056 | Two MMCEMAN driver sources (581 lines) are compiled by nothing | S | Low | VERIFIED |
| RE-LAUNCH-057 | Dead vendored code in the smbd compiled set: two empty-by-#ifdef translation units and two client-only libraries linked into a server IRX | S | Low | VERIFIED |
| RE-LAUNCH-058 | Dead file-based PSU extractors and the "historical" file download entry point have no production callers | S | Low | VERIFIED |
| RE-LAUNCH-059 | Action pinning is inconsistent: the `contents: write` build workflow uses mutable tags, the read-mostly metrics workflow pins SHAs for exactly that reason | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-060 | `scripts/stale_code_audit.sh` cannot find what it claims to scan, and is run by nothing | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-061 | res/README.md resource inventory is wrong on sounds, missing on the PS2 sound set, and lists 2 CA roots for a 3-root bundle | S | Low | VERIFIED |
| RE-LAUNCH-062 | 491,822 B of payload files are referenced by no build, script, test or doc-as-used asset | S | Low | VERIFIED |
| RE-LAUNCH-063 | Shipped LAUNCHELF.CNF template contradicts itself on the Triangle launch key | S | Low | VERIFIED |
| RE-LAUNCH-064 | SMB site page credits libsmb2 as the SMB2 driver; the shipped driver is source-built usmb2 | S | Low | WEAKENED |
| RE-LAUNCH-065 | CHANGELOG Rolling NFS entry gives the pre-split settings location for NFS shares | S | Low | VERIFIED |
| RE-LAUNCH-066 | Documentation-site Ecosystem page is a full revision behind the Ecosystem markdown it mirrors | S | Low | VERIFIED |
| RE-LAUNCH-067 | Plan/follow-up status layer is stale: a merged feature is labelled "not merged", and the newest plans carry no status at all and are absent from the docs index | S | Low | VERIFIED |
| RE-LAUNCH-068 | architecture-maintenance.md's largest-files list is already wrong | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-069 | BUILDING.md claims to be generated from building.html; no generator exists, and the three build-prerequisite lists disagree | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-070 | dashboard.md calls the embedded interface sounds "Sony ADP samples"; the repo's own provenance statements say the opposite / unrecorded | S | Low | VERIFIED |
| RE-LAUNCH-071 | __Headers/ ships four APA header sample sets for other applications with a one-line, misspelled README and no usage or provenance note | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-072 | `tests/nfsman_live/` is wired into no workflow and cannot run unattended | S | Low | WEAKENED |
| RE-LAUNCH-073 | Two build targets are defined twice: root `ps2.yaml` and per-module `ps2.yaml` files must be kept in sync by hand | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-074 | Dependency version strings are duplicated between build scripts and tests, so a version bump must edit test fixtures | S | Low | VERIFIED |
| RE-LAUNCH-075 | One CI-run test tests a research model that is explicitly not the shipped code | S | Low | VERIFIED |
| RE-LAUNCH-076 | Two suites can report PASS while a named sub-check did not run | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-077 | `test_transition_latency.py` validates an audio asset through a Python reimplementation of a decoder that is not in this repo | S | Low | WEAKENED |
| RE-LAUNCH-078 | First-open retry loop is copy-pasted three times in filer.c, each with its own empty busy-wait | M | Medium | WEAKENED |
| RE-LAUNCH-079 | The sixteen Misc_* launch strings are rebuilt by three separate hand-written sprintf blocks | M | Medium | VERIFIED-DRIFT |
| RE-LAUNCH-080 | MainExecuteContext is assembled field-by-field three times in main.c | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-081 | IPv4 validation is implemented three times with divergent semantics; the Neutrino copy accepts the leading-zero forms the SMB parser deliberately rejects | M | Low | VERIFIED |
| RE-LAUNCH-082 | BCD and calendar conversions are duplicated inside dash_clock_sync.c, with two different day-count algorithms | S | Low | WEAKENED |
| RE-LAUNCH-083 | The ps2ipc socket ABI block (externs, hostent view, sockaddr view, htons) is copy-pasted between the TLS and SNTP clients | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-084 | The SIO2 hook exists as two near-identical copies (mmceman, mx4sio_bd) already diverged in one behaviour | S | Low | VERIFIED |
| RE-LAUNCH-085 | Text editor result messages are shown with five empty busy-wait spins of 1.5 s each | S | Low | VERIFIED |
| RE-LAUNCH-086 | filer_copy recomputes strlen(out) on every character while counting path depth | S | Low | WEAKENED |
| RE-LAUNCH-087 | SystemCnf_VMODE is parsed, stored and never read — write-only state | S | Low | VERIFIED |
| RE-LAUNCH-088 | WriteFont_C survives as a ~57-line commented-out function inside draw.c | S | Low | VERIFIED |
| RE-LAUNCH-089 | icon.c stores 2048 bytes as 2055 one-byte-per-line C source | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-090 | Development scaffolding (autotest script player, theme tour) is compiled into production sources behind ifdefs | S | Low | WEAKENED |
| RE-LAUNCH-091 | Failed smbd thread start leaks the created thread | S | Low | VERIFIED-DRIFT |
| RE-LAUNCH-092 | ps2ftpd keeps one static 32 KB transfer buffer for the whole server | S | Low | WEAKENED |

## Narrowed scopes
- RE-LAUNCH-020: Past-length credential parse is real, but the claimed indefinite UI/wire divergence is defeated in-tree (init.c:2809-2810 guards the login update and the status screen shows the running login); true scope is a latent IOP over-read on a malformed blob from a future/non-stock caller plus a documented, EE-compensated no-op on START-while-running.
- RE-LAUNCH-036: Buffered-path parser bugs are all real, but the streaming path is not stricter — it uses the same atoi (:1123) and the identical case-sensitive chunked check (:1139-1141); true scope is the buffered-path bug set plus a two-implementation hazard, with no stricter in-tree pattern to mirror.
- RE-LAUNCH-064: smb.html:86 is a provenance sentence about upstream work (where libsmb2 genuinely was the driver), and libsmb2 is a shipped driver for the IOP SMB server, not the smb: client; true scope is an ambiguous sentence needing a reword, not the proposed one-word swap.
- RE-LAUNCH-072: Unwired and unreferenced is verified, but 'cannot run unattended' overstates — run.sh's 3-arg mode is fully non-interactive against a pre-existing export; true scope is no unattended self-provisioning path and nothing invokes it.
- RE-LAUNCH-077: All facts are true, but there is no defect, no fix, and risk is self-rated INFERRED; true scope is a caveat about test epistemology, not a countable finding.
- RE-LAUNCH-078: Three-copy retry duplication is verified, but the fix cannot replace the MX4SIO copy as stated (the helper takes one path; MX4SIO probes four spellings with separate readiness flags); true scope is the ATA copy as only a near-exact replacement.
- RE-LAUNCH-082: BCD pairs are byte-identical duplicates, but the proposed deletion breaks non-Ethernet builds — the SNTP copies live inside #ifdef ETH and the manual copies deliberately sit after #endif; true scope also splits on differing epochs (1970 vs 2000).
- RE-LAUNCH-086: The O(n²) strlen loop is real, but 'only such site in the core' is false — src/config_network.c:100 is a second, worse instance (and dash_card_tool.c:2452 a literal third hit); true scope is one of at least two same-mechanism sites.
- RE-LAUNCH-090: Both dev aids are documented intentional tooling (docs/dashboard.md:573-576, docs/themes.md:436); true scope is that the finding's own minimum alternative — recording usage next to the flag — is already satisfied.
- RE-LAUNCH-092: All facts are verified, but buffer sharing is safe by the server's single-threaded select() design, and with no defect/trigger/fix by its own admission; true scope is an observation, not a finding.

## Defeating evidence
- RE-LAUNCH-007: Allocation is sizeof(SETTING) 28,248 + sizeof(PathPad) 30,750 + 8,192 = 67,190 B while maximum output with every field full-but-valid is ≈61,038 B (slack ≈6,152 B) — the claimed ≈48 KB of path text is already contained inside the allocation, so no independent overrun occurs.

## Candidates
### CAND-01
- Label: CAND-1 (xparam argc gap, low)
- Mechanism: xparam `_start` handles `argc < 3` (game-ID lookup) and `argc > 3` (parameter pairs), so `argc == 3` — an ID plus one dangling argument — silently does nothing at all, skipping even the ID lookup.
- Locations: iop/xparam/src/xparam.c:30-45
- Proposed class: NEW
- Justification: Distinct dispatch fall-through in the same module as RE-LAUNCH-012 but a different mechanism and fix site (argument handling, not the DB-size macro); no redo finding covers it.

### CAND-02
- Label: CAND-2 (save-builder suffix read past a short field, speculative)
- Mechanism: The save builder prints `setting->Misc_* + strlen(setting->Misc)`; if `Misc` is ever longer than a given `Misc_*` field's own string, the pointer lands past that field's NUL and sprintf reads into adjacent, potentially uninitialised, heap SETTING bytes — the only route that could break the bounded-output invariant refuting RE-LAUNCH-007.
- Locations: src/config.c:563,583-597
- Proposed class: REJECTED
- Justification: Speculative and heap-content dependent; the verifier states it would need a host harness with independently-set Misc fields to settle, so it is unverifiable as filed.

### CAND-03
- Label: CAND-3 (NetShareEnum string termination, unsettleable in-clone)
- Mechanism: NetShareEnum uses `strncpy(…, 256)` for ShareName/ShareComment; if the 256-byte destination in `ShareEntry_t` can be left unterminated, EE consumers reading it as a C string over-read — a termination gap that RE-LAUNCH-003's offset/length validation would not by itself address.
- Locations: iop/smbman/smb.c:755-756
- Proposed class: REJECTED
- Justification: `ShareEntry_t` is defined outside this repo (PS2SDK headers), so termination cannot be settled from the clone — unverifiable as filed.

### CAND-04
- Label: C-V2-1 — usbmass_bd warmup divides by zero for sectorSize > 1,000,000 (same ingest value as RE-LAUNCH-016, different, earlier sink)
- Mechanism: The capacity debug print computes `bd->sectorCount / ((1000 * 1000) / bd->sectorSize)`; for a device reporting `block_length > 1,000,000` the divisor is 0 and the division traps on the IOP during warmup, before RE-LAUNCH-016's alloca path is ever reached.
- Locations: iop/usbmass_bd/src/scsi.c:254, iop/usbmass_bd/src/scsi.c:267
- Proposed class: EXTENSION of RE-LAUNCH-016
- Justification: Same unvalidated ingest value and same ingest-whitelist fix site as RE-LAUNCH-016; an additional, earlier crash sink for that mechanism.

### CAND-05
- Label: C-V2-2 — ps2ips send path never validates pkt->length itself (gap in RE-LAUNCH-015's proposed fix)
- Mechanism: `do_send`/`do_sendto` compute `sendlen = MIN(BUFF_SIZE, pkt->length)`; a negative EE `length` yields a negative sendlen passed to `sceSifGetOtherData` and to `send`/`sendto`. RE-LAUNCH-015's fix guards `malign` and the recv length only, so the send-length case survives it.
- Locations: iop/ps2ips_rpc/ps2ips.c:343, iop/ps2ips_rpc/ps2ips.c:376
- Proposed class: EXTENSION of RE-LAUNCH-015
- Justification: Same functions and same entry-validation fix site as RE-LAUNCH-015; an additional unvalidated field instance the parent fix must also cover.

### CAND-06
- Label: C-V2-3 — smbd START copies are not guaranteed NUL-terminated
- Mechanism: `strncpy(user, arg, sizeof(user) - 1)` / `strncpy(password, …, sizeof(password) - 1)` into 32-byte statics never forces a NUL, so a source of ≥31 chars leaves byte 31 holding its previous value and the credential strings are terminated only by leftover state; the same pattern recurs for the module args.
- Locations: iop/smbd/smbd.c:285-286, iop/smbd/smbd.c:331-332
- Proposed class: EXTENSION of RE-LAUNCH-020
- Justification: Same START handler and credential-copy fix site as RE-LAUNCH-020; an additional termination defect in the copies that handler already has to fix.

### CAND-07
- Label: CAND-V3-1 — APA chain walk can loop forever, not merely 10,000 times (strengthens RE-LAUNCH-033)
- Mechanism: The walk's only cycle brake, `part_count`, increments only inside `if (flags == 0 && type == 0x1337)` via `apa_part_add`, while `sector = part.next` advances for every valid in-bounds header; a cyclic chain of ordinary (non-0x1337) partition headers therefore spins the do-while indefinitely with no brake at all.
- Locations: iop/hdl_info/apa.c:164-178
- Proposed class: EXTENSION of RE-LAUNCH-033
- Justification: Same walk, same mechanism and same visited-set fix as RE-LAUNCH-033; it extends the parent's capped-cycle scope to the unbounded ordinary-header case (already folded into 033's drift).

### CAND-08
- Label: CAND-V3-2 — VMCMAN mcman_unit2card mask cannot produce slots 2–7
- Mechanism: `mcman_mc_slot = unit & (MCMAN_MAXSLOT - 1)` with `MCMAN_MAXSLOT == 10` (not a power of two) yields only {0, 1, 8, 9}; if the iomanX unit number is meant to encode VMC slots 0–9 linearly, slots 2–7 are unreachable and units collide (2→0, 3→1, …).
- Locations: iop/vmcman/src/mcdev.c:223, iop/vmcman/src/mcman-internal.h:106
- Proposed class: REJECTED
- Justification: Tentative as filed — whether the encoding is linear or deliberately sparse depends on the EE-side unit-assignment convention, which the verifier could not settle, so no defect is established.

### CAND-09
- Label: CAND-V3-3 — mmce_cmd_set_gameid dereferences a possibly-NULL devctl arg
- Mechanism: The `MMCE_CMD_SET_GAMEID` devctl case passes `arg` unchecked and `mmce_cmd_set_gameid` immediately calls `strlen(ptr)` on it, so a NULL arg dereferences; the neighbouring `MMCE_CMD_SET_CARD_CHANNEL` case explicitly guards `arg == NULL`, showing the guard was considered for one case and missed here.
- Locations: iop/mmceman/src/mmce_fs.c:1364-1366, iop/mmceman/src/mmce_cmds.c:230
- Proposed class: EXTENSION of RE-LAUNCH-026
- Justification: Same function and same entry-validation fix site as RE-LAUNCH-026 (a NULL check belongs in the same top-of-function guard); an additional missing-validation instance there, distinct only in trigger.

### CAND-10
- Label: CANDIDATE V4-1 — ExecuteBrowserFrom has no callers
- Mechanism: `ExecuteBrowserFrom` opens the classic FileBrowser via `getFilePath` + `executeBrowserSelection`, but a tree-wide grep finds no call site in src/, include/, tests/, or docs/ — a public, caller-less entry into the classic browser surface covered by neither RE-LAUNCH-043 nor RE-LAUNCH-044 (grep of the redo report for `ExecuteBrowserFrom`: 0 hits).
- Locations: src/main_actions.c:875-886, include/main_actions.h:27
- Proposed class: NEW
- Justification: Distinct caller-less function in a file/mechanism neither 043 (main() loop/main_menu.c) nor 044 (config/info/editor screens) covers.

### CAND-11
- Label: CANDIDATE V4-2 (minor) — tests/test_app_packages.py is invoked twice in one workflow run
- Mechanism: The same suite runs at compile.yml:238 (inside the 211-test step) and again at :448 (packaging step, after the builds), so the workflow's 211 named tests are 211 unique files across 212 invocations — the same hand-maintained-enumeration drift surface as RE-LAUNCH-045.
- Locations: .github/workflows/compile.yml:238, .github/workflows/compile.yml:448
- Proposed class: NEW
- Justification: Duplicate invocation is a distinct defect from RE-LAUNCH-045 (missing suites) and RE-LAUNCH-046 (single-step masking); no redo finding states it.

### CAND-12
- Label: C1 — Orphan export table for the dead driver (companion to RE-LAUNCH-056)
- Mechanism: `iop/mmceman/src/exports.tab` declares an `mmcedrv` export table (`DECLARE_EXPORT(mmcedrv_get_size/read_sector/config_set/read/write/lseek)`) for the uncompiled `mmcedrv.c`; the mmceman target has no `exports_tab` field and no compiled source calls any `mmcedrv_*` symbol, so deleting RE-LAUNCH-056's files without it leaves a third unexplained orphan.
- Locations: iop/mmceman/src/exports.tab
- Proposed class: EXTENSION of RE-LAUNCH-056
- Justification: Same dead-driver mechanism and same deletion as RE-LAUNCH-056; an additional orphaned location that belongs in its fix.

### CAND-13
- Label: C2 — RE-LAUNCH-058's deletion removes a test oracle and breaks a guard test (unmentioned cost)
- Mechanism: `tests/test_app_packages.py`'s harness uses the file-based extractors as the byte-for-byte reference the RAM variants are asserted to agree with, and `tests/test_self_update.py:13-14` asserts `cloudHttpDownloadFd` is present in `include/cloud_http.h` and `src/cloud_http.c`, so RE-LAUNCH-058's deletion fails CI until that guard is also edited.
- Locations: tests/test_app_packages.py, tests/test_self_update.py:13-14, include/cloud_http.h, src/cloud_http.c
- Proposed class: REJECTED
- Justification: Not a defect — it is an implementation cost of RE-LAUNCH-058's fix (two test edits the fix understates); the verifier confirms no production caller exists, so nothing here stands as its own finding.

### CAND-14
- Label: C3 — RE-LAUNCH-054 scope extension (same mechanism, listed for the fixer's checklist)
- Mechanism: Beyond RE-LAUNCH-054's drift items, `capture-manifest.json` records `bytes` + `sha256` per asset, so deleting the GIFs without regenerating the manifest leaves it internally inconsistent, and the README embed needs a replacement illustration decision, not just link removal.
- Locations: docs-site/static/assets/ui/capture-manifest.json (application-views ~:255, theme-highlights ~:460), README.md:68, docs-site/content/index.html:14
- Proposed class: EXTENSION of RE-LAUNCH-054
- Justification: Same GIF-deletion mechanism as RE-LAUNCH-054; additional locations (manifest entries and a fourth link mention) its fix must include.

### CAND-15
- Label: V6-C1 (from RE-LAUNCH-068): src/filer.c (2,556 lines) is a second omission from architecture-maintenance.md's largest-files list
- Mechanism: `src/filer.c` at 2,556 lines exceeds both listed tail files (`src/filer_browser.c` 2,528; `src/main_exploit_installer.c` 2,519), is hand-written, and is absent from the largest-files sentence, so any fix for RE-LAUNCH-068 that adds only `dash_card_tool.c` leaves the sentence still wrong; the corrected five are dash_settings / dash_browser / init / dash_card_tool / filer.
- Locations: docs/architecture-maintenance.md:89-91, src/filer.c
- Proposed class: EXTENSION of RE-LAUNCH-068
- Justification: Same list, same omission mechanism and same fix sentence as RE-LAUNCH-068; a second omitted instance.

### CAND-16
- Label: V6-C2 (pair with RE-LAUNCH-070): the "Sony ADP" provenance wording also lives in code at src/embed_blobs.S:81
- Mechanism: The comment at `src/embed_blobs.S:81` — "Dashboard UI sounds (Sony ADP, played through the SDK audio driver via audsrv-compat)" over the classic-set embeds — is the likely source of dashboard.md:372's sentence; RE-LAUNCH-070's doc-only fix leaves the same unrecorded-provenance claim alive in the build file.
- Locations: src/embed_blobs.S:81, src/embed_blobs.S:82-88
- Proposed class: EXTENSION of RE-LAUNCH-070
- Justification: Same false-provenance claim and same one-line correction as RE-LAUNCH-070; an additional location in code.

### CAND-17
- Label: CAND-V7-1 — Two divergent Python ADPCM decoders, one of which the finding (077) missed
- Mechanism: `scripts/gen_sfx.py`'s `decode()` (the reference decoder used by --check and by `tests/test_sfx_ps2.py`) uses float arithmetic, while `tests/test_transition_latency.py` reimplements the same decode in fixed-point integer arithmetic instead of importing it; coefficients agree but rounding does not, so the two host decoders can disagree on edge samples.
- Locations: scripts/gen_sfx.py (decode()), tests/test_transition_latency.py:9-21, tests/test_sfx_ps2.py:25
- Proposed class: NEW
- Justification: A host-side divergence between two in-repo decoders is a distinct mechanism from RE-LAUNCH-077 (Python decoder vs the absent shipped decoder); no redo finding covers it.

### CAND-18
- Label: CAND-V7-2 — The Misc enumeration has at least five hand-written sites, not three (extends 079)
- Mechanism: Beyond init/load/lang-rebuild, `src/lang.c:382-411` hand-writes 15 per-entry `isMiscLaunchNameAlias` branches with per-entry `LK_Path` sprintfs, and the config saver hand-lists all 15 `Misc_*` names again in its format string and argument list, so adding/renaming a Misc entry touches ≥5 sites in 2 files.
- Locations: src/lang.c:382-411, src/config.c:566-597
- Proposed class: EXTENSION of RE-LAUNCH-079
- Justification: Same Misc enumeration/lockstep mechanism as RE-LAUNCH-079; two additional hand-written instances (enumeration, not construction) beyond its three rebuild blocks.

### CAND-19
- Label: CAND-V7-3 — The ps2ips sync assertion that 073 proposes copying does not cover headers/cflags (extends 073)
- Mechanism: Even for `ps2ips_rpc`, the module with a sync test, a drift confined to `headers:`, `cflags:` or `include_dirs:` in either `ps2.yaml` copy would pass all current tests — `check_build_config()` plus the isolated-file asserts check type, sources, imports_lst, defines and the workflow line only — so the drift class RE-LAUNCH-073 names is only partially closed for the module it holds up as the pattern.
- Locations: tests/test_ps2ips_rpc.py (check_build_config(); isolated-file asserts :44-49), iop/ps2ips_rpc/ps2.yaml, ps2.yaml (root)
- Proposed class: EXTENSION of RE-LAUNCH-073
- Justification: Same dual-definition drift mechanism and same sync-assertion fix as RE-LAUNCH-073; it extends the parent's scope to the fields its proposed pattern leaves unchecked.

### CAND-20
- Label: CAND-V8-1 — Second (worse) strlen-in-loop site, refuting RE-LAUNCH-086's uniqueness claim
- Mechanism: `ipStringToOctet` re-evaluates `strlen(ip)` in the loop condition and again in the body (`i == strlen(ip)`) — two strlen recomputations per character, the same O(n²) mechanism and same one-line hoist fix as RE-LAUNCH-086; RE-LAUNCH-081 cites this function for IPv4-validation semantics only.
- Locations: src/config_network.c:100-101
- Proposed class: EXTENSION of RE-LAUNCH-086
- Justification: Same strlen-in-loop mechanism and fix as RE-LAUNCH-086; an additional, worse instance that also supplies 086's narrowed scope.

### CAND-21
- Label: CAND-V8-2 — Third commented WriteFont_C artefact not cited by RE-LAUNCH-088
- Mechanism: Two commented lines reference the dead font-dump utility ("a patch to save font, if/when needed (needs patch in draw.c too)" plus a commented `WriteFont_C(...)` call); RE-LAUNCH-088's deletion fix should remove them too, or the tree keeps a dangling reference to the deleted utility.
- Locations: src/main_info_screens.c:302-303
- Proposed class: EXTENSION of RE-LAUNCH-088
- Justification: Same dead WriteFont_C artefact and same deletion as RE-LAUNCH-088; an additional location its fix must include.

### CAND-22
- Label: CAND-V8-3 — Same overwritten-tid cleanup flaw as RE-LAUNCH-091's defective fix, live in usb_mass
- Mechanism: `ret = StartThread(ret, 0)` overwrites the created thread id with StartThread's return, so on failure `DeleteThread(ret)` passes the error code, not the tid — the apparent cleanup does not free the thread and the leak it claims to handle persists, the same pattern RE-LAUNCH-091's prescribed fix gets wrong (correct pattern in-tree at ps2ftpd FtpControl.c:129-131 and udpfs_smap smap.c:489-491).
- Locations: iop/usbmass_bd/src/usb_mass.c:887-897 (overwrite :893, DeleteThread :896)
- Proposed class: EXTENSION of RE-LAUNCH-091
- Justification: Same overwritten-tid / failed-start thread-leak mechanism as RE-LAUNCH-091, live in a second module (grep of the redo report for `usb_mass`: 0 hits as a finding location); an additional instance for the same preserve-the-tid fix.
