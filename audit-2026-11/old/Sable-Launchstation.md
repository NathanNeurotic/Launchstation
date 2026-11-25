# Sable Audit — Launchstation

- **Auditor:** Sable
- **Repo:** NathanNeurotic/Launchstation
- **Pin:** `master` @ `8cd3329a98c09a570bfbf60137717edd42324990` (2026-11-13 09:19:42 -0800, "Updater: stop making a .BAK of the old ELF"), fresh clone at `~/workspace/audit-clones-2026-11-13/Launchstation`, checked 2026-11-13 ~10:04 PST. See `PINS.md`.
- **Method:** READ ONLY. No files in the repo were edited, no branches/PRs/issues/workflows touched. Inventory via `git ls-files`, grep sweeps, line-level reading of the main EE sources, `ps2.yaml` (the ps2build project definition — there is no top-level Makefile), and all 6 workflows. Two host tests were run for evidence (`tests/test_browser_sort.py`, `tests/test_navigation_wraparound.py` — both PASS at the pin).
- **Scope note:** Centre scrolling and wraparound navigation are already on this master (verified present; `test_navigation_wraparound.py` passes) — not flagged as missing.
- **Rule applied:** LESS, NOT MORE. Every fix below deletes or simplifies unless a justification is given. Benefit figures are estimates and are labelled as such.

## Inventory at the pin

- 1,065 tracked files: 241 `.c`, 234 `.py`, 208 `.h`, 78 `.png`, 66 `.md`, 49 `.html`, 7 `.irx`.
- Largest areas: `tests/` 273 files, `iop/` 266 files, `src/` 126 files (~119,571 lines of EE C incl. headers), `docs-site/` 123 files, `include/` 66, `docs/` 55, `res/` 44.
- Biggest single sources: `src/dash_settings.c` 4,096 lines, `src/dash_browser.c` 3,616, `src/init.c` 3,205, `src/dash_card_tool.c` 2,903, `src/filer.c` 2,556, `src/filer_browser.c` 2,528, `src/main_exploit_installer.c` 2,519.
- Build: one 700-line `ps2.yaml` defining 26 targets (24 IOP/EE-lib targets + loader + the `wLaunchELF` EE target, which embeds ~55 IRX modules — `ps2.yaml:643-700`).
- CI: `.github/workflows/compile.yml` alone is 812 lines and invokes `python3 tests/…` 212 times (grep count).
- Remote refs at pin: 104 branches, 140 tags (88 of them `build-*`).

---

## Findings (sorted by benefit, descending)

### LAUNCHSTATION-001 / All nine languages are compiled into the ELF / Category 4 (Size and memory)
- **Location(s):** Launchstation/src/lang.c:5-56
- **Problem:** `lang.c` `#include`s all nine `Lang/*.LNG` tables (ENG, ITA, SPA, GER, POR, PTBR, POL, FRE, HUN) as static arrays, so every release ELF carries every language's strings whether or not the user ever selects that language. The eight non-English files total ~305 KB of source text (36,423–39,167 bytes each, `wc -c Lang/*`).
- **Fix:** Keep English compiled in; ship the other eight as external `.LNG` files loaded on selection. The loading machinery already exists in the same file (`Load_External_Language`, `src/lang.c:452`; `External_Lang_Buffer`, `src/lang.c:63`) — no new code is needed, only deleting eight `#include` blocks and their static arrays.
- **Benefit:** ~250–300 KB ELF size (estimate; string bytes minus pointer-table overhead) on a 32 MB machine, plus the same in download/update size.
- **Effort:** M. **Risk:** Med (language switching must fall back to English when the file is absent — that path is the existing external-language path). **Confidence:** High (READ).

### LAUNCHSTATION-002 / Three pre-rasterised font families × three sizes = 585,728 bytes of pixel data in .rodata / Category 4
- **Location(s):** Launchstation/src/dash_font_data.c:8 (Roboto: 34,816+57,344+122,880 = 215,040 B), Launchstation/src/dash_font_emotion_engine.c:8 (176,128 B), Launchstation/src/dash_font_emotion_engine_bold.c (194,560 B); selected in Launchstation/src/dash_font.c:380; layout in Launchstation/include/dash_font_data.h:14-16,37-39
- **Problem:** All three families at all three sizes are linked into the ELF permanently, although `include/dash_font_data.h:37` itself notes "only the selected family's textures are resident" — the other two families' ~390–570 KB still occupies ELF space and EE RAM as `.rodata` for the life of the process.
- **Fix:** Simplest: drop one family (the bold Emotion Engine face is the most expendable — the Roboto path already synthesises bold, `src/dash_font.c:568`). Larger win, still a deletion: keep one family in the ELF and load alternates from a file beside the ELF, like the existing `BGM.ADP` sidecar pattern (`src/dash_sound.c:362-409`).
- **Benefit:** 194,560 B ELF/RAM for deleting the bold family alone; up to ~390 KB if two of three families become sidecars (estimates from the array sizes above).
- **Effort:** M. **Risk:** Med (theme presets referencing a removed family must fall back). **Confidence:** High (READ).

### LAUNCHSTATION-003 / Legacy PS2SDK-v1.0 network stack is a second, parallel TCP/IP stack kept for one config flag / Category 2/8 (Duplication / redundant feature)
- **Location(s):** Launchstation/src/embed_blobs.S:77-80 (embeds `iop/__v10/ps2dev9.irx` 13 KB and `iop/__v10/ps2ip.irx` 118,183 B — the single largest embedded IRX); Launchstation/src/init.c:28-30, 496-499, 639-660 (variant selection and the separate no-NETMAN load path); Launchstation/ps2.yaml:643ff (`smap-ps2ip:ps2smap` also embedded); Launchstation/src/config.c:858,1018 and Launchstation/include/launchelf.h:320 (`legacy_net_drivers` setting); Launchstation/src/dash_settings.c:1188,1906 (settings UI row for it)
- **Problem:** One boolean (`legacy_net_drivers`, "MrRexD network-compatibility fallback") drags in a complete second DEV9 + PS2IP + SMAP stack, a second load path in `load_ps2ip()`, a config key and a settings row. It is a fallback for an unspecified compatibility case, exactly the kind of speculative redundancy the audit rule targets.
- **Fix:** Delete the v1.0 stack, the `DEV9_VARIANT_PS2SDK_V10` path, the setting and its UI row. If a specific named device genuinely needs it, that is a separate build, not bytes in every user's ELF.
- **Benefit:** ~131 KB ELF (13 KB + 118 KB IRX, measured file sizes) plus the `smap-ps2ip` module, ~80 lines of `init.c` branches, one config key.
- **Effort:** M. **Risk:** Med (any user actually relying on legacy drivers loses them — that is the decision to make explicitly). **Confidence:** High (READ).

### LAUNCHSTATION-004 / Native Google Drive "cloud" subsystem (HTTP + TLS + OAuth + QR) is built into every ELF / Category 8/4
- **Location(s):** Launchstation/src/cloud_http.c:1-1312, Launchstation/src/cloud_google.c:1-1635, Launchstation/src/qrcodegen.c:1-1027 (vendored QR generator), Launchstation/include/cloud_google_roots.h (embedded CA roots, parsed at Launchstation/src/cloud_http.c:397-398), Launchstation/ps2.yaml:503-507,614-642 (`WLE_NATIVE_CLOUD` defined unconditionally; `mbedtls, mbedx509, mbedcrypto` in the EE link), Launchstation/src/dash_tools.c:179-239 (QR device-flow screen), Launchstation/scripts/gen_cloud_client.py:28-52 (OAuth client id/secret baked in at CI time from GitHub secrets — values not reproduced here), Launchstation/windows/cloud-relay/ (a second, PC-side implementation of the same relay idea)
- **Problem:** A file manager/launcher carries a full HTTPS + Google OAuth + Drive sync stack — at least 3,974 lines of first-party C plus Mbed TLS — for a feature orthogonal to launching and file management. It also forces a CA-roots blob, a QR library and a baked-in OAuth client into every build.
- **Fix:** Delete the native cloud feature (the PC-side relay in `windows/cloud-relay/` already covers the same user story without PS2-side TLS), or, if it must stay, make it a non-default build variant so the normal ELF does not pay for it. No replacement layer is proposed.
- **Benefit:** ≥3,974 lines of EE C removed, plus Mbed TLS link size and the QR/roots data (ELF saving INFERRED, plausibly several hundred KB — not measured without a build).
- **Effort:** L. **Risk:** High (feature removal; needs the user's call). **Confidence:** High on size/complexity (READ), Med on the ELF byte estimate (INFERRED).

### LAUNCHSTATION-005 / SMB1 client (smbman) duplicates the SMB2/3 client (smb2man) / Category 2
- **Location(s):** Launchstation/ps2.yaml:364-385 (`smbman` target: 7 C sources incl. its own DES/MD4 auth), Launchstation/ps2.yaml:249-277 (`smb2man`), both embedded at Launchstation/ps2.yaml:643ff; Launchstation/src/init.c:40-41,151-152,934-1000 (parallel `have_smbman`/`have_smb2man` state and load paths, `smb:` vs `smb1:` devices)
- **Problem:** Two complete SMB client stacks are embedded so a legacy `smb1:` device can sit beside `smb:`. SMB1 servers are the shrinking minority, and every user pays the ELF/IOP cost of both DES-era auth and the modern stack.
- **Fix:** Delete `iop/smbman`, the `smb1:` device and its branch in `loadSmb…` (`src/init.c:975`). Users with SMB1-only NAS use the PS2-Servers side or an SMB2 share.
- **Benefit:** One IOP module (INFERRED tens of KB ELF), 7 source files, one config/share code path.
- **Effort:** M. **Risk:** Med (SMB1-only devices lose access). **Confidence:** High (READ).

### LAUNCHSTATION-006 / A full libsmb2-based SMB *server* (smbd) is embedded alongside the FTP server, host server and NetFS / Category 8/2
- **Location(s):** Launchstation/ps2.yaml:295-363 (`smbd` target listing ~60 vendored libsmb2 sources, fetched at build time by `scripts/fetch_libsmb2.sh`), embedded at Launchstation/ps2.yaml:643ff; started on demand from Launchstation/src/init.c:2780; alongside `ps2ftpd` (Launchstation/ps2.yaml:215-231), `ps2host` (Launchstation/ps2.yaml:232-248) and SDK `ps2netfs`, also embedded
- **Problem:** The ELF embeds four different ways for a PC to reach the PS2's files (FTP, host:, NetFS, and now an SMB2/3 server serving only mc0/mc1). Each is a network-facing parser maintained forever; smbd is by far the largest and newest.
- **Fix:** Pick one PC-access server to keep (FTP is the incumbent) and delete smbd + the libsmb2 fetch, or move smbd to a standalone IRX the user drops beside the ELF. Simplest net win is deletion.
- **Benefit:** ~60 vendored source files out of the build, one network attack surface removed, ELF saving INFERRED (libsmb2 server is a large IOP module; not measured without a build).
- **Effort:** M. **Risk:** Med (users of the MC-over-SMB feature, issue #249 per the `ps2.yaml:295` comment, lose it). **Confidence:** High (READ).

### LAUNCHSTATION-007 / Exploit installer + KELF signer + ~148 KB of exploit payloads ship in the everyday launcher / Category 8
- **Location(s):** Launchstation/src/main_exploit_installer.c:1-2519, Launchstation/src/kelf_signer.c:1-1153, Launchstation/src/embed_blobs.S:54-73 (embeds XLF headers/bodies, two KERNEL images, four region `.sys` files, `IOPRP_LTS.IMG`), Launchstation/exploits/ (147,662 B total across 14 files)
- **Problem:** Installing boot exploits is a once-per-card operation, but its code, signer and payloads ride in every ELF every user downloads and loads, and the installer is the 6th-largest EE source file.
- **Fix:** Split it the honest way — by deletion from this binary: ship the installer as its own small ELF (the release kit already packages multiple apps via `scripts/package_apps.py`). No new framework; one less feature compiled into the launcher.
- **Benefit:** ~3,672 lines of EE C and ~148 KB of payload data out of the main ELF (payload total measured; ELF saving slightly less after packing, INFERRED).
- **Effort:** L. **Risk:** Med. **Confidence:** High (READ).

### LAUNCHSTATION-008 / docs-site carries 17.4 MB of media in the repo, including the same demos as both GIF and MP4 / Category 7 (Repo clutter)
- **Location(s):** Launchstation/docs-site/ (123 files, 17,360,636 B total by `du -cb`); Launchstation/docs-site/static/assets/ui/theme-highlights.gif (4,885,148 B) **and** theme-highlights.mp4 (597,007 B); application-views.gif (2,894,342 B) **and** application-views.mp4 (337,371 B); ~60 further UI PNGs of 110–200 KB each
- **Problem:** The documentation site's screenshot library is larger than the rest of the source tree's payload combined, and two demos are stored twice in two formats. Every clone pays for it forever.
- **Fix:** Delete the GIF versions where an MP4 exists (GIF is the larger copy in both pairs), and downscale/re-encode the 110–200 KB PNG captures. Keep the site, lose the bytes.
- **Benefit:** ≥7.7 MB of repo size from the two GIFs alone (measured file sizes); more from PNG re-encode (estimate).
- **Effort:** S. **Risk:** Low (verify the site templates reference the MP4s before deleting the GIFs). **Confidence:** High (READ).

### LAUNCHSTATION-009 / 213 Python "tests" (31,353 lines) mostly scrape C source as text; CI runs 212 of them in the build job / Category 7/3
- **Location(s):** Launchstation/tests/ (273 files, 37,919 lines total; 213 `test_*.py`, 31,353 lines by `wc -l`); 127 of the Python tests call `read_text()` on production sources (grep count); Launchstation/.github/workflows/compile.yml (212 `python3 tests/…` invocations in one job, grep count); example: Launchstation/tests/test_browser_sort.py:1-25 (regex-extracts a function body out of `src/dash_browser.c` and compiles the excerpt)
- **Problem:** The test suite is ~26% the line count of the product code, and much of it asserts that source text contains/forbids particular strings — tests that break on renames and pass on broken behaviour. They also run serially inside the build workflow, taxing every push/PR.
- **Fix:** Delete the pure source-grep tests whose assertion is "string X exists in file Y" (keep the ones that compile and *execute* production code, e.g. the browser-sort harness, and the host-compiled IOP tests under `tests/smb2man_live`, `tests/smbd_live`, `tests/ftp`). Run what remains as one `pytest` invocation in a separate job so the build job is build-only.
- **Benefit:** INFERRED thousands of lines of test code and minutes of CI per run; the real win is a suite whose failure means behaviour broke.
- **Effort:** M. **Risk:** Low (deleting assertions, not coverage of executed behaviour — each deletion still needs a skim). **Confidence:** High on counts (READ), Med on the per-file classification (INFERRED — only a sample was read in full).

### LAUNCHSTATION-010 / CI builds the entire project twice for the DS34 variant; DS34 code is dead in the default build / Category 7/1
- **Location(s):** Launchstation/.github/workflows/compile.yml:350-360 ("Build optional DS34 controller variant" — a second full `ps2build build`, plus four downstream verify steps that take both trees as arguments, e.g. line 403); Launchstation/scripts/configure_ds34.py:1-45 (generates a whole second `ps2.yaml` by string replacement); Launchstation/src/pad_ds34.c:1-519 (the **only** EE source not referenced by the default `ps2.yaml` — verified by checking every `src/*.c` against `ps2.yaml`); Launchstation/iop/ds34bt/, Launchstation/iop/ds34usb/; Launchstation/src/init.c:87-2973 (14 `#ifdef DS34` sites)
- **Problem:** A niche DualShock-3/4 adapter doubles the main build's cost in CI, needs a generated-config hack, and leaves a 519-line alternate pad implementation plus 14 conditional sites in the shipping sources.
- **Fix:** Build DS34 only on release/manual dispatch, not on every push/PR; or drop the variant entirely if usage is negligible. Removing the per-push second build is the S-effort version.
- **Benefit:** ~Half the compile job's build time per push (INFERRED from two equal builds); 519+ lines and 14 `#ifdef`s if dropped entirely.
- **Effort:** S (CI trigger change) / M (full removal). **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-011 / Two complete UI sound themes are both embedded / Category 4
- **Location(s):** Launchstation/src/embed_blobs.S:82-98 (7 ADP files from `res/sfx/` plus 8 from `res/sfx_ps2/`); Launchstation/res/sfx/ + Launchstation/res/sfx_ps2/ (359,840 B total across both sets, `du -cb`)
- **Problem:** Both the "Sony" and the "PS2" sound themes are baked into the ELF so a settings toggle can switch — ~180 KB (estimate, roughly half the total) is spent on the theme not in use.
- **Fix:** Embed one theme; load the alternate from the app folder like `BGM.ADP` already is (`src/dash_sound.c:362`). Or simply delete the less-used theme.
- **Benefit:** ~180 KB ELF (estimate from measured totals).
- **Effort:** S/M. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-012 / Legacy 8×16 font (font_uLE) is a third text path beside the dashboard font system / Category 2
- **Location(s):** Launchstation/src/font_uLE.c:1-652 (~41 KB source), declared Launchstation/include/launchelf.h:717-719, used as fallback at Launchstation/src/dash_font.c:155; Launchstation/src/filer.c:92 (`memalign(64, 55016)` font load in the classic filer)
- **Problem:** The classic filer's bitmap font, the dashboard's three rasterised families (LAUNCHSTATION-002) and the GS text path in `src/draw_text.c` are three overlapping ways to put text on screen, each with its own data.
- **Fix:** When the classic filer screens finish migrating to the dashboard UI, delete `font_uLE.c` and the fallback; until then this is a marker, not an action — listed once here so the duplication is on record.
- **Benefit:** ~41 KB source / its compiled table (INFERRED ~55 KB by the filer's own allocation figure) when the migration completes.
- **Effort:** L. **Risk:** Med. **Confidence:** Med (READ for existence, INFERRED for removability timing).

### LAUNCHSTATION-013 / Dead media assets: bgm.ogg (462 KB) and res/sfx/boot.adp (144 KB) are neither embedded nor packaged / Category 1/7
- **Location(s):** Launchstation/res/bgm.ogg (462,185 B; the repo itself says "Not referenced by any build script and not packaged" — Launchstation/res/README.md:15, and Launchstation/docs/THIRD_PARTY_CREDITS.md:68); Launchstation/res/sfx/boot.adp (143,984 B — the current `src/embed_blobs.S:82-98` embeds the *other* set's boot sound, `sfx2_boot_adp` from `res/sfx_ps2/`; the only references to `res/sfx/boot.adp` are in the stale plan at Launchstation/docs/superpowers/plans/2026-09-26-osdsys-dash.md:584,607)
- **Problem:** 606 KB of dead audio sits in the tree, one of them self-documented as unused.
- **Fix:** Delete both files. If `bgm.ogg` is wanted as source material, it belongs outside the repo (release asset / Drive), not in every clone.
- **Benefit:** 606,169 B repo size (measured).
- **Effort:** S. **Risk:** Low. **Confidence:** High for bgm.ogg (READ, self-documented), Med for boot.adp (INFERRED from absence in the current embed list).

### LAUNCHSTATION-014 / Busy-wait spin loops burn the EE and freeze the UI for up to 1.5 s / Category 5 (Performance)
- **Location(s):** Launchstation/src/editor_file.c:59,182,208,250,319 (five copies of `while (Timer() < WaitTime + 1500) ;  // print operation result during 1.5 sec.`); Launchstation/src/draw.c:288-289 (5 ms spin in `drawScr()`); Launchstation/src/filer.c:371,2016,2163 (empty 100 ms backoff spins, with a second implementation of the same wait at Launchstation/src/filer.c:359-375 duplicated at 1997ff and 2122ff)
- **Problem:** Empty `while (Timer() < …)` loops spin the EE at full speed, block pad input and animation for the duration (1.5 s in the text editor, five times), and the filer copy-pastes its wait/backoff block three times.
- **Fix:** Replace the editor's five spins with the dashboard's existing timed-message/modal mechanism (it already displays timed toasts without blocking); replace the filer's three copies with one wait helper — justified under the audit rule because the same logic appears 3× and sharing it removes code. At minimum, delete the spins and return immediately.
- **Benefit:** Up to 1.5 s of UI freeze removed per editor file operation; ~60 duplicated lines removed in `filer.c` (estimate).
- **Effort:** M. **Risk:** Low/Med. **Confidence:** High (READ).

### LAUNCHSTATION-015 / sprintf/strcpy/strcat are pervasive (863 grep hits), including the config-save chain / Category 6 (Bugs and security)
- **Location(s):** grep `strcpy(|strcat(|sprintf(` over Launchstation/src + Launchstation/include + Launchstation/iop = 863 hits; concrete chain: Launchstation/src/config.c:545-712 (`sprintf(tmp + CNF_size, …)` with `%n` measurement into a heap buffer sized `sizeof(SETTING) + sizeof(PathPad) + 8192 + smb_len` at line 545 — correctness depends on every field staying inside that arithmetic), Launchstation/src/config.c:732-782 (repeated `strcpy(c, LaunchElfDir); strcat(c, CNF);` path assembly)
- **Problem:** Unbounded string writes into fixed/derived buffers, fed in part by config-file values (paths, SMB user/password, share definitions) that come from a memory card or a downloaded config. One over-long field overflows the heap buffer during save.
- **Fix:** Convert the config-save path and path assembly to the bounded pattern the newer code already uses (`snprintf` with the return checked against the buffer size, as in `src/self_update.c:268`). This is a replacement, not an addition — same line count, bounded writes. Do it for `config.c` first; the 863-hit tail can follow file by file.
- **Benefit:** Removes a real heap-overflow class on user-controlled data (INFERRED exploitability; the unbounded writes themselves are READ).
- **Effort:** M (config.c) / L (tree-wide). **Risk:** Low. **Confidence:** High on the pattern (READ), Med on overflow reachability (INFERRED).

### LAUNCHSTATION-016 / Unchecked genRead/genWrite returns on file and font data / Category 6
- **Location(s):** Launchstation/src/cloud_google.c:1464 (`genWrite(fd, line, len);` result discarded — a failed/short write to the upload ledger is silently treated as done); Launchstation/src/draw_text.c:86,94 (`genRead` of a font header and font bitmap unchecked before use); Launchstation/src/editor_file.c:230-232,296-298 (`genWrite` of the editor buffer unchecked on save)
- **Problem:** Short writes on a slow/failing memory card or full device are reported to the user as success in at least the editor-save and cloud-ledger paths; the font path consumes whatever partial data arrived.
- **Fix:** Check the three cited returns and fail the operation (the pattern already used correctly at Launchstation/src/config.c:430, `written == len`). No new machinery.
- **Benefit:** Prevents silent data loss presented as a successful save (bug fix, no byte saving claimed).
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-017 / Self-update holds a whole second ELF in RAM on a 32 MB machine / Category 4/5
- **Location(s):** Launchstation/src/self_update.c:14-20 (`UPDATE_MAX_ELF (16 * 1024 * 1024)`, `UPDATE_RAM_RESERVE (4 * 1024 * 1024)`), Launchstation/src/self_update.c:377 (package extracted from the RAM buffer when present), Launchstation/src/update_psu.c:1-342 (PSU extraction, incl. a second in-memory reader)
- **Problem:** The updater downloads up to 16 MB into EE RAM (with a 4 MB reserve heuristic) beside a running dashboard, GS textures and TLS — the most memory-pressured moment in the app's life, and the cause class of "update failed/OOM" fragility.
- **Fix:** Always stream the download to the target device (the code already has the card path — delete the RAM-sink path and `UPDATE_RAM_RESERVE`). Less code, one path to test.
- **Benefit:** Up to ~16 MB peak RAM freed during update (by design figures in the file); one of two update data paths deleted (INFERRED ~50-100 lines).
- **Effort:** M. **Risk:** Med (updates get slower on card; they get *more* reliable). **Confidence:** High (READ).

### LAUNCHSTATION-018 / Thirteen #if 0 blocks keep dead code in IOP modules / Category 1
- **Location(s):** Launchstation/iop/vmcman/src/main.c:2913,3360,3372; Launchstation/iop/vmcman/src/ps1mc_fio.c:175,189,227,525; Launchstation/iop/ps2hdd_osd/cache.c:56 (an `#if 0`/`#else` where the live branch is the `#else`, Launchstation/iop/ps2hdd_osd/cache.c:56-70); Launchstation/iop/ps2host/ps2host.c:14,27; Launchstation/iop/ps2host/irx_imports.h:16; Launchstation/iop/usbd-ra/src/usbd.c:200
- **Problem:** Dead code inside the most size-constrained (IOP, 2 MB) and hardest-to-debug modules, including a block whose own comment says the condition "is always false".
- **Fix:** Delete all 13 blocks (in `cache.c`, delete the `#if 0` half and keep the `#else` body). Git history is the backup.
- **Benefit:** INFERRED 100-300 dead lines removed; zero runtime change.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-019 / Stale precompiled udpfs_smap.irx is superseded by the source-built target / Category 1/7
- **Location(s):** Launchstation/iop/__precompiled/udpfs_smap.irx (7.9 KB tracked binary) vs Launchstation/ps2.yaml:28-35 (source-built `udpfs_smap` target) and Launchstation/ps2.yaml:644 (the EE embeds the built one); Launchstation/src/embed_blobs.S:74-75 embeds only the ministack/ioman prebuilts, **not** this file
- **Problem:** A stale binary copy of a module that is now built from source sits in the tree inviting exactly the "which one ships?" confusion the freshness rule exists to prevent.
- **Fix:** Delete `iop/__precompiled/udpfs_smap.irx`. (The ministack/ioman prebuilts beside it are genuinely used — keep those.)
- **Benefit:** 7.9 KB and one ambiguity removed.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-020 / Unused exploit payloads: IOPRP.IMG and XSYSTEM2.XLF / Category 1
- **Location(s):** Launchstation/exploits/IOPRP.IMG (16 KB — only `IOPRP_LTS.IMG` is embedded, Launchstation/src/embed_blobs.S:70; no other reference found by grep); Launchstation/exploits/XSYSTEM2.XLF (14 KB — no reference anywhere in src/ps2.yaml/scripts by grep; only SYSTEM.XLF and XSYSTEM.XLF are sliced at Launchstation/src/embed_blobs.S:54-56)
- **Problem:** ~30 KB of tracked exploit binaries that no build input touches.
- **Fix:** Delete both files.
- **Benefit:** ~30 KB repo size (measured file sizes).
- **Effort:** S. **Risk:** Low. **Confidence:** Med (INFERRED from exhaustive grep absence, not from a build).

### LAUNCHSTATION-021 / Byte-identical duplicate files across the tree / Category 2/7
- **Location(s):** (md5-verified pairs/groups) Launchstation/AGENTS.md ≡ Launchstation/CLAUDE.md; Launchstation/res/memory-card/copy.icn ≡ del.icn ≡ list.icn (37 KB each — the code already exploits this, Launchstation/src/config.c:372-373,401-430, but all three files remain in the repo and in release packages); Launchstation/iop/mmceman/src/ioplib.c ≡ Launchstation/iop/mx4sio_bd/src/ioplib.c and Launchstation/iop/mmceman/include/ioplib.h ≡ Launchstation/iop/mx4sio_bd/src/ioplib.h; Launchstation/iop/ds34bt/iop/smsutils.S ≡ Launchstation/iop/ds34usb/iop/smsutils.S; Launchstation/iop/nfsman/irx_imports.h ≡ Launchstation/iop/smb2man/irx_imports.h; Launchstation/docs/theme-example/background.png ≡ Launchstation/docs-site/static/assets/wallpaper-example.png
- **Problem:** Each duplicate is a place where a fix lands in one copy and silently not the other (the two `ioplib.c` copies are the risky ones — same driver-support code in two IOP modules).
- **Fix:** For `ioplib.*`/`smsutils.S`: point both `ps2.yaml` targets at one shared source path and delete the second copy (net removal, no new abstraction — it is the same file referenced twice). For AGENTS/CLAUDE: keep one, make the other a one-line pointer or delete it. For the `.icn` trio: keep the files (the MC format needs three filenames on the card) but note this is packaging-only — no action beyond AGENTS/CLAUDE and ioplib.
- **Benefit:** INFERRED ~200-400 duplicated lines removed (ioplib + smsutils); drift risk removed.
- **Effort:** S/M. **Risk:** Low. **Confidence:** High (READ, hash-verified).

### LAUNCHSTATION-022 / res/icons/*.png (16 files) are not used by the icon system / Category 1
- **Location(s):** Launchstation/res/icons/ (16 PNGs + 1 SVG, ~30 KB total); the actual icon renderer is a procedural SDF atlas — Launchstation/src/dash_icons.c:4-5,732-914 — and the only reference to `res/icons/` in code is a comment crediting `rocket-launch.svg` at Launchstation/src/dash_icons.c:170
- **Problem:** A folder of pad-glyph PNGs that nothing embeds, packages or loads (grep over src/scripts/ps2.yaml found no reference).
- **Fix:** Delete the PNGs; keep the SVG if the credit comment needs its source.
- **Benefit:** ~30 KB repo size.
- **Effort:** S. **Risk:** Low. **Confidence:** Med (INFERRED from grep absence).

### LAUNCHSTATION-023 / Branch and tag clutter: 104 remote branches, 140 tags (88 build-*) / Category 7
- **Location(s):** Remote refs at pin (LIST ONLY, per instructions): 104 branches — 66 `claude/project-thread-*` / `claude/ls-*`, 13 `fix/*`, 5 `codex/*`, plus `test/*`, `audit/*`, `download-metrics`, `gh-pages`; 140 tags — 88 sequential `build-1013`…`build-1174`, ~40 `preview-*`, plus dated one-offs (`usb-stability-2026-10-08-*`, `app-packages-2026-10-08-*`, etc.) and `latest`, `beta-1`, `language-files`
- **Problem:** Sequential CI build numbers and per-thread agent branches are not releases; they bury the tags that are (`latest`, real versions) and make every fetch/clone slower.
- **Fix:** (Listing only — no action taken.) Delete the `build-*` series and merged `claude/project-thread-*` branches; keep version tags and `latest`.
- **Benefit:** Cleaner refs; smaller clone metadata (INFERRED).
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — counts verified at pin: 104 / 140 / 88).

### LAUNCHSTATION-024 / Two settings systems run in parallel: classic CNF (config.c) and the dashboard store (dash_store.c) / Category 2/3
- **Location(s):** Launchstation/src/config.c:1-1782 (CNF parse/save, `SETTING` struct at Launchstation/include/launchelf.h:300-340) and Launchstation/src/dash_store.c (own settings save, `dashStoreSaveSettings` at Launchstation/src/dash_store.c:683), plus Launchstation/src/dash_settings.c:1-4096 (4,096 lines of settings UI) and the split classic editors Launchstation/src/config_screen.c, config_startup.c, config_network.c, config_advanced.c; the project's own test names admit the duality (Launchstation/tests/test_classic_settings_persistence.py)
- **Problem:** Two persistence formats, two save paths writing overlapping user preferences (network, paths, language), and the largest EE file in the repo is settings UI. Every new setting must be wired through both worlds or silently diverge.
- **Fix:** Finish the migration: move the remaining classic-only keys into the dashboard store and delete the classic config editors + CNF writer, keeping a one-time CNF *reader* for import. This is the biggest single simplification available after the cloud removal, and it is a deletion programme, not a new layer.
- **Benefit:** INFERRED several thousand lines across config*.c and dash_settings.c consolidation; one settings format to test.
- **Effort:** L. **Risk:** High (settings loss on upgrade is the failure mode — needs the import path tested first). **Confidence:** Med (READ for the duality, INFERRED for the migration scope).

### LAUNCHSTATION-025 / Development-only code paths compiled into production sources / Category 1
- **Location(s):** Launchstation/include/dash_autotest.h:1-6 + Launchstation/src/pad.c:284-313 + Launchstation/src/dash_main.c:1295-1296 (timed fake-button script, `DASH_AUTOTEST`); Launchstation/src/dash_main.c:1483ff (`#ifdef DASH_THEME_TOUR` — "development only: show every preset for five seconds"); Launchstation/ps2.yaml:642 (the EE link includes the SDK `debug` library and `scr_printf` support, Launchstation/src/main.c:19)
- **Problem:** Dev harnesses live inside the shipping sources behind defines. They cost nothing at runtime today and everything the day a release build defines one by accident (the autotest script *injects controller input*).
- **Fix:** Delete the theme-tour block outright; move the autotest script out of `pad.c` into the tests tree or delete it. Leave the `debug` lib decision to a release checklist — listed here, not counted.
- **Benefit:** ~40 lines and one footgun removed (estimate).
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-026 / Three external-launcher integrations (PopStarter, Ember, Neutrino) each carry their own game-ID / path / launch stack / Category 2/8
- **Location(s):** Launchstation/src/popstarter.c + popstarter_path.c + popstarter_vcd.c + popstarter_gameid.c + popstarter_elf.c (5 files); Launchstation/src/ember.c + ember_gameid.c; Launchstation/src/gui_neutrino.c + neutrino_iso.c + neutrino_smb.c + neutrino_gameid.c; Launchstation/src/main_gameid.c + main_gameid_table.c (PVD-timestamp→game-ID table); three separate opt-out flags in the settings struct (Launchstation/include/launchelf.h:306-308: `disable_ember`, `disable_popstarter`, `disable_neutrino`)
- **Problem:** Launching other people's ELFs with per-game identification is three parallel mini-frameworks inside a file manager. Each new launcher multiplies settings flags, game-ID logic and launch plumbing.
- **Fix:** No code change proposed today; the consolidation candidate is game-ID: the three `*_gameid` implementations plus `main_gameid*` should collapse to the one disc-ID routine they share, *only if* a side-by-side read shows the logic is actually the same (not verified — do not do this on inference). The honest S-effort action is a scope freeze: no fourth launcher integration without removing one.
- **Benefit:** Unquantified (INFERRED).
- **Effort:** L. **Risk:** Med. **Confidence:** Low/Med (INFERRED — file-level read only, implementations not diffed line by line).

### LAUNCHSTATION-027 / Six workflows, including a download-counter running 8× a day / Category 7
- **Location(s):** Launchstation/.github/workflows/download-metrics.yml:1-32 (cron `17 */3 * * *` — every 3 hours, forever, to count downloads); Launchstation/.github/workflows/neutrino-watch.yml (cron 3×/month), Launchstation/.github/workflows/ps2build-sdk-watch.yml:1-245 (weekly, 245 lines), Launchstation/.github/workflows/sync-wiki.yml, Launchstation/.github/workflows/docs-pages.yml, Launchstation/.github/workflows/compile.yml
- **Problem:** Three separate "watch" automations plus a 3-hourly metrics commit loop is a lot of standing CI for a homebrew project; each is also a supply-chain/maintenance surface (the SDK watch auto-pins toolchain versions).
- **Fix:** Drop download-metrics to daily (or delete it — release download counts are visible on the releases page); fold the two watch workflows' schedules into one weekly job. Keep compile + docs-pages as-is.
- **Benefit:** ~75% fewer scheduled runs (estimate); 245-line watch workflow halved.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-028 / Stale planning/audit documents are kept as if current / Category 7
- **Location(s):** Launchstation/USB-STABILITY-AUDIT-2026-10-08.md (self-described historical, root cause "not proven"); Launchstation/storage_driver_fixes_plan.md (self-described "index only; PR #176 merged"); Launchstation/docs/plans/ (10 dated Oct-2026 plans), Launchstation/docs/superpowers/plans/ + specs/ (Sep-Oct 2026 agent plans, the source of the stale `res/sfx/boot.adp` reference in LAUNCHSTATION-013), Launchstation/docs/TODO.txt (self-described 2014-era, "not a roadmap"), Launchstation/research/sd2psxtd/ (a host-side *model* of the MMCE restore logic duplicating `src/mmce_backup.c`'s domain)
- **Problem:** ~55 files under `docs/` plus root-level audits mix current documentation with completed agent work-plans. New contributors (and agents) read plans as facts.
- **Fix:** Delete the completed plans/superpowers/audit files (git history preserves them) or move them to one `docs/archive/` folder in a single commit. Keep `docs/wiki/` and the user-facing docs.
- **Benefit:** INFERRED 20-30 files removed from the live tree; no code effect.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ — several files self-label as historical).

### LAUNCHSTATION-029 / __Headers APA samples and ship/ shortcut .url files are release-kit clutter in the source repo / Category 7
- **Location(s):** Launchstation/__Headers/ (168 KB, 13 files: four near-identical `PP.*` header sets of icon.sys/list.ico/system.cnf, self-described "APA INJECTON SAMPLES" in Launchstation/__Headers/README.MD); Launchstation/ship/shortcuts/ (5 Windows `.url` shortcuts to other projects); Launchstation/ship/kit-externals.json (pins ~1.6 MB of externals incl. another repo's `ember.elf` by hash — the binaries themselves are fetched, not stored)
- **Problem:** Packaging samples and desktop shortcuts live in the source tree and drift from the packages they describe (see LAUNCHSTATION-021: three of the four header sets differ only in partition name).
- **Fix:** Generate the `PP.*` headers from one template at package time (`scripts/package_apps.py` already builds packages) and delete three of the four sets plus the `.url` files.
- **Benefit:** ~120 KB and 15+ files (estimate).
- **Effort:** S. **Risk:** Low. **Confidence:** Med (READ; packaging scripts not fully traced — INFERRED that generation is trivial).

### LAUNCHSTATION-030 / Dashboard font system allocates a 512 KB scratch texture arena / Category 4
- **Location(s):** Launchstation/src/dash_font.c:80 (`memalign(128, 512 * 1024)`), plus per-face textures at Launchstation/src/dash_font.c:254 and the icon atlas at Launchstation/src/dash_icons.c:734
- **Problem:** Half a megabyte of EE RAM is reserved for font texture scratch in an app whose headline constraint is 32 MB shared with game launching, TLS (LAUNCHSTATION-004) and the updater's RAM sink (LAUNCHSTATION-017).
- **Fix:** Size the arena from the largest face actually loaded (the biggest single face atlas is 122,880 B, LAUNCHSTATION-002) instead of a round 512 KB, or allocate it lazily and free it when the dashboard hands off to a launched ELF. Measure first — this is a sizing change, not new code.
- **Benefit:** INFERRED up to ~384 KB EE RAM.
- **Effort:** S. **Risk:** Med (undersizing breaks text rendering — needs on-hardware check). **Confidence:** Med (READ for the allocation, INFERRED for safe size).

### LAUNCHSTATION-031 / QR generator (1,027 vendored lines) exists for one cloud-login screen / Category 8
- **Location(s):** Launchstation/src/qrcodegen.c:1-1027, used only at Launchstation/src/dash_tools.c:179-239 (`CLOUD_QR_MAX_VERSION 6`, two version-6 buffers on the stack)
- **Problem:** A full QR encoder ships in the ELF to display one OAuth URL. It is a dependent of LAUNCHSTATION-004 and worthless without it.
- **Fix:** Delete with the cloud feature (the same screen already prints the URL and user code as text, `src/dash_tools.c:211ff`). If cloud stays, this stays — do not act on this finding alone.
- **Benefit:** 1,027 lines and its tables (INFERRED several KB) — counted here only to avoid double-counting with LAUNCHSTATION-004.
- **Effort:** S (with -004). **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-032 / scripts/ accumulates one-off audit/migration tools beside the build scripts / Category 7
- **Location(s):** Launchstation/scripts/ (24 files), incl. Launchstation/scripts/stale_code_audit.sh, Launchstation/scripts/audit_r3z_settings.py, Launchstation/scripts/check_ds34_build.py (only used by the DS34 CI path, LAUNCHSTATION-010), Launchstation/scripts/copy_test_pack.py (builds a CI fixture), alongside load-bearing ones (ps2build_sdk.sh, gen_dash_font.py, package_apps.py, build_cloud_tls.sh, build_jpeg.sh, fetch_libsmb2.sh)
- **Problem:** One person's completed audit script becomes every future reader's "is this part of the build?" question.
- **Fix:** Delete the one-off audit scripts; keep the build/package generators that `ps2.yaml`/CI actually invoke (verifiable by grepping the workflows).
- **Benefit:** INFERRED 3-6 files; clarity, not bytes.
- **Effort:** S. **Risk:** Low. **Confidence:** Med (READ list; per-script usage INFERRED from workflow grep).

---

## Quick wins (Low risk + S effort, from the list above)

LAUNCHSTATION-008 (GIF duplicates), -013 (dead audio), -016 (three unchecked returns), -018 (#if 0 blocks), -019 (stale IRX), -020 (unused payloads), -023 (ref cleanup — listed only), -027 (metrics cron), -028 (stale plans).

## Files not read, and why

- **Most IOP module internals** (`iop/mmceman/src/*.c`, `iop/mx4sio_bd/src/*.c`, `iop/usbmass_bd/`, `iop/ps2hdd_osd/` beyond the cited blocks, `iop/vmcman/` beyond the `#if 0` sites, `iop/smb2man/usmb2/`, `iop/nfsman/` incl. vendored unfs4, `iop/pccf/`): inventoried and grep-swept, not line-read. They are vendored/forked driver code where a line audit needs the upstream diff, which is a separate pass.
- **Vendored/fetched code not in the tree:** libsmb2 (`.smbd-deps/`, fetched by `scripts/fetch_libsmb2.sh`), Mbed TLS and libjpeg-turbo (`.cloud-deps/`/`.jpeg-deps/`, built by scripts) — sized by their role in the link, not read.
- **EE sources read at file level / sampled, not exhaustively line-by-line:** `src/dash_browser.c`, `src/dash_card_tool.c`, `src/filer*.c` (5 files, ~11,000 lines), `src/dash_hdd.c`, `src/hdd_header_injector.c`, `src/mmce_backup.c`, `src/mc_*.c` (6 files), `src/dash_editor.c`/`editor*.c`, `src/icon.c` (2,055 lines), `src/elf.c`, `src/gui*.c`. Findings citing them are from grep-verified locations; a line-by-line pass over the filer/MC cluster is the most valuable follow-up.
- **Tests:** sampled (structure, the source-scraping pattern, two executed) — the other ~210 Python tests were counted and pattern-classified, not individually read.
- **docs-site HTML content and docs/wiki prose:** inventoried by size only; factual staleness of prose not audited.
- **Binary assets** (IRX/ELF/PNG/ADP/OGG): sized and hash-compared, not disassembled.
- **No build was run:** the ps2build SDK is not installed in this sandbox, so no ELF size was measured directly; all ELF-byte benefits are computed from source array sizes / file sizes and labelled as estimates. No real-hardware behaviour is claimed anywhere in this report.

## Unfinished categories

- Category 6 (bugs/security) is **partial**: the unsafe-string and unchecked-return sweeps are grep-complete for EE `src/`, but IOP modules and the FTP/SMB network parsers were not line-audited for overflows — for network-facing IOP code that is a real gap, not a clean bill.
- Category 5 (performance) is **partial** beyond the cited busy-waits: redraw frequency, per-frame allocations and device-scan costs in `dash_browser.c`/`dash_devices.c` were sampled, not profiled (no hardware, no build).
- Category 2 (cross-repo duplication vs RiptOPL — e.g. the usmb2/smb2auth code `ps2.yaml:249` says is taken from RiptOPL) is **out of scope for this per-repo pass** and belongs in the cross-repo summary.
