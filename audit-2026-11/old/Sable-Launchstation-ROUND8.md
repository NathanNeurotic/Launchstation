# Sable Audit — Launchstation ROUND 8 (docs claims-vs-code pass)

- **Repo / pin:** NathanNeurotic/Launchstation, master. **Pin HELD:** clone HEAD == `git ls-remote origin master` == `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790`, verified at start and end. No delta, no revalidations owed.
- **Mode:** READ ONLY throughout; `git status --porcelain` empty at consolidation. Tests executed by section D ran on the host against the pin tree without writing to it.
- **Scope:** the round-7 residue's item (d) — **docs/docs-site prose, inventoried by size only through seven rounds, now claims-checked.** The same discipline as the other repos' round-7 docs passes (RiptOPL 67 claim groups, POPSLoader 100 claims, PS2-Servers 16 claims): every checkable claim about behaviour, defaults, limits, formats, paths, ports, compatibility or requirements extracted and checked against the code at the pin, with both-side citations.
- **Method:** four parallel section passes (`round8-sections/08A-core-docs.md`, `08B-docssite.md`, `08C-feature-docs.md`, `08D-internal-docs.md`), followed by a coordinator consolidation that **personally spot-checked the headline mechanisms against the clone**: the root `LICENSE` file's existence, the `Misc` double-slash chain (`docs/LAUNCHELF.CNF:52` → `src/config.c:973` → `src/main_actions.c:732`), the cloud defaults (`src/dash_store.c:263`), the CNF rename (`src/main_boot.c:63`), and the PSX i.Link return (`src/init.c:261-262`). Every spot-check matched the section's claim. Cross-section duplicates were merged into single findings with every doc location listed; doc claims whose falsity is wholly owned by an existing finding were cross-referenced, not re-filed (LAUNCHSTATION-187, -194, -119 — see §3).
- **Result: 24 new findings, LAUNCHSTATION-200…223.** No revisions to LAUNCHSTATION-001…199 were forced; R4-R1…R3 settlements stand; -066's `ps2smb.h` caveat stands as-is. Launchstation total: **223 findings**.

---

## 1. Census and tallies

**Docs surface read in full: 109 files, 11,439 lines.**

| Section | Files | Lines | Contents |
|---|---|---|---|
| A — core docs | 11 | 1,698 | `README.md`, `docs/README.md`, `docs/dashboard.md`, `docs/BUILDING.md`, `docs/wiki/` (6 files), `CHANGELOG.md` |
| B — docs-site | 49 | 2,823 | all `docs-site/content/*.html` pages |
| C — feature docs | 15 | 2,115 | `docs/NATIVE_CLOUD.md`, `themes.md`, `theme-example/`, `MMCE_RESTORE_PROTOCOL.md`, `MMCE_SD2PSXTD_FIRMWARE_AUDIT.md`, `ECOSYSTEM-COMPARISON.md`, `EMBER-RETROGEM.md`, `FTP-SELECTIVE-PORT-HANDOFF.md`, `architecture-maintenance.md`, `font-generation.md`, `UI-CAPTURES.md`, `THIRD_PARTY_CREDITS.md`, `DISPLAY_FEEDBACK_2026-10-15.md`, `LAUNCHELF.CNF` |
| D — internal docs | 34 | 4,803 | `docs/plans/` (10), `docs/follow-ups/`, `docs/superpowers/**`, `docs/R3Z-*.md` (4), `docs/report-follow-up.md`, `docs/TODO.txt`, `docs/changes.txt`, `docs/keyboard.txt`, three `docs/*.txt` notes, root `USB-STABILITY-AUDIT-2026-10-08.md`, `storage_driver_fixes_plan.md`, `exploits/README.md`, `res/README.md`, `res/UPDATE-CA.md`, `AGENTS.md`, `CLAUDE.md` |

Excluded, stated: `docs/licenses/` (3 licence texts — not claims); `docs-site/static/`, `manifest.json`, `build.py` (assets/generator, no prose claims); binary captures under `docs-site/static/assets/`.

**Claim groups checked: 209.**

| Section | Groups | MATCH | CONTRADICTED | UNVERIFIABLE (whole/partial) |
|---|---|---|---|---|
| A | 52 | 45 | 5 | 4 |
| B | 49 | 36 | 8 | 5 |
| C | 79 | 63 (+1 partial) | 13 groups / 14 discrete | 2 (+1 partial) |
| D | 29 | 23 | 6 | 6 categories |
| **Total** | **209** | **167** | **32 groups / 33 discrete** | **~18** |

The docs are, on the evidence, unusually accurate: every source-verifiable numeric limit in sections A and B checked out — 18 theme presets (`src/dash_themes.c:149-168`), 9 languages, 1 MiB BGM cap, 2048 px / 1.5 MB image caps, 512 library entries, 48 favourites, 250/500/1,000 saves limits, 16 MiB update cap + 4 MiB reserve, ports 21/445/2049/18193, the 61-key R3Z config parity (script-executed, PASS), MMCE command IDs, CA fingerprints, font atlas sizes to the KiB, the keyboard map key-for-key. The 33 discrete contradictions dedup to the 24 findings below (5 are the same falsehood at multiple doc sites) plus 3 cross-references (§3).

Of section D's 4,803 lines, ~60% (~2,900) is future-tense planning, dated historical record, or hardware-acceptance checklists — not claim-checkable, and not counted above.

---

## 2. Findings (benefit order)

### LAUNCHSTATION-200 / The shipped example config's `Misc = MISC/` double-slashes on load, breaking its own FileBrowser shortcut / Category 6
- Locations: doc `docs/LAUNCHELF.CNF:52` (`Misc = MISC/`); code `src/config.c:973` (`sprintf(setting->Misc, "%s/", value)` — appends `/` unconditionally), `src/config.c:975-976` (`Misc_FileBrowser` derived as `Misc` + value), dispatch `src/main_actions.c:732` (`!stricmp(path, setting->Misc_FileBrowser)` exact compare).
- Problem: A user who copies the shipped template gets `Misc = "MISC//"` and `Misc_FileBrowser = "MISC//FileBrowser"`, which fails the exact dispatch compare — the template's own `Misc/FileBrowser` launch key (`docs/LAUNCHELF.CNF:85`) does not fire. Each save/load round-trip appends another slash. This is the one docs finding that breaks function rather than merely describing it wrongly. (Coordinator-verified: all three code sites read at the pin.)
- Fix: Doc-side — change the template to `Misc = MISC`. Code hardening (strip a trailing slash before appending) is also defensible and would forgive every existing user config; LESS prefers the one-character doc fix, with the hardening noted as the author's call.
- Benefit: The shipped example config works as shipped.
- Effort: S / Risk: Low / Confidence: High (READ, coordinator-verified).

### LAUNCHSTATION-201 / docs-site cloud page says automatic sync is off by default; the default is on, in local-backup mode / Category 7
- Locations: doc `docs-site/content/cloud.html:14`; code `src/dash_store.c:263` (`igr_auto_backup = 1`; `:264` `igr_cloud_sync = 0`), `src/dash_settings.c:1163` (three-mode control defaults to Local backup).
- Problem: The default posture is *automatic backup on, cloud sync off* — neither "off" nor cloud. A user trusting the page believes no automatic backup runs; one runs locally after IGR. Four other pages state the default correctly; this page is the outlier. (Coordinator-verified the defaults at `dash_store.c:263-264`.)
- Fix: Doc-side — state the three-mode default as the other pages do.
- Benefit: Users' mental model of a data-moving default matches the binary.
- Effort: S / Risk: Low / Confidence: High (READ, coordinator-verified).

### LAUNCHSTATION-202 / The main dashboard doc names `LAUNCHELF.CNF` as the live settings file at five sites; nothing writes that name any more / Category 7
- Locations: doc `docs/dashboard.md:46,211-213,357,364,478`; code `src/main_boot.c:63` (`LAUNCHSTATION.CNF`; an old `LAUNCHELF.CNF` is imported read-only per the comment at `src/main_boot.c:57-62`), `src/dash_settings.c:563-564,2323`. In-set correction already present: `CHANGELOG.md:21` ("Nothing writes LAUNCHELF.CNF any more").
- Problem: The primary user doc sends users to edit/bookmark a file the program only reads as a legacy import; edits there are silently ignored. (Coordinator-verified `main_boot.c:63`.)
- Fix: Doc-side — replace the five sites with `LAUNCHSTATION.CNF` and describe the legacy import once.
- Benefit: Settings edits land in the file the program actually writes.
- Effort: S / Risk: Low / Confidence: High (READ, coordinator-verified).

### LAUNCHSTATION-203 / Configuration page says CNF files over 128 KB are rejected; the normal load path has no cap / Category 7
- Locations: doc `docs-site/content/configuration.html:26`; code `src/config.c:449-463` (`preloadCNFFd()` normal seekable path — no size check), contrasted with the streamed fallback at `src/config.c:483-507` (cap exists there only) and `dashLoadTextFile` readers.
- Problem: Users are told an oversized hand-edited CNF will be rejected; on the normal path it is loaded whole. The claim describes the fallback path as if it were the rule.
- Fix: Doc-side — scope the 128 KB statement to the streamed fallback, or remove it.
- Benefit: The documented config limits are the real ones.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-204 / FileBrowser note lists six sort modes with Name as default and ELFs ungrouped; the code has seven modes, defaults to Type, and groups ELFs first / Category 7
- Locations: doc `docs/FileBrowser sort+display modes.txt:9-13`; code `src/dash_browser.c:80-83` (seven modes incl. `FB_SORT_TYPE`; `static int fb_sort = FB_SORT_TYPE`), `src/dash_browser.c:208-254` (`fbFileType`/`fbCompare` group ELFs above text/other files under the default).
- Problem: All three assertions in the note — mode count, default, grouping — are wrong against the shipped browser, including the default sort a user sees on first open.
- Fix: Doc-side — rewrite the note's sort section from `dash_browser.c:80-83,208-254`.
- Benefit: The only sort-modes doc describes the browser users actually get.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-205 / Themes page inverts panel transparency: it says 0% removes panels and 100% is opaque; the code is the reverse / Category 7
- Locations: doc `docs-site/content/themes.html:68`; code `src/dash_settings.c:252` (help text), `src/dash_settings.c:1374` (`panel.a = (10−v)*255/10` — 0% = fully opaque).
- Problem: A user following the page sets the opposite of what they want. The settings page itself (`docs-site/content/settings.html:41`) states it correctly — the two published pages contradict each other.
- Fix: Doc-side — invert the sentence in `themes.html`.
- Benefit: One less inverted control in the docs.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-206 / Ecosystem page denies the All-saves view and batch card saves exist; both are shipped and documented elsewhere in the same site / Category 7
- Locations: doc `docs-site/content/ecosystem.html:7`; code `src/dash_card_tool.c:1630-1632` (`CT_ALL_CARDS 64 / CT_ALL_SUBS 256 / CT_ALL_SAVES 1000`) plus the marked-batch flow; correct in-set doc: `docs-site/content/saves.html:56-72`.
- Problem: The comparison page understates the shipped feature set against sibling projects — the class of claim an ecosystem comparison exists to get right.
- Fix: Doc-side — update the two clauses from `dash_card_tool.c` and `saves.html`.
- Benefit: The comparison stops underselling shipped features.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-207 / Cloud doc caps the HTTP body at 64 KiB; the code's limit is 256 KiB / Category 7
- Locations: doc `docs/NATIVE_CLOUD.md:33`; code `include/cloud_http.h:9-10` (`CLOUD_HTTP_BODY_MAX (256 * 1024)`).
- Problem: A stated protocol limit is 4× off. Anyone implementing against the doc (or debugging a large backup) works from the wrong ceiling.
- Fix: Doc-side — 64 KiB → 256 KiB.
- Benefit: The cloud protocol doc's one hard number is right.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-208 / Two docs say `CNF_version` must be the first line; the parser accepts it anywhere, and a missing version loads as legacy / Category 7
- Locations: docs `docs-site/content/configuration.html:35`, `docs/LAUNCHELF.CNF:33-34` ("MUST exist… must be first"); code `src/config.c:951-953` (handled inside the general parse loop, any position), `src/config.c:1104-1105` (missing → legacy path, load succeeds), writer-only first line at `src/config.c:548`. The configuration page itself admits the legacy behaviour at `:26` — an internal inconsistency within one page.
- Problem: Hand-editors are told a hard requirement that does not exist, by the template and the reference page alike.
- Fix: Doc-side — describe first-line as the writer's convention, not a parser requirement.
- Benefit: Hand-edited configs stop being "fixed" to meet a phantom rule.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-209 / Two docs say the PSX (DESR) is treated as having i.Link; `consoleHasIlink()` returns 0 on PSX / Category 7
- Locations: docs `docs-site/content/ilink.html:4`, `CHANGELOG.md:107` ("i.Link stays visible on PSX"); code `src/init.c:261-262` (`if (console_is_PSX) return 0;`), loader refusal at `src/init.c:2433`. Correct in-set docs: `README.md:180`, `docs-site/content/special-hardware.html:18`.
- Problem: Two sites promise PSX users a device entry the code deliberately suppresses. (Coordinator-verified `init.c:261-262`.)
- Fix: Doc-side — align both sites with `special-hardware.html:18`.
- Benefit: PSX users stop hunting for a port entry that never appears.
- Effort: S / Risk: Low / Confidence: High (READ, coordinator-verified).

### LAUNCHSTATION-210 / Example config's comment says Triangle shows About; the same file's empty `LK_Triangle_E1` overrides the built-in About default / Category 7
- Locations: doc `docs/LAUNCHELF.CNF:80` (comment) vs `docs/LAUNCHELF.CNF:88` (`LK_Triangle_E1 =` empty); code `src/config.c:828-829` (built-in default: Triangle = About).
- Problem: The template contradicts itself: copy it and Triangle does nothing, despite the comment's promise. The empty assignment wins over the built-in.
- Fix: Doc-side — delete the empty line from the template or correct the comment to say the key is unassigned.
- Benefit: Template users keep the default the comment advertises.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-211 / Example config says titles are gated by `Menu_Hide_Paths`; the gate is `Show_Titles` / Category 7
- Locations: doc `docs/LAUNCHELF.CNF:432`; code `src/main_menu.c:175` (`Show_Titles` gates titles; `Hide_Paths` at `:180` only strips paths). The same file states it correctly at `docs/LAUNCHELF.CNF:291-295` — an internal self-contradiction.
- Problem: A user toggling the named key to control titles changes path display instead.
- Fix: Doc-side — correct `:432` to match `:291-295`.
- Benefit: The template's key documentation agrees with itself and the code.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-212 / Example config's Neutrino comment omits the SMB backend and claims `-qb` is unconditional; SMB is supported and suppresses `-qb` / Category 7
- Locations: doc `docs/LAUNCHELF.CNF:370-372`; code `src/neutrino_iso.c:141-144` (`bsd="smb"` supported), `src/neutrino_iso.c:259` (`-qb` omitted for SMB).
- Problem: The backend list in the shipped template is incomplete in exactly the network case, and the flag claim is wrong for it.
- Fix: Doc-side — add SMB to the list and scope the `-qb` sentence.
- Benefit: Network-boot users get a template that mentions their backend.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-213 / CHANGELOG says the share editor writes SMB2.CNF and SMB1 opens via `smb1:/`; shares live in LAUNCHSTATION.CNF and SMB1 is a folder in `smb:/` / Category 7
- Locations: doc `CHANGELOG.md:52`; code `src/dash_settings.c:563-564` ("SMB2.CNF itself is never written" — shares are a block in `LAUNCHSTATION.CNF`). Correct in-set doc: the newer bullet at `CHANGELOG.md:26`.
- Problem: The changelog's own older bullet contradicts its newer one and the code; users go looking for a file that is never written.
- Fix: Doc-side — correct or annotate the `:52` bullet.
- Benefit: One less phantom file in the project's own history.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-214 / README says HDD/ATA is "Hidden on Deckard (SCPH-75K+)"; there is no Deckard check — gating is PSX-only plus the Hide-HDD setting / Category 7
**Verification note (2026-11-14, full verification): WITHDRAWN — REFUTED on adversarial re-derivation: the Deckard gate exists — `shouldHideHddAtaDevices()` (main_startup.c:746-756) hides HDD/ATA when `romver_prefix >= 220` and is consumed by filer.c and dash_devices.c; README.md:181 is substantively correct; see Sable-VERIFY2-LAUNCHSTATION.md. Do not act on this finding.**
- Locations: doc `README.md:181`; code: no Deckard/75K gate exists anywhere in `src/`/`include/`/`iop/` (exhaustive grep); actual gates at `src/dash_devices.c:401,405` and `src/filer.c:33-41` (PSX-only suppression + `Hide_HDD` setting). The bullet's second half (second unit hidden) MATCHes.
- Problem: Deckard owners are told a device is hidden from them when it is not; non-Deckard users with the setting on get no explanation.
- Fix: Doc-side — replace the Deckard clause with the PSX + setting gates.
- Benefit: The device-visibility table in the README matches the device code.
- Effort: S / Risk: Low / Confidence: High (READ — absence verified by exhaustive grep).

### LAUNCHSTATION-215 / Issue-plan doc states MC/VMC copies use 16 KiB chunks; the copy path uses 128 KiB for those destinations and no 16 KiB chunking exists / Category 7
- Locations: doc `docs/plans/2026-10-19-zack-issues.md` (#184 row); code `src/filer_copy.c:95-96` (MC/VMC destinations use 131072). The cap's removal is recorded in `docs/plans/2026-10-24-stable-issue-recovery.md` (#206), but the zack-issues banner supersedes only #187, leaving the stale present-tense row live.
- Problem: A present-tense implementation claim in a tracked plan is 8× off the shipped chunk size — the kind of number a later agent or contributor will build on.
- Fix: Doc-side — correct the row or extend the supersession banner to cover it.
- Benefit: The plan stops teaching a wrong constant.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-216 / Themes page lists two UI fonts; the code ships three (`emotion_engine` omitted — though the same page mentions it later) / Category 7
- Locations: docs `docs-site/content/themes.html:58,125` (only `roboto`, `system`); code `src/dash_themes.c:123` (`font_ids = {"roboto","system","emotion_engine"}`), `src/dash_settings.c:978`. In-page inconsistency: `themes.html:160` already mentions Emotion Engine.
- Problem: Theme authors reading the font list don't learn the third font ID the picker offers.
- Fix: Doc-side — add `emotion_engine` at `:58` and `:125`.
- Benefit: The theme-author reference is complete.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-217 / Three docs stop the TV-mode list at 576p; mode 6 (576i) exists and is exposed in both UIs / Category 7
- Locations: docs `docs/New resolutions.txt:8-10` ("TV_mode = 0..5"), `docs/DISPLAY_FEEDBACK_2026-10-15.md:4` (576p as the "last" entry), `docs/LAUNCHELF.CNF:214-220` (list stops at 5=576p); code `include/launchelf.h:543-545` (`TV_mode_576I`, range 0..6), `src/dash_settings.c:966` (`tv_names` ends `"576i"`).
- Problem: The mode added most recently is missing from all three places a user would enumerate modes — including the shipped template's own value list.
- Fix: Doc-side — add mode 6 at all three sites.
- Benefit: Config authors can discover 576i without reading the header.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-218 / Five doc sites deny a root LICENSE exists; a scoped 0BSD LICENSE has been tracked at root since 2026-11-02 / Category 7
- Locations: docs `docs-site/content/credits.html:62`, `docs-site/content/contributing.html:6`, `docs-site/content/ecosystem.html:4`, `docs/THIRD_PARTY_CREDITS.md:73`, `docs/architecture-maintenance.md:132`; code/tree: root `LICENSE` (1,096 bytes, scoped 0BSD, commit `05bc1b0`, 2026-11-02 — before the credits file's own 2026-11-10 review date). (Coordinator-verified: the file exists at the pin.)
- Problem: The "no blanket licence" caution the sentence exists to support remains valid — the LICENSE's own scope section says so — but the factual premise ("no root LICENSE file") is false at five sites, including the credits page users are sent to for licensing.
- Fix: Doc-side — at all five sites, replace the denial with the scoped statement the LICENSE itself makes.
- Benefit: Licensing statements stop being falsifiable by `ls`.
- Effort: S / Risk: Low / Confidence: High (READ, coordinator-verified).

### LAUNCHSTATION-219 / Two docs inventory the update CA bundle as two roots; the bundle contains three — the omitted E46 root is load-bearing / Category 7
- Locations: docs `res/README.md:9`, `docs/THIRD_PARTY_CREDITS.md:57` (both name Root R46 + ISRG Root X1 only); artefact `res/WLE-UPDATE-CA.PEM` (3 certificates; recomputed DER SHA-256s match `res/UPDATE-CA.md`'s own table, which includes the Sectigo Root E46 both prose docs omit).
- Problem: The prose inventory of the trust bundle the updater relies on is short one root, and the omitted root is in the verification path. `res/UPDATE-CA.md` has it right; the two prose summaries don't.
- Fix: Doc-side — add E46 at both sites.
- Benefit: The CA inventory matches the bundle and its own fingerprint table.
- Effort: S / Risk: Low / Confidence: High (READ — fingerprints recomputed by section D).

### LAUNCHSTATION-220 / Follow-up manifest says the move-manifest work is "Implemented on the draft PR branch; not merged"; it is merged at the pin / Category 7
- Locations: doc `docs/follow-ups/move-manifest.md:3`; code `src/filer_copy.c:135` (`move_manifest` in tree), commit `5c4083f` (2026-11-02) verified an ancestor of the pin.
- Problem: A status header in a tracked follow-up doc asserts the opposite of the tree state — the -028 staleness class, but as a discrete false present-tense claim rather than general clutter.
- Fix: Doc-side — update the header to merged, or delete the follow-up (its purpose is served).
- Benefit: The follow-up stops misreporting its own feature's status.
- Effort: S / Risk: Low / Confidence: High (READ — ancestry verified).

### LAUNCHSTATION-221 / architecture-maintenance's inventories don't match the tree: largest-files list omits the two largest files, workflow table lists 3 of 6 / Category 7
- Locations: doc `docs/architecture-maintenance.md:89-92` (largest files — omits `dash_card_tool.c` 2,903 and `filer.c` 2,556 lines, lists smaller `filer_browser.c` 2,528 / `main_exploit_installer.c` 2,519), `docs/architecture-maintenance.md:35-41` (3 workflows); tree `.github/workflows/` has 6 (also `download-metrics.yml`, `neutrino-watch.yml`, `ps2build-sdk-watch.yml`).
- Problem: The one doc whose job is an accurate map of the tree is wrong in both of its inventories — and it is the doc a maintainer consults before refactoring.
- Fix: Doc-side — regenerate both lists from the tree.
- Benefit: The maintenance map matches the territory.
- Effort: S / Risk: Low / Confidence: High (READ — counts measured at the pin).

### LAUNCHSTATION-222 / Wiki build page says "The retained Makefile describes historical profiles"; no Makefile exists at the pin — it was retired 2026-10-26 / Category 7
- Locations: doc `docs/wiki/Building-and-Validation.md:18`; tree: no `Makefile` at the pin (verified by path existence); correct in-set docs: `README.md:194`, `docs/BUILDING.md:24` (retirement dated 2026-10-26).
- Problem: The wiki — the set's canonical build page — points readers at a file that doesn't exist. Also this round's only pure dead reference in section A's set (section B found zero dead references across 34 sampled paths; section D found four in internal docs — agent worktree paths and supplied files, recorded in `08D-internal-docs.md`, not filed: they are agent-scratch references, not user-facing paths).
- Fix: Doc-side — delete the sentence or point at `ps2.yaml` as the other docs do.
- Benefit: The build page stops citing a ghost file.
- Effort: S / Risk: Low / Confidence: High (READ).

### LAUNCHSTATION-223 / Display-feedback doc claims a 320×256 wallpaper help string "still" exists in dash_settings.c; no such string exists at the pin / Category 7
- Locations: doc `docs/DISPLAY_FEEDBACK_2026-10-15.md:7-8`; code: exhaustive grep of `src/dash_settings.c` — the string is absent.
- Problem: A feedback doc's one checkable code claim is a phantom: anyone acting on it searches for text that isn't there. Minor in impact, filed because it is the doc's sole actionable assertion and it is false.
- Fix: Doc-side — delete the claim or mark the doc historical (cf. -028).
- Benefit: The doc stops sending readers on a grep that cannot succeed.
- Effort: S / Risk: Low / Confidence: High (READ — absence grep-verified).

---

## 3. Cross-references (doc claims owned by existing findings — NOT re-filed)

- **LAUNCHSTATION-187** — `docs/follow-ups/move-manifest.md:26-30` claims the PSU backup/restore host suites "pass", and `CHANGELOG.md:8` cites `tests/test_copy_psu_listing.py` as the coverage for the PSU-skip feature. The test fails deterministically at the pin (round 7, reproduced again by section D this round). The false validation claim is a consequence of -187's fact; the fix order is -187 first, then the manifest sentence becomes checkable again.
- **LAUNCHSTATION-194** — `docs/architecture-maintenance.md:44-45` asserts "Every `tests/test_*.py` script is listed there" (in `compile.yml`). 213 test files exist; 211 are invoked; `test_boot_deferred_init.py` and `test_usb_stat_timeout.py` run nowhere — exactly -194's substance. The doc sentence is wrong because -194 is open.
- **LAUNCHSTATION-119** — `docs-site/content/special-hardware.html:15` and `docs-site/content/devices.html:75` promise per-title XPARAM (Deckard) behaviour. The mechanism was re-confirmed this round at `iop/xparam/include/xparam.h:36` (`GET_XPARAM_DB_SIZE()` ≡ 1) and `iop/xparam/src/xparam.c:55`: the promise fails because -119 is open, not because the docs misdescribe the intended feature. No doc in any section promises behaviour that would contradict -017/-034 (update caps are stated correctly where stated at all — `developers.html:20` carries the 16 MiB figure; user-facing updater prose omits it, an omission, not a contradiction), -085/-118 (all docs correctly describe vmc0:/vmc1: only), -053/-063/-064 (no doc promises large-file SMB serving; the SMB server is documented as memory-card-scoped), -090, or -186 (no doc makes a randomness claim).

## 4. Unverifiable from source (listed, not filed)

Per section files: build-974 capture provenance and VRAM MiB figures (A); whether official release builds actually embed the OAuth client (CI-secret fact — the injection mechanism MATCHes; secret values were never read, location only: generated `wle_cloud_client.h` via `scripts/gen_cloud_client.py`, consumed at `src/cloud_google.c:20-24`); the kit's bundled Neutrino upstream version/date and ~1.6/4.6 MB ELF sizes (B/A — external artefacts); firmware behaviours (MMCE/SD2PSXTD) and hardware acceptance results (D — tester claims with no in-repo evidence either way); external CI run outcomes; live-network CA checks; upstream-side comparisons in `ECOSYSTEM-COMPARISON.md` (other projects' behaviour is not in this repo). ~18 groups/categories in total; each is itemised in its section file.

## 5. Internal inconsistencies (beyond the findings above)

Recorded in the section files; the load-bearing ones are already findings (205: themes vs settings pages; 208: configuration page vs itself; 211: LAUNCHELF.CNF vs itself; 213: CHANGELOG vs itself; 218: five sites vs the tree). Additionally: section D found `AGENTS.md`/`CLAUDE.md:5` forbidding AI Co-Authored-By trailers while `docs/plans/2026-10-18-cloud-handoff.md:54` instructs "Every commit must credit OpenAI" — an instruction conflict for agents, not a code claim, recorded here not filed; section B found `docs-site/content/devices.html:4` carrying two drafts of one callout concatenated, and the credits "not bundled" wording (Neutrino/POPStarter/Ember) sitting awkwardly against the full-kit contents at `install.html:18-24` (kit composition is a release-artefact fact — unverifiable from source, noted not filed).

---

## 6. Updated residue statement (supersedes round 7's for item (d))

Round 8 closed the last unexamined class. What remains open for Launchstation, exactly:

- **(a) The smbd link test — still un-auditable in this environment (no PS2 toolchain).** It still gates only the R4-R3(c)/(d) four deletions and the GC-reachability class of the libsmb2 client findings (-188, -189). Nothing from round 8 depends on it.
- **(b) libsmb2 client/crypto — CLOSED round 7.** Unchanged.
- **(c) tests/ — CLOSED round 7.** Unchanged (-187 remains an open product/harness question, not a coverage gap).
- **(d) docs/docs-site prose — CLOSED round 8.** All 109 prose files (11,439 lines) read in full; 209 claim groups checked: 167 MATCH, 32 groups contradicted (24 findings after dedup, LAUNCHSTATION-200…223), ~18 unverifiable-from-source groups listed not filed, 3 doc claims cross-referenced to open findings (-187, -194, -119). The class is closed as *read and claims-checked*; the docs' future drift is a maintenance fact, not an audit residue.
- **(e) Binaries/IRX/payloads — unchanged in every round:** sized and hashed, never disassembled.
- **(f) Build glue — CLOSED round 7.** Unchanged.
- **(g) Assembly — CLOSED round 7.** Unchanged.
- **(h) xparam data — CLOSED round 7.** Unchanged.
- **(i) One open question, still not a finding:** ps2ips `do_dns_getserver`'s unchecked lwIP dereference — external PS2SDK/lwIP contract, unresolvable from the clones. Round 8 touched nothing that changes it.
- **(j) Standing limits, unchanged:** `ps2smb.h` remains an external PS2SDK header absent from the clones (-066's caveat as-is). No PS2 build, link test, or hardware result exists for anything in this audit; all Category 5 magnitudes remain structural; doc claims about hardware behaviour, firmware, release artefacts and CI secrets are UNVERIFIABLE from source by construction and were listed, never filed.

**Net state:** every source-shaped and prose-shaped class in this repo has now been read at the pin — EE/IOP C, headers and macros, assembly, build glue, tests (read + executed), fetched libsmb2 bodies, xparam data, and now all documentation prose. A round 9 on this pin has no unread class to take: what remains is environment-blocked (the link test, builds, hardware), external (the ps2smb.h/lwIP contracts, upstream facts), or binary (disassembly, which no round has been scoped to do). Productive continuations are a pin move or a scope change, not another reading round.

*End of round 8 — Launchstation. 24 new findings (LAUNCHSTATION-200…223), 0 revisions forced, pin held, read-only held. Section evidence: `round8-sections/08A-core-docs.md`, `08B-docssite.md`, `08C-feature-docs.md`, `08D-internal-docs.md`.*
