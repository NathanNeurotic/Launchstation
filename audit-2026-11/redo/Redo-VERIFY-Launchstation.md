# Adversarial Verification — Launchstation Redo Audit (RE-LAUNCH-001…092)

- **Subject:** `~/workspace/Sable-audit-redo/Redo-Launchstation.md` (92 findings).
- **Pin:** `~/workspace/audit-clones-2026-11-13/Launchstation` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` — HEAD verified before and after; `git status` clean (0 entries) at the end. The clone was read-only throughout; all executions ran in `/tmp` copies.
- **libsmb2:** reconstructed by the coordinator at `/tmp/launchverify/libsmb2` — upstream `a52c1571a1ff223bde5bb3df9fec7af5ce3855d4` (fetched per the repo's own `scripts/fetch_libsmb2.sh`) with the repo's two patches applied (`iop/smbd/libsmb2-session-key-leak.patch`, `libsmb2-raw-query-info.patch`; resulting diff touches exactly `lib/libsmb2.c` and `lib/smb2-cmd-query-info.c`). All `libsmb2/…` citations were checked against this tree; repo glue/config against the clone's `iop/smbd/`.
- **Method (refute-first):** eight chunk verifiers (001–012, 013–024, 025–036, 037–048, 049–060, 061–072, 073–084, 085–092) independently opened every cited file at the pin and re-derived each mechanism from scratch: citations checked line-by-line, defeating guards/callers/type widths/build facts actively hunted before acceptance, proposed fixes checked for sanity and LESS-NOT-MORE compliance, and bodies cross-read for mechanism duplicates. The coordinator read all 92 finding bodies, ran the RE-LAUNCH-009 suite personally (below), and synthesised the duplicate census and candidate index. Nothing under `~/workspace/Sable-audit/` was read at any point.

## Closing tally (92 findings, counted from the eight section files)

| Verdict | Count | IDs |
|---|---|---|
| VERIFIED | 48 | 001, 002, 003, 005, 006, 008, 009, 010, 011, 012, 014, 015, 016, 017, 018, 024, 025, 028, 032, 034, 035, 037, 039, 041, 042, 045, 047, 048, 050, 053, 055, 056, 057, 058, 061, 062, 063, 065, 066, 067, 070, 074, 075, 081, 084, 085, 087, 088 |
| VERIFIED-DRIFT | 33 | 004, 013, 019, 021, 022, 023, 026, 027, 029, 030, 031, 033, 038, 040, 043, 044, 046, 049, 051, 052, 054, 059, 060, 068, 069, 071, 073, 076, 079, 080, 083, 089, 091 |
| WEAKENED | 10 | 020, 036, 064, 072, 077, 078, 082, 086, 090, 092 |
| REFUTED | 1 | 007 |
| BLOCKED-ENV | 0 | — |

The drifts are overwhelmingly citation-level (wrong line ranges, off-by-a-few counts) with mechanisms intact. Four drifts run in the finding's *favour* — the truth is worse than filed: **033** (a cycle of ordinary APA headers loops unbounded, not 10,000-capped), **040** (`lock_cmd` doesn't even validate the handle), **046** (009's suite sits 21st of 211 in the masking step, not mid-list), **052** (10 full-MAX_ENTRY FILEINFO allocations in `src/*.c`, not 4).

## REFUTED — RE-LAUNCH-007 (full reasoning in Chunk V1)

The save-text builder's allocation is `sizeof(SETTING)` 28,248 + `sizeof(PathPad)` 30,750 + 8,192 = **67,190 B**; the maximum output with every field full-but-valid, summed line-by-line over the exact format strings, is **≈61,038 B** — slack ≈6,152 B. The claimed "≈48 KB of path text" is already contained inside the allocation. The unbounded-`sprintf` chain's safety does rest on NUL-termination invariants that RE-LAUNCH-006's corruption could destroy, but that is a downstream consequence of 006, not the independent overrun 007 claims. As filed: refuted.

## WEAKENED index (true scope; full reasoning in the chunk sections)

- **020** — past-length credential parse is real, but the "UI and wire disagree indefinitely" half is defeated in-tree (`init.c:2809-2810` guards the login update; the status screen shows the running login), and the proposed `-EBUSY` fix would break `startSmbServer`'s idempotent-start flow.
- **036** — buffered-path parser bugs all real, but the "streaming path is stricter" framing fails: streaming uses the same `atoi` (:1123) and the identical case-sensitive chunked check (:1139-1141). There is no stricter in-tree pattern to mirror.
- **064** — `smb.html:86` is a provenance sentence about upstream work (where libsmb2 genuinely was the driver), and libsmb2 *is* a shipped driver — for the IOP SMB server, not the smb: client. The one-word swap would falsify the true half; the fix is a reworded sentence.
- **072** — unwired and unreferenced, verified; but "cannot run unattended" overstates — run.sh's 3-arg mode is fully non-interactive against a pre-existing export. True scope: no unattended self-provisioning path, and nothing invokes it.
- **077** — all facts true, but no defect, no fix, risk self-rated INFERRED: a caveat, not a countable finding.
- **078** — three-copy retry duplication verified, but the fix cannot replace the MX4SIO copy as stated (the helper takes one path; MX4SIO probes four spellings with separate readiness flags). ATA replacement is only near-exact.
- **082** — BCD pairs are byte-identical duplicates, but the proposed deletion breaks non-Ethernet builds: the SNTP copies live inside `#ifdef ETH` and the manual copies deliberately after `#endif`; the epochs also differ (1970 vs 2000).
- **086** — the O(n²) `strlen` loop is real, but "only such site in the core" is false: `src/config_network.c:100` is a second, worse instance (and `dash_card_tool.c:2452` a literal third hit).
- **090** — both dev aids are documented intentional tooling (`docs/dashboard.md:573-576`, `docs/themes.md:436`); the finding's own minimum alternative (record usage next to the flag) is already satisfied.
- **092** — all facts verified, but buffer sharing is safe by the server's single-threaded `select()` design, and with no defect/trigger/fix by its own admission it is an observation, not a finding.

## Fixes that are defective or unsound as prescribed (mechanism stands — do not apply as written)

- **013** — bound "derived from clusters_per_card" is unsound (that field is itself card-controlled); correct bound is the constant `ifc_index >= 32`.
- **020** — `-EBUSY` on START-while-running breaks idempotent start (see WEAKENED).
- **036** — "mirror the streaming path" — no stricter pattern exists to mirror (see WEAKENED).
- **078** — helper substitution cannot express the MX4SIO branch (see WEAKENED).
- **080** — "build the context once in `main()`" is impossible as written (`Execute()` sits outside `main()`); sane form is a static builder helper. (Title/counts also drift: 2 sites, 10 explicit assignments, ~50 lines apart.)
- **082** — deletion breaks non-ETH builds (see WEAKENED).
- **091** — `DeleteThread(id)` as written deletes the wrong value: `StartThread`'s return overwrites the tid at `smbd.c:262`, so the literal fix leaves the leak in place. Correct fix preserves the tid (cf. `ps2ftpd/src/FtpControl.c:129-131`).

## Coordinator's RE-LAUNCH-009 rerun (personal, not delegated)

Copied the tree (excluding `.git`) to `/tmp/launchverify/repo` and ran `python3 tests/test_copy_psu_listing.py` three times: **3/3 deterministic failures** — the embedded C harness dies with `SIGABRT` (signal 6) on every run, Python exits 1 via `CalledProcessError`. CI wiring confirmed at `.github/workflows/compile.yml:71`. Consistent in kind with the redo's claimed 4/4; combined evidence is 7/7 deterministic failures across two independent /tmp copies at the pin. Whether the current GitHub CI run is red remains INFERRED (not checked), exactly as the finding labels it.

## Duplicate census (coordinator synthesis of all chunk flags; all 92 bodies read)

**No mechanism-level duplicates exist in the corpus.** Every flagged pair is a distinct mechanism with a distinct fix site; the closest approaches:

- **003 ↔ 024** — same root-cause family (unvalidated server offsets in SMB1 reply parsers), different functions/sinks; 024's body already cross-notes 003.
- **002 ↔ 013 ↔ 035** — one module (vmcman), three different sinks: superblock geometry overwrite / FAT-chain `ifc_list` indexing / VMC slot validation.
- **006 ↔ 079** — same `Misc_*` field family; 006 is the loader's unbounded copies, 079 the triplicated construction. Distinct mechanisms, cross-referenced by 079.
- **018 ↔ 087** — intersecting only: they share one line (`main_startup.c:159`, the VMODE `strncat`). 087's deletion would shrink 018 from four overflow sites to three; the overflow mechanism and the dead-state mechanism are independent.
- **039 ↔ 040 ↔ 041** — one family (smbd success-without-effect), three distinct commands, mechanisms, and fixes.
- **043 ↔ 044** — complementary (dead loop vs shadowed screens), not duplicates. **045 ↔ 046 ↔ 068** — same workflow/doc, three distinct claims; 068's CI-coverage limb was already merged into 045 by the redo itself.
- Remaining flagged adjacencies, all distinct: 001↔021/022 (entropy family), 004↔038, 010↔011 (complementary by design), 010↔038, 008↔029–041, 009↔045/046, 014↔025, 017↔027/028, 019↔031/032, 023↔026/034, 049↔060, 052↔053, 056/057/058 (disjoint dead-code sets), 061↔062, 061↔070, 066/067/068/069 (doc-drift class), 072/075/076 (test-coverage family), 073↔074, 078↔085 (busy-wait family), 091↔020 (same file, different mechanism).

## Candidates index (new defects noticed while verifying — kept separate, none folded into a verdict)

22 candidates, detailed in the chunk sections: V1 CAND-1 (xparam `argc == 3` gap), CAND-2 (save-builder suffix read past a short field — the only route that could break the invariant refuting 007), CAND-3 (NetShareEnum `strncpy` termination, unsettleable in-clone); V2 C-V2-1 (usbmass_bd warmup divide-by-zero — an earlier crash sink than 016's alloca, same ingest value), C-V2-2 (ps2ips send path never validates `pkt->length` itself), C-V2-3 (smbd START `strncpy` never forces NUL); V3 CAND-V3-1 (APA unbounded loop — folded above as 033's drift), CAND-V3-2 (vmcman unit mask yields only slots {0,1,8,9}), CAND-V3-3 (mmceman SET_GAMEID strlen of possibly-NULL arg); V4 V4-1 (`ExecuteBrowserFrom` has zero callers — a caller-less classic-browser entry covered by neither 043 nor 044), V4-2 (`test_app_packages.py` invoked twice in compile.yml); V5 C1 (mmcedrv `exports.tab` orphan companion to 056), C2 (058's deletion breaks CI until two tests are edited), C3 (054's manifest/README scope extension); V6 V6-C1 (filer.c also missing from the largest-files list), V6-C2 ("Sony ADP" wording also in `embed_blobs.S:81` — 070's doc-only fix leaves the source alive); V7 CAND-V7-1 (two divergent Python ADPCM decoders), CAND-V7-2 (Misc enumeration has ≥5 hand-written sites, not 3), CAND-V7-3 (ps2ips sync test leaves headers/cflags drift undetectable — extends 073); V8 CAND-V8-1 (`config_network.c:100-101` strlen-in-loop — the site refuting 086's uniqueness), CAND-V8-2 (third WriteFont_C artefact at `main_info_screens.c:302-303`), CAND-V8-3 (usb_mass has 091's flawed thread pattern live, with the same wrong-value `DeleteThread`).

---

# Chunk sections (verbatim verifier rows)


---

# Adversarial Verification — Launchstation chunk V1 (RE-LAUNCH-001 … RE-LAUNCH-012)

- **Pin:** `~/workspace/audit-clones-2026-11-13/Launchstation` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (HEAD re-checked). Clone read-only throughout.
- **libsmb2 citations** checked against the reconstruction at `/tmp/launchverify/libsmb2` (upstream `a52c1571` + the repo's two patches); repo glue/config in the clone's `iop/smbd/`. Build wiring confirmed in `ps2.yaml` (smbd target: `defines: [_IOP, … HAVE_CONFIG_H]`, `cflags: [-include, config.h, …]`, sources include `lib/init.c`, `lib/compat.c`, `lib/ntlmssp.c`, `lib/smb3-seal.c`).
- **Method:** refute-first — every cited file opened at the pin, mechanism re-derived, callers/greps/type widths/bounds hunted for a defeating guard before acceptance. Nothing under `~/workspace/Sable-audit/` was read.
- **Counts:** VERIFIED 10 · VERIFIED-DRIFT 1 (004) · REFUTED 1 (007) · WEAKENED 0 · BLOCKED-ENV 0.

## Verdict table

| ID | Verdict | Evidence (one line for VERIFIED; full reasoning below for all others) |
|---|---|---|
| RE-LAUNCH-001 | VERIFIED | Checked `iop/smbd/config.h:1-16` (only `NEED_RANDOM`/`NEED_SRANDOM`/`NEED_GETPID`, no `HAVE_ARC4RANDOM_BUF`/`HAVE_GETRANDOM`/`HAVE_DEV_URANDOM`), `lib/init.c:342-411` (fallback `p[i] = random() & 0xff` at :407), seed at `lib/init.c:427` (`srandom(time(NULL) ^ getpid() ^ (uintptr_t)smb2)`), `lib/compat.c:509-532` (IOP branch :515-516 `next = next * 1103515245 + 12345`, returns `(next/65536) % 32768` = 15 bits), `getpid_num() 27` at `compat.c:134` under `_IOP`, and all three consumers (`ntlmssp.c:1009` with its own "has to be unpredictable" comment :1003-1008; `init.c:440-443`; `smb3-seal.c:155`). No strong source is compiled in; fix via the existing patch mechanism (two `libsmb2-*.patch` files in `iop/smbd/`) is sane. Minor imprecision, mechanism unchanged: the LCG state `next` is `static unsigned long` (`compat.c:136`, 32-bit on IOP), so "31-bit-state" in the Problem understates the state by one bit — seed entropy, output width (15 bits/call, 8 used) and predictability are as claimed. |
| RE-LAUNCH-002 | VERIFIED | Checked `iop/vmcman/src/main.c:1371` (`mcman_setdevspec` derives `pages_per_cluster = MCMAN_CLUSTERSIZE / pagesize`, :1344-1345), then the 0x150-byte superblock copy over `MCDevInfo` at :1391-1393 with no re-validation after it; `McReadCluster` loop :3766-3772 writes `pages_per_cluster × pagesize` bytes into `mce->cl_data`; cache slots are exactly `MCMAN_CLUSTERSIZE` (1024, `src/mcman-internal.h:110`) apart in the contiguous `mcman_cachebuf` (`main.c:45`, `:3217-3223`). Mount validation `src/mciomanx_backing.c:106-141` checks magic, cardtype, pagesize ∈ {512,528} (:130-134) and the size equation (:116-128) but never bounds `pages_per_cluster` (a `u16`, `mcman-internal.h:302`). Example in the finding (512 × 64 = 32 KB into a 1024-byte slot) reproduces arithmetically. Proposed fix (re-validate `pages_per_cluster * pagesize == MCMAN_CLUSTERSIZE` at the single ingest point) mirrors the driver's own derived invariant — LESS-compliant. |
| RE-LAUNCH-003 | VERIFIED | Checked `iop/smbman/smb.c:736-763`: `ParamOffset` is dereferenced unchecked already at :736 (API-status read) and :740 (`AvailableEntries`, signed `s16buff` read), `DataOffset` used unchecked at :743, the walk at :746-762 does unbounded `strlen(p)`, a server-controlled comment offset at :756, and advances `p` by server string length + padding + 4 with no comparison against the received length `r` (which `GetSMBServerReply` returned at :716 and the function then ignores). Buffer is `SMB_buf.smb` = 4096 + 128 = 4224 B (`smb.h:900-901`, `smb.c:45`). The hardened sibling is real and in-file (`smb.c:1530-1559`, validates ParamOffset/DataOffset/name length against `r`). Callers exactly as cited (`smb_fio.c:1021,1031`). Finding is, if anything, understated: the first OOB access is the :736 ParamOffset deref, before the walk. Fix mirrors the in-file pattern — sane. |
| RE-LAUNCH-004 | VERIFIED-DRIFT | Mechanism verified; two citation ranges are off — corrected lines in the reasoning below. |
| RE-LAUNCH-005 | VERIFIED | Checked `src/self_update.c:217-245` (`overwrite_elf`: same path, `FIO_O_WRONLY\|CREAT\|TRUNC` at :224, header comment :217-219 states "no second file, rename, delete or backup"), :279-287 (a pre-existing `.BAK` is *deleted*; none is created — comment :279 says so explicitly), :349-357 (failure text at :353-355 concedes the ELF "may be damaged"), SHA-256 verification in RAM before any write (:298-303), and no disk fallback when RAM is short (:268-272 returns an error). `CHANGELOG.md:13` indeed claims the old ELF "is kept as `.BAK` only during the swap" and a RAM-short disk fallback — neither exists. `docs-site/content/install.html:31` and `README.md:104-106` state the true no-backup design, as claimed. Fix (staging leaf + rename swap) is the minimum recoverable mechanism; the finding itself flags the rename-semantics hardware risk. |
| RE-LAUNCH-006 | VERIFIED | Checked every cited site in `src/config.c`: `strcpy(setting->LK_Path[i], value)` :962, `sprintf` Misc family :973-1004, eight unbounded `strcpy` path fields :1040-1067 (`usbkbd_file`, `kbdmap_file`, `CNF_Path`, `lang_file`, `font_file`, `popstarter_file`, `ember_dir`, `neutrino_file`); destinations `include/launchelf.h:285-300` (`Misc*` are `char[64]`), :281-307 (path fields `char[MAX_PATH]`, `MAX_PATH = 1025` at :204); `SETTING` is `malloc`'d at `config.c:799`. The parser `get_CNF_string` (`config.c:111-159`) returns the whole rest-of-line as `value` with no length bound, so a downloaded CNF controls the length fully. The two correctly-guarded families exist exactly as cited (SMB credentials :1050-1053 `strlen(value) < sizeof(...)`, `PathPad` :1097-1099 `strncpy` + forced NUL; `LK_Title` :1088-1089 is also bounded). Fix extends the function's own idiom — LESS-compliant. |
| RE-LAUNCH-007 | REFUTED | Full arithmetic in the reasoning below: with every field full-but-valid, maximum output ≈ 61,038 B < allocation 67,190 B (slack ≈ 6,152 B). The claimed worst case does not overrun. |
| RE-LAUNCH-008 | VERIFIED | Checked `iop/smbd/smbd.c:150-160` exactly: both `smbd_pread` and `smbd_pwrite` call `iomanX_lseek(fd, (int)off, FIO_SEEK_SET)` on a `uint64_t off` (signatures `smbd.h:41-42`). Only two callers exist (grep over `iop/smbd/`): `smbd_fs.c:456` (`read_cmd`, function at :438) and `smbd_fs.c:482` (`write_cmd`, function at :466 — the finding's "~:441 / ~:466" is within its stated approximation), both passing the wire `req->offset` with no range check. Offset 2³²+5 casts to 5 and seeks there; bit-31 offsets cast negative. Proposed fix (reject `off > INT32_MAX` in the two wrappers, `-EINVAL`) is two lines at the single choke point — LESS-compliant. |
| RE-LAUNCH-009 | VERIFIED | Per the task's special note I did **not** re-run the suite; the coordinator's /tmp re-run at the pin (3/3 deterministic failures, SIGABRT at the harness assertion) is taken as the execution evidence, matching the report's original "4/4" claim in kind. Citations verified by reading: the failing assertion is exactly at `tests/test_copy_psu_listing.py:557` (cut/move PSU case over UDPFS: `!message[0] && !nclipFiles && !exists("mc0:/SAVEDIR") && udpfs_read_opens==2 && udpfs_last_read == "udpfs:/SAVEDIR.psu"`), and the passing copy variant is at :523-525 as claimed. CI wiring is exact: `.github/workflows/compile.yml:71` runs `python3 tests/test_copy_psu_listing.py`. Production path: the test reads `src/filer_copy.c` at test :27 and brace-extracts its functions into the harness at :285 (docstring :1-2, extraction helper :34-41); nuance — the harness also extracts `subfunc_Paste` from `src/filer_browser.c` (:288) and real code from `src/psu_functions.c`, so "production path under test in `src/filer_copy.c`" is correct as the principal path but not the only source file drawn on. Fix instruction (identify the failing conjunct; do not relax the assertion) is the right, LESS-compliant shape. |
| RE-LAUNCH-010 | VERIFIED | Checked (values not reproduced here, per the original): default user/password pair assigned at `src/config.c:833-834`; both written in clear text to the config file by the save builder at `src/config.c:706-707`; login plumbing `src/init.c:2785-2814` (`startSmbServer`, cited :2783-2816) passes them to the IOP and caches them in `smbd_login`; shares and on-screen print at `src/dash_smbd.c:21-24` — line :23-24 prints **both** user name and password (`"User name: %s    Password: %s"`), shares `\\<ip>\mc0` / `\\<ip>\mc1` at :21, and the only mitigation text is "Use a network you trust" at :28 (cited as :29, one line off — immaterial). The default is additionally published in the repo itself (`CHANGELOG.md:25`). Fix is part deletion (stop printing the password) and part small first-run gate — justified. |
| RE-LAUNCH-011 | VERIFIED | Checked the libsmb2 leg bound at `lib/libsmb2.c:5179-5184` (`++c_data->session_setup_legs > SMB2_MAX_SESSION_SETUP_LEGS`, = 16 at :142) and the reset at :5430-5432 (`if (!more_processing_needed) c_data->session_setup_legs = 0;`). The failed-AUTHENTICATE path sets `more_processing_needed = 0` at :5346 (after replying `SMB2_STATUS_LOGON_FAILURE` at :5340-5342), so each failed attempt resets the counter and attempts per connection are unbounded; new connections are likewise unbounded over time (concurrent clients are capped at `SMBD_MAX_CLIENTS`, `smbd_fs.c:983,1001`, but nothing throttles reconnects). Glue side: `authorize_user` (`smbd_fs.c:252-263`) is a pure credential comparison — grep of `iop/smbd/smbd.c` + `smbd_fs.c` finds no failure counter, delay or lockout anywhere (the only `DelayThread` is the DEVCTL_STOP poll, `smbd.c:281`); fallback login compiled in at `smbd.c:28` as cited (location only). NTLMv2 verification itself was not impugned and I found no flaw in it. Proposed fix (per-run failure counter + growing delay in the glue, no protocol change) is sane; one implementation note — the existing STATUS devctl (`smbd.c:277`) returns a bit-packed int (running/listening/client-count), so surfacing a fail count through it needs an encoding decision, a detail the fix glosses over. |
| RE-LAUNCH-012 | VERIFIED | Checked `iop/xparam/include/xparam.h:34-36` exactly: `XPARAMS` is `extern const xparam_t XPARAMS[]` (incomplete type in this TU), and the macro is `sizeof(*XPARAMS) / sizeof(XPARAMS[0])` = `sizeof(xparam_t) / sizeof(xparam_t)` = **1 by construction**. Sole consumer confirmed by grep: `iop/xparam/src/xparam.c:55`, the `CheckGameID` loop (:52-63). Database re-counted by script: 292 entries in `iop/xparam/src/database_merged.c` (grep count of entry lines = 292), first entry `PAPX_905.17` at the head of the array — so only that one entry can ever match, and because consecutive same-game entries are handled by the `else if (f) break;` logic at :61, even that game's later parameter rows are unreachable. Fix (define the count in the TU that defines the array) is a one-line class of fix, no new mechanism. |

## Full reasoning — non-VERIFIED verdicts

### RE-LAUNCH-004 — VERIFIED-DRIFT

The mechanism is fully confirmed; only the `dash_main.c` citation range is stale:

- `src/init.c:1176-1183` is exact: `load_ps2ftpd` passes `"-anonymous"` (with an optional `-no-hdd` suffix when `shouldHideHddAtaDevices()` holds).
- `src/remote_control.c:86-88` is exact in substance: `EXECUTAR\t<path>` at :86-88 copies any supplied path into `runpath` and returns 1; `MENU` (:90-93) and `DESLIGAR`/power-off (:94-97) are equally unauthenticated. The command file is `ata0:/PS2-COMANDO.TXT` (:8), consumed one-shot at :79, with a completeness marker (`FIM`) as the only integrity check — no credential, token, or source check anywhere in the file.
- `src/dash_main.c`: the finding cites ":766-790 (poll wrapper gates only on HOME section, worker idle, FTP readiness)". The implementation actually spans :756-785 (`pollRemotePcCommandImpl`: gates at :763 on `boot_phase || dashWorkerIoBusy() || dashIopParked() || !isFtpNetworkReady()`, launch via `dashLaunchApp` at :780), and the **HOME-section gate is in the wrapper at :796-800** (`pollRemotePcCommand` returns unless `section == DASH_SEC_HOME`) — just outside the cited range. Corrected citation: `src/dash_main.c:756-785` (impl + gates) and `:796-800` (HOME wrapper).
- `src/pc_game_receiver.c`: cited :37-76 / :309-353; actual `readRequest` is :35-… (request file `ata0:/PS2-RECEBER.TXT` defined :11, staging dir `ata0:/PS2-RECEBENDO` :13, destinations built as `ata0:/…` at :250,268) and `pcGameReceiverPoll` is :309-340 — inside the cited range. No drift of substance here.
- Defeater hunt: FTP is not on by default — it runs only after the user starts the FTP server screen (`src/dash_ftp.c:70-…`), and the finding's own wording is scoped to "while FTP runs", so this does not weaken it. The `-no-hdd` variant restricts the IOP side on some consoles, but the EE poll/launch chain has no equivalent gate, and the finding already labels the IOP FTP filesystem scope INFERRED. Fix (default-off opt-in gating the two polls) is the smallest chain-breaking change — LESS-compliant.

### RE-LAUNCH-007 — REFUTED

Citations exist and say what is claimed (`src/config.c:544` allocation `malloc(sizeof(SETTING) + sizeof(PathPad) + 8192 + smb_len)`; unbounded `sprintf`/`%n` chain :548-712, including the 17 launch-key lines :550-561 and 30 `PathPad` lines :693-704; bounded `snprintf` helpers do exist in the same file at :38-101). The **mechanism does not follow**, because the allocation is not an under-estimate of the bounded worst case — it literally contains the size of every source array whose contents are printed, plus 8192 bytes for format overhead:

- `sizeof(SETTING)`, computed field-by-field from `include/launchelf.h:279-340` with alignment (u64 `color[8]`): **28,248 B**. It includes `LK_Path[17][1025]` (17,425), the eight 1025-byte path fields (CNF_Path + 7 file fields, 8,200), `Misc[17][64]` (1,088) and `LK_Title[17][72]` (1,224). `sizeof(PathPad)` = 30 × 1025 = 30,750. **Allocation (excluding `smb_len`, which is added exactly) = 28,248 + 30,750 + 8,192 = 67,190 B.**
- Maximum possible output with every field full (each string at its array bound minus NUL, `%d` at the full 11 characters, language/layout names at a generous 20 — actual maxima are 10, `src/lang.c:65-75`), summed line by line over the exact format strings at `config.c:548-712` and `src/gui_colors.c:20-49`: header 17 + LK lines 17,665 + Misc block 1,316 + GUI colours 355 + main block 9,015 + title lines 1,410 + duplicate `PathPad_Lock` 28 + PathPad lines 31,230 + SMB login lines 106 ≈ **61,038 B** (plus the 14-byte `SMB_CNF_SECTION` header at :712, whose block body is covered by `smb_len`). **Slack ≈ 6,152 B.** Total format overhead across all ~130 lines is ≈ 2.6 KB, well inside the 8192 reserve — the code's own comment at :536-537 ("Values are bounded by their source arrays; reserve extra space for key names, numeric fields and line endings") states exactly this invariant, and it holds.
- The finding's headline figure — "≈48 KB of path text alone when every field is full" — is `LK_Path` (17 × 1024 ≈ 17.4 KB) + `PathPad` (30 × 1024 ≈ 30.7 KB) ≈ 48 KB, but both arrays are **already counted inside the allocation** (`sizeof(SETTING)` contains the former, `sizeof(PathPad)` is the latter), so that figure cannot exceed the estimate; adding every other printable field still lands ~6 KB under.
- Reachability check for an *unbounded* source that could break the bound: every `%s` argument is either an array field above, `smbSharesBlocks()` (length added exactly as `smb_len`, +14 B header), or a fixed literal returned by `getBuiltinLanguageConfigName` / `getVirtualKeyboardLayoutConfigName`. The `Misc_*` lines print `field + strlen(Misc)` suffixes (:563, :583-597), which can only shorten those outputs. No caller supplies a different `setting` (other callers: `src/dash_settings.c:2372,2397`, same global).
- Residual truth, stated narrowly so it is not lost: the chain is unbounded `sprintf` whose safety rests entirely on the invariant "every printed string is NUL-terminated within its array". If RE-LAUNCH-006's loader overflow (or any other corruption) destroys a field's termination, this builder would run past both the field and the heap block — but that is a downstream consequence of 006's corruption, not an overrun reachable with full-but-valid fields as 007 claims. As an independent finding with the stated worst case, it is refuted; the proposed `snprintf` rewrite is harmless hardening but is not justified as a bug fix under LESS-NOT-MORE, and if applied it should be re-scoped as defence-in-depth for 006, not as this finding.

## Duplicate / overlap check (RE-LAUNCH-001…092, bodies read for all neighbours)

No pair in 001…012 states the same mechanism as another finding. Adjacent pairs, all distinct mechanisms with distinct fix locations:

- **001 ↔ 021 / 022** — entropy family only: 021 is the EE SNTP transmit timestamp, 022 the EE TLS entropy callback; 001 is the IOP libsmb2 LCG. Different code, different fixes.
- **002 ↔ 013 / 035** — all vmcman, different sinks: superblock geometry overwrite (002), FAT-chain `ifc_list` indexing (013), VMC slot validation (035).
- **003 ↔ 024** — same root-cause *family* (unvalidated server `DataOffset` in SMB1 reply parsers); 024's body already cross-notes this. Different function, different sink, different fix site — correctly kept separate.
- **004 ↔ 038** — 038 cites 004 as an exposure amplifier for clear-text credential files; mechanisms (anonymous launch inbox vs credential storage) are distinct. **010 ↔ 038** likewise distinct: 010 is the SMB *server* login (default + screen print); 038 covers the Google token, OAuth client material and SMB *share* (client) passwords.
- **006 ↔ 007 / 018 / 027 / 028 / 079** — 006 is the CNF loader's unbounded copies. 007 was its alleged save-side counterpart (refuted above, and its Problem leans on 006's loader for reachability). 018 (SYSTEM.CNF `strncat`), 027 (dialog helpers), 028 (editor save-as) are different parsers/sinks; 079 is the Misc-string triplication (duplication), whose body notes it shares 006's field family.
- **008 ↔ 029 / 030 / 031 / 032 / 039–041** — all smbd, each a different wire-semantics defect (offset truncation vs WRITE length vs CREATE leaf limit vs file-id width vs identity vs success-without-effect classes).
- **009 ↔ 045 / 046** — 009 is a CI-wired failing suite; 045 is suites *absent* from CI, 046 is the single-step masking that would hide 009's failure downstream. Related, not duplicates.
- **010 ↔ 011** — complementary by design (011's title: "against that same default login"): default+disclosure vs absence of throttling. Different fixes.
- **012** — no other finding touches `xparam` / `GET_XPARAM_DB_SIZE` (grep over the full report confirms a single occurrence).

## CANDIDATES (new defects noticed while verifying — not folded into any verdict)

- **CAND-1 (xparam argc gap, low):** `iop/xparam/src/xparam.c:30-45` — `_start` handles `argc < 3` (game-ID lookup) and `argc > 3` (parameter pairs), so `argc == 3` (an ID plus one dangling argument) silently does nothing at all. May be intentional (a pair needs two arguments), but the ID lookup is also skipped, which looks unintended. Needs an intent check against the module's caller before calling it a defect.
- **CAND-2 (save-builder suffix read past a short field, speculative):** `src/config.c:563,583-597` prints `setting->Misc_* + strlen(setting->Misc)`. If `Misc` is ever longer than a given `Misc_*` field's own string (possible if the fields are edited independently, e.g. by the classic config screen rather than the loader, which always prefixes), the pointer lands past that field's NUL and `sprintf` reads into adjacent — and, since `setting` is `malloc`'d without zeroing (`src/config.c:799`), potentially uninitialised — SETTING bytes. That would break the bounded-output invariant that defeats RE-LAUNCH-007. Heap-content dependent; would need a host harness with independently-set Misc fields to settle. Distinct from 007 as claimed.
- **CAND-3 (NetShareEnum string termination, unverifiable in-clone):** `iop/smbman/smb.c:755-756` uses `strncpy(…, 256)` for `ShareName`/`ShareComment`; `ShareEntry_t` is defined outside this repo (PS2SDK headers), so whether a 256-byte destination can be left unterminated could not be settled from the clone. It sits inside 003's parser, but 003's proposed offset/length validation would not by itself address termination. Flagged for whoever fixes 003.

---

# Adversarial Verification — Chunk V2: RE-LAUNCH-013 … RE-LAUNCH-024

- **Target report:** `~/workspace/Sable-audit-redo/Redo-Launchstation.md`
- **Code pin:** `~/workspace/audit-clones-2026-11-13/Launchstation` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (verified `git rev-parse HEAD`; clone read-only throughout; libsmb2 patched tree at `/tmp/launchverify/libsmb2` used for RE-LAUNCH-019)
- **Method:** refute-first. Every cited file opened at the pin; mechanism re-derived from scratch; all callers / guards / type widths / upstream bounds hunted before accepting.
- **Counts:** VERIFIED 6 · VERIFIED-DRIFT 5 · WEAKENED 1 · REFUTED 0 · BLOCKED-ENV 0

## Summary table

| ID | Verdict | One-line evidence |
|---|---|---|
| RE-LAUNCH-013 | VERIFIED-DRIFT | Unbounded `ifc_list[ifc_index]` is real (`main.c:3886,3917,1734`), but the cited walk, formula, lines, header path and mount check are all wrong — see full reasoning |
| RE-LAUNCH-014 | VERIFIED | `strcpy` at `hdl.c:48,49` and `hdd.c:180` exact; dest sizes `hdl.h:9-10` exact |
| RE-LAUNCH-015 | VERIFIED | `malign` is `s32` (`ps2ip_rpc.h:99`), unchecked at `ps2ips.c:337,370`; setsockopt guard precedent exact at `:526-530` |
| RE-LAUNCH-016 | VERIFIED | Only `==0` rejected at `scsi.c:246` (RC16 `!=0` at `:259`); `alloca(sectorSize+4)` at `:321`, `memcpy` `:334`, products `:156,174` — no other clamp exists in the module |
| RE-LAUNCH-017 | VERIFIED | All five sites exact: `filer_copy.c:849,873,1117-1120`, `filer_actions.c:1980` after checked `snprintf` `:1972-1973`, `filer.c:494-495` |
| RE-LAUNCH-018 | VERIFIED | `strncat` appends at `main_startup.c:153,155,157,159`; dests `main.c:67-70`; value is an unbounded file line (`config.c:148-151`) |
| RE-LAUNCH-019 | VERIFIED-DRIFT | Logoff/tree-disconnect are no-ops and handles key on the connection only, but the three location labels are scrambled and there is no `conn->generation` — see full reasoning |
| RE-LAUNCH-020 | WEAKENED | Past-length password parse verified exactly (`smbd.c:283-288`); the claimed EE/UI divergence is defeated by the `init.c:2809` guard — see full reasoning |
| RE-LAUNCH-021 | VERIFIED-DRIFT | Predictable nonce exact at `dash_clock_sync.c:149-152`; cited authentication/era line ranges point at the wrong code — see full reasoning |
| RE-LAUNCH-022 | VERIFIED-DRIFT | `getrandom` failure swallowed at `cloud_http.c:127-129`, success return at `:154` (not `:156-157`); "unconditional" overstates two non-entropy failure returns |
| RE-LAUNCH-023 | VERIFIED-DRIFT | `strcpy` exact at `mmce_cmds.c:216`, caller exact at `mmce_fs.c:1360-1362`; guard header is `include/mmce_restore_guard.h`, not `src/` |
| RE-LAUNCH-024 | VERIFIED | Unchecked `DataOffset` casts at `smb.c:877,890`; received length `r` never applied; hardened sibling pattern exact at `smb.c:1530-1559` |

---

## Full reasoning — non-VERIFIED verdicts

### RE-LAUNCH-013 — VERIFIED-DRIFT (mechanism real; nearly every citation wrong; fix as worded insufficient)

What the code actually shows at the pin:

- There is **no `McGetDirectory` walk** and no `cluster >> 5` anywhere in this mechanism. The cited range `iop/vmcman/src/main.c:3872-3932` is `McSetFATentry` (3872) and `McGetFATentry` (3905). Both compute `indirect_index = fat_index / FATentries_per_cluster`, then `ifc_index = indirect_index / FATentries_per_cluster` (`:3880-3884`, `:3911-3915`), with `FATentries_per_cluster = 256` (`mcman_setdevspec`, `main.c:1349`; constant `MCMAN_CLUSTERFATENTRIES 256`). So `ifc_index = fat_index / 65536` — OOB requires `fat_index ≥ 2,097,152`, not the small values the `>> 5` formula implies.
- The `ifc_list` accesses are at `main.c:3886` (McSetFATentry) and `main.c:3917` (McGetFATentry) — **not** `:3903` (blank/comment line between the two functions) or `:3924` (an error-return check). A third identical access exists at `main.c:1734` (`mcman_findfree2`). `ifc_list` itself is only *read* at these sites; the write primitive is indirect — the OOB value is used as a cluster number for `McReadCluster` (which itself has no cluster bound check, `main.c:3721-3777`), and McSetFATentry then writes `fc->entry[fat_offset] = fat_entry` (`:3898`) into whatever cluster that names.
- List size citation is wrong in path and line: the array is `u32 ifc_list[32]` at `iop/vmcman/src/mcman-internal.h:312` (under `src/`, not `include/`; line 99 of that header is inside `MCCacheDir`). Size 32 itself is correct.
- The mount-side citation is wrong: `iop/vmcman/src/mciomanx_backing.c:138` is `cardinfo->blocksize = superblock.blocksize;` — an assignment, not a check. The actual VMC mount validation is at `:106-134` (magic, `cardtype`, file-size equation, `pagesize ∈ {512,528}`) and contains **no** cluster/FAT bound at all. The physical-card path is worse than the finding states: `mcman_setdevinfos` (`main.c:1391-1393`) copies the first `0x150` bytes of the card superblock over `MCDevInfo` — a range that covers `clusters_per_card` (offset 48), `alloc_offset`, `backup_block1/2` and `ifc_list` itself (offset 80) — after checking only magic and version (`:1383-1389`). `cluster_size`/`FATentries_per_cluster`/`clusters_per_block` sit past offset 336 and survive, which is what keeps the `/65536` divisor sane.
- The worked example is wrong: `backup_block2 = 0xFFFF` does not feed `ifc_index` on any path found. The real trigger is the FAT chain itself: `McReadDirEntry` (`main.c:3813-3828`) passes the card-supplied chain value `clust` (masked only with `~0x80000000`, checked only against `0xffffffff`) straight into `McGetFATentry` as `fat_index`. A FAT entry of e.g. `0x7FFFFFFF` yields `ifc_index ≈ 32767`, reading ~128 KB past the 32-entry table. `mcman_findfree2`'s own access (`:1734`) is loop-bounded by `max_allocatable_clusters` and is only reachable OOB if that derived bound is itself inflated via the card-controlled `clusters_per_card`.
- **Duplicates:** distinct from RE-LAUNCH-002. 002's sink is `McReadCluster` overrunning the 1024-byte cache entries from superblock *geometry*; 013's sink is the `ifc_list` index from a *FAT-chain value*. They share the same unvalidated superblock ingest (`main.c:1391-1393`) but are different mechanisms with different fixes — correctly separate findings.
- **Fix assessment:** the direction (bound the walk) is right, but the proposed bound — "the table size derived from `clusters_per_card`" — is not LESS-NOT-MORE sound: the table size is the constant 32, and `clusters_per_card` is itself card-controlled and unvalidated, so deriving a safety bound from it proves nothing. Correct minimal fix: reject `ifc_index >= 32` (equivalently `fat_index >= max_allocatable_clusters`) at the top of `McGetFATentry`/`McSetFATentry`, covering all three call sites at once.

### RE-LAUNCH-019 — VERIFIED-DRIFT (core mechanism verified; location labels scrambled)

- There is no `smbd_logoff` in the glue. The three cited ranges are each attached to the wrong claim:
  - `iop/smbd/smbd_fs.c:159-175` is `lookup()` — the handle validator — not a logoff function.
  - `iop/smbd/smbd_fs.c:284-289` is `logoff_cmd` (a no-op returning 0) — the finding labels this range as tree disconnect.
  - `iop/smbd/smbd_fs.c:323-330` is `tree_disconnect_cmd` (also a no-op returning 0, `:323-329`) — the finding labels this range as "table indexed by `conn->generation` only". No symbol `conn->generation` exists in the glue.
- The substance underneath is nevertheless correct, re-derived as follows: `struct handle` (`smbd_fs.c:28-38`) stores `owner` (a `struct smb2_context *`, i.e. the **connection**), a `gen`, path and flags — no session or user field. `lookup()` (`:170-173`) authorises a file-id solely by `handles[i].owner == smb2` plus the 16-bit `gen` in file-id bytes 2–3. Neither `logoff_cmd` (`:284-289`) nor `tree_disconnect_cmd` (`:323-329`) releases anything. Handles are released only in `destruction_event` (`:272-282`), i.e. when the connection itself is destroyed. A second session setup on the same connection re-runs `authorize_user` (`smbd_fs.c:252-263`), which overwrites the context identity via `smb2_set_user`/`smb2_set_password` (invoked from libsmb2's session-setup processing, `/tmp/launchverify/libsmb2/lib/libsmb2.c:1386` region; the cited `:5400-5455` range is the reply-construction tail, including the `session_established` handler call at `:5454-5455`, not the identity store itself). Open handles therefore survive logoff and tree disconnect and remain usable under whatever identity the connection next authenticates as. One wording correction: handles do not "change owner" — the `owner` pointer is unchanged; the *effective user* behind that pointer changes.
- **Duplicates:** distinct from RE-LAUNCH-031 (file-id width/generation wrap — same table, different defect: validation strength vs lifetime) and RE-LAUNCH-032 (server identity/session-id counters). No overlap with any other finding's mechanism.
- **Fix assessment:** closing the connection's handles in `logoff_cmd` is correct SMB semantics and uses the existing `release()`/`destruction_event` loop — sane. Closing on tree disconnect is weaker: handles in this implementation are connection-scoped, not tree-scoped (the handle does carry a `share` field, so a per-share close is implementable), and the report already flags the interop risk honestly (Risk: Medium, hardware testing needed). The "re-key to session" alternative would be MORE, not less; the close-on-logoff half is the compliant fix.

### RE-LAUNCH-020 — WEAKENED (parse defect verified; the success/divergence half is largely defeated in-tree)

Verified half, exactly as claimed: `iop/smbd/smbd.c:283-288` — the START handler checks only `!running && arglen > 2 && ((char *)arg)[arglen-1] == '\0'` (`:284`), then takes the password from `(char *)arg + strlen(arg) + 1` (`:286`, `strncpy(password, …, sizeof(password)-1)` into the 32-byte static at `smbd.c:28`). Nothing checks that a second NUL-terminated string lies inside `arglen`. A single-string blob (`"ABC\0"`, `arglen 4`) passes both checks and the copy starts one byte past the supplied buffer, reading up to 31 bytes of adjacent IOP memory as the password. The header contract (`include/smbd_devctl.h`) defines only the command numbers; the `"USER\0PASSWORD\0"` layout lives in the `smbd.c:5,283` comments. The in-tree caller builds the blob correctly (`src/init.c:2790-2793`: user + NUL + password + NUL, `len` exact) — so "latent trap for the next caller" is the accurate framing, as the finding itself concedes.

Weakened half: "reports success for credentials it did not take … the EE records the new login as pending while the server keeps the old one, so UI and wire can disagree indefinitely."

- The IOP half is true: `start()` returns 0 when already running (`smbd.c:251-252`), and the handler returns `start()` unconditionally (`:288`).
- The EE consequence is **defeated by the caller**: `src/init.c:2809-2810` copies the blob into `smbd_login` only `if (ret == 0 && !smbd_active)` — with the comment the finding cites, "a running server keeps its login". The status screen displays `smbd_login`, i.e. the login the server actually took (`src/dash_smbd.c:23-24`). So the in-tree UI and the wire do **not** diverge: both keep showing/using the running login. What can differ is the *Settings* value (the user's unapplied edit) versus the running server — ordinary pending-until-restart semantics, documented in the code, not a silent credential mismatch.
- Consequently the proposed `-EBUSY` return is **not** LESS-NOT-MORE compliant as stated: `startSmbServer()` treats `ret == 0` on an already-running server as success and sets `smbd_active = 1` (`init.c:2811-2812`); returning `-EBUSY` from START-while-running would make a routine re-start report failure. The parse validation (first half of the fix) stands on its own and is the real finding.
- True scope: a latent IOP-side over-read on a malformed devctl blob from a future/non-stock EE caller, plus a documented, EE-compensated no-op on START-while-running. Benefit is lower than the stated Medium for the second half; the first half alone justifies the finding.

### RE-LAUNCH-021 — VERIFIED-DRIFT (predictability verified; two location ranges point at the wrong code)

- Verified exactly: `src/dash_clock_sync.c:149-152` — `nonce_hi = (u32)Timer() ^ 0x574C4554U; nonce_lo = nonce_hi * 1664525U + 1013904223U;` written as the transmit timestamp at `:151-152`. The whole 64-bit value is one Numerical-Recipes LCG step from a boot-relative millisecond timer. `dashClockSyncInternet` at `:315-361` writes the accepted result to the RTC via `writePs2Clock` (`:352`). The TLS-side date dependency text is at `src/cloud_http.c:341-344` (inside the cited `:340-352`).
- Drift: the reply-authentication checks are **not** at `:207-215`. They are at `:187-196` (mode check `reply[0] & 7`, stratum `reply[1] > 15` at `:187`; origin-timestamp `memcmp(reply+24, req+40, 8)` at `:190-191`; stratum-0 refusal `:193-194`; LI unsync `:195-196`), over a **connected** UDP socket (`ps2ipc_connect`, `:165` — an attacker must spoof the pool server's source address, which the finding's INFERRED caveat covers). Lines `:207-215` are the era conversion and sample fill. The era window is **not** at `:229-233` (that is `civilFromDays`); it is at `:201-209` (era-1 adjustment, `SNTP_EPOCH_DELTA` floor, `unix_sec < UNIX_2020 || >= UNIX_2100` rejection at `:208`).
- No defeating guard found: nothing else authenticates the reply, and no other timestamp source is mixed in. The proposed fix (draw the timestamp from the TLS entropy material) is sane and small. Exploitability remains correctly labelled INFERRED in the original (single guess per user-initiated sync, racing the real reply).

### RE-LAUNCH-022 — VERIFIED-DRIFT (contract defect verified; "unconditional" overstates, return line off)

- Verified: `src/cloud_http.c:104-155` is `mbedtls_hardware_poll`. `getrandom` failure is handled by zeroing the field and continuing (`:127-129`) — the one OS source can fail silently. All other inputs are as described (`:131-140`: COP0 tick samples, `clock()`, RTC via `sceCdReadClock`, `Timer()`, `++cloud_entropy_counter`, network ticks), SHA-256 conditioned at `:142-144`. Wiring exact: `scripts/build_cloud_tls.sh:66` sets both `MBEDTLS_NO_PLATFORM_ENTROPY` and `MBEDTLS_ENTROPY_HARDWARE_ALT`.
- Drift/overstatement: the success return is at `cloud_http.c:154` (`*olen = produced; return 0;`), not `:156-157` (blank / next function). And the callback is not literally unconditional: it returns `MBEDTLS_ERR_ENTROPY_SOURCE_FAILED` for NULL arguments (`:109-110`) and on SHA-256 failure (`:143-144`). Neither failure path is entropy-quality-based, so the finding's actual claim — *nothing in the path can ever say "not enough entropy"* — survives intact; only the word "unconditionally" and the line numbers need correcting.
- Fix assessment: returning `SOURCE_FAILED` when no variable source contributed is the right shape and reuses the existing TLS error path; the report's own Risk note (threshold needs hardware validation) is the correct caveat. Seed quality on real hardware remains INFERRED, as labelled.

### RE-LAUNCH-023 — VERIFIED-DRIFT (mechanism and caller exact; one path wrong)

- Verified exactly: `iop/mmceman/src/mmce_cmds.c:195-224` is `mmce_cmd_get_gameid`; `rdbuf` is a fixed 255-byte stack packet (`:200`); on reply-const match it does `strcpy(ptr, &rdbuf[0x4])` at `:215-216` with no NUL check anywhere in `rdbuf[0x4..0xFE]`. A card returning 251 non-NUL bytes makes the copy read past `rdbuf` and write unbounded into the destination. Caller exact: `iop/mmceman/src/mmce_fs.c:1360-1362` passes the devctl `buf` straight through — and the devctl handler (`mmce_fs.c:1313`) never consults `buflen` for this command, so the cmds layer cannot know the destination size; bounding by the packet (the proposed fix) is the only correct layer and is LESS-compliant.
- Drift: the guard header is `iop/mmceman/include/mmce_restore_guard.h` (`mmce_fs_wire_name_ok` at `:12`), not `iop/mmceman/src/mmce_restore_guard.h`. The guard itself and its fs-layer call sites (`mmce_fs.c:241,632,696,…`) are real, so the "fs layer was guarded, cmds layer was not" contrast stands.
- **Duplicates:** distinct from RE-LAUNCH-026 (the opposite-direction `mmce_cmd_set_gameid`, `mmce_cmds.c:226+`, u8 length narrowing — a sibling, correctly separate) and RE-LAUNCH-034 (directory-read filename termination in the fs layer — different function and sink).

---

## One-line confirmations — VERIFIED

- **RE-LAUNCH-014 — VERIFIED:** `hdl.c:48` `strcpy(ginfo->name, buffer+8)` and `:49` `strcpy(ginfo->startup, buffer+0xac)` from a 1024-byte disk buffer into `name[65]`/`startup[13]` (`hdl.h:9-10`), rename `strcpy(game->name, Packet->NewName)` at `hdd.c:180` with the `strcmp` at `:178` (cited `:177`, one-line slip, no mechanism impact), `NewName[64]` at `hdd.h:68`, RPC buffer `int Rpc_Buffer[1024]` = 4 KB at `main.c:23`; bounded-copy fix matches the `hdl.c:46-47` pattern. Distinct from RE-LAUNCH-025 (the `nsub` sum at `hdl.c:43-44` — same function, different mechanism, correctly separate).
- **RE-LAUNCH-015 — VERIFIED:** `malign` is `s32` (`ps2ip_rpc.h:99`, buffer `[64]` at `:101`); `do_send` memcpy at `ps2ips.c:336-337`, `do_sendto` at `:369-370`, neither checks `malign` against 64, `length`, or sign — negative `malign` makes `s_offset > 64` and the `memcpy` length a huge `size_t`, `malign > 64` writes before `lwip_buffer` (1088 bytes, `:51`); negative recv `length` survives `MIN(BUFF_SIZE, …)` at `:149`/`:249` into `recv`/`recvfrom`, and also drives `srest`/`s_offset` out of range at `:136-148`; the `do_setsockopt` guard precedent is exact at `:526-530`. Scope note (not a weakening of the code claim): the in-tree EE client always sends `malign ∈ [0,63]`, `malign ≤ length` (`ee_libs/tcpips/ps2ipc.c:354-364,402-412`), so triggering requires a buggy/non-stock EE RPC client — the IOP-side missing check is exactly as stated. No other finding in 001–092 covers this mechanism (083 is EE-side ABI duplication, a different claim).
- **RE-LAUNCH-016 — VERIFIED:** `bd->sectorSize` is assigned only at `scsi.c:244` (RC10) and `:261` (RC16), rejected only when zero (`:246-249`; RC16 guarded by `!= 0` at `:259`); a tree-wide grep finds no other clamp in `usbmass_bd`. The value sizes `__builtin_alloca(sectorSize + 4)` at `:321` and the `memcpy` at `:334` on the misaligned-write path, and multiplies transfer lengths at `:156`/`:174` (`sectorCount` is `u16`-chunked by `max_sectors = 128`, `usb_mass.c:846`, which bounds the count but not the product's 32-bit overflow with a huge `sectorSize`). Nothing in the stack restricts a USB device to sane block lengths — the device reports a raw `u32`. The proposed ingest whitelist (powers of two in [512,4096]) is a single check at the single ingest point: LESS-compliant and sane. Note the report understates one point: the module thread stack is only 2 KB (`usb_mass.c:884`), so even a legitimate 4 KB-sector device's 4,100-byte alloca is stack-critical if writes run on that thread.
- **RE-LAUNCH-017 — VERIFIED:** all five concatenations confirmed verbatim — `filer_copy.c:849` and `:873` `sprintf` into `char in/out[MAX_PATH]` (declared `:764`, `MAX_PATH = 1025`), repeated at `:1117-1118` with `genLimObjName(out, 0)` only *after* the overflow at `:1119` and `strcat(out, "/")` at `:1120`; `filer_actions.c:1980` `strcat(dir, "/")` immediately after the length-checked `snprintf` at `:1972-1973` (a `dir` of exactly `sizeof(dir)-1` chars passes and the `strcat` writes the NUL one byte past); `filer.c:494-495` `strcpy`/`strcat` into `char dir[MAX_PATH]` (`:481`). Weakest site is `readMC` (overflow needs a caller path longer than `MAX_PATH`, which in-tree callers generally prevent); the copy/delete sites are reachable with deep trees and long names as claimed. Minor citation blemish: the "checked snprintf" sibling at `filer_actions.c:159-161` is truncation-safe `snprintf` + forced NUL (`:162`) rather than length-checked — the `:1972` example is exact. Distinct from RE-LAUNCH-027/028 (same bug class, different files/sinks, correctly separate).
- **RE-LAUNCH-018 — VERIFIED:** `scanSystemCnf` appends with `strncat(dst, value, MAX_PATH-1)` / `…, 9)` at `main_startup.c:153,155,157,159` into `SystemCnf_BOOT/BOOT2[MAX_PATH]`, `SystemCnf_VER[10]`, `SystemCnf_VMODE[10]` (`main.c:67-70`); `readSystemCnf` zeroes once (`:173-176`) then calls the scanner per parsed line (`:180-181`), and `get_CNF_string` imposes no value-length bound (`config.c:146-151`, value runs to end-of-line in the loaded file). Two `VER` lines of 9 chars append 18 + NUL into the 10-byte global; two `BOOT` lines overflow likewise. First-wins bounded copy is smaller than the append form — fix is LESS-compliant. (RE-LAUNCH-087 cross-references the VMODE branch as dead state; it does not restate this mechanism.)
- **RE-LAUNCH-024 — VERIFIED:** `smb_QueryPathInformation` (`smb.c:811`) receives `r` at `:855`, checks only `r <= 0`, header magic and status (`:856-873`), then casts `&SMB_buf.smb.u8buff[QPIRsp->smbTrans.DataOffset]` to `BasicFileInfo_t *` (36 bytes) at `:877` and `StandardFileInfo_t *` (22 bytes) at `:890` with no check against `r` or the 4224-byte buffer (`MAX_SMB_BUF 4096 + MAX_SMB_BUF_HDR 128`, `smb.h:900-901`); `DataOffset` is a server-supplied `u16`. The claimed in-file fix pattern exists verbatim at `smb.c:1530-1559` (FindFirstNext2 validates `ParamOffset`/`DataOffset` against `r`). Correctly separate from RE-LAUNCH-003 — same root cause (unvalidated server offsets), different function (`NetShareEnum` vs `QueryPathInformation`) and sink, as the finding itself states.

---

## CANDIDATES (new defects noticed while verifying — not folded into any verdict)

- **C-V2-1 — `usbmass_bd` warmup divides by zero for `sectorSize > 1,000,000` (same ingest value as RE-LAUNCH-016, different, earlier sink).** `iop/usbmass_bd/src/scsi.c:254` and `:267` compute `bd->sectorCount / ((1000 * 1000) / bd->sectorSize)` in the capacity `M_PRINTF`. For a device reporting `block_length > 1,000,000`, `(1000000 / sectorSize) == 0` and the division traps on the IOP during warmup — before the alloca path of RE-LAUNCH-016 is ever reached. RE-LAUNCH-016's ingest whitelist would also fix this, but the sink is distinct and unlisted.
- **C-V2-2 — `ps2ips` send path never validates `pkt->length` itself (gap in RE-LAUNCH-015's proposed fix).** `do_send`/`do_sendto` compute `sendlen = MIN(BUFF_SIZE, pkt->length)` (`ps2ips.c:343,376`); a negative EE `length` yields a negative `sendlen`, passed as `sendlen - pkt->malign` to `sceSifGetOtherData` and as the length to `send`/`sendto`. RE-LAUNCH-015's fix guards `malign` and the *recv* length only; the send `length < 0` case survives it.
- **C-V2-3 — `smbd` START copies are not guaranteed NUL-terminated.** `strncpy(user, arg, sizeof(user) - 1)` / `strncpy(password, …, sizeof(password) - 1)` (`iop/smbd/smbd.c:285-286`, likewise module args at `:331-332`) into 32-byte statics: a source of ≥31 chars leaves byte 31 holding its previous value rather than a forced `'\0'`, so the credential strings are terminated only by leftover state. Minor, but the same handler RE-LAUNCH-020 already touches.

---

# Adversarial Verification — Chunk V3: RE-LAUNCH-025 … RE-LAUNCH-036

- Source report: `~/workspace/Sable-audit-redo/Redo-Launchstation.md` (sections 025–036 only read)
- Code pin: `~/workspace/audit-clones-2026-11-13/Launchstation` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (HEAD verified, read-only)
- libsmb2 tree checked at `/tmp/launchverify/libsmb2` where a finding cites it
- Method: refute-first; every cited file opened at the pin, mechanism re-derived, defeating guards/callers/types hunted before acceptance.

## Summary

| Verdict | Count | IDs |
|---|---|---|
| VERIFIED | 5 | 025, 028, 032, 034, 035 |
| VERIFIED-DRIFT | 6 | 026, 027, 029, 030, 031, 033 |
| WEAKENED | 1 | 036 |
| REFUTED | 0 | — |
| BLOCKED-ENV | 0 | — |

No duplicate-mechanism pairs inside 001…092 (near-neighbours distinguished in each row). No finding is refuted; every core mechanism in this chunk survives re-derivation. The corrections are citation drift (026/027/029/030/031), one understated severity (033), and one overstated comparison (036).

---

## Rows

| ID | Verdict | Confirmation / reasoning |
|---|---|---|
| RE-LAUNCH-025 | VERIFIED | Citations exact (`hdl.c:43-44`, `apa.c:243`, cross-check `apa.c:257`, `ps2_hdd.h:20` + `:76`); `nsub` is a raw on-disk u32 with no clamp at either site, and the apa.c count check fires only *after* the inner loop has already read `subs[k]` for `k < nsub`. |
| RE-LAUNCH-026 | VERIFIED-DRIFT | See below. |
| RE-LAUNCH-027 | VERIFIED-DRIFT | See below. |
| RE-LAUNCH-028 | VERIFIED | Citations exact (`editor_file.c:278-281` incl. the two commented variants at 278–279, live `strcpy` at 280; `editor.c:38`; `keyboard(tmp, 36)` at `editor_file.c:277`; `getFilePath` source at :269); `MAX_PATH` is 1025 (`launchelf.h:204`), `browseFilePath` `strcpy(out, path)` (`filer_browser.c:1261,1270`) can return a near-full path, and the function's own `abort:` label exists (`editor_file.c:312`), so the proposed fix fits. |
| RE-LAUNCH-029 | VERIFIED-DRIFT | See below. |
| RE-LAUNCH-030 | VERIFIED-DRIFT | See below. |
| RE-LAUNCH-031 | VERIFIED-DRIFT | See below. |
| RE-LAUNCH-032 | VERIFIED | GUID literal exact at `smbd_fs.c:998` (`memcpy(server.guid, "Launchstation-PS", 16)` — exactly 16 chars, no NUL); `server->session_counter = 0x1234` exact at `/tmp/launchverify/libsmb2/lib/libsmb2.c:5807`, executed on every `smb2_serve_port` run after listen, with session ids then issued as `session_counter++` (libsmb2.c:5240,5307,5370); server GUID is what negotiate/VALIDATE_NEGOTIATE_INFO returns (libsmb2.c:4730). READ/INFERRED split (client-visible impact INFERRED, no Windows test) is honest. |
| RE-LAUNCH-033 | VERIFIED-DRIFT | See below (severity understated, not overstated). |
| RE-LAUNCH-034 | VERIFIED | Citations exact (`mmce_fs.c:1040-1043`); `filename_len` is a u8 local (`mmce_fs.c:949`) taken from `rdbuf[0x29]`, received straight into `dirent->name`, and the function returns at :1063 with no byte ever forced to NUL; `iox_dirent_t.name` is `char[256]` (in-tree stubs `tests/pccf-stubs/iox_stat.h:117`, `tests/smb2man_live/inc/iomanX.h:29`), so a 255-byte name leaves `name[255]` stale and the proposed `name[filename_len] = '\0'` fits exactly. |
| RE-LAUNCH-035 | VERIFIED | Off-by-one and negative-slot hole exact (`mciomanx_backing.c:46` `if (slot > MCMAN_MAXSLOT)`, array `[MCMAN_MAXSLOT]` at :26, `MCMAN_MAXSLOT 10` for VMCMAN at `mcman-internal.h:106`); unreachability confirmed — see below. Cat-6 filing is fair — see below. |
| RE-LAUNCH-036 | WEAKENED | See below. |

---

## Full reasoning — non-VERIFIED rows

### RE-LAUNCH-026 — VERIFIED-DRIFT
- Mechanism re-derived and correct. `mmce_cmd_set_gameid` (`iop/mmceman/src/mmce_cmds.c:226`): `u8 wrbuf[0xFF]` (255 bytes), header occupies `wrbuf[0..3]`, `strcpy(&wrbuf[4], ptr)`. Overflow threshold is exactly as claimed: `strlen(ptr) == 250` fits precisely (4 + 250 + NUL = 255); `strlen >= 251` writes past the stack buffer. The u8 wrap is also as claimed: `len = strlen(ptr) + 1` truncates at `strlen == 255` (256 → 0), so transmitted length and copied length diverge. Caller confirmed unchecked: `mmce_fs.c:1364-1367` (`MMCE_CMD_SET_GAMEID` devctl case passes `(char *)arg` through with no NULL/arglen check — in pointed contrast to the `MMCE_CMD_SET_CARD_CHANNEL` case at :1379, which does check `arg == NULL || arglen < 5`).
- Drift (1 line): `u8 len = strlen(ptr) + 1` is at **:230**, not :231; `wrbuf` decl at :232, `strcpy` at :241 (finding's :226-243 range covers all). No substance change.
- Fix sane / LESS-NOT-MORE: reject `strlen(ptr) > 250` at entry is exactly the wire-expressible maximum. 
- Duplicates: none. RE-LAUNCH-023 is the opposite-direction game-ID copy (card→EE buffer in the *get* path); different function, buffer, and attacker input — complementary, not the same mechanism.

### RE-LAUNCH-027 — VERIFIED-DRIFT
- Mechanism re-derived and correct, and it is reachable, not merely contractual. `ynDialog`: `char msg[512]` + `strcpy(msg, message)` at `src/gui.c:12,20`; `nonDialog`: `char msg[80 * 30]` (2400) + `strcpy` at `src/gui.c:95,111`. Override bypass confirmed (`gui.c:18-19`, `gui.c:100-102`). Concrete oversized caller: `src/filer_copy.c:760` declares `static char progress[MAX_PATH * 4]` (4100 bytes, built from two full paths plus labels) and passes it to both `ynDialog(progress)` (`filer_copy.c:1065`) and `nonDialog(progress)` (`filer_copy.c:1537`) — a >512-byte message to `ynDialog` is constructible from ordinary long in/out paths; `lang.c:564` also passes a 4096-byte buffer (`tmp_s[4096]`, `lang.c:537`). No caller-side length contract exists.
- Drift: the prototypes are at **`include/launchelf.h:586-587`**, not :594-595 (:594-595 are `filerMountVmc` / `filerUnmountVmc`). The finding's `nonDialog` override citation `:102-105` is the return/NULL-check region; the override check itself is at :100-102. Function ranges otherwise accurate.
- Fix sane: bounded copy + truncation degrades gracefully because both helpers re-measure/re-wrap after copying. No new mechanism.
- Duplicates: none. RE-LAUNCH-006/017/018 are unbounded-copy findings in other subsystems (CNF loader, filer copy cluster, scanSystemCnf) with different sinks and inputs.

### RE-LAUNCH-029 — VERIFIED-DRIFT
- Special check performed as tasked. `read_cmd` really clamps: `smbd_fs.c:449-450` `if (len > SMBD_MAX_IO) len = SMBD_MAX_IO;`. `write_cmd` (`smbd_fs.c:466-489`) really does not: after the zero-length early return (:476-479) it calls `smbd_pwrite(card_fd, req->buf, (int)req->length, req->offset)` at :482 with no comparison. `smbd_pwrite` (`iop/smbd/smbd.c:156-160`) is a single `iomanX_lseek` + single `iomanX_write` of the full length — no internal chunking rescues it.
- Decoder cap claim verified in the libsmb2 tree: `smb2_recv_pdu_cap()` at `/tmp/launchverify/libsmb2/include/libsmb2-private.h:359-368` returns `max(max_read, max_write) + SMB2_RECV_PDU_SLACK` (64 KiB); the cap is enforced on received PDUs at `lib/socket.c:572-573`, and the WRITE decoder (`lib/smb2-cmd-write.c`, `smb2_process_write_request_fixed`) rejects a `req->length` extending beyond the PDU (`smb2->spl`). With the negotiated 64 KiB maximum, a client can therefore present a WRITE of ≈128 KiB (131072 minus headers) in one call — the finding's "~128 KiB" is accurate, and the `(int)` cast is safe at that bound.
- READ/INFERRED split is honest: the in-code short-write reporting (`rep->count = n`, `smbd_fs.c:487-488`) supports the INFERRED "inefficiency/protocol violation rather than corruption" downstream judgement, and MCMAN behaviour is correctly left unread.
- Drift: advertised-maxima assignment is at **`smbd_fs.c:997`**, not :1000; `SMBD_MAX_IO 65536` is at **`smbd.h:28`**, not :27 (:27 is `SMBD_LEAF_MAX`); read clamp is at :449-450, not :450-451; libsmb2 cap function spans :359-368, not :359-370. All trivially locatable, no substance change.
- Fix sane: copying read_cmd's two-line clamp into write_cmd is the minimal symmetric fix.
- Duplicates: none. RE-LAUNCH-008 is 64→32-bit *offset* truncation in the same file — different field, different sink.

### RE-LAUNCH-030 — VERIFIED-DRIFT
- Mechanism re-derived and correct. `make_path` (`smbd_fs.c:120-149`) walks the relative path component-by-component, refuses `..` and `:` (:137-138) and enforces only the total `n + 1 + len >= SMBD_PATH_MAX` (:139-140) — no per-component leaf check anywhere in it. `create_cmd` calls it at :345 and proceeds to `smbd_mkdir` / `smbd_open` (:374, :383 in function order) with whatever leaf resulted. `rename_handle` by contrast does enforce the leaf: `if (strlen(b + 1) > SMBD_LEAF_MAX) return … SMB2_STATUS_NAME_TOO_LONG`. `SMBD_LEAF_MAX 31` is `smbd.h:25` in the finding's citation — actually :27 (see 029 drift note; the define itself is real) — and the 31-byte basis is confirmed in-tree: `sceMcTblGetDir.EntryName` is `char EntryName[32]` (`iop/vmcman/include/mcman.h:32`), i.e. 31 chars + NUL. `smbd_rename` does trust the check: it `strcpy`s the leaf into `info.EntryName` with no length test of its own.
- Drift (material citation error, mechanism intact): the rename leaf check is at **`smbd_fs.c:871-872`**, not "~855-860"; `smbd_rename` is at **`iop/smbd/smbd.c:209-218`** with the `strcpy` at **:216**, not :230-241 (:230-241 is `smbd_space` / the serve thread). A reader following the printed smbd.c citation lands in unrelated code.
- READ/INFERRED split judged honest: the finding claims only the missing check as READ and explicitly labels the MCMAN consequence (clean failure vs silent truncation/collision) INFERRED because the driver behaviour was outside its read set. That is the correct split for the evidence it cites; nothing in the cited code overclaims.
- Fix sane / LESS-NOT-MORE: putting the per-component check in `make_path` is the right single point (CREATE, RENAME, and every other verb already funnel through it; card directory components are subject to the same 31-byte entry limit, so applying it to intermediate components is correct, not overreach), and deleting rename's then-redundant check is a net deletion.
- Duplicates: none in 001…092.

### RE-LAUNCH-031 — VERIFIED-DRIFT
- Mechanism re-derived and correct. `lookup()` (`smbd_fs.c:159-175`): slot index from `id[0..1]` (:169), generation compared from `id[2..3]` only (:172); bytes 4–15 play no part in validation (the only scan of all 16 bytes is the all-`0xff` compound-request sentinel at :163-167, which is a distinct value, not validation entropy). Issued ids zero bytes 4–15 (`memset(rep->file_id, 0, SMB2_FD_SIZE)` then only [0..3] written). `gen` is `uint16_t` in `struct handle` (:30) and `next_gen` is `uint16_t`, so after 65,536 CREATEs the same slot reissues with the same generation and a stale id validates again. Owner check (`handles[i].owner != smb2`, :170) bounds the impact to the same authenticated connection, as the finding's Low benefit reflects.
- Drift: `next_gen` is at **:39**, not :40 (:40 is `tree_counter`); file-id construction is at **:408-413**, not :405-409; `h->gen = next_gen++` at :397 is exact.
- Fix sane: widening `gen`/`next_gen` to 32 bits into bytes 2–5 and extending the comparison is small and local; no new subsystem.
- Duplicates: none. RE-LAUNCH-019 (handles survive logoff) is a handle-lifetime issue, not id-validation entropy — different mechanism.

### RE-LAUNCH-033 — VERIFIED-DRIFT (finding understates the defect)
- Walk, bounds check, and TODO/cap citations are exact: loop `apa.c:154-179`, checksum+magic gate :159-161, bounds check :162-163, TODO comment :175-176, cap :177-178. `apa_part_add` (`apa.c:64-87`) has **no duplicate/visited check** — it appends unconditionally — so a cyclic `next` chain is never detected as a repeat, as claimed. The wrap sub-point is correctly labelled INFERRED: `get_u32(&part.start) + get_u32(&part.length)` is 32-bit arithmetic on the IOP (`u_long`/`u32`), a large `length` can wrap the sum below `total_sectors`, and the checksum+magic gate is correctly credited as the limiting factor.
- Correction to the claimed stopping behaviour: the cap is on **`(*table)->part_count > 10000`**, and `part_count` increments **only** for headers with `flags == 0 && type == 0x1337` — `apa_part_add` is called inside that condition (`apa.c:164-165`), while `sector = get_u32(&part.next)` at :166-167 executes for *every* in-bounds valid header. Therefore the "stopped only after 10,000 iterations" claim holds only for cycles composed of MBR-type headers. A cycle among ordinary partition headers (the normal APA chain content) never increments `part_count`, the cap never fires, and the walk loops **unbounded** — the true scope is worse than the finding states, not better. This is drift in the finding's favour-direction (understatement); the proposed fix (visited-set via the table being built + 64-bit comparison) addresses both forms and remains sane and local.
- Duplicates: none. RE-LAUNCH-025 shares the file and the disk-trust theme but is a different mechanism (bounded-array over-read via `nsub`, not chain following).

### RE-LAUNCH-035 — reachability and filing fairness (VERIFIED, expanded)
- Unreachability claim holds after tracing the full caller graph, which is one hop longer than the finding's citation suggests. Direct backing entry points: mount/umount are called only from `mcdev.c:755,773` with `mcman_mc_slot`, set by `mcman_unit2card` (`mcdev.c:216-223`); in the VMCMAN branch `mcman_mc_slot = unit & (MCMAN_MAXSLOT - 1)` = `unit & 9`, whose result set is {0, 1, 8, 9} — never 10, never negative (unit is `u32`). The remaining backing functions (read/write/erase/getcardspec, `mcsio2.c:492,640,736,798`) receive `port, slot` through the `Mc*` layer (`McReadPage`/`McWritePage`/`mcman_eraseblock`/`McGetCardSpec`), whose in-tree callers in `main.c`/`mcdev.c` pass the same `mcman_mc_port`/`mcman_mc_slot` state (or a literal slot 0, `main.c:1167`). No in-tree path supplies slot 10 or a negative slot to `mcman_iomanx_backing_validate`. The finding's "only callers derive the slot through mcman_unit2card (mcdev.c:216-223)" is a fair summary of that chain.
- Cat-6 filing is fair: the finding is explicitly titled "(latent)", states "not a live bug", and rates Benefit Low — it does not inflate a dead path into a live vulnerability. The defective code is the validation function whose sole job is this check, and the one-line fix (`slot < 0 || slot >= MCMAN_MAXSLOT`) is exactly LESS-NOT-MORE. Filing it as a latent trap for the next caller is proportionate.

### RE-LAUNCH-036 — WEAKENED
- True core (verified in the buffered path, `read_response`, `src/cloud_http.c:560-680`): `header_value` (:501-526) matches header *names* case-insensitively (`strncasecmp`, :509) but silently truncates values to the caller's buffer (:515-518); chunked detection is `strstr(value, "chunked")` — case-sensitive — at :597-599 and again at :658-659, so a legal `Transfer-Encoding: Chunked` is missed twice and raw chunk framing reaches the JSON parsers as body; `Content-Length` goes through `atoi` at :596, :644 and :668 (overflow is UB, trailing garbage is accepted); `Location` is read into `response->location[CLOUD_HTTP_LOCATION_MAX]` = 2048 (`include/cloud_http.h:11`) at :657, so a >2047-byte session URL is silently truncated and used. Two separate implementations do exist (buffered `read_response` vs streaming `cloudHttpDownloadFdInternal`, :953-1277).
- Where the finding overstates — the "streaming path is really stricter" comparison:
  1. The streaming path parses Content-Length with **the same `atoi`** at :1123. Its only extra strictness is rejecting `< 0` immediately (:1124-1130) and enforcing the caller's `max_bytes` (:1132-1137) — and the buffered path *also* rejects `declared < 0 || declared > CLOUD_HTTP_BODY_MAX` in its second pass (:644-649). The difference is timing/scope, not parsing rigour.
  2. The streaming path's chunked detection at :1139-1141 is the **identical case-sensitive `strstr(value, "chunked")`** — capital-C `Chunked` is missed there too. On the chunked axis the two paths are not divergent at all; they share the bug.
  3. The streaming path reads `Location` through the same truncating `header_value` (:1083) into an equally-sized 2048 buffer.
  4. Consequently the fix's parenthetical — parse Content-Length "with a bounded digit loop … (mirroring the streaming path)" — points at a pattern that does not exist in the streaming path; there is no digit loop anywhere in `cloud_http.c`. The digit-loop fix is still the right fix, but it is new code, not a mirror.
- True scope: a real buffered-path parsing bug set (case-sensitive chunked, atoi Content-Length, silent Location truncation) plus a genuine two-implementations maintenance hazard — but the streaming implementation is a co-defendant on chunked-case and atoi, not the clean reference implementation the finding presents. Hence WEAKENED, not VERIFIED-DRIFT: one of the finding's load-bearing comparative claims does not survive re-reading.
- Duplicates: none. RE-LAUNCH-050 (duplicated JSON extraction) and RE-LAUNCH-081 (triplicated IPv4 validation) are divergent-duplication findings in other subsystems.

---

## CANDIDATES (new / strengthened defects found while verifying)

1. **CAND-V3-1 — APA chain walk can loop forever, not merely 10,000 times (strengthens RE-LAUNCH-033).** `iop/hdl_info/apa.c:164-178`: `part_count` — the only cycle brake — increments only inside `if (flags == 0 && type == 0x1337)` via `apa_part_add`, while `sector = part.next` advances for every valid in-bounds header. A cyclic chain of ordinary (non-0x1337) partition headers, each with valid checksum+magic and in-bounds start/length, spins the do-while indefinitely with no brake at all. Settles by: constructing the header cycle from the code as written (done above) — no hardware needed to confirm the control flow.
2. **CAND-V3-2 — VMCMAN `mcman_unit2card` mask cannot produce slots 2–7.** `iop/vmcman/src/mcdev.c:223`: `mcman_mc_slot = unit & (MCMAN_MAXSLOT - 1)` with `MCMAN_MAXSLOT == 10` (`mcman-internal.h:106`, not a power of two) yields only {0, 1, 8, 9}. If the iomanX unit number is meant to encode VMC slots 0–9 linearly, slots 2–7 are unreachable and units collide (2→0, 3→1, …). Tentative: needs the EE-side unit-assignment convention for VMC devices to settle whether the encoding is linear or deliberately sparse.
3. **CAND-V3-3 — `mmce_cmd_set_gameid` dereferences a possibly-NULL devctl arg.** `iop/mmceman/src/mmce_fs.c:1364-1366` passes `arg` unchecked and `mmce_cmds.c:230` immediately calls `strlen(ptr)`; the neighbouring `MMCE_CMD_SET_CARD_CHANNEL` case (`mmce_fs.c:1379`) explicitly guards `arg == NULL`, showing the guard was considered for one case and missed here. Adjacent to RE-LAUNCH-026 but a distinct mechanism (NULL deref vs length overflow).

---

## Notes on method

- Nothing under `~/workspace/Sable-audit/` was read. The clone was not written to.
- All line numbers above were taken from `awk`/`grep -n` output at the pin during this run; where a verdict corrects a finding's citation, the corrected line is the one printed here.

---

# Adversarial verification — chunk V4: RE-LAUNCH-037 … RE-LAUNCH-048

- Source report: `~/workspace/Sable-audit-redo/Redo-Launchstation.md` (sections 037–048 only)
- Code: `~/workspace/audit-clones-2026-11-13/Launchstation` at pin `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (HEAD verified, tree clean, read-only)
- libsmb2 patched tree: `/tmp/launchverify/libsmb2` (used for RE-LAUNCH-041)
- Method: refute-first; every cited file opened at the pin and re-derived from scratch; tests run only in a `/tmp` copy (`/tmp/v4check/repo-copy`)
- Tallies: **VERIFIED 7 · VERIFIED-DRIFT 5 · WEAKENED 0 · REFUTED 0 · BLOCKED-ENV 0**

| ID | Verdict | Confirmation / correction |
|---|---|---|
| RE-LAUNCH-037 | VERIFIED | One-line confirmation below. |
| RE-LAUNCH-038 | VERIFIED-DRIFT | Mechanism verified; one citation corrected (see reasoning). |
| RE-LAUNCH-039 | VERIFIED | One-line confirmation below (fix caveat noted). |
| RE-LAUNCH-040 | VERIFIED-DRIFT | Mechanism verified; `maximal_access` citation corrected (see reasoning). |
| RE-LAUNCH-041 | VERIFIED | One-line confirmation below. |
| RE-LAUNCH-042 | VERIFIED | One-line confirmation below. |
| RE-LAUNCH-043 | VERIFIED-DRIFT | Mechanism verified; `while (1)` citation corrected (see reasoning). |
| RE-LAUNCH-044 | VERIFIED-DRIFT | Interceptions exact and no live route found; line-count figure corrected (see reasoning). |
| RE-LAUNCH-045 | VERIFIED | One-line confirmation below (independently script-verified + executed). |
| RE-LAUNCH-046 | VERIFIED-DRIFT | Mechanism verified; position of RE-LAUNCH-009's suite corrected (see reasoning). |
| RE-LAUNCH-047 | VERIFIED | One-line confirmation below. |
| RE-LAUNCH-048 | VERIFIED | One-line confirmation below. |

---

## VERIFIED — one-line confirmations

- **RE-LAUNCH-037 — VERIFIED.** `src/cloud_google.c:227-228` comment promises a custom client is used "only as a complete pair", but `load_client` at `:232-233` returns as soon as `client_id[0]` is set (secret kept as-is, including empty); `builtin_client_available` at `:205-212` tests only the ID; `cloudGoogleSaveClientConfig` at `:457-472` rejects only an empty ID and writes an empty secret verbatim (`:480-484`); `refresh_access_token` at `:377-378` and the device-token poll at `:629-630` then omit `client_secret` when empty — and `scripts/gen_cloud_client.py` itself refuses an unpaired ID/secret ("Google's device flow requires the secret"), corroborating the pair rule. Fix (fall back to the built-in pair whole; reject secret-less save) is sane and LESS-NOT-MORE.
- **RE-LAUNCH-039 — VERIFIED.** `iop/smbd/smbd_fs.c:911-914`: `SMB2_FILE_BASIC_INFORMATION` and `SMB2_FILE_ALLOCATION_INFORMATION` fall through to `return 0` with no effect; `:924-932`: `END_OF_FILE` returns `SMB2_STATUS_NOT_SUPPORTED` only when the requested size is *below* current size, and `return 0` for any size ≥ current without extending; unsupported classes honestly return `SMB2_STATUS_NOT_SUPPORTED` at `:937`. (Return 0 = libsmb2 encodes a success reply; `reply()` at `:73-85` is the error path returning 1.) Fix caveat, not a defect in the finding: no time-setting backend exists in smbd (backend list is stat/open/read/write/rename/remove/mkdir only), so of the report's two BASIC options only the `NOT_SUPPORTED` half is immediately available — which the report already offers.
- **RE-LAUNCH-041 — VERIFIED.** `query_directory_cmd` (`iop/smbd/smbd_fs.c:542-609`) never reads `req->file_information_class` (grep: the field is used only by QUERY_INFO/SET_INFO), fills `smb2_fileidbothdirectoryinformation` entries regardless of class, `memset`s each entry at `:589` and never assigns `file_id` (always 0), and sets `file_index = n` at `:590` with batch-local `n` (restarts per batch). In the patched libsmb2, `dir_entry_size()` (`lib/smb2-cmd-query-directory.c:201-218`) returns 0 for any class outside its six, the encoder's first loop (`:272-273`) then computes `fs_size = 0` per entry and zeroes the total output length, and at `:285-287` returns success with a header-only reply — while the server dispatch (`lib/libsmb2.c`, `smb2_query_directory_request_cb`) only converts a *handler-supplied* zero length to `STATUS_NO_MORE_FILES`, so a non-empty directory asked in an unsupported class yields a successful zero-entry reply, not an error. `SMB2_STATUS_INVALID_INFO_CLASS` exists (`include/smb2/smb2-errors.h:43`), so the whitelist fix is a few lines. Minor example weakness, mechanism unaffected: FileStreamInformation is a QUERY_INFO class a client would not normally send for QUERY_DIRECTORY; the code-proven exposure is any non-six class value (e.g. the newer IdExtd directory classes, which libsmb2 does not define at all).
- **RE-LAUNCH-042 — VERIFIED.** `iop/smb2man/usmb2/usmb2.c:791-792`: `u32 = le32toh(*(uint32_t *)&usmb2->buf[4])` (the reply's OutputBufferLength) is used unclamped as `memcpy(&dh[16 + 4], &usmb2->buf[8], u32)`; destination is `calloc(1, size)` in `usmb2_create` (`:708`) with `size = 16 + MAX_DIR_SIZE` = 272 for directories (`:739-740`, `MAX_DIR_SIZE 256` at `:69`), so the copy overruns once `u32 > 252`; the entry-chain walk at `:795-817` is likewise unbounded. Gate verified exhaustively: `USMB2_FEATURE_OPENDIR` appears nowhere as a define except the `#ifdef` blocks at `usmb2.c:731/743` and documentation in `usmb2.h:20`/`README`; the root `ps2.yaml:262-271` smb2man defines are exactly WRITE/NTLM/UNICODE/STAT/MKDIR/UNLINK/RENAME, no other `ps2.yaml` (root, `iop/pccf`, `iop/ps2ips_rpc`) mentions USMB2, and `usmb2.h:34-39` auto-defines only `USMB2_FEATURE_CLOSE`. `smb2man.c`'s own querydir (`:1207-1236`) does clamp the same field (`len > SMB2MAN_DIRBUF` at `:1231-1232`), confirming the "hardened own path, unhardened vendored path" contrast, and `VENDORED.txt`/`riptopl.patch` list no readdir patch. Fix (clamp to `MAX_DIR_SIZE - 4` and `reply_len`, both in scope) or deleting the OPENDIR block are both sane and LESS.
- **RE-LAUNCH-045 — VERIFIED.** Independently script-verified: 213 `tests/test_*.py` files; exactly 211 are named in `.github/workflows/compile.yml`; the only two absent from compile.yml — and from *all six* workflows — are `tests/test_boot_deferred_init.py` and `tests/test_usb_stat_timeout.py`. The false claim is verbatim at `docs/architecture-maintenance.md:44-45` ("Every `tests/test_*.py` script is listed there"). Both orphans were executed in a `/tmp` copy of the tree: `test_boot_deferred_init.py` → PASS, `test_usb_stat_timeout.py` → PASS (it compiles and runs the production USB-stat helper from `src/init.c`). Fix is 2 lines.
- **RE-LAUNCH-047 — VERIFIED.** Wrong path verbatim at `README.md:104`, `docs/wiki/Current-Features.md:49`, `docs/wiki/Releases-and-Recovery.md:53` ("Settings → Storage & devices → MMCE backups → Check for updates"); code places the row in System: `S_CHECK_UPDATES` is in `system_items[]` at `src/dash_settings.c:776` (array at `:766`), and `mmce_backup_items[]` at `:366-381` contains no update row. The "other texts state it correctly" claim also checks out: `docs/dashboard.md:199` (System row includes Check for updates), `docs-site/content/cloud.html:22`, `install.html:30`, `getting-started.html:22` all say Settings → System, as do `network.html:18` and `developers.html:20`.
- **RE-LAUNCH-048 — VERIFIED.** "It is off by default" verbatim at `docs/NATIVE_CLOUD.md:95` and `docs-site/content/cloud.html:14`; code default is `dashCfg.igr_auto_backup = 1` with `igr_cloud_sync = 0` at `src/dash_store.c:263-264` (persisted-value fallback at `:366` also defaults to 1), and the mode mapping at `src/dash_settings.c:1163` with `igr_sync_names[] = {"Off", "Local backup", "Local + Google Drive"}` at `:970` makes mode 1 = Local backup. Correct texts confirmed: `mmce-backup.html:52` ("Local backup (default)"), `igr.html:26` ("default: Local backup"), and `configuration.html:84` (defaults column lists `Dash_IGRAutoBackup` = 1).

---

## Non-VERIFIED — full reasoning

### RE-LAUNCH-038 — VERIFIED-DRIFT

Core mechanism re-derived and true:

- Refresh token in clear text: `src/cloud_google.c:323` writes `Refresh_Token = %s` as a plain line to `WLE-CLOUD.TOK`; the staged duplicate `WLE-CLOUD.TOK.NEW` is written first at `:328`, verified, and removed on the success path at `:353` — exactly as the report's fix note (3) states (it is deliberately *retained* on the primary-write failure path, `:346-350`, as a recovery copy, which the report does not claim otherwise).
- Custom client ID/secret in clear text: `cloudGoogleSaveClientConfig` at `:457-492` (report's range exact) writes both as plain `Google_Client_ID = …` / `Google_Client_Secret = …` lines to `WLE-CLOUD.CNF`.
- Built-in client material: location-only handling confirmed — `src/cloud_google.c:20-27` (generated-header include) and `scripts/gen_cloud_client.py` (values from CI secrets, never committed); no values reproduced in the report.
- SMB share passwords in clear text: `SmbShare.username`/`password` are plain `char[SMB_CRED_LEN]` fields parsed from and written back to config text by `src/smb_cnf.c` (e.g. `:392`, `:515-517`); shares persist in `SMB2.CNF` / the `LAUNCHSTATION.CNF` share section (`src/dash_settings.c:386`, `:563-564`).

Correction (citation drift only): the report cites `include/smb_shares.h:57-58` for "SMB share user names/passwords held … in clear text". At the pin, `:57-58` are a comment and the `server` field; `username`/`password` are at `include/smb_shares.h:49-50`. The report's own confidence label (READ for storage format; FTP readability of the settings folder INFERRED) is honest — the token/config files live beside the loaded configuration or in `mc0:/SYS-CONF/` via `dashStoreBuildPath` (`src/cloud_google.c:266-269`, `src/dash_store.c:6`, `:212`), and no code was found that excludes that folder from the FTP server's exposed roots, but no executed proof was attempted. Fix (document the token in the link flow; keep files out of default shares; preserve the `.TOK.NEW` success-path deletion) is sane and LESS-NOT-MORE.

### RE-LAUNCH-040 — VERIFIED-DRIFT

- LOCK half verified, and stronger than stated: `lock_cmd` at `iop/smbd/smbd_fs.c:491-497` ignores `srvr`/`smb2`/`req` and returns 0 unconditionally — it does not even validate that the file_id names an open handle, so a lock on a nonexistent handle also "succeeds". The libsmb2 server dispatch (`lib/libsmb2.c`, `smb2_lock_request_cb`) adds no locking of its own; the handler's 0 is encoded as a successful LOCK reply.
- CREATE half verified: `create_cmd` at `:331-415` never reads `req->share_access` or `req->requested_oplock_level` (grep over the whole file: zero occurrences of either field), and sets `rep->oplock_level = 0` at `:402`. The libsmb2 create dispatch (`smb2_create_request_cb`) performs no share-access or oplock enforcement before calling the handler, so the handler is the only place it could happen. `SMBD_MAX_CLIENTS` is 2 (`iop/smbd/smbd.h:29`), and the handle table records no lock ranges or sharing intent — the two-client write-under-a-fake-lock scenario stands.
- Correction (citation drift only): the report locates the `maximal_access = FILE_ALL_ACCESS` grant at "`smbd_fs.c:340-345` region". At the pin, `:340-345` is inside `create_cmd` (the `write`-flag computation and share check); the grant is in `tree_connect_cmd` at `smbd_fs.c:312` (`rep->maximal_access = 0x001f01ff; /* FILE_ALL_ACCESS */`). Mechanism unaffected. Fix (fail LOCK with `NOT_SUPPORTED`, as IOCTL at `:499-506` and CHANGE_NOTIFY at `:611-618` already do; deny a conflicting second CREATE using the existing `handles[].path` table) is sane; the status-only half is a few lines.

### RE-LAUNCH-043 — VERIFIED-DRIFT

- DashRun never returns on normal paths, re-derived end-to-end: `DashRun` is at `src/dash_main.c:1331-1529`; its only `return` in the whole function is the guard at `:1338-1339` (`if (dash_active || ctx == NULL) return;`); after setup it enters the dashboard loop and there is no `return`, `break`-to-exit, or exit path in the loop body (`:1388-1528`). In `main()` the guard cannot fire on the boot path (`dash_active` is 0, `ctx` is a non-NULL stack struct), so the classic loop after the call is unreachable in normal operation.
- Classic loop and callers verified: comment at `src/main.c:301` ("The dashboard is the only shell; DashRun() does not return"), `DashRun(&dash_ctx, 1)` at `:315`, loop at `:321-371`, including the unbounded `sprintf(mainMsg + strlen(mainMsg), …)` at `:345`. Tree-wide grep for the three menu functions: the only call sites are `src/main.c:357` (`drawMainMenuScreen`), `:363` (`MainMenuHandlePad`), `:364` (`MainMenuHandleAutoLaunch`); definitions at `src/main_menu.c:81`, `:254`, `:330`; declarations at `include/main_menu.h:34-36` — all exactly as cited. (`MainMenuState_Init`/`BeginTimers` at `src/main.c:284`/`:286` run *before* DashRun and `MainMenuMarkActionTaken`/`MainMenuState_UpdateTimers` are also main.c-only, so the report's conditional keep/delete wording for them is correct.)
- Correction (citation drift only): the report cites the `while (1)` at `src/dash_main.c:1396-1397`. At the pin, `:1396-1397` is a comment about USB fileXio/PAD input inside the loop; the loop itself is at `:1387-1388` (`/* the dashboard is the only shell: it never returns */` + `while (1) {`). Also the report's `src/main.c:302-377` range is one line late at both ends (actual `:301-373`). Fix (delete the post-DashRun loop and the three caller-less menu functions) is sane; the report correctly notes the guard-return path currently falls into this loop with an uninitialised dashboard, so deletion makes that failure explicit rather than hiding it.

### RE-LAUNCH-044 — VERIFIED-DRIFT (INFERRED core claim survives a targeted live-route hunt)

Interceptions and shadowed implementations are exact:

- `dashRunMisc` at `src/dash_main.c:442-486`: Configure → dashboard Settings (`:447-451`), HddManager → `dashHddManager` (`:452-456`), FileBrowser → `dashOpenBrowser` (`:457-460`), TextEditor → `dashOpenTextEditor` (`:461-464`), Exploit_Installer → `dashExploitInstaller` (`:465-468`), About → `dashShowAbout` (`:469-472`), BuildInfo → `dashShowBuildInfo` (`:473-476`), DebugInfo → `dashShowSystemInfo` (`:477-480`), ShowFont → `dashShowFontViewer` (`:481-484`); only non-intercepted paths fall through to `runMiscCommand` at `:485`. Second layer exact: `runMiscCommand` returns early for Misc_Configure/Misc_Exploit_Installer at `:389-390`.
- Shadowed code exact: `config()` at `src/config.c:1492-1779` (288 lines ≈ the cited 290), `src/config_screen.c` 361 lines, `src/config_advanced.c` 329, `src/config_startup.c` 364, `src/dash_settings.c` 4,096 lines, `ShowFont` at `src/main_info_screens.c:285`, classic dispatch in `ExecuteMainAction` at `src/main_actions.c:768-802` (report cited `:768-799`; the Exploit_Installer clause extends to `:802`). Direct callers of `config()`/`ShowFont()`/`ShowDebugInfo()`/`Show_About_uLE()`/`Show_build_info()` exist *only* in that dispatch.

Live-route hunt (the report labels "no live route" INFERRED; I tried to defeat it):

- **Launch keys**: `runLaunchKey` (`src/dash_main.c:950-963`) sends a path to `dashLaunchApp` only if it contains `:` *and* ends in `.ELF`; everything else goes to `dashRunMisc` and is intercepted. Misc strings are `MISC/<name>` (no colon, `src/config.c:810`), so a launch key bound to a Misc command cannot bypass. Home launch-key chips behave identically (`src/dash_home.c:214-222`).
- **Library / File Browser launches**: `dashLaunchApp` (`src/dash_main.c:583-673`) calls `ExecuteMainAction` directly at `:660`, but only with resolved application paths (device-prefixed filesystem paths from the library scan, `src/dash_apps.c`, `src/dash_browser.c:652`, `src/dash_home.c`); no route was found by which a scanned app path can equal a `MISC/…` string. Dashboard File Browser MISC entries route through `dashRunMisc` (`src/dash_browser.c:713-716`).
- **Remote PC commands**: `src/dash_main.c:774-782` — OSDSYS/PowerOff go to `dashRunMisc`; any other path requires a `:` and goes to `dashLaunchApp`; a colon-less Misc string is rejected as "not launchable".
- **Exploit installer**: reached natively via `dashExploitInstaller` (`src/dash_exploit.c:150-162`), which sets `exploitInstallerUi`, so the installer's classic `getFilePath` call sites (`src/main_exploit_installer.c:974`, `:1023` — exactly as the report cites) are hook-bypassed while the dashboard is active; the installer is not one of the shadowed config/info/editor screens in any case.
- **Error / pre-boot paths**: the only pre-DashRun `Execute()` calls are the IGR-after routes (`src/main.c:228` Misc_OSDSYS, `:241` `dashCfg.igr_after_path`). The latter is an ELF file-picker field in the UI (`src/dash_settings.c:2075`, `p->ext = "ELF"`); reaching `config()` through it would require hand-editing `DASHBOARD.CNF` to the literal `MISC/Configure` string — a contrived config forgery, not a live user route. `ExecuteBrowserFrom` (`src/main_actions.c:875`) — the one other classic-browser entry — has **zero callers** tree-wide (see Candidates).

Correction (numeric drift only): "the classic `config()` family (~1,050 lines across four files)". The three satellite files alone total 361 + 329 + 364 = **1,054** lines; including `config()` itself (288 lines) the family is **1,344** lines. The ~1,050 figure matches the satellites only. Conclusion and fix (decide the fate explicitly; delete the shadowed implementations or name a concrete entry point) stand — and the risk note in the report is well-placed, since the deletion decision is exactly where a hidden route would bite.

### RE-LAUNCH-046 — VERIFIED-DRIFT

- One-step claim verified precisely: the step "Test Settings sessions, navigation and theme compatibility" at `.github/workflows/compile.yml:50` has a single `run: |` block at `:51-265` containing exactly **211** sequential `python3 tests/…` invocations (counted), with no `shell:` override on the step — GitHub Actions' default Linux step shell is `bash -e` (with pipefail), so the first failing test terminates the step and every later line never runs, with no per-test summary. The step name describes only a fraction of its contents (editors, copy pipeline, SMB, UDPFS, PCCF, …).
- Outside-the-step claim verified literally: the only `tests/test_*.py` invocation anywhere else in compile.yml is `tests/test_app_packages.py` in the packaging step at `:448`, after the builds. (For completeness: `test_app_packages.py` *also* runs inside the big step at `:238` — see Candidates — which does not contradict the report's statement about outside invocations.)
- Correction (position only): the report says RE-LAUNCH-009's suite "sits mid-list". `tests/test_copy_psu_listing.py` is at `compile.yml:71`, the **21st of 211** invocations — early in the step, so a failure there masks ~190 later tests, more than "mid-list" implies. The masking mechanism is therefore understated, not overstated. Fix (split into grouped steps, or run-all-and-report) is a rearrangement of existing lines; sane and LESS.

---

## Duplicate / same-mechanism check (against RE-LAUNCH-001…092)

- **039 / 040 / 041 — same family, not duplicates.** All three are smbd handlers in `iop/smbd/smbd_fs.c` that answer success without the promised semantics, but the mechanisms, commands, and fixes are distinct: SET_INFO classes discarded (039), LOCK unimplemented + CREATE share-access/oplock ignored (040), QUERY_DIRECTORY class never validated in the handler *and* collapsed to a zero-length success by the libsmb2 encoder + FileId never filled (041). No pair should be merged; they share only the recommended pattern (return `NOT_SUPPORTED`/an error status instead of a silent success), which IOCTL and CHANGE_NOTIFY in the same file already follow.
- **043 / 044 — complementary, not duplicates.** 043 is the dead post-DashRun loop in `main()` plus the three `main_menu.c` functions that serve only it. 044 is the dashboard interception layer (`dashRunMisc`/`runMiscCommand`) shadowing the classic config/info/editor implementations, which are dispatched from `ExecuteMainAction` — a function that is itself live for ELF launches. 044 depends on 043's premise (dashboard-only shell) but covers different files and a different remedy (explicit fate decision vs outright deletion).
- **045 vs 068 — distinct claims, same document, already cross-referenced.** 045 concerns the CI-coverage sentence at `docs/architecture-maintenance.md:44`; 068 concerns that file's largest-files list. Different facts, different fixes; the report's 068 entry already notes the CI claim was merged into 045.
- **045 vs 046 — distinct.** Same workflow file, different mechanisms: hand-maintained enumeration drift (two files missing) vs single-step `-e` masking (all 211 present but serialised in one step).
- **045 vs 072 / 075 — distinct.** 072 is a live-test directory wired into no workflow (not a `tests/test_*.py` file); 075 is a CI-run test of a research model. Neither overlaps the two orphaned `test_*.py` files.
- **037 / 038 — distinct.** 037 is the pair-rule logic bug in client selection/saving; 038 is the storage-format exposure of the resulting credentials (plus SMB share passwords and the built-in client location). 038 also does not duplicate RE-LAUNCH-010/011 (the smbd *server's* published default login and lack of throttling — a different credential, file, and direction).
- **042 — distinct** from RE-LAUNCH-003/024 (EE-side SMB1 client walks) and from RE-LAUNCH-057 (dead vendored code in the *smbd* compiled set): 042 is IOP-side vendored usmb2 client code, excluded by a feature define rather than compiled-but-empty.
- **047 / 048 — distinct** from each other and from RE-LAUNCH-065 (CHANGELOG NFS settings location): different documents, different facts (menu path vs IGR default), different fixes.
- No other finding in 001…092 covers the 039–042 smbd/usmb2 mechanisms, the 043/044 dead/shadowed classic UI, or the 047/048 doc facts.

## CANDIDATES (new defects found while verifying — not in 001…092)

- **CANDIDATE V4-1 — `ExecuteBrowserFrom` has no callers.** `src/main_actions.c:875-886` (declared `include/main_actions.h:27`) opens the classic FileBrowser via `getFilePath` + `executeBrowserSelection`; a tree-wide grep finds no call site in `src/`, `include/`, `tests/`, or `docs/`. It is a public, caller-less entry into the classic browser surface — adjacent to RE-LAUNCH-043/044's dead/shadowed classic UI, but covered by neither (043 is the `main()` loop and `main_menu.c`; 044 is the config/info/editor screens). Deleting it (and, if it goes, re-checking `executeBrowserSelection`'s remaining callers, which are all inside `ExecuteMainAction`'s classic dispatch) belongs on the same fate-decision list as RE-LAUNCH-044.
- **CANDIDATE V4-2 (minor) — `tests/test_app_packages.py` is invoked twice in one workflow run.** `.github/workflows/compile.yml:238` (inside the 211-test step of RE-LAUNCH-046) and `:448` (packaging step, after the builds). Harmless but it is the same hand-maintained-enumeration drift surface as RE-LAUNCH-045: the count "211 named tests" is 211 unique files across 212 invocations in the file. Worth one line in any 045/046 fix pass (keep the packaging-step invocation, drop the in-step duplicate, or vice versa).

## Notes on method limits

- All verdicts are READ-level (source at the pin + the patched libsmb2 tree + host execution of the two orphaned Python tests in a `/tmp` copy). No PS2 build or hardware run was attempted; nothing in 037–048 turns on hardware behaviour except RE-LAUNCH-044's reachability, which is labelled INFERRED in the original and remains a source-level (not executed) reachability result here.

---

# Adversarial Verification — Chunk V5: RE-LAUNCH-049 … RE-LAUNCH-060

- Report under test: `~/workspace/Sable-audit-redo/Redo-Launchstation.md`
- Code: `~/workspace/audit-clones-2026-11-13/Launchstation` at pin `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (HEAD verified, read-only)
- libsmb2 patched tree: `/tmp/launchverify/libsmb2` (for RE-LAUNCH-057)
- Method: refute-first; every citation re-opened at the pin, every claimed number recomputed by script/ffprobe/wc.

## Verdict table

| Finding | Verdict |
|---|---|
| RE-LAUNCH-049 | VERIFIED-DRIFT |
| RE-LAUNCH-050 | VERIFIED |
| RE-LAUNCH-051 | VERIFIED-DRIFT |
| RE-LAUNCH-052 | VERIFIED-DRIFT |
| RE-LAUNCH-053 | VERIFIED |
| RE-LAUNCH-054 | VERIFIED-DRIFT |
| RE-LAUNCH-055 | VERIFIED |
| RE-LAUNCH-056 | VERIFIED |
| RE-LAUNCH-057 | VERIFIED |
| RE-LAUNCH-058 | VERIFIED |
| RE-LAUNCH-059 | VERIFIED-DRIFT |
| RE-LAUNCH-060 | VERIFIED-DRIFT |

Counts: **VERIFIED 6 · VERIFIED-DRIFT 6 · WEAKENED 0 · REFUTED 0 · BLOCKED-ENV 0**

---

## RE-LAUNCH-049 — VERIFIED-DRIFT

Core mechanism confirmed. Template header `docs/LAUNCHELF.CNF:20-21` (within the cited :14-26 note) states "This file is only rewritten (without comments) when a classic setting is saved in Settings, or a File Browser bookmark is added or removed" — directly contradicted by `src/main_boot.c:62-66` (own file is `LAUNCHSTATION.CNF`; old `LAUNCHELF.CNF` imported read-only, "saves go to the same folder under our name") and by CHANGELOG Rolling (#346): "Nothing writes `LAUNCHELF.CNF` any more." Dashboard as-live references confirmed at `docs/dashboard.md:46` ("saved in LAUNCHELF.CNF"), `:211-213` ("LAUNCHELF.CNF and IPCONFIG.DAT keep their existing save targets; System's alternate save action writes only LAUNCHELF.CNF"), and `:357` (dashboard files written "Next to LAUNCHELF.CNF when it was loaded").

Corrections to the finding's scope:
- `docs/dashboard.md:478` — cited in Locations — is a **correct** reference, not an as-live one: "not the folder of an imported `LAUNCHELF.CNF`." It must not be replaced.
- `docs/dashboard.md:364` ("LAUNCHELF.CNF itself is never modified behind your back") is literally true after the rename.
- Corrected count: **4 unambiguous as-live references** (46, 211, 213, 357), not 5. Fix otherwise sane and LESS-NOT-MORE (reuses the `main_boot.c:62-66` wording).

## RE-LAUNCH-050 — VERIFIED

One-line confirmation: both extractors read in full (`src/cloud_google.c:61-107,109-122`; `src/dash_tools.c:715-745`); sub-bugs (a) single-backslash end-scan at `cloud_google.c:84-88` overruns a value ending in `\\` into the next field, (b) `\uXXXX` decoded as literal `uXXXX` by `json_string`'s default case (:92-103, traced: `caf\u00e9` → `cafu00e9`) and `releaseJsonString` ends at the first `"` via `strchr` with no escape handling at all, (c) `json_string` truncates to `out_size-1` and still returns 1, (d) both take the first `strstr` match — all traced; every cited caller verified (OAuth :389/:599-612/:642, Drive `id` :744, browse/`wle_sha256` :815-825, updater :760-790/:864/:968).

## RE-LAUNCH-051 — VERIFIED-DRIFT

Mechanism confirmed, headline numbers do not reproduce at the pin. My recount over git-tracked files under `tests/` (273 tracked entries, **214** `.py` files, not 213):

- Files defining exactly `def function` or `def extract`: **99** (83 `function` + 16 `extract`), not 103.
- No plausible pattern reproduces 103: word-containing names (`def extract_function`, `def c_function`, `def production_function`, …) give 105 files; brace-`depth` pattern gives 96.

The qualitative claims all hold: sampled copies (`tests/test_boot_deferred_init.py:9`, `test_copy_pipeline.py:22`, `test_cloud_recovery.py:19`, `test_ps2ips_rpc.py:53` — its own `def function(name, source=server)` variant) are genuinely divergent reimplementations, and **no shared helper exists**: no `conftest.py`, `helpers.py`, or `__init__.py` anywhere under `tests/` (script-verified). Fix (one shared module, migrate as touched, no churn commit) is sane and net-deletion. Corrected headline: **99 of 214**.

## RE-LAUNCH-052 — VERIFIED-DRIFT

The ~900 KB math **does** re-derive, but only via the alignment the finding does not spell out: `FILEINFO` (`include/launchelf.h:560-565`) = `name[256]` + `title[66]` + `sceMcTblGetDir stats`; that struct is 64 B with 64-byte alignment "as in libmc.h" (in-tree evidence: `tests/test_copy_pipeline.py:85`, `tests/test_file_browser_heap.py` harness, and `src/filer_copy.c:746` "FILEINFO's 64-byte alignment"), so `stats` sits at offset 384 and `sizeof(FILEINFO) = 448`; 2048 × 448 = **917,504 B ≈ 896 KiB**, matching the code's own "about 900 KB" comments at all four cited sites. All four cited allocations verified verbatim: `src/filer_actions.c:311` (recursive size), `:1984` (`MAX_ENTRY + 1`, recursive delete), `src/filer_browser.c:2010`, `src/filer_copy.c:915`; `-ENOMEM` plumbing present at both recursive sites; the `getDirComplete(..., &more)` chunk contract exists at `src/filer.c:2224-2244`.

Correction — the site count is understated, so the true scope is **larger**, not smaller: a tree-wide grep finds **10** full-`MAX_ENTRY` `FILEINFO` heap allocations in `src/*.c`, not four. The six uncited ones: `src/filer_actions.c:404` (`timestamp_folders`), `src/dash_browser.c:559` and `:3376`, `src/dash_lib.c:890`, `src/dash_themes.c:990`, `src/dash_widgets.c:1005`, and `src/self_update.c:106` (that is six additional sites beyond the cited four in the filer files, plus the filer_copy conditional). Any right-sizing fix should include the dashboard allocations, several of which are long-lived job buffers rather than per-level listings.

## RE-LAUNCH-053 — VERIFIED

One-line confirmation: identical `static sceMcTblGetDir mcDir[MAX_ENTRY + 1] __attribute__((aligned(64)))` at `src/filer.c:480` (`readMC`) and `:544` (`readXFROM`), 2049 × 64 B = 131,136 B BSS each; the sole entry path `getDirComplete` → static `listDir` (`src/filer.c:1919`) dispatches them as mutually exclusive `mc*`/`xfrom*` branches, neither reader calls any listing function, and both block on `mcSync`/`xfromSync` and copy fully into the caller's `info[]` before returning — no nested or concurrent use found anywhere in `src/`.

## RE-LAUNCH-054 — VERIFIED-DRIFT

All numbers reproduce exactly: GIFs 4,885,148 B + 2,894,342 B = **7,779,490 B**; MP4s 597,007 B + 337,371 B = **934,378 B**; ratios 8.18× and 8.58× ("8–9×" ✓); `docs-site` total **17,359,939 B** (both `du -sb` and per-file sum); ffprobe durations identical (theme 24.000 s vs 24.000 s; application-views 12.500 s vs 12.450 s); the offline ZIP does ship the same assets (`docs-site/build.py` `--zip`, `.github/workflows/docs-pages.yml:36-43`).

Corrections — the fix's deletion list is incomplete:
- There is a **4th GIF link mention** the finding misses: `docs-site/content/index.html:14` links `theme-highlights.gif` beside the embedded MP4. Deleting "their 3 link mentions" leaves it dangling.
- `docs-site/static/assets/ui/capture-manifest.json` also carries entries for both GIFs (application-views at ~:255, theme-highlights at ~:460, with `bytes`/`sha256`) — not mentioned in the fix.
- Minor framing drift: in `README.md:68` the theme GIF is not a "secondary link beside an embedded MP4" — it is the README's embedded illustration itself, with no MP4 beside it. Deleting it there is a content decision, not just a link cleanup.

Net-saving claim (−7,779,490 B, −2 files) is otherwise exact.

## RE-LAUNCH-055 — VERIFIED

One-line confirmation: file sizes exact (591,973 + 492,279 + 555,019 = **1,639,271 B**), all three headers say "GENERATED by scripts/gen_dash_font.py, do not edit" at line 2, `docs/font-generation.md` confirms the TTFs are not redistributed (hashes given only for the two Emotion Engine TTFs — Roboto's is not even recorded, which strengthens the point), `gen_dash_font` appears in tests only at `tests/test_font_weight.py:16` as a parse of the generator and in no workflow regeneration/comparison step, while the sibling audio pipeline does have `scripts/gen_sfx.py --check`; the proposed hash-recording fix in `docs/font-generation.md` + `tests/test_maintained_documentation.py` is the LESS-NOT-MORE option the finding itself selects.

## RE-LAUNCH-056 — VERIFIED

One-line confirmation: `wc -l` gives 413 + 168 = **581 lines**, `IRX_ID("mmcedrv", …)` at `iop/mmceman/src/mmcedrv.c:19`, the mmceman target's source list at `ps2.yaml:153-162` has exactly the other 6 sources, neither file is `#include`d by any compiled source (the `#include`-compiled siblings `usmb2.c`/`unfs4.c` prove what that pattern looks like in this tree), and neither filename appears in any other `*.yaml`/`*.sh`/`*.md`/`*.txt`; census anchor also reproduces (26 targets in root `ps2.yaml`).

## RE-LAUNCH-057 — VERIFIED

One-line confirmation: on the patched tree at `/tmp/launchverify/libsmb2`, `wc -l` gives quic.c **342**, smbdirect.c **1,021**, sync.c **1,274**, smb2-share-enum.c **1,005**; the bodies sit under `#ifdef HAVE_LIBQUIC` (quic.c:34) and `#ifdef HAVE_LIBRDMACM` (smbdirect.c:50), neither macro is defined in `iop/smbd/config.h` nor in the smbd target's `defines` in `ps2.yaml`; the source-list citations are exact (`ps2.yaml:315` quic, `:316` smbdirect, `:344` share-enum, `:349` sync) and the smbd `ldflags` at `ps2.yaml:358` do carry `-Wl,--gc-sections`; grep of `iop/smbd/*.c` finds no caller of any `smb2_share_enum*` or `*_sync` client symbol.

## RE-LAUNCH-058 — VERIFIED

One-line confirmation: my own grep of `src/` + `include/` finds zero production callers of `selfUpdateExtractPsu` (`src/update_psu.c:183`), `selfUpdateExtractPsuMem` (:201), `selfUpdateExtractPsuSidecar` (:335), or `cloudHttpDownloadFd` (`src/cloud_http.c:1280`, "Historical self-update interface" comment at :1279) — production uses only `selfUpdateLocatePsuElf` + `selfUpdateExtractPsuSidecarMem` (`src/self_update.c:306,327,333`) and `cloudHttpDownloadMem`/`cloudHttpDownloadGoogleFd`, which call `cloudHttpDownloadFdInternal` directly; the only callers of the dead four are the host harness embedded in `tests/test_app_packages.py`. (Deletion cost the finding understates, not a defeater: see Candidate C2.)

## RE-LAUNCH-059 — VERIFIED-DRIFT

Mechanism fully confirmed across all six workflows. `compile.yml` holds top-level `contents: write` (:15-16) yet uses mutable tags at exactly the cited lines (:29 checkout, :285 cache/restore, :301 cache/save, :513/:523 upload-artifact, all `@v4`); `docs-pages.yml:24,46`, `sync-wiki.yml:29` (also `contents: write`), and `ps2build-sdk-watch.yml:42,57,70` (plus further `@v4` uses at :73,85,107,117,147,151, including its own `contents: write` job at :135-136) likewise use tags; `neutrino-watch.yml` uses no actions at all.

Single citation correction: in `download-metrics.yml` the pin is not at :19-20 — those lines are the job's `permissions:` / `contents: write`; the comment "Pinned because this job holds contents: write." is at **:22** and `actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4` at **:23**. The quoted comment text and SHA are exact. Fix (apply the repo's own stated practice to the writing workflows) is sane, no new policy.

## RE-LAUNCH-060 — VERIFIED-DRIFT

Mechanism confirmed by running the script's own patterns: `grep -rn "LNCHELF\.CNF"` over the script's scan roots (`src include iop loader .github/workflows ps2.yaml`) returns **nothing**, so the §1 legacy scan (`scripts/stale_code_audit.sh:21`) and the `--strict` gate (:37) can never fire on the legacy filename; §3 (:27) probes only `src/main.c`/`src/config.c`, whose only `LAUNCHELF.CNF` hits are comments (`src/config.c:530,880`), while the sole literal `"LAUNCHELF.CNF"` string in `src/` is at `src/main_boot.c:66`, never probed; no workflow references the script (grep of `.github/` empty — the docs themselves concede "CI does not run this"); script is 41 lines; doc citations at `docs/architecture-maintenance.md:72,83` exact.

Correction to one phrase: "a string that occurs nowhere" is true only **within the scan roots**. Repo-wide, `LNCHELF.CNF` occurs once — at `docs/changes.txt:619`, as the genuine historical 8+3 filename alias ("renamed LNCHELF.CNF (or LNCHELF1.CNF…)") — in a tree the script does not scan. This does not rescue the script (its roots exclude `docs/`, and the name it presumably meant to catch is `LAUNCHELF.CNF`), but the pattern is better described as "matches nothing the script scans" than as a pure invented typo. Fix (correct the pattern + probe list, or delete script + citation) remains sane either way.

---

## Same-mechanism pairs within RE-LAUNCH-001…092

- **049 ↔ 060 (cross-reference, not a duplicate):** both are fallout of the #346 `LAUNCHELF.CNF` rename — 049 is stale docs/template, 060 is the maintenance script that should have caught exactly that staleness and cannot. Distinct mechanisms, worth cross-linking.
- **052 ↔ 053 (related, distinct):** both are listing-buffer memory (heap `FILEINFO` per listing/level vs static `sceMcTblGetDir` BSS per reader). Different allocations, different fixes; neither subsumes the other.
- **056 / 057 / 058:** all Category-1 dead code, but three disjoint code sets with three different proofs (unlisted IOP sources / empty-by-`#ifdef` + client-only vendored TUs / caller-less EE functions). No overlap.
- No other pair in 001…092 shares a mechanism with any finding in this chunk. (049 vs 063 both touch `docs/LAUNCHELF.CNF` but different defects: rename propagation vs the Triangle-key self-contradiction.)

## CANDIDATES (new defects / costs found during verification)

- **C1 — Orphan export table for the dead driver (companion to RE-LAUNCH-056).** `iop/mmceman/src/exports.tab` declares an `mmcedrv` export table (`DECLARE_EXPORT(mmcedrv_get_size/read_sector/config_set/read/write/lseek)`) for the uncompiled `mmcedrv.c`. The mmceman target in `ps2.yaml` has no `exports_tab` field (unlike udpfs_smap/vmcman/ata_bd), and no compiled source calls any `mmcedrv_*` symbol. If RE-LAUNCH-056's files are deleted or moved to `research/`, `exports.tab` must go with them or it becomes a third unexplained orphan.
- **C2 — RE-LAUNCH-058's deletion removes a test oracle and breaks a guard test (unmentioned cost).** `tests/test_app_packages.py`'s harness uses the file-based extractors as the byte-for-byte reference the RAM variants are asserted to agree with ("The file and RAM readers must agree byte for byte, including failures"); deleting them removes that cross-check, not just dead weight. Separately, `tests/test_self_update.py:13-14` asserts `cloudHttpDownloadFd` is **present** in `include/cloud_http.h` and `src/cloud_http.c`, so the deletion fails CI until that guard is also edited. Neither defeats the finding (no production caller exists), but the fix as written ("keep the test harness exercising the RAM path") understates both edits.
- **C3 — RE-LAUNCH-054 scope extension (same mechanism, listed here for the fixer's checklist).** Beyond the drift items in the 054 row: `capture-manifest.json` records `bytes` + `sha256` per asset, so deleting the GIFs without regenerating the manifest leaves it internally inconsistent; and the README embed at `README.md:68` needs a replacement illustration decision, not just link removal.

---

# Adversarial verification — chunk V6: RE-LAUNCH-061 … RE-LAUNCH-072

- Source report: `~/workspace/Sable-audit-redo/Redo-Launchstation.md` (sections 061–072 only)
- Code pin: `~/workspace/audit-clones-2026-11-13/Launchstation` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (verified `git rev-parse HEAD`; clone read-only, nothing written into it)
- Method: refute-first. For each finding the cited doc text was quoted back from the file, and the code/config evidence was re-derived independently (embed lists read in full, CA bundle counted with openssl, byte totals re-added from `stat`, reference censuses re-grepped, workflow wiring re-grepped).
- Counts: **VERIFIED 7 · VERIFIED-DRIFT 3 · WEAKENED 2 · REFUTED 0 · BLOCKED-ENV 0**

| ID | Verdict |
| --- | --- |
| RE-LAUNCH-061 | VERIFIED |
| RE-LAUNCH-062 | VERIFIED |
| RE-LAUNCH-063 | VERIFIED |
| RE-LAUNCH-064 | WEAKENED |
| RE-LAUNCH-065 | VERIFIED |
| RE-LAUNCH-066 | VERIFIED |
| RE-LAUNCH-067 | VERIFIED |
| RE-LAUNCH-068 | VERIFIED-DRIFT |
| RE-LAUNCH-069 | VERIFIED-DRIFT |
| RE-LAUNCH-070 | VERIFIED |
| RE-LAUNCH-071 | VERIFIED-DRIFT |
| RE-LAUNCH-072 | WEAKENED |

---

## VERIFIED (one-line confirmations)

- **RE-LAUNCH-061 — VERIFIED.** All three sub-claims re-derived: `res/README.md:8` lists `boot` among `sfx/*.adp` "Embedded by src/embed_blobs.S", but `src/embed_blobs.S:82-88` embeds only 7 classic files (cursor/confirm/cancel/transition/message/bd_connect/bd_disconnect, no boot) and the only embedded boot is `res/sfx_ps2/boot.adp` at `:96`; `res/sfx/boot.adp` (143,984 B, `stat`-confirmed) is referenced by no current build file (grep across `.S/.py/.yaml/.yml/.sh`/Makefiles finds only the historical 2026-09-26 plan). `res/README.md` has no `res/sfx_ps2/` row at all, while all 8 PS2 files are embedded at `embed_blobs.S:91-98` and gated by `tests/test_sfx_ps2.py`. CA bundle: `grep -c "BEGIN CERTIFICATE"` = 3, and openssl subjects are Sectigo R46, ISRG Root X1, **and Sectigo E46** — `res/README.md:9` names only the first two; `res/UPDATE-CA.md`'s table lists all three. Fix (edit 2 rows, add 1) is LESS-NOT-MORE sane.
- **RE-LAUNCH-062 — VERIFIED.** Census re-run: exact-path grep for `exploits/IOPRP.IMG` and `exploits/XSYSTEM2.XLF` returns zero references anywhere; `res/bgm.ogg` is referenced only by its own `res/README.md:15` ("Not referenced by any build script and not packaged"), `docs/THIRD_PARTY_CREDITS.md:68` ("not packaged or embedded") and historical plans. Sizes re-measured: 462,185 + 15,741 + 13,896 = **491,822 B**, exact. `exploits/README.md:26-27` self-documents the deadness; `embed_blobs.S:70` embeds only `IOPRP_LTS.IMG` and `:54` slices `XSYSTEM.XLF` (not XSYSTEM2); the packaged BGM is `res/music.sad` (`scripts/package_apps.py:37,44`). The bare name `IOPRP.IMG` in `README.md:96` / `ship/KIT-README.txt:30` / `docs-site/content/install.html:25` is **not** a defeating reference: those passages require a user-supplied POPStarter file and state such files "are not included" — they do not consume the file in `exploits/`. Deletion fix sane.
- **RE-LAUNCH-063 — VERIFIED.** `docs/LAUNCHELF.CNF:79-80` comment: "Circle starts the file browser … and Triangle shows About"; `:88` sets `LK_Triangle_E1 =` (empty). The override mechanism was verified in code, not assumed: the CNF parser at `src/config.c:960-963` does `strcpy(setting->LK_Path[i], value)` for any `LK_*_E` key, so the empty template value overwrites the code default set at `src/config.c:828-829` (`LK_Path[SETTING_LK_TRIANGLE] = Misc_About_uLE`, flag 1). `docs-site/content/configuration.html:37` likewise gives "Triangle = About" as the default. Proposed fix value `MISC/About uLE` matches `src/config.c:813` (`Misc` + "/" + `About_uLE`) and `docs/changes.txt:203`. One line either way — sane.
- **RE-LAUNCH-065 — VERIFIED.** `CHANGELOG.md:16` verbatim: "(Settings > Network > SMB shares, Server type **NFS**, with User ID and Group ID rows)". Code has a dedicated NFS page: `src/dash_settings.c:746` `{ST_PAGE, P_NFS, "NFS shares", …}` in `net_items[]`, dedicated editor array `nfs_items[]` at `:717`, and the SMB editor itself says at `:705` "NFS shares have their own page: Settings > Network > NFS shares." `README.md:164` and `docs-site/content/nfs.html:8` already give the correct location. (The same CHANGELOG sentence's "saved with the SMB shares at the end of LAUNCHSTATION.CNF" is storage-accurate — only the settings location is stale.) One-parenthetical replacement — sane.
- **RE-LAUNCH-066 — VERIFIED.** Every quoted stale string is present in `docs-site/content/ecosystem.html:7`'s page body: manager cell "No 3D icons and no "All saves" view", copy cell "one save at a time", Suggested-order item 2 lists "All saves" as *next* work and item 3 calls templates "later work that needs design decisions". The markdown twin contradicts all of it: `docs/ECOSYSTEM-COMPARISON.md:30-31` documents the shipped **All saves here...** view and batch copy/move/delete via Square-marked saves (#232), and its Suggested order (lines 82-84 in the full list) states All-saves/batch and templates (sets) "are in (#232)". Feature confirmed in code at the cited line: `src/dash_browser.c:2808` `ADD(DI_MC, "All saves here...", … F_ALL_SAVES)`. `docs/README.md:25` classifies ECOSYSTEM-COMPARISON.md as Reference (current). Precise split is 2 stale table cells + 2 stale Suggested-order items (the HTML Templates *table* cell is already #232-current) — the finding's "3 stale cells + closing list" grouping is loose, the substance is exact. Replace-not-add fix sane.
- **RE-LAUNCH-067 — VERIFIED.** `docs/follow-ups/move-manifest.md:3` verbatim: "Implemented on the draft PR branch; not merged. NOT TESTED on hardware." — defeated by HEAD code: `src/filer_copy.c:124` `#define MOVE_MANIFEST_LIMIT`, `:143` `filerMoveBegin`, plus host suite `tests/test_move_manifest.py`. The three Nov-4 files (`docs/superpowers/plans/2026-11-04-hide-tabs.md`, `docs/superpowers/specs/2026-11-04-boot-defer-design.md`, `2026-11-04-hide-tabs-design.md`) open with title/content and **no status banner**, while the Sept-26 superpowers files and every `docs/plans/2026-10-*.md` carry a "Status as of 2026-10-26" banner. Both Nov-4 features are in code: hide-tabs at `src/dash_themes.c:131` (`tabs_ids`) / `include/dash_internal.h:187` (`enum DashTabs`), and boot-defer exactly as specced at `src/dash_main.c:1505-1513` (deferred `dashConsoleIdentityInit()` + `dashSoundInit()` + `dashBgmStart()` once `fade_in > 0.95f`). `docs/README.md`'s index lists superpowers items only up to the 2026-09-26 pair (lines 66-67) and contains no `docs/follow-ups/` entry at all (grep: zero). Banner + one index line — sane, no new documents.
- **RE-LAUNCH-070 — VERIFIED.** `docs/dashboard.md:372` verbatim: "They are Sony ADP samples embedded in the ELF" — a blanket sentence covering both embedded sets. `docs/dashboard.md:380` itself says the PS2 set is "synthesized by scripts/gen_sfx.py (original tones, not Sony samples)"; `scripts/gen_sfx.py:6`: "Nothing is sampled from Sony firmware"; `res/README.md:17-18` (final paragraph): "The origin and license of the audio and icon artwork are not recorded in this repository" for the classic set. So the claim is denied for one set and unrecorded for the other. Fix replaces `:372` with the two-set description already at `:380` — delete/replace, LESS-NOT-MORE sane. (See candidate V6-C2: the same provenance wording also lives in a code comment.)

---

## Non-VERIFIED — full reasoning

### RE-LAUNCH-064 — WEAKENED (true scope: an ambiguous provenance sentence, not a flat shipped-driver claim; the proposed one-word fix would create a new error)

What checks out: `docs-site/content/smb.html:86` does say "using the SMB2 driver from [libsmb2]", and for the `smb:` **client** device that page is about, the shipped driver is not libsmb2: `iop/smb2man/smb2man.c:9-11,199` compiles Ronnie Sahlberg's **usmb2** (`#include "usmb2/usmb2.c"`), `docs-site/content/credits.html:17` states it "replaced the prebuilt libsmb2 smb2man.irx", and `docs-site/content/architecture.html:4` lists "smb2man (SMB2/SMB3, usmb2)". The page's worked examples at `smb.html:45,51` do use password-shaped example values with no "example only" marker (location-only note in the finding confirmed; values not reproduced here).

Why it is weakened, two defeating nuances the finding misses:

1. **The sentence is a provenance sentence about the upstream origin, and on that reading it is historically true.** Full text of `:86`: "The `smb:` device and the `SMB2.CNF` format **come from** Ronnie Sahlberg's work **for saildot4k's upstream launcher**, using the SMB2 driver from libsmb2." The "using" clause grammatically attaches to Sahlberg's upstream work — and credits.html:17 confirms the upstream wLaunchELF_R3Z PR #63 driver *was* the prebuilt libsmb2 `smb2man.irx`, replaced only in this fork. The page never says in so many words "the shipped driver is libsmb2".
2. **libsmb2 is in fact a shipped driver in this repo — for a different SMB component.** It is the protocol stack of the IOP SMB **server** `iop/smbd` (memory-card sharing): `iop/smbd/smbd_fs.c:3` "libsmb2 (Ronnie Sahlberg, LGPL-2.1) does the protocol", `iop/smbd/config.h:1`, `scripts/fetch_libsmb2.sh`, `CHANGELOG.md:25` (SMB server "Built on … libsmb2"), `src/dash_info.c:166` "libsmb2 (SMB server for memory cards)". The task's hypothesis that `smb.html`'s libsmb2 might refer to an EE-side client does **not** hold either: there is no EE libsmb2 client; the EE side talks to the IOP `smb2man`. But the blanket framing "the shipped driver is source-built usmb2" is itself incomplete — there are two shipped SMB2 stacks.

True scope: a present-tense, unqualified provenance sentence on the user-facing client page that a reader will naturally take as describing the current client driver, and which contradicts the same site's Credits/Architecture pages for that client. Worth fixing — but **not** by the finding's one-word swap (`libsmb2` → `usmb2`), which would falsify the upstream-origin half of the sentence. Correct LESS-NOT-MORE fix: reword the provenance, e.g. "…upstream launcher, whose SMB2 driver was libsmb2; this fork's smb2man is source-built usmb2 (see Credits)" — one sentence replaced, none added.

### RE-LAUNCH-068 — VERIFIED-DRIFT (cited counts exact; the file census is incomplete — a second, larger omission means the proposed fix still leaves the sentence wrong)

Verified part: `docs/architecture-maintenance.md:89-91` ("Refactoring history and next splits") lists "src/dash_settings.c, src/dash_browser.c, src/init.c, src/main_exploit_installer.c and src/filer_browser.c". Re-run `wc -l`: dash_settings 4,096; dash_browser 3,616; init 3,205; **dash_card_tool 2,903**; main_exploit_installer 2,519; filer_browser 2,528 — every cited number exact, and `dash_card_tool.c` is indeed omitted while exceeding both tail entries.

Drift: a full `wc -l src/*.c | sort -rn` census (excluding the generated font-data files the doc itself excludes) shows **`src/filer.c` at 2,556 lines also exceeds both tail entries (2,528 / 2,519) and is also omitted** — `src/filer.c` is an ordinary hand-written source (legacy file header, no generation notice). Corrected top five: dash_settings (4,096), dash_browser (3,616), init (3,205), dash_card_tool (2,903), filer (2,556). The finding's fix ("add dash_card_tool.c to the sentence") therefore does **not** make the list correct; the sentence needs dash_card_tool **and** filer added, with main_exploit_installer / filer_browser dropping out of a five-item list. See candidate V6-C1.

### RE-LAUNCH-069 — VERIFIED-DRIFT (generator claim fully verified; the prerequisite-divergence sub-claim is overstated in one direction)

Verified part: `docs/BUILDING.md:189` (final line) verbatim: "This file is generated from `docs-site/content/building.html`; keep the two in sync." No generator exists: grep for `BUILDING` across `scripts/*.py`, `docs-site/build.py` and `.github/workflows/compile.yml` returns nothing that writes it (build.py contains no BUILDING reference at all); `tests/test_maintained_documentation.py:7-16` only reads both files and asserts shared substrings in each. Lengths re-measured: BUILDING.md 14,746 chars vs building.html 17,663 chars (the finding's "17,663 vs 14,746" values are exact, order = html vs md). The actionable divergence is also real both ways for the SDK installer: README.md:196 requires GNU `timeout` and `setsid`, which appear in **neither** BUILDING.md nor building.html (grep count 0), yet `scripts/ps2build_sdk.sh` genuinely uses them (`timeout` at `:383,425`, `setsid` at `:426`); and BUILDING.md:27 requires `sha256sum`, which README.md:196 omits, yet the installer uses it (`ps2build_sdk.sh:89,369`).

Drift: the finding's claim that BUILDING.md "requires … host gcc/NumPy, which README.md:196 omits" is true only of the `:196` list in isolation. BUILDING.md:27 itself scopes gcc/NumPy to *running the test suite*, and README covers exactly that separately at `README.md:256` ("Host tests use Python 3.11+, GCC and NumPy"). So that leg is a difference of document structure, not a missing prerequisite. Corrected fix scope: change `:189` to "kept in sync manually with" (one line — sane) and add `timeout`/`setsid` to BUILDING.md's tool list (and `sha256sum` to README.md:196 if the lists are to match); no gcc/NumPy change is needed.

### RE-LAUNCH-071 — VERIFIED-DRIFT (folder-local claim exact; two citation drifts, and repo-wide "cannot tell" is overstated)

Verified part: `__Headers/README.MD` is exactly one line, "# APA INJECTON SAMPLES" [sic — INJECTON misspelled], and nothing inside the folder names a consumer, a usage, or a provenance. The consumer chain checks out: file specs in `src/hdd_header_injector.c` (table at `:69-74`), driven from `src/dash_hdd.c:800` ("Write the APA header of hdd0:%s from %s__Headers/%s/? …"). The sets are for partitions/apps other than Launchstation itself. Expanding this one README to the 4–5-line folder norm is sane and is a replacement of a one-liner, not a new document.

Drifts: (1) the fourth set is **`PP.FHDB.APPS`**, not "PP.FHDB" as the finding names it (directory listing: `PP.FHDB.APPS`, `PP.GSM`, `PP.OPL`, `PP.ULE`, each holding icon.sys/list.ico/system.cnf). (2) The header file-spec table is at `src/hdd_header_injector.c:69-74`, not `:71-72` (those are two rows of it). (3) Scope: the problem statement's "a user browsing the repo cannot tell these are samples" is folder-local only — elsewhere the repo *does* say so: `docs-site/content/architecture.html:11` calls `__Headers/` "sample APA injection headers" and `docs-site/content/hdd-manager.html:2,27,31` explains the header-source picker and the injection in full. The defect is that the folder itself — the thing a user copies onto a device — carries none of that.

### RE-LAUNCH-072 — WEAKENED (never wired and no self-provisioning path — verified; "cannot run unattended" is overstated)

Verified part: grep for `nfsman_live` over `.github/workflows/`, `docs/` and `README.md` returns **zero** references (the only hits in the repo are the suite's own files). Its SMB siblings are CI steps at `.github/workflows/compile.yml:40-48` (`tests/smb2man_live/run.sh` at `:40-44`, `tests/smbd_live/run.sh` at `:47-48`). The Python-side coverage `tests/test_nfs_shares.py` does run in CI (`compile.yml:83`, parser/wiring only). `iop/nfsman` is a shipped target (`ps2.yaml:278-286,660`). Default mode of `tests/nfsman_live/run.sh` requires root plus a running kernel NFSv4.0 server (`exportfs` presence and `/proc/fs/nfsd/versions` `+4.0` checks at the top of the script, exiting 2 otherwise).

Weakening: "cannot run unattended" is too strong. The script's 3-argument mode (`run.sh <server-ip> <export> <dir-in-export>`, documented in its own header) is fully non-interactive against a pre-existing export — nothing in it prompts. What is true is narrower: it has **no unattended self-provisioning path** (unlike the smb2man CI step, which apt-installs Samba and runs under sudo) and **no automation invokes it in any mode**, so in practice it never runs. True scope: "wired into no workflow and, as shipped, never runs; running it requires either root + a provisioned kernel NFSv4.0 server or a pre-existing export passed as arguments." The finding's fix options (wire it where a root NFS server is available, or retire it and let `test_nfs_shares.py` be the stated coverage) remain sane under the corrected scope.

---

## Same-mechanism / pair flags (within 001–092 scope checked for this chunk)

- **061 ↔ 062 — related, not duplicates.** Both touch `res/` inventory truth: 061(a) is the false *embedded* claim for `res/sfx/boot.adp`; 062 is the dead-payload census (`bgm.ogg` + the two exploit files — boot.adp is not in 062's total). Fixing 062 by deletion does not fix 061's row, and vice versa.
- **061 ↔ 070 — related theme, distinct statements.** Sound-set inventory/provenance in two different documents (`res/README.md` vs `docs/dashboard.md:372`); the PS2-set facts (embedded, synthesized) are the shared evidence base. See also candidate V6-C2, a third location with the same provenance wording.
- **066 ↔ 067 ↔ 068 ↔ 069 — one drift class, four distinct mechanisms/files.** All are hand-maintained doc/status layers falling behind code (site-HTML mirror behind its markdown twin; plan status banners/index; a hand-written largest-files list; a manual sync mislabelled as generation). No two share a fix location; none is a duplicate. 066 and 069 are mirror-image cases (HTML behind MD; MD falsely claimed generated *from* HTML).
- **064 ↔ CHANGELOG.md:25 / iop/smbd — not a pair.** libsmb2 is *correctly* credited there for the SMB server; that correctness is precisely what makes 064's scope narrower than stated (see reasoning above).
- **072 ↔ report coverage note (Redo-Launchstation.md:701)** — consistent: the report's own "live suites not run" note already distinguishes the CI-wired first two suites from `nfsman_live`.

## CANDIDATES (new defects found while verifying — not in 061–072)

- **V6-C1 (from RE-LAUNCH-068): `src/filer.c` (2,556 lines) is a second omission from architecture-maintenance.md's largest-files list.** It exceeds both listed tail files (`src/filer_browser.c` 2,528; `src/main_exploit_installer.c` 2,519), is hand-written, and is absent from the `docs/architecture-maintenance.md:89-91` sentence. Any fix for 068 that adds only `dash_card_tool.c` leaves the sentence still wrong; the corrected five are dash_settings / dash_browser / init / dash_card_tool / filer.
- **V6-C2 (pair with RE-LAUNCH-070): the "Sony ADP" provenance wording also lives in code at `src/embed_blobs.S:81`** — comment: "Dashboard UI sounds (Sony ADP, played through the SDK audio driver via audsrv-compat)" over the classic-set embeds at `:82-88`. This is the likely source of dashboard.md:372's sentence. 070's fix edits only the markdown; the same unrecorded-provenance claim (classic-set origin is "not recorded", per `res/README.md:17-18`) survives in the build file comment. One-word/one-line correction in the same pass ("PS2 ADPCM (.adp)" for the format, dropping the provenance claim) would close the mechanism, not just one instance.

---

# Adversarial Verification — Chunk V7: RE-LAUNCH-073 … RE-LAUNCH-084

- Source report: `~/workspace/Sable-audit-redo/Redo-Launchstation.md` (sections 073–084 only)
- Code: `~/workspace/audit-clones-2026-11-13/Launchstation` at pin `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (detached, clean; read-only — no writes into the clone; the one test executed, `test_transition_latency.py`, only reads `res/sfx/transition.adp`)
- Method: refute-first. Every citation re-read at the pin; every duplication claim re-derived by reading both/all copies in full and diffing semantics, not shapes.
- Counts: **VERIFIED 4 · VERIFIED-DRIFT 5 · WEAKENED 3 · REFUTED 0 · BLOCKED-ENV 0** (12 findings)

| ID | Verdict | One-line summary |
|---|---|---|
| RE-LAUNCH-073 | VERIFIED-DRIFT | Duplication and CI dual-build true and definitions agree today; pccf is not assertion-free — it has a toolchain-only check, and the ps2ips "sync" check is itself partial. |
| RE-LAUNCH-074 | VERIFIED | All version literals, counts and the clean ps2build contrast check out exactly. |
| RE-LAUNCH-075 | VERIFIED | Research-model test is CI-run and its own header/doc say not implemented on device. |
| RE-LAUNCH-076 | VERIFIED-DRIFT | Conditional-skip mechanism true; kit line numbers drift and the "both run on current CI" claim is not supported for the kit suite — per the workflow it likely already does not run there. |
| RE-LAUNCH-077 | WEAKENED | Facts all true, but it is a non-finding: no defect observed, no fix proposed, risk self-rated INFERRED. |
| RE-LAUNCH-078 | WEAKENED | Three-copy retry duplication true; the proposed fix cannot replace the MX4SIO copy as stated (multi-path probing the helper does not support). |
| RE-LAUNCH-079 | VERIFIED-DRIFT | Three 16-entry blocks, the `config.c:814` LNG/LNG_DEF divergence and the load-ordering constraint all verified; lang lines off by one and the load side cannot be driven by the proposed LNG_DEF loop alone. |
| RE-LAUNCH-080 | VERIFIED-DRIFT | Duplication true, but it is 2 sites / 10 explicit assignments (not "three times" / 11 fields), ~50 lines apart, and the stated fix cannot be implemented as written. |
| RE-LAUNCH-081 | VERIFIED | Three IPv4 implementations with exactly the claimed divergent semantics, including the octal/leading-zero hazard and the Neutrino re-parse/write path. |
| RE-LAUNCH-082 | WEAKENED | BCD bodies are byte-identical and the date algorithms differ, but the stated fix breaks non-Ethernet builds — the SNTP copies live inside `#ifdef ETH`, the manual copies deliberately outside it. |
| RE-LAUNCH-083 | VERIFIED-DRIFT | ABI blocks are copies (structs/htons identical modulo names), but cloud is a superset and the SNTP `htons` is at `:95`, not in the cited `:22-48` block. |
| RE-LAUNCH-084 | VERIFIED | Self-diffed: md5s differ, exactly one functional divergence (PORT_NR) plus trailing whitespace. |

---

## RE-LAUNCH-073 — VERIFIED-DRIFT

- Root vs per-module definitions agree today, semantically: root `ps2.yaml` `pccf` target (sources `iop/pccf/main.c, pccf_hw.c, pccf_ata.c, pcdiag_cis.c, pcdiag_cf.c`, `defines: [PCCF_SETTLE_US=1000]`, `cflags: [-Wall, -fno-strict-aliasing]`, headers `[bdm, iomanx, loadcore, stdio, sysclib, threadman]`) matches `iop/pccf/ps2.yaml` (10 lines) field-for-field once the necessary relative-path prefixing is normalised (`iop/pccf/main.c` vs `main.c`, `[iop/pccf]` vs `[.]`). Same for `ps2ips_rpc`: root (`sources: [iop/ps2ips_rpc/ps2ips.c]`, `defines: [PS2IP_DNS]`, headers `[intrman, loadcore, ps2ip-nm, sifcmd, sifman, stdio, sysclib, threadman]`) vs `iop/ps2ips_rpc/ps2.yaml`. Both per-module files and the root all say `toolchain: llvm`.
- CI builds both forms — verified in `.github/workflows/compile.yml`: `ps2build build -c iop/pccf/ps2.yaml` (line 339), `ps2build build -c iop/ps2ips_rpc/ps2.yaml` (line 342), plus the root `ps2build build` (line ~348).
- Drift 1 — pccf assertion absence is overstated if read as "no assertion at all": `tests/test_llvm_toolchain.py:12-13,21-22` reads `iop/pccf/ps2.yaml` and asserts `toolchain: llvm` in both it and the root. What pccf lacks is the *target-field* sync pattern, so the finding's word "equivalent" saves the claim, but the record should state the toolchain-only check exists.
- Drift 2 — the ps2ips pattern being copied is itself partial: `tests/test_ps2ips_rpc.py` `check_build_config()` plus the isolated-file asserts (lines 44–49) check `type`, `sources`, `imports_lst`, `defines` and the workflow line, but **not** `headers`, `cflags`, `include_dirs` or `toolchain` for either definition (isolated toolchain is checked separately in `test_llvm_toolchain.py`). Adding pccf "to the same pattern" therefore still leaves those fields unsynchronised for both modules.
- Fix: sane and LESS-NOT-MORE compliant — one assertion reusing an existing pattern, no new mechanism. Ideally it should compare the full field set above, not only the fields the ps2ips test happens to check.

## RE-LAUNCH-074 — VERIFIED

One-line confirmation: `scripts/build_cloud_tls.sh:6` is `version="3.6.7"` and `mbedtls-3.6.7` occurs exactly four times in `tests/test_cloud_tls_paths.py` at lines 30, 47, 54, 64; `scripts/build_jpeg.sh:14` is `version="3.1.3"` and `tests/test_llvm_toolchain.py:37` asserts the literal `'version="3.1.3"'` in that script; the contrast is clean — `.github/ps2build-sdk.env` is the single source for the SDK pins and `.github/scripts/test_ps2build_sdk_watch.py` machine-checks them and runs at `compile.yml:245`. Fix is deletion-only (both tests already read the scripts they restate) and passes LESS-NOT-MORE.

## RE-LAUNCH-075 — VERIFIED

One-line confirmation: `tests/test_mmce_restore_txn_model.py:1-5` docstring says verbatim it tests "a host-only model, not existing sd2psXtd firmware and not any PS2 restore operation"; `research/sd2psxtd/mmce_restore_txn_model.h:13` is `#define MMCE_TXN_IMPLEMENTED_ON_DEVICE 0`; `docs/MMCE_RESTORE_PROTOCOL.md` states "No firmware transaction exists upstream or in this repository"; and the test **is** in the CI list at `compile.yml:211`, unlike the orphan suites elsewhere. Fix (reclassify/move out of per-commit CI, no new test) is sane.

## RE-LAUNCH-076 — VERIFIED-DRIFT

- FTP half verified: `tests/test_ftp_server.py:22` gates the freestanding 32-bit formatter compile on `linux` + `x86_64/i386/…` target, and the `else` at `:44-45` prints `SKIP: freestanding x86 check is unavailable for {target}` and continues; the remaining FTP checks still run and can fail, and the suite reports overall PASS. On the CI runner (`ubuntu-latest`, x86_64, `compile.yml:26`) this branch does run.
- Kit half — line drift: the conditional branches are at `tests/test_kit_package.py:165` (`if py7zr is not None or shutil.which("7z") or …7za… or …7zz…`) and `:207` (`if py7zr is not None and not (shutil.which("7z") or …)`), with the `import py7zr` attempts at `:162` and `:204`. The finding's `:163,205` points at the imports, not the branches. The two branches also have *different* conditions: the second is by-design exclusive to a py7zr-only host, so on a host with both tools it is skipped deliberately, not silently lost.
- Kit half — material correction to "on the current CI platform both sub-checks do run … this is latent": the workflow installs only `numpy cryptography` by pip (`compile.yml:35`) — no `py7zr` anywhere in the workflow — and installs `p7zip-full` only at `compile.yml:471`, inside the later "Download the latest Neutrino core" kit step, **after** the test step that runs `test_kit_package.py` at `compile.yml:239`. On repo evidence alone, at test time CI therefore has neither `py7zr` nor a workflow-installed `7z`, so the .7z/Neutrino branches are in all likelihood **already absent from CI runs today**, not merely latent on other runners. (The only escape hatch is a `7z` binary preinstalled in the `ubuntu-latest` runner image, which nothing in the repo establishes; `scripts/package_kit.py:321` itself comments "CI installs p7zip-full" as the production path's source of `7z`.) The defect is thus at least as strong as claimed for the kit suite, but the finding's current-CI framing is wrong.
- Fix (assert the precondition on CI-shaped runners, keep the graceful path locally) remains sane, minimal, and is in fact more urgent for the kit suite than the finding states.

## RE-LAUNCH-077 — WEAKENED (to a non-finding)

- Every factual claim verifies: `tests/test_transition_latency.py:9-21` is a fresh Python PS-ADPCM decoder (coefficient table `[(0,0),(60,0),(115,-52),(98,-55),(122,-60)]`, fixed-point `((n<<12)>>shift)+((h1*coef+ h2*coef +32)>>6)`, clamped); there is **no** ADPCM decoder in `src/` — `grep` over `src/*.c,*.h` finds only audsrv call sites (`src/dash_sound.c:171,179,439` `audsrv_adpcm_init` / `audsrv_load_adpcm` / `audsrv_ch_play_adpcm`), i.e. decoding is done by the IOP audsrv/SPU2, not by repo code; SFX generation is `scripts/gen_sfx.py`; the test passes at the pin (run: `PASS: transition waveform retained; signal onset 6.67 ms`).
- Why it is not a finding: it demonstrates no divergence, no failing behaviour and no defect; its own text says "INFERRED risk only — no divergence was observed" and its Fix is "none required". A shared-misunderstanding scenario is hypothetical for every golden-hash test of an external format. Correct disposition is a caveat/note attached to the asset, excluded from the defect count — not a numbered finding with Benefit/Effort ratings. True scope: an accurate observation about test epistemology, nothing actionable at the pin.

## RE-LAUNCH-078 — WEAKENED

- Duplication verified in full. All three copies poll `fileXioDopen`, re-list with `readGENERIC` on success, and spin 100 ms in an identical empty loop `while (Timer() < backoff_start + 100) {}`:
  - Helper `src/filer.c:340-382` `readGENERICWithFirstOpenRetry(path, info, max, wait_budget_ms)` — parameterised budget, PcCard-offline break, non-root pre-check that returns without re-listing if the first `Dopen` already succeeds.
  - ATA branch `src/filer.c:1990-2025` (loop at `:1996-2020`): budget `is_ata_root ? 2000 : 750`, canonicalised ATA path, root test inline on the canonical path, no PcCard check, re-lists once on readiness then breaks for non-root.
  - MX4SIO branch `src/filer.c:2112-2170` (pre-listing at `:2109-2130`, loop at `:2134-2170`): budget `is_root ? 2000 : 750`, and the claimed extra is real and large — it probes up to four path spellings (`path`, `path_alt` `mx4sio:`↔`mx4sio:/`, `indexed_path` `mx4sio0:…`, `indexed_path_alt`) both before and inside the loop, with separate `path_ready`/`indexed_ready` flags.
  - Budgets/extras drift as claimed; `DelayThread` is available and used elsewhere including in the same file (`src/filer.c:423,506`, `MC_BROWSER_RETRY_DELAY_US`), so the empty-spin criticism stands.
- Where the finding weakens — the fix: "call `readGENERICWithFirstOpenRetry` from the ATA and MX4SIO branches, deleting the two inline copies" is **not** behaviour-preserving as stated. The helper takes a single path and knows nothing of alternate spellings; replacing the MX4SIO copy would either lose the indexed/alt probing, or require calling the helper sequentially per spelling — multiplying the budget (up to 4× 2000 ms) and changing break semantics — or require extending the helper with a path-list parameter, i.e. adding mechanism the finding does not count. The ATA replacement is close but not exact either (helper's non-root pre-check returns the initial empty listing without the re-list the ATA copy performs on readiness). True scope: one-and-a-half copies are dedupable (ATA, with care); the MX4SIO copy is a genuinely extended variant, so net deletion and Risk: Medium are both overstated for that branch.

## RE-LAUNCH-079 — VERIFIED-DRIFT

- All three blocks verified at 16 entries each (the `Misc` parent plus 15 sub-entries):
  - `src/config.c:801-816` `initConfig`: 16 `sprintf` lines, `%s/%s` from `LNG_DEF(MISC)`/`LNG_DEF(…)`.
  - `src/config.c:975-1003` `loadConfig` (with the `Misc` branch itself at `:972-973`): 16 hand-written `else if` branches, inverse form `%s%s` from `setting->Misc` + file `value`.
  - `src/lang.c:418-433` language reload (`updateLocalizedMiscPaths` tail): the same 16 lines again, but from `LNG(…)` throughout. Citation drift: the finding says `419-434`; the block is `418-433`.
  - Divergence at `src/config.c:814` verified exactly: `Misc_Show_Build_Info` is built from `LNG(MISC), LNG(Build_Info)` while all 15 neighbours in `initConfig` use `LNG_DEF(…)` (definitions at `include/launchelf.h:682-683`).
  - Load-ordering constraint verified: comment at `src/config.c:971-973` ("the Misc device must be defined before its subprograms") directly above the `Misc` branch.
- Corrections the finding omits: (a) the load block is not a pure mirror — `Misc_Exploit_Installer` at `:1000-1002` has a special case translating the legacy value `"Exploit Installer"` to `LNG_DEF(Exploit_Installer)`, and its branch order differs from init order (Exploit_Installer last-but-one in load, mid-list in init); (b) consequently the proposed fix — "drive all three sites from the one name table (the `LNG_DEF` ids in order)" — can drive init and language reload (both build from a language table; they differ only in `LNG` vs `LNG_DEF` base, parameterisable), but **cannot** drive the load side, which is value-driven string-name dispatch into distinct struct fields and needs a name→field-offset table plus the Exploit special case. Net deletion is still plausible across the three sites, but it is a mapping-table refactor, not the single shared loop described. Duplication, drift and risk rating otherwise stand.

## RE-LAUNCH-080 — VERIFIED-DRIFT

- The two assemblies are field-by-field identical — diffed: `src/main.c:255-264` (`Execute`) and `src/main.c:305-314` (the `DashRun` call site in `main`) assign, in the same order from the same globals, `boot_argc, boot_argv, boot_path, main_msg, cnf_path, default_esr_path, default_osdsys_path, default_osdsys_path2, rough_region, romver_data`. No third assembly exists in `main.c` (`grep` for `exec_ctx.`/`dash_ctx.` returns only these two blocks).
- Count/title corrections (material): the title says "three times" and the problem says "11-field context / the same eleven assignments" — both are wrong. The struct `MainExecuteContext` (`include/main_actions.h:11-24`) has 11 fields, but each site explicitly assigns only **10**; the 11th, `elf_iop_mode`, is left at its `{0}` zero-initialisation in both copies. And the sites are ~50 lines apart (`:253` vs `:303`), not "eleven lines apart". The duplication the title/body count cannot agree on is 2 copies × 10 assignments.
- Fix correction: "build the context once in `main()` and pass that one instance to both consumers" cannot be done as written — one consumer, `Execute()` (`src/main.c:251`), is a separate static function called from `runWleIgrAfter()` (`:228,241`, before the dashboard block) and from the main event loop (`:368`); it is not a consumer inside `main()`'s dashboard block, and its signature takes only `pathin`. Sharing one instance would require changing `Execute`'s signature and all callers, or a file-scope builder/global. The sane LESS fix is a small static `buildMainExecuteContext()` helper called by both sites (~10 lines net deletion) — same destination, different (honest) route.

## RE-LAUNCH-081 — VERIFIED

One-line confirmation: `src/smb_cnf.c:149-171` `isIpv4` rejects leading zeros with the octal comment at `:162-163` ("010 would reach the drivers' inet_addr as octal 8: another host") plus digits>3 and value>255 checks; `src/neutrino_smb.c:8-19` `ipv4` has the range/digit checks but **no** leading-zero rejection, and is used to re-parse the share URL in `neutrinoSmbResolve` (`:63`, after extracting `share->server` at `:61-62`) and again before writing Neutrino's config in `neutrinoSmbConfig` (`:151-152`, config writer at `:140-187`); `src/config_network.c:86-124` `ipStringToOctet`/`BuildOctets` uses `atoi` per octet with no 0–255 check at all (octets are truncated to 4 chars by the `oct_str[5]` buffer, so e.g. `999`/`1234` survive), and the result flows through `applyNetworkSettings` into the `ip/netmask/gw` globals persisted by `saveNetworkSettings` (`:59-68`) to `IPCONFIG.DAT`. No fourth validator exists in `src/`. Fix (keep the strict `smb_cnf.c` validator as the single one) is sane; sharing removes code overall.

## RE-LAUNCH-082 — WEAKENED

- Verified parts, diffed byte-for-byte: `toBcd`/`fromBcd` (`src/dash_clock_sync.c:251-259`) and `manualToBcd`/`manualFromBcd` (`:377-385`) have literally identical bodies (`((v/10)<<4)|(v%10)`; `((v>>4)&15)*10+(v&15)`). The day-count algorithms genuinely differ: `civilDays`/`civilFromDays` (`:219-249`) are Howard-Hinnant era arithmetic (400-year era, `146097`-day cycle, epoch 1970), while `manualSerialSeconds` (`:402-412`) loops year-by-year from **2000** with `manualLeap` plus month-by-month via `dashClockDaysInMonth` (`:392-400`). Both paths do feed verified RTC writes: SNTP `writePs2Clock` (`:291-313`, `sceCdWriteClock` + read-back within 2 s) and manual `dashClockSetManual` (`:414-482`, same write + read-back pattern).
- Defeating fact for the fix as stated: the SNTP helpers are inside `#ifdef ETH` — the guard opens at `src/dash_clock_sync.c:10` and closes at `:372` (with an `#else` stub for `dashClockSyncInternet` at `:363-372`) — while the manual section begins after `#endif` under the explicit comment at `:374-376`: "Manual clock entry is available even in builds without Ethernet." The `manual*` copies are therefore not careless duplication; they are the non-ETH compilation's only copies. "Delete the `manual*` BCD pair and call `toBcd`/`fromBcd`" and "express `manualSerialSeconds` via `civilDays`" both break non-ETH builds unless the shared helpers are first moved above/outside the guard — a relocation the finding never mentions, and which changes its Effort/Risk from S/Low.
- Two further scope corrections: (a) `manualSerialSeconds` is epoch-2000 and range-bounded (2000–2099, validated in `dashClockSetManual`), `civilDays` is epoch-1970 and general — expressing one via the other needs the `civilDays(2000,1,1)` offset the finding omits (it happens to cancel in the current diff-only use, which the finding also does not note); (b) `dashClockDaysInMonth` is **not** duplicated at all — it is a public API (`include/dash_internal.h:1120`, used by `src/dash_settings.c:3172,3207` for the clock field editor) with no counterpart on the SNTP side, so bundling it into the "two day-count algorithms" overstates the duplicated surface. True scope: 2 truly duplicated one-line BCD functions (dedupable after moving them out of the ETH guard) plus two intentionally separate date conversions for different epochs/build configurations.

## RE-LAUNCH-083 — VERIFIED-DRIFT

- The copy is real, diffed declaration-by-declaration: the 7 common `ps2ipc_*` externs are identical in `src/cloud_http.c:41-52` and `src/dash_clock_sync.c:23-30`; `CloudHostEnt`/`SntpHostEnt` and `CloudSockaddrIn`/`SntpSockaddrIn` are field-identical (only type names and spaces-vs-tabs differ); `cloud_htons` (`cloud_http.c:68-71`) and `sntpHtons` (dash_clock_sync.c) have identical bodies `((v<<8)|(v>>8))`. The header-not-installed explanation is verified at `src/cloud_http.c:37-40`: "ps2build does not install the internal ps2ipc.h header, so keep the small public ABI surface declared here."
- Drift 1: the SNTP `htons` is **not** in the cited block — it sits at `src/dash_clock_sync.c:95`, ~50 lines below the cited `:22-48` (which covers only externs + the two structs). The cloud block `:37-75` does contain its htons.
- Drift 2: the blocks are not exact copies — cloud is a **superset**, declaring two further externs the SNTP client does not (`ps2ipc_ioctl`, `ps2ipc_getpeername`, `cloud_http.c:50-51`), and the SNTP file carries no equivalent of the cloud comment explaining why the declarations are hand-kept (that rationale is inferred for SNTP, stated only for cloud).
- Fix: hoisting to one internal header still nets a deletion and is LESS-NOT-MORE compliant, provided the header declares the cloud-only pair as well and is includable from the SNTP file's `#ifdef ETH` region — both satisfiable, neither noted in the finding.

## RE-LAUNCH-084 — VERIFIED

One-line confirmation: self-run `md5sum`/`diff` at the pin — `iop/mmceman/src/sio2man_hook.c` is 320 lines (md5 `96cee49a3b2791feaf5579f939fe2a78`), `iop/mx4sio_bd/src/sio2man_hook.c` is 315 lines (md5 `f1b452f7e07a53a2e4cf60ab70d8a849`), and the full diff is exactly the PORT_NR divergence (mmceman: 4-line rationale comment + `/* #define PORT_NR 3 */` commented out for both-slot access; mx4sio_bd: `#define PORT_NR 3` active, gating the `#ifdef PORT_NR` transfer filtering in both files) plus one trailing-whitespace difference on the final `CpuResumeIntr(state);` line — i.e. exactly one functional divergence as claimed; the checked-clean contrast also verifies (`ps2.yaml:119-155`: `mcman_physical`/`vmcman` build from the shared `iop/vmcman/src` sources with different `defines`). The do-not-merge, comment-only fix is the correct LESS-NOT-MORE call.

---

## Same-mechanism pairs (within RE-LAUNCH-001…092)

- **078 ↔ 085 (related, not duplicates):** both are empty `while (Timer() < …) {}` busy-spins with no yield — 078 in `src/filer.c` (3× 100 ms), 085 in `src/editor_file.c` (5× 1.5 s). Same anti-pattern/mechanism family, different files and purposes; a single spin/yield idiom fix would serve both, but neither finding's count subsumes the other.
- **079 ↔ 006 (cross-referenced by 079 itself, distinct mechanisms):** same `Misc_*` field family in `src/config.c` — 006 is the unbounded-copy overflow in `loadConfig`, 079 is the triplicated construction of the same fields. Fixing 079's load side with a table must not regress 006's required bounds.
- **073 ↔ 074 (same category, distinct mechanisms):** both are hand-synchronised duplication (build-target definitions vs version literals). No shared code; no double count.
- **075 ↔ 076 ↔ 072 (test-coverage-illusion family, distinct mechanisms):** 072 = live suite wired into no workflow, 075 = research model counted as product coverage in CI, 076 = conditional branches silently absent from a passing suite. Three different failure modes; none duplicates another.
- No other pair in 073–084 shares a mechanism: 080/082/083/084 are all Category-2 duplication but of unrelated code (context assembly, BCD/date math, socket ABI, IOP hook) with no overlapping lines or fixes.

## CANDIDATES (new defects / observations not in the original findings)

1. **CAND-V7-1 — Two divergent Python ADPCM decoders, one of which the finding (077) missed.** `scripts/gen_sfx.py` `decode()` (docstring: "Reference decoder (used by --check and by the tests)") uses float arithmetic (`v * (1 << (12 - shift)) + (h1*K0 + h2*K1) / 64.0`, `int(round(...))`) and is in fact used by `tests/test_sfx_ps2.py:25`; `tests/test_transition_latency.py:9-21` reimplements the same decode in fixed-point integer arithmetic instead of importing it. Coefficients agree, rounding does not, so the two host decoders can disagree on edge samples — a host-side divergence risk independent of the audsrv question 077 raises. (Observation; no PS2-side defect shown.)
2. **CAND-V7-2 — The Misc enumeration has at least five hand-written sites, not three (extends 079).** Beyond init/load/lang-rebuild, `src/lang.c:382-411` (`updateLocalizedMiscPaths`) hand-writes 15 per-entry `isMiscLaunchNameAlias` branches with per-entry `LK_Path` sprintfs, and the config saver at `src/config.c:566-597` hand-lists all 15 `Misc_*` names again in its format string and argument list. Adding/renaming a Misc entry touches ≥5 sites in 2 files, so 079's "three blocks in two files in lockstep" understates the lockstep surface (the two extra sites are enumeration, not construction, which is why they are a candidate extension rather than a correction to 079's count of *rebuild* blocks).
3. **CAND-V7-3 — The ps2ips sync assertion that 073 proposes copying does not cover headers/cflags (extends 073).** Even for `ps2ips_rpc`, the module with a sync test, a drift confined to `headers:`, `cflags:` or `include_dirs:` in either `ps2.yaml` copy would pass all current tests (see 073 Drift 2). The drift class 073 names is therefore only partially closed for the module it holds up as the pattern.

---

# Adversarial Verification — Chunk V8: RE-LAUNCH-085 … RE-LAUNCH-092

- Source report: `~/workspace/Sable-audit-redo/Redo-Launchstation.md`
- Code: `~/workspace/audit-clones-2026-11-13/Launchstation` at pin `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (HEAD verified, read-only)
- Method: refute-first; every cited file opened at the pin and re-derived from scratch; uniqueness / no-reader / no-define claims re-grepped tree-wide independently.
- Tally: **VERIFIED 3 · VERIFIED-DRIFT 2 · WEAKENED 3 · REFUTED 0 · BLOCKED-ENV 0**

| ID | Verdict | One-line summary |
|---|---|---|
| RE-LAUNCH-085 | VERIFIED | Five empty 1.5 s spins at the exact cited lines; no draw/poll/yield inside any of them. |
| RE-LAUNCH-086 | WEAKENED | The `strlen(out)`-in-loop bug is real, but the "only such site in the core" claim is false — `src/config_network.c:100` is a second, worse instance. |
| RE-LAUNCH-087 | VERIFIED | `SystemCnf_VMODE` has exactly four tree-wide hits, all writes/declaration; no reader exists. |
| RE-LAUNCH-088 | VERIFIED | Commented `WriteFont_C` block is exactly `src/draw.c:343-399` (57 lines incl. markers); no live reference anywhere. |
| RE-LAUNCH-089 | VERIFIED-DRIFT | Arrays, sizes, ranges and consumers are exact; the "largest non-generated, non-font file in the core" superlative is false. |
| RE-LAUNCH-090 | WEAKENED | Blocks exist and no in-tree build config defines either flag, but both are documented, intentionally retained developer aids — not unrecorded scaffolding; `pad.c` range also understates the footprint. |
| RE-LAUNCH-091 | VERIFIED-DRIFT | The thread leak is real, but the stated fix `DeleteThread(id)` is defective as written: at `smbd.c:262` `id` has already been overwritten with `StartThread`'s return value. Corrected fix below. |
| RE-LAUNCH-092 | WEAKENED | Every fact checks out (32 KB static, shared, deliberate, largest sampled IOP static), but by its own text there is no defect, no trigger and no fix — it does not meet the bar of an actionable finding; downgrade to an observation/note. |

---

## RE-LAUNCH-085 — VERIFIED

One-line confirmation: all five sites verified verbatim at `src/editor_file.c:59`, `:182`, `:208`, `:250`, `:319` — each is `WaitTime = Timer(); while (Timer() < WaitTime + 1500) ; // print operation result during 1.5 sec.` (the `:319` comment reads "display operation result…"), the loop body is the empty statement in every case (no draw, no pad poll, no yield — the `drawMsg(...)` call happens once, before the spin), and the replacement claim holds: `src/dash_editor.c` exists and `dashOpenTextEditor` is the dashboard's native editor entry (`src/dash_main.c:504`, intercepted at `src/dash_main.c:462`; cf. RE-LAUNCH-044). Fix direction (yielding wait / delete the delay, no new timing system) is sane and LESS-NOT-MORE. Pair note: same *family* as RE-LAUNCH-078 (empty `Timer()` spins in `src/filer.c`), different mechanism — 078's spins are 100 ms backoffs inside condition-polling retry loops, 085's are unconditional 1.5 s result-display delays; not a duplicate.

## RE-LAUNCH-086 — WEAKENED

**True part (re-derived):** `src/filer_copy.c:1105-1108` is exactly as cited:
```c
for (i = 0; i <= strlen(out); i++) {
    if (out[i] == '/')
        ret++;
}
```
The condition is re-evaluated every iteration, so `strlen(out)` is recomputed per character — O(n²) scans. `out` is `char out[MAX_PATH]` at `src/filer_copy.c:764` with `MAX_PATH = 1025` (`include/launchelf.h:204`), the site is inside the `isHddCommonParty(outParty)` HDD `__common` branch, and the one-line hoist fix is sane and minimal.

**Defeating part — the uniqueness claim is false.** The finding states this is "the only `for (... strlen ...)` site in the core (script-verified)". Re-running that grep over `src/` at the pin returns three hits, not one:

- `src/filer_copy.c:1105` — the cited site.
- `src/config_network.c:100` — `for (i = 0; ((i <= strlen(ip)) && (oct_cnt < 4)); i++) {` in `ipStringToOctet`, with a *second* per-iteration `strlen(ip)` inside the body at `:101` (`else if ((ip[i] == '.') || (i == strlen(ip)))`). Same mechanism, strictly worse (two recomputations per iteration). `config_network.c` is EE core (`src/`), so no "core" scoping rescues the claim. (This function is cited by RE-LAUNCH-081, but only for its divergent IPv4-validation semantics — the strlen mechanism there is unreported; see CAND-V8-1.)
- `src/dash_card_tool.c:2452` — `for (k = 0; k < lib->nsets && (strlen(lib->sets[k]) != sl || …); k++)`. Counted literally it is a third `for…strlen` site, though mechanistically weaker: the string changes with `k`, so it is not an O(n²) rescan of one string. It does not defeat the claim on its own; `config_network.c:100` does.

**True scope:** a real, trivial O(n²) on a ≤1025-char path, one of (at least) two same-mechanism sites in the EE core — not the sole site. The fix for the cited line is unchanged.

## RE-LAUNCH-087 — VERIFIED

One-line confirmation: tree-wide grep for `SystemCnf_VMODE` at the pin returns exactly four hits — declaration `src/main.c:70` ("As yet unused"), parser write `src/main_startup.c:159` (`strncat(SystemCnf_VMODE, value, 9)`, branch at `:158`), clear `src/main_startup.c:176`, extern `include/launchelf.h:374` — and no reader in any file type; the only other `VMODE`-adjacent hits are the unrelated dashboard `S_TVMODE` setting in `src/dash_settings.c`. Deletion fix is sane and LESS-NOT-MORE. **Overlap flag (087 vs 018, as tasked): intersecting, not duplicate.** RE-LAUNCH-018's mechanism is the `strncat`-as-bounded-copy overflow across all four `scanSystemCnf` branches (`src/main_startup.c:152-162`); 087's mechanism is dead write-only state. They share exactly one line (`:159`). Applying 087 shrinks 018's scope from four sites to three; applying 018 first leaves 087 fully intact. Neither subsumes the other.

## RE-LAUNCH-088 — VERIFIED

One-line confirmation: the block is exactly `src/draw.c:343-399` — header comments `:343-344`, `/*` at `:345`, function `int WriteFont_C(char *path_arg)` at `:346`, `*/` at `:397`, `//endfunc WriteFont_C` at `:399` — 57 lines inclusive as claimed ("~57-line"), containing the file's only two `strcat` calls (`:378-379`, both inert inside the comment) plus unbounded `sprintf`s; the commented prototype is at `include/launchelf.h:507-508` exactly as cited; tree-wide grep shows **no live reference** to `WriteFont_C` (only the block, the prototype, and one further *commented-out* call — see CAND-V8-2). Deletion fix is sane and LESS-NOT-MORE.

## RE-LAUNCH-089 — VERIFIED-DRIFT

**Verified exactly:** `src/icon.c` is 2055 lines; `icon_folder[1024]` header at `:2`, closing `};` at `:1027`; `icon_warning[1024]` header at `:1030`, closing `};` at `:2055`; a scripted parse of both initialisers counts exactly 1024 entries each (2048 bytes total), one byte per line; consumers are exactly `src/draw.c:211` (`TexIcon[0].Mem = (void *)icon_folder`) and `src/draw.c:221` (`icon_warning`); the contrast case is real — `src/dash_font_data.c:1-5` is script-generated (`scripts/gen_dash_font.py`) into packed multi-byte rows. The representation-only fix (regenerate in packed-row style, data byte-identical) is sane and LESS-NOT-MORE.

**Corrected value — the superlative is refuted:** the problem statement calls `icon.c` "the largest non-generated, non-font file in the core by line count". `wc -l src/*.c` at the pin contradicts this under any reading of "core": larger non-generated, non-font files include `src/dash_settings.c` (4096), `src/dash_browser.c` (3616), `src/init.c` (3205), `src/dash_card_tool.c` (2903), `src/filer.c` (2556), `src/filer_browser.c` (2528), `src/main_exploit_installer.c` (2519) and `src/filer_actions.c` (2259) — and even excluding all `dash_*` files, `init.c`, `filer.c`, `filer_browser.c`, `main_exploit_installer.c` and `filer_actions.c` still exceed 2055. Corrected statement: `icon.c` is a 2055-line file whose *entire* content is two 1024-byte arrays — the waste claim stands on the representation alone and needs no ranking; the ranking as printed should be deleted. (Ironically the same largest-files error class as RE-LAUNCH-068 documents in `docs/architecture-maintenance.md`; different mechanism/location, not a duplicate.)

## RE-LAUNCH-090 — WEAKENED

**Verified part:** all three blocks exist at the cited anchors — `src/pad.c:284` `#ifdef DASH_AUTOTEST` (player + `#include "dash_autotest.h"` at `:289`), `src/dash_main.c:1295-1296` `#ifdef DASH_AUTOTEST` sample-library hook, `src/dash_main.c:1483` `#ifdef DASH_THEME_TOUR` with the verbatim comment "development only: show every preset for five seconds" inside `DashRun`. The no-define claim was re-grepped independently across every build configuration in the repo (`ps2.yaml`, all `.github/workflows/*.yml`, `scripts/*.sh`, all `*.mk`/Makefiles): **zero definitions** of `DASH_AUTOTEST` or `DASH_THEME_TOUR`. The INFERRED label on "no release configuration defines them" is therefore honest and, in-tree, confirmed.

**Weakening facts:**

1. **These are documented, intentionally retained aids, not unrecorded scaffolding.** `docs/dashboard.md:573-576`: "Defining `DASH_AUTOTEST` (add it to the `wLaunchELF` target's `defines` in ps2.yaml) replays the timed button script in `include/dash_autotest.h` and seeds a sample library, so every screen can be exercised in an emulator. `DASH_THEME_TOUR` shows every theme preset for five seconds. Never ship such builds." The same instructions appear in `docs/themes.md:436` and `docs-site/content/developers.html` ("Developer aids… neither belongs in a release"). The in-source comments already record the status the finding asks for as its minimum alternative: `src/pad.c:286-287` "Development aid (never enabled in normal builds)", `src/dash_main.c:1296` "development-only sample library", `src/dash_main.c:1484` "development only". So the finding's fallback remedy is already satisfied, and its primary remedy (delete both blocks / move to a development branch) would delete a documented emulator-screenshot workflow whose manual-define step is precisely what keeps it out of shipped binaries. That is not LESS-NOT-MORE against the status quo.
2. **Citation drift on the `pad.c` footprint:** the autotest code is *two* blocks, not one span ending at `:313` — the helper block is `:284-307` and the `readpad` injection block is `:313-325`; `:313` as cited is only the second block's opening line. Likewise the `dash_main.c` sample-library block runs `:1295-1314`, not just the cited `:1295-1296` anchor. The reader-cost claim ("conditional branches every reader of `readpad`/`DashRun` must reason past") is true but amounts to two small, clearly-labelled ifdefs in each file.

**True scope:** two documented, off-by-default, manually-enabled development aids, correctly labelled in code and docs, costing a few conditional branches in two hot files. A legitimate taste call for a maintainer; as a defect finding it is weakened to an observation.

## RE-LAUNCH-091 — VERIFIED-DRIFT

**Leak verified (full read of `start()`, `iop/smbd/smbd.c:246-267`):**
```c
if ((id = CreateThread(&t)) < 0)
    return id;
last_error = 0;
running = 1;
if ((id = StartThread(id, NULL)) < 0) {
    running = 0;
    return id;
}
```
`CreateThread` success → `StartThread` failure returns the error with `running = 0` and no `DeleteThread` anywhere in the file (the only delete is `serve()`'s `ExitDeleteThread()` at `:243`, unreachable for a never-started thread). The next `SMBD_DEVCTL_START` (`:283-288` → `start()`) creates another thread, so repeated failures accumulate dormant IOP threads. Every other error path in `start()` was checked: the `CreateThread`-failure return has nothing to clean up (correct), and the `running` early-return at `:251-252` creates nothing. Location drift is minor: `start()` is `:246-267`, not `:246-270` (`:269` is already `dev_devctl`).

**The stated fix is defective as written — corrected here.** The finding prescribes "`DeleteThread(id)` in that branch — one call". Inserted literally, that call deletes the wrong thing: at `:262` the assignment `id = StartThread(id, NULL)` **overwrites the thread id with `StartThread`'s return value**, so inside the `< 0` branch `id` holds the negative error code, not the created tid (which is irretrievably lost). `DeleteThread(<error code>)` would not free the created thread; the leak would survive the "fix". The correct one-mechanism fix preserves the tid in a second variable — the pattern already used twice in this same tree: `iop/oldlibs/ps2ftpd/src/FtpControl.c:129-131` (`if (StartThread(id, NULL) < 0) { DeleteThread(id); … }`, return value *not* assigned over `id`) and `iop/oldlibs/ps2ftpd/src/main.c:146-148` (`i = StartThread(pid, NULL)`, then `DeleteThread(pid)`), likewise `iop/udpfs_smap/src/smap.c:489-491` (deletes the separately stored `IntrHandlerThreadID`). With that correction the fix is sane, one branch, and LESS-NOT-MORE. Pair note: same file as RE-LAUNCH-020 (`smbd.c` devctl START path), different mechanism (thread lifecycle vs credential-blob parsing); not a duplicate.

## RE-LAUNCH-092 — WEAKENED (facts verified; does not meet the bar of a finding)

**Every factual claim re-derived and confirmed:**

- `iop/oldlibs/ps2ftpd/src/FtpClient.c:88`: `static char buffer[32768];` with the deliberate-size comment at `:87-88` — "this shared buffer is used for everything the clients do" / "fewer round trips per transfer on slow devices such as mmce".
- Sharing is real: `MAX_CLIENTS 4` (`FtpClient.c:41`); the buffer backs command parsing (`strcpy`/`strtok` at `:186-188`), uploads (`recv` at `:557`, bounded by `m_uiDataBufferSize = sizeof(buffer)` at `:546`) and downloads (`FileSystem_ReadFile` at `:581` with an explicit `received > (int)pClient->m_uiDataBufferSize` rejection, then bounded `FtpClient_SendAll` at `:586` — the "send path is properly bounded" claim holds).
- "Largest single static" claim sampled as tasked: a scripted census of simple static arrays ≥4096 elements across `iop/**/*.c` returns exactly two hits — this buffer (32768) and `iop/vmcman/src/main.c:53` `mcman_backupbuf[16384]`, i.e. the next largest is half the size; the main macro-sized rival, `SMB_buf` in `iop/smbman/smb.c`, is `MAX_SMB_BUF + MAX_SMB_BUF_HDR` = 4096+128 = 4224 B (`iop/smbman/smb.h:900-901`). The claim survives sampling.
- 32 KB of the IOP's 2 MB is arithmetically as stated, and the `.bss` reservation lasts exactly as long as the on-demand module is resident.

**Why it is weakened to a non-finding:** the text itself concedes "this is a footprint/robustness observation, not a defect with a known trigger" and "Fix: none required". The verification adds the defeating context for even the "robustness" framing: sharing is safe *by design*, not by luck — `ps2ftpd` is a single-threaded `select()` server (`iop/oldlibs/ps2ftpd/src/FtpServer.c:198` dispatch loop calls one client's handler at a time), every buffer use above completes synchronously inside one handler invocation, and the code already documents the one real hazard and its guard at `FtpClient.c:89` ("Sockets are blocking. Finish each shared-buffer send before another client runs" → `FtpClient_SendAll`). There is no defect, no trigger, no fix and no decision the finding enables beyond "look here first *if* IOP RAM pressure ever forces a choice" — that is a triage note for a future audit, not a finding. It should be moved out of the numbered findings (or explicitly reclassed as an observation) so it does not inflate the actionable count; kept as a note, its content is accurate and useful.

---

## Duplicate / overlap map (chunk vs RE-LAUNCH-001…092)

- **087 ↔ 018 — intersecting, not duplicate** (detailed under 087): shared line `src/main_startup.c:159` only; overflow mechanism (018) vs dead-state mechanism (087) are independent, each survives the other's fix.
- **085 ↔ 078 — same family, not duplicate:** empty `Timer()` busy-waits, but 078 = condition-polling retry backoffs in `src/filer.c`, 085 = unconditional display delays in `src/editor_file.c`.
- **091 ↔ 020 — same file/path, not duplicate:** `smbd.c` START lifecycle (thread leak) vs START credential-blob parsing.
- **089 ↔ 068 — same error *class* in prose, not duplicate:** both involve a wrong "largest files" claim (068 about the docs list, 089's own superlative); no shared mechanism or location.
- No other same-mechanism pairs in 085…092 (086/088/090/092 mechanisms are unique in 001…092 — subject to CAND-V8-1, which extends 086's mechanism to a site no finding covers *for that mechanism*).

## CANDIDATES (new defects found during verification)

- **CAND-V8-1 — Second (worse) strlen-in-loop site, refuting RE-LAUNCH-086's uniqueness claim.** `src/config_network.c:100-101`, `ipStringToOctet`: `for (i = 0; ((i <= strlen(ip)) && (oct_cnt < 4)); i++)` plus `i == strlen(ip)` re-evaluated in the body — two `strlen` recomputations per character. Same mechanism and same one-line hoist fix as RE-LAUNCH-086; either fold into 086's scope or file separately. (RE-LAUNCH-081 cites this function for IPv4-validation semantics only.)
- **CAND-V8-2 — Third commented `WriteFont_C` artefact not cited by RE-LAUNCH-088.** `src/main_info_screens.c:302-303`: `//The next line is a patch to save font, if/when needed (needs patch in draw.c too)` + `// WriteFont_C("mc0:/SYS-CONF/font_uLE.c");`. Harmless (commented), but 088's deletion fix should remove these two lines too, or the tree keeps a dangling reference to the deleted utility.
- **CAND-V8-3 — Same overwritten-tid cleanup flaw as RE-LAUNCH-091's defective fix, live in usb_mass.** `iop/usbmass_bd/src/usb_mass.c:887-897`: `ret = CreateThread(&thread); … ret = StartThread(ret, 0); if (ret < 0) { … DeleteThread(ret); }` — the `DeleteThread(ret)` at `:896` passes `StartThread`'s error code, not the created thread id (lost at `:893`), so the apparent cleanup does not free the thread and the leak it claims to handle persists. Correct pattern in-tree: `iop/oldlibs/ps2ftpd/src/FtpControl.c:129-131`, `iop/udpfs_smap/src/smap.c:489-491`.

---
*Verification performed read-only at pin 9fb0598a79f0. No files in the clone were modified. Nothing under `~/workspace/Sable-audit/` was read.*

---

## Closing — coordinator confirmation

- **Coverage:** all 92 findings carry a verdict row above (one per finding in the chunk sections; ID coverage in this file re-checked by script: 92 unique `RE-LAUNCH-###` IDs).
- **Tally (from the eight section files):** VERIFIED 48 · VERIFIED-DRIFT 33 · WEAKENED 10 · REFUTED 1 · BLOCKED-ENV 0 = 92.
- **Pin/clean:** clone HEAD `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` at start and end; `git status --porcelain` = 0 entries at the end. Read-only held; every execution (including the coordinator's three RE-LAUNCH-009 runs) happened in `/tmp` copies. Nothing under `~/workspace/Sable-audit/` was read.
- **Net assessment:** the redo corpus is substantially sound at the mechanism level — 81 of 92 findings survive re-derivation intact or with citation-level drift only, and four drifts show the truth is worse than filed. The failures concentrate in framing and prescription, not discovery: one arithmetic refutation (007), ten scope overstatements (notably 036's "stricter sibling" that doesn't exist, 077/092 as non-findings, 090's already-satisfied alternative), and seven fixes that would mislead an implementer if applied as written (013, 020, 036, 078, 080, 082, 091) — of which 091's is the most dangerous, since its one-line fix silently fails to fix.
