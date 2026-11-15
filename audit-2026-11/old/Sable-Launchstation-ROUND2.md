# Sable Audit — Launchstation, Round 2

- **Auditor:** Sable
- **Repo:** NathanNeurotic/Launchstation
- **Round-2 pin:** `master` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790`, checked live 2026-11-14 09:05–09:10 PST. See `PINS-ROUND2.md`. Clone at `~/workspace/audit-clones-2026-11-13/Launchstation`, updated in place; `git status` clean at the end of this pass.
- **Delta from pass 1** (`8cd3329a98c0` → `9fb0598a79f0`, 3 commits): `1882fe9` "Check for Updates: write the verified ELF straight over the running one" (#384, 12 files, +226/−325), `d87b48f` (#385) and `9fb0598` (#386) — docs-site palette only.
- **Method:** READ ONLY. Line-level reading of the new self-update code, the IOP network parsers pass 1 skipped (ps2ftpd, smbman, smb2man/usmb2, ps2host, nfsman/unfs4), and the filer/MC/browser EE cluster pass 1 only grep-swept. Every line number below is at the round-2 pin and was grep-verified this session unless marked INFERRED.
- **Tests run for evidence:** `python3 tests/test_self_update.py` — PASS. `python3 tests/test_app_packages.py` — PASS. No PS2 build was run (no PS2SDK toolchain in this VM); all size figures for compiled code are measured source arithmetic, labelled as such.
- **Rule applied:** LESS, NOT MORE — same as pass 1.

---

## A) Revalidation of pass-1 findings touched by the delta

Only 12 files changed in the delta: `src/self_update.c`, `src/update_psu.c`, `include/self_update.h`, `README.md`, five docs-site HTML/CSS files, `docs/wiki/Releases-and-Recovery.md`, and three test files. Every pass-1 finding whose files are outside that list stands at the round-2 head on that basis (delta verified with `git log --stat`); the ones inside it are revalidated individually below.

| Finding | Verdict | Evidence at round-2 head |
|---|---|---|
| **LAUNCHSTATION-017 — self-update holds a whole second ELF in RAM** | **STILL TRUE (changed form)** | See detail below the table. |
| LAUNCHSTATION-009 — Python "tests" mostly scrape C source as text | STILL TRUE | #384 reworked `tests/test_self_update.py`, `tests/test_app_packages.py`, `tests/test_update_sidecar_transaction.py`, but the pattern is unchanged: `tests/test_self_update.py:5-13` still loads sources with `read_text()` and asserts string presence/absence (`"cloudHttpDownloadFd" in http_h` at :13; the "gone" string list at :29). The rework changed *what* is grepped, not *that* it greps. Both reworked host tests pass (see Method). |
| LAUNCHSTATION-004 — native cloud subsystem built into every ELF | STANDS | The updater stopped using `cloudHttpDownloadFd`, but the function and the subsystem remain, with live callers inside the cloud code itself: definition `src/cloud_http.c:1280`, memory variant at :1298, bearer-token variant at :1309. The finding was never about the updater. |
| LAUNCHSTATION-008 — docs-site carries 17.4 MB of media | STANDS | #385/#386 touched only `docs-site/static/assets/style.css` and `docs-site/build.py` (palette). No media file changed. |
| LAUNCHSTATION-028 — stale planning/audit documents kept as if current | STANDS | #384 correctly refreshed `docs/wiki/Releases-and-Recovery.md` for the new overwrite-in-place flow. The stale plans/audits named in -028 were not touched. |
| LAUNCHSTATION-030 — 512 KB font scratch arena | STANDS | Its problem statement cites -017's RAM sink as part of the same memory picture; the sink persists (below), so the combined picture is unchanged. |
| All other pass-1 findings (001–003, 005–007, 010–016, 018–027, 029, 031, 032) | STAND | Their files are untouched by the three delta commits (verified via the delta file list above). Not re-read line by line this round; the basis is the delta, stated plainly. |

### LAUNCHSTATION-017 in detail — what #384 actually did

Read in full at the new head: `src/self_update.c` (434 lines, was 543), `src/update_psu.c` (364 lines), `include/self_update.h`.

**What improved (credit where due):** the staging machinery is gone — no `.UPDATE`/`.PACKAGE` files, no rename dance, no new `.BAK` (a legacy `.BAK` is now only *deleted* if one is lying around, `src/self_update.c:280-282`). `self_update.c` shrank by ~109 lines net. Sidecars (WLE-UPDATE-CA.PEM, BGM.ADP) are still installed only when missing (`src/self_update.c:327,333` region), and free space is checked before the running ELF is touched (:340-348).

**Why the verdict is still STILL TRUE:** pass 1's suggested fix was "always stream the download to the target device; delete the RAM-sink path." #384 did the opposite — it deleted the *device* path and kept the RAM sink as the **only** path:

- The card/download-to-file fallback (`download_file`, staging through `cloudHttpDownloadFd`) was deleted. The code now says so itself, `src/self_update.c:174-175`: "The package is downloaded into RAM only… otherwise NULL and the update is refused before anything is touched."
- `update_ram_buffer` (`src/self_update.c:176-195`) still mallocs the whole package — up to `UPDATE_MAX_ELF` 16 MB (`src/self_update.c:15`) — plus the same 4 MB reserve probe (`UPDATE_RAM_RESERVE`, `src/self_update.c:19`; probe malloc'd and freed at :186-193). The "package" is the whole PSU (ELF + sidecars + padding), held in RAM beside the running dashboard for the entire install. It is called at `src/self_update.c:410`.
- So the peak-RAM problem pass 1 flagged — up to ~16 MB beside a running dashboard on a 32 MB machine, at the most memory-pressured moment in the app's life — is unchanged in size and is now unconditional: there is no longer any code path that avoids it.

**And the change traded that standing problem for a new one** — the loss of the old design's failure safety — filed as LAUNCHSTATION-034 below.

---

## B) New findings (LAUNCHSTATION-033+, sorted by benefit)

### LAUNCHSTATION-033 / The FTP server is anonymous with full read/write/delete, by launch configuration / Category 6 (Bugs and security)
- **Location(s):** `src/init.c:1176` (`arg_p = "-anonymous"`), `src/init.c:1182-1183` (adds `-no-hdd` only when the console policy hides HDD), module loaded at `src/init.c:1190`; `iop/oldlibs/ps2ftpd/src/FtpCommands.c:55-63` (`FtpClient_OnCmdUser` grants `AUTHSTATE_VALID` for user "anonymous" when `m_iAnonymous` is set).
- **Problem:** The EE side loads ps2ftpd with `-anonymous` unconditionally. Any host on the LAN that can reach the PS2 gets a fully authenticated FTP session with write, delete and rename over every device the FileSystem layer exposes (mc, usb/mass, mmce, pfs, etc. — only the HDD family is excluded, and only when the HDD itself is policy-hidden). There is no EE setting that requires a password instead. On a home LAN this is "any compromised or curious device can wipe or plant files on every attached PS2 storage device."
- **Fix:** Stop passing `-anonymous` by default; require an explicit user opt-in setting for anonymous FTP, defaulting to the credential path (once -040 makes that path real). This is a configuration deletion — the insecure default goes away — not a new feature.
- **Benefit:** Removes unauthenticated write access to all attached storage from the default configuration.
- **Effort:** S. **Risk:** Low (users who rely on anonymous FTP re-enable it once, deliberately). **Confidence:** High (READ).

### LAUNCHSTATION-034 / Self-update now overwrites the running ELF in place: no staging, no backup, no readback — one interrupted write bricks the install / Category 6
- **Location(s):** `src/self_update.c:220-247` (`overwrite_elf`: opens the running ELF's own path with `FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC` at :224, writes in 16 KB chunks, no readback), called at `src/self_update.c:351`; the failure message at `src/self_update.c:354` admits the outcome: "%s may be damaged: copy the Launchstation ELF to this device again."
- **Problem:** #384 (see -017 revalidation) removed the staged write entirely. From the moment of `O_TRUNC`, the only copy of Launchstation on that device is incomplete until the last chunk lands. Power loss, card pull, or a media error mid-write leaves a truncated ELF and an unbootable install — on exactly the removable, failure-prone media (MC, USB, MMCE) this app targets. Two mitigations exist and both are partial: the pre-write free-space check (:340-348) covers ENOSPC only, and the SHA-256 verification covers the *download in RAM* — the bytes actually written to the device are never read back, even though the success message at :356-360 says "Update installed and verified."
- **Fix:** Restore the shape #384 removed, minus the retention habit: write the verified ELF to a temporary name **on the same device**, read it back and compare the hash, then rename it over the running ELF (rename within one device is the atomic step the filer already relies on elsewhere), and delete the temp on any failure. No `.BAK` is kept afterwards.
- **Justification (ADD):** This re-adds a staging path, against LESS-NOT-MORE's grain — justified because the concrete problem is total loss of the only copy on an interrupted write, and the pre-#384 code proves the staging shape is the simplest form that avoids it. Net add is one temp file path and a rename; the `.BAK`-keeping and `.PACKAGE` machinery stay deleted.
- **Benefit:** An interrupted update leaves the old, working ELF in place instead of a truncated one; the "verified" claim becomes true of the written file.
- **Effort:** M. **Risk:** Low (restores a previously shipped behaviour shape). **Confidence:** High (READ).

### LAUNCHSTATION-035 / ps2host's read reply trusts the host-supplied byte count into the caller's buffer / Category 6
- **Location(s):** `iop/ps2host/net_fio.c:406` (`nbytes = ntohl(readrly->nbytes)`), `iop/ps2host/net_fio.c:413` (`pko_recv_bytes(pko_fileio_sock, &buf[0], nbytes)`), return at :418; related: `iop/ps2host/net_fio.c:115-125` (`pko_accept_pkt` logs a short header via `dbgprintf` — compiled out in release — and keeps parsing the packet as if it were whole).
- **Problem:** `pko_read_file` asks for `length` bytes, then receives however many the host *says* are coming — `nbytes` is never clamped to `length` or to any buffer size — into the IOP caller's buffer, and returns `nbytes` as the count read. A buggy or malicious ps2link host (or anything impersonating one on the LAN) overflows an IOP buffer with a single reply. IOP has no memory protection; this is arbitrary IOP memory corruption from the wire. The short-header path in `pko_accept_pkt` is the same shape in miniature: a framing error is noted only in a debug print and parsing continues on a partial header.
- **Fix:** Clamp `nbytes` to the requested `length` before receiving (refuse or drain-and-fail on a larger claim — refusing is less code), and make `pko_accept_pkt` return failure on a short header instead of continuing. Deletions of trust, not additions.
- **Benefit:** Removes a wire-reachable IOP buffer overflow in the host: device path.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-036 / Every folder listing allocates 2,048 FILEINFO entries (~768 KB) regardless of folder size — ten allocation sites, and the dashboard browser holds two at once / Category 4 (Size and memory)
- **Location(s):** `src/dash_browser.c:559` (per-listing job buffer), `src/dash_browser.c:3376` (the browser's resident `fb.files`), copy loop `src/dash_browser.c:359-368` (`listDone` struct-copies the job buffer into the resident one, then frees the job's); also `src/dash_lib.c:890`, `src/dash_themes.c:990`, `src/dash_widgets.c:1005`, `src/filer_actions.c:311`, `src/filer_actions.c:404`, `src/filer_browser.c:2010`, `src/filer_copy.c:915`, `src/self_update.c:106`. Constants: `include/launchelf.h:201-206` (`MAX_ENTRY 2048`, `MAX_NAME 256`), `FILEINFO` at `include/launchelf.h:560-565`.
- **Problem:** A `FILEINFO` is `name[256]` + `title[66]` + an `sceMcTblGetDir` — 384 bytes by field arithmetic (INFERRED: the SDK struct is not in this tree; 256 + 66 + 60, padded). Every listing allocation is therefore ~786,432 bytes, sized for the maximum folder the code tolerates, even for a folder with six files. The dashboard browser allocates a fresh one per folder opened while its resident one stays allocated, then copies up to ~768 KB struct-by-struct on completion instead of swapping pointers. On a 32 MB machine that also hosts the -017 RAM sink, this is standing waste in the app's most-used screen. The tree already contains the better pattern: the clipboard grows by `realloc` to the size actually needed (`clipReserve`, `src/filer.c:136-147`).
- **Fix:** Two deletions of waste, no new machinery: (1) in the dashboard browser, swap the job's buffer pointer into `fb.files` (freeing the old one) instead of copying entries; (2) grow listing buffers like `clipReserve` does — start small, `realloc` on demand — at the shared `getDir` boundary so all ten sites benefit from one change.
- **Benefit:** ~768 KB peak saved on every dashboard folder open, up to ~700 KB per listing allocation elsewhere (INFERRED from the same arithmetic); one up-to-768 KB struct copy removed per folder open.
- **Effort:** M. **Risk:** Med (listing code is central; the pointer swap alone is S/Low and can land first). **Confidence:** High for the allocation sites and the copy (READ); the 384-byte entry size is INFERRED arithmetic.

### LAUNCHSTATION-037 / FTP PORT command is an unvalidated FTP-bounce primitive / Category 6
- **Location(s):** `iop/oldlibs/ps2ftpd/src/FtpCommands.c:179-195` (`FtpClient_OnCmdPort`; the only check is `port >= 1024` at :183, beside the literal comment `// TODO: validate IP?` at :184); address components parsed with unbounded `strtol` at `iop/oldlibs/ps2ftpd/src/FtpClient.c:280-284` (no 0–255 clamp per octet).
- **Problem:** An FTP client can tell the PS2 to open its data connection to **any** IP address and any port ≥ 1024 — the classic FTP bounce: the PS2 becomes a TCP connect/proxy probe against other LAN hosts, driven by whoever holds an FTP session (which, per -033, is anyone). The server already knows the real peer address — it is stored per client (`iop/oldlibs/ps2ftpd/src/FtpServer.c:334`, field declared `iop/oldlibs/ps2ftpd/src/FtpClient.h:106`) — but uses it only for duplicate-client counting (`iop/oldlibs/ps2ftpd/src/FtpControl.c:63`), never to validate PORT.
- **Fix:** Refuse a PORT address that differs from the control connection's peer address (one comparison against an already-stored field), and clamp the parsed octets to 0–255 while parsing. Both are checks on existing data, not new state.
- **Benefit:** The PS2 stops being a LAN scan/connect proxy; PORT keeps working for every legitimate client (they all name their own address).
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-038 / FTP PASV data connection accepts any host that connects first / Category 6
- **Location(s):** `iop/oldlibs/ps2ftpd/src/FtpServer.c:291` (`accept()` on the passive data socket, result used without comparing the accepted peer against the control client's stored address, `FtpServer.c:334` / `FtpClient.h:106`).
- **Problem:** In passive mode the server listens and takes the first inbound data connection from anywhere. A third party that guesses or observes the PASV port can win the race against the real client and receive the file the client asked for, or supply the file the client is uploading. Combined with -033, the session itself needs no credentials, so the race is open to the whole LAN.
- **Fix:** After `accept()`, compare the accepted socket's peer address with the control client's stored `m_uiPeerAddress` and drop the connection on mismatch. One comparison; no new state.
- **Benefit:** Passive transfers go to and from the client that asked for them.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-039 / Card tool keeps ten whole-image safety copies per card image, and re-scans the folder for every prune / Category 3/5 (Complexity / Performance)
- **Location(s):** `src/dash_card_tool.c:32` (`CT_SAFETY_KEEP 10`), `src/dash_card_tool.c:266-336` (`ctSafetyCopyOf`: verified whole-image copy into a sibling `WLE-Safety/` folder, then a prune loop that re-opens and re-reads the whole folder directory for **each** deletion, up to a 64-iteration guard), invoked once per batch via `ctSafetyOnce` (`src/dash_card_tool.c:379`, call sites :402, :434); the PS1 tool uses the buffer variant at `src/dash_ps1_card.c:151`.
- **Problem:** This is a deliberate, verified safety feature for destructive card-image writes — not a stealth backup, and it should not be deleted. But its sizing is the `.BAK` habit at industrial scale: before the first write of a batch, the **entire** card image is copied (and read back) — 8 MB for a standard PS2 card image, far more for large MMCE images — and up to **ten** generations are kept per image, i.e. ≥80 MB of host storage per 8 MB image, on the same removable media whose free space the tool elsewhere checks cluster by cluster. The prune also re-enumerates the safety folder from scratch per deleted generation.
- **Fix:** Keep the feature, cut its constants: keep the newest 2 copies instead of 10, and collect the prune list in the single enumeration `ctSafetyCopyOf` already performs before writing, instead of re-scanning per deletion. Both changes are deletions (a smaller constant, a loop that goes away).
- **Benefit:** Up to ~64 MB of storage returned per 8 MB card image (more for larger images); one full image read+write remains per batch (the safety property), but pruning stops costing a directory scan per generation.
- **Effort:** S. **Risk:** Low (fewer historical generations retained — the latest pre-write state, which is the one recovery actually uses, is always kept). **Confidence:** High (READ).

### LAUNCHSTATION-040 / The FTP credential path is dead code that looks like security: `-pass` can never match, `-port` is overwritten, defaults are hardcoded, and the EE never passes any of them / Category 1/6
- **Location(s):** `iop/oldlibs/ps2ftpd/src/main.c:51` (the `-pass` branch tests `!strcmp("-pass", argv[0])` — the program name — instead of `argv[i]`, so it can never fire); `iop/oldlibs/ps2ftpd/src/main.c:111` (`_start` calls `FtpServer_SetPort(&srv, 21)` **after** `process_args`, silently discarding any `-port` — the LINUX build's main at :170 does it in the working order); `iop/oldlibs/ps2ftpd/src/FtpServer.c:52-53` (compiled-in default username/password, both the same well-known string — value not reproduced here); and the EE loader passes neither option anyway (`src/init.c:1176`).
- **Problem:** Anyone reading the code — or setting the options — would believe ps2ftpd can be run with a chosen username, password and port. It cannot: the password option is unreachable, the port option is clobbered, and the only credentials that work are the hardcoded defaults, behind a login path the shipped configuration bypasses entirely via `-anonymous` (-033). This is worse than no credential code: it is credential code that fails silently in the unsafe direction.
- **Fix:** Make it real or delete it: fix the `argv[i]` comparison and the `SetPort` ordering (two one-word/one-line corrections, no new code), have the EE pass a user-configured user/password when FTP is enabled without anonymous, and drop the hardcoded default password in favour of refusing credential login until one is set. If credential FTP is not a feature the project wants, delete the `-user`/`-pass` handling and the defaults instead — either way the tree stops claiming a protection it does not provide.
- **Benefit:** The FTP auth story becomes true, whichever way it is decided; -033's fix depends on this path working.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-041 / The file-based PSU extractors are dead in the shipped app — kept alive only by the host test harness — and the updater still gates on a `.BAK` name it never creates / Category 1 (Dead code)
- **Location(s):** `src/update_psu.c:183` (`selfUpdateExtractPsu`), `src/update_psu.c:201` (`selfUpdateExtractPsuMem`), `src/update_psu.c:335` (`selfUpdateExtractPsuSidecar`), declarations `include/self_update.h:13-17`; the `PsuIn` file-descriptor reader branch inside `update_psu.c` that only these functions use. Sole callers anywhere: the harness main embedded in `tests/test_app_packages.py:69-73`. The shipped flow uses only `selfUpdateLocatePsuElf` (`src/update_psu.c:213`, called `src/self_update.c:306`) and `selfUpdateExtractPsuSidecarMem` (`src/update_psu.c:356`, called `src/self_update.c:327,333`). Related residue: `src/self_update.c:261` still builds the `<leaf>.BAK` path and **refuses the update** if that name doesn't fit the buffer — for a backup file #384 stopped creating; the name's only remaining use is deleting a legacy `.BAK` at :280-282.
- **Problem:** After #384, three public extraction functions and an entire file-reading backend of the PSU parser have no caller in the product. They survive because the host harness compiles `update_psu.c` directly and exercises them — the test suite is keeping dead product code alive, and the dead code is what a reader of `update_psu.c` mostly sees. (INFERRED: whether the EE link strips them depends on section GC in the EE link, for which no `--gc-sections` evidence was found in `ps2.yaml`'s EE target; the source- and maintenance-level cost is certain either way.)
- **Fix:** Delete the three functions, the `PsuIn` fd branch, and their header declarations; point the harness at the Mem entry points the product actually uses (it already tests one: `test_app_packages.py:73`). Drop the `.BAK` length gate at `self_update.c:261` while keeping the legacy-`.BAK` deletion (build that one path in a local buffer at the deletion site).
- **Benefit:** INFERRED ~150-200 lines of parser surface deleted; one PSU reader remains, the one the product runs; the test suite stops certifying code the console never executes.
- **Effort:** S. **Risk:** Low (harness change is mechanical; both host tests were verified passing pre-change this session). **Confidence:** High for "no product callers" (READ, grep-verified across `src/`, `include/`, `tests/`); INFERRED for the line count and link behaviour.

### LAUNCHSTATION-042 / nfsman and smb2man each carry their own copy of the same URL parser and connect skeleton / Category 2 (Duplication)
- **Location(s):** `iop/nfsman/nfsman.c:1205` (`nfsman_parse_url`) vs `iop/smb2man/smb2man.c:1449` (`smb2man_parse_url`) — near-identical ~70-line functions: same scheme check, same "an IP address only: the driver has no DNS" comment and logic, same bounded-copy pattern; plus parallel devctl-connect wrappers (call sites `nfsman.c:1286`, `smb2man.c:1524`) and parallel root-listing name copies (`nfsman.c:992`, `smb2man.c:1256`).
- **Problem:** Two modules that must agree on what a share/export URL means implement it twice, in the same words. Any fix to one's parsing (trailing slashes, port handling, length edges) silently doesn't reach the other. Both modules already use the include-a-.c pattern — `nfsman.c:145` includes `unfs4/unfs4.c`, `smb2man.c:199` includes `usmb2/usmb2.c` — so the project's own idiom for sharing source between IRXs exists in these very files.
- **Fix:** One shared parse source, included by both modules exactly as `unfs4.c`/`usmb2.c` already are, parameterised only by the struct it fills (or by returning the parsed fields). The duplicated connect/root-listing skeletons follow the same path if they survive the comparison.
- **Benefit:** INFERRED ~100+ lines removed across the two modules; one URL grammar to fix, test and read.
- **Effort:** M. **Risk:** Med (two live network drivers; behaviour must be kept field-for-field — the near-identity of the copies makes that checkable by diff). **Confidence:** High (READ).

### LAUNCHSTATION-043 / "Collect a TCP reply, match its id, bound its length" is implemented three times; MD4 twice; UTF-16 conversion twice / Category 2
- **Location(s):** reply collection: `iop/smbman/smb.c:226` (`GetSMBServerReply`), `iop/smb2man/usmb2/usmb2.c:88-140` (`recv_reply_from_socket`), `iop/nfsman/unfs4/unfs4.c:120` (`recv_reply_from_socket`); MD4: `iop/smbman/md4.c` (182 lines) vs `iop/smb2man/usmb2/md4c.c` (240 lines); UTF-16: `iop/smbman/smb.c:276,294` (`asciiToUtf16`/`utf16ToAscii`) vs `iop/smb2man/usmb2/unicode.c` (204 lines).
- **Problem:** These are separate IRXs, so each needs its own *runtime* copy — this finding is not "link them together." It is that the *source* of three security-sensitive receive loops (the code that decides how many wire bytes may land in a fixed buffer, and which reply belongs to which request) is maintained in three places, and the round-2 line-audit found their hardening uneven precisely because of it: usmb2's loop verifies signatures, MessageId and padding; smbman's was hardened in its own separate pass; unfs4's in a third. The same holds, at lower stakes, for two MD4s and two UTF-16 converters.
- **Fix:** Single-source each receive loop the way -042 proposes (one .c included by each IRX that needs it, per the pattern at `smb2man.c:199` / `nfsman.c:145`), with the per-protocol parts (header size, id offset, validation callback) as the only per-module residue. Same for MD4 and the UTF-16 converters.
- **Benefit:** The next receive-loop hardening lands everywhere at once; INFERRED ~300-400 duplicated source lines removed across the three pairs.
- **Effort:** L. **Risk:** Med (three live drivers; do it one pair at a time, receive loops last and most carefully). **Confidence:** High that the duplication exists (READ); the line estimate is INFERRED.

### LAUNCHSTATION-044 / ps2ftpd and smbman IRX targets build without `--gc-sections`, so their dead code ships in the IRX / Category 1/7
- **Location(s):** `ps2.yaml:215` (ps2ftpd target) and `ps2.yaml:364` (smbman target) carry no `ldflags` for section GC; the flag exists in the same file for smb2man (`ps2.yaml:273`), nfsman (:290), smbd (:358) and two later targets (:439, :461) — five of the targets.
- **Problem:** IOP RAM is 2 MB and every IRX byte is resident. The two oldest, largest hand-maintained network modules are exactly the ones whose unreferenced functions are *not* stripped at link time — including, in ps2ftpd's case, handlers like the ones in -049. Their sibling modules made the opposite choice in the same build file, so this reads as drift, not policy.
- **Fix:** Add the same `ldflags` line the sibling targets already carry to the ps2ftpd and smbman targets. A build-file alignment with an in-file precedent — the opposite of a new mechanism.
- **Benefit:** INFERRED: whatever unreferenced code those two modules currently hold stops shipping; every future dead function in them is free. (No build was run in this VM, so no byte figure is claimed.)
- **Effort:** S. **Risk:** Low (same flag, same file, five precedents; needs one build + on-hardware FTP/SMB1 smoke check before being called done). **Confidence:** High for the flag's absence/presence (READ); INFERRED for the size effect.

### LAUNCHSTATION-045 / `isHddRootPath` exists three times, with three different definitions / Category 2
- **Location(s):** `src/filer.c:657-660` (via `getHddDeviceFromPath`, then `path[5] == '/'`), `src/filer_browser.c:23-26` (via `isHddBrowserPath`, then `path[6] == '\0'`), `src/filer_copy.c:349-352` (open-coded `strncmp`/digit/`':'`/`'/'` checks).
- **Problem:** The same predicate — "is this path an HDD device root" — gates menu behaviour, browsing and copy behaviour in three files, and the three bodies are three different formulations of it. They agree on today's device names by construction, not by sharing: a future device form (or a fix to one's edge case, e.g. the filer.c version never checks `path[4] == ':'` itself, relying on its helper) changes one screen's answer and not the others'.
- **Fix:** One definition in the filer shared header the three files already include, the other two deleted. Eleven lines become four, once.
- **Benefit:** One answer to a path question three subsystems depend on; ~15 lines removed.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ, all three bodies read this session).

### LAUNCHSTATION-046 / `getXfromRelativePath` is byte-identical in two files / Category 2
- **Location(s):** `src/filer_copy.c:490-500` and `src/filer_actions.c:13-23` (bodies diff-verified identical this session, both under `#ifdef XFROM`).
- **Problem:** The pass-1 rule for a shared helper: same logic in 2+ places **and** net removal — this is the textbook case. Any change to XFROM relative-path handling must currently be made twice, in files a maintainer would not expect to be linked by this function.
- **Fix:** Keep one copy in the shared filer header (or the file both already depend on), delete the other.
- **Benefit:** ~11 lines removed; one definition.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-047 / Little-endian read/write helpers are copied into five memory-card files (16 copies), and the save-name rule exists twice / Category 2
- **Location(s):** `rd16`: `src/mc_card_view.c:31`, `src/mc_card_write.c:20`, `src/mc_ps1_card.c:17`, `src/mc_save_formats.c:26`. `rd32`: `mc_card_view.c:32`, `mc_card_write.c:21`, `mc_ps1_card.c:18`, `src/mc_save_export.c:14`, `mc_save_formats.c:27`. `wr16`: `mc_card_write.c:25`, `mc_ps1_card.c:22`, `mc_save_formats.c:31`. `wr32`: `mc_card_write.c:30`, `mc_ps1_card.c:27`, `mc_save_export.c:18`, `mc_save_formats.c:36` — all identical one-to-four-line statics. Save-name rule: `src/mc_save_formats.c:44-46` defines `nameOk` under the comment "The same rule as mc_card_write.c" — the public `mcCardNameOk` already exists at `src/mc_card_write.c:41`; the formats copy differs mainly in taking the field size as a parameter.
- **Problem:** Individually trivial, collectively a drift surface across the five files that parse and write card images — the code where a byte-order or validation slip corrupts saves. The name-rule duplication is the substantive half: two implementations of "what may a save be called" can diverge, and the comment in one of them is already doing the work a shared definition should.
- **Fix:** One small shared header for the four endian helpers (the mc_* files already share headers), delete the 15 redundant copies; fold `nameOk` into `mcCardNameOk` with the field-size parameter the formats file needs. Net removal, no new abstraction beyond a header that replaces sixteen definitions.
- **Benefit:** ~45 lines removed; one place to get endianness and name validation right.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-048 / ps2host segments writes using the read-segment constant, and copies a host-supplied dirent name without forcing termination / Category 6/1
- **Location(s):** `iop/ps2host/net_fio.c:308-310` (the segmentation test compares against `PKO_MAX_WRITE_SEGMENT`, then assigns `nbytes = PKO_MAX_READ_SEGMENT`); constants at `iop/ps2host/hostlink.h:276-277` (write segment = 1460 − `sizeof(pko_pkt_write_req)`, read segment = 1460 − `sizeof(pko_pkt_read_rly)` — different sizes); `iop/ps2host/net_fio.c:648` (`memcpy(dirent->name, dirrly->name, sizeof(dirent->name))`, no forced NUL afterwards).
- **Problem:** The write path can emit segments sized by the wrong struct's arithmetic — larger than the write segment the protocol sized its 4096-byte packet buffers around (`net_fio.c:25-28`) whenever the read-reply struct is smaller than the write-request struct. It works today only if the two constants happen to stay close; it is a latent protocol violation written as a plausible-looking line. Beside it, a directory name is copied from the host's reply at exactly the destination's size, so a maximal-length host name leaves the IOP dirent unterminated for whatever EE code later prints or compares it as a string.
- **Fix:** Use `PKO_MAX_WRITE_SEGMENT` in the write path (one identifier), and write a terminating NUL into the last byte of `dirent->name` after the copy (one line). Both are corrections, not additions.
- **Benefit:** Removes a latent oversize-segment bug and an unterminated-string source in the host: device.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

### LAUNCHSTATION-049 / Dead FTP handlers: SITE always answers 500, FEAT is declared and never implemented / Category 1
- **Location(s):** `iop/oldlibs/ps2ftpd/src/FtpCommands.c:406-429` (`FtpClient_OnCmdSite` — a dispatch whose every path ends in "500 Unknown command"); `iop/oldlibs/ps2ftpd/src/FtpClient.h:160` (`FtpClient_OnCmdFeat` declared, no implementation anywhere in the module, no handler registered). (Same category, same audit area: the unreachable second `return readbytes;` after `return nbytes;` at `iop/ps2host/net_fio.c:418-419`.)
- **Problem:** A SITE handler that can only refuse, kept as a full function with a command-table entry, and a FEAT declaration promising an implementation that does not exist — in the module that -044 shows is not section-stripped, so the SITE body ships. Readers tracing FTP behaviour waste time on both.
- **Fix:** Delete the SITE function and let the generic unknown-command path answer (it already produces the same 500), delete the FEAT declaration, delete the unreachable return.
- **Benefit:** INFERRED ~25 lines removed; the command table says only what the server does.
- **Effort:** S. **Risk:** Low (behaviour identical by construction — SITE already always 500s). **Confidence:** High (READ).

### LAUNCHSTATION-050 / One shared static 32 KB buffer serializes every FTP client / Category 5 (Performance)
- **Location(s):** `iop/oldlibs/ps2ftpd/src/FtpClient.c:88` (`static char buffer[32768]`, under the comments "this shared buffer is used for everything the clients do" and "Sockets are blocking. Finish each shared-buffer send before another client runs."); server allows `MAX_CLIENTS 4` (`iop/oldlibs/ps2ftpd/src/FtpServer.h:16`).
- **Problem:** All data transfers for all (up to four) clients funnel through one static buffer on blocking sockets, so clients are effectively served one transfer at a time and a slow receiver stalls the others. The code knows it — the comment is a warning to future editors, not a design anyone chose on purpose. It also means the buffer can never shrink: it is sized for the largest single transfer chunk on the slowest device.
- **Fix:** Give each client struct its own transfer buffer (the client array already exists; the buffer moves into it). This is a relocation of an existing allocation, not a new mechanism — and it can go smaller per client if the maintainer wants the IOP bytes back.
- **Benefit:** Concurrent clients stop head-of-line blocking each other; transfer throughput on multi-client use stops being capped by the slowest receiver.
- **Effort:** M. **Risk:** Med (touches the transfer hot path; IOP cost is +3 buffers if all four slots are live — state it, don't hide it). **Confidence:** High (READ).

### LAUNCHSTATION-051 / smbman's `prepare_path` does not guarantee NUL termination by construction / Category 6
- **Location(s):** `iop/smbman/smb_fio.c:311-312` (`strncpy(full_path, smb_curdir, max_path - 3)` followed immediately by `strcat(full_path, "\\\\")`), function at `smb_fio.c:305`; the destination is `smb_curdir`-sized storage (`SMB_PATH_MAX 1024`, `smb_fio.c:93-99` region), and `smb_curdir` itself is set with a capped `strncpy` at `smb_fio.c:859`.
- **Problem:** If `smb_curdir` ever fills its bound without a terminator, `strncpy` leaves `full_path` unterminated and the following `strcat` walks past the buffer looking for a NUL. In practice the surrounding caps make a full, unterminated `smb_curdir` hard to reach — every buffer in the chain is the same 1024 and the setters cap below it — so this is filed as a code-smell with an honest confidence label, not as a demonstrated overflow. It is the one place in smbman still using the unbounded-string idiom the rest of the module (and smb2man, per the checked-clean list below) moved away from.
- **Fix:** Terminate explicitly after the `strncpy` (one line: `full_path[max_path - 3] = '\0'` before the `strcat`), or build the path with the bounded pattern the module uses elsewhere.
- **Benefit:** The last unterminated-string path in the SMB1 client closes by construction instead of by surrounding luck.
- **Effort:** S. **Risk:** Low. **Confidence:** Low (READ — the idiom is verified; exploitability is INFERRED-unproven and stated as such).

### LAUNCHSTATION-052 / The nine-line `FILEOP_TRACE` guard block is pasted into three filer files / Category 2
- **Location(s):** `src/filer_fileops.c:5-12`, `src/filer.c:157-164`, `src/filer_copy.c:23-30` — the identical `#ifdef ULE_DEBUG_BUILD / #ifndef FILEOP_TRACE / … / #endif` block, three times.
- **Problem:** Trivial in itself; filed because it is the kind of copy that drifts (one file's default changes, the others don't, and debug builds stop meaning the same thing across the copy engine). Verified benign today: the guard correctly resolves to 0 in non-debug builds in all three copies.
- **Fix:** Define it once in the shared filer header all three files include; delete the two redundant copies (keep one home).
- **Benefit:** ~16 lines removed; one definition of the trace switch.
- **Effort:** S. **Risk:** Low. **Confidence:** High (READ).

---

## Checked in round 2 and deliberately NOT flagged

Line-auditing produces non-findings too; these areas were read and are in better shape than their age suggests. Recording them so they are not re-flagged from grep alone:

- **FTP command length is bounded.** The control connection receives into `m_CommandBuffer[512]` (`FtpClient.h:123`) and over-long commands disconnect the client; the `strcpy` into the 32 KB transfer buffer at `FtpClient.c:186` is bounded by that caller. **FTP path traversal is handled:** `FileSystem_BuildPath` normalizes `..` clamped at the virtual root, and device/unit/partition are validated in `ClassifyPath` (`FileSystem.c`, read in full).
- **smbman (SMB1) is largely pre-hardened.** `GetSMBServerReply` bounds the reply to `SMB_buf` (`smb.c:226` ff.); negotiate validates `KeyLength`/`ByteCount` against the received byte count before the `memcpy` (`smb.c:410-426`); TRANS2 find bounds `ParamOffset`/`DataOffset`/`FileNameLen` against the received length (`smb.c:1532-1575`). All `smb_*` entry points have live fio/internal callers — none are dead.
- **smb2man/usmb2 is largely pre-hardened (RiptOPL ports).** Reply signature verification with MessageId matching, an interim-reply cap, READ `DataOffset` padding validation and a bounded padding drain (`usmb2.c:240-490`); the devctl connect path validates NUL termination with `memchr` before any `strcpy` (`smb2man.c:1501-1528` region); `dread` clamps `namelen` and bounds the name conversion (`smb2man.c:1288-1289`).
- **nfsman/unfs4:** `get_fh` bounds the filehandle length against `UNFS4_MAX_FH` before copying (`unfs4.c:472-482`); the nfsman devctl does the same `memchr` NUL validation as smb2man's.
- **Memory-card save parsing is careful.** `mc_save_formats.c` enforces `MAX_TOTAL`, `MAX_INFLATE` and the name rule on hostile `.psu`/`.cbs`-style inputs (read :40-175 plus targeted sections); the inflate path allocates one spare byte past a stated size specifically so an over-long stream is caught, not cut (:150-161 region).
- **The dashboard does not do blocking file I/O on the UI thread.** Listings run as worker jobs with generation guards (`dash_browser.c:294-420`), copies run through the worker-backed chunk path (`filer_copy.c:80-84,1562-1563`) with a synchronous fallback only when no progress UI exists, and the dashboard's copy/move/delete menus drive the *same* filer engine as the classic browser (`filerPasteModeInto`, `filerPsuPasteInto`) rather than a second implementation. The per-device directory readers in `filer.c` (readMC/readCD/readHDD/readHOST/…) are separate because the devices genuinely differ; no honest merge was found there.
- **`FILEOP_TRACE` costs nothing in release builds** — the guard resolves to 0 without `ULE_DEBUG_BUILD` in all three copies (see -052).
- **ps2host's other `pko_*` functions all have callers** in `net_fsys.c` (grep-verified one by one) — no dead protocol verbs there.

---

## C) Coverage — what was fully read, what is still unread

### Fully read this round (at `9fb0598a79f0`)
- Self-update: `src/self_update.c` (434 lines), `src/update_psu.c` (364), `include/self_update.h` — end to end.
- ps2ftpd: `FtpCommands.c` (429), `FtpClient.c` (705), `FtpServer.c` (371), `FileSystem.c` (960), `main.c` (193), `FtpControl.c` (138), `FtpClient.h`, `FileSystem.h` — end to end.
- ps2host: `net_fio.c` :95-480 end-to-end plus targeted sections to :648; `net_fsys.c` :1-80 plus caller greps for every `pko_*` function.
- EE cluster, substantial sections: `dash_browser.c` :280-590 and :795-1085 plus targeted reads/greps throughout; `filer_copy.c` :1400-1721 (the copy engine) plus targeted sections; `dash_card_tool.c` safety-copy/write path :227-434 plus allocation greps; `mc_save_formats.c` :40-175 plus targeted sections; `filer.c`, `filer_browser.c`, `filer_actions.c` targeted sections around every citation.

### Read in targeted sections only (citations verified; full-file read still owed)
- smbman: `smb.c` :140-470 end-to-end, targeted sections to ~:1670; `smb_fio.c` targeted sections only.
- smb2man: `usmb2/usmb2.c` :60-620 and :890-1085; `smb2man.c` targeted ranges (~1,000 lines outside them unread); `usmb2.h` :1-64.
- nfsman: `nfsman.c` and `unfs4/unfs4.c` targeted ranges only (parse/connect/devctl/receive paths).
- EE cluster remainders: `dash_browser.c` outside the ranges above, `dash_card_tool.c` outside the write path, `mc_card_write.c`, `mc_card_view.c`, `mc_card_export.c`, `mc_save_export.c`, `mc_ps1_card.c`, `filer_mount.c`, `filer_device.c` — grep-swept and read around citations, not end to end.

### Still unread
- `iop/smbd` (vendored libsmb2 server, ~60 sources) — inventoried in pass 1 only, never line-audited.
- `iop/vmcman`, `iop/mmceman`, `iop/mx4sio_bd`, `iop/ata_bd`, `iop/usbmass_bd` internals — out of this round's scope, untouched.
- No PS2 build or on-hardware test was possible in this VM (no PS2SDK); every compiled-size statement above is labelled INFERRED where it depends on one.

*End of round 2 — Launchstation. 20 new findings (LAUNCHSTATION-033…052), 1 revalidation in full (-017: **STILL TRUE**), 6 further pass-1 findings revalidated or confirmed standing, and the checked-clean list above.*
