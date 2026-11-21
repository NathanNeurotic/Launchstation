# OLD vs REDO comparison — Launchstation

**Pin (both finals):** Launchstation `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (master). Clone `~/workspace/audit-clones-2026-11-13/Launchstation` re-verified at this HEAD with a clean `git status` at the start and end of this comparison; read-only throughout. libsmb2 findings in both corpora concern upstream `a52c1571a1ff223bde5bb3df9fec7af5ce3855d4` + the repo's two patches.

**Corpora.** OLD final: `~/workspace/Sable-audit/` — LAUNCHSTATION-001…223 in `Sable-Launchstation*.md`, statuses per `Sable-MASTER-INDEX.md`; verification in `Sable-VERIFY-OTHERS-CAT6.md` (phase 1) and `Sable-VERIFY2-LAUNCHSTATION.md` (full verification). REDO final: `~/workspace/Sable-audit-redo/` — RE-LAUNCH-001…096 in `Redo-Launchstation.md` + `Redo-NEW-FINDINGS.md`, statuses per `Redo-MASTER-INDEX.md`; verification in `Redo-VERIFY-Launchstation.md`.

**Actionable sets.** OLD: 219 findings (195 STANDING + 24 REVISED; the 4 WITHDRAWN items — 065, 100, 127, 214 — are excluded from matching and treated in the appendix). REDO: 95 findings (STANDING / STANDING-DRIFT / REVISED + NEW; the 1 WITHDRAWN item, RE-LAUNCH-007, is excluded as a match target).

**Method.** Matching is mechanism-level: four passes anchored on the old corpus read both sides' bodies (never titles alone) and recorded MATCH / PARTIAL / NONE per old finding. Adjudication then re-derived every NONE/PARTIAL Category 6 item and every high-benefit (Effort M/L) NONE: REALness rests on the filing corpus's own adversarial verification verdict (every actionable item in both finals carries one) plus coordinator spot-checks of the code at the pin for the most consequential and contested items (evidence in §5); the COVERAGE vs genuine-miss split rests on the missing side's own declared coverage ledgers (redo: `Redo-Launchstation.md` "Files not read (explicit)" + `redo-sections/ee-core.md` §A.4 + `redo-sections/iop-other.md` coverage ledger; old: round 5/7/8 residue statements in `Sable-MASTER-INDEX.md` §1.2). Remaining non-Category-6 NONEs are classified COVERAGE vs UNEXPLAINED by declared scope only.

---

## 1. Counts

### 1.1 Old-anchored (219 actionable old findings)

| Outcome | Count |
|---|---|
| MATCH (same mechanism in the redo final) | 41 |
| PARTIAL (same family; different site/limb or partial scope) | 22 |
| NONE (no redo counterpart) | 156 |

### 1.2 Old-only (NONE = 156) adjudication split

| Class | Sub-class | Count |
|---|---|---|
| Category 6 NONE (65) | REAL — genuine redo miss | 39 |
| Category 6 NONE (65) | COVERAGE — real, inside the redo's declared files-not-read | 26 |
| Category 6 NONE (65) | NOT-REAL | 0 |
| Category 6 PARTIAL (11) | mechanism REAL in all 11; unmatched limb COVERAGE in 2 (LAUNCHSTATION-113, -144), genuine-miss limbs in 9 | 11 |
| Non-Cat-6 NONE, Effort M/L (20) | REAL | 14 |
| Non-Cat-6 NONE, Effort M/L (20) | COVERAGE | 6 |
| Non-Cat-6 NONE, Effort S/XS/Trivial (71) | COVERAGE (by declared scope) | 22 |
| Non-Cat-6 NONE, Effort S/XS/Trivial (71) | UNEXPLAINED (by declared scope) | 49 |

### 1.3 Redo-anchored (95 actionable redo findings)

| Outcome | Count |
|---|---|
| Has an old actionable counterpart (MATCH/PARTIAL above consume 54 redo IDs) | 54 |
| Redo-only (no old actionable counterpart) | 41 |

### 1.4 Redo-only (41) adjudication split

| Class | Count |
|---|---|
| REAL Category 6 — genuine old misses | 14 |
| REAL Category 8 (smbd fake-success / vendored-latent cluster) — genuine old misses | 4 |
| UNEXPLAINED non-Cat-6/8 — genuine old misses by declared scope (old read these areas; no old residue covers them) | 23 |
| NOT-REAL | 0 |
| COVERAGE (old declared residue) | 0 |

---

## 2. Mapping table (old-anchored, all 219 actionable old findings)

Verdicts and mechanism notes are the four matching passes' body-level records. Cat is the old finding's category as filed.

| Old ID | Old Cat | Verdict | Redo ID(s) | Mechanism note |
|---|---|---|---|---|
| LAUNCHSTATION-001 | 4 | NONE | — | All nine languages compiled into ELF |
| LAUNCHSTATION-002 | 4 | NONE | — | Three font families at three sizes in .rodata |
| LAUNCHSTATION-003 | 2/8 | NONE | — | Legacy PS2SDK-v1.0 second parallel TCP/IP stack |
| LAUNCHSTATION-004 | 8/4 | NONE | — | Native Google Drive cloud subsystem in every ELF |
| LAUNCHSTATION-005 | 2 | NONE | — | SMB1 client duplicates SMB2/3 client |
| LAUNCHSTATION-006 | 8/2 | NONE | — | libsmb2 SMB server embedded alongside FTP/host/NetFS |
| LAUNCHSTATION-007 | 8 | NONE | — | Exploit installer, KELF signer, payloads in launcher |
| LAUNCHSTATION-008 | 7 | MATCH | RE-LAUNCH-054 | Same GIF/MP4 duplicate demos dominate docs-site; redo narrower — GIF pair only, no PNG re-encode limb |
| LAUNCHSTATION-009 | 7/3 | PARTIAL | RE-LAUNCH-045, RE-LAUNCH-046 | Redo covers CI test-list drift and one sequential masking step; it does not file old's core source-scraping-test deletion mechanism |
| LAUNCHSTATION-010 | 7/1 | NONE | — | CI builds whole project twice for DS34 variant |
| LAUNCHSTATION-011 | 4 | NONE | — | Two complete UI sound themes both embedded |
| LAUNCHSTATION-012 | 2 | NONE | — | Legacy font_uLE is third overlapping text path |
| LAUNCHSTATION-013 | 1/7 | PARTIAL | RE-LAUNCH-062, RE-LAUNCH-061 | bgm.ogg dead-file deletion matches RE-062 exactly; boot.adp non-embedding is documented in RE-061, whose fix is a README correction, not deletion |
| LAUNCHSTATION-014 | 5 | MATCH | RE-LAUNCH-085, RE-LAUNCH-078 | Editor's five 1.5 s empty spins match RE-085 and filer's triplicated retry/backoff spins match RE-078; redo does not cover the draw.c 5 ms spin |
| LAUNCHSTATION-015 | 6 | PARTIAL | RE-LAUNCH-006, RE-LAUNCH-017 | Same unbounded string-copy family in config load (RE-006) and path assembly (RE-017), different sites; old's config-save sprintf chain itself is only RE-LAUNCH-007, WITHDRAWN, so that limb is unmatched |
| LAUNCHSTATION-016 | 6 | NONE | — | Unchecked genRead/genWrite returns on file/font data |
| LAUNCHSTATION-017 | 4/5 | NONE | — | Self-update holds whole second ELF in RAM |
| LAUNCHSTATION-018 | 1 | NONE | — | Thirteen #if 0 dead blocks in IOP modules |
| LAUNCHSTATION-019 | 1/7 | NONE | — | Stale precompiled udpfs_smap.irx superseded by source build |
| LAUNCHSTATION-020 | 1 | MATCH | RE-LAUNCH-062 | Same two unreferenced payloads, IOPRP.IMG and XSYSTEM2.XLF, deleted in RE-062 (which adds bgm.ogg) |
| LAUNCHSTATION-021 | 2/7 | NONE | — | Byte-identical duplicate files: ioplib, smsutils, AGENTS/CLAUDE, icons |
| LAUNCHSTATION-022 | 1 | NONE | — | res/icons PNGs unused by procedural icon system |
| LAUNCHSTATION-023 | 7 | NONE | — | Remote branch and build-tag clutter, 104/140 refs |
| LAUNCHSTATION-024 | 2/3 | MATCH | RE-LAUNCH-044 | Same parallel classic-vs-dashboard settings system editing the same SETTING; redo frames it as dashboard shadowing/interception, old as dual persistence/save paths |
| LAUNCHSTATION-025 | 1 | MATCH | RE-LAUNCH-090 | Same DASH_AUTOTEST player and DASH_THEME_TOUR blocks in production sources; redo does not cover old's debug-lib/scr_printf limb |
| LAUNCHSTATION-026 | 2/8 | NONE | — | Three launcher integrations, separate game-ID/path/launch stacks |
| LAUNCHSTATION-027 | 7 | NONE | — | Six workflows, download-counter running eight times daily |
| LAUNCHSTATION-028 | 7 | PARTIAL | RE-LAUNCH-067 | Same stale plan/status layer misleading readers; redo covers different specific files (move-manifest, Nov-4 plans) and fixes by status banners, not deletion/archive |
| LAUNCHSTATION-029 | 7 | PARTIAL | RE-LAUNCH-071 | Same __Headers APA sample sets; redo's mechanism is missing usage/provenance documentation, old's is clutter/deletion — and redo omits ship/ shortcuts and kit-externals |
| LAUNCHSTATION-030 | 4 | NONE | — | Dashboard font allocates 512 KB scratch texture arena |
| LAUNCHSTATION-031 | 8 | NONE | — | QR generator, 1,027 lines, for one login screen |
| LAUNCHSTATION-032 | 7 | PARTIAL | RE-LAUNCH-060 | stale_code_audit.sh is one of old's one-off scripts; redo proves it broken and unrun and offers deletion, but covers none of old's other named scripts |
| LAUNCHSTATION-033 | 6 | PARTIAL | RE-LAUNCH-004 | Same `-anonymous` FTP launch at init.c:1176; redo's limb is the anonymous remote ELF-launch command inbox, not old's anonymous full read/write/delete over all devices, and its fix gates the command polls, not FTP itself |
| LAUNCHSTATION-034 | 6 | MATCH | RE-LAUNCH-005 | Same in-place FIO_O_TRUNC overwrite of the running ELF with no staging/backup/readback; redo adds the false CHANGELOG `.BAK` claim |
| LAUNCHSTATION-035 | 6 | NONE | — | ps2host read reply trusts host byte count |
| LAUNCHSTATION-036 | 4 | MATCH | RE-LAUNCH-052 | Same fixed MAX_ENTRY FILEINFO (~768–900 KB) allocation per listing regardless of folder size; redo covers the classic filer sites, not old's dashboard two-buffer copy or dash_* sites |
| LAUNCHSTATION-037 | 6 | NONE | — | FTP PORT command unvalidated bounce primitive |
| LAUNCHSTATION-038 | 6 | NONE | — | FTP PASV accepts any host connecting first |
| LAUNCHSTATION-039 | 3/5 | NONE | — | Card tool keeps ten whole-image safety copies |
| LAUNCHSTATION-040 | 1/6 | NONE | — | FTP credential path dead: -pass, -port, hardcoded defaults |
| LAUNCHSTATION-041 | 1 | MATCH | RE-LAUNCH-058 | Same dead file-based PSU extractors kept alive only by the host harness; redo adds cloudHttpDownloadFd, old's `.BAK` length-gate limb is unmatched |
| LAUNCHSTATION-042 | 2 | NONE | — | nfsman and smb2man duplicate URL parser/connect skeleton |
| LAUNCHSTATION-043 | 2 | NONE | — | Reply-collection loop thrice; MD4 and UTF-16 twice |
| LAUNCHSTATION-044 | 1/7 | NONE | — | ps2ftpd and smbman build without --gc-sections |
| LAUNCHSTATION-045 | 2 | NONE | — | isHddRootPath exists three times, three definitions |
| LAUNCHSTATION-046 | 2 | NONE | — | getXfromRelativePath byte-identical in two files |
| LAUNCHSTATION-047 | 2 | NONE | — | Endian helpers copied sixteen times across MC files |
| LAUNCHSTATION-048 | 6/1 | NONE | — | ps2host write segmentation uses read-segment constant; unterminated dirent |
| LAUNCHSTATION-049 | 1 | NONE | — | Dead FTP SITE handler and unimplemented FEAT declaration |
| LAUNCHSTATION-050 | 5 | PARTIAL | RE-LAUNCH-092 | Same single static 32 KB FTP buffer at FtpClient.c:88; old frames it as client-serialising performance, redo as permanent IOP footprint with no fix required |
| LAUNCHSTATION-051 | 6 | NONE | — | smbman prepare_path strncpy/strcat without guaranteed NUL termination |
| LAUNCHSTATION-052 | 2 | NONE | — | FILEOP_TRACE guard block pasted into three filer files |
| LAUNCHSTATION-053 | 6 | MATCH | RE-LAUNCH-008 | Same defect: smbd_pread/pwrite cast 64-bit SMB offset to (int) before iomanX_lseek, wrapping large offsets; identical fix site. |
| LAUNCHSTATION-054 | 6 | MATCH | RE-LAUNCH-026 | Same defect: mmce_cmd_set_gameid narrows strlen+1 to u8 and strcpys unbounded into 255-byte wrbuf stack buffer. |
| LAUNCHSTATION-055 | 6 | MATCH | RE-LAUNCH-023 | Same defect: mmce_cmd_get_gameid strcpys firmware string from rdbuf[0x4] with no NUL check into caller's buffer; redo omits the discarded-buflen detail. |
| LAUNCHSTATION-056 | 6/2 | NONE | — | smbman FileTimeToDate hand-rolled algorithm computes wrong dates |
| LAUNCHSTATION-057 | 6 | NONE | — | mmceman devctl dereferences arg as u32 without NULL/arglen check |
| LAUNCHSTATION-058 | 6 | NONE | — | smb_closeAll CLOSEs directory search handles, clears no handle slots |
| LAUNCHSTATION-059 | 6 | NONE | — | smbman lseek64 discards SEEK_END offset, accepts negative positions |
| LAUNCHSTATION-060 | 6 | NONE | — | smbman read/write/dread/lseek dereference handle unvalidated, no negative-size check |
| LAUNCHSTATION-061 | 6 | MATCH | RE-LAUNCH-020 | Same defect: SMBD_DEVCTL_START takes password at strlen(arg)+1 without checking a second string lies within arglen; redo adds an already-running-returns-success limb. |
| LAUNCHSTATION-062 | 5 | NONE | — | DMA_sendEE spins forever with no timeout, SMB IO lock held |
| LAUNCHSTATION-063 | 6 | NONE | — | smbman dread strncpy can leave dirent->name unterminated at ≥256 bytes |
| LAUNCHSTATION-064 | 6/5 | NONE | — | smbd replacement calloc/malloc perform unchecked size arithmetic |
| LAUNCHSTATION-066 | 6 | NONE | — | smbman devctl dereferences caller buffers with no NULL/length validation |
| LAUNCHSTATION-067 | 6 | MATCH | RE-LAUNCH-010 | Same defect: SMB server ships with a known published default login; redo additionally covers clear-text storage and on-screen password printing. |
| LAUNCHSTATION-068 | 6/2 | MATCH | RE-LAUNCH-035 | Same defect: vmcman backing validator `slot > MCMAN_MAXSLOT` admits slot==MAXSLOT and negatives, one past the array; identical one-line fix. |
| LAUNCHSTATION-069 | 6 | NONE | — | smbman chdir reads path[-1] for root/empty directory names |
| LAUNCHSTATION-070 | 1 | MATCH | RE-LAUNCH-056 | Same defect: mmcedrv.c (413 lines) and mmceigr.c (168 lines) are compiled by nothing and referenced nowhere. |
| LAUNCHSTATION-071 | 2 | NONE | — | Card-image copy+verify engine written five times in mc_card_export.c |
| LAUNCHSTATION-072 | 1 | PARTIAL | RE-LAUNCH-057 | Same dead-compiled-libsmb2 mechanism for quic/smbdirect/sync/share-enum, but redo covers only 4 of old's 7 files — old also lists aes_reference.c (as duplicate) and the two smb2-data-* files, which redo does not cover. |
| LAUNCHSTATION-073 | 4 | NONE | — | ~0.4 MB static .bss held for modal export/card-tool/browser screens |
| LAUNCHSTATION-074 | 1 | NONE | — | unfs4_init_context has no caller, incompatible second connect path |
| LAUNCHSTATION-075 | 2 | NONE | — | IOP malloc/free/calloc trio byte-identical in smb2man and nfsman |
| LAUNCHSTATION-076 | 5 | NONE | — | Dashboard browser insertion sort moves 384-byte FILEINFO structs |
| LAUNCHSTATION-077 | 5 | NONE | — | MMCE history prune re-enumerates folder for every generation deleted |
| LAUNCHSTATION-078 | 1/7 | NONE | — | Dead ps2ip import I_lwip_shutdown has no caller under either name |
| LAUNCHSTATION-079 | 6 | MATCH | RE-LAUNCH-002 | Same defect: mcman_setdevinfos overwrites derived geometry with raw 0x150-byte superblock copy, unvalidated; old enumerates more sinks (divisors, pmce[16], pagedata[32]), redo centres on the McReadCluster cache-entry overrun. |
| LAUNCHSTATION-080 | 6 | MATCH | RE-LAUNCH-013 | Same defect: unbounded index into 32-entry ifc_list in main.c:3872-3932 from cluster/FAT values; old frames it via the McGet/McSetFATentry exports, redo via the FAT-chain/directory walk in the same range. |
| LAUNCHSTATION-081 | 6 | NONE | — | Card entry names consumed unterminated by strlen/strcmp/strcpy in vmcman |
| LAUNCHSTATION-082 | 6 | NONE | — | PS1 open/getstat/setinfo dereference dir-entry pointer before checking lookup result |
| LAUNCHSTATION-083 | 6 | NONE | — | McWrite negative length reaches memcpy as huge size_t in both write paths |
| LAUNCHSTATION-084 | 6 | PARTIAL | RE-LAUNCH-035 | Same missing slot-validation family, different site: old covers the mcman library exports' unvalidated port/slot indexing in main.c/mcsio2.c, redo covers only the backing-layer validator in mciomanx_backing.c. |
| LAUNCHSTATION-085 | 4 | NONE | — | VMCMAN MAXSLOT 10 costs ~33.8 KB IOP statics over physical build's 4 |
| LAUNCHSTATION-086 | 5 | NONE | — | VMCMAN diagnostics print unconditionally from mcman_open2 create path |
| LAUNCHSTATION-087 | 5 | NONE | — | Directory scans re-walk FAT chain from head for every entry |
| LAUNCHSTATION-088 | 2 | NONE | — | 16-byte-chunk manual struct copy open-coded 11 times instead of memcpy |
| LAUNCHSTATION-089 | 2 | NONE | — | PS1 cache write-flag bit computation open-coded 11 times |
| LAUNCHSTATION-090 | 6 | NONE | — | mmce_fs_write returns firmware byte count unclamped, read side clamps |
| LAUNCHSTATION-091 | 6 | MATCH | RE-LAUNCH-016 | Same defect: usbmass_bd trusts any non-zero device sector size at READ CAPACITY ingest and uses it in transfer-length multiplication; old emphasises the size-print divide-by-zero sink, redo the stack-alloca sink — same ingest point and same [512,4096] power-of-two fix. |
| LAUNCHSTATION-092 | 6 | MATCH | RE-LAUNCH-034 | Same defect: mmceman dread receives filename_len bytes directly into dirent->name at mmce_fs.c:1040-1043 with no forced NUL termination. |
| LAUNCHSTATION-093 | 3/6 | NONE | — | Unplug NULL-priv guard covers start-up commands but not scsi data path |
| LAUNCHSTATION-094 | 6 | NONE | — | mmceman dopen stores any firmware fd except 0xff unchecked, open validates |
| LAUNCHSTATION-095 | 4 | NONE | — | use_alarm setting honoured by SIO2 receive paths, ignored by transmit paths |
| LAUNCHSTATION-096 | 1 | NONE | — | mmce_sio2_rx_dma's sole caller is the unbuilt mmcedrv.c |
| LAUNCHSTATION-097 | 6 | MATCH | RE-LAUNCH-006 | Same mechanism: unbounded strcpy/sprintf of CNF values into Misc* char[64] and path fields in loadConfig (config.c:962,975–1003,1040–1067) |
| LAUNCHSTATION-098 | 6 | NONE | — | Editor row-width array written past 2048 entries, no bound |
| LAUNCHSTATION-099 | 6 | MATCH | RE-LAUNCH-014, RE-LAUNCH-025 | Both limbs covered, split in redo: strcpy of disk name/startup into 65/13-byte fields (RE-014, hdl.c:48–49) and nsub sum past 64-entry subs[] (RE-025, hdl.c:43–44) |
| LAUNCHSTATION-101 | 6 | NONE | — | Journal restore trusts on-disk count/sectors, erases journal on partial failure |
| LAUNCHSTATION-102 | 6 | PARTIAL | RE-LAUNCH-025 | Same defect family — on-disk nsub trusted as loop bound/index past the sub array — but redo site is hdl_info (hdl.c/apa.c), old sites are ps2hdd_osd hdd_fio.c/apaRemove |
| LAUNCHSTATION-103 | 6 | NONE | — | hdl_ginfo_write writes back unchecked read; 128-byte copy from 65-byte name |
| LAUNCHSTATION-104 | 6 | MATCH | RE-LAUNCH-036 | Same mechanism: atoi() on server Content-Length overflows before range check (cloud_http.c:596,643–656); redo also bundles chunked case-sensitivity and the dual-parser divergence |
| LAUNCHSTATION-105 | 6 | MATCH | RE-LAUNCH-027 | Same mechanism: ynDialog/nonDialog strcpy unbounded caller text into fixed stack buffers (gui.c:20,111) |
| LAUNCHSTATION-106 | 6 | NONE | — | USB config-descriptor fetch trusts wTotalLength, never checks transferedBytes |
| LAUNCHSTATION-107 | 6 | NONE | — | HDIOC READ/WRITESECTOR unbounded LBA/count; writes bypass APA cache/journal |
| LAUNCHSTATION-108 | 6 | NONE | — | HIOCGETSIZE indexes parts[] with caller u32, no bounds/NULL check |
| LAUNCHSTATION-109 | 6 | NONE | — | ioctl2Transfer end check overflowable; transfer direction caller-supplied, unvalidated |
| LAUNCHSTATION-110 | 6 | NONE | — | APA header injection rewrites 2 MB in place, no rollback or readback verify |
| LAUNCHSTATION-111 | 6 | NONE | — | Journal write has no 126 capacity check; flush ignores journal errors, resets anyway |
| LAUNCHSTATION-112 | 6 | NONE | — | Backing read/write/erase never check page against stored cardsize; file extends |
| LAUNCHSTATION-113 | 6 | PARTIAL | RE-LAUNCH-034 | Same defect family — directory-entry name consumed as C string without forced NUL termination — but redo site is IOP mmceman dirent->name, old sites are EE exploit-installer mcGetDir/fileXioDread consumers |
| LAUNCHSTATION-114 | 6 | PARTIAL | RE-LAUNCH-052 | Same recursive delete()/getFileSize() walks in filer_actions.c: redo covers their ~900 KB-per-level allocation cost, not old's missing MAX_RECURSE depth guard / stack exhaustion |
| LAUNCHSTATION-115 | 6 | NONE | — | OSDMBR journal-overlap guard compiled out by APA_SUPPORT_GPT ifdef in OSD build |
| LAUNCHSTATION-116 | 6 | NONE | — | SMB share editor 96-byte edit buffer silently truncates 128-byte folder field |
| LAUNCHSTATION-117 | 6 | NONE | — | apaRename double-releases fioSema: inner signals plus caller's unconditional signal |
| LAUNCHSTATION-118 | 6, latent | NONE | — | VMCMAN unit & 9 mask aliases units 2–7 onto slots 0/1; slots 2–7 unreachable |
| LAUNCHSTATION-119 | 6 | MATCH | RE-LAUNCH-012 | Same mechanism: GET_XPARAM_DB_SIZE() identically 1, 292-entry database searched over one entry (xparam.h:36, xparam.c:55) |
| LAUNCHSTATION-120 | 6 | NONE | — | readHOST elf-list returns hostcount−1, drops last entry without trailing newline |
| LAUNCHSTATION-121 | 6 | NONE | — | MMCE history newest/prune lexicographic order sorts same-second suffixes backwards |
| LAUNCHSTATION-122 | 6 | NONE | — | HDL title rename keyboard capped at 31 despite 65-byte title buffer; truncation |
| LAUNCHSTATION-123 | 6 | NONE | — | loadHdlInfoModule sets have_hdl_info=1 discarding load and bind results |
| LAUNCHSTATION-124 | 6 | NONE | — | filerFreeSpace XFROM branch reports success without MC branch's start/sync checks |
| LAUNCHSTATION-125 | 6 | NONE | — | preloadCNF seek path has no size cap; 128 KB cap only in bypassed fallback |
| LAUNCHSTATION-126 | 6 | MATCH | RE-LAUNCH-050 | Same json_string defects: escaped-backslash closing-quote test, no \uXXXX decode, first-occurrence key match (cloud_google.c:61–107); redo also files the duplicate second copy and silent truncation |
| LAUNCHSTATION-128 | 6 | MATCH | RE-LAUNCH-036 | Same mechanism: case-sensitive strstr "chunked" Transfer-Encoding detection (cloud_http.c:598,659,1140); redo bundles it with the atoi/dual-parser finding |
| LAUNCHSTATION-129 | 6 | PARTIAL | RE-LAUNCH-081 | IPv4 limb overlaps only: redo files three divergent IPv4 validators incl. atoi-per-octet with no range check (different sites); old's overflow-before-clamp in theme parseColor/parseInt and dash_widgets ipParse is not covered |
| LAUNCHSTATION-130 | 6 | NONE | — | fioInputBreaker accepts field exactly buffer-length, leaving ID/password unterminated |
| LAUNCHSTATION-131 | 6 | NONE | — | ps2hdd_osd start args admit 0/negatives for -o, no maximum for -n |
| LAUNCHSTATION-132 | 6 | PARTIAL | RE-LAUNCH-033 | Same hdl_info apa.c:162–163 walk and same start+length wrap limb; redo's main mechanism is the cycle/10,000-count hack, and old's exact-end (< vs <=) exclusion limb is not in redo |
| LAUNCHSTATION-133 | 6 | NONE | — | Recycled hub buffer keeps previous hub's bNbrPorts on short descriptor |
| LAUNCHSTATION-134 | 6 | NONE | — | Isochronous halted-ED cleanup clears wrong LUT slot, dangling request |
| LAUNCHSTATION-135 | 6 | NONE | — | Three MMCE setters transmit uninitialised stack bytes to the card |
| LAUNCHSTATION-136 | 6 | NONE | — | ds34bt DS4 report zeroed at DS3 length, sent at DS4 length; 29 uninitialised bytes |
| LAUNCHSTATION-137 | 6 | NONE | — | KELF signing key material never scrubbed from stack copies |
| LAUNCHSTATION-138 | 6, latent | PARTIAL | RE-LAUNCH-035 | Same vmcman backing slot-validation family in mciomanx_backing.c: redo covers the validator's > vs >= / negative-slot comparison only; old's mount cleanup clear_slot on rejected slots and re-mount live-fd overwrite are not in redo |
| LAUNCHSTATION-139 | 6 | NONE | — | Four apaCacheAlloc() results dereferenced without NULL check |
| LAUNCHSTATION-140 | 6, latent | NONE | — | usbd _start args unclamped, feed unchecked allocation; AllocSysMemory memset before NULL check |
| LAUNCHSTATION-141 | 6 | MATCH | RE-LAUNCH-015 | Same mechanism: ps2ips do_send/do_sendto trust EE malign (negative offset, length underflow); redo bundles send and recv limbs in one finding |
| LAUNCHSTATION-142 | 6 | MATCH | RE-LAUNCH-015 | Same mechanism: ps2ips do_recv/do_recvfrom never reject negative EE length (the recv limb of RE-LAUNCH-015) |
| LAUNCHSTATION-143 | 6 | MATCH | RE-LAUNCH-014 | Same core mechanism: EE-supplied NewName strcpy'd into game->name with no termination guarantee in hdl_info rename; scope delta — old's RPC-handler-ignores-Size limb is not in redo |
| LAUNCHSTATION-144 | 6, latent | PARTIAL | RE-LAUNCH-014 | Same defect family — unbounded strcpy into fixed HDL name fields — but old site is the EE wrappers in hdl_rpc.c (33/64-byte packet fields), redo's rename coverage is the IOP handler side |
| LAUNCHSTATION-145 | 6 | NONE | — | Editor navigation rules read row-width entries at i−1/i+1 and past last row |
| LAUNCHSTATION-146 | 6 | NONE | — | SETTING mallocs dereferenced without NULL check in initConfig and config() |
| LAUNCHSTATION-147 | 6, latent | NONE | — | des_cbc_crypt fixed 8-byte block copies with no size validation |
| LAUNCHSTATION-148 | 6, latent | NONE | — | Scene-override stack increments depth past 8 without storing; pop desyncs |
| LAUNCHSTATION-149 | 6, minor | NONE | — | dashPngDecode dereferences out-params without JPEG sibling's NULL checks |
| LAUNCHSTATION-150 | 6 | NONE | — | smap_transmit retries full/dead TX queue forever, no timeout or stop check |
| LAUNCHSTATION-151 | 6 | NONE | — | ds34bt reset waits forever for pads leaving RUNNING |
| LAUNCHSTATION-152 | 5 | NONE | — | usbd descriptor short read re-issues request forever unbounded |
| LAUNCHSTATION-153 | 5 | NONE | — | waitUntilTimer busy-spins EE during clipboard source wait |
| LAUNCHSTATION-154 | 5 | NONE | — | pio_profile validation reads trigger full driver power-cycle restart |
| LAUNCHSTATION-155 | 1/5 | NONE | — | pcdiag sampling dead work costs ~700ms every start |
| LAUNCHSTATION-156 | 5 | NONE | — | Input DES encryption runs though passwords never verified |
| LAUNCHSTATION-157 | 1 | NONE | — | HandleRxIntr never counts packets; link-check reset dead |
| LAUNCHSTATION-158 | 4 | MATCH | RE-LAUNCH-053 | Aligns: both file the two private mcDir[2049] statics in readMC/readXFROM as shareable; same sites |
| LAUNCHSTATION-159 | 4 | NONE | — | PIO tuning reserves 8KiB static for one-shot check |
| LAUNCHSTATION-160 | 6/8 | NONE | — | MINI_DRIVER 512-byte descriptor cap silently drops devices |
| LAUNCHSTATION-161 | 2 | NONE | — | Three mmce get commands are identical pasted functions |
| LAUNCHSTATION-162 | 2 | NONE | — | Classic New Icon flow duplicates filerCreateFolderIcon unchecked |
| LAUNCHSTATION-163 | 2 | NONE | — | Three recursive directory walks duplicated in mmce_backup.c |
| LAUNCHSTATION-164 | 2 | NONE | — | Theme-folder read/load duplicate same routine; mkdir preamble repeated |
| LAUNCHSTATION-165 | 2 | NONE | — | getExternalFilePath re-implements genFixPath HDD/DVR transform |
| LAUNCHSTATION-166 | 2 | NONE | — | resetScreen and resetDisplayThemeDefaults duplicate classic reset block |
| LAUNCHSTATION-167 | 2 | NONE | — | PNG decoder open-codes flush-row block three times |
| LAUNCHSTATION-168 | 2 | PARTIAL | RE-LAUNCH-082 | In-file dash_clock_sync BCD duplication aligns; redo omits old's cross-file bcdToInt copies in main_title/main_history |
| LAUNCHSTATION-169 | 2/7 | MATCH | RE-LAUNCH-073 | Aligns: pccf target duplicated root vs iop/pccf/ps2.yaml; redo broader, also covers ps2ips_rpc duplication |
| LAUNCHSTATION-170 | 2 | NONE | — | pathApplied sets ESR/OSDSYS LK_Flag twice redundantly |
| LAUNCHSTATION-171 | 2 | NONE | — | CNF save writes PathPad_Lock key twice |
| LAUNCHSTATION-172 | 1/7 | NONE | — | hdd_blkio.c compiles empty; dead BDM/vHDD backends carried |
| LAUNCHSTATION-173 | 1+6 | NONE | — | Four libhdd entry points uncalled; walker strcpy overflow |
| LAUNCHSTATION-174 | 1 | NONE | — | load_ps2atad_stack DVRP loader has no caller |
| LAUNCHSTATION-175 | 1 | NONE | — | Six settings IDs have handlers but no page row |
| LAUNCHSTATION-176 | 6 | NONE | — | hdl_glist_write leaks games array on every rename |
| LAUNCHSTATION-177 | 1 | NONE | — | Duplicated dead DTL check, MagicGate block, mmce_igr include |
| LAUNCHSTATION-178 | 1 | NONE | — | Four usbd imports declared but never called |
| LAUNCHSTATION-179 | 1 | NONE | — | ata_bd imports bdm_disconnect_bd but never calls it |
| LAUNCHSTATION-180 | 1 | NONE | — | usbmass_bd imports sceUsbdSetPrivateData but never calls it |
| LAUNCHSTATION-181 | 1 | NONE | — | pccf declares I_memcpy import but never calls memcpy |
| LAUNCHSTATION-182 | 1 | MATCH | RE-LAUNCH-088 | Aligns: commented-out WriteFont_C block in draw.c carried as dead code; same site and size |
| LAUNCHSTATION-183 | 7 | NONE | — | Commented CodeBreaker filter pasted twice in filer.c |
| LAUNCHSTATION-184 | 7 | NONE | — | submenu GetSize carries commented-out debug scaffolding |
| LAUNCHSTATION-185 | 6 | PARTIAL | RE-LAUNCH-093 | argc==3 fall-through aligns; redo omits old's second limb, pair loop dropping the trailing pair |
| LAUNCHSTATION-186 | 6 | MATCH | RE-LAUNCH-001 | Aligns: IOP LCG random() seeded time^pid^pointer feeding NTLM challenge, salt and seal nonce; same sites |
| LAUNCHSTATION-187 | 6 | MATCH | RE-LAUNCH-009 | Aligns: test_copy_psu_listing.py deterministic failure at the pin in CI; same test and stage |
| LAUNCHSTATION-188 | 6 | NONE | — | libsmb2 notify list freed by server-controlled recursion depth |
| LAUNCHSTATION-189 | 6 | NONE | — | Four libsmb2 client error paths leak callback allocations |
| LAUNCHSTATION-190 | 6 | NONE | — | AES-CCM tag verification uses early-exit memcmp timing leak |
| LAUNCHSTATION-191 | 2 | MATCH | RE-LAUNCH-051 | Aligns: brace-extractor copy-pasted into 103 test files; redo lacks old's 71-variant / ~1,420-line census |
| LAUNCHSTATION-192 | 1 | PARTIAL | RE-LAUNCH-057 | Same empty-by-#ifdef TU family in the smbd build, different files: redo files quic/smbdirect, old files sha1.c |
| LAUNCHSTATION-193 | 7 | NONE | — | AllowDVDV secrman import hidden in C, not imports.lst |
| LAUNCHSTATION-194 | 7 | MATCH | RE-LAUNCH-045, RE-LAUNCH-072 | Aligns across a redo split: RE-045 covers the two orphaned tests, RE-072 covers nfsman_live unwired; together the full mechanism |
| LAUNCHSTATION-195 | 1 | NONE | — | ds34 drivers import sceUsbdSetPrivateData but never call it |
| LAUNCHSTATION-196 | 1 | NONE | — | ps2ips_rpc imports sceSifInitRpc but never calls it |
| LAUNCHSTATION-197 | 1 | NONE | — | ps2host imports puts but never calls it |
| LAUNCHSTATION-198 | 7 | NONE | — | ds34 irx_imports.h declares unused libraries; header unincluded |
| LAUNCHSTATION-199 | 2 | NONE | — | xparam database duplicates SLPM_660.22 row consecutively |
| LAUNCHSTATION-200 | 6 | NONE | — | Template Misc trailing slash double-slashes, breaks FileBrowser shortcut |
| LAUNCHSTATION-201 | 7 | MATCH | RE-LAUNCH-048 | Aligns: cloud.html states auto sync off, code default is local backup on; redo also covers the NATIVE_CLOUD.md site |
| LAUNCHSTATION-202 | 7 | MATCH | RE-LAUNCH-049 | Aligns: dashboard.md treats LAUNCHELF.CNF as live at five sites, code writes LAUNCHSTATION.CNF; redo also covers the template header |
| LAUNCHSTATION-203 | 7 | NONE | — | Docs claim 128KB CNF rejected; normal path uncapped |
| LAUNCHSTATION-204 | 7 | NONE | — | Sort note wrong count, default, ELF grouping |
| LAUNCHSTATION-205 | 7 | NONE | — | Themes page inverts panel transparency meaning |
| LAUNCHSTATION-206 | 7 | MATCH | RE-LAUNCH-066 | Aligns: ecosystem.html denies the All-saves view and batch saves that the code ships; same page and claims |
| LAUNCHSTATION-207 | 7 | NONE | — | Cloud doc caps HTTP body 64KiB; code 256KiB |
| LAUNCHSTATION-208 | 7 | NONE | — | Docs require CNF_version first line; parser accepts anywhere |
| LAUNCHSTATION-209 | 7 | NONE | — | Docs claim PSX has i.Link; code returns none |
| LAUNCHSTATION-210 | 7 | MATCH | RE-LAUNCH-063 | Aligns: template comment promises Triangle=About while the empty LK_Triangle_E1 overrides the default; same lines |
| LAUNCHSTATION-211 | 7 | NONE | — | Template misnames title gate Menu_Hide_Paths for Show_Titles |
| LAUNCHSTATION-212 | 7 | NONE | — | Neutrino comment omits SMB backend, misstates -qb flag |
| LAUNCHSTATION-213 | 7 | NONE | — | CHANGELOG misstates SMB share file and smb1 path |
| LAUNCHSTATION-215 | 7 | NONE | — | Plan claims 16KiB copy chunks; code uses 128KiB |
| LAUNCHSTATION-216 | 7 | NONE | — | Themes page omits third font emotion_engine |
| LAUNCHSTATION-217 | 7 | NONE | — | Docs omit TV mode 6 576i from lists |
| LAUNCHSTATION-218 | 7 | NONE | — | Docs deny root LICENSE exists; scoped LICENSE tracked |
| LAUNCHSTATION-219 | 7 | PARTIAL | RE-LAUNCH-061 | CA bundle 2-vs-3 roots aligns at res/README.md; redo omits old's THIRD_PARTY_CREDITS.md site |
| LAUNCHSTATION-220 | 7 | MATCH | RE-LAUNCH-067 | Aligns: move-manifest.md labels the merged feature not merged; redo also covers other stale plan statuses and the docs index |
| LAUNCHSTATION-221 | 7 | PARTIAL | RE-LAUNCH-068 | Largest-files omission aligns for dash_card_tool.c; redo omits the filer.c omission and old's workflow-table limb |
| LAUNCHSTATION-222 | 7 | NONE | — | Wiki cites retained Makefile; no Makefile exists |
| LAUNCHSTATION-223 | 7 | NONE | — | Feedback doc claims wallpaper string exists; string absent |

---

## 3. Adjudication

### 3.A Category 6 NONE/PARTIAL (76 items)

"Old verify" is the old corpus's own adversarial verdict on the item (phase 1 / full verification). Ruling REAL = the mechanism is confirmed and the redo missed it despite declared coverage; COVERAGE = the mechanism is confirmed but sits inside the redo's declared files-not-read.

| Old ID | Match | Old verify | Ruling | Evidence / coverage basis |
|---|---|---|---|---|
| LAUNCHSTATION-015 |  6  | WEAKENED | REAL (PARTIAL stands) | PARTIAL stands. Old verification WEAKENED (concrete config-save overflow unreachable; hygiene core stands). Redo files the load-side (RE-006) and filer (RE-017) limbs; the save-chain limb's only redo counterpart is the withdrawn RE-007 |
| LAUNCHSTATION-016 |  6  | VERIFIED | REAL | cross-cutting pattern across EE files in redo read scope (cloud_google.c read by the network section; editor.c read); not a declared gap |
| LAUNCHSTATION-033 |  6  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands (REAL). Redo RE-004 covers the anonymous-FTP transport + EXECUTAR limb; old-033's FTP launch-configuration mechanism is the same defect from the config side |
| LAUNCHSTATION-035 |  6  | VERIFIED-DRIFT | REAL | redo IOP ledger: ps2host net_fio.c read/write segmentation read — the exact cited range (:406-418) is in scope and unfiled |
| LAUNCHSTATION-037 |  6  | VERIFIED | REAL | ps2ftpd FtpCommands.c parsing read (redo IOP ledger, Partial) — PORT handler in scope, unfiled |
| LAUNCHSTATION-038 |  6  | VERIFIED | REAL | ps2ftpd read, as -037 |
| LAUNCHSTATION-040 |  1/6  | VERIFIED | REAL | ps2ftpd + EE init read; redo filed RE-004 on the same FTP surface but not the dead credential path |
| LAUNCHSTATION-048 |  6/1  | WEAKENED | REAL | redo read the exact site (net_fio.c:308-310) and documented the same wrong-constant assignment as a latent trap in its IOP ledger ("noted here so it is on record without inflating the candidate count") — a deliberate non-filing, not an oversight; old verification WEAKENED it to the same latent scope. Mechanism confirmed by both sides; coordinator spot-read hostlink.h constants context via the redo ledger |
| LAUNCHSTATION-051 |  6  | VERIFIED | REAL | iop/smbman deep-read (redo IOP ledger); prepare_path in scope, unfiled |
| LAUNCHSTATION-056 |  6/2  | VERIFIED | REAL | iop/smbman/smb_fio.c in deep-read scope, unfiled |
| LAUNCHSTATION-057 |  6  | VERIFIED | REAL | iop/mmceman deep-read incl. devctl dispatch (redo IOP ledger); site mmce_fs.c:1340-1400 in scope, unfiled |
| LAUNCHSTATION-058 |  6  | VERIFIED | REAL | smb_fio.c in deep-read scope, unfiled |
| LAUNCHSTATION-059 |  6  | VERIFIED | REAL | smb_fio.c in deep-read scope, unfiled |
| LAUNCHSTATION-060 |  6  | VERIFIED | REAL | smb_fio.c in deep-read scope, unfiled |
| LAUNCHSTATION-063 |  6  | VERIFIED | REAL | smb_fio.c in deep-read scope, unfiled |
| LAUNCHSTATION-064 |  6/5  | VERIFIED | REAL | iop/smbd glue read by the redo (eight findings filed there); smbd.c:38-66 allocator in scope, unfiled |
| LAUNCHSTATION-066 |  6  | VERIFIED | REAL | smb_fio.c devctl in deep-read scope, unfiled |
| LAUNCHSTATION-069 |  6  | VERIFIED | REAL | smb_fio.c in deep-read scope, unfiled |
| LAUNCHSTATION-081 |  6  | VERIFIED | REAL | iop/vmcman/src/main.c deep-read on parsing (redo IOP ledger); mcman_getdirinfo in scope, unfiled |
| LAUNCHSTATION-082 |  6  | VERIFIED | COVERAGE | site `iop/vmcman/src/ps1mc_fio.c` — redo IOP ledger records ps1mc_fio.c/ps2mc_fio.c as skimmed (fixed-record glue), not deep-read |
| LAUNCHSTATION-083 |  6  | VERIFIED | REAL | site iop/vmcman/src/main.c:838-875 (McWrite) in deep-read scope; coordinator spot-check at the pin confirms fd/status/wrflag checks only in the shown head, length forwarded per the finding. (Second path in ps2mc_fio.c was skimmed — primary mechanism is in the read file) |
| LAUNCHSTATION-084 |  6  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands (REAL). Redo RE-035 covers only the backing-layer slot validator (old-068's site); old-084's library-export port/slot scope is unmatched |
| LAUNCHSTATION-090 |  6  | VERIFIED | REAL | iop/mmceman mmce_fs.c read; return-count path in scope, unfiled |
| LAUNCHSTATION-093 |  3/6  | VERIFIED-DRIFT | REAL | iop/usbmass_bd scsi.c read (redo IOP ledger, Deep on SCSI layer); guard-coverage gap in scope, unfiled |
| LAUNCHSTATION-094 |  6  | VERIFIED | REAL | mmce_fs.c read; dopen in scope, unfiled |
| LAUNCHSTATION-098 |  6  | VERIFIED | COVERAGE | site `src/editor_rules.c` — ee-core §A.4: not opened by any redo section |
| LAUNCHSTATION-101 |  6  | VERIFIED | COVERAGE | site `iop/ps2hdd_osd/journal.c` — redo declares journal.c deep internals skimmed/grep-only |
| LAUNCHSTATION-102 |  6  | VERIFIED-DRIFT | REAL (PARTIAL stands) | PARTIAL stands (REAL). Redo RE-025 covers the hdl_info limb; the ps2hdd_osd hdd_fio.c limbs (a file the redo read) are unmatched |
| LAUNCHSTATION-103 |  6  | VERIFIED | REAL | iop/hdl_info read by the redo (RE-014/-025 filed there); hdl.c:108-123 in scope, unfiled |
| LAUNCHSTATION-106 |  6  | VERIFIED | COVERAGE | site `iop/usbd-ra/src/hub.c` — usbd-ra declared skim/not-read beyond length sites; redo skim note covers the adjacent wTotalLength bound, not the unchecked-transferedBytes limb |
| LAUNCHSTATION-107 |  6  | VERIFIED | REAL | iop/ps2hdd_osd hdd_fio.c devctl read (redo IOP ledger); coordinator spot-check at the pin: HDIOC_READSECTOR/WRITESECTOR at hdd_fio.c:1136-1146 pass caller lba/size/pointer straight to blkIoDmaTransfer — mechanism exact |
| LAUNCHSTATION-108 |  6  | VERIFIED | REAL | hdd_fio.c read, as -107 |
| LAUNCHSTATION-109 |  6  | VERIFIED | REAL | hdd_fio.c read, as -107 |
| LAUNCHSTATION-110 |  6  | VERIFIED | COVERAGE | site `src/hdd_header_injector.c` — ee-core §A.4: not opened |
| LAUNCHSTATION-111 |  6  | VERIFIED | COVERAGE | site `iop/ps2hdd_osd/journal.c` — as -101 |
| LAUNCHSTATION-112 |  6  | VERIFIED | REAL | iop/vmcman/src/mciomanx_backing.c read in full (redo IOP ledger); page-vs-cardsize bound absent there, unfiled (distinct mechanism from RE-035's slot validator in the same file) |
| LAUNCHSTATION-113 |  6  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands; mechanism REAL (old VERIFIED). The unmatched limb sits in `src/main_exploit_installer.c`, read by the redo only at grep-verified caller lines (final list) — unmatched limb = COVERAGE |
| LAUNCHSTATION-114 |  6  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands (REAL). Redo RE-052 files the same walks' allocation cost; the depth-guard mechanism is unmatched, and the site (filer_actions.c:1944-2047) contains a range the redo did read (:1980) |
| LAUNCHSTATION-115 |  6  | VERIFIED | REAL | hdd_fio.c read, as -107 |
| LAUNCHSTATION-116 |  6  | VERIFIED | COVERAGE | site `src/dash_settings.c:688-700` — dash_settings.c full body not read (final list + §A.4) |
| LAUNCHSTATION-117 |  6  | VERIFIED | REAL | hdd_fio.c read; coordinator spot-check at the pin: apaRename signals fioSema on its -EEXIST exit (hdd_fio.c:539 area) inside the helper whose caller also signals — mechanism as filed |
| LAUNCHSTATION-118 |  6, latent  | VERIFIED | REAL | iop/vmcman/src/mcdev.c unit mapping read (redo IOP ledger); RE-035 even cites mcman_unit2card — the aliasing mechanism itself unfiled |
| LAUNCHSTATION-120 |  6  | VERIFIED-DRIFT | COVERAGE | site `src/filer.c:1740-1782` — outside ee-core read ranges for filer.c (~340-390, 475-570, 1980-2180) |
| LAUNCHSTATION-121 |  6  | VERIFIED | COVERAGE | site `src/mmce_backup.c` — §A.4 not opened (mmce_*.c bodies beyond cited lines) |
| LAUNCHSTATION-122 |  6  | WEAKENED | COVERAGE | site `src/dash_hdd.c:700-727` — §A.4 not opened (dash_*.c beyond cited ranges) |
| LAUNCHSTATION-123 |  6  | VERIFIED | REAL | src/init.c opened by ee-core (read ledger lists init.c, 3205 lines); loadHdlInfoModule at :2563-2573 unfiled |
| LAUNCHSTATION-124 |  6  | VERIFIED | REAL | src/filer_browser.c opened by ee-core (2528 lines, read ledger); XFROM branch :2480-2487 unfiled |
| LAUNCHSTATION-125 |  6  | VERIFIED | COVERAGE | site `src/config.c:435-463` — outside ee-core read ranges for config.c (38-162, 533-719, 793-1161, 1492-1530) |
| LAUNCHSTATION-129 |  6  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands (REAL). Redo RE-081 covers the IPv4-parser limb; the theme-parser (dash_themes.c) limb is unmatched |
| LAUNCHSTATION-130 |  6  | VERIFIED | REAL | site iop/ps2hdd_osd/hdd_fio.c:107-127 — the redo IOP ledger expressly records "hdd_fio.c devctl/ioctl2 and input breaker read"; fioGetInput is that input breaker, in scope, unfiled |
| LAUNCHSTATION-131 |  6  | VERIFIED | REAL | iop/ps2hdd_osd/hdd.c in read scope (module Partial, hdd.c not among the skimmed pair), unfiled |
| LAUNCHSTATION-132 |  6  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands (REAL). Same APA family as RE-033 (hdl chain walk) but a different function/mechanism (apa_ptable_read_ex end-of-disk wrap, hdl_info/apa.c:162-163) |
| LAUNCHSTATION-133 |  6  | VERIFIED | COVERAGE | site `iop/usbd-ra/src/hub.c` — usbd-ra declared skim/not-read |
| LAUNCHSTATION-134 |  6  | VERIFIED | COVERAGE | site `iop/usbd-ra/src/usbd.c` — as -133 |
| LAUNCHSTATION-135 |  6  | VERIFIED | REAL | iop/mmceman mmce_cmds.c read; the three setters in scope, unfiled |
| LAUNCHSTATION-136 |  6  | VERIFIED | COVERAGE | site `iop/ds34bt/iop/ds34bt.c` — full event parser not read in full (final list); LED/report path in the unread remainder. Spot-checked at the pin by the coordinator: DS4 branch memsets PS3 length (:1243) into the PS4-sized buffer (:1208) and sends at PS4 length (:1263) — mechanism exact |
| LAUNCHSTATION-137 |  6  | VERIFIED | COVERAGE | site `src/kelf_signer.c` — §A.4: not opened by any redo section |
| LAUNCHSTATION-138 |  6, latent  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands (REAL). Redo RE-035 covers the slot-validator limb in the same file (mciomanx_backing.c, read in full by the redo); the live-fd-overwrite limb is unmatched |
| LAUNCHSTATION-139 |  6  | VERIFIED | REAL | iop/ps2hdd_osd/apa.c read (header fill + cache paths; only journal.c/password.c were skimmed); apaCacheAlloc sites unfiled |
| LAUNCHSTATION-140 |  6, latent  | VERIFIED | COVERAGE | site `iop/usbd-ra/src/usbd.c` — as -133 |
| LAUNCHSTATION-144 |  6, latent  | VERIFIED | REAL (PARTIAL stands) | PARTIAL stands; mechanism REAL (old VERIFIED). The unmatched EE-wrapper limb sits in `src/hdl_rpc.c`, not opened by any redo section (§A.4) — unmatched limb = COVERAGE |
| LAUNCHSTATION-145 |  6  | VERIFIED-DRIFT | COVERAGE | site `src/editor_rules.c` — not opened, as -098 |
| LAUNCHSTATION-146 |  6  | VERIFIED | REAL | both sites inside ee-core read ranges for config.c (initConfig 793-883 covers :797-801; config() entry 1492-1530 covers :1511-1513), unfiled |
| LAUNCHSTATION-147 |  6, latent  | WEAKENED | COVERAGE | site `src/kelf_signer.c:396-423` — not opened, as -137 (old verification WEAKENED: narrowed scope stands) |
| LAUNCHSTATION-148 |  6, latent  | VERIFIED | COVERAGE | site `src/dash_main.c:1159-1175` — dash_*.c beyond cited ranges (redo cites dash_main.c:389-485 only) |
| LAUNCHSTATION-149 |  6, minor  | VERIFIED | COVERAGE | site `src/dash_png.c` — §A.4 not opened |
| LAUNCHSTATION-150 |  6  | VERIFIED | REAL | site iop/udpfs_smap/src/xfer.c:249-271 — redo IOP ledger records xfer.c RX/TX paths read (module rated Skim overall; the retry loop sits in the read TX path), unfiled |
| LAUNCHSTATION-151 |  6  | VERIFIED | COVERAGE | site `iop/ds34bt/iop/ds34bt.c` — as -136 |
| LAUNCHSTATION-160 |  6/8  | WEAKENED | COVERAGE | site `iop/usbd-ra/src/usbd.c` (MINI_DRIVER delta) — as -133 (old verification WEAKENED: narrowed scope stands) |
| LAUNCHSTATION-173 |  1+6  | VERIFIED | COVERAGE | Cat-6 limb sits in `ee_libs/hdd/libhdd.c` — redo final list: libhdd.c body grep/script only |
| LAUNCHSTATION-176 |  6  | VERIFIED | REAL | iop/hdl_info/hdl.c read, as -103 |
| LAUNCHSTATION-185 |  6  | WEAKENED | REAL (PARTIAL stands) | PARTIAL stands. Old verification WEAKENED (narrowed scope stands). Redo RE-093 (a verification-phase NEW finding) covers the argc==3 limb; the trailing-pair limb is unmatched |
| LAUNCHSTATION-188 |  6  | VERIFIED | COVERAGE | site libsmb2 `lib/libsmb2.c:4198-4252` — client-only middles of libsmb2.c pattern-scanned only (final list) |
| LAUNCHSTATION-189 |  6  | VERIFIED-DRIFT | COVERAGE | sites libsmb2 `lib/libsmb2.c` client async paths — as -188 |
| LAUNCHSTATION-190 |  6  | VERIFIED | COVERAGE | site libsmb2 `lib/aes128ccm.c` — crypto internals not read line-by-line (final list) |
| LAUNCHSTATION-200 |  6  | VERIFIED | REAL | code site src/config.c:973 sits inside ee-core read range 883-1161 (loadConfig), and the redo docs pass checked LAUNCHELF.CNF claims; coordinator spot-check at the pin: config.c:973 appends "/" unconditionally and docs/LAUNCHELF.CNF:52 ships `Misc = MISC/` — mechanism exact, unfiled |

### 3.B High-benefit non-Category-6 NONE (Effort M/L, 20 items)

| Old ID | Cat | Effort | Ruling | Evidence / coverage basis |
|---|---|---|---|---|
| LAUNCHSTATION-001 | 4 | M | REAL | Old VERIFIED. Redo read the language subsystem (its Cat-4 pass filed only 5 items repo-wide); the compile-in-all-nine-languages mechanism is unfiled |
| LAUNCHSTATION-002 | 4 | M | REAL | Old VERIFIED (byte total re-summed in old verification). Redo RE-055 files the font C provenance/gate mechanism — a different mechanism; the 585,728 B .rodata size mechanism is unfiled |
| LAUNCHSTATION-003 | 2/8 | M | REAL | Old VERIFIED-DRIFT. Legacy stack files in tree scope; no redo counterpart |
| LAUNCHSTATION-004 | 8/4 | L | REAL | Old STANDING (INFERRED in part). Redo read the cloud subsystem (RE-036/-037/-038) but filed no build-composition/removal finding |
| LAUNCHSTATION-005 | 2 | M | REAL | Old VERIFIED. smbman and smb2man both deep-read by the redo; the client-duplication mechanism is unfiled |
| LAUNCHSTATION-006 | 8/2 | M | REAL | Old STANDING. Redo read smbd deeply (defect findings) but filed no embedding/bloat finding |
| LAUNCHSTATION-007 | 8 | L | REAL | Old STANDING. The claim is build-composition level (installer + payloads compiled into the everyday ELF) — determinable from build files and the grep-verified call sites the redo did check; not a body-read gap |
| LAUNCHSTATION-012 | 2 | L | REAL | Old STANDING. src/font_uLE.c is in ee-core's read ledger (652 lines); the third-text-path mechanism is unfiled |
| LAUNCHSTATION-017 | 4/5 | M | REAL | Old STANDING. self_update.c was read by the redo (RE-005); RE-005 acknowledges the RAM hold/verify but files the in-place-overwrite mechanism (old-034) — the 16 MB RAM sink as itself the defect is unfiled |
| LAUNCHSTATION-026 | 2/8 | L | REAL | Old STANDING. Launcher-backend sources in tree scope; no redo counterpart |
| LAUNCHSTATION-039 | 3/5 | M | COVERAGE | Sites in src/dash_card_tool.c (ee-core §A.4: not opened) and the card-tool copy path; the ten-copy/prune-rescan mechanism sits in the unread dashboard/card files |
| LAUNCHSTATION-042 | 2 | M | REAL | Old STANDING. Redo IOP ledger: smb2man glue incl. parse_url deep-read, nfsman in scope; the duplicated URL-parser/connect-skeleton mechanism is unfiled |
| LAUNCHSTATION-043 | 2 | L | REAL | Old STANDING. Sites across smbman/smb2man/nfsman, all deep-read by the redo; the reply-collect/MD4/UTF-16 duplication mechanism is unfiled |
| LAUNCHSTATION-062 | 5 | M | COVERAGE | DMA_sendEE sits in the smap driver — iop/smap/smap.c not read (final list); udpfs_smap/smap.c likewise outside the read xfer.c paths |
| LAUNCHSTATION-071 | 2 | M | COVERAGE | Site src/mc_card_export.c — src/mc_*.c bodies not opened (§A.4 / final list) |
| LAUNCHSTATION-073 | 4 | M | COVERAGE | Sites src/mc_card_export.c:20-22 and src/dash_card_tool.c:43-48 — both files not opened (§A.4) |
| LAUNCHSTATION-162 | 2 | M | REAL | Old STANDING. Both copies are in src/filer_browser.c:1460-1501 and :2420-2458 — a file ee-core read in full (2528 lines); the unchecked-duplicate mechanism is unfiled. Coordinator confirmed locations from the old body |
| LAUNCHSTATION-163 | 2 | M | COVERAGE | Site src/mmce_backup.c — not opened (§A.4) |
| LAUNCHSTATION-165 | 2 | M | COVERAGE | Duplication limb in src/init.c (opened) but the canonical copy, genFixPath at src/filer.c:1029-1073, sits outside ee-core's read ranges for filer.c (~340-390, 475-570, 1980-2180) — the comparison the finding rests on was not available to the redo |
| LAUNCHSTATION-172 | 1/7 | M | REAL | Old STANDING. hdd_blkio.c build-list membership is script/build level (the redo censused build glue); the compiles-to-nothing mechanism is unfiled |

### 3.C Remaining non-Category-6 NONE (Effort S/XS/Trivial, 71 items) — scope classification

| Old ID | Class | Basis |
|---|---|---|
| LAUNCHSTATION-010 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-011 | COVERAGE | src/dash_sound.c not opened (§A.4) |
| LAUNCHSTATION-018 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-019 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-021 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-022 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-023 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-027 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-030 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-031 | COVERAGE | src/qrcodegen.c body not read — only its dashboard call site checked (§A.4) |
| LAUNCHSTATION-044 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-045 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-046 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-047 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-049 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-052 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-074 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-075 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-076 | COVERAGE | src/dash_browser.c not opened (§A.4) |
| LAUNCHSTATION-077 | COVERAGE | src/mmce_backup.c not opened (§A.4) |
| LAUNCHSTATION-078 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-085 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-086 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-087 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-088 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-089 | COVERAGE | iop/vmcman/src/ps1mc_fio.c skimmed (redo IOP ledger) |
| LAUNCHSTATION-095 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-096 | COVERAGE | iop/mmceman/src/mmce_sio2.c skimmed (transport/glue, redo IOP ledger) |
| LAUNCHSTATION-152 | COVERAGE | iop/usbd-ra skim/not-read |
| LAUNCHSTATION-153 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-154 | COVERAGE | iop/pccf deep internals skimmed |
| LAUNCHSTATION-155 | COVERAGE | iop/pccf deep internals skimmed |
| LAUNCHSTATION-156 | COVERAGE | iop/ps2hdd_osd/password.c grep-skim only |
| LAUNCHSTATION-157 | COVERAGE | smap register driver not read (final list) |
| LAUNCHSTATION-159 | COVERAGE | iop/pccf deep internals skimmed |
| LAUNCHSTATION-161 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-164 | COVERAGE | src/dash_theme.c/dash_themes.c not opened (§A.4) |
| LAUNCHSTATION-166 | COVERAGE | src/dash_settings.c not read (code side of both copies) |
| LAUNCHSTATION-167 | COVERAGE | src/dash_png.c not opened (§A.4) |
| LAUNCHSTATION-170 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-171 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-174 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-175 | COVERAGE | settings dispatch in dash_*.c beyond cited ranges |
| LAUNCHSTATION-177 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-178 | COVERAGE | iop/usbd-ra skim/not-read (import tables in the unread remainder) |
| LAUNCHSTATION-179 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-180 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-181 | COVERAGE | iop/pccf deep internals skimmed |
| LAUNCHSTATION-183 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-184 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-193 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-195 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-196 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-197 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-198 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-199 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-203 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-204 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-205 | COVERAGE | code side (panel-alpha semantics) in src/dash_themes.c, not opened |
| LAUNCHSTATION-207 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-208 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-209 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-211 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-212 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-213 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-215 | COVERAGE | docs/plans sampled only (final list) |
| LAUNCHSTATION-216 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-217 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-218 | COVERAGE | licence texts deliberately excluded from redo audit scope (final list) — the root LICENSE fact sits in that exclusion |
| LAUNCHSTATION-222 | UNEXPLAINED | site in redo read scope (tree census / read files / executed suites); no declared gap covers it |
| LAUNCHSTATION-223 | COVERAGE | code side in src/dash_settings.c, not read |

### 3.D Redo-only findings (41 items)

"Redo verify" is the redo corpus's own adversarial verdict. REAL = mechanism confirmed at the pin and genuinely absent from the old final; UNEXPLAINED = non-Cat-6/8 item whose site the old audit read (rounds 5/7/8 coverage) without filing.

| Redo ID | Cat | Redo verify | Ruling | Evidence / basis |
|---|---|---|---|---|
| RE-LAUNCH-003 | 6 | VERIFIED | REAL | Mechanism absent from the old corpus (keyword: NetShareEnum — no hit in any old report). Coordinator spot-check at the pin: smb.c:736-762 dereferences server ParamOffset/DataOffset and walks with unbounded strlen(p) — exact, and the redo verification notes the first OOB access is even earlier than filed |
| RE-LAUNCH-011 | 6 | VERIFIED | REAL | Keyword absence (throttl/lockout/session_setup_legs) in the old corpus; old-067 files the default login itself, not the missing throttle |
| RE-LAUNCH-018 | 6 | VERIFIED | REAL | scanSystemCnf strncat misuse (main_startup.c) — no old finding files this function/mechanism; old-097/RE-006 cover the config.c load path, a different site |
| RE-LAUNCH-019 | 6 | VERIFIED-DRIFT | REAL | Old corpus's logoff item (old-058) is the SMB1 *client* closeAll; the server-side handle/session binding in smbd_fs.c (logoff_cmd :284, tree_disconnect_cmd :323 landmarks confirmed at the pin) is unfiled in the old corpus |
| RE-LAUNCH-021 | 6 | VERIFIED-DRIFT | REAL | Coordinator spot-check at the pin: dash_clock_sync.c:149-152 — nonce_hi = Timer() ^ 0x574C4554, nonce_lo one LCG step; the whole transmit timestamp is a function of the ms timer, as filed |
| RE-LAUNCH-022 | 6 | VERIFIED-DRIFT | REAL | Coordinator spot-check at the pin: cloud_http.c:125-129 zeroes a failed getrandom result and the poll returns 0 (success) unconditionally at :156-157, as filed |
| RE-LAUNCH-024 | 6 | VERIFIED | REAL | Keyword absence (QueryPathInformation) in the old corpus; smb.c QueryPathInformation parser unfiled by the old audit |
| RE-LAUNCH-028 | 6 | VERIFIED | REAL | Keyword absence (editorSaveAs) in the old corpus |
| RE-LAUNCH-029 | 6 | VERIFIED-DRIFT | REAL | smbd_fs.c write path landmarks confirmed at the pin (:466-486); the negotiated-maximum limb is unfiled in the old corpus |
| RE-LAUNCH-030 | 6 | VERIFIED-DRIFT | REAL | smbd CREATE leaf-limit asymmetry vs RENAME — unfiled in the old corpus |
| RE-LAUNCH-031 | 6 | VERIFIED-DRIFT | REAL | smbd file-id validation (4 of 16 bytes, 16-bit generation wrap) — unfiled in the old corpus |
| RE-LAUNCH-032 | 6 | VERIFIED | REAL | Coordinator-verified from the finding's citations: smbd_fs.c:998 constant GUID memcpy; libsmb2 session_counter reset — unfiled in the old corpus |
| RE-LAUNCH-037 | 6 | VERIFIED | REAL | Custom-OAuth-override pair rule (cloud_google.c) — old-126 files the JSON extractor defects, not the override semantics |
| RE-LAUNCH-038 | 6 | VERIFIED-DRIFT | REAL | Long-lived credential storage in clear-text config on removable media — old-067 covers only the SMB default login; the cloud-token storage mechanism is unfiled |
| RE-LAUNCH-039 | 8 | VERIFIED | REAL | smbd set_info_cmd (:900 landmark confirmed at the pin) reporting success without performing the operation — unfiled in the old corpus (old SET_INFO keyword hits are usmb2-client contexts) |
| RE-LAUNCH-040 | 8 | VERIFIED-DRIFT | REAL | smbd lock_cmd (:491 landmark confirmed) succeeding without locking + CREATE share-access/oplock ignored — unfiled in the old corpus |
| RE-LAUNCH-041 | 8 | VERIFIED | REAL | smbd query_directory_cmd (:542 landmark confirmed) never validating the information class — unfiled in the old corpus |
| RE-LAUNCH-042 | 8 | VERIFIED | REAL | Same code the old corpus filed as LAUNCHSTATION-100 and WITHDREW as refuted (live-walk framing). Coordinator spot-check at the pin: the unbounded memcpy at usmb2.c:791-792 sits inside #ifdef USMB2_FEATURE_OPENDIR (:743-820), defined nowhere — the redo's latent/vendored framing is the correct one and stands; the old final has no actionable counterpart |
| RE-LAUNCH-043 | 1 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-047 | 7 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-055 | 7 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-059 | 7 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-064 | 7 | WEAKENED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss. Redo status REVISED (its verification narrowed the claim) |
| RE-LAUNCH-065 | 7 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-069 | 7 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-070 | 7 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-074 | 2 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-075 | 7 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-076 | 7 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-077 | 7 | WEAKENED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss. Note: the redo's own verification WEAKENED this item to "a caveat, not a countable finding"; it remains REVISED (actionable) in the redo final |
| RE-LAUNCH-079 | 2 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-080 | 2 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-083 | 2 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-084 | 2 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-086 | 5 | WEAKENED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-087 | 1 | VERIFIED | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-089 | 7 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-091 | 4 | VERIFIED-DRIFT | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-094 | 1 | — | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-095 | 7 | — | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |
| RE-LAUNCH-096 | 2 | — | UNEXPLAINED | old corpus read this area at source level (rounds 5/7/8 coverage); no declared old residue covers it — genuine old miss |

---

## 4. Most consequential one-sided findings

### 4.1 Old-only REAL (redo missed them despite coverage)

1. **LAUNCHSTATION-107** (Cat 6) — `HDIOC_READSECTOR`/`HDIOC_WRITESECTOR` take LBA, count and data pointer from the caller with no bound against the disk, and writes bypass the APA cache and journal entirely. Spot-checked at the pin (hdd_fio.c:1136-1146). Arbitrary disk write primitive for any EE caller.
2. **LAUNCHSTATION-083** (Cat 6) — `McWrite` with a negative length reaches `memcpy` as a huge `size_t` in both write paths. Spot-checked at the pin (vmcman main.c:838+).
3. **LAUNCHSTATION-117** (Cat 6) — `apaRename` signals fioSema on three exits and its caller signals again: semaphore double-release on every rename error path. Spot-checked at the pin.
4. **LAUNCHSTATION-059** (Cat 6) — smbman's `lseek64` ignores the SEEK_END offset and accepts negative positions; the 32-bit `lseek` truncates past 2 GiB. The standard tail-read idiom lands at EOF on smb1: — silent data misplacement in a deep-read file.
5. **LAUNCHSTATION-200** (Cat 6) — the shipped example config's `Misc = MISC/` double-slashes on load and breaks its own FileBrowser shortcut: the one function-breaking docs/config finding of the old round 8. Spot-checked at the pin (config.c:973 + docs/LAUNCHELF.CNF:52).

### 4.2 Redo-only REAL (old missed them despite coverage)

1. **RE-LAUNCH-003** (Cat 6) — SMB1 NetShareEnum walks the share list on fully server-controlled offsets, count and string lengths (smb.c:736-762, spot-checked). A hostile SMB1 server drives IOP out-of-bounds reads; the redo verification notes the finding is, if anything, understated.
2. **RE-LAUNCH-019** (Cat 6) — smbd file handles and auth state survive logoff and tree disconnect; handles bind to the connection, not the session. The old corpus filed the client-side analogue (old-058) but never the server side.
3. **RE-LAUNCH-022** (Cat 6) — the TLS entropy callback (`mbedtls_hardware_poll`, cloud_http.c) always reports success, including when its only OS source failed and every other input is at its cold-boot value. Spot-checked at the pin.
4. **RE-LAUNCH-021** (Cat 6) — SNTP clock sync uses a predictable transmit timestamp (Timer() ^ constant, one LCG step — spot-checked at dash_clock_sync.c:149-152), and the RTC it sets is what TLS certificate validation trusts. Chains with RE-LAUNCH-022.
5. **RE-LAUNCH-011** (Cat 6) — no authentication throttling anywhere in the SMB server, against the published default login (old-067). The session-setup leg counter resets on every failed attempt, so attempts per connection are unbounded.

Also notable in the redo-only set: the smbd fake-success cluster RE-LAUNCH-039/-040/-041 (SET_INFO and LOCK report success without performing the operation; QUERY_DIRECTORY never validates the information class) and RE-LAUNCH-042 — the usmb2 readdir hardening item the old corpus filed too broadly as LAUNCHSTATION-100, refuted, and withdrew; the redo's latent framing is the version that survives (see appendix).

### 4.3 NOT-REAL rulings

**None on either side.** Every actionable NONE/PARTIAL item in both finals carries a VERIFIED / VERIFIED-DRIFT / WEAKENED verdict from its own corpus's adversarial verification, and the coordinator's spot-checks at the pin confirmed every contested mechanism sampled. The nearest case is LAUNCHSTATION-048: its mechanism (wrong segment constant in ps2host's write path) is real but its effect is currently unreachable — the old verification WEAKENED it to that latent scope, and the redo, which read the exact lines, documented the same conclusion in its IOP ledger as a deliberate non-filing. It is counted REAL (latent) above, not NOT-REAL.

---

## 5. Coordinator spot-check evidence (code read at the pin for this comparison)

| Item | What was read | Result |
|---|---|---|
| RE-LAUNCH-003 | `iop/smbman/smb.c:736-762` | Server-controlled ParamOffset/DataOffset/AvailableEntries; unbounded `strlen(p)` walk — as filed |
| LAUNCHSTATION-200 | `src/config.c:971-975`; `docs/LAUNCHELF.CNF:50-54` | `sprintf(setting->Misc, "%s/", value)` appends `/` unconditionally; example ships `Misc = MISC/` — as filed |
| LAUNCHSTATION-101 | `iop/ps2hdd_osd/journal.c:58-88` | Restore loops on on-disk `journalBuf.num` into `sectors[i]` with no clamp; journal erased via apaJournalReset even after a mid-loop break — as filed |
| LAUNCHSTATION-107 | `iop/ps2hdd_osd/hdd_fio.c:1136-1146` | HDIOC_READ/WRITESECTOR pass caller lba/size/pointer straight to blkIoDmaTransfer — as filed |
| LAUNCHSTATION-117 | `iop/ps2hdd_osd/hdd_fio.c:531-545` (+ old body) | apaRename signals fioSema on its -EEXIST exit inside the helper — as filed |
| LAUNCHSTATION-136 | `iop/ds34bt/iop/ds34bt.c:1208,1219,1243,1263` | DS4 branch memsets PS3_01_REPORT_LEN+2 into the PS4-sized buffer, sends at PS4 length — as filed |
| LAUNCHSTATION-083 | `iop/vmcman/src/main.c:838-850` | McWrite head checks fd/status/wrflag only — consistent with the filed mechanism |
| RE-LAUNCH-019/-039/-040/-041 | `iop/smbd/smbd_fs.c` landmarks | logoff_cmd :284, tree_disconnect_cmd :323, lock_cmd :491, query_directory_cmd :542, set_info_cmd :900 all present as cited |
| RE-LAUNCH-021 | `src/dash_clock_sync.c:149-152` | nonce_hi = Timer() ^ 0x574C4554; nonce_lo one LCG step — as filed |
| RE-LAUNCH-022 | `src/cloud_http.c:125-129,154-158` | Failed getrandom zeroed and ignored; poll returns 0 unconditionally — as filed |
| RE-LAUNCH-042 | `iop/smb2man/usmb2/usmb2.c:789-795,731-820`; tree-wide grep for USMB2_FEATURE_OPENDIR | Unbounded memcpy present, entirely inside the never-defined `#ifdef USMB2_FEATURE_OPENDIR` — latent, as the redo filed it |

---

## Appendix — items withdrawn in one final

| Item | Status | Relationship to the other final |
|---|---|---|
| LAUNCHSTATION-065 | WITHDRAWN in the old final (refuted in round 4, R4-R2) | No redo counterpart; RE-LAUNCH-029 is the smbd *glue* WRITE-maximum item, a different layer from old-065's libsmb2 write_cmd claim |
| LAUNCHSTATION-100 | WITHDRAWN in the old final (full verification REFUTED the live-walk framing: the code sits inside the never-defined USMB2_FEATURE_OPENDIR with zero callers) | The redo files the same code as RE-LAUNCH-042 with the latent/vendored framing, which survives — counted as redo-only REAL in §3.D |
| LAUNCHSTATION-127 | WITHDRAWN in the old final (REFUTED: escape helpers fail rather than truncate; truncation arithmetically unreachable) | No redo counterpart — the redo likewise does not file it (its RE-LAUNCH-036 covers the separate atoi/chunked-case mechanisms, matched to old-104/-128) |
| LAUNCHSTATION-214 | WITHDRAWN in the old final (REFUTED: shouldHideHddAtaDevices exists at main_startup.c:746 and is consumed; the README claim is substantively correct) | No redo counterpart |
| RE-LAUNCH-007 | WITHDRAWN in the redo final (its own verification REFUTED the overrun arithmetic: allocation 67,190 B vs max full-but-valid output ≈61,038 B) | Its subject (the config save-text builder) is a limb of old LAUNCHSTATION-015; per the matching rules old-015 is PARTIAL via RE-LAUNCH-006/-017, with the save-chain limb recorded unmatched |

---

## Method note on coverage declarations

The redo's consolidated "Files not read" list understates three gaps that its own section ledgers state plainly and that this comparison therefore used: `src/kelf_signer.c`, `src/hdd_header_injector.c`, `src/hdl_rpc.c`, `src/editor_rules.c` and `src/dash_png.c` were not opened by any redo section (§A.4), and `src/config.c`/`src/filer.c` were read only in the ranges the ee-core ledger lists. Conversely the consolidated list's "most of src/dash_*.c beyond cited ranges" is broader than the per-file truth for `src/filer_browser.c` and `src/init.c`, which ee-core did open — items sited there (LAUNCHSTATION-123, -124) are ruled REAL, not COVERAGE. On the old side, no source-level residue in the final's declarations covers any redo-only item: rounds 7-8 closed the libsmb2 client/crypto, tests, build-glue, assembly, xparam and docs-prose residues, leaving only binaries-not-disassembled, hardware behaviour and live-interop classes, none of which any redo-only item occupies.
