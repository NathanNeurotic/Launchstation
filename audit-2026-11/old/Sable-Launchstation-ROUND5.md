# Sable Audit — Launchstation, ROUND 5

- **Repo:** NathanNeurotic/Launchstation
- **Pin:** `master` @ `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` — **held**. Verified three ways at the start and end of this round: clone `git rev-parse HEAD`, `git ls-remote origin master`, and the round-4 report's end-state all return the same SHA. No clone update was needed, so there is no delta and no revalidation is owed. `git status --porcelain` empty at end. **READ ONLY held throughout.**
- **Method:** nine parallel section passes over the round-4 still-unread list plus a repo-wide status sweep, followed by a coordinator integration pass that read all nine section files in full and **personally spot-checked the headline citations** (mask line, MAXSLOT define, validator, mount cleanup chain, EE slot gate, unit2card call-site count; mmce wrbuf assignments + both dead imports; apaRename semaphore sites + journal restore body; IsoTd LUT clear + usbd dead imports; xparam macro + 292-row count; ds34bt memset/send lines + length constants; dead ATAD loader + both mcDir statics + unconditional HDL flag; config.c CNF sinks + double PathPad_Lock write + `char[64]` Misc fields; cloud_http atoi sites + MMCE naming formats; editor row-write + both gui.c strcpy sites + zero-caller grep for the libhdd four). Every spot-check matched. Two gaps the sweep found outside all nine sections were closed by the coordinator directly: `src/popstarter_internal.h` (75 lines, declarations/constants only, no defect) and `iop/include_compat/poweroff.h` (vendored PS2SDK declarations header, no code, no defect).
- **Category note:** categories below follow the audit scheme (1 dead code, 2 duplication, 3 overengineering, 4 size/memory, 5 performance, 6 bugs/security, 7 build/CI/repo clutter, 8 half-finished/redundant). Two section passes used category numbers loosely (several bounds bugs labelled 1/2, several hangs labelled 3); **all were re-mapped to the scheme at integration** — no finding's substance changed.
- **Mandatory note (Task 2):** `ps2smb.h` is an external PS2SDK header and is **absent from this repo and from the clones**. LAUNCHSTATION-066's caveat (the `McGetDefrag`/`ps2smb.h` uncertainty attached to the smbman devctl finding) is therefore **unresolvable from the clones and remains exactly as-is**. Nothing in this round changes -066.
- **Result:** **89 new findings, LAUNCHSTATION-097…185**, plus two settlements (-085 closed as proven waste; the round-4 unnumbered mcdev mask lead filed as -118). **No revisions to -001…-096 were forced** by anything read this round; -033, -052, -014, -015, -026, -036, -003, -068, -077 and the X-18/-021 items were re-verified in context where sections passed over them and all stand. Launchstation total after round 5: **121 + 89 = 210 findings** (counting -065, withdrawn in round 4, in the 121).
- **No build, no link test, no hardware** — there is no PS2 toolchain in this VM. All Category 5 magnitudes are structural; all IOP/EE RAM figures are source arithmetic.

Section sources: `round5-sections/01-mcdev-085.md` … `09-ee-small-misc.md` (local labels T1…T9 are cross-referenced in each finding's header line).

---

## 1. Settlements

### 1.1 LAUNCHSTATION-085 — SETTLED: proven waste. Apply the fix. (Section 01)

-085 asked whether any flow can populate VMCMAN slots 4–9 under `MCMAN_MAXSLOT=10` (`iop/vmcman/src/mcman-internal.h:105-109`). After a full read of `mcdev.c` (842 lines) and `mciomanx_backing.c` (371 lines) and every EE Mc*/vmc consumer, the answer is stronger than the question: **no in-tree flow can populate slots 2–9 at all; only slots 0 and 1 are ever populated.**

Evidence chain (all READ):
1. **Device layer:** `mcman_unit2card` (mcdev.c:216-231) is the only slot derivation; all 11 call sites are in mcdev.c, each under `mcman_io_sema`. Its VMCMAN branch computes `unit & (MCMAN_MAXSLOT-1)` = `unit & 9`, whose image is exactly **{0,1,8,9}** — slots 2–7 are arithmetically unreachable from the device layer for any unit (see §1.2).
2. **EE gates:** the mounted-state table is `int vmcMounted[2]` (`src/filer.c:60`); the canonical mount entry `filerMountVmc` rejects `slot > 1` (`src/filer_browser.c:2353`, spot-verified); every enumeration loop is bounded at 2 (`src/dash_browser.c:475-486`, `src/dash_devices.c:485-492`, `src/filer.c:2343-2348`, `src/filer_mount.c:161-168`); every consumer-side re-parse of a `vmcN:` path also gates at 2 (`src/main_actions.c:371`, `src/filer_browser.c:219-221`, `src/dash_devices.c:231-234`); no `"vmc2:"`…`"vmc9:"` literal exists in `src/` or `include/`. The feature's own documentation promises only vmc0:/vmc1: (`docs/changes.txt:258`).
3. **Library side:** the backing layer's only mount callers are `mc_mount`/`mc_umount` (the masked device path); the only other in-tree mcman-library consumer, `iop/smbd`, imports **xmcman** (physical), slot 0 only. VMCMAN's extended exports are `_dummy` in this build.
4. **Cost, restated:** fatcache −24,624 B (`main.c:60`, McFatCache 2,052 B × 2 × (10−4)), devinfos −9,216 B (`main.c:64`, MCDevInfo 384 B × 4 × 6), backing cardinfo −144 B (`mciomanx_backing.c:26`, 24 B × 6; 24 B is INFERRED from field layout). **Total ≈33,984 B** reclaimable at MAXSLOT=4.
5. **The fix also fixes the mask:** at MAXSLOT=4 the expression becomes `unit & 3` — contiguous and complete; the physical branch at MAXSLOT=4 is already exact. In-tree evidence would support even 2, but **4 is the recommendation** (physical parity, headroom for a future 3rd/4th VMC); dropping to 2 saves a further ~5.3 KB at the cost of capping the device at two VMCs forever — the maintainer's call, not required by the evidence.

**Settlement: -085 is CLOSED as proven waste. The fix is -085's own: delete the `#ifdef BUILDING_VMCMAN` override at `mcman-internal.h:105-109` so VMCMAN uses 4 — a pure deletion.**

### 1.2 The mcdev slot mask — verdict: LATENT LIVE DEFECT, filed as LAUNCHSTATION-118

The round-4 unnumbered lead asked whether `unit & 9` is a live defect, an intentional sparse map, or dead generality. **Verdict: a latent live defect** — the power-of-two mask idiom applied to the non-power-of-two constant 10. No evidence of intent exists (no comment at either site, no consumer of slots 8/9 anywhere in-tree, the vmcman README's local-delta list never mentions the widening or a sparse scheme, the feature documentation names only vmc0:/vmc1:). Its failure mode is silent aliasing: a presented `vmc2:`–`vmc7:` does not error — it opens, reads and **writes** slot 0's or 1's mounted image; `vmc8:`/`vmc9:` address permanently unformatted state. It is latent rather than live-in-tree for exactly one reason — every EE gate in §1.1(2) stops units above 1 before they reach the IOP — while the device itself is registered globally with iomanX and accepts any unit any caller presents. Full finding text at -118; **its fix is the -085 deletion itself** (do not "fix" the mask with `% MCMAN_MAXSLOT` while keeping 10 — that keeps the dead statics and adds a division to a hot IOP path).

Interaction with -068 (not re-filed): -068's "currently unreachable" judgement for `slot == MCMAN_MAXSLOT` relied on this mask and **holds, strengthened** — and it survives the -085 fix too (at MAXSLOT=4 the mask bounds slots to [0,4)). Neither fix substitutes for the other.

---

## 2. Findings (LAUNCHSTATION-097…185, sorted by benefit)

### LAUNCHSTATION-097 / CNF load copies file-supplied values unbounded into fixed SETTING fields — the 64-byte Misc* fields overflow on a ~65-char line / Category 6 (T7-F1)
- Locations: `src/config.c:962` (`strcpy(setting->LK_Path[i], value)`); `src/config.c:973-1004` (`sprintf(setting->Misc, "%s/", value)` + fifteen `sprintf(setting->Misc_*, "%s%s", …)`); `src/config.c:1040-1048,1058-1067` (`strcpy` into `usbkbd_file`, `kbdmap_file`, `CNF_Path`, `lang_file`, `font_file`, `popstarter_file`, `ember_dir`, `neutrino_file`). Fields: `include/launchelf.h:285-300` (Misc and every Misc_* are `char[64]`, spot-verified), path fields `char[MAX_PATH]` = 1025. Downstream sink: `src/config.c:1674` (`strcat` of LK_Path into a MAX_PATH display buffer).
- Problem: `value` is a whole CNF line, length-unbounded (the streamed preload accepts up to 128 KB). `Misc = <65+ chars>` overruns Misc[64]; every Misc_* overruns once the Misc prefix is prepended; LK/path keys overrun their 1025-byte fields. `setting` is one heap struct — the overrun corrupts following fields, then heap metadata. This is the **load** side; -015 filed the config-*save* chain, never these sinks. The bound is known in this very function: SMB credentials are length-checked (`config.c:1050-1053`) and LK_Title/PathPad use bounded copies (`:1088-1089,1098-1099`) — these fields are the unconverted remainder.
- Fix: Convert exactly the cited copies to the bounded pattern already used at :1088-1089 (`strncpy` + forced NUL; `snprintf(field, sizeof(field), …)` for the Misc sprintfs). Replacement only; over-long values truncate, as LK_Title already does.
- Benefit: A crafted or merely corrupted LAUNCHELF.CNF — a file users routinely download, share and hand-edit — can no longer write past SETTING at startup.
- Effort: S · Risk: Low · Confidence: READ (sizes, source length, absent checks); INFERRED (what a given overrun corrupts on hardware).

### LAUNCHSTATION-098 / Text editor writes one row-width entry per text row into a 2,048-entry array with no bound / Category 6 (T9-F1)
- Locations: `src/editor_rules.c:26` (`Editor_nRowsWidth[Editor_nRowsNum] = Editor_nCharRows; Editor_nRowsNum++;`, spot-verified); array `src/editor.c:11` / `include/editor_private.h:63`, `MAX_ENTRY = 2048` (`include/launchelf.h:205`); editor accepts files up to 512 KiB (`src/editor_file.c`).
- Problem: Row count is a function of file content. A 512 KiB file of two-byte lines yields ~262,000 rows; `editorApplyRules` writes past entry 2047 into following BSS for the remaining ~260,000 rows, on any open/scroll/edit that recomputes rules. No clamp anywhere on the path.
- Fix: Stop the write at `MAX_ENTRY - 1` rows (treat the rest as one trailing row) or refuse over-row-count files with the existing load-error dialog. One bound check.
- Benefit: Removes a content-triggered BSS corruption reachable by opening an ordinary, if line-dense, text file in the shipped editor.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-099 / `hdl_ginfo_read` strcpy's disk-controlled strings into 65- and 13-byte fields and sums subs[] for a disk-controlled nsub past the 64-entry array / Category 6 (T5-F4)
- Locations: `iop/hdl_info/hdl.c:43-49` (loop over `get_u32(&part->nsub)` summing `subs[i].length`; `subs` is 64 entries per `ps2_hdd.h`); `hdl.c:48-49` (`strcpy(ginfo->name, buffer + 8)`, `strcpy(ginfo->startup, buffer + 0xac)`); destinations `hdl.h` (`name[65]`, `startup[13]`); source is a 1 KiB sector from the game's own header area (`hdl.c:33-40`).
- Problem: A crafted/corrupt HDL game partition controls all three: nsub > 64 reads past the header struct while summing; an unterminated name/startup overflows the heap games array. The APA checksum does not gate this sector — it is a different, unchecksummed area.
- Fix: Clamp the loop to PS2_PART_MAXSUB; bounded copies + forced NUL (the adjacent partition_name copy at :46-47 already does exactly this pattern).
- Benefit: Hostile/corrupt HDD images stop being a heap-overflow vector in a module whose whole job is parsing HDD images.
- Effort: S · Risk: Low · Confidence: READ (mechanism); INFERRED (exploitability beyond heap corruption).

### LAUNCHSTATION-100 / `usmb2_readdir` walks server-supplied directory-entry offsets with no bound against the 272-byte directory buffer / Category 6 (T5-F14)
**Verification note (2026-11-14, full verification): WITHDRAWN — REFUTED on adversarial re-derivation: `usmb2_readdir` sits inside `#ifdef USMB2_FEATURE_OPENDIR` (usmb2.c:743-820), a macro no build file defines, and has zero callers; the live directory path (smb2man.c) does clamp; see Sable-VERIFY2-LAUNCHSTATION.md. Do not act on this finding.**
- Locations: `iop/smb2man/usmb2/usmb2.c`, `usmb2_readdir` (QUERYDIR block in the 620-890 remainder): reply length copied unbounded into `dh[16 + 4]`; chain walk `curr_de`/`next_de` advanced without comparison to buffer size; filename length at `dh[16 + curr_de + 0x3c]` trusted. Buffer: `dh` = 16 + MAX_DIR_SIZE = 272 B (`usmb2.c:69`).
- Problem: A malicious or broken SMB2 server controls the reply length, the NextEntryOffset chain and each FileNameLength; none is checked against 272 B (nor against `usmb2->reply_len`, which the RiptOPL patch *does* check for READ replies in the same file range — the pattern exists in-file and was not applied here). Heap over-copy on the reply store; out-of-bounds read/write (`ptr[i] = 0`) during name conversion.
- Fix: Clamp the reply copy to MAX_DIR_SIZE − 4 (the OutputBufferLength actually requested); break the chain walk when curr_de/next_de or the filename field would pass the copied length — mirroring the READ-reply checks.
- Benefit: A hostile server stops owning IOP heap via directory listing, the most server-exposed operation in the driver.
- Effort: S · Risk: Low · Confidence: READ (absent checks); INFERRED (that build_request does not pre-cap the reply).

### LAUNCHSTATION-101 / Journal restore trusts the on-disk journal count and sector list completely, then erases the journal even when restore fails part-way / Category 6 (T3-F5)
- Locations: `iop/ps2hdd_osd/journal.c:58-88` (`apaJournalRestore`; spot-verified); layout `iop/ps2hdd_osd/libapa.h:184-189` (`sectors[126]`); callers `hdd.c:342-348` and the dead branches of T3-F10's file.
- Problem: After a magic check, `journalBuf.num` from disk is used as loop bound and `sectors[]` index with no `0 <= num <= 126` validation and no per-sector range check; journaled headers are written verbatim to `sectors[i]`. A corrupt/crafted journal therefore replays out-of-bounds sector values as destination LBAs for 2-sector header writes at startup. Compounding: `clink = apaCacheAlloc()` at :73 is unchecked before `clink->header` is dereferenced at :76, and the function ends with `return apaJournalReset(device)` unconditionally — a part-way replay failure still erases the journal, destroying the only copy of unreplayed entries.
- Fix: Treat `num < 0 || num > 126` as "no valid journal"; check the apaCacheAlloc result; on replay error return the error **without** apaJournalReset.
- Benefit: Crash-recovery replay stops being a disk-controlled arbitrary-header-write primitive and stops discarding unreplayed entries.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-102 / The on-disk nsub field is trusted as a loop bound and array index in four places; the guards that exist check a different variable / Category 6 (T3-F8)
- Locations: `iop/ps2hdd_osd/hdd_fio.c:728-736` (fioGetStatFiller sums `subs[i]` for header nsub; `subs` is `apa_sub_t subs[APA_MAXSUB]`, `libapa.h:116`); `:872-874` (ioctl2AddSub writes `subs[header->nsub]` — the guard at :838 tests `fileSlot->nsub`, not the header's); `:885-895` (ioctl2DeleteLastSub reads `subs[header->nsub - 1]` — guard at :891 tests fileSlot->nsub == 0; a diverged header nsub of 0 indexes subs[-1]); `:498-526` (apaRemove's sub-deletion loop bounded by header nsub).
- Problem: nsub comes from the on-disk APA header — checksum-protected against corruption but fully chosen by whoever wrote the disk image, and independently writable via HDIOC_WRITESECTOR (-107). fileSlot->nsub and header->nsub agree in normal operation; every site indexing by the header copy is one crafted/desynchronised header away from out-of-bounds access inside the 1,024-byte cache allocation. fioGetStatFiller runs on getstat/dread against any partition header on the disk.
- Fix: Validate once where the header is first trusted (apaOpen): reject/clamp `header->nsub > APA_MAXSUB` with -EIO. One site covers all four.
- Benefit: A crafted partition header can no longer drive out-of-bounds reads/writes in stat, add-sub, delete-sub or remove.
- Effort: S · Risk: Low · Confidence: READ (code); INFERRED (divergence requires a crafted or raw-written header).

### LAUNCHSTATION-103 / `hdl_ginfo_write` writes a sector back to disk without checking the read that filled it, and copies 128 bytes from a 65-byte name field / Category 6 (T5-F3)
- Locations: `iop/hdl_info/hdl.c:108-123` (read result assigned at :115, overwritten at :120; `memset`+`memcpy(buffer + 8, ginfo->name, PS2_PART_NAMEMAX)` at :117-118); `ginfo->name` is `char[65]` (`hdl.h`), PS2_PART_NAMEMAX is 128 (`ps2_hdd.h`).
- Problem: (1) If the initial read fails, `buffer[1024]` is uninitialised stack, the name patch is applied to garbage, and the garbage is **written to the game's header sector on disk** — a failed read becomes on-disk corruption. (2) The 128-byte memcpy over-reads 63 bytes past ginfo->name (into the next game's entry in the caller's array) and writes them into the sector past the name field.
- Fix: Return the read result if non-zero / bytes != 1024 before touching buffer (mirrors hdl_ginfo_read's own check at :40); copy strlen bounded to the field, not PS2_PART_NAMEMAX.
- Benefit: Removes the only write-to-disk-on-error path in the module and a 63-byte struct over-read.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-104 / `atoi()` on server-supplied Content-Length overflows before the range check / Category 6 (T8-F1)
- Locations: `src/cloud_http.c:596,644,668` (read_response) and `:1123` (download path) — all four spot-verified.
- Problem: The header value — fully server-controlled — is converted with atoi() into an int and only afterwards compared against CLOUD_HTTP_BODY_MAX (256 KiB, `include/cloud_http.h:10`). `Content-Length: 99999999999` overflows the conversion first (UB; in practice wraparound), so a wrapped negative or small-positive result passes a check the real length would fail, and body handling proceeds on a false length.
- Fix: Parse with strtol into a long; reject/clamp outside 0…CLOUD_HTTP_BODY_MAX before any use; unparseable = protocol error.
- Benefit: Removes UB on untrusted network input at the single point where every response body size is decided.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-105 / Classic yes/no and info dialogs strcpy unbounded caller text into fixed stack buffers that several callers already exceed / Category 6 (T9-F3)
- Locations: `src/gui.c:20` (ynDialog: `strcpy(msg, message)` into `char msg[512]`, spot-verified), `src/gui.c:111` (nonDialog: same into `char msg[2400]`). Oversized callers: `src/lang.c:537,564,571` (error report built in `char tmp_s[4096]`); `src/main_exploit_installer.c:1004,2487` (msg is `char[MAX_PATH + 96]` = 1,121 B with a full path interpolated).
- Problem: The dialog is the sink for exactly the strings most likely to be long — error reports and path-bearing confirmations. A language-load failure report, or an installer overwrite confirmation for a near-MAX_PATH path, exceeds 512 bytes by construction and overwrites ynDialog's stack frame. nonDialog has the same shape with a larger ceiling.
- Fix: Bound both copies to their buffers (dashStrCopy-style truncation already used across the dashboard) — the dialogs word-wrap and clip visually anyway.
- Benefit: Closes a stack overwrite on the two most-called classic UI entry points, including an error path that fires precisely when something else already went wrong.
- Effort: S · Risk: Low · Confidence: READ (mechanism); INFERRED (frequency).

### LAUNCHSTATION-106 / Config-descriptor fetch trusts device-supplied wTotalLength and never checks transferedBytes; short transfers parse stale bytes, oversize devices wedge silently / Category 6 (T4-F2)
- Locations: `iop/usbd-ra/src/hub.c:402-440` (fetchConfigDescriptors); feeder `hub.c:444-475`; consumer walk `iop/usbd-ra/src/hcd.c:185-206`.
- Problem: (1) The fetch length is `READ_UINT16(&desc->wTotalLength)` from the 4-byte header fetch; no check that wTotalLength >= 9 or that it matches delivered bytes — `req->transferedBytes` is never consulted. A device that ACKs the full-length request but delivers fewer bytes leaves the tail of its 512-byte static slot holding the **previous device's descriptors** (slots zeroed only once at pool allocation), and doGetDeviceStaticDescriptor plus every LDD probe/connect then parse that mixture as the new device's own. (2) bNumConfigurations (device byte, up to 255) is trusted as the loop bound. (3) When cumulative size exceeds maxStaticDescSize the function just returns (`hub.c:427-429`) — no killDevice, no completion: the device stays neither connected nor flushed until unplug.
- Fix: Require `transferedBytes == readLen` (and wTotalLength >= 9 on the header pass) before advancing; on mismatch or the too-large branch, call the existing killDevice instead of returning bare.
- Benefit: Descriptor parsing stops consuming cross-device stale data; a non-conforming device is rejected deterministically instead of occupying a slot in limbo.
- Effort: S · Risk: Low–Med · Confidence: READ (mechanism); INFERRED (field prevalence).

### LAUNCHSTATION-107 / HDIOC_READSECTOR / HDIOC_WRITESECTOR take LBA, count and data pointer from the caller with no bound against the disk, and writes bypass the APA cache and journal entirely / Category 6 (T3-F13)
- Locations: `iop/ps2hdd_osd/hdd_fio.c:1136-1146` (dispatch; arglen/buflen discarded at :1060-1062); primitives forward unchecked via `hdd_blkio.h:37-40`.
- Problem: Neither case tests lba, lba + size, or size against `hddDevices[f->unit].totalLBA` (available in the same switch at :1087). A WRITESECTOR over an APA header sector writes underneath the header cache without invalidation and outside the journal protocol, so later cached reads serve the pre-write header and a dirty flush can silently resurrect it over the raw write. Devctl callers are EE-side code, conventionally trusted on PS2 — a hardening/consistency gap in a deliberately privileged interface, not a remote-exploit claim.
- Fix: Overflow-safe bound `lba + size <= totalLBA` on both cases (-EINVAL otherwise); for WRITESECTOR, flush/invalidate the unit's APA cache before returning (apaCacheFlushAllDirty already exists, `cache.c:113`).
- Benefit: Raw sector access stays inside the physical disk and can no longer desynchronise the driver's own metadata cache.
- Effort: S · Risk: Low–Med · Confidence: READ (checks/coherence); INFERRED (threat framing).

### LAUNCHSTATION-108 / HIOCGETSIZE indexes fileSlot->parts[] with a caller-supplied u32 and no bounds or pointer check / Category 6 (T3-F1)
- Locations: `iop/ps2hdd_osd/hdd_fio.c:957-958` (`rv = fileSlot->parts[*(u32 *)argp].length;`); identical shape in the compiled-out sibling at :977-978 (HIOCGETPARTSTART, gated by a define the OSD branch does not set — dead today, identical defect if enabled); array `apa_sub_t parts[APA_MAXSUB+1]` at `hdd.h:24`; dispatch discards length arguments at :925-933.
- Problem: The ioctl2 caller supplies the index verbatim: no test against fileSlot->nsub or APA_MAXSUB, no argp NULL / arglen < 4 test anywhere in hddIoctl2. An out-of-range index reads past the heap slot array and returns the word as the result; a null argp dereferences null in IOP space.
- Fix: Reject `argp == NULL || arglen < sizeof(u32)` and `*(u32 *)argp > fileSlot->nsub` with -EINVAL before indexing (and in the gated sibling before anyone enables it).
- Benefit: Closes an out-of-bounds read / null deref on a live ioctl path.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-109 / ioctl2Transfer's bounds check is overflowable and the transfer direction is caller-supplied / Category 6 (T3-F2)
- Locations: `iop/ps2hdd_osd/hdd_fio.c:265-282`: sub-index test :267, minimum-sector tests :271-275, end test :277 (`parts[arg->sub].length < arg->sector + arg->size`), transfer :280-281 passing `arg->mode` straight to blkIoDmaTransfer.
- Problem: The only end-of-partition test is the sum `sector + size` with no overflow-safe form, and `arg->mode` is never validated as read-vs-write — the caller's struct decides both span and direction. On any 32-bit field width a sector near the wrap point makes the sum small, passes :277, and the transfer runs outside the partition the check exists to confine it to. (Field widths live in the SDK's hdd-ioctl.h, outside this repo — the wrap is INFERRED; the missing checks are READ. The sub-index test itself is correct — see checked-clean.)
- Fix: Rewrite :277 as `arg->size > parts[arg->sub].length - arg->sector` after the existing minimum-sector tests; reject any mode that is not exactly BLKIO_DIR_READ or BLKIO_DIR_WRITE.
- Benefit: The one raw-transfer ioctl that is supposed to be partition-confined actually stays confined.
- Effort: S · Risk: Low · Confidence: READ / INFERRED (split as above).

### LAUNCHSTATION-110 / APA header injection rewrites 2 MB in place with no rollback and no readback verification / Category 6 (T8-F5)
- Locations: `src/hdd_header_injector.c:1142-1153` (InjectHddPartitionHeaderFromSource); write helper `src/hdd_header_injector.c:466-478`.
- Problem: The injector reads 4,096 sectors of partition header, patches them in memory, and writes them back sector-by-sector directly over the only on-disk copy. A part-way failure returns an error but leaves a mixed old/new header — the partition's SYSTEM.CNF/icon area is then neither version — and even on success the result is never re-read to confirm the write landed. Distinct from -034 (the self-updater overwriting the running ELF); this is the APA partition header on the HDD.
- Fix: After the write loop, re-read the sectors and compare against the patched image; report mismatch as failure. (A true rollback copy is a bigger change; verification alone converts silent corruption into a reported error.)
- Benefit: The most destructive write in the dashboard becomes detectable when it goes wrong instead of surfacing later as an unbootable partition.
- Effort: S (verify) · Risk: Low · Confidence: READ.

### LAUNCHSTATION-111 / Journal write side has no capacity check, and the flush path ignores journal errors and resets the journal anyway / Category 6 (T3-F6)
- Locations: `iop/ps2hdd_osd/journal.c:47-55` (apaJournalWrite: writes at `(journalBuf.num << 1) + APA_SECTOR_APAL_HEADERS`, stores `sectors[num]`, `num++` — no bound test); `iop/ps2hdd_osd/cache.c:113-130` (apaCacheFlushAllDirty ignores apaJournalWrite's and apaJournalFlush's returns, transfers headers, then resets); capacity 126 (`libapa.h:188`); cache size set by the unbounded -n argument (-131).
- Problem: (1) Nothing stops num reaching/passing 126, at which point the header write lands past the reserved journal-header area (sectors 10-262 = exactly 126×2, `libapa.h:31-34`) and `sectors[num]` writes past the static journalBuf. At the default cache size (3) the dirty count per flush cannot reach 126 — reachability needs a raised -n (INFERRED); the mechanism is READ. (2) Unconditionally reachable today: if the journal write/flush fails with -EIO, the flush proceeds to write headers anyway and resets the journal — a failed protection step is silently converted into an unprotected metadata write, defeating the journal's purpose on exactly the disks already erroring.
- Fix: apaJournalWrite returns -ENOSPC (writing nothing) at num >= 126; apaCacheFlushAllDirty propagates journal failure — skip the header transfers and the reset, return the error.
- Benefit: Journal overflow becomes a clean error instead of an out-of-area disk write; journal failure stops masquerading as success.
- Effort: S · Risk: Low–Med · Confidence: READ (mechanisms) / INFERRED (overflow reachability at default cache size).

### LAUNCHSTATION-112 / Backing read/write/erase never check page against the stored cardsize: writes and erases past the end of the image extend the backing file / Category 6 (T1-F3)
- Locations: `iop/vmcman/src/mciomanx_backing.c:211-242` (erase), `:244-292` (write), `:294-371` (read) — each validates only the slot, then seeks to `page * effective_page_size`; `cardinfo->cardsize` is set at mount (:140) and afterwards read only by getcardspec (:195-197). Dispatchers add no bound: `McReadPage` (`main.c:1177-1192`) and `McWritePage`'s VMCMAN branch (`mcsio2.c:640`) forward page verbatim.
- Problem: Any page >= cardsize seeks beyond the end of the image file that mount had just size-checked to the byte (:117-124). A read fails short (benign); a write or erase **extends the backing file** at the wrong offset, silently changing the image's size so it no longer matches its own superblock — the exact invariant mount enforced. In-tree pages are geometry-derived, so reachability is via -079-class crafted geometry or a direct export caller (-084's surface); filed separately because the bound belongs to this layer, which already stores the one number needed, and -079's fix does not bound direct page arguments.
- Fix: One comparison per function after validation: `if ((u32)page >= (u32)cardinfo->cardsize) return 1;` (3 lines, no new state).
- Benefit: The backing layer can no longer grow or scribble outside the image it validated at mount, whatever produced the page number.
- Effort: S · Risk: Low · Confidence: READ (absent check, stored field, dispatchers); INFERRED (reachability).

### LAUNCHSTATION-113 / Exploit installer trusts card/dir-supplied entry names to be NUL-terminated — the one filer path without the codebase's own termination guard / Category 6 (T7-F3)
- Locations: `src/main_exploit_installer.c:1205-1214` (wipe_mc_folder_contents: `entries[i].EntryName` from mcGetDir passed to strcmp :1209 and snprintf :1213); `src/main_exploit_installer.c:1701-1723` (estimate_existing_folder_kb: `iox_dirent_t entry.name` from fileXioDread to strcmp :1706 / snprintf :1709). Name width: `EntryName[32]` (`iop/vmcman/include/mcman.h:32`).
- Problem: A directory entry can fill all 32 name bytes with no NUL; neither consumer forces termination before treating the field as a C string, so strcmp/%s read past the field — past the end of the 8-entry stack scan array in the wipe loop's last slot. The codebase treats exactly this input as untrusted elsewhere: `filerRenameCaseState` refuses a dirent with no NUL in its field (`src/filer_actions.c:2103-2106`), and -081 filed the same class inside mcman. These two EE-side consumers — one of which **deletes** by the name it read — were covered by neither.
- Fix: Force termination where the data lands (`EntryName[sizeof-1] = '\0'` after each mcGetDir; the memchr check or forced NUL on entry.name), reusing the filer_actions.c:2103 pattern. Two one-line guards.
- Benefit: A crafted/corrupt card or directory can no longer make the installer read past its scan buffers — or delete a path assembled from bytes past a name field.
- Effort: XS · Risk: Near zero · Confidence: READ (call sites, absent guards, in-tree contrast); INFERRED (that the IOP/libmc layer can hand back a fully-filled unterminated field — consistent with -081's IOP-side evidence).

### LAUNCHSTATION-114 / `delete()` and `getFileSize()` recurse with no depth limit — the MAX_RECURSE 16 guard `copy()` has was never given to them / Category 6 (T7-F4)
- Locations: `src/filer_actions.c:1944-2047` (delete; recursion at :1998; per-frame `char dir[MAX_PATH], hdddir[MAX_PATH]` at :1948 ≈ 2,050 B plus locals); `src/filer_actions.c:298-369` (getFileSize; recursion at :324; per-frame MAX_PATH buffer plus iox_stat_t). Contrast `include/filer_shared.h:40` (`MAX_RECURSE 16`) enforced in `src/filer_copy.c:797-798`.
- Problem: Depth is bounded only by total path length: with 1–2-char folder names a 1,025-byte path admits hundreds of levels, each delete level carrying ~2.1 KB of stack. The project's own comment puts the main stack at 128 KB (`src/filer_copy.c:744-745`), so a tree roughly 60 levels deep — easily produced by extracting a nested archive — can exhaust it mid-delete or mid-size-scan, i.e. during a destructive operation.
- Fix: Thread a depth counter through both functions exactly as copy() takes `recurses`, failing with -ELOOP (already used for this at `main_exploit_installer.c:1694-1695`) past MAX_RECURSE; internal wrappers keep the public signatures.
- Benefit: Delete/GetSize on a hostile or pathological tree fail cleanly instead of overflowing the stack halfway through a recursive delete.
- Effort: M · Risk: Low–Med (a legitimate tree deeper than 16 now refuses — but copy() already imposes exactly that refusal on the same trees, so behaviour becomes consistent) · Confidence: READ (absent guard, frame sizes, precedent); INFERRED (exact overflow depth on hardware).

### LAUNCHSTATION-115 / The OSDMBR journal-overlap guard is compiled out of the OSD build — the one build this module exists for / Category 6 (T3-F4)
- Locations: `iop/ps2hdd_osd/hdd_fio.c:1030-1052` (devctlSetOsdMBR): the guard at :1039-1043 (`if (mbrInfo->start < APA_SECTOR_MIN_OSDSTART) return -EINVAL;`) sits inside `#ifdef APA_SUPPORT_GPT`; the constant is defined unconditionally at `libapa.h:34` ("OSD program cannot overlap the journal"); live caller HDIOC_SETOSDMBR at :1124-1126; neither `ps2.yaml:107-110` nor `apa-opt.h:30-37` defines APA_SUPPORT_GPT.
- Problem: In the shipped configuration there is **no** validation of mbrInfo->start/size at all: any values are written into the MBR header, marked dirty and flushed through the journal (:1044-1048). A start below sector 263 points the OSD program area at the APA journal/MBR region itself — the exact overlap the dead guard describes. devctlSetOsdMBR is an OSD-only feature, and the OSD build is the one without the check. (In the dead branch only, the guarded return also leaks clink — no apaCacheFree before return, contrast :1049.)
- Fix: Drop the `#ifdef APA_SUPPORT_GPT`/`#endif` around :1039-1043 and add `apaCacheFree(clink);` before that return. Net: −2 preprocessor lines, +1 line.
- Benefit: HDIOC_SETOSDMBR can no longer be used to aim the OSD area at the driver's own journal.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-116 / The SMB share editor's shared edit buffer is 96 bytes, but the folder field it edits is 128 — long folder names are silently truncated on save / Category 6 (T6-F4)
- Locations: `src/dash_settings.c:688-700` (smbedEditField, `char edit[96]`); field `char folder[128]` at `src/dash_settings.c:398`; caller S_SMBED_FOLDER passes `smbed.folder, sizeof(smbed.folder)`.
- Problem: smbedEditField copies the field into the 96-byte buffer with dashStrCopy (truncating), lets the user edit, then — after a length check against the *target* size (128), which the truncated text always passes — copies it back. A share whose folder path is 96–127 characters (legal today via a hand-edited CNF, preserved by smbedLoadPick) is silently shortened to 95 characters the first time the user opens that field and confirms, even if they changed nothing. All other fields through this helper are ≤80 bytes; the folder is the only one wider than the buffer.
- Fix: `char edit[128]`. One line; the existing check then does its job for every field.
- Benefit: Opening a field in the editor can no longer destroy data the editor claims to support ("At most 127 characters", per its own error text).
- Effort: Trivial · Risk: Minimal · Confidence: READ.

### LAUNCHSTATION-117 / `apaRename` signals fioSema on three of its exits and its caller signals it again — semaphore ownership is double-released on every rename error path / Category 6 (T3-F3)
- Locations: helper `iop/ps2hdd_osd/hdd_fio.c:531-582`: inner SignalSema at :539 (-EEXIST), :548 (-EBUSY), :562 (not-found); the one inner return that does not signal is the system-partition refusal at :556-559; sole caller hddReName at :808-817 does WaitSema → apaRename → SignalSema unconditionally. All spot-verified.
- Problem: Every other helper in this file leaves signalling to its caller; apaRename alone signals inside, on exactly three of five exits. Those three error paths release the semaphore twice for one acquisition (created initial=1, max=1 at :293-296). Whatever the kernel does with an over-max signal, the count no longer means "one holder": a second thread can enter the APA critical section while the first still runs, over the shared cache, journal and slot table. It fires precisely when a rename *fails* — the common case for a duplicate-name attempt.
- Fix: Delete the three inner SignalSema calls at :539, :548, :562. Pure deletion; the caller's signal then balances every exit, including :556-559 which already relies on it.
- Benefit: Restores the file-wide locking convention and mutual exclusion over cache/journal/slots on rename errors.
- Effort: S · Risk: Low (the exits that work today — success, system-partition refusal — already run on caller-signal only) · Confidence: READ.

### LAUNCHSTATION-118 / VMCMAN unit→slot mask `unit & (MCMAN_MAXSLOT - 1)` is `unit & 9`: units 2–7 silently alias onto slots 0/1 and slots 2–7 are unreachable / Category 6, latent (T1-F1)
- Locations: `iop/vmcman/src/mcdev.c:221-223` (VMCMAN branch of mcman_unit2card, spot-verified); constant at `iop/vmcman/src/mcman-internal.h:105-109`. All 11 call sites listed in §1.1(1).
- Problem: MAXSLOT − 1 = 9 (0b1001) is not a contiguous mask. Device units map only to slots {0,1,8,9}: a request for vmc2:–vmc7: is silently served by the card mounted as slot 0 or 1 — including writes, format, remove and mount/umount, since every op derives its slot here — and slots 2–7 of the fatcache/devinfos/cardinfo tables can never be indexed from the device layer at all. Reachability today is blocked only by the EE gates (§1.1(2)); the IOP device itself accepts any unit any EE caller presents.
- Fix: **The -085 fix** — delete the BUILDING_VMCMAN override at `mcman-internal.h:105-109` so VMCMAN uses MAXSLOT 4; the mask becomes `unit & 3`, contiguous and complete, and ≈33,984 B of statics are reclaimed in the same deletion. Do **not** fix the mask in isolation with `% MCMAN_MAXSLOT` while keeping 10.
- Benefit: Removes the silent wrong-card aliasing for units 2–7, makes every remaining slot reachable, and closes -085 — one deletion, both findings.
- Effort: S · Risk: Low–Med (an out-of-tree consumer of slots 8/9 would break; none exists in-tree and the feature has only ever promised vmc0:/vmc1:) · Confidence: READ (arithmetic, call sites, EE gates); INFERRED (out-of-tree consumers).

### LAUNCHSTATION-119 / `GET_XPARAM_DB_SIZE()` always evaluates to 1 — the 292-entry database is searched over exactly one entry / Category 6 (T5-F12)
- Locations: `iop/xparam/include/xparam.h:36` (`#define GET_XPARAM_DB_SIZE() (sizeof(*XPARAMS) / sizeof(XPARAMS[0]))` — *XPARAMS is one element, so numerator == denominator; spot-verified); sole user `iop/xparam/src/xparam.c:55` (CheckGameID loop).
- Problem: The lookup can only ever match XPARAMS[0] ("PAPX_905.17"). Every other game in `database_merged.c` (292 rows, counted; including multi-row games whose consecutive-entry break logic at `xparam.c:61` was written for them) silently gets no XPARAM — on Deckard (PSX-DESR) hardware, the compatibility parameter the module exists to set is simply never set. Pure preprocessor arithmetic; no hardware needed to prove it.
- Fix: Drop the `*` in `xparam.h:36` (one character).
- Benefit: Restores the entire database's function; currently ~291/292 of the data file is dead weight in the ELF.
- Effort: One character · Risk: Low · Confidence: READ.

### LAUNCHSTATION-120 / `readHOST()`'s elf-list branch returns hostcount − 1: the last entry is dropped whenever elflist.txt has no trailing newline / Category 6 (T6-F8)
- Locations: `src/filer.c:1740-1782` (elf-list branch of readHOST); return at :1782.
- Problem: The branch appends one FILEINFO per parsed line, driving the loop with a synthetic 0x0a at end-of-file so the final line is processed whether or not the file ends with a newline — then returns `hostcount - 1`. That −1 only makes sense as cancelling a phantom empty entry for a file that *does* end with a newline; for a file without one it deletes a real entry: a one-entry list returns 0 entries, and an empty list returns −1, which callers read as an error rather than an empty folder. Whether a user's hand-made elflist.txt ends in a newline is not something the code controls.
- Fix: Track the phantom explicitly: skip a parsed line whose host_next is empty before the open/dopen probes, and `return hostcount;`.
- Benefit: The host root listing shows exactly the files the list names, independent of the file's final byte.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-121 / MMCE history newest/prune ordering is lexicographic, but same-second collision names sort backwards / Category 6 (T8-F8)
- Locations: `src/mmce_backup.c:136-164` (newest_history) and prune selection in the same file; naming at `src/mmce_backup.c:486-517` (unique_history_path: base `WLE-%s.%s` at :499, suffixed `WLE-%s-%02d.%s` at :501 — both spot-verified).
- Problem: When two backups land in the same second, later copies are named `-01`, `-02`, … Both "newest" selection and prune-victim selection compare names with plain strcmp. Because '-' (0x2D) sorts before '.' (0x2E), the base file sorts *after* every suffixed sibling: "newest" can return the older base copy, and pruning can delete a genuinely newer suffixed backup first.
- Fix: Compare by (timestamp, numeric suffix) instead of raw string order — or zero-pad so the base name carries `-00`.
- Benefit: Restore-from-newest and history pruning act on the backup the user expects in the same-second case.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-122 / HDL game-title rename caps the title at partition-name length (31) despite a 65-byte title buffer — silent truncation / Category 6 (T8-F13)
- Locations: `src/dash_hdd.c:700-727` (actRenameGame); buffer `char name[64 + 1]` at :702 fed from `p->game.Name`; keyboard call at :711 with max length `MAX_PART_NAME - 1` = 31 (`MAX_PART_NAME` = 32, `include/launchelf.h:202`).
- Problem: The buffer is deliberately sized for a full game title, but the keyboard is opened with the partition-name cap — apparently carried over from actRename (which correctly uses MAX_PART_NAME − 3 for a 33-byte partition buffer). The modal keyboard clamps to max_len on confirm, so renaming a game whose title exceeds 31 characters silently cuts it to 31 even if the user only edited its start.
- Fix: Pass the title's real capacity (`sizeof(name) - 1`) to the keyboard at this call site.
- Benefit: Long HDL titles survive a rename intact.
- Effort: S (one argument) · Risk: Low · Confidence: READ (code path); INFERRED (that fielded titles exceed 31 chars — the 65-byte buffer in this same function is the in-file evidence).

### LAUNCHSTATION-123 / `loadHdlInfoModule()` marks the HDL Info RPC ready unconditionally — module-load and bind results are both discarded / Category 6 (T6-F6)
- Locations: `src/init.c:2563-2573` (spot-verified); sole caller `src/dash_hdd.c:434`.
- Problem: The function runs SifExecModuleBuffer, logs ID/ret only via DPRINTF, executes `ret = Hdl_Info_BindRpc();` and immediately overwrites the outcome with `have_hdl_info = 1;` — ret is never tested. Every sibling loader in this file gates its have_* flag on the load/bind results. Here a failed IRX load or failed RPC bind is recorded as success, never retried, and the HDD screen proceeds to issue HDL Info RPCs against a server that may not exist.
- Fix: Set the flag from the results, mirroring the sibling pattern; leave it 0 on failure so the next entry retries.
- Benefit: The HDD tool fails/retries honestly instead of RPC-ing a dead server; the flag means the same thing as every other have_* flag in the file.
- Effort: ~3 lines · Risk: Low · Confidence: READ.

### LAUNCHSTATION-124 / `filerFreeSpace()`'s XFROM branch reports success without the start/sync checks its own MC branch documents as necessary / Category 6 (T6-F13)
- Locations: `src/filer_browser.c:2480-2487` (XFROM branch); contrast the MC branch at :2469-2478.
- Problem: The MC branch checks `mcGetInfo(...) < 0` before syncing (comment: "not started: mcSync would leave ret unset") and rejects `ret < -1` after sync before trusting type/free_units. The XFROM branch directly below discards xfromGetInfo's return, calls xfromSync and never reads ret at all, then unconditionally computes *out from whatever type/free_units hold (their zero initialisers on failure) and returns 1 — "valid". A failed XFROM query therefore displays a confident, wrong free-space figure (typically 0) instead of none — exactly the failure the MC branch was written to avoid.
- Fix: Mirror the MC branch: return 0 when xfromGetInfo < 0 and when the sync result indicates failure, before computing *out (~3 lines copied from the sibling branch).
- Benefit: The browser header shows no figure rather than a fabricated one when the XFROM query fails.
- Effort: S · Risk: Minimal · Confidence: READ (asymmetry, unchecked values); INFERRED (real-world failure frequency).

### LAUNCHSTATION-125 / `preloadCNFFd`'s seek path has no size cap — the 128 KB cap exists only in the fallback it bypasses / Category 6 (T7-F2)
- Locations: `src/config.c:435-463` (seek path: SEEK_END result used directly as allocation/read size at :446-455); contrast :441,469-503 (fallback stream: `CNF_MAX_SIZE = 128 * 1024`, payload past the cap explicitly rejected at :498-503 as "suspiciously large").
- Problem: The file's own policy is that a CNF over 128 KB is rejected — enforced only when the seeks *fail*. On every normal local device the seek succeeds, so a CNF of any size is allocated whole (`memalign(64, CNF_size + 1)`) and read with a single genRead whose short return is silently accepted as the whole file (any rd > 0 terminates the buffer at rd). A multi-MB "CNF" on USB/HDD either crowds out the 32 MB EE or silently parses a truncated prefix — the behaviour the fallback was written to prevent, applied only on the path that cannot be reached normally.
- Fix: Apply the existing constant in the seek path: `cnf_seek_size > CNF_MAX_SIZE` fails (or falls through to the streamed path, which already rejects). One comparison against a constant the same function defines.
- Benefit: One consistent CNF size policy on all devices; no unbounded file-size-driven allocation at startup.
- Effort: XS · Risk: Low · Confidence: READ.

### LAUNCHSTATION-126 / Hand-rolled JSON string extractor mis-handles escaped backslash, never decodes \uXXXX, and matches keys by first occurrence / Category 6 (T8-F2)
- Locations: `src/cloud_google.c:61-107` (json_string and its callers, e.g. drive_find_child's first-"id" extraction).
- Problem: Three defects in the one parser every Drive response passes through. (1) The closing-quote test `end[-1] != '\\'` treats a string ending in an escaped backslash (`"…\\"`) as unterminated, so the scan runs past the true end into following fields and returns corrupted contents. (2) `\uXXXX` escapes fall into the default copy case and pass through literally, mangling Drive file/folder names or IDs containing them. (3) Key lookup is `strstr(json, "\"key\"")` — first occurrence wins, so a nested object carrying the same key name earlier in the payload shadows the intended field.
- Fix: Track escape state with a proper in-escape flag; decode \uXXXX (at minimum to UTF-8 for the BMP); scope key searches to the object level actually being read.
- Benefit: Drive listings, backup-history lookups and uploads stop silently corrupting names/IDs on legal JSON the server is entitled to send.
- Effort: M · Risk: Low–Med (parser behaviour changes; keep signature and failure contract identical) · Confidence: READ.

### LAUNCHSTATION-127 / Drive request builders ignore snprintf truncation / Category 6 (T8-F3)
**Verification note (2026-11-14, full verification): WITHDRAWN — REFUTED on adversarial re-derivation: `q_escape`/`json_escape` fail (−1) rather than truncate and all three call sites check those returns; with the checked input bounds, worst cases are ≈1,109 < query[1280], ≈3,913 < path[4352], ≈974 < body[1024] and ≈1,018 < body[1280] — truncation is arithmetically unreachable; see Sable-VERIFY2-LAUNCHSTATION.md. Do not act on this finding.**
- Locations: `src/cloud_google.c:718-735` (drive_find_child query), `:1193-1195` (drive_create_folder body), `:1317-1326` (drive_upload_file path/metadata).
- Problem: Query strings, JSON bodies and request paths are built with snprintf whose return is not checked. On truncation the code proceeds as if the request were complete: a truncated Drive query silently searches for the wrong folder; a truncated JSON body is sent malformed. The same file proves the checked pattern is intended — cloudGoogleListBackupHistory (:878-901) and cloud_recovery_path (:968-975) do test the returns.
- Fix: Check the return at these three sites and fail the operation, as the two correct sites already do.
- Benefit: Long folder/file names fail loudly at the client instead of creating/searching the wrong Drive object.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-128 / `Transfer-Encoding: chunked` detection is case-sensitive / Category 6 (T8-F4)
- Locations: `src/cloud_http.c:598,659,1140`.
- Problem: All three transfer-decoding decisions test `strstr(value, "chunked")` case-sensitively. HTTP tokens are case-insensitive; a server answering `Transfer-Encoding: Chunked` is treated as identity-encoded, so chunk framing is delivered to the caller as body bytes and the connection state desyncs from the real message boundaries.
- Fix: Use a case-insensitive token test (the codebase already links stricmp/strnicmp-style helpers) at the three sites.
- Benefit: Interoperability with any conforming server, not just ones that happen to lowercase the token.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-129 / Unbounded atoi/digit accumulation overflows before clamping in theme and IPv4 parsers / Category 6 (T8-F11)
- Locations: `src/dash_themes.c:692` (parseColor decimal path), `src/dash_themes.c:737-742` (parseInt), `src/dash_widgets.c:690-705` (ipParse, accumulation at :697).
- Problem: Same defect class as -104, on local file input: theme files and stored settings are parsed with atoi() / open-coded `v = v*10 + digit` loops over digit runs of unbounded length, with the range clamp applied only after the conversion. A theme.cfg value like `brightness = 99999999999999999999` or a stored IP string with a long digit run overflows signed int (UB) before the clamp can run. The values are clamped eventually — the overflow happens first.
- Fix: Clamp inside the accumulation loop (stop once past the field's maximum) or parse with strtol and clamp the long.
- Benefit: User-supplied theme/config files — an explicitly supported sharing format — cannot invoke UB in the parser.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-130 / `fioInputBreaker` accepts a field exactly as long as its buffer, leaving IDs and passwords unterminated / Category 6 (T3-F9)
- Locations: `iop/ps2hdd_osd/hdd_fio.c:107-127` (both branches test `if (maxout < len) return -EINVAL;` — len == maxout passes — then memcpy with no terminator written); call sites :161 (params->id, APA_IDMAX), :166 (fpwd), :175 (rpwd), inside fioGetInput (:134-210), reached from open/remove/getstat/devctlSwapTemp name parsing.
- Problem: apa_params_t is zeroed at fioGetInput:146, so any shorter field is implicitly terminated; a field of exactly maxout bytes fills the buffer completely and is thereafter handled as a string with no terminator in bounds. Fixed-width consumers (memcmp over APA_IDMAX) are unaffected, which is why this survives; length-sensitive consumers of the same fields read past the field into the adjacent struct member.
- Fix: Change both tests from `maxout < len` to `maxout <= len` (reserve the terminator byte the zeroed struct currently provides only by luck of length).
- Benefit: Every parsed ID/password is provably terminated; the exact-length edge stops being a latent over-read.
- Effort: S (two characters) · Risk: Low · Confidence: READ.

### LAUNCHSTATION-131 / Module start arguments admit 0 and negatives for -o, and no maximum at all for -n / Category 6 (T3-F12)
- Locations: `iop/ps2hdd_osd/hdd.c:265-281`: -o at :270-272 (`if (i - 1 < 32) apaMaxOpen = i;` — a signed test that i = 0 and every negative pass), -n at :279-281 (`if (cacheSize < i) cacheSize = i;` — no upper bound); consumers :325 (allocation), :333 (memset), :335 (apaCacheInit); defaults `hdd_fio.c:38` (apaMaxOpen = 1), `hdd.c:218` (cacheSize = 3).
- Problem: `i - 1 < 32` was plainly meant as "1..32" but admits i <= 0: apaMaxOpen = 0 makes every getFileSlot return -EMFILE; a negative makes the :325 allocation size a huge unsigned product. Unbounded -n is the enabling condition for -111's journal-capacity overflow and sizes two apaAllocMem calls in apaCacheInit with no sanity ceiling. Module arguments come from the embedding loader (trusted local configuration) — a robustness/foot-gun defect, not a remotely reachable one.
- Fix: -o accepts only `1 <= i && i <= 32`; -n accepts only `3 <= i && i <= 126` (126 is the journal capacity from `libapa.h:188` — a cache larger than the journal can never be flushed safely).
- Benefit: A mistyped loader argument can no longer zero out, negate, or over-size the driver's two core tables.
- Effort: S · Risk: Low · Confidence: READ (tests); INFERRED (local-config-only exposure).

### LAUNCHSTATION-132 / `apa_ptable_read_ex` end-of-disk test can wrap, and excludes a partition ending exactly at the disk end / Category 6 (T5-F7)
- Locations: `iop/hdl_info/apa.c:162-163` (`get_u32(&part.start) + get_u32(&part.length) < total_sectors`, 32-bit arithmetic).
- Problem: start + length wraps modulo 2^32, so a header claiming a huge length can pass the "behind end-of-HDD" test (the walk then follows part.next and reads headers at attacker-chosen sectors — bounded reads, but the table is then trusted by hdl.c); conversely `<` (not `<=`) rejects a legitimate final partition ending exactly at total_sectors.
- Fix: `length <= total_sectors - start` after checking `start < total_sectors` (one expression, no addition to wrap).
- Benefit: The one bounds gate in the APA walk actually gates.
- Effort: S · Risk: Low · Confidence: READ (arithmetic); INFERRED (consequence).

### LAUNCHSTATION-133 / Recycled hub buffer keeps the previous hub's bNbrPorts when the new hub descriptor is short / Category 6 (T4-F4)
- Locations: `iop/usbd-ra/src/hub.c:71-81` (allocHubBuffer); copy at `hub.c:714-720` (hubDrvConnect); use at `hub.c:606-625` (hubSetupPorts), gated only by `hub.c:632` (bNbrPorts <= maxPortsPerHub = 4 under MINI_DRIVER).
- Problem: allocHubBuffer resets only desc.bDescriptorType and the two busyFlags — the rest of UsbHub.desc survives from the previous hub that used the slot. hubDrvConnect copies the device-supplied descriptor with len clamped **above** to sizeof(UsbHubDescriptor) but never **below**: a descriptor with bLength < 3 copies the type byte but not bNbrPorts, so the stale previous-hub value flows into hubSetupPorts, which attaches that many child devices. The ≤4 cap bounds the damage to phantom ports of a previous, larger hub — wrong topology, spurious child slots consumed from a 16-device MINI pool.
- Fix: `memset(&res->desc, 0, sizeof(res->desc))` in allocHubBuffer (one line), and/or reject len < 3 in hubDrvConnect. The memset alone closes it for every field.
- Benefit: Hub topology derives only from the attached hub's own descriptor; recycled slots stop leaking state across devices (same stale-slot class as -106, local instance).
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-134 / Isochronous halted-ED cleanup clears the wrong LUT slot, leaving a dangling request / Category 6 (T4-F1)
- Locations: `iop/usbd-ra/src/usbd.c:137-139` (processDoneQueue_IsoTd; spot-verified); contrast the correct twin processDoneQueue_GenTd in the same file.
- Problem: In the halted-endpoint walk, the code fetches `req = hcIsoTdToIoReqLUT[curTd - hcIsoTdBuf]` but then clears `hcIsoTdToIoReqLUT[arg - hcIsoTdBuf]` — arg's slot, already cleared at `usbd.c:109`. curTd's slot keeps pointing at an IoRequest that is simultaneously queued for completion (USB_RC_ABORTED) and freed/callbacked below. A later done-queue pass for that TD index can complete the same request a second time (double callback / use-after-free of the request slot). The generic-TD path in the same file does the identical walk correctly — copy-drift inside one file, not a design choice.
- Fix: Clear `memPool.hcIsoTdToIoReqLUT[curTd - memPool.hcIsoTdBuf]` (one line, mirrors GenTd).
- Benefit: Removes a double-completion path on isochronous endpoints (audio/webcam-class devices) after a halt — the rarest exercised path in the driver, hence the one most likely to still carry it.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-135 / Three MMCE command setters transmit uninitialised stack bytes to the card / Category 6 (T2-F1)
- Locations: `iop/mmceman/src/mmce_cmds.c:109,121` (mmce_cmd_set_card: wrbuf[8], assignments cover [0]-[6], sends sizeof = 8 → wrbuf[7] uninitialised — spot-verified); `:169,180` (set_channel: wrbuf[7], [0]-[5] assigned → wrbuf[6]); `:290,298` (reset: wrbuf[5], [0]-[2] assigned → wrbuf[3],[4]). Transmit path clocks out exactly tx_size bytes (`iop/mmceman/src/mmce_sio2.c:212-214`).
- Problem: Three of the eleven commands send 1–2 stack-residue bytes to the MMCE firmware on every call. The file's own convention proves trailing bytes are meant to be defined: mmce_cmd_ping zero-fills its padding (:20-23), mmce_cmd_set_card_channel writes an explicit 0xFF termination byte (:328), and every other function assigns every byte it sends. Consequences: IOP stack contents leak over SIO2 to an external device, and the wire packet is nondeterministic — no working firmware can be depending on a specific value there, since it cannot know it. (The same pattern exists at `mmce_cmds.c:244` in set_gameid — not filed, per the -054 scope; noted in §09's section file so the -054 fix can absorb it: bounding len there does not by itself define that fifth byte.)
- Fix: Zero-fill the three buffers before the field assignments (`memset(wrbuf, 0, sizeof(wrbuf));` — sysclib.h already included at :2), matching ping's precedent. Do **not** shrink the transfer sizes: packet lengths are the protocol's; only their content is undefined. 3 added lines, 0 wire-format changes.
- Benefit: Deterministic packets; no stack residue leaves the IOP; the three functions join the file's own fully-initialised majority.
- Effort: 3 lines · Risk: Minimal · Confidence: READ (bytes sent uninitialised); INFERRED (firmware's use of them — no protocol document in-tree).

### LAUNCHSTATION-136 / ds34bt DS4 LED/rumble report: buffer zeroed with the DS3 length, then sent at the DS4 length — 29 uninitialised stack bytes transmitted / Category 6 (T5-F10)
- Locations: `iop/ds34bt/iop/ds34bt.c:1208` (`u8 led_buf[PS4_11_REPORT_LEN + 2]` = 79 B), `:1243` (`mips_memset(led_buf, 0, PS3_01_REPORT_LEN + 2)` = 50 B, in the `type == DS4` branch — spot-verified), `:1263` (`size += PS4_11_REPORT_LEN` → 79), `:1273` (HID_command send). Lengths: PS3_01_REPORT_LEN = 0x30, PS4_11_REPORT_LEN = 0x4D (`iop/ds34bt/iop/ds34bt.h:247,251`).
- Problem: **This is the exact defect class the audit has now found three times** — RIPTOPL-066 (ds34bt), POPSLOADER-048 (pademu DS4), and now Launchstation's own ds34bt copy: the DS4 branch inherited the DS3 constant. Bytes 50–78 of the report — everything past the fields written at :1245-1260, which end at index 13 — are uninitialised stack, sent to the controller in every DS4-over-Bluetooth LED/rumble update. The DS4 BT report's trailing bytes include its CRC field region, so a conforming controller may reject the report outright (INFERRED — CRC is computed by neither this function nor HID_command, READ); at minimum the driver transmits nondeterministic stack content. The USB sibling does not have the bug: `ds34usb.c:414` zeroes the whole buffer before either branch.
- Fix: Change :1243 to `PS4_11_REPORT_LEN + 2` (one constant).
- Benefit: Deterministic, spec-shaped DS4 BT output reports; removes a stack-disclosure-to-peripheral.
- Effort: One token · Risk: Low · Confidence: READ.

### LAUNCHSTATION-137 / KELF signing key material is never scrubbed from stack copies / Category 6 (T8-F6)
- Locations: `src/kelf_signer.c:619-732` (parse_key_store), `:734-742` (KelfSignValidateKeys), `:981-1095` (KelfSignElfToMemory); key-store struct at `src/kelf_signer.c:92-107`. (Structure/locations only — no values reproduced, per audit rules. The presence of embedded key material itself is -007, not re-filed.)
- Problem: Key-store material is copied into stack-resident KelfKeyStore instances and derived-key buffers in several functions and never wiped before return. On a shared-console threat model this is minor, but the copies outlive the operation in freed stack frames and can surface in crash dumps or later stack reuse.
- Fix: Zero the local key-store/derived-key buffers on every exit path of the three functions.
- Benefit: Shrinks the lifetime of signing material to the operation itself at near-zero cost.
- Effort: S · Risk: Low · Confidence: READ (absence of any scrubbing in the three functions).

### LAUNCHSTATION-138 / `mcman_iomanx_backing_mount` trusts its slot twice: cleanup runs clear_slot on slots the validator just rejected, and re-mounting overwrites a live fd / Category 6, latent (T1-F2)
- Locations: `iop/vmcman/src/mciomanx_backing.c:60-159` — validator call at :69, unconditional-on-error cleanup at :154-157 (`if (r < 0) mcman_iomanx_backing_clear_slot(slot);` — spot-verified), fd overwrite at :84; clear_slot at :28-40. Sole caller: `mcdev.c:755`.
- Problem: (1) When validate rejects a slot > MAXSLOT (-068's `>` territory), mount still falls into cleanup and calls clear_slot(slot), which dereferences `&mcman_vmc_cardinfo[slot]` out of bounds, conditionally iomanX_close()s whatever integer sits there as an "fd", memsets 24 bytes out of bounds, and calls mcman_probePS2Card(0, slot) into `mcman_devinfos[0][slot]`. The validator's rejection is thus immediately un-done by the error path — contrast umount (:161-176), whose cleanup does **not** call clear_slot on validation failure, and getcardspec/erase/write/read, which return before touching the array. (2) Mount never checks `cardinfo->mounted`: re-mounting an already-mounted slot overwrites cardinfo->fd at :84, leaking the previous iomanX fd permanently (and if the new image then fails validation, clear_slot wipes the slot, so the previously mounted card is lost as well as leaked). Reachability today: none in-tree — the sole caller's slot comes from the -118 mask (always valid), and both EE mount paths unmount before mounting.
- Fix: (1) Run the cleanup clear_slot only when validation passed, mirroring umount's shape; (2) after validation, `if (cardinfo->mounted) mcman_iomanx_backing_clear_slot(slot);` before opening the new image — reusing the existing function, no new state.
- Benefit: A rejected slot can no longer cause an out-of-bounds write in the error path itself, and a re-mount can no longer leak an IOP file descriptor (a scarce, fixed-table resource) per occurrence.
- Effort: S · Risk: Low · Confidence: READ (code paths, umount contrast); INFERRED (latent reachability).

### LAUNCHSTATION-139 / Four entry-reachable `apaCacheAlloc()` results are used without a null check, beside a fifth site in the same files that does check / Category 6 (T3-F7)
- Locations: unchecked: `iop/ps2hdd_osd/apa.c:43-49` (apaSetPartErrorSector — alloc :46, deref :47); `apa.c:455-463` (apaGetFormat — alloc :463, deref via apaReadHeader); `iop/ps2hdd_osd/hdd_fio.c:329-335` (hddFormat — alloc :333, memset(clink->header) :334); `journal.c:73` (counted in -101). Checked, for contrast: `apa.c:51-57` (apaGetPartErrorSector returns -ENOMEM).
- Problem: apaCacheAlloc returns NULL when the free list is empty (`cache.c:201-218`); the cache is small (default 3 entries) and entries are held across the nested apaCacheGetHeader walks these callers run under, so exhaustion is a real state. Each unchecked site turns it into a null dereference in IOP space instead of an error return. Entry reachability: apaSetPartErrorSector from the HIOCSETPARTERROR/HIOCGETPARTERROR ioctls and HDIOC_* error paths; apaGetFormat from module start (`hdd.c:350`); hddFormat is the iomanX format op (`hdd.c:50`).
- Fix: Add the `if (!clink) return -ENOMEM;` check the checked site already uses, at the three sites above.
- Benefit: Cache exhaustion degrades to -ENOMEM instead of crashing the HDD driver.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-140 / usbd `_start` config arguments are parsed without range checks and feed unchecked allocation arithmetic; AllocSysMemory result is memset before any NULL check / Category 6, latent (T4-F5)
- Locations: `iop/usbd-ra/src/usbd.c:295-333` (ParseOptionInput), `:335-390` (_start option table); `iop/usbd-ra/src/hcd.c:378-381` (memSize computation + AllocSysMemory + memset).
- Problem: A chain of three. (1) ParseOptionInput accepts any char where `*p - '0' <= 9` — characters **below** '0' yield a negative digit and are accumulated; the `value*10+digit` accumulation has no overflow guard. (2) The parsed ints overwrite usbConfig fields (maxDevices, maxEndpoints, TD counts, maxStaticDescSize, thread priorities) with no clamping against the compiled MINI maxima. (3) initHcdStructs computes `memSize = (u32)devDescBuf + maxDevices * maxStaticDescSize` in wrapping u32 arithmetic, calls AllocSysMemory, and immediately memsets it — memBuf is never checked for NULL (its sibling initHubDriver in hub.c does check). Mitigation, READ: Launchstation itself loads the module with argc=0 (`src/init.c:1566,2092,2096`), so today this is reachable only by another loader/homebrew passing arguments — a latent trap in a module whose whole purpose is to be loaded by other software.
- Fix: Clamp each parsed value to [1, compiled-in maximum] in _start after parsing; reject negative parse results in ParseOptionInput; return an error from initHcdStructs if memBuf == NULL, mirroring initHubDriver.
- Benefit: Module initialisation fails closed instead of corrupting IOP memory when handed bad arguments; costs a few comparisons at load time only.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-141 / ps2ips `do_send`/`do_sendto` trust the EE-supplied `malign` count: negative buffer offset and length underflow / Category 6 (T5-F8)
- Locations: `iop/ps2ips_rpc/ps2ips.c:332-348` (do_send) and `:365-381` (do_sendto); field is `s32 malign` with a 64-byte inline `malign_buff[64]` (`ps2ip_rpc.h:99-101`).
- Problem: Nothing validates malign. With malign > 64: `s_offset = 64 - malign` goes negative and `memcpy(lwip_buffer + s_offset, malign_buff, malign)` writes **before** the static lwip_buffer and over-reads the 64-byte source; `sceSifGetOtherData(..., sendlen - malign, ...)` underflows when malign > sendlen. With malign < 0 the same expressions invert. The legitimate EE client only ever sends a 0–63 alignment remainder (INFERRED from the design comment at `ps2ips.c:20-22`), so this is a malformed/buggy-client hole, not a network one — but the sibling do_setsockopt in the same file got exactly this validation as a documented local change, which makes the omission here a proven-inconsistent posture.
- Fix: Reject `malign < 0 || malign > 64 || malign > sendlen` with `ptr[0] = -1` at the top of both functions, mirroring :526-529.
- Benefit: Closes an IOP static-buffer under-write from one bad RPC packet.
- Effort: S · Risk: Low · Confidence: READ (arithmetic); INFERRED (client behaviour).

### LAUNCHSTATION-142 / ps2ips `do_recv`/`do_recvfrom` never reject a negative EE-supplied `length` / Category 6 (T5-F9)
- Locations: `iop/ps2ips_rpc/ps2ips.c:136-153` and `:236-253` (`s_recv_pkt.length` is s32, `ps2ip_rpc.h:107`; `recvlen = MIN(BUFF_SIZE, length)` keeps a negative value; `srest = length` when length <= 64; `s_offset = 64 - srest` then exceeds the 64-byte headroom, lwip_buffer being BUFF_SIZE + 64 at :51).
- Problem: A negative length flows into recv/recvfrom as its length argument and shifts the receive offset past the reserved headroom; any positive return then computes the sbuffer/ebuffer copies from offsets the 1,088-byte buffer was not sized for.
- Fix: Early-return the zero-payload metadata path (a documented local change already built it: memset rests + send_rest) with ret = -1 when length < 0.
- Benefit: Same class as -141, closed with the pattern the file already has.
- Effort: S · Risk: Low · Confidence: READ (data flow); INFERRED (exact lwIP behaviour for a negative length — not read in this pass).

### LAUNCHSTATION-143 / hdl_info rename path: EE-supplied NewName is strcpy'd with no termination guarantee, and the RPC handler never looks at Size / Category 6 (T5-F5)
- Locations: `iop/hdl_info/hdd.c:259,272-274` (`Packet->NewName` is `char[64]` in Rpc_Packet_Send_Rename, `hdd.h`; `strcpy(game->name, Packet->NewName)` into `char[65]`); dispatcher `iop/hdl_info/main.c:60-73` (rpcCommandHandler ignores Size, casts Data unconditionally for commands 4/5).
- Problem: The EE client (`src/hdl_rpc.c:74-76`) strcpy's into the same 64-byte field, so a ≥64-char new name arrives unterminated; the IOP strcpy then reads past the RPC buffer field and writes past game->name. The in-tree EE caller is the only client, so this needs a long name in the loader UI, not a network attacker — filed at that weight.
- Fix: strncpy + force `game->name[64] = 0` (or reject Size smaller than the packet struct in main.c and bound the copy to 64).
- Benefit: One long game name stops corrupting the heap games list.
- Effort: S · Risk: Low · Confidence: READ (IOP side); INFERRED (reachability via UI name-length limits — but see -144/-122 for the EE-side caps).

### LAUNCHSTATION-144 / HDL RPC wrappers strcpy into 33- and 64-byte packet fields with no length check — safety exists only in today's callers / Category 6, latent (T9-F4)
- Locations: `src/hdl_rpc.c:60` (strcpy into Partition_Name, `char[32 + 1]` at `src/hdl_rpc.c:11`); `src/hdl_rpc.c:77-78` (strcpy into OldName[64]/NewName[64]).
- Problem: Neither wrapper validates its input. Current callers happen to be safe — partition names are capped by MAX_PART_NAME = 32 and the rename flow caps the new title via the modal keyboard in actRenameGame (the cap that is itself -122) — but the RPC layer is the last code before a fixed DMA packet (Rpc_Buffer[1024], `src/hdl_rpc.c:7`), and any future or alternate caller with a longer string corrupts adjacent packet/RPC state silently. This is the EE-side half of -143's surface.
- Fix: Reject (return -1) when strlen meets/exceeds the field size, in the two wrappers only.
- Benefit: The invariant moves to the one place that owns the field sizes; callers keep working unchanged.
- Effort: Trivial · Risk: Low · Confidence: READ.

### LAUNCHSTATION-145 / Editor navigation rules read one row-width entry before/after the cursor's row, and past the last row after the Home/End scans / Category 6 (T9-F2)
- Locations: `src/editor_rules.c:66-67` (`Editor_nRowsWidth[i + 1]`, PushRows-up branch); `:89-92` (`Editor_nRowsWidth[i - 1]`, PushRows-down branch); `:42,65,87` (`Editor_nRowsWidth[i]` after a scan loop that can exit with `i == Editor_nRowsNum`).
- Problem: In the PushRows-up branch the scan can leave i on the last row, making i + 1 one past the last valid entry (at the -098 array limit, one past the array). In the PushRows-down branch the scan can leave i == 0, making i − 1 == −1. The Home/End scans post-decrement with `Editor_nRowsWidth[i]` where i can equal the row count at end-of-text. All are reads of adjacent BSS feeding cursor arithmetic — wrong cursor positions at best.
- Fix: Clamp i to [0, Editor_nRowsNum − 1] once after each scan loop, and guard the i ± 1 reads with the same bound.
- Benefit: Cursor math stops depending on out-of-range BSS values at file start/end — the two positions every editing session visits.
- Effort: S · Risk: Low · Confidence: INFERRED (indexing READ; exact reachability of each edge depends on cursor/scroll state combinations not executed here).

### LAUNCHSTATION-146 / SETTING allocations are dereferenced without a NULL check, in both places config.c creates one / Category 6 (T7-F7)
- Locations: `src/config.c:797-801` (initConfig: malloc at :799, first sprintf into setting->Misc at :801); `src/config.c:1511-1513` (config: malloc at :1512, struct copy `*setting = *tmpsetting` at :1513).
- Problem: SETTING is a large struct (~25 KB: seventeen 1,025-byte paths plus titles). On a 32 MB machine whose heap routinely holds ~900 KB folder listings, either allocation can fail; both callers dereference unconditionally, turning OOM into a NULL write during startup config load or on entering the config screen — instead of the fallback-to-defaults behaviour a failed load otherwise gets. initConfig additionally frees the previous setting at :797-798 *before* allocating, so a failure also loses the old settings.
- Fix: Check both results: in initConfig, allocate first and free the old object only on success; in config(), abort the screen (restoring tmpsetting) when the copy cannot be allocated. No new machinery.
- Benefit: Memory exhaustion during config load/edit degrades instead of crashing.
- Effort: S · Risk: Low · Confidence: READ (absent checks, immediate derefs); INFERRED (practical reachability of allocation failure).

### LAUNCHSTATION-147 / `des_cbc_crypt` performs fixed 8-byte block copies with no size validation / Category 6, latent (T8-F7)
- Locations: `src/kelf_signer.c:396-423` (des_cbc_crypt); validating wrapper at `:436-439` (tdes_cbc_crypt).
- Problem: des_cbc_crypt loops in 8-byte steps issuing fixed 8-byte memcpys with no check that size is a multiple of 8; a non-multiple size would read and write past the caller's buffers on the final partial block. Today the defect is latent: the wrapper validates, and all direct callers pass 8 or 32. It is one future caller away from a live overflow in the signing path.
- Fix: Route all callers through tdes_cbc_crypt and make des_cbc_crypt static-internal with an early return on bad size (LESS-preferred form: delete the unvalidated path).
- Benefit: Removes a loaded foot-gun from the most sensitive file in the tree.
- Effort: S · Risk: Low · Confidence: INFERRED (latent — no current caller triggers it).

### LAUNCHSTATION-148 / Scene-override stack loses one level when nesting exceeds depth 8 / Category 6, latent (T8-F10)
- Locations: `src/dash_main.c:1159-1175` (dashPushSceneOverride / dashPopSceneOverride).
- Problem: On push, scene_override_depth is incremented even when the store is skipped because the fixed stack (depth 8) is full. The matching pop then restores NULL instead of the 8th override, and every subsequent pop is off by one — the dashboard can lose its scene (black/no-render frame) after deeply nested full-screen tools exit. Current nesting is ~2, so this needs 9 nested overrides to bite.
- Fix: Only increment depth when the entry was actually stored (or refuse the push), keeping push/pop symmetric.
- Benefit: Removes a latent scene-loss desync ahead of any deeper tool nesting.
- Effort: S · Risk: Low · Confidence: INFERRED (latent at current call depth).

### LAUNCHSTATION-149 / `dashPngDecode` dereferences its out-parameters without the NULL checks its JPEG sibling performs / Category 6, minor (T9-F8)
- Locations: `src/dash_png.c` (dashPngDecode: `*out = NULL;` as the first statement; *out_w/*out_h written at the end; no NULL test on any of the three); contrast `src/dash_jpeg.c:56-59` (dashJpegDecode returns -1 when out/out_w/out_h is NULL). Current in-tree caller dashPngLoadFile (same file) passes valid pointers.
- Problem: Latent only: the public decode entry point crashes on a NULL out-parameter instead of failing, inconsistent with the sibling API it is documented alongside.
- Fix: Add the same three-way NULL check the JPEG decoder already has (three lines, copied shape).
- Benefit: The two bounded image decoders present one contract.
- Effort: Trivial · Risk: Low · Confidence: READ.

### LAUNCHSTATION-150 / `smap_transmit` retries a full/dead TX queue forever with no stop or timeout / Category 6 (T5-F2)
- Locations: `iop/udpfs_smap/src/xfer.c:249-271` (`while (1)` around HandleTxReqs, 100 µs delay, no iteration bound, no NetDevStopFlag test — contrast smap.c's InitPHY loops, which all test NetDevStopFlag).
- Problem: If BDs never drain (link lost mid-transfer, EMAC stalled), the calling thread spins forever and the packet's owner never gets an error.
- Fix: Bound the loop by the same 2 s budget the function already uses for init waits (xfer.c:252-256) and return non-zero on expiry.
- Benefit: A wedged TX path becomes a recoverable error instead of a hung loader thread.
- Effort: S · Risk: Low–Med (a genuinely slow drain >2 s would now fail; 100 Mb/s TX FIFO drain is orders of magnitude faster) · Confidence: READ; INFERRED (trigger frequency).

### LAUNCHSTATION-151 / `ds34bt_reset` waits forever for pads to leave RUNNING state / Category 6 (T5-F11)
- Locations: `iop/ds34bt/iop/ds34bt.c`, reset function in the final third of the file (`while (1)` + DelayThread(500) polling DS34BT_STATE_RUNNING, no counter/timeout; followed by a fixed DelayThread(1000000)).
- Problem: If a pad never clears RUNNING (stuck HCI state, clone pad ignoring disconnect), the RPC thread that services every DS34BT call hangs permanently; the loader's reset call (`src/init.c:2157`) never returns. The USB sibling's reset is a bounded loop over pads calling usb_release — no wait at all.
- Fix: Add a retry bound (e.g. 2,000 polls ≈ the existing 1 s tail delay's order) and proceed regardless.
- Benefit: Reset degrades instead of deadlocking the DS34 RPC server.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-152 / usbd device-descriptor short read retries forever with no counter / Category 5 (T4-F3)
- Locations: `iop/usbd-ra/src/hub.c:451-454` (requestDevDescrptCb); re-arm at `hub.c:465-475`.
- Problem: If the device answers USB_RC_OK but with transferedBytes < sizeof(UsbDeviceDescriptor), the callback unconditionally re-issues the identical full-descriptor request. A device that persistently short-reads loops control transfers indefinitely: never killed, never connected, generating bus traffic for as long as it is plugged in. Every other retry in this driver is bounded; this one has no bound at all.
- Fix: Count retries on the Device and killDevice after a small fixed number (3 matches the pccf CIS retry convention in-tree).
- Benefit: A stuck/broken device stops consuming the control endpoint and bus bandwidth forever.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-153 / `waitUntilTimer()` busy-spins the EE for up to 6 s on the UI thread while waiting for a clipboard source device / Category 5 (T6-F11)
- Locations: `src/filer_browser.c:120-124` (waitUntilTimer), used by waitForClipboardSourceDevice at `:240-265` (deadline +6000 ms, 1000 ms slices).
- Problem: waitUntilTimer is an empty `while (Timer() < end_time) {}` spin. During a paste whose source device needs re-preparation after an IOP reset, the browser sits in this spin for up to six seconds, redrawing the "Pasting" message only between slices — the EE burns at full rate and the UI thread does nothing else. The codebase already has the non-spinning primitives for exactly this shape of wait and uses them in sibling code (DashPause in filer.c's waitUsbDiscoverySettle; DelayThread in init.c's bounded-stat polling). This instance is not among -014's cited sites.
- Fix: Replace the empty spin with the same sleep the sibling waits use (DelayThread/DashPause in ≤100 ms slices), keeping the one-second re-check cadence unchanged.
- Benefit: Same wait, same cadence, without pinning the EE for up to 6 s per paste-after-reset.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-154 / pccf `pio_profile` does up to 18 validation reads at start, and any failure — including "known model, card smaller than 4096 sectors" — triggers a full power-cycle restart of the whole driver / Category 5 (T4-F10)
- Locations: `iop/pccf/pccf_pio_profile.inc:12-24` (`if (ata->sectors<4096) return -1;` after the model match; 3 samples × (2 baseline + 1 tuned) reads of 8 sectors); restart machinery `iop/pccf/pccf_hw.c:238-244` (pio_fallback_restart: pccf_hw_stop + 100 ms delay + recursive pccf_hw_start).
- Problem: For the two recognised models, start-up performs 9 separate 4 KiB validation reads across three LBAs before the card can serve its first real request. Worse, the failure path is indiscriminate: a recognised-model card with <4096 sectors (<2 MiB) returns −1 at the size check — before any timing conclusion — and pccf_hw_start treats every pio_profile failure identically: full stop, power-cycle, and a complete second start (≈522 ms of power sequencing, CIS parse with up to 5 s of retries, CF probe, IDENTIFY), at the end of which the recursive call skips profiling anyway via pio_fallback_restart. The small-card case therefore always pays two full starts to arrive at the conservative profile it could have taken directly.
- Fix: Return 0 (keep conservative PIO) instead of −1 for the sectors < 4096 case — a small card is not a tuning failure — and reserve the restart path for an actual tuned-vs-baseline data mismatch.
- Benefit: Eliminates a guaranteed double-start (seconds of delay) for small recognised cards; start-up failure handling distinguishes "cannot tune" from "tuning is unsafe".
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-155 / `pcdiag_cf_probe`'s sampling phase is diagnostic dead work in the production driver: results go to a no-op trace, the return is unconditional, and it costs ~700 ms on every start / Category 1/5 (T4-F8)
- Locations: `iop/pccf/pcdiag_cf.c:40-70` (3 samples × 2 selector spaces × read8/read16 sweeps, wait_ms(500) + 2× wait_ms(100)); production caller `iop/pccf/pccf_hw.c` (pccf_hw_start) passes quiet_trace, the variadic sink in the same file that discards every argument; only the candidate/COR gates at the top of pcdiag_cf.c influence the outcome.
- Problem: The function's own header calls it "DIAG 4 … interpretation awaits hardware comparison". In the shipped module every sampled byte/word is formatted for a trace callback that is a no-op in production, nothing compares or acts on the samples, and the function returns 0 unconditionally once the COR gate passes. What production actually needs from this file is only the gate: pcdiag_cis_cf_candidate + COR index/soft-reset check. The remaining ~30 lines are an in-vivo diagnostic that also inserts a fixed ~700 ms of delay into every pccf_hw_start — and again into the -154 fallback restart.
- Fix: Reduce pcdiag_cf_probe to its two gates (candidate + COR) and delete the sampling loops/waits from the production path; the full diagnostic already lives in the upstream repo's DIAG branches per the file header's own provenance note.
- Benefit: −~700 ms from every CF start (twice on a profile-fallback start), and removal of ~30 lines whose only output is discarded.
- Effort: S · Risk: Low (no return value or register write depends on the deleted phase — it performs reads only) · Confidence: READ.

### LAUNCHSTATION-156 / Passwords are DES-encrypted on input and then never verified in this build — the DES run on every open/remove/getstat name parse is dead work / Category 5 (T3-F11)
- Locations: `iop/ps2hdd_osd/password.c:25-35` (apaPassCmp returns 0 unconditionally when APA_ENABLE_PASSWORDS is undefined, comment: "Passwords are not supported, hence this check should always pass"); `apa-opt.h:30-37` (OSD branch does not define it — deliberate); callers that still pay: `hdd_fio.c:169-179` (fioGetInput runs apaEncryptPassword over both password fields on every parse); verification-shaped call sites that therefore always pass: `hdd_fio.c:476-477` (apaOpen), :504 (apaRemove), :747 (hddGetStat), :1011 (devctlSwapTemp).
- Problem: This is **not** a "passwords are broken" finding: protected-partition access without passwords is the documented reason this OSD build exists (README.md:5-8, apa-opt.h:18-23), and the stored values still matter for interop with stock drivers, so hddFormat's MBR encryption and the DES implementation in password.c:40-522 must stay. What is pure waste is the input side: every partition-name parse DES-encrypts (a ~480-line bit-permutation implementation, run in IOP software) two fields whose encrypted forms are then compared by a function compiled to `return 0`. It costs time on every open and, worse, makes the code read as if a check is happening.
- Fix: Guard the two apaEncryptPassword calls in fioGetInput with `#ifdef APA_ENABLE_PASSWORDS`. Non-OSD builds of the same source keep today's behaviour byte-for-byte.
- Benefit: Removes a full DES run (×2) from every name parse in the shipped driver and makes the no-verification design honest at the call site.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-157 / `HandleRxIntr` never counts a packet, so the link-check traffic reset is dead / Category 1 (T5-F1)
- Locations: `iop/udpfs_smap/src/xfer.c:158,165,199` (NumPacketsReceived initialised 0, never incremented, returned); consumer `iop/udpfs_smap/src/smap.c:431,443-450` (`ResetCounterFlag = HandleRxIntr(...)`; `if (ResetCounterFlag) counter = 3;` else the 1 s link-check counter decrements and CheckLinkStatus runs).
- Problem: The return contract ("packets received this interrupt") is always 0, so received traffic never resets the idle counter: InitPHY can be re-entered via CheckLinkStatus (smap.c:367-373) after ~3 s regardless of traffic. That is the code as written (READ); whether it misbehaves on hardware is INFERRED — a link that still reads LINK in CheckLinkStatus costs only a PHY read, so impact may be nil, but the counter/reset mechanism is provably inert.
- Fix: Either increment NumPacketsReceived once per processed BD in the loop at xfer.c:169-197 (one line), or — LESS — delete the variable, the return plumbing and the ResetCounterFlag branch and document that link check is purely periodic.
- Benefit: Either the intended traffic-aware behaviour, or ~6 dead lines removed.
- Effort: Trivial · Risk: Low · Confidence: READ (mechanism); INFERRED (impact).

### LAUNCHSTATION-158 / Two private 2,049-entry sceMcTblGetDir statics (readMC and readXFROM) duplicate ~120 KB of .bss each / Category 4 (T6-F9)
- Locations: `src/filer.c:480` and `src/filer.c:544` (identical declarations `static sceMcTblGetDir mcDir[MAX_ENTRY + 1]`, both spot-verified).
- Problem: The two readers are never active at once: directory listing is a single dispatch (listDir picks exactly one reader per path), and both arrays are pure scratch for one mcGetDir/xfromGetDir call. Keeping two means ~120 KB of the EE's 32 MB is permanently reserved for a second copy of a buffer that is idle whenever the first is in use. (Byte size INFERRED: a sceMcTblGetDir is ~60 B by the field arithmetic used for -036's 384-byte FILEINFO, so each array is ≈120 KB.)
- Fix: Hoist one file-scope `static sceMcTblGetDir mcDir[MAX_ENTRY + 1]` and use it from both functions (delete one declaration).
- Benefit: ≈120 KB of .bss returned (INFERRED size) with no behavioural change.
- Effort: Trivial · Risk: Low (no path lists an MC and an XFROM folder concurrently; the shared clipboard/FILEINFO design already assumes serial listing) · Confidence: READ (duplication, serial use); INFERRED (exact saving).

### LAUNCHSTATION-159 / pccf PIO tuning profile reserves 8 KiB of static IOP memory for a one-shot, two-model check / Category 4 (T4-F9)
- Locations: `iop/pccf/pccf_pio_profile.inc:3` (`static union { u32 words[1024]; unsigned char bytes[4096]; } profile_ref, profile_check;` = 2 × 4,096 B = 8,192 B .bss, included into `pccf_hw.c:172`).
- Problem: Two 4 KiB scratch buffers live in the resident IRX for the module's entire lifetime to serve a start-up-only comparison that runs only when the card's IDENTIFY model string exactly matches one of two strings (`TS16GCF133`, `INIC-2051 CompactFlash Card`, pccf_pio_profile.inc:7-8); for every other card the buffers are never touched at all. For scale: the whole usbd-ra static descriptor pool for 16 devices is the same 8 KiB.
- Fix: Shrink the validation window (profile against 8 sectors read into one 4 KiB buffer compared sector-wise on a second pass — halving it to 4 KiB), or move the pair into a start-up-only allocation freed after pio_profile returns. The one-buffer variant is LESS.
- Benefit: −4 to −8 KiB permanent IOP .bss from a module whose feature is not yet user-facing (VENDORED.md states no Files/boot caller yet).
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-160 / The local MINI_DRIVER choice caps total descriptors at 512 bytes/device and the overflow path drops the device with no diagnostics in release builds / Category 6/8 (T4-F6)
- Locations: local delta `ps2.yaml:58-62` (MINI_DRIVER); `iop/usbd-ra/src/usbd.c` MINI usbConfig (maxStaticDescSize = 0x200, maxDevices = 0x10, maxHubDevices = 4, maxPortsPerHub = 4); drop site `hub.c:427-429`.
- Problem: MINI_DRIVER is Launchstation's own selection. Any device whose device+config descriptor set exceeds 512 bytes total — routine for multi-interface composite devices — hits the -106 too-large branch and is ignored; dbg_printf compiles to nothing without DEBUG (usbdpriv.h), so on release builds the device simply never appears, with no log and no user-visible reason. Distinct from -106's parsing bug: even a perfectly conforming large-descriptor device is dropped. Whether any target device class actually exceeds 512 bytes is INFERRED; the cap and the silence are READ.
- Fix: No code growth required to surface it: keep the cap, but make the drop observable (one unconditional printf on that branch, as other fatal paths in hcd.c already use printf). Raising maxStaticDescSize is a separate RAM-costing decision (16 devices × +512 B = +8 KiB IOP) for the maintainer, not recommended here by default.
- Benefit: An invisible compatibility cliff becomes diagnosable; support reports can distinguish "not recognised" from "descriptors too large for the mini driver".
- Effort: S · Risk: Low · Confidence: READ (cap + silence); INFERRED (real-world hit rate).

### LAUNCHSTATION-161 / `mmce_cmd_get_status`, `mmce_cmd_get_card` and `mmce_cmd_get_channel` are one function pasted three times / Category 2 (T2-F2)
- Locations: `iop/mmceman/src/mmce_cmds.c:47-74`, `:76-103`, `:136-163` — 28 lines each, byte-identical except the command enum at :55/:84/:144 and the function name (including the reply decode `rdbuf[0x3] << 8 | rdbuf[0x4]`).
- Problem: Three copies of the same send-a-3-byte-command / return-a-u16-reply logic, with three distinct devctl callers (`iop/mmceman/src/mmce_fs.c:1333,1337,1350`). Any fix to the shared logic (reply validation, timeout choice, the -135 class of buffer bug) must currently be made three times and can silently be made twice. (The stale `//TODO:` at `mmce_cmds.c:46` above the fully-implemented, twice-called get_status can go in the same edit.)
- Fix: One static helper (e.g. `static int mmce_cmd_get_u16(u8 cmd)`) plus three one-line wrappers keeping the existing public names/signatures in `iop/mmceman/include/mmce_cmds.h:53,54,56` — callers unchanged. Net ≈ −50 lines. Do not generalise further into the setter boilerplate in the same change.
- Benefit: One place to fix/validate the u16-reply command shape; ~50 fewer lines in a 344-line file.
- Effort: S · Risk: Minimal · Confidence: READ (bodies compared line by line).

### LAUNCHSTATION-162 / The classic browser's New Icon flow is a second, unchecked copy of `filerCreateFolderIcon()` — including a statement its own comment calls dummy code / Category 2 (T6-F12)
- Locations: classic copy `src/filer_browser.c:1460-1501` (NEWICON branch of browseFilePath); checked implementation `src/filer_browser.c:2420-2458` (filerCreateFolderIcon); dummy statement at `src/filer_browser.c:1499`.
- Problem: The same file contains two implementations of "write icon.sys + icon.icn into a folder, asking before overwrite". The dashboard-facing one checks genFixPath, checks make_iconsys/make_icon results and reports errors. The classic browser's inline copy checks neither maker's return — a failed icon write is silently presented as done — and duplicates the genFixPath/strcat/open/confirm/remove sequence twice within itself (once per file). It also contains, verbatim, `strcpy(tmp, tmp1);  //Dummy code to make 'goto DoneIcon' legal for gcc` — a statement that exists only to satisfy a label-placement quirk and copies a full path buffer for nothing.
- Fix: Extract the write-one-icon-file half (fix-path, exists-confirm, remove, make, check) into one static helper used by both the classic branch and filerCreateFolderIcon; the classic branch keeps its two keyboard prompts around the calls. Drop the dummy statement by giving DoneIcon: a following real statement or a scoped block.
- Benefit: One checked implementation; the classic flow gains the error reporting the dashboard flow already has; ~35 lines of duplicate sequence removed.
- Effort: M · Risk: Med-Low (prompts and ordering must stay identical) · Confidence: READ.

### LAUNCHSTATION-163 / Three near-identical recursive directory walks in `mmce_backup.c` / Category 2 (T8-F9)
- Locations: `src/mmce_backup.c:686-717` (count_candidates), `:742-786` (process_tree), `:920-963` (scan_tree).
- Problem: The same skeleton — dread loop, dot-entry skip, path join, recursing into subdirectories — is implemented three times, differing only in the leaf action (count / copy / fingerprint). Any fix to traversal behaviour (depth guard, error handling, skip rules) must currently be made three times and can silently diverge.
- Fix: One walker taking a leaf callback; delete the other two skeletons.
- Benefit: Net deletion; traversal fixes land once.
- Effort: M · Risk: Low–Med (the three walks run at different backup phases; keep call order identical) · Confidence: READ.

### LAUNCHSTATION-164 / Theme-folder read and load are the same ~35-line routine twice; write and export repeat the mkdir preamble / Category 2 (T8-F12)
- Locations: `src/dash_themes.c:1046-1079` (dashThemeFolderRead) vs `:1122-1159` (dashThemeFolderLoad); preamble at `:1108-1119` (dashThemeFolderWrite) repeated at `:1171-1182` (dashThemeFolderExport).
- Problem: Read and Load duplicate the entire root-iteration, 16 KiB text load, preset-seed, KV-parse, name-fallback, relative-path resolution and image-fallback sequence, differing only in destination (caller's DashTheme vs the live theme + commit). The folder-create preamble is likewise copied between Write and Export. Any change to theme-folder semantics must be mirrored by hand.
- Fix: Extract one `loadThemeFolder(folder, DashTheme*)`; Load becomes that plus assignment/commit, Read becomes that alone. Same for the mkdir preamble.
- Benefit: Net deletion of ~40 lines; one place to fix theme-folder behaviour.
- Effort: S–M · Risk: Low (Load's worker-safety note — Read is called off-thread — must be preserved in the shared helper's contract) · Confidence: READ.

### LAUNCHSTATION-165 / `getExternalFilePath()` re-implements `genFixPath()`'s HDD/DVR partition transform in init.c / Category 2 (T6-F7)
- Locations: `src/init.c:1412-1466` (getExternalFilePath; HDD branch :1428-1437, DVRP branch :1440-1458); canonical implementation `src/filer.c:1029-1073` (inside genFixPath).
- Problem: Both functions perform the same non-trivial transform for `hddN:/partition/rest`: split the partition name off, build the party string, build the pfs0:/dvr_pfs0: file path from the remainder, mount the partition, and patch the pfs unit digit (including the DVRP __xdata → unit-1 special case via getDVRPPartyMountIndex). They are written differently enough that a fix to the transform must be discovered and mirrored by hand; init.c's version additionally uses unbounded sprintf into MAX_PATH buffers where filer.c's uses strcpy/sprintf of the same shape — neither is safer, they are simply two copies.
- Fix: Have getExternalFilePath delegate its HDD/DVRP branches to genFixPath (it already delegates its fallback branch to it at :1464), deleting the local transform (~30 lines). If the pre-mount side effects differ in a way that matters, keep a single shared helper for the party/pfs string split and call it from both.
- Benefit: One implementation of the HDD path transform — the transform every external-module and keyboard-map load depends on.
- Effort: M · Risk: Med (path canonicalisation is load-bearing for every device; do it with the filer's existing tests in hand) · Confidence: READ (duplication of logic; the exact safe merge shape is a maintainer call).

### LAUNCHSTATION-166 / `resetScreen()` and `resetDisplayThemeDefaults()` duplicate the same 15-line classic reset / Category 2 (T6-F2)
- Locations: `src/dash_settings.c:2767-2786` (resetScreen), `src/dash_settings.c:2788-2810` (resetDisplayThemeDefaults).
- Problem: Both functions write the identical block: `color[COLOR_BACKGR..COLOR_GRAPH4] = DEF_COLOR1..8`, TV_mode = TV_mode_AUTO, screen_x = screen_y = 0, Menu_Frame = DEF_MENU_FRAME, Popup_Opaque = DEF_POPUP_OPAQUE, updateScreenMode(). They differ only in their confirm prompt and in the second function's three extra dashboard lines. Any future change to the classic defaults must be made twice, and the two "reset" rows in the UI can silently diverge.
- Fix: Extract the shared block into one `resetClassicScreenFields(void)` helper (~15 lines, called twice); both functions keep their own prompt and extra work. Net ≈ −13 lines — the helper replaces two copies with one copy plus two call sites.
- Benefit: One definition of "classic screen defaults".
- Effort: S · Risk: Low (block verified side-effect-identical line by line) · Confidence: READ.

### LAUNCHSTATION-167 / PNG decoder open-codes its flush-row block three times; the JPEG decoder beside it already has it as a function / Category 2 (T9-F7)
- Locations: `src/dash_png.c` — the accumulator-flush block appears in the interlaced path, the streaming path, and the final flush (three near-identical copies: per-column divide by acc_n, alpha halving, `dst[cur_dy * dw + x] = …`, two memsets); contrast `src/dash_jpeg.c:30-41` (flushRow, one function, called twice).
- Problem: The pixel-packing expression — including the GS alpha `(a + 1) >> 1` quirk — lives in three places in one file. A fix to alpha or channel order must be replicated perfectly or the interlaced and non-interlaced paths diverge visually.
- Fix: Extract the block as a static flushRow-equivalent in dash_png.c (mirroring the JPEG file's existing shape) and call it three times. Net lines decrease.
- Benefit: One definition of dashboard PNG pixel packing; the two decoders become structurally parallel.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-168 / BCD conversion is copy-pasted at least four times, twice inside one file / Category 2 (T9-F6)
- Locations: `src/main_title.c:46` and `src/main_history.c:22` (byte-identical bcdToInt bodies); `src/dash_clock_sync.c:251,256` (toBcd/fromBcd) and again in the same file's manual-clock section (manualToBcd/manualFromBcd, identical bodies, ~150 lines later).
- Problem: Four implementations of a two-line, spec-defined conversion (PS2 RTC BCD). The in-file duplication in dash_clock_sync.c is the telling part: the manual-entry half was written as if the SNTP half's helpers did not exist. Any fix to BCD edge handling (e.g. invalid nibbles from a corrupt RTC read) must currently be made four times.
- Fix: Delete the manual* pair and call the file-local toBcd/fromBcd in dash_clock_sync.c; leave the two cross-file bcdToInt copies or fold them into the existing clock helpers — the in-file half is the net win with zero linkage change.
- Benefit: −2 functions; one BCD definition per file at most.
- Effort: Trivial · Risk: Low (bodies verified identical) · Confidence: READ.

### LAUNCHSTATION-169 / The same `pccf` IOP target is defined twice — root `ps2.yaml` and `iop/pccf/ps2.yaml` — with identical sources, defines, flags and headers / Category 2/7 (T4-F11)
- Locations: `ps2.yaml:37-44`; `iop/pccf/ps2.yaml:1-10` (project launchstation_pccf).
- Problem: Both definitions currently agree exactly (same 5 sources, PCCF_SETTLE_US=1000, -Wall -fno-strict-aliasing, same header set — verified by direct comparison this round). Two authoritative copies of one target definition is a drift trap: the first silent divergence makes the standalone/host build test different code from the shipped IRX, in a module whose VENDORED.md leans on host tests as its evidence base. No divergence exists today; the finding is the duplication itself.
- Fix: Make one definition the source of truth — the standalone file documents that it mirrors the root target and is generated/checked, or the root target references the directory's file if ps2build supports it. If the build system cannot share it, a comment cross-pointing both files is the minimum.
- Benefit: Host-test evidence stays applicable to the shipped module; one less silent-drift surface.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-170 / `pathApplied()` sets the ESR/OSDSYS LK_Flag twice — the second pair makes the first redundant / Category 2 (T6-F3)
- Locations: `src/dash_settings.c:2112-2115` (generic pair in pathApplied), `src/dash_settings.c:2140-2143` (open-coded repeat at the end of the same function).
- Problem: pathApplied first computes lk from the id and sets `setting->LK_Flag[lk] = setting->LK_Path[lk][0] != '\0'` for S_ESR_PATH/S_OSDSYS_PATH. After the language/font block it then repeats the same assignments as two separate open-coded ifs for the same two ids with the same right-hand sides. Nothing between the two pairs touches LK_Flag or LK_Path (the font/language loads touch neither), so the second pair is pure re-work, and a reader cannot tell whether the repetition is load-bearing.
- Fix: Delete the second pair (4 lines at :2140-2143).
- Benefit: One place sets the flag; no behavioural change.
- Effort: Trivial · Risk: Minimal · Confidence: READ (whole function body read between the two pairs).

### LAUNCHSTATION-171 / The CNF save text writes `PathPad_Lock` twice / Category 2 (T7-F6)
- Locations: `src/config.c:621` (inside the large settings block at :604-670) and `src/config.c:685-691` (the same key written again, alone, immediately after the launch-key-title loop) — both spot-verified.
- Problem: Every saved LAUNCHELF.CNF contains the key twice with the same value. Loading is idempotent, so this is not a correctness bug today — it is a duplicated writer for one setting, and the two copies can silently diverge the first time either block is edited.
- Fix: Delete the standalone second write at :685-691 (the block at :621 already emits the key in its settings group). Pure deletion, 7 lines.
- Benefit: One writer per key in the save format; saved files shrink by one line.
- Effort: XS · Risk: Near zero · Confidence: READ.

### LAUNCHSTATION-172 / `hdd_blkio.c` (431 lines) compiles to nothing in the shipped build and is still listed as a build source; the BDM/vHDD backends it carries are dead weight with their own latent defects / Category 1/7 (T3-F10)
- Locations: `iop/ps2hdd_osd/hdd_blkio.c:17` (`#ifdef APA_USE_BDM` block, :17-182), `:182` (`#ifdef APA_USE_IOMANX` vHDD block, :182-303), `:305-431` (`#ifndef APA_USE_ATAD` block); the live backend is the static inline ATAD forwarding in `iop/ps2hdd_osd/hdd_blkio.h:17-82`; build listing `ps2.yaml:96-104` with defines at :107-110 (APA_USE_ATAD defined; neither APA_USE_BDM nor APA_USE_IOMANX defined anywhere in the build). Knock-on dead code: hddMount/hddUmount bodies at `hdd_fio.c:1168-1207` (same IOMANX gate; the ops table at `hdd.c:72-73` resolves to the EPERM macros in `hdd_fio.h:30-36`), and every APA_SUPPORT_BHDD / APA_SUPPORT_GPT branch across apa.c, hdd.c, hdd_fio.c.
- Problem: An entire 431-line translation unit is preprocessed away on every build while masquerading in the source list as the block-I/O layer (the real one is a header). The dead bodies are not inert documentation: the vHDD validator at `hdd_blkio.c:206-220` tests `if (slot > BLKIO_MAX_VOLUMES)` where `>=` is required (its BDM sibling at :113-119 uses the correct form), so slot == BLKIO_MAX_VOLUMES would index one past `hdd_blkio_vhdd_diskinfo[]` if the backend were ever enabled as-is; `hdd_blkio_vhdd_mount` at :228-256 also truncates `disksize >> 9` into an int sectorcount. Dead code with live-looking defects is how a future "just flip the define" change imports bugs unreviewed.
- Fix: Remove `hdd_blkio.c` from the ps2.yaml:96-104 source list and delete the file (the ATAD inline header is the whole live backend); alternatively, if BDM/vHDD support is genuinely planned, delete only the `#ifndef APA_USE_ATAD` fallback bodies and fix the `>` → `>=` test before the file is allowed back into a build. Deletion is the LESS-NOT-MORE default.
- Benefit: −431 lines of audit surface; the block-I/O layer becomes what the build actually uses; two latent defects leave with the dead backend.
- Effort: M (confirm no other target references the vhdd/BDM symbols — grep shows only the dead mount/umount pair) · Risk: Low for the shipped ATAD build (the file contributes no symbols to it) · Confidence: READ.

### LAUNCHSTATION-173 / Four libhdd entry points have no caller anywhere in the tree, and the one filesystem-list walker among them strcpy's dirent names into a 32-byte field / Category 1 + 6 (T9-F5)
- Locations: dead (whole-tree grep over src/, iop/, loader/, include/, ps2.yaml: zero call sites — coordinator re-grepped, zero outside libhdd itself): `hddGetFilesystemList` (`ee_libs/hdd/libhdd.c:135`), `hddGetInfo` (`:253`), `hddRemoveFilesystem` (`:459`), `hddExpandFilesystem` (`:473`). Unsafe copy inside the dead walker: `ee_libs/hdd/libhdd.c:173,178,183` — strcpy of dirent names into `char name[32]` (`ee_libs/hdd/include/libhdd.h:44`), while the same function formats the sibling field with `%.34s` (:167) — the code itself expects names longer than 31 characters. Live libhdd entry points for contrast: hddCheckFormatted, hddCheckPresent, hddFormat, hddMakeFilesystem.
- Problem: ~200 lines of uncalled HDD filesystem management ship in the EE link surface — maintained, security-relevant-looking code that does nothing — and if hddGetFilesystemList is ever revived as-is, a maximum-length partition name overruns name[32].
- Fix: Delete the four uncalled functions and their include/libhdd.h declarations. If the project wants a filesystem list later, the dashboard's own HDD code already enumerates partitions.
- Benefit: Smaller EE binary and audit surface; removes a latent overflow by removing its only container.
- Effort: S · Risk: Low · Confidence: READ (deadness by whole-tree grep); INFERRED (exact maximum dirent length on a real APA disk).

### LAUNCHSTATION-174 / `load_ps2atad_stack()` is a 52-line DVRP loader with no caller, kept alive only by `__attribute__((unused))` / Category 1 (T6-F5)
- Locations: `src/init.c:749-800` (definition), forward declaration `src/init.c:386` — both spot-verified; whole-tree grep finds only these two.
- Problem: The function loads ps2atad + ps2hdd + ps2fs for the PSX DVR stack, but the live DVRP path never calls it — loadDVRPHddModules goes through load_ps2dvr() / load_ps2hdd_stack(1). Both declaration and definition carry `__attribute__((unused))`, the tell that the compiler already knows it is dead. Its pfsarg also diverges from the live loader's (`-m 4` here vs `-m 8` in load_ps2hdd_stack), so it is not even a faithful spare copy — reviving it as-is would silently halve the PFS mount slots.
- Fix: Delete the function and its forward declaration (~54 lines).
- Benefit: One less HDD-stack loader to audit; removes a divergent-argument trap for anyone wiring DVRP code in future.
- Effort: Trivial · Risk: Low · Confidence: READ.

### LAUNCHSTATION-175 / Six settings IDs have choice/action dispatch but no row in any page — the handlers are dead / Category 1 (T6-F1)
- Locations: `src/dash_settings.c:1019` (choiceInfo S_LEGACY_THEME), `:1022` (S_ENABLED), `:1072` (S_FILES_MODE), `:1170` (S_IGR_AUTO_BACKUP), `:1174` (S_IGR_CLOUD_SYNC); matching setChoice cases at `:1698,1702,1777,1844,1848`; `src/dash_settings.c:3093` (runThemeAction S_T_CUSTOMIZE case).
- Problem: choiceInfo/setChoice/runAction are static and are only ever called with the id of the currently selected SetItem. Extracting every S_* id used in any {ST_…} initializer in the file (all page tables, :234-920) shows none of these six ids appears in any page. S_IGR_AUTO_BACKUP/S_IGR_CLOUD_SYNC were superseded by the combined S_IGR_SYNC_MODE row; S_T_CUSTOMIZE by the P_THEME page row. Supporting evidence for S_LEGACY_THEME: `src/dash_store.c:325` forces legacy_theme = 1 with the comment "no unstyled classic look any more", so even its backing field is vestigial. (The other handled-but-rowless ids — S_CLOCK_MANUAL/SOFTWARE/HARDWARE, S_LAUNCHKEYS, S_SAVE_CLASSIC, S_REVERT — are reachable through the S_CLOCK dispatch table and the settings menu; checked, not included.)
- Fix: Delete the eleven dead choiceInfo/setChoice cases and the S_T_CUSTOMIZE case in runThemeAction. Keep the enum values themselves — the comment at `src/dash_settings.c:118-120` says the IGR split flags are retained for persisted-settings compatibility, and renumbering SetId is not needed to remove unreachable switch arms.
- Benefit: ~35 lines of dispatch that can never run stop masquerading as live settings; the next editor of S_IGR_SYNC_MODE no longer has two shadow implementations to keep consistent.
- Effort: S (deletions only) · Risk: Low · Confidence: READ.

### LAUNCHSTATION-176 / `hdl_glist_write` leaks the games array on every rename / Category 6 (T5-F6)
- Locations: `iop/hdl_info/hdl.c:141-166` (tmp malloc'd at :141, tmplist at :144; exit path frees only tmplist at :162; the `if (result != 0) free(tmp)` at :165-166 is unreachable on the success path and also skipped when the write loop breaks with result == 0).
- Problem: Every successful rename leaks `sizeof(hdl_game_info_t) * count` (~112 B × games) of IOP heap for the life of the module. Compare hdl_glist_read (:91-96), which does free both on its error path.
- Fix: `free(tmp)` beside `free(tmplist)`. One line.
- Benefit: LESS heap held forever per rename.
- Effort: One line · Risk: None (array not referenced after return) · Confidence: READ.

### LAUNCHSTATION-177 / Three duplicated dead branches/includes: second DTL check, second PSX MagicGate block, second `mmce_igr.h` include / Category 1 (T7-F5)
- Locations: `src/main_exploit_installer.c:535-540` (in get_psx_desr_payload, the dtl_signing_console test at :539-540 repeats the test at :535-536 that already returned); `src/main_exploit_installer.c:2106-2116` (in build_magicgate_result_message, the second `if (console_is_PSX)` block at :2111-2116 is unreachable — the identical condition at :2106-2109 already returned, so the hard-coded OSDMenu message inside it can never display); `src/dash_tools.c:10,12` (`#include "mmce_igr.h"` twice, three lines apart).
- Problem: Dead duplicates that mislead maintenance: the MagicGate block in particular looks like the PSX failure path, but the live PSX path is the LNG string at :2107 — anyone editing the visible-but-dead text changes nothing on screen.
- Fix: Delete the second DTL test (3 lines), the unreachable PSX block (6 lines), and one duplicate include (1 line). Pure deletion, 10 lines.
- Benefit: The PSX MagicGate failure message has exactly one source; the payload selector reads as the three-way choice it is.
- Effort: XS · Risk: Near zero · Confidence: READ.

### LAUNCHSTATION-178 / Four usbd imports are declared and never called (`strtol`, `DeleteSema`, `DeleteThread`, `DelayThread`) / Category 1 (T4-F7)
- Locations: `iop/usbd-ra/src/imports.lst:17` (I_strtol), `:32` (I_DeleteSema), `:45-46` (I_DeleteThread, I_DelayThread) — all spot-verified; zero callers in all seven .c files (grep-verified). `src/irx_imports.h` also includes thmsgbx.h and vblank.h, which no source uses.
- Problem: Dead import stubs are linked into the IRX for functions the module hand-rolls instead (decimal parsing in -140 is hand-written while strtol sits imported and unused). Pure residue, multiplied across every embedded copy (the module ships in both EE targets).
- Fix: Delete the four imports.lst lines and the two unused irx_imports.h includes.
- Benefit: Smaller IRX stub table in both embeddings; removes the false signal that strtol validates the _start arguments.
- Effort: S · Risk: Low · Confidence: READ.

### LAUNCHSTATION-179 / ata_bd imports `bdm_disconnect_bd` and never calls it / Category 1 (T2-F3)
- Locations: `iop/ata_bd/src/imports.lst:54` (I_bdm_disconnect_bd, inside the `#ifdef ATA_ENABLE_BDM` block :51-56 — a block the Launchstation build does enable, `ps2.yaml:405`; spot-verified).
- Problem: The module's only source (ps2atad.c, fully read in round 4) calls bdm_connect_bd once (:1381) and bdm_disconnect_bd zero times. There is no path that could call it: the BDM stop callback only issues a standby (ps2atad.c:1575-1580) and _exit returns MODULE_RESIDENT_END (:389), so the module never unloads or disconnects a device. The import stub is dead weight in the shipped IRX.
- Fix: Delete `imports.lst:54` (1 line). If an unload/disconnect path is ever added, the line returns with it.
- Benefit: One fewer import stub/table entry in a resident IOP module; the imports list again states exactly what the module uses.
- Effort: 1 line · Risk: Negligible · Confidence: READ + grep-verified caller count of zero.

### LAUNCHSTATION-180 / usbmass_bd imports `sceUsbdSetPrivateData` and never calls it / Category 1 (T2-F4)
- Locations: `iop/usbmass_bd/src/imports.lst:36` (I_sceUsbdSetPrivateData, usbd block :31-40; spot-verified).
- Problem: No call exists anywhere in the module's three sources (main.c, usb_mass.c, scsi.c — fully read in round 4, symbol-grepped this round). The driver deliberately tracks devices in its own array instead (`g_mass_device[i].scsi.priv = &g_mass_device[i]`, usb_mass.c:842) with connect/disconnect keyed by devId — the USBD private-data slot is never used by design, not by oversight.
- Fix: Delete `imports.lst:36` (1 line).
- Benefit: One fewer import stub/table entry; the imports list again states exactly what the module uses.
- Effort: 1 line · Risk: Negligible · Confidence: READ + grep-verified caller count of zero.

### LAUNCHSTATION-181 / pccf: one dead import — `I_memcpy` is declared but memcpy is never called in the module / Category 1 (T4-F12)
- Locations: `iop/pccf/imports.lst:11` (I_memcpy); zero call sites across main.c, pccf_ata.c, pccf_hw.c, pcdiag_*.c and the three .inc files (grep-verified; the module uses memset/memcmp/strcmp, all imported and used).
- Problem: Single dead import stub linked into the IRX. Trivial alone; filed because the same sweep (-178, -179, -180) shows the pattern is tree-wide in these vendored modules and the fix is one line each.
- Fix: Delete `imports.lst:11`.
- Benefit: One less stub; the import list again documents the module's real dependencies.
- Effort: 1 line · Risk: Low · Confidence: READ.

### LAUNCHSTATION-182 / A ~55-line commented-out font-dump function is carried in draw.c / Category 1 (T9-F9)
- Locations: `src/draw.c:343-399` — WriteFont_C in its entirety inside a `/* … */` block (header comment at :343-345 even says "Comment it out if not used").
- Problem: Dead, uncompilable-as-written legacy code (it uses unbounded sprintf/strcat throughout) kept in a live drawing file. It invites accidental resurrection — uncommenting it reintroduces exactly the string handling the rest of the tree has been eliminating — and it is the only place in draw.c a reader must skip.
- Fix: Delete the block. Font generation is already owned by `scripts/gen_dash_font.py` (see docs/font-generation.md), so nothing is lost.
- Benefit: −55 lines; one less trap.
- Effort: Trivial · Risk: None (comment text) · Confidence: READ.

### LAUNCHSTATION-183 / The commented-out CodeBreaker partition filter is pasted twice, inside two near-identical partition-list functions / Category 7 (T6-F10)
- Locations: `src/filer.c:728-746` (comment block in setPartyListForDevice, function at :709) and `src/filer.c:827-845` (identical block in setDVRPPartyList, function at :812); live loop duplication `src/filer.c:1149-1228` (readHDD) vs `:1230-1311` (readHDDDVRP).
- Problem: Each partition-list function contains the same ~18-line commented-out block ("Patch this to see if new CB versions use valid PFS format… if (!strncmp(dirEnt.name, "PP.",3))…") for filtering behaviour that was never enabled — dead text describing removed behaviour, carried in two copies. The surrounding functions are themselves DVRP mirrors differing essentially in device string and output array.
- Fix: Delete both commented blocks (~36 lines). Separately, when the DVRP reader next needs a change, parameterise the party-list loop and the dread loop rather than editing both copies; do not do that refactor speculatively now.
- Benefit: The partition code stops carrying a disabled filter as if it were a pending decision, in duplicate.
- Effort: Trivial (deletion) · Risk: Minimal · Confidence: READ.

### LAUNCHSTATION-184 / `submenu_func_GetSize()` carries ~50 lines of commented-out debug scaffolding / Category 7 (T6-F14)
- Locations: `src/filer_browser.c:2029-2034` (commented variable declarations), `:2081-2104` (commented printf attribute dump), plus the section-marker comments around them ("Start/End of section for debug display of attributes", "Comment out this section to skip attributes entirely" at :2065).
- Problem: A block of declarations (test, stats, time) and a ~24-line printf dump of stat fields survive as comments inside a shipping function, referring to variables that no longer exist in the live code. It is the only debug scaffolding of this size in the four files read in that section, and it sits in the middle of the attributes logic a reader is actually trying to follow.
- Fix: Delete the commented declarations and the commented dump (~50 lines including markers). Keep the live attribute code and its one-line explanatory comments.
- Benefit: The Get Size path reads as the code that runs; nothing of value is lost (the dump is reproducible from the live fields in minutes if ever needed).
- Effort: Trivial · Risk: None (comments only) · Confidence: READ.

### LAUNCHSTATION-185 / xparam `_start` argument handling: `argc == 3` does nothing, and the pair loop drops a trailing pair / Category 6 (T5-F13)
- Locations: `iop/xparam/src/xparam.c:30-45` (branches are `argc < 3` → CheckGameID, `3 < argc` → pair loop; `argc == 3` falls through both; the loop advances i1 by 2 but tests `i2 = i1 + 3 < argc`, so for odd argc ≥ 5 the final complete pair is never applied — e.g. argc = 5 applies only argv[2..3]).
- Problem: A caller passing exactly one explicit param/value pair (argc == 3: name + pair) gets the dummy value from SetDummyXparamValue and nothing else; a caller passing two pairs gets only the first. How the in-tree loader actually invokes xparam was not in the reading section's scope, so live impact is INFERRED; the control flow is READ.
- Fix: Change the second branch to `argc >= 3` and the loop condition to `i1 + 1 < argc` (pair = two argv slots).
- Benefit: The explicit-parameter path does what its shape promises — and, with -119 applied, the module's arguments start mattering again.
- Effort: Trivial · Risk: Low · Confidence: READ (code); INFERRED (impact).

---

## 3. Revisions to earlier rounds

**None forced.** No evidence read in round 5 contradicts LAUNCHSTATION-001…096. The two open items carried into this round are disposed of in §1 (-085 settled; the mcdev mask lead filed as -118). -068's reachability judgement is confirmed and strengthened (§1.2). -066's caveat stands unchanged because `ps2smb.h` is absent from the clones (header note). Items re-verified in context and standing: -003, -014 (with -153 filed as its new instance rather than appended), -015 (with -097 filed as its load-side remainder), -026, -033, -036, -052, -077 (the mmce_backup prune re-enumeration is -077; -121 is a distinct defect in the same file), X-18, X-21.

**Category re-mapping (integration correction, substance unchanged):** section 03's T3-F1/F2/F7/F8/F9/F12/F13 and section 05's T5-F1/F2/F6/F11/F12/F13 arrived with category numbers outside the audit scheme; they are filed above under the scheme's numbers (mostly 6). The section files in `round5-sections/` retain the original labels.

## 4. Provenance determinations made this round (first reads)

- **`iop/ps2hdd_osd`:** vendored PS2SDK ps2hdd/APA, imported wholesale in the single initial commit `944a2fc`; **no post-import source edits** (only a Makefile deletion and a one-line README edit since). The sole README-declared local change (HDD-unit tracking in open slots, `hdd.h:19`; checks `hdd_fio.c:223,493,546`) is present and complete. The OSD flavour comes from `APA_OSD_VER` in `ps2.yaml:107-110`, not from source edits. (INFERRED as to byte-identity with upstream — no upstream fetch was made; rests on import history, banners, README, in-source markers.)
- **`iop/usbd-ra`:** pristine ps2sdk pre-rewrite (314d87e7, via RiptOPL), single import commit `959c271`, zero .c/.h edits since; local delta is build config only (MINI_DRIVER, USE_GP_REGISTER=0). Built and embedded **twice** (`ps2.yaml:46-63`; in wLaunchELF and wle_igr), loaded with 0 args (`src/init.c:1566`).
- **`iop/pccf`:** mihhru/PS2CF @ `9b6ed37` per VENDORED.md; local delta = DEV9-reject/no-reclaim entry in main.c, the entirely local `pccf_control.inc`, a uintptr_t fix. Built (`ps2.yaml:37-44`), embedded in wLaunchELF only, on-demand/ownership-gated; VENDORED.md states no Files/boot caller yet — the mount feature is unfinished by its own documentation (recorded as a build fact, not filed: the gating is documented and deliberate).
- **`iop/udpfs_smap`:** Neutrino @06188cce, built + embedded; documented local fixes verified present in the text.
- **`iop/ps2ips_rpc`:** ps2sdk @329e2bb4, built + embedded; all 10 documented local fixes spot-verified present (-141/-142 are the missed siblings of the setsockopt-style fix).
- **`iop/hdl_info`:** 2006 HDLoader legacy, built + embedded. **APA record, settled with hashes:** `hdl_info/apa.c` (9,728 B, md5 `e71d6ec1…`) vs `ps2hdd_osd/apa.c` (15,657 B, md5 `37fcea9a…`), diff 843 lines — **NOT a copy** (a minimal table reader vs the full cache/journal driver); no LESS deletion available.
- **`iop/ds34usb` vs `iop/ds34bt`:** NOT in the default ps2.yaml (DS34 variant only, via generated `.ps2-ds34.yaml`). Drift verdict: mostly bug-for-bug; BT ahead on the DS4 parse guard (bytes > 63 touchpad); press_emu is an intentional BT variant; the one real divergence is -136.
- **`iop/smb2man/usmb2` helpers (inventory):** ntlm.c 263 (locally patched per VENDORED.txt), md4c.c 240 (pristine RFC 1320), md5.c 190 (pristine), unicode.c 204 (one documented local fix), hmac-md5.c 99 (the documented Latin-1 change); single import commit `7082163`, no later local edits.
- **Binaries:** `iop/__precompiled/udpfs_smap.irx` (8,081 B, sha256 `4b81d9bf…`) is a documented historical binary, NOT embedded (negative control for `scripts/check_udpfs_smap.py` only); the other four inventoried IRX (udpfs_ioman, udpfs_ministack, __v10/ps2dev9, __v10/ps2ip) are embedded via `src/embed_blobs.S`.

## 5. Coverage table — complete, all rounds (Launchstation, 1,065 tracked files)

| Area | Status after Round 5 |
|---|---|
| **EE `src/` — 126 files** | **Every file read end-to-end in at least one round.** R5 closed the remainder: §06 (dash_settings, init, filer, filer_browser), §07 (main_exploit_installer, filer_actions, icon [pure data], config, dash_tools, filer_copy 1-1400 completing R2's 1400-1721), §08 (cloud_google, cloud_http, dash_ui, dash_main, dash_editor, dash_hdd, dash_widgets, dash_themes, hdd_header_injector, kelf_signer, mmce_backup — 14,818 lines), §09 (89 files ≈36,600 lines incl. mc_save_formats 176-954 completing prior rounds' 1-175). Earlier rounds: dash_browser, dash_card_tool, filer_device, filer_mount, self_update, update_psu (pass 1/R2/R3), mc_card_write/view/export, mc_save_export, mc_ps1_card (R3). Coordinator-closed in R5: `popstarter_internal.h` (declarations only, clean), `embed_blobs.S` (assembly embed list, inventoried), font data ×3 + qrcodegen.c (pure data / vendored Nayuki, classified, not line-audited). |
| **`include/` — 66 headers** | 63 read fully in R5 (every header ≤150 lines, per §09's table); qrcodegen.h tabled as vendored; launchelf.h (763) and dash_internal.h (1,186) read across rounds 1–4 as the core headers (not re-read line-by-line in R5). `iop/include_compat/poweroff.h` read by coordinator in R5 (vendored declarations, clean). |
| **`iop/` — 25 dirs / 24 modules** | **Every module's C sources read end-to-end in at least one round.** vmcman (R3/R4 + R5 §01 full mcdev/backing), mmceman (R4 + R5 §02 full mmce_cmds), smbd local (R2/R3/R4), smbman (R2/R3; -066 caveat stands), smb2man/usmb2 (R2 + R5 §05 remainder; helpers inventory), oldlibs/ps2ftpd (R2 line-read, all seven build sources), ps2hdd_osd (R5 §03, first full read: 4,628 lines), hdl_info (R5 §05 full; R3 ranges), ata_bd (R4 .c + R5 §02 headers/exports/imports — exports↔imports↔definitions match slot-for-slot, 17/17), usbmass_bd (R4 .c + R5 §02 glue), usbd-ra (R5 §04 full, 4,029 lines), pccf (R5 §04 full), udpfs_smap (R5 §05), ps2ips_rpc (R5 §05), ps2host (R2 + R5 §05 remainder — clean), ds34usb/ds34bt (R5 §05 full), xparam (R5 §05; database_merged.c = 292 data rows, structure read, per-game factual correctness not validated), dev9_poweroff + AllowDVDV (R5 §05, clean), mx4sio_bd (R2), nfsman/unfs4 (R2/R3), cdvdman (R3), ps2dev9 (R2). `__precompiled/` + `__v10/` binaries: sized/hashed (§4). |
| **Vendored/fetched (smbd's libsmb2)** | Fetched copy (R4, /tmp only, pin a52c1571…, both patches applied): server halves + smbd glue read R3/R4; **client halves of lib/libsmb2.c, asn1-ber decode internals, compat bodies, crypto primitive bodies remain inventory-level only** (pristine upstream) — residue (b). |
| **`ee_libs/`, `loader/`, `research/`, `windows/`** | Read fully in R5 §09: libhdd.c/.h, SifIopRebootBuffer.c, patch_romdrv_set_rom1_info.c, ps2ipc.c (checked-clean), loader/loader.c (checked-clean); loader/linkfile inventoried; research txn model read (checked-clean, not shipped); Cloud Relay.ps1 read (inherits the already-filed -033/-040 FTP posture; its hardcoded password noted by location only). |
| **`igr/`, `exploits/`, `ship/`** | Inventoried in R5 §09: wle_igr.c classified (standalone trampoline, not line-audited); exploits/ 13 binaries + README inventoried (-020 already covers the unreferenced pair); ship/ release-kit material inventoried. |
| **`scripts/` — 24** | Status-tabled in R5 §09: 18 invoked by ps2.yaml/workflows; 6 manual/one-off (incl. stale_code_audit.sh, completing -032's row without re-filing). Not line-audited as code. |
| **`.github/workflows/` + `ps2.yaml`** | Read across rounds (pass 1; R4 census; R5 stanzas for every module verdict above). |
| **`tests/` — 273 files** | 9 paths/groups individually cited/read across rounds 1–4 (test_self_update, test_app_packages, test_update_sidecar_transaction, test_browser_sort, test_classic_settings_persistence, test_navigation_wraparound, ftp/, smb2man_live/, smbd_live/); **the remainder is pattern-classified only** (host harnesses/fixtures) — residue (c). |
| **`docs/` (~161 files, 676 KB) + `docs-site/` (17 MB)** | Inventoried by size/structure only (prose) — residue (d). -028 (stale documents) stands from pass 1. Root prose (README, CHANGELOG, CLAUDE.md, USB-STABILITY-AUDIT, storage_driver_fixes_plan, Lang/, res/, __Headers/) inventoried in pass 1 / R5 sweep. |

## 6. Checked and clean (consolidated; full lists with citations in the nine section files)

- **vmcman/mcdev:** all 11 unit2card call sites hold mcman_io_sema; fd ops carry no slot risk; mc_chstat's semaphore is not leaked on the full-info early return; mc_rename's name copy is bounded; backing erase buffer is exactly sufficient (544 B vs max effective page 544); backing read's software-ECC loop is bounded; mount's post-open failure paths do close the new fd; VMCMAN diagnostic printfs use only mask-bounded slots.
- **mmce_cmds:** the other eight commands assign every byte they send (-135's majority proof); no reply field is used as a count/length/index inside the file; reply validation posture is uniform (rdbuf[0]/[2] unchecked — not filed: no protocol document in-tree to show they carry a checkable value); all 11 header prototypes match definitions and all 11 have live callers — no dead command function exists.
- **ata_bd / usbmass_bd glue:** exports↔imports↔definitions match slot-for-slot (17/17 named exports); the IRX_ID 2,7 vs table 1,3 pairing is upstream convention, not skew (dissolved with cross-repo evidence); conditional glue is self-consistent in both modules; usbmass printf import is live via M_PRINTF (a naive grep would mis-file it — dissolved with the macro evidence); the transfer helpers resolve to sceUsbdTransferPipe via usbd_macro.h — no genuinely undeclared import exists in either module.
- **ps2hdd_osd:** the declared unit-tracking delta is present and complete (no fourth duplicate-open scan); apaCacheFree-then-read patterns dissolve (non-destructive pool return under fioSema); **no password value is printed or logged anywhere in the directory** (grep-verified); hddOpen/hddFormat unit range checks exist; ioctl2Transfer's sub-index test is correct; fioDataTransfer/hddLseek windows are confined; hddDread's name copy is bounded (the unbounded SONY original survives only as a comment); journal single-buffer sharing across two units is safe under fioSema; free.c's walk terminates; misc.c retry loops are bounded.
- **usbd-ra:** interface.c's export surface validates device/endpoint via range- and liveness-checked lookups; mem.c free-list functions scan for double-free; the OHCI write-to-set/write-to-clear register assignments are not lost-update bugs (dissolved with hardware semantics); driver.c's post-free read is compiled out under USE_GP_REGISTER=0; exports.tab exports exactly the 14 implemented functions.
- **pccf:** the CIS parser is the model citizen of the two modules (bounded walk, length-before-copy, malformed flags instead of trust); ATA layer validation is thorough (IDENTIFY gating, bounded polls, checked writes with read-back — slow by documented design, not filed); BDM entry points fail closed; the local devctl (pccf_control.inc) validates exactly as several round-2/3 drivers did not; _start's DEV9 scan is capped and fail-closed with reverse-order unwind.
- **udpfs_smap / ps2host / dev9_poweroff / AllowDVDV:** header pre-read clamped; odd-tail fix present as ORIGIN.md claims; DMA block-size tiers respected; EEPROM MAC checksum + all-zero rejection present; ps2host remainder clean (every request builder bounded + forced NUL; one INFERRED one-descriptor nit below filing threshold); dev9_poweroff registers exactly the two handlers its comment names; AllowDVDV's memcpy offsets verified by hand to end exactly at 137, key tables zeroed after use.
- **EE big files (§06/§07):** editClockDateTime's day-of-month init is safe (dissolved by loop trace); settingsRevert matches store policy; the SMB login strcpy cannot overflow (31-char keyboard cap into a 32-byte field); editScanPaths' strcat is gated; startSmbServer's args packing fits by construction (32+32=64 exactly); configureDashboardDns's fallbacks produce the intended pair; readNetDir/readHOST's sentinel write stays inside the allocation via listDir's max; get_kelf_key_offset is bounded as written (residual: BIT_count's width lives in PS2SDK's libsecr.h, outside the repo — INFERRED-unverifiable, not filed); make_title_cfg's index subtraction cannot underflow under its callers' extension gate; dash_tools' GitHub JSON parsing is length-checked; cloudBrowseHistory's depth stack is bound-checked before the push; PSU header walk is monotonic and bounded.
- **EE mid files (§08):** cloudGoogleFinishLink's slow_down mapping is correct (looks inverted, isn't); download_write's MEM_FD guard and SHA ordering are correct; decode_chunked checks each chunk length before the memmove; bearer-token redirect discipline re-validates the host per hop; the injector's single genRead sites fail on short reads; kelf_signer's three output-size computations agree; mmce_backup's per-chunk open/close is forced by the device (one open file at a time), not waste; dash_editor's edIo job ownership is correct (backout path frees via the abandoned flag); dashThemeSerialize's headroom guard cannot truncate for table-generated content; dash_hdd's scan/pfs job ownership is leak-free.
- **EE small files (§09):** pc_game_receiver (whole file — validates folder, rejects traversal names, caps items, verifies resume sizes); smb_cnf (exemplary parser — copyValue refuses overflow rather than truncating; one ≤2-byte BOM over-read dissolved as no-consequence); neutrino_smb; dash_clock_sync (SNTP validation incl. stratum-0 refusal and RTC read-back — clean apart from -168); mmce_save_versions (staged, hash-verified publish); psu_functions (traversal-validated restore); dash_jpeg (caps + NULL-checked allocations); mc_save_formats 176-954 (record counts bounds-checked before filling; inflate capped and exact-size verified); mc_sha256 (verified against the FIPS 180-4 shape); lang.c's two-pass loader validates index < LANG_COUNT in both passes; ftp_session/dash_ftp (DMA state never reused after timeout; protocol version checked per reply); pad.c vs pad_ds34.c are build-flag alternatives, not duplication (dissolved).

**Open question recorded, NOT filed:** ps2ips `do_dns_getserver` (ps2ips.c:565-573) dereferences dns_getserver()'s return unchecked — lwIP's contract for an out-of-range index was not read in this pass, so no defect is claimed. It joins the residue list as item (i).

## 7. FORMAL VERDICT

**Launchstation is EXHAUSTED at source level in this environment, subject to the exact residue below.** After five rounds, every C source file in `src/` (126/126), every IOP module's C sources (24/24 modules), every header ≤150 lines, and the ee_libs/loader/research/EE-adjacent code have been read end-to-end at the pin at least once, with per-file ledgers in the round reports and section files; the two settlements this round was commissioned to produce are closed with full evidence chains (-085: proven waste, fix = delete the MAXSLOT override, ≈33,984 B; the mcdev mask: latent live defect, filed as -118, fixed by the same deletion). Round 5 added 89 findings (LAUNCHSTATION-097…185) for a five-round total of **210**, and forced **no revisions** — the marginal yield has collapsed from whole subsystems (rounds 1–3) to single-line defects, dead imports, and latent paths, which is the signature of a swept tree. What remains is not unread Launchstation source; it is the following named residue, none of which further reading *in this environment* can close:

- **(a) The smbd link test — un-auditable in this environment.** Round 4's R4-R3(c)/(d) deletions (the fetched libsmb2's `sync.c`, `smb2-share-enum.c`, `smb2-data-security-descriptor.c`, `smb2-data-reparse-point.c`) are safe iff a real smbd build's `--gc-sections` discards the referencing client sections as analysed. There is **no PS2 toolchain in this VM**, so the build + `tests/smbd_live` run that settles it cannot be performed here. This gates **only** those four deletions; R4-R3(a)'s two deletions are source-proven and do not depend on it.
- **(b) Fetched libsmb2 client-side surfaces — inventory level only:** the client halves of `lib/libsmb2.c`, `lib/asn1-ber.c` decode internals, `lib/compat.c` bodies, and the crypto primitive bodies (md4c/md5/sha/hmac/aes128ccm/smb3-seal/smb2-signing). Pristine upstream by the fetch record; the server halves and all smbd glue were read in rounds 3–4. (The same inventory-only status applies to usmb2's five helper files inside the clone — §4.)
- **(c) tests/ beyond the individually cited subset:** 9 paths/groups read or executed across rounds; the remaining ~264 files are pattern-classified only (host harnesses and fixtures that largely scrape C source as text).
- **(d) docs/ and docs-site/ prose:** inventoried by size and structure only (~161 files / 676 KB and 17 MB respectively), plus root prose and Lang/res/__Headers assets.
- **(e) Binaries:** the five precompiled/v10 IRX and the exploits/ payloads are sized and hashed only — never disassembled (no binary audit was in scope in any round).
- **(f) Small-module build glue partially unread:** `imports.lst`/`exports.tab`/`irx_imports.h` for udpfs_smap, hdl_info, ps2ips_rpc, ps2host, ds34usb/ds34bt, xparam, dev9_poweroff and AllowDVDV were inventoried, not line-read (the ata_bd/usbmass_bd/mmceman/vmcman glue **was** fully read and cross-checked, §6).
- **(g) Assembly:** `ds34*` `smsutils.S` (both modules) — accounted by name only; `src/embed_blobs.S` inventoried as an embed list.
- **(h) Data correctness, not code:** xparam's 292 database rows were read as structure; whether each game's XPARAM values are *factually* right is game-compatibility data validation, outside a code audit. The three EE font-data files (~26,497 lines) and qrcodegen.c are classified data/vendored, not line-audited.
- **(i) One open question, not a finding:** ps2ips `do_dns_getserver`'s unchecked dereference of lwIP's return — the lwIP contract lives outside this repo and was not read (§6).
- **(j) Standing environmental limits (all rounds):** no PS2 build, no link test, and no real-hardware result exists for anything in this audit; every Category 5 magnitude is structural, every RAM figure is source arithmetic, and every fix is a code-read claim. Per the user's standard, a build is not proof — hardware is, and none was available here.

**End-state verification:** after all passes, `git rev-parse HEAD` and `git ls-remote origin master` in the clone both return `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` and `git status --porcelain` is empty. Read-only held. No libsmb2 work was needed this round (round 4's fetch remains in /tmp only; the clone's `.smbd-deps/` was never created).

*End of round 5 — Launchstation. 89 new findings (LAUNCHSTATION-097…185), 2 settlements (-085 closed as proven waste; the mcdev mask lead filed as -118), 0 revisions forced, complete coverage table, formal exhaustion verdict with a 10-item named residue (a)–(j).*
