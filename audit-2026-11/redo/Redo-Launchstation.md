# Launchstation — Independent Redo Audit (Blind)

- **Repo / pin:** `~/workspace/audit-clones-2026-11-13/Launchstation`, HEAD `9fb0598a79f09b4ff06fdc3078ab26c38d7f4790` (master), verified at start and end; clone git-clean throughout; read-only held (all test executions in `/tmp` copies).
- **Blind rule:** nothing under `~/workspace/Sable-audit/` was read, listed, grepped, or opened at any point. Every finding below derives from the clone's code, docs, and tests only.
- **libsmb2:** not vendored. Pin determined from the repo's own `scripts/fetch_libsmb2.sh`: upstream `a52c1571a1ff223bde5bb3df9fec7af5ce3855d4`, fetched to `/tmp`, with the repo's two patches applied (`libsmb2-session-key-leak.patch` → `lib/libsmb2.c`; `libsmb2-raw-query-info.patch` → `lib/smb2-cmd-query-info.c`; diff = exactly those two files). Integration and patched files audited; unpatched upstream internals bounded in the files-not-read list.
- **Rule:** LESS, NOT MORE — remove / simplify / consolidate / fix; deletion is a valid fix; one mechanism = one finding with all locations.
- **Categories:** 1 dead code · 2 duplication · 3 overengineering · 4 size/memory · 5 performance (structural only) · 6 bugs/security · 7 build/CI/repo clutter · 8 half-finished/redundant features.
- **Totals: 92 findings** (counted from the numbered headings in this file). By category: Cat 1 = 6, Cat 2 = 11, Cat 3 = 1, Cat 4 = 5, Cat 5 = 2, Cat 6 = 38, Cat 7 = 24, Cat 8 = 5.
- **Coverage:** all EE source areas, all IOP modules, headers, the Python test suites (214 host suites executed: 211 pass, 1 deterministic fail, 2 environment-blocked), build/CI, and 176 doc/config claims checked against code (148 match, 19 contradicted, 9 unverifiable). Section working papers: `redo-sections/ee-core.md`, `ee-network-cloud.md`, `iop-smbd-libsmb2.md`, `iop-other.md`, `tests-build-ci.md`, `docs-assets.md`. Nothing is PS2-build- or hardware-verified (no PS2SDK in this VM); Category 5 is structural only.
- **Parent spot-checks:** the coordinator personally re-verified the LCG (compat.c:515), the `(int)off` cast (smbd.c:150-160), the vmcman superblock copy + cluster loop, anonymous FTP + EXECUTAR, and found RE-LAUNCH-012 in the course of checking.

---

## Findings (sorted by benefit)

### RE-LAUNCH-001 — libsmb2 randomness on the IOP is a 15-bit LCG seeded from boot seconds, a constant pid, and a heap pointer
- Category: 6
- Locations: `iop/smbd/config.h:1-16` (no strong-source define; `NEED_RANDOM`/`NEED_SRANDOM` set); `libsmb2/lib/init.c:342-411` (`smb2_random_bytes` falls back to `random() & 0xff`), `:427` (seed `time(NULL) ^ getpid() ^ (uintptr_t)smb2`); `libsmb2/lib/compat.c:509-532` (IOP `random()`: `next = next * 1103515245 + 12345`, 15 bits/call; `getpid()` is constant 27); consumers `libsmb2/lib/ntlmssp.c:1009` (NTLM server challenge — its own comment at :1003-1008 says it "has to be unpredictable"), `libsmb2/lib/init.c:440-443` (pre-auth salt/GUID), `libsmb2/lib/smb3-seal.c:155` (SMB3 seal nonces)
- Problem: every value the library documents as needing unpredictability — server challenge, 3.1.1 pre-auth salt, AES-CCM/GCM seal nonces — is drawn from a 31-bit-state LCG whose seed is largely guessable. One observed challenge or nonce exposes the state; a repeated CCM nonce under one session key breaks both confidentiality and integrity of sealed traffic (the warning is in the code itself, `libsmb2/lib/init.c:327-340`).
- Fix: give `smb2_random_bytes` one real entropy input on the IOP inside `iop/smbd`'s platform layer (strong override or config-selected source), via the existing patch mechanism — not a fork of the library.
- Benefit: High · Effort: M · Risk: Medium · Confidence: READ

### RE-LAUNCH-002 — vmcman overwrites derived geometry with raw superblock bytes; cluster reads then write past 1024-byte cache entries
- Category: 6
- Locations: `iop/vmcman/src/main.c:1391-1393` (0x150-byte superblock copy over `MCDevInfo` after `mcman_setdevspec` derived sane values), `:3766-3773` (`McReadCluster` loops `pages_per_cluster` × `pagesize` into `cl_data`), cache entry size `iop/vmcman/src/mcman-internal.h:110` (`MCMAN_CLUSTERSIZE 1024`); VMC mount validation `iop/vmcman/src/mciomanx_backing.c:116-141` (checks magic/type/pagesize ∈ {512,528} and a size equation, never bounds `pages_per_cluster`)
- Problem: a crafted card or VMC image (e.g. pagesize 512, pages_per_cluster 64) makes every cluster read write 32 KB over a 1024-byte static cache entry and adjacent IOP statics. Physical cards and downloaded VMC files both reach this code.
- Fix: after the superblock copy, re-validate the geometry the driver depends on (`pagesize ∈ {512,528}`, `pages_per_cluster * pagesize == MCMAN_CLUSTERSIZE`, `clusters_per_block != 0`) and reject the card otherwise — one check at the single ingest point.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ (the mount size-equation int-overflow sub-point is INFERRED)

### RE-LAUNCH-003 — SMB1 NetShareEnum walks the share list on fully server-controlled offsets, count, and string lengths
- Category: 6
- Locations: `iop/smbman/smb.c:736-763` (`ParamOffset`, `AvailableEntries`, `DataOffset`, unbounded `strlen` walk over the 4224-byte `SMB_buf`, `iop/smbman/smb.h:900-901`); callers `iop/smbman/smb_fio.c:1021,1031`
- Problem: none of the reply's offsets or the entry count is checked against the received length. A hostile or broken SMB1 server drives the walk pointer and a server-supplied comment offset far past the buffer, reading arbitrary IOP memory into the share list returned to the EE. The sibling parser `smb_FindFirstNext2` in the same file was already hardened (`smb.c:1530-1559`); this one was left.
- Fix: validate once at the top, mirroring FindFirstNext2: offsets < received length, count clamped to what fits, NUL required within remaining bytes, comment offset inside the received data.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-004 — Anonymous FTP is the unauthenticated transport for a remote ELF-launch command inbox
- Category: 6
- Locations: `src/init.c:1176-1183` (FTP started with `-anonymous`); `src/remote_control.c:86-88` (`EXECUTAR\t<path>` returns any path as the launch target); `src/dash_main.c:766-790` (poll wrapper gates only on HOME section, worker idle, FTP readiness); `src/pc_game_receiver.c:37-76,309-353` (second file-drop job staging files onto `ata0:`)
- Problem: anyone with LAN access while FTP runs can drop a command file that launches an arbitrary ELF path, powers off, or starts a bulk receive — no credential, token, or source check anywhere in the EE path. File transfer being anonymous makes the command inbox anonymous too.
- Fix: gate the command/receiver polls behind a separate default-off opt-in setting (smallest change that breaks the anonymous-to-launch chain); a per-install token is the stronger variant.
- Justification: the opt-in is the smallest addition that separates file transfer from remote launch; no new protocol.
- Benefit: High · Effort: M · Risk: Low · Confidence: READ for the EE chain; the IOP FTP filesystem scope is INFERRED

### RE-LAUNCH-005 — Self-update overwrites the running ELF in place with no recoverable copy — and the CHANGELOG tells users a `.BAK` exists during the swap
- Category: 6
- Locations: `src/self_update.c:218-251` (`FIO_O_TRUNC` overwrite of the running ELF's own path), `:280-287` (a `.BAK` from older builds is deleted; none is created), `:351-358` (failure text concedes the ELF "may be damaged"); `CHANGELOG.md:13` (Rolling claims the old ELF "is kept as `.BAK` only during the swap" and that download falls back to disk when RAM is short — neither exists in the code)
- Problem: the download is SHA-256-verified in RAM before any write, but once truncation succeeds, any failure — power loss, card removal, short write — leaves a truncated unbootable ELF as the only copy. The changelog's false reassurance makes it worse: a user who believes a backup exists will not keep their own known-good ELF. README and the install pages state the no-backup design correctly.
- Fix: write the verified ELF to a staging leaf in the same folder, verify on disk, then swap by rename (old → backup, staging → live), deleting the backup only after the new file verifies; correct `CHANGELOG.md:13` to the no-backup wording already used in `docs-site/content/install.html:31`.
- Justification: a rename/backup pair is the minimum mechanism that makes the one destructive step recoverable; everything else already exists.
- Benefit: High · Effort: M · Risk: Medium (rename semantics vary by device driver; needs hardware validation) · Confidence: READ

### RE-LAUNCH-006 — CNF loader copies unbounded file values into fixed SETTING fields with strcpy/sprintf
- Category: 6
- Locations: `src/config.c:962` (`strcpy(setting->LK_Path[i], value)`), `:975-1003` (`sprintf` into the `Misc`/`Misc_*` family), `:1040-1067` (`strcpy` into eight path fields); field sizes `include/launchelf.h:285-308` (`Misc*` are `char[64]`; path fields `char[1025]`; SETTING is heap-allocated, `src/config.c:799`)
- Problem: `loadConfig()` imposes no length check on values from a user-supplied CNF — exactly the kind of file users download and copy between cards. A >64-byte value overflows a `Misc*` field into adjacent SETTING members; a >1025-byte value overflows the heap block. The same function already guards two families correctly (SMB credentials at `:1051-1053`, `PathPad` at `:1098-1099`); the guard was missed everywhere else.
- Fix: bound every value copy in `loadConfig` to its destination size, dropping entries that do not fit — the function's own existing idiom, extended.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-007 — Config save-text builder can overrun its heap buffer (unbounded sprintf chain measured with %n into a size estimate)

> **WITHDRAWN (Stage B):** No overrun occurs: the allocation is 67,190 B (sizeof(SETTING) 28,248 + sizeof(PathPad) 30,750 + 8,192) while maximum output with every field full-but-valid is ≈61,038 B — slack ≈6,152 B.
- Category: 6
- Locations: `src/config.c:533-548` (allocation `sizeof(SETTING) + sizeof(PathPad) + 8192 + smb_len`), `:548-712` (unbounded `sprintf(tmp + CNF_size, …)` chain, incl. 17 launch-key lines and 30 `PathPad` lines at `:693-712`)
- Problem: the allocation is an estimate, not a bound: worst-case output (≈48 KB of path text alone when every field is full — which RE-LAUNCH-006's loader and the classic config screen both permit) exceeds the estimate, and the `%n`-measured sprintf chain writes past the malloc'd block while saving config.
- Fix: write with the remaining-space `snprintf` idiom already used by the save-path helpers in the same file (`src/config.c:38-101`) and fail the save if the text does not fit.
- Benefit: High · Effort: M · Risk: Low · Confidence: READ

### RE-LAUNCH-008 — smbd truncates 64-bit SMB file offsets to signed 32-bit before seeking on the card
- Category: 6
- Locations: `iop/smbd/smbd.c:150-160` (`smbd_pread`/`smbd_pwrite`: `iomanX_lseek(fd, (int)off, FIO_SEEK_SET)`); callers `iop/smbd/smbd_fs.c` `read_cmd` (~:441) and `write_cmd` (~:466) pass the wire's uint64 offset unchecked
- Problem: a WRITE at offset 2³²+5 silently overwrites offset 5 of the file; an offset with bit 31 set seeks negative. Memory-card files can never legitimately approach 2 GiB, so every large offset is an error or an attack — the code converts the dangerous ones into wrong-offset I/O instead of an error.
- Fix: reject `off > INT32_MAX` once in `smbd_pread`/`smbd_pwrite`, returning `-EINVAL`. Two lines.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-009 — CI-wired test `test_copy_psu_listing.py` fails deterministically at the pin
- Category: 6
- Locations: `tests/test_copy_psu_listing.py:557` (embedded C harness assertion for the cut/move PSU case over UDPFS; the copy variant at `:523-525` passes); production path under test in `src/filer_copy.c`
- Problem: executed 4/4 times in a `/tmp` copy — same assertion failure every run (SIGABRT at the harness assertion), while 211 of 214 host suites pass. Either the production paste/move code or the harness expectation is wrong; the suite is in the CI list, so this is a red gate or a broken guard, and which one matters.
- Fix: first establish which conjunct fails (open-count vs last-read path vs leftover source), then fix that one mechanism — production code or harness, whichever is wrong. Do not relax the assertion to make it pass.
- Benefit: High · Effort: M · Risk: Low · Confidence: READ (executed locally; whether the current GitHub CI run is red is INFERRED, not checked)

### RE-LAUNCH-010 — The SMB server ships with a published default login, stored in clear text and printed on the status screen
- Category: 6
- Locations: `src/config.c:833-834` (default user/password pair — values not reproduced here), `:706-707` (both written in clear text to the config file); `src/init.c:2783-2816` (login plumbing); `src/dash_smbd.c:20-24` (shares `mc0`/`mc1` to the LAN and prints user name **and password** on screen)
- Problem: anyone who knows the project's published default can connect to a user who never changed it and read/write both memory cards; the on-screen print additionally exposes even a user-chosen password to anyone who can see the TV. The only mitigation offered is "use a network you trust" (`src/dash_smbd.c:29`).
- Fix: require a password to be set (or generate and show a random one) on first server start while the default is still in place; stop printing the password on the status screen (user name + "password set" instead — a deletion).
- Justification: a first-run prompt is a small addition; the screen change is pure deletion.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-011 — No authentication throttling anywhere in the SMB server, against that same default login
- Category: 6
- Locations: `libsmb2/lib/libsmb2.c:5179` (session-setup leg bound) vs `:5431` (leg counter reset whenever a sequence completes — including a failed AUTHENTICATE); `iop/smbd/smbd_fs.c` `authorize_user` and `iop/smbd/smbd.c` add no delay, counter, or lockout; fallback login compiled in at `iop/smbd/smbd.c:28` (location only)
- Problem: NTLMv2 verification itself is sound, but an unauthenticated LAN peer can guess passwords at wire speed, forever, against an account whose default is publicly known. Attempts per connection are unbounded and new connections are accepted indefinitely.
- Fix: count consecutive failed authorizations per server run in the glue and add a growing delay (or refuse further session setups until restart) after a small threshold; surface the count through the existing STATUS devctl. No protocol change.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-012 — `GET_XPARAM_DB_SIZE()` is identically 1, so the 292-entry game-parameter database is searched over exactly one entry
- Category: 6
- Locations: `iop/xparam/include/xparam.h:36` (`#define GET_XPARAM_DB_SIZE() (sizeof(*XPARAMS) / sizeof(XPARAMS[0]))` with `XPARAMS` declared `extern const xparam_t XPARAMS[]` at `:34`); sole consumer `iop/xparam/src/xparam.c:55` (`CheckGameID` loop); database `iop/xparam/src/database_merged.c` (292 entries by script count; first entry `PAPX_905.17`)
- Problem: `sizeof(*XPARAMS)` is the size of one element — the same as `sizeof(XPARAMS[0])` — so the macro is 1 by construction, and `CheckGameID` can only ever match the database's first entry. Every other game's XParam override silently never applies. (Found by the coordinator during spot-checks; the section pass had read this module and recorded only a cosmetic argc case.)
- Fix: define the size where the array is defined (a count constant in `database_merged.c` exported via the header), or move the loop bound computation into the same translation unit as the array. One-line class of fix; no new mechanism.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-013 — vmcman's FAT-chain walk indexes the 32-entry ifc_list with an unbounded cluster number
- Category: 6
- Locations: `iop/vmcman/src/main.c:3872-3932` (`McGetDirectory` walk: `ifc_index = cluster >> 5`, then `mcdi->ifc_list[ifc_index]` read/written at :3903/:3924), list size `iop/vmcman/include/mcman-internal.h:99` (32); the mount-side check it trusts `iop/vmcman/src/mciomanx_backing.c:138` (validates one inequality only)
- Problem: the walk never checks `ifc_index` against the 32-entry table, and a crafted FAT (e.g. `backup_block2 = 0xFFFF`) makes it read and write far outside the table into IOP memory.
- Fix: clamp the walk by the same invariant the mount check already computes: stop or fail when `ifc_index` reaches the table size derived from `clusters_per_card`.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-014 — hdl_info copies game name/startup with strcpy from disk data and from the EE rename packet
- Category: 6
- Locations: `iop/hdl_info/hdl.c:48` (`strcpy(ginfo->name, buffer + 8)`), `iop/hdl_info/hdl.c:49` (`strcpy(ginfo->startup, buffer + 0xac)`), `iop/hdl_info/hdd.c:180` (`strcpy(game->name, Packet->NewName)`) with unbounded compare at `iop/hdl_info/hdd.c:177`; destinations `iop/hdl_info/hdl.h:9-10` (`name[65]`, `startup[13]`); packet field `iop/hdl_info/hdd.h:68` (`NewName[64]`); RPC buffer `iop/hdl_info/main.c:23` (4 KB)
- Problem: neither the name field at offset 8 nor the startup field at offset 0xac of the on-disk HDLoader header is guaranteed NUL-terminated, and both are strcpy'd into 65- and 13-byte heap fields — a crafted/corrupt game partition overflows the game-list heap. The rename path repeats the pattern for EE-supplied data: an unterminated `NewName`/`OldName` makes strcpy/strcmp read through the RPC buffer and write past `game->name`. The same function bounds its other copy correctly (`hdl.c:46-47`), so the strcpys are the inconsistency.
- Fix: bounded copies matching the existing `partition_name` pattern (copy at most dest−1, force NUL), and validate `NewName`/`OldName` termination once at the top of the rename handler.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-015 — ps2ips_rpc trusts the EE packet's `malign` value in send/sendto; negative lengths pass through recv
- Category: 6
- Locations: `iop/ps2ips_rpc/ps2ips.c:336-345` (`do_send`), `:365-378` (`do_sendto`), packet layout `iop/ps2ips_rpc/ps2ip_rpc.h:99-102` (`malign_buff[64]`); same family: `recvlen = MIN(BUFF_SIZE, recv_pkt->length)` at `iop/ps2ips_rpc/ps2ips.c:149` and `:249` with no lower bound
- Problem: `s_offset = 64 - pkt->malign; memcpy(lwip_buffer + s_offset, pkt->malign_buff, pkt->malign)` — `malign` is an EE-supplied int, never checked against the 64-byte `malign_buff` or against `sendlen`. `malign > 64` writes before `lwip_buffer` and reads past the packet; negative `malign` turns the memcpy length into a huge size_t; `malign > sendlen` sends stale buffer contents. In recv/recvfrom a negative `length` survives `MIN()` and reaches `recv()`. The neighbouring `do_setsockopt` was fixed with exactly the missing check (`ps2ips.c:526-530`), proving the pattern is wanted here.
- Fix: at the top of `do_send`/`do_sendto` reject `malign < 0 || malign > 64 || malign > pkt->length`; at the top of `do_recv`/`do_recvfrom` reject `length < 0`. Four small guards, no restructuring.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-016 — usbmass_bd uses the device-reported block length for a stack alloca and unchecked transfer lengths
- Category: 6
- Locations: `iop/usbmass_bd/src/scsi.c:244-267` (READ CAPACITY 10/16 ingest; only `== 0` rejected), `:321` (`__builtin_alloca(sectorSize + 4)`), `:334` (`memcpy(misalign_buffer, buffer, sectorSize)`), transfer length `bd->sectorSize * sectorCount` at `:156` and `:174`
- Problem: any nonzero device-reported `block_length` becomes `bd->sectorSize`. On the misaligned-buffer write path that value sizes a stack alloca on an IOP thread stack: a device reporting e.g. 64 KB blocks overflows the stack outright, and even a legitimate 4 KB-sector device spends 4 KB+ of stack whenever the EE buffer is misaligned. The same value multiplies READ/WRITE(10/16) transfer lengths with no sanity ceiling — 512/1024/2048/4096 are the only block lengths the rest of the stack (FAT, APA, PFS) can actually use.
- Fix: at capacity ingest, reject `sectorSize` values that are not powers of two in [512, 4096] — the set every consumer already assumes. One check at the single ingest point; the alloca and multiplications become safe by construction.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-017 — Unbounded sprintf/strcat path assembly in the copy/delete/MC-read path cluster
- Category: 6
- Locations: `src/filer_copy.c:764` (`char out[MAX_PATH], in[MAX_PATH]`), `:849` (`sprintf(in, "%s%s", inPath, file.name)`), `:873` (`sprintf(out, "%s%s", outPath, newfile.name)`), `:1117-1120` (same pair repeated plus `strcat(out, "/")`), `src/filer_actions.c:1980` (`strcat(dir, "/")` immediately after a length-checked `snprintf` at :1972 — a `dir` of exactly `sizeof(dir)-1` chars passes the check and the strcat then writes past the end), `src/filer.c:494-495` in `readMC` (`strcpy(dir, &path[4]); strcat(dir, "*")` into `char dir[MAX_PATH]` at `src/filer.c:481`)
- Problem: one mechanism, five sites: a destination path and an entry name are concatenated with no bound into a MAX_PATH buffer. `genLimObjName(out, 0)` is called after the sprintf at `src/filer_copy.c:1118-1119` — it truncates the result, but the overflow has already happened during the sprintf. Directory trees copied from USB/HDD/MC are attacker- and accident-shaped input (deep nesting + 255-char names is normal on exFAT), so `inPath`/`outPath` near MAX_PATH plus a long name is a realistic paste, not a theory. Sibling code in the same files does this correctly with checked `snprintf` (`src/filer_actions.c:159-161`, `:1972`), so the unbounded sites are the inconsistency.
- Fix: convert these five concatenations to the checked-`snprintf` form already used in the same files, returning the existing `-ENAMETOOLONG`-style failure when the joined path does not fit; delete the now-redundant post-hoc `genLimObjName` truncation at the copy site if the checked join makes it unreachable.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-018 — scanSystemCnf uses strncat as if it were a bounded copy — duplicate SYSTEM.CNF keys overflow
- Category: 6
- Locations: `src/main_startup.c:152-162` (`strncat(SystemCnf_BOOT, value, MAX_PATH - 1)`, `strncat(SystemCnf_BOOT2, value, MAX_PATH - 1)`, `strncat(SystemCnf_VER, value, 9)`, `strncat(SystemCnf_VMODE, value, 9)`), destinations `src/main.c:67-70` (`SystemCnf_BOOT2[MAX_PATH]`, `SystemCnf_VER[10]`, `SystemCnf_VMODE[10]`), caller loop `src/main_startup.c:178-183`
- Problem: `strncat(dst, src, n)` appends up to `n` more characters; it does not bound the total length. `readSystemCnf` zeroes the destinations once, then calls `scanSystemCnf` for every parsed line. A SYSTEM.CNF containing the same key twice (malformed homebrew discs, hand-edited images, dual-boot discs) appends twice: two `VER` lines can write up to 18 chars + NUL into a 10-byte global; two `BOOT` lines up to ~2048 into a 1025-byte global. The disc being read is untrusted input by definition.
- Fix: replace the append with a bounded copy that only writes when the destination is still empty (first occurrence wins, matching how the results are consumed at `src/main_startup.c:185-193`), or ignore subsequent duplicates outright. Less code than the append form.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-019 — smbd file handles and auth state survive logoff and tree disconnect; handles bind to the connection, not the session
- Category: 6
- Locations: `iop/smbd/smbd_fs.c:159-175` (`smbd_logoff` frees only the session struct), `:284-289` (`smbd_tree_disconnect` is a no-op returning success), `:323-330` (table indexed by `conn->generation` only); a second session setup replaces the identity wholesale (`libsmb2/lib/libsmb2.c:5400-5455`)
- Problem: after a client logs off, its open file handles remain valid and are inherited by whoever uses the connection next, and every open handle silently changes owner when a second user authenticates on the same connection. Server-side state outlives the authentication that created it — the opposite of what LOGOFF/TREE_DISCONNECT mean to the client.
- Fix: close the connection's file table in `smbd_logoff` and `smbd_tree_disconnect`, and re-key handles to the session as well as the connection (or close the table on session replacement). The table and its close helper already exist.
- Benefit: Medium · Effort: M · Risk: Medium (client-visible behaviour change on a path some clients abuse; needs hardware interop testing) · Confidence: READ

### RE-LAUNCH-020 — `SMBD_DEVCTL_START` parses the credential blob past its stated length, and reports success for credentials it did not take
- Category: 6
- Locations: `iop/smbd/smbd.c:283-290` (devctl START handler); caller contract `include/smbd_devctl.h`, construction `src/init.c:2790-2793`, divergence acknowledged at `src/init.c:2810`
- Problem: the handler checks `arglen > 2` and that the last byte is NUL, then takes the password from `(char *)arg + strlen(arg) + 1` without ever checking that a second NUL-terminated string actually lies inside `arglen`. A blob containing one string makes the copy read up to 31 bytes past the supplied buffer in IOP memory and installs whatever it finds as the password. The in-tree caller always builds a well-formed blob, so today this is a latent trap for the next caller. Separately, when the server is already running the blob is silently ignored and start returns 0 — the EE records the new login as pending while the server keeps the old one, so UI and wire can disagree about the password indefinitely.
- Fix: validate `strlen(arg) + 1 < arglen` and a terminator for the second string within `arglen` before copying; return `-EBUSY` (or the existing running status) when START arrives while running instead of unconditional 0.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-021 — SNTP clock sync uses a predictable transmit timestamp, and the RTC it sets is what TLS certificate validation trusts
- Category: 6
- Locations: `src/dash_clock_sync.c:149-152` (`nonce_hi = Timer() ^ constant`; `nonce_lo` is one LCG step from it — the whole 64-bit transmit timestamp is a function of the millisecond timer); `src/dash_clock_sync.c:207-215` (the only reply authentication is echo of that timestamp, plus stratum/mode checks); era window `src/dash_clock_sync.c:229-233`; `src/dash_clock_sync.c:315-360` (`dashClockSyncInternet` writes the result to the RTC); `src/cloud_http.c:340-352` (TLS verify errors explicitly depend on "the PS2 date and time")
- Problem: SNTP is unauthenticated UDP. The origin-timestamp check is meant to bind the reply to the request, but the timestamp is predictable to anyone who can guess roughly when the user pressed "sync" (Timer() is milliseconds since boot). A LAN attacker who can spoof the pool server's address can set the console clock to any date in the accepted 2020–2100 window. That clock is then the reference for X.509 expiry checks in the cloud/updater TLS path, so a shifted clock can make an expired certificate appear valid (or break all cloud access as a denial of service).
- Fix: draw the transmit timestamp from the same conditioned entropy material the TLS stack already builds (`mbedtls_hardware_poll` inputs in `src/cloud_http.c`) instead of Timer() alone, and keep the existing stratum/era checks. This raises spoofing from "guess the boot-relative millisecond" to "guess 64 conditioned bits".
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ for predictability; exploitability in a given LAN is INFERRED (depends on spoofing position)

### RE-LAUNCH-022 — TLS entropy callback always reports success, including when its only OS source failed and every other input is at its cold-boot value
- Category: 6
- Locations: `src/cloud_http.c:104-160` (`mbedtls_hardware_poll`: `getrandom` failure is zeroed and ignored at :127-129; the function returns 0/success unconditionally at :156-157); inputs are COP0 tick samples, `clock()`, the RTC, `Timer()`, a local counter, and network timing (`src/cloud_http.c:116-145`); wiring at `scripts/build_cloud_tls.sh:66` (`MBEDTLS_ENTROPY_HARDWARE_ALT`, `MBEDTLS_NO_PLATFORM_ENTROPY`)
- Problem: the code's own comment is honest that this is "defense-in-depth … not a claim that cpu_ticks is [a TRNG]". The residual defect is the failure contract: on a cold boot with a dead RTC battery (a common PS2 state — fixed default date, timer near zero, counter starting at 0), the material can be highly predictable across identical boots, yet the callback still presents a full-success entropy block to the CTR_DRBG that keys every OAuth/Drive/updater TLS session. SHA-256 conditioning mixes the inputs but cannot add entropy that is not there. INFERRED: the actual seed entropy on real hardware cannot be measured from this code; the finding is that nothing in the path can ever say "not enough entropy".
- Fix: track whether at least one genuinely variable source contributed (e.g. `getrandom` returned bytes, or network timing jitter above a floor) and return `MBEDTLS_ERR_ENTROPY_SOURCE_FAILED` when none did, letting the existing TLS error path report it instead of silently keying a session.
- Benefit: Medium · Effort: S · Risk: Medium (a too-strict floor could block cloud features on healthy hardware; the threshold needs hardware validation) · Confidence: READ for the contract; real-world seed quality INFERRED

### RE-LAUNCH-023 — mmceman copies the card-supplied game ID with strcpy into the caller's devctl buffer
- Category: 6
- Locations: `iop/mmceman/src/mmce_cmds.c:195-224` (`mmce_cmd_get_gameid`; strcpy at :216), caller `iop/mmceman/src/mmce_fs.c:1360-1362` (devctl `MMCE_CMD_GET_GAMEID` passes its `buf` straight through)
- Problem: the MMCE device returns a fixed 255-byte packet; the game ID is taken as a C string starting at `&rdbuf[0x4]` with no check that a NUL exists within the packet, and strcpy'd into whatever buffer the EE devctl caller supplied. A card (or clone/faulty unit) returning 251 non-NUL bytes makes strcpy read past the stack `rdbuf` and write unbounded into the destination. The fs layer's other name paths were explicitly guarded against this class (`mmce_fs_wire_name_ok`, `iop/mmceman/src/mmce_restore_guard.h`) — the cmds layer was not.
- Fix: bound the copy by the packet — bounded scan over `rdbuf[0x4..0xFF]` (fail if no NUL) and memcpy that many bytes + NUL. One function changed.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-024 — SMB1 QueryPathInformation dereferences the server's DataOffset without validation
- Category: 6
- Locations: `iop/smbman/smb.c:877` (`BasicFileInfo_t *` cast), `iop/smbman/smb.c:890` (`StandardFileInfo_t *` cast), in `smb_QueryPathInformation` (`iop/smbman/smb.c:811`)
- Problem: after the reply, `&SMB_buf.smb.u8buff[QPIRsp->smbTrans.DataOffset]` is cast to the info structs and ~40 bytes are read from it, with no check that `DataOffset` (a server-supplied 16-bit field, up to 65535) plus the struct size fits in the received message / `SMB_buf` (4224 bytes). A hostile server makes the IOP read out-of-bounds memory and report it to the EE as file metadata. Same root cause as RE-LAUNCH-003 but a different function and sink; the fix pattern (FindFirstNext2, `smb.c:1530-1559`) already exists in-file.
- Fix: one check before each cast (or once, before the `queryType` branch): `DataOffset` must be `<= r - sizeof(*struct)`, else `-EIO`.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-025 — hdl_info sums sub-partition lengths using the disk-controlled `nsub` past the 64-entry array
- Category: 6
- Locations: `iop/hdl_info/hdl.c:43-44`; same pattern `iop/hdl_info/apa.c:243` (count cross-check at `apa.c:257`); array bound `iop/hdl_info/ps2_hdd.h:20` (`PS2_PART_MAXSUB 64`) and `:76` (`subs[PS2_PART_MAXSUB]`)
- Problem: `for (i = 0; i < get_u32(&part->nsub); ++i) size += get_u32(&part->subs[i].length);` — `nsub` is a raw u32 from the on-disk APA header; nothing clamps it to 64, so a crafted partition header reads far past the header struct (into adjacent stack/heap) while totalling a game's size. Read-only overflow, but it feeds the `total_size_in_kb` the EE displays and acts on.
- Fix: clamp the loop to `PS2_PART_MAXSUB` (and treat `nsub > PS2_PART_MAXSUB` as a corrupt header) at both sites.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-026 — mmceman set-game-ID narrows the length to u8 and strcpys into a 255-byte stack buffer
- Category: 6
- Locations: `iop/mmceman/src/mmce_cmds.c:226-243` (`u8 len = strlen(ptr) + 1` at :231; `strcpy(&wrbuf[0x4], ptr)` into `u8 wrbuf[0xFF]`), caller `iop/mmceman/src/mmce_fs.c:1364-1367` (devctl passes the EE string through unchecked)
- Problem: an EE-supplied game-ID string of 251+ bytes overflows the stack `wrbuf`; at 255+ bytes the u8 `len` also wraps, so the transmitted length and the copied length disagree. EE-side callers are trusted local code, so this is robustness, not a remote hole — but the devctl is a public interface any homebrew can call.
- Fix: reject `strlen(ptr) > 250` at the top of `mmce_cmd_set_gameid` (the wire format cannot express more anyway).
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-027 — Classic dialog helpers strcpy unbounded caller text into fixed stack buffers
- Category: 6
- Locations: `src/gui.c:12-20` (`ynDialog`: `char msg[512]; strcpy(msg, message)`), `src/gui.c:95-111` (`nonDialog`: `char msg[80 * 30]; strcpy(msg, message)`); no length contract at the prototypes (`include/launchelf.h:594-595`)
- Problem: both helpers copy an unbounded caller-supplied message into a fixed stack buffer before doing their own line-splitting. These are the fallback dialogs used whenever the dashboard override hooks are not installed (`src/gui.c:17-19`, `:102-105`) — i.e. during launch-time prompts, exactly when messages are built from file paths and device error text. Any caller message over 512 bytes (ynDialog) smashes the stack.
- Fix: bound both copies to the buffer size and truncate the displayed message (the helpers already re-measure and re-wrap the text, so a truncated copy degrades gracefully). No new mechanism.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-028 — editorSaveAs appends the typed filename with strcpy at the end of a possibly-full path buffer
- Category: 6
- Locations: `src/editor_file.c:278-281` (live `strcpy(&Path[Win][strlen(Path[Win])], tmp)`; commented-out `strcat`/`sprintf` variants kept directly above it), buffer `src/editor.c:38` (`char Path[10][MAX_PATH]`), input bound `src/editor_file.c:277` (`keyboard(tmp, 36)`), path source `src/editor_file.c:269`
- Problem: the typed name is bounded (36 chars), but `Path[Win]` can legitimately be near MAX_PATH long for a deep directory; appending up to 36 chars + NUL at `strlen(Path[Win])` writes past the 1025-byte buffer. The two commented-out alternative lines kept above the live line document that this spot has been patched by trial before, without the bound ever being added.
- Fix: check `strlen(Path[Win]) + strlen(tmp) < MAX_PATH` before the append and abort the save-as (the function's existing `abort:` path) if it does not fit; delete the two commented-out variants while there.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-029 — smbd WRITE accepts lengths above the negotiated maximum the server itself advertised
- Category: 6
- Locations: `iop/smbd/smbd_fs.c:466-489` (`write_cmd` never compares `req->length` with `SMBD_MAX_IO`); contrast `read_cmd` at `smbd_fs.c:450-451`, which clamps to `SMBD_MAX_IO`; advertised maxima set at `smbd_fs.c:1000` (`max_write_size = SMBD_MAX_IO` = 65536, `smbd.h:27`)
- Problem: the libsmb2 decoder bounds `req->length` only against the received PDU (cap ≈ max + 64 KiB slack, `libsmb2/include/libsmb2-private.h:359-370`), so a client can push ~128 KiB writes at a server that negotiated 64 KiB, in one `iomanX_write` through `smbd_pwrite`. MCMAN-side behaviour for oversized single writes is outside the read set (INFERRED: short writes are handled via `rep->count`, so the likely outcome is inefficiency/protocol violation rather than corruption) — but the asymmetry with `read_cmd` shows the clamp was intended and missed on the write side.
- Fix: the same two-line clamp `read_cmd` already has.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ (downstream effect INFERRED)

### RE-LAUNCH-030 — smbd CREATE does not enforce the card's 31-byte leaf limit that RENAME carefully enforces
- Category: 6
- Locations: `iop/smbd/smbd_fs.c:340-410` (`create_cmd` → `make_path` checks total length `SMBD_PATH_MAX` only); contrast `rename_handle` (~`smbd_fs.c:855-860`), which rejects a leaf longer than `SMBD_LEAF_MAX` (31, `smbd.h:25`) with NAME_TOO_LONG, and `iop/smbd/smbd.c:230-241` (`smbd_rename` strcpys the leaf into `sceMcTblGetDir.EntryName` trusting that check)
- Problem: path escape is properly refused (checked clean), but a CREATE with a 40-byte leaf goes straight to `iomanX_open`; what MCMAN does with an over-long component — fail cleanly or truncate — is in the unread driver set, so the risk is INFERRED: if any card driver truncates, the client is told it created a name that does not exist under that name, and a later open of the requested name misses while the truncated one collides with other long names sharing a 31-byte prefix.
- Fix: apply the existing `SMBD_LEAF_MAX` per-component check inside `make_path` (it already walks component by component) so CREATE, RENAME, and every other verb share one rule; rename's duplicate check can then be deleted.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ (mechanism); card-driver consequence INFERRED

### RE-LAUNCH-031 — smbd file-id validation uses only 4 of the 16 file-id bytes, and the generation counter wraps at 16 bits
- Category: 6
- Locations: `iop/smbd/smbd_fs.c:159-175` (`lookup()` compares `id[0..3]` only — slot index + `gen`; bytes 4–15 ignored), `iop/smbd/smbd_fs.c:397` (`h->gen = next_gen++`, `next_gen` is `uint16_t` at :40), `:405-409` (file_id construction zeroes the other 12 bytes)
- Problem: a stale file id becomes valid again after 65,536 CREATEs recycle the same slot with the same generation — and because the unused 12 bytes are always zero in issued ids and ignored in lookup, there is no extra entropy to save it. Impact is bounded by the single authenticated user, hence low benefit, but the fix is equally small.
- Fix: widen `gen` to 32 bits into file-id bytes 2–5 and compare them — simplest: make `next_gen` `uint32_t` and extend the existing comparison by two bytes.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-032 — smbd server identity is a compile-time constant and session ids restart from a fixed counter on every serve
- Category: 6
- Locations: `iop/smbd/smbd_fs.c:998` (`memcpy(server.guid, "Launchstation-PS", 16)` — the same server GUID for every console and every boot); `libsmb2/lib/libsmb2.c:5807` (`server->session_counter = 0x1234` at every `smb2_serve_port` entry, so session ids are 0x1234, 0x1235, … after each start)
- Problem: SMB clients key server identity and session state on the GUID; two Launchstation consoles on one LAN are indistinguishable to a client's cache, and a restarted server reissues identical session ids — the exact conditions MS-SMB2's GUID/session-id uniqueness rules exist to prevent (stale client state applied to a fresh server). Signing keys differ per authentication, so this is confusion/staleness, not key compromise.
- Fix: derive the GUID once per serve from the console's own identity material available on the IOP (e.g. MAC address from the already-loaded network stack, mixed with boot time) instead of a string literal; no new subsystem.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ; client-visible impact INFERRED (no Windows client test in the read set)

### RE-LAUNCH-033 — hdl_info APA chain walk follows disk `next` pointers with only a 10,000-count hack against cycles
- Category: 6
- Locations: `iop/hdl_info/apa.c:154-179` (walk loop; bounds check at :162-163; count cap and TODO comment at :175-178)
- Problem: the walk follows `part.next` from header to header. The in-code comment admits the missing visited check: a cyclic chain is stopped only after 10,000 iterations of sector reads. The guard that does exist, `get_u32(&part.start) + get_u32(&part.length) < total_sectors`, is 32-bit arithmetic — INFERRED: a start/length pair whose sum wraps below `total_sectors` passes the check, after which the walk trusts `next` from a header outside the real partition area (the magic+checksum gate at :158-161 still limits which sectors are accepted as headers, which is why this stays low).
- Fix: track visited start sectors in the table being built (it already accumulates them via `apa_part_add`) and stop on a repeat; widen the start+length comparison to 64-bit. Both are local to this loop.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ (wrap sub-point INFERRED)

### RE-LAUNCH-034 — mmceman directory read stores the card-supplied filename without ensuring NUL termination
- Category: 6
- Locations: `iop/mmceman/src/mmce_fs.c:1040-1043` (`filename_len = rdbuf[0x29]; … mmce_sio2_tx_rx_pio(0, filename_len, NULL, dirent->name, …)`)
- Problem: the card supplies the name length (u8, so ≤ 255, which fits the iomanX dirent name field) and that many bytes are received directly into `dirent->name`; no byte is forced to NUL afterwards. A card returning a full 255-byte name with no NUL leaves termination to whatever stale byte follows in the dirent, and EE consumers treat the field as a C string.
- Fix: after a successful receive, write `dirent->name[filename_len] = '\0'` (the field has room for exactly this case).
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-035 — vmcman VMC-backing slot validation is off by one and admits negative slots (latent)
- Category: 6
- Locations: `iop/vmcman/src/mciomanx_backing.c:46` (`if (slot > MCMAN_MAXSLOT)`), array `iop/vmcman/src/mciomanx_backing.c:26` (`mcman_vmc_cardinfo[MCMAN_MAXSLOT]`, `MCMAN_MAXSLOT 10` at `iop/vmcman/src/mcman-internal.h:106`)
- Problem: `slot == 10` passes validation and indexes one element past the array; negative slots pass too. Currently unreachable as far as this tree shows: the only callers derive the slot through `mcman_unit2card` (`iop/vmcman/src/mcdev.c:216-223`), which masks the unit into range — so this is a latent trap for the next caller, not a live bug (labelled accordingly).
- Fix: change the one comparison to `if (slot < 0 || slot >= MCMAN_MAXSLOT)`. One line, at the validation function whose whole job is this check.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-036 — cloud_http maintains two divergent HTTP response parsers; chunked detection is case-sensitive and Content-Length goes through atoi
- Category: 6
- Locations: `src/cloud_http.c:501-526` (`header_value`: name match is case-insensitive, values are silently truncated to the caller's buffer); `src/cloud_http.c:560-680` (`read_response`: `strstr(value, "chunked")` case-sensitive at :597-599 and :658-659; `content_length = atoi(value)` at :596, :643-656); `src/cloud_http.c:953-1277` (`cloudHttpDownloadFdInternal`: a second, separate header/chunk/Content-Length implementation at :1122-1141 with stricter checks)
- Problem: the buffered path (all OAuth/Drive JSON and GitHub release JSON) and the streaming path (downloads) parse the same protocol differently. In the buffered path: a standards-legal `Transfer-Encoding: Chunked` (capital C) is not recognised, so raw chunk framing is handed to the JSON parsers as if it were the body; `Content-Length` is converted with `atoi`, whose behaviour on a value outside int range is undefined and which accepts trailing garbage; and a truncated `Location` value (over 2047 bytes) is silently used for the resumable-upload session URL. The streaming path already demonstrates the stricter pattern (explicit invalid-length rejection at :1122-1137).
- Fix: in `read_response`, match the transfer-coding case-insensitively, parse Content-Length with a bounded digit loop that rejects overflow/trailing characters (mirroring the streaming path), and fail instead of truncating when `Location` does not fit. Longer term, share the header-parsing helper between both paths.
- Benefit: Medium · Effort: M · Risk: Low · Confidence: READ

### RE-LAUNCH-037 — Custom OAuth client override contradicts its own "complete pair" rule: an ID alone silently discards the built-in secret
- Category: 6
- Locations: `src/cloud_google.c:227-241` (`load_client`: comment says a custom client is used "only as a complete pair", but the code returns as soon as `client_id[0]` is set, keeping whatever secret — including none — the file had); `src/cloud_google.c:205-212` (`builtin_client_available` likewise tests only the ID); `src/cloud_google.c:457-472` (`cloudGoogleSaveClientConfig` accepts an empty secret); `src/cloud_google.c:380-385` (`refresh_access_token` then omits `client_secret` from the token request when empty)
- Problem: a user who pastes only a custom client ID (or whose `WLE-CLOUD.CNF` loses its secret line) silently falls out of the working built-in configuration into a half-configuration: device-flow/refresh requests go out without the secret Google's device flow requires for this client type, and the failure surfaces only as a generic OAuth error far from the cause.
- Fix: make `load_client` honour its comment — use the custom client only when both ID and secret are present; otherwise fall back to the built-in pair whole. Reject a secret-less save in `cloudGoogleSaveClientConfig` with a specific message.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-038 — Long-lived credentials are stored in clear text in ordinary config files on removable media
- Category: 6
- Locations: `src/cloud_google.c:317-355` (Google refresh token written as a plain `Refresh_Token = …` line to `WLE-CLOUD.TOK`, with a staged duplicate in `WLE-CLOUD.TOK.NEW`); `src/cloud_google.c:457-492` (custom OAuth client ID/secret written in clear text to `WLE-CLOUD.CNF`); `scripts/gen_cloud_client.py` + `src/cloud_google.c:20-27,229-241` (built-in OAuth client material — **location only**, no values reproduced); `src/smb_cnf.c` / `include/smb_shares.h:57-58` (SMB share user names/passwords held and persisted in clear text in `SMB2.CNF` / the `LAUNCHSTATION.CNF` share section)
- Problem: a Google refresh token is a durable, revocable-only-by-the-user credential for the `drive.file` scope. It, the client secret, and every SMB share password sit unprotected on a memory card/USB stick that other PS2 tools — and, while it runs, the anonymous FTP server of RE-LAUNCH-004 — can read. There is no secure storage on a PS2, so full encryption is not honestly available; the finding is about exposure surface and user awareness, not about a missing cipher.
- Fix: (1) document in the link flow itself that linking writes a reusable token to the card and that Disconnect (`cloudGoogleDisconnect`, `src/cloud_google.c:662-684`) is the revocation path; (2) ensure the token/config files are never inside any directory the built-in servers share by default; (3) do not keep the staged `.TOK.NEW` duplicate after a successful save (already done on the success path at `src/cloud_google.c:353` — keep that property under test).
- Justification: only (1) adds anything (UI text); (2)/(3) are placement/deletion.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ for storage format; FTP readability of the settings folder specifically is INFERRED

### RE-LAUNCH-039 — smbd SET_INFO classes report success without performing the operation
- Category: 8
- Locations: `iop/smbd/smbd_fs.c:910-940` (`set_info_cmd`): `SMB2_FILE_BASIC_INFORMATION` returns 0 with timestamps/attributes silently discarded; `SMB2_FILE_ALLOCATION_INFORMATION` returns 0, nothing allocated or truncated; `SMB2_FILE_END_OF_FILE_INFORMATION` returns 0 for any size ≥ current without extending the file
- Problem: three distinct client-visible lies with one shape — success status, no effect, no record. Tools that set timestamps after a copy (the common BASIC case) silently produce wrong metadata; tools that grow-then-write-sparse corrupt their own bookkeeping. The honest behaviour already exists in the same function — unsupported classes return `SMB2_STATUS_NOT_SUPPORTED` at the bottom.
- Fix: return `SMB2_STATUS_NOT_SUPPORTED` for ALLOCATION and for END_OF_FILE growth, and either implement BASIC times via the existing stat path or return NOT_SUPPORTED for it too. Fewer promises, no new code paths of substance.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-040 — smbd LOCK succeeds without locking, and CREATE ignores share-access and oplock semantics
- Category: 8
- Locations: `iop/smbd/smbd_fs.c:491-497` (`lock_cmd` returns 0 for every lock/unlock request), `iop/smbd/smbd_fs.c:340-410` (`create_cmd` never reads `req->share_access` or `req->requested_oplock_level`, always replies `oplock_level = 0` while granting `maximal_access = FILE_ALL_ACCESS` at tree connect, `smbd_fs.c:340-345` region)
- Problem: a client that takes an exclusive byte-range lock is told it holds one; the second allowed client (SMBD_MAX_CLIENTS = 2) can then write the same ranges through the shared single card descriptor. Nothing in the handle table records ranges or sharing intent. On a single-console LAN tool the realistic failure is two PCs copying to the same save folder "safely" under locks that do not exist.
- Fix: either track the (tiny) lock set per handle — more code — or, LESS: fail LOCK with `SMB2_STATUS_NOT_SUPPORTED` like IOCTL/CHANGE_NOTIFY already do, and deny a second CREATE whose share-access excludes an existing handle on the same path. The status-only half is a few lines.
- Benefit: Medium · Effort: S · Risk: Medium · Confidence: READ

### RE-LAUNCH-041 — smbd QUERY_DIRECTORY never validates the information class: unsupported classes return a successful empty listing, and FileId is always zero
- Category: 8
- Locations: `iop/smbd/smbd_fs.c:545-640` (`query_directory_cmd` fills `smb2_fileidbothdirectoryinformation` entries regardless of `req->file_information_class`; `ent[n].file_id` is never assigned, `ent[n].file_index = n` restarts per batch); `libsmb2/lib/smb2-cmd-query-directory.c:201-218` (`dir_entry_size()` returns 0 for any class outside its six) and the encoder at :239-300, which then computes a zero total output length and produces a successful reply with no entries
- Problem: ask for any class outside the six encodable ones and the client is told "this folder is empty" instead of NOT_SUPPORTED — a data-hiding lie of the same family as RE-LAUNCH-039, reachable by any client that prefers e.g. FileStreamInformation or a future class. Separately, every entry in the IdBoth/IdFull classes carries FileId 0, so clients that key on FileId (backup tools, some shells) see all files as the same file.
- Fix: whitelist the six classes at the top of `query_directory_cmd` (one comparison chain, mirroring `dir_entry_size`), return an INVALID_INFO_CLASS-family status otherwise; fill `file_id` from the existing `file_number(path)` helper used by QUERY_INFO — which needs the entry's full path, so if that is judged too much code, at minimum do the whitelist half.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-042 — Vendored usmb2 contains an unhardened server-length memcpy in readdir, kept out of the IOP build only by a feature define
- Category: 8
- Locations: `iop/smb2man/usmb2/usmb2.c:791-792` (u32 = first 4 reply bytes; `memcpy(&dh[16+4], &usmb2->buf[8], u32)`), entry-chain parsing `usmb2.c:795-817`; destination sized at `usmb2.c:739-740` (`16 + MAX_DIR_SIZE`, `MAX_DIR_SIZE 256` at `usmb2.c:69`); gate: whole block inside `#ifdef USMB2_FEATURE_OPENDIR` (`usmb2.c:743-830`), and `ps2.yaml:265-271` defines only WRITE/NTLM/UNICODE/STAT/MKDIR/UNLINK/RENAME for smb2man
- Problem: the QUERY_DIRECTORY reply's own length field is trusted as a memcpy length into a 272-byte heap allocation — a textbook server-controlled overflow — and the follow-on entry-chain walk is likewise unbounded. **Today this is not reachable:** `USMB2_FEATURE_OPENDIR` is not defined for the IOP build, and `smb2man.c` does its own (hardened) querydir. The hazard is that the vendored copy carries local hardening patches for other paths (`usmb2/VENDORED.txt`, `riptopl.patch`) but not this one, so the first future feature flip lands a known-bad parser.
- Fix: patch the two lines at the vendoring level (clamp `u32` to `MAX_DIR_SIZE - 4` and to the validated reply length, both already available in the function) and note it in `VENDORED.txt`. Deleting the OPENDIR block from the vendored copy is an equally valid, even smaller alternative.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-043 — Classic main-menu loop in main() is unreachable — DashRun never returns
- Category: 1
- Locations: `src/main.c:302-377` (comment at :302 "The dashboard is the only shell; DashRun() does not return", followed by the full classic event loop), `src/dash_main.c:1331`, `:1396-1397` (DashRun ends in `while (1)`; its only return is the guard at :1338-1339), `src/main_menu.c:81` (`drawMainMenuScreen`), `:254` (`MainMenuHandlePad`), `:330` (`MainMenuHandleAutoLaunch`), `include/main_menu.h:34-36`
- Problem: the only callers of the three menu functions in the tree are the post-DashRun lines in `main()` (`src/main.c:357`, `:363-364`; tree-wide grep verified). Since DashRun never returns on any normal path, that whole loop — disc-change detection, `DiscTypes` formatting with unbounded `sprintf(mainMsg + strlen(mainMsg), …)` at `src/main.c:341-348`, timer updates, pad handling — is dead, along with the three `main_menu.c` functions that exist only to serve it. It still costs review, compile and audit surface, and misleads every reader about which shell is live.
- Fix: delete the post-DashRun loop from `main()` and the three menu functions (plus header declarations) that have no other caller. Keep `MainMenuState_Init`/`BeginTimers` only if a live caller remains after the deletion; otherwise delete those too.
- Benefit: High · Effort: S · Risk: Low (the guard-return path of DashRun currently falls into this loop with an uninitialised dashboard; deleting the loop makes that failure explicit) · Confidence: READ

### RE-LAUNCH-044 — Classic config/info/editor screens are shadowed by dashboard interception — a second, parallel settings/UI system is carried but not presented
- Category: 8
- Locations: interceptions `src/dash_main.c:447-485` (`dashRunMisc` routes Misc_Configure → dashboard Settings, Misc_TextEditor → `dashOpenTextEditor`, Misc_ShowFont → `dashShowFontViewer`, Misc_About/BuildInfo/DebugInfo/HddManager/FileBrowser → native dashboard screens), `src/dash_main.c:389-390` (launch hook returns early for Misc_Configure/Misc_Exploit_Installer); shadowed implementations `src/config.c:1492` (`config()`, 290 lines) with `src/config_screen.c` (361), `src/config_advanced.c` (329), `src/config_startup.c` (364); `src/main_info_screens.c:285` (`ShowFont`), `src/main_actions.c:768-799` (classic Misc_* dispatch)
- Problem: the dashboard is the only shell (RE-LAUNCH-043), and every user-visible route to Configure/TextEditor/ShowFont/About/DebugInfo is intercepted and served natively before the classic dispatch can run. The repo therefore carries two complete implementations of the same settings surface — the classic `config()` family (~1,050 lines across four files, editing the same SETTING that `src/dash_settings.c` (4,096 lines) edits) — plus classic duplicates of the font viewer and info screens. Two systems to keep in sync on every settings change, and a standing risk that a fix lands in the presented UI while the shadowed one keeps the bug. This finding does not claim the classic *browser* (`getFilePath`) is dead — it has a live caller in the exploit installer (`src/main_exploit_installer.c:974`, `:1023`, grep-verified).
- Fix: decide the classic screens' fate explicitly: either delete the shadowed implementations (keeping `loadConfig`/`saveConfigToPath`/`initConfig`, which are live) or name one concrete non-dashboard entry point that reaches them and record it next to the interception. What should not continue is the unrecorded middle state.
- Benefit: Medium · Effort: L · Risk: Medium (a hidden entry point, e.g. a launch-key path that bypasses the hook, would lose its screen) · Confidence: INFERRED (interceptions and implementations are READ; "no live route remains" is inferred from the dashboard-only shell plus the two interception layers, not an executed reachability proof)

### RE-LAUNCH-045 — Two test files are orphaned from CI — and architecture-maintenance.md claims every test runs there
- Category: 7
- Locations: `tests/test_boot_deferred_init.py`, `tests/test_usb_stat_timeout.py` (absent from all workflows; script diff: 213 test files, 211 named in `.github/workflows/compile.yml`); the false claim at `docs/architecture-maintenance.md:44` ("Every `tests/test_*.py` script is listed there")
- Problem: both are real, executable tests (one compiles and runs production code; both pass when run by hand in this audit), silently providing zero protection. Root mechanism: the CI list is a hand-maintained enumeration of 211 invocations in one step (`compile.yml:51-265`), so a new test file is inert until someone also edits the workflow — these two are the proof it has already drifted twice. The docs claim then converts the drift into false assurance.
- Fix: add the two invocations to the existing step (2 lines). Optionally afterwards replace the enumeration with a directory glob — only if that is a net deletion of maintenance, not a new framework.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ (script-verified + executed)

### RE-LAUNCH-046 — All 211 CI tests run as one sequential step: first failure masks the rest
- Category: 7
- Locations: `.github/workflows/compile.yml:50-265` — one step ("Test Settings sessions, navigation and theme compatibility") containing 211 sequential `python3 tests/…` lines under the default `bash -e` step shell; the step name no longer describes most of its contents. The only test invoked outside this step is `tests/test_app_packages.py` (packaging step, after the builds)
- Problem: a single early failure (RE-LAUNCH-009's suite sits mid-list) prevents every later test in the step from running and gives no per-test summary, so one red test hides all other regressions in the same run.
- Fix: split the existing lines into a few grouped steps by area (no new tooling), or make the one step run all lines and report failures at the end. Either is a rearrangement of existing lines, not an addition of infrastructure.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-047 — Updater menu path in README and both wiki guides points to a page that does not contain it
- Category: 7
- Locations: `README.md:104` ("Settings → Storage & devices → MMCE backups → Check for updates"); identical path at `docs/wiki/Current-Features.md:49` and `docs/wiki/Releases-and-Recovery.md:53`; code: `src/dash_settings.c:776` places `S_CHECK_UPDATES` in `system_items[]` (`src/dash_settings.c:766`), i.e. Settings → System; `mmce_backup_items[]` (`src/dash_settings.c:366-385`) contains no update row
- Problem: the row lives in System and three texts kept the MMCE-backups path; the other five texts were updated (`docs/dashboard.md:199`, `docs-site/content/cloud.html:22`, `install.html:30`, `getting-started.html:22`), so the corpus now contradicts itself and every user following the manual fails to find the control.
- Fix: replace the path with "Settings → System → Check for updates" in the 3 files. No new text.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-048 — Automatic IGR sync default stated as Off in two texts; code default is Local backup
- Category: 7
- Locations: `docs/NATIVE_CLOUD.md:95` ("It is off by default"); `docs-site/content/cloud.html:14` (same); code: `src/dash_store.c:263` (`dashCfg.igr_auto_backup = 1`) with `igr_cloud_sync = 0` (:264), mode mapping at `src/dash_settings.c:1163` (mode 1 = Local backup)
- Problem: users are told no automatic backup runs by default when a local backup *does* run after OPL IGR returns — a behaviour/privacy expectation set wrong in the two cloud texts, while `mmce-backup.html:52`, `igr.html:26` and `configuration.html` state it correctly.
- Fix: change "off by default" to "Local backup by default" in the 2 files. No new text.
- Benefit: High · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-049 — CNF rename to LAUNCHSTATION.CNF not propagated through dashboard.md and the LAUNCHELF.CNF template header
- Category: 7
- Locations: `docs/LAUNCHELF.CNF:17-21` (template header claims the file is loaded and rewritten on save), header note at `docs/LAUNCHELF.CNF:14-26`; `docs/dashboard.md:46`, `:211-213`, `:357-364`, `:478` (LAUNCHELF.CNF treated as the live file); code: `src/main_boot.c:62-66` — the live file is `LAUNCHSTATION.CNF`; an old `LAUNCHELF.CNF` is imported **read-only** and "saves go to the same folder under our name"; CHANGELOG Rolling (#346) states "Nothing writes `LAUNCHELF.CNF` any more"
- Problem: the #346 rename updated the site/configuration pages but not the maintainer dashboard reference or the template's "verified against the source" header, which now certifies a superseded claim — maintainer reference and shipped template describe the wrong file as the live, writable config.
- Fix: in the template header, state the file is a legacy-format template, imported read-only, with saves written to LAUNCHSTATION.CNF (wording already exists at `src/main_boot.c:62-66`); in dashboard.md replace the 5 LAUNCHELF.CNF-as-live-file references with LAUNCHSTATION.CNF.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-050 — Hand-rolled JSON string extraction is duplicated, and both copies mishandle escapes
- Category: 2
- Locations: `src/cloud_google.c:61-107` (`json_string`); `src/cloud_google.c:109-122` (`json_int`, first-`strstr`-match companion); `src/dash_tools.c:715-745` (`releaseJsonString`, the second implementation); callers include OAuth token/device-code parsing (`src/cloud_google.c:389,599-612,642`), Drive `id` lookup (:744), browse items (:815-825), and the updater's release digest/commit parsing (`src/dash_tools.c:760-790,864,968`)
- Problem: one mechanism — "find `"key"`, scan to the closing quote" — exists twice with different bugs. (a) `json_string`'s end scan treats a quote as escaped whenever the previous byte is a backslash (`src/cloud_google.c:84-88`), so a value ending in an escaped backslash overruns into the following JSON and merges fields. (b) Neither copy decodes `\uXXXX`: `json_string`'s escape switch (:92-103) copies the `u` literally, mangling non-ASCII Drive file names; `releaseJsonString` has no escape handling at all and ends the value at the first `"` even if escaped. (c) `json_string` silently truncates over-long values and still returns success, so a truncated token/ID is used as if valid. (d) Both search from the start of the body for the first key occurrence, correct only because callers arrange the wanted object first. These parsers decide OAuth tokens, Drive file identity (including the `wle_sha256` recovery digest path), and the self-update's expected SHA-256/commit ancestry — a merged or truncated field lands directly in a trust decision.
- Fix: replace both with one shared bounded extractor that tracks escape state correctly, decodes `\u` (at least to UTF-8 or a clean failure), and fails on truncation instead of returning a cut value. Delete the other copy.
- Justification: no addition — a merge of two existing implementations into one, net deletion.
- Benefit: High · Effort: M · Risk: Medium (many call sites; the host-test style already used for `smb_cnf.c` should be applied to the merged parser) · Confidence: READ

### RE-LAUNCH-051 — The same C-function brace-extractor helper is copy-pasted into 103 test files
- Category: 2
- Locations: 103 of 213 files under `tests/` define `def function` or `def extract` (script count); examples `tests/test_boot_deferred_init.py:9`, `tests/test_copy_pipeline.py:22`, `tests/test_cloud_recovery.py:19`, `tests/test_ps2ips_rpc.py` (its own variant); no shared test helper module exists anywhere under `tests/` (no `conftest.py`, `helpers.py`, or `__init__.py`)
- Problem: 103 private reimplementations of one brace-matching extractor, with differing signatures and robustness — a bug or improvement must be made 103 times, and tests do not all extract functions the same way.
- Fix: one small shared helper module under `tests/` and delete the per-file copies as they are touched. Net deletion of ~100 duplicated bodies; do not rewrite all 103 files in one churn commit.
- Justification: the shared module replaces ~100 copies — net deletion.
- Benefit: Medium · Effort: M · Risk: Low · Confidence: READ (script-counted + sampled reads)

### RE-LAUNCH-052 — Every directory listing allocates ~900 KB of FILEINFO; recursive size/delete allocate another ~900 KB per level
- Category: 4
- Locations: `include/launchelf.h:202` (`MAX_ENTRY = 2048`) and `:560-565` (`FILEINFO` = `char name[256]` + 66-byte title + `sceMcTblGetDir stats`); `src/filer_actions.c:310-311` (the code's own comment: "each level lists on the heap: MAX_ENTRY entries are about 900 KB"; `memalign(64, MAX_ENTRY * sizeof(FILEINFO))` in the recursive size walk), `:1981-1984` (same allocation, `MAX_ENTRY + 1`, in recursive delete), `src/filer_browser.c:2007-2010` (classic browser listing), `src/filer_copy.c:915` (per copy level)
- Problem: one listing costs ~900 KB on a 32 MB console (the code's own figure). Four call sites allocate a full MAX_ENTRY listing, and two of them allocate one per directory level, so a deep tree holds several megabytes of mostly-empty listing buffers at once — most directories hold tens of entries, not 2048. This is the dominant heap consumer of the filer and the first thing to fail (`-ENOMEM` returns are already plumbed at both recursive sites) when memory is fragmented by fonts, art and caches.
- Fix: size the listing to the directory instead of to the constant — list into a right-sized or grow-on-demand buffer using the existing `getDirComplete(..., &more)` continuation contract (`src/filer.c:2221-2238` already documents listing-in-chunks for oversize folders), so the common case costs kilobytes and only genuinely huge folders grow. No new user-visible behaviour.
- Justification: removes a fixed ~900 KB-per-level cost; any growth logic replaces, rather than supplements, the fixed allocation at these four sites.
- Benefit: Medium · Effort: L · Risk: Medium (listing truncation semantics for >2048-entry folders must be preserved via the existing `more` contract) · Confidence: READ

### RE-LAUNCH-053 — Two identical 2049-entry sceMcTblGetDir static buffers are kept in BSS for two readers that never run concurrently
- Category: 4
- Locations: `src/filer.c:480` (`readMC`: `static sceMcTblGetDir mcDir[MAX_ENTRY + 1]`), `src/filer.c:544` (`readXFROM`: identical declaration)
- Problem: directory listing is single-threaded through `setFileList`'s dispatch — `readMC` and `readXFROM` are alternative branches for one listing, never nested or concurrent. Each nevertheless owns a private permanent BSS array of 2049 MC directory entries. One shared buffer serves both.
- Fix: delete one array; declare the buffer once at file scope and use it from both readers.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-054 — Two animated GIFs duplicate their MP4s at 8–9× the size and dominate the published site and offline ZIP
- Category: 4
- Locations: `docs-site/static/assets/ui/theme-highlights.gif` (4,885,148 B) vs `theme-highlights.mp4` (597,007 B) — same 24 s content, durations verified; `application-views.gif` (2,894,342 B) vs `application-views.mp4` (337,371 B) — same 12.5 s content; link sites `docs-site/content/ui-captures.html:9`, the applications `<details>` block, `README.md:66-70`
- Problem: 7,779,490 B of GIF vs 934,378 B for the same content as MP4; the site total is 17,359,939 B and the offline documentation ZIP ships the same assets. Both GIFs are offered only as secondary links beside an embedded MP4.
- Fix: delete both GIF files and their 3 link mentions; keep the MP4s and PNG posters. Net −7,779,490 B, −2 files.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ (script census + ffprobe durations)

### RE-LAUNCH-055 — 1,639,271 B of generated font C has no source font in the repo and no regeneration/comparison gate
- Category: 7
- Locations: `src/dash_font_data.c:2` ("GENERATED by scripts/gen_dash_font.py, do not edit", 591,973 B), `src/dash_font_emotion_engine.c:2` (492,279 B) and the bold twin (555,019 B); `docs/font-generation.md` states the original TTFs are not redistributed and gives their SHA-256 hashes — hashes that cannot be checked against anything in tree; no workflow or test regenerates or compares the outputs (grep: `gen_dash_font` appears in `tests/test_font_weight.py:16` only to parse the generator's code; `compile.yml` has no font step). Contrast `res/sfx_ps2/`, which has both a `--check` mode and a content test
- Problem: the artefacts cannot be reproduced or verified from the clone; a silent hand-edit or generator drift would be undetectable. The font pipeline kept the "do not edit" label but not the reproducibility gate its sibling audio pipeline has.
- Fix: a `--check` verification is not possible without the TTFs — so instead record, in `docs/font-generation.md`, the generated files' own SHA-256 hashes (3 lines) and check those in `tests/test_maintained_documentation.py`, so any drift is at least detected. Do not add new tooling.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-056 — Two MMCEMAN driver sources (581 lines) are compiled by nothing
- Category: 1
- Locations: `iop/mmceman/src/mmcedrv.c` (413 lines; its own `IRX_ID("mmcedrv", …)` at :19) and `iop/mmceman/src/mmceigr.c` (168 lines) — absent from the mmceman target's source list (`ps2.yaml:153-162`, 6 sources), not `#include`d by any compiled source, and referenced in no other `*.yaml`/`*.sh`/`*.md`/`*.txt` in the repo (grep-verified)
- Problem: uncompiled sources in a driver tree invite readers (and future audits) to treat them as live code, and they still get edited/checked as if they shipped. They are 2 of only 3 unexplained unlisted production sources in the whole `ps2.yaml` census (26 targets, 270 source entries, script-verified).
- Fix: delete both files, or move them under `research/` with a one-line note if they are reference material. Less code in the live driver tree either way.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ (build/reference absence script-verified; "dead" is INFERRED from total absence of build/reference paths — if they are kept as reference material, that is stated nowhere in the tree)

### RE-LAUNCH-057 — Dead vendored code in the smbd compiled set: two empty-by-#ifdef translation units and two client-only libraries linked into a server IRX
- Category: 1
- Locations: `ps2.yaml:315-316,349,344` compiles `libsmb2/lib/quic.c` (342 lines; entire body under `#ifdef HAVE_LIBQUIC`, not defined in `iop/smbd/config.h` — an empty object), `libsmb2/lib/smbdirect.c` (1,021 lines; body under `#ifdef HAVE_LIBRDMACM`, likewise undefined), `libsmb2/lib/sync.c` (1,274 lines; the client synchronous API — the server path never calls it), and `libsmb2/lib/smb2-share-enum.c` (1,005 lines; client share enumeration). Counts by `wc -l` on the pinned tree
- Problem: `--gc-sections` (`ps2.yaml:358`) strips the unreferenced client code at link time, so the IRX-size cost is mostly contained — the real cost is build time on every build, four more upstream files whose warnings/patches must be reasoned about during libsmb2 pin bumps, and audit surface that looks reachable when it is not.
- Fix: delete the four entries from the smbd target's source list in `ps2.yaml` (keep them in the fetched tree; the fetch script is unchanged). Verify with one build that the IRX still links — the serve path references none of them.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ (guards and target definition read; the link-strip claim rests on the target's own ldflags, not on a performed build)

### RE-LAUNCH-058 — Dead file-based PSU extractors and the "historical" file download entry point have no production callers
- Category: 1
- Locations: `src/update_psu.c:183-211` (`selfUpdateExtractPsu`, `selfUpdateExtractPsuMem`); `src/update_psu.c:335-364` (`selfUpdateExtractPsuSidecar` file variant); declarations `include/self_update.h:14-22`; `src/cloud_http.c:1279-1287` (`cloudHttpDownloadFd`, self-described in its comment as the "Historical self-update interface")
- Problem: production update code uses only the RAM variants — `selfUpdateLocatePsuElf` and `selfUpdateExtractPsuSidecarMem` (`src/self_update.c:306,327,333`) and `cloudHttpDownloadMem`/`cloudHttpDownloadGoogleFd` for transport. A full-text search of `src/` found the file-based extractors and `cloudHttpDownloadFd` referenced only by the host test harness (`tests/test_app_packages.py`) — they are compiled into the EE binary, reviewed, and kept compiling for no runtime purpose.
- Fix: delete `selfUpdateExtractPsu`, `selfUpdateExtractPsuMem`, `selfUpdateExtractPsuSidecar`, and `cloudHttpDownloadFd` (and their header declarations); keep the test harness exercising the RAM path it already also covers.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-059 — Action pinning is inconsistent: the `contents: write` build workflow uses mutable tags, the read-mostly metrics workflow pins SHAs for exactly that reason
- Category: 7
- Locations: `.github/workflows/download-metrics.yml:19-20` pins `actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4` with the comment "Pinned because this job holds contents: write." `compile.yml` holds top-level `contents: write` (`compile.yml:15-16`) yet uses `actions/checkout@v4` (:29), `actions/cache/restore@v4` (:285), `actions/cache/save@v4` (:301), `actions/upload-artifact@v4` (:513, :523); `docs-pages.yml:24,46`, `sync-wiki.yml` and `ps2build-sdk-watch.yml:42,57,70` likewise use `@v4` tags, several in jobs that also push or publish
- Problem: the repo's own stated rule is applied in the low-privilege workflow and not in the high-privilege one.
- Fix: apply the repo's existing pinning practice to the workflows that write/publish; no new policy needed, just use the one already written down in `download-metrics.yml`.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-060 — `scripts/stale_code_audit.sh` cannot find what it claims to scan, and is run by nothing
- Category: 7
- Locations: `scripts/stale_code_audit.sh:21` and the strict-mode check at :37 search for `LNCHELF\.CNF` (missing the `A`) — a string that occurs nowhere, so the legacy-filename scan and the strict gate can never fire on the real name; section 3 (:27) probes only `src/main.c` and `src/config.c`, while the actual legacy filename literal lives at `src/main_boot.c:66`, which is never probed; no workflow references the script, and it requires `rg`, which no CI step installs; it is nevertheless cited as a maintenance tool in `docs/architecture-maintenance.md:72,83`
- Problem: a "stale code audit" whose pattern is misspelled and whose probe files are wrong gives false assurance in both directions.
- Fix: fix the two-character typo and the probe list, or delete the script and its doc citation. A 41-line script does not need both a report mode and a strict mode that share the same broken pattern.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-061 — res/README.md resource inventory is wrong on sounds, missing on the PS2 sound set, and lists 2 CA roots for a 3-root bundle
- Category: 7
- Locations: (a) `res/README.md:8` lists `boot` among `sfx/*.adp` "Embedded by src/embed_blobs.S" — `src/embed_blobs.S:82-88` embeds only cursor/confirm/cancel/transition/message/bd_connect/bd_disconnect from `res/sfx/` (7 files, no boot); the boot sound actually embedded is the PS2 set's (`src/embed_blobs.S:96`); the 143,984 B `res/sfx/boot.adp` is therefore an embedded-claim file that no build file embeds. (b) The table has no row at all for `res/sfx_ps2/` (8 files, all embedded at `src/embed_blobs.S:91-98` and gated by `tests/test_sfx_ps2.py`). (c) `res/README.md:9` names 2 roots — the bundle contains 3 certificates (openssl subject check) and `res/UPDATE-CA.md`'s table lists all 3 including Sectigo E46
- Problem: this is the only map of `res/`; packagers and builders rely on it, and it is wrong in three independent places.
- Fix: correct row 8 (remove boot, or mark it not embedded), add one sfx_ps2 row, add E46 to row 9. Net: edit 2 rows, add 1.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-062 — 491,822 B of payload files are referenced by no build, script, test or doc-as-used asset
- Category: 7
- Locations: `res/bgm.ogg` (462,185 B) — `res/README.md:15` itself: "Not referenced by any build script and not packaged" (the packaged BGM is `res/music.sad`, `scripts/package_apps.py:37,44`); `exploits/IOPRP.IMG` (15,741 B) and `exploits/XSYSTEM2.XLF` (13,896 B) — `exploits/README.md:26-27` itself: "not referenced by any current build file; only IOPRP_LTS.IMG is embedded" (embed list at `src/embed_blobs.S:52-68` confirms). Total 462,185 + 15,741 + 13,896 = 491,822 B
- Problem: source/legacy material retained in shipped directories after its consumer was removed, with the READMEs documenting deadness instead of the files being removed — dead weight in every clone, and the exploit files in particular invite mistaken editing.
- Fix: delete the 3 files; delete the corresponding README sentences. If bgm.ogg must be kept as audio source, it belongs outside `res/` — moving it is still a deletion from the shipped tree.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ (script census)

### RE-LAUNCH-063 — Shipped LAUNCHELF.CNF template contradicts itself on the Triangle launch key
- Category: 7
- Locations: `docs/LAUNCHELF.CNF:80` comment "Circle starts the file browser… and Triangle shows About"; `docs/LAUNCHELF.CNF:88` sets `LK_Triangle_E1 =` (empty); code default (no CNF) does set Triangle to About: `src/config.c:828`; `docs-site/content/configuration.html`'s key table likewise gives Triangle = About as the default
- Problem: a user who drops the template in as-is gets different behaviour from what its own comment promises — loading the template overrides the default with empty.
- Fix: set line 88 to `LK_Triangle_E1 = MISC/About uLE` **or** delete the Triangle clause from the line-80 comment — one line either way.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-064 — SMB site page credits libsmb2 as the SMB2 driver; the shipped driver is source-built usmb2
- Category: 7
- Locations: `docs-site/content/smb.html:86` ("using the SMB2 driver from [libsmb2]"); opposite statements at `docs-site/content/credits.html:17` ("smb: uses smb2man (iop/smb2man, source-built): Ronnie Sahlberg's usmb2… it replaced the prebuilt libsmb2 smb2man.irx"), CHANGELOG Rolling, and `docs-site/content/architecture.html:4` (`smb2man (SMB2/SMB3, usmb2)`)
- Problem: wrong driver identity on the user-facing SMB page; the Credits page in the same site says the opposite. (Same page's worked examples at `smb.html:45,51` use password-shaped example values with no "example only" marker — location-only note; when editing this page, label them as examples.)
- Fix: replace "libsmb2" with "usmb2" in smb.html:86 (one word, matching credits.html:17).
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-065 — CHANGELOG Rolling NFS entry gives the pre-split settings location for NFS shares
- Category: 7
- Locations: `CHANGELOG.md:16` ("(Settings > Network > SMB shares, Server type **NFS**, with User ID and Group ID rows)"); code: NFS shares have their own page — `src/dash_settings.c:746` (`{ST_PAGE, P_NFS, "NFS shares", …}`) under Network, dedicated editor array at `src/dash_settings.c:717`; the SMB editor itself says at `src/dash_settings.c:705` "NFS shares have their own page: Settings > Network > NFS shares." `README.md:164` and `docs-site/content/nfs.html:8` already give the correct location
- Problem: Rolling is republished as the release-page summary, so the stale path is the most visible NFS instruction in the repo.
- Fix: replace the parenthetical in CHANGELOG.md:16 with "(Settings > Network > NFS shares, with User ID and Group ID rows)".
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-066 — Documentation-site Ecosystem page is a full revision behind the Ecosystem markdown it mirrors
- Category: 7
- Locations: `docs-site/content/ecosystem.html:7` table ("No 3D icons and no 'All saves' view", card contents copies "one save at a time", closing list calls templates "later work that needs design decisions"); `docs/ECOSYSTEM-COMPARISON.md:30-31` documents the shipped All-saves view and batch copy/move/delete (#232); feature exists in code at `src/dash_browser.c:2808`; `docs/README.md` classifies ECOSYSTEM-COMPARISON.md as Reference (current)
- Problem: the markdown was updated for #232; the site HTML twin was not — the published site understates shipped features and lists finished work as future work.
- Fix: update the 3 stale cells + closing list item in ecosystem.html from ECOSYSTEM-COMPARISON.md:30-31 and its "Suggested order" — replace, do not add a second comparison.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-067 — Plan/follow-up status layer is stale: a merged feature is labelled "not merged", and the newest plans carry no status at all and are absent from the docs index
- Category: 7
- Locations: `docs/follow-ups/move-manifest.md:3` ("Implemented on the draft PR branch; not merged. NOT TESTED on hardware.") — but the mechanism is in HEAD code (`src/filer_copy.c:124` `#define MOVE_MANIFEST_LIMIT`, `:143` `filerMoveBegin`) with a host suite at `tests/test_move_manifest.py`; `docs/superpowers/plans/2026-11-04-hide-tabs.md`, `docs/superpowers/specs/2026-11-04-boot-defer-design.md` and `2026-11-04-hide-tabs-design.md` open with no status banner, unlike every Oct plan/spec (dated 2026-10-26 status); the hide-tabs feature is implemented (`src/dash_themes.c:131`, `include/dash_internal.h:187`); `docs/README.md`'s index lists plans/superpowers only up to the 2026-09-26 items and never lists `docs/follow-ups/` at all
- Problem: an agent or maintainer resuming from these files would redo or re-litigate finished work.
- Fix: add the standard one-line status banner to the 4 files (move-manifest: merged, hardware untested; Nov-4 trio: implemented) and add one index line for `docs/follow-ups/` in `docs/README.md`. No new documents.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-068 — architecture-maintenance.md's largest-files list is already wrong
- Category: 7
- Locations: `docs/architecture-maintenance.md` §"Refactoring history" ("The largest hand-written sources today are src/dash_settings.c, src/dash_browser.c, src/init.c, src/main_exploit_installer.c and src/filer_browser.c") — by line count `src/dash_card_tool.c` (2,903) exceeds both `src/main_exploit_installer.c` (2,519) and `src/filer_browser.c` (2,528) and is omitted (`wc -l`: dash_settings 4,096; dash_browser 3,616; init 3,205; dash_card_tool 2,903). (The same file's CI-coverage overstatement is merged into RE-LAUNCH-045 — one mechanism, one fix location set.)
- Problem: a grown file postdates the note; maintainers scoping refactors from this list will miss the fourth-largest hand-written source.
- Fix: add `dash_card_tool.c` to the largest-files sentence.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ (script counts)

### RE-LAUNCH-069 — BUILDING.md claims to be generated from building.html; no generator exists, and the three build-prerequisite lists disagree
- Category: 7
- Locations: `docs/BUILDING.md:189` ("This file is generated from `docs-site/content/building.html`; keep the two in sync") — no generator exists (grep for BUILDING across `scripts/*.py`, `docs-site/build.py`, `.github/workflows/compile.yml` returns nothing that writes it; `tests/test_maintained_documentation.py` only asserts shared substrings in both files); the files differ in length and content (17,663 vs 14,746 chars); prerequisite lists diverge: `README.md:196` requires GNU `timeout` and `setsid`, which appear in neither BUILDING.md nor building.html (script check), while BUILDING.md requires sha256sum/host gcc/NumPy, which README.md:196 omits
- Problem: a manual-sync relationship was mislabelled as generation — contributors editing the "source" HTML will expect the MD to follow; it will not.
- Fix: change BUILDING.md:189 to "kept in sync manually with" (one line), and add `timeout`/`setsid` to BUILDING.md's tool list (or delete them from README.md:196 if the installer no longer needs them — verify against `scripts/ps2build_sdk.sh` first).
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-070 — dashboard.md calls the embedded interface sounds "Sony ADP samples"; the repo's own provenance statements say the opposite / unrecorded
- Category: 7
- Locations: `docs/dashboard.md:372` ("They are Sony ADP samples embedded in the ELF"); `docs/dashboard.md:380` itself says the PS2 set is "synthesized by scripts/gen_sfx.py (original tones, not Sony samples)"; `scripts/gen_sfx.py:6` "Nothing is sampled from Sony firmware"; `res/README.md` final paragraph: "The origin and license of the audio and icon artwork are not recorded in this repository" (for the classic set)
- Problem: one blanket sentence claims a provenance the rest of the corpus either denies (PS2 set) or declares unrecorded (classic set) — a false provenance claim in the maintainer reference.
- Fix: replace the sentence at docs/dashboard.md:372 with the two-set description already at docs/dashboard.md:380. Delete, don't add.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-071 — __Headers/ ships four APA header sample sets for other applications with a one-line, misspelled README and no usage or provenance note
- Category: 7
- Locations: `__Headers/README.MD:1` is the entire documentation ("# APA INJECTON SAMPLES" [sic]); the tree holds PP.ULE, PP.OPL, PP.GSM and PP.FHDB sets — none is Launchstation itself, and nothing in the folder says so, nor that the consumer is `src/hdd_header_injector.c` (file specs at :71-72) driven from `src/dash_hdd.c:800`
- Problem: a user browsing the repo (or a device, via the HDD Manager's header-source picker) cannot tell these are samples, whose they are, or that injecting one overwrites a partition header.
- Fix: expand `__Headers/README.MD` to the 4–5 lines the other folders use: what the sets are, that they are third-party app samples (not Launchstation), and that the HDD Manager injects them. Fix the spelling in passing. No new files.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-072 — `tests/nfsman_live/` is wired into no workflow and cannot run unattended
- Category: 7
- Locations: `tests/nfsman_live/run.sh` requires root and a running kernel NFSv4.0 server (`exportfs`, `/proc/fs/nfsd/versions` checks at the top of the script) or a pre-existing export passed as arguments; grep over `.github/workflows/`, `docs/`, `README.md`: no reference to `nfsman_live` at all, while `tests/smb2man_live/run.sh` and `tests/smbd_live/run.sh` are CI steps at `.github/workflows/compile.yml:40-48`; the Python-side NFS coverage (`tests/test_nfs_shares.py`, parser/wiring only) does run in CI
- Problem: the NFS client (`iop/nfsman`, a shipped target in `ps2.yaml`) has exactly one live suite, and unlike its SMB siblings it never runs, so its coverage is illusory.
- Fix: either wire it where a root NFS server is available or delete/retire the harness and let `test_nfs_shares.py` be the stated coverage. Keeping an unrunnable suite as apparent coverage is the worst of the three options.
- Benefit: Medium · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-073 — Two build targets are defined twice: root `ps2.yaml` and per-module `ps2.yaml` files must be kept in sync by hand
- Category: 2
- Locations: `iop/pccf/ps2.yaml` (whole file, 10 lines) redefines target `pccf` also defined in root `ps2.yaml`; `iop/ps2ips_rpc/ps2.yaml` (whole file) redefines `ps2ips_rpc` likewise; CI builds both forms (`compile.yml`: `ps2build build -c iop/pccf/ps2.yaml`, `-c iop/ps2ips_rpc/ps2.yaml`, plus the root build); `tests/test_ps2ips_rpc.py` asserts both definitions for `ps2ips_rpc`; no equivalent sync assertion was found for `pccf`
- Problem: they agree today, but nothing except one test for one of them keeps them agreeing.
- Fix: keep the isolated builds (they are a deliberate standalone-driver check) but add `pccf` to the same sync-assertion pattern `ps2ips_rpc` already has — one small addition that removes a whole drift class, instead of a third definition-tracking mechanism.
- Justification: one assertion reuses the existing pattern; it removes a drift class rather than adding a mechanism.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-074 — Dependency version strings are duplicated between build scripts and tests, so a version bump must edit test fixtures
- Category: 2
- Locations: Mbed TLS: `scripts/build_cloud_tls.sh:6` `version="3.6.7"`; the string `mbedtls-3.6.7` is hard-coded four times in `tests/test_cloud_tls_paths.py:30,47,54,64` as fixture/expected paths. libjpeg-turbo: `scripts/build_jpeg.sh:14` `version="3.1.3"`; `tests/test_llvm_toolchain.py:37` asserts the literal `'version="3.1.3"'` in the script. (Contrast, checked clean: the ps2build SDK pins in `.github/ps2build-sdk.env` are single-sourced and machine-checked by `.github/scripts/test_ps2build_sdk_watch.py`, which runs at `compile.yml:245`.)
- Problem: two single-source-of-truth violations of the same shape; each bump touches production script + test in lockstep or CI goes red for a non-regression.
- Fix: have the two tests read the version from the script they already read, instead of restating it. Deletion of duplicated literals only.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-075 — One CI-run test tests a research model that is explicitly not the shipped code
- Category: 7
- Locations: `tests/test_mmce_restore_txn_model.py:1-5` (docstring: "This tests a host-only model, not existing sd2psXtd firmware and not any PS2 restore operation"); it compiles `research/sd2psxtd/mmce_restore_txn_model.c`, whose header carries `#define MMCE_TXN_IMPLEMENTED_ON_DEVICE 0` (`research/sd2psxtd/mmce_restore_txn_model.h:13`); `docs/MMCE_RESTORE_PROTOCOL.md:8` likewise states no firmware transaction exists. The test **is** in the CI list, unlike RE-LAUNCH-045's orphans
- Problem: CI time and a green tick are spent on a model whose own header says it is not implemented on device, while the real MMCE restore path is covered by other suites.
- Fix: keep the model and its test if the protocol work is live, but stop counting it as product coverage; if the protocol work is dormant, move both out of the per-commit CI list into an on-demand run. No new test needed either way.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-076 — Two suites can report PASS while a named sub-check did not run
- Category: 7
- Locations: `tests/test_ftp_server.py:44-45` — on non-x86 hosts the freestanding 32-bit formatter check prints `SKIP: …` and the suite continues to an overall pass (the remaining FTP checks do still run and can fail); `tests/test_kit_package.py:163,205` — the .7z/Neutrino branches execute only `if py7zr is not None or shutil.which("7z")…`; with neither present those branches are silently absent from an otherwise passing run (CI installs `p7zip-full` only inside the later kit step at `compile.yml:462-484`, not before the test step)
- Problem: on the current CI platform both sub-checks do run, so this is latent: on any other platform/runner the suite stays green with less coverage and only a stdout note distinguishes it.
- Fix: make the two conditional branches assert their precondition on CI-shaped runners (x86_64 + a 7z tool are both guaranteed there), keeping the graceful path for developer machines. No new machinery.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-077 — `test_transition_latency.py` validates an audio asset through a Python reimplementation of a decoder that is not in this repo
- Category: 7
- Locations: `tests/test_transition_latency.py:9-21` (fresh Python ADPCM implementation); production decoding is not in this repo (no ADPCM decoder source under `src/`; SFX generation is `scripts/gen_sfx.py`, playback is via audsrv)
- Problem: the golden-hash check is strong for the asset, but the ADPCM decode it hashes is a fresh Python implementation, not the audsrv/SDK decoder the PS2 actually uses; a shared misunderstanding of the format would pass here and fail on hardware. INFERRED risk only — no divergence was observed; the decoded-onset and hash assertions themselves are concrete and the test passes.
- Fix: none required. If the asset is ever regenerated, re-derive the golden hash from a hardware/audsrv decode once rather than extending the Python decoder.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ (risk INFERRED)

### RE-LAUNCH-078 — First-open retry loop is copy-pasted three times in filer.c, each with its own empty busy-wait
- Category: 2
- Locations: `src/filer.c:340-382` (`readGENERICWithFirstOpenRetry`), `src/filer.c:1990-2025` (ATA branch of `setFileList`), `src/filer.c:2112-2170` (MX4SIO branch of `setFileList`)
- Problem: the same mechanism — poll `fileXioDopen` until a budget (750/2000 ms) expires, re-list on success, spin 100 ms in an empty `while (Timer() < backoff_start + 100) {}` between attempts — is implemented three times with slightly different extras (root detection, alternate path spellings). Three copies means three places to fix timing or RPC behaviour, and the copies have already drifted (only the MX4SIO copy probes alternate path forms; only the ATA copy distinguishes root budgets inline). The empty spin also burns the EE for up to 100 ms per iteration with no thread yield, in a codebase that has `DelayThread` available and uses it elsewhere.
- Fix: keep the one existing helper (`readGENERICWithFirstOpenRetry` already parameterises budget) and call it from the ATA and MX4SIO branches, deleting the two inline copies. Keep the behaviour; delete the duplication.
- Justification: net deletion — one mechanism replaces three.
- Benefit: Medium · Effort: M · Risk: Medium (device-readiness quirks are hardware-timing sensitive; each branch's budget and break conditions must be preserved in the call) · Confidence: READ

### RE-LAUNCH-079 — The sixteen Misc_* launch strings are rebuilt by three separate hand-written sprintf blocks
- Category: 2
- Locations: `src/config.c:801-816` (`initConfig`: sixteen `sprintf(setting->Misc_*, "%s/%s", …)` lines), `src/config.c:975-1003` (`loadConfig`: the inverse, sixteen hand-written `else if` branches), `src/lang.c:419-434` (language reload: the same sixteen `sprintf` lines again)
- Problem: one mechanism — "Misc_X is `Misc/` + the localised name X" — is written out longhand three times (init, load, language-switch), 16 entries each. Adding or renaming a Misc entry means editing three blocks in two files in lockstep; the blocks have already diverged in detail (`initConfig` builds `Misc_Show_Build_Info` from `LNG()` where its neighbours use `LNG_DEF()`, `src/config.c:814`). This is also the field family at the centre of RE-LAUNCH-006, so the triplication triples the overflow surface.
- Fix: drive all three sites from the one name table the entries already imply (the `LNG_DEF` ids in order), so init, load and language reload share a single loop. The fix shrinks all three blocks; it does not add a fourth mechanism.
- Justification: one shared loop replaces three hand-written blocks — net deletion of ~30 lines and two of the three copies.
- Benefit: Low · Effort: M · Risk: Medium (the load-side branches also handle the `Misc` prefix ordering constraint noted at `src/config.c:971-973`; the loop must preserve "Misc before its subprograms") · Confidence: READ

### RE-LAUNCH-080 — MainExecuteContext is assembled field-by-field three times in main.c
- Category: 2
- Locations: `src/main.c:255-266` (`Execute` builds the 11-field context), `src/main.c:305-316` (the `DashRun` call site rebuilds the identical 11 fields into a second context)
- Problem: the same eleven assignments from the same globals are written out twice, eleven lines apart in the same file. Any new context field must be added in both places, and the two copies can silently diverge — the exact failure mode a context struct exists to prevent.
- Fix: build the context once in `main()` and pass that one instance to both consumers. Delete the second assembly.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-081 — IPv4 validation is implemented three times with divergent semantics; the Neutrino copy accepts the leading-zero forms the SMB parser deliberately rejects
- Category: 2
- Locations: `src/smb_cnf.c:149-171` (`isIpv4`: rejects leading zeros, with the comment at :162-163 explaining that `010` would reach the drivers' `inet_addr` as octal — a different host); `src/neutrino_smb.c:8-19` (`ipv4`: no leading-zero rejection) used at `src/neutrino_smb.c:55-58` when re-parsing the share URL for the Neutrino handoff; `src/config_network.c:86-124` (`ipStringToOctet`/`BuildOctets`: `atoi` per octet, no 0-255 range check at all)
- Problem: the same dotted-quad string can be valid for one subsystem and mean a different address in another. A server string the browser validated under `smb_cnf.c` rules is re-parsed by `neutrino_smb.c` under weaker rules before being written into Neutrino's config (`src/neutrino_smb.c:130-187`), and the network-settings path accepts octet values the other two reject, persisting them into `IPCONFIG.DAT` (`src/config_network.c:56-66`).
- Fix: keep the strictest implementation (`smb_cnf.c`'s, which documents the octal hazard) as the single validator and call it from `neutrino_smb.c`; range-check octets in `config_network.c` or route its parsing through the same helper.
- Benefit: Medium · Effort: M · Risk: Low · Confidence: READ

### RE-LAUNCH-082 — BCD and calendar conversions are duplicated inside dash_clock_sync.c, with two different day-count algorithms
- Category: 2
- Locations: `src/dash_clock_sync.c:251-259` (`toBcd`/`fromBcd`) vs `src/dash_clock_sync.c:377-385` (`manualToBcd`/`manualFromBcd` — byte-identical bodies); `src/dash_clock_sync.c:219-249` (`civilDays`/`civilFromDays`, era arithmetic) vs `src/dash_clock_sync.c:392-412` (`dashClockDaysInMonth` + `manualSerialSeconds`, year-by-year loops)
- Problem: the SNTP path and the manual-entry path each grew their own copy of the same conversions. The BCD pairs are literal duplicates; the date paths implement the same civil-date math two ways, so a fix to leap-year or range handling in one (both feed verified RTC writes, at :291-313 and :414-482) can silently miss the other.
- Fix: delete the `manual*` BCD pair and call `toBcd`/`fromBcd`; express `manualSerialSeconds` via `civilDays` (plus seconds-of-day) and delete the loop version.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-083 — The ps2ipc socket ABI block (externs, hostent view, sockaddr view, htons) is copy-pasted between the TLS and SNTP clients
- Category: 2
- Locations: `src/cloud_http.c:37-75` (extern declarations, `CloudHostEnt`, `CloudSockaddrIn`, `cloud_htons`); `src/dash_clock_sync.c:22-48` (the same externs, `SntpHostEnt`, `SntpSockaddrIn`, `sntpHtons`)
- Problem: both network clients hand-declare the same PS2IPS RPC surface because the internal header is not installed (explained at `src/cloud_http.c:37-40`). The copies can drift independently — a struct-layout or prototype fix in one client will not reach the other, and on this ABI a drift is a wire-format bug, not a style issue.
- Fix: hoist the declarations into one small internal header (e.g. `include/ps2ipc_abi.h`) included by both `.c` files, and delete both private copies.
- Justification: one header replaces two blocks; net deletion.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-084 — The SIO2 hook exists as two near-identical copies (mmceman, mx4sio_bd) already diverged in one behaviour
- Category: 2
- Locations: `iop/mmceman/src/sio2man_hook.c` (320 lines) and `iop/mx4sio_bd/src/sio2man_hook.c` (315 lines); md5s differ; `diff` shows exactly one functional divergence: mmceman comments out `#define PORT_NR 3` with a rationale comment (both-slot access), mx4sio keeps the port filter
- Problem: both modules hook the same sio2man exports and implement the same transfer serialisation/locking. Any future locking or DMA fix must be made twice, in files that look interchangeable but are not; the one existing divergence is precisely the kind that gets silently lost when one copy is refreshed from the other.
- Fix: do **not** merge them (their one difference is deliberate and load-order-sensitive). Instead, add a one-line cross-reference comment at the top of each file naming the other copy and the intentional difference — the cheapest guard against the next silent divergence. (Contrast, checked clean: `mcman_physical`/`vmcman` share one source directory with different defines, `ps2.yaml:119-155` — the right pattern, already in use.)
- Justification: two comment lines are the smallest addition that prevents a known silent-divergence failure mode; merging was considered and rejected as riskier.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-085 — Text editor result messages are shown with five empty busy-wait spins of 1.5 s each
- Category: 5
- Locations: `src/editor_file.c:59`, `:182`, `:208`, `:250`, `:319` (all `while (Timer() < WaitTime + 1500) ;  // print operation result during 1.5 sec.`)
- Problem: each editor file operation ends by spinning the EE in an empty loop for 1.5 seconds — no draw, no pad poll, no thread yield. Input during the spin is lost, and the pattern is copy-pasted at five sites so the duration and behaviour can drift. The dashboard editor (`src/dash_editor.c`) already exists as the native replacement for this UI, which makes polishing the spin a poor investment; the minimal correct change is at the five sites themselves.
- Fix: replace the five empty spins with the single existing wait idiom that yields (or simply delete the delay at four sites and keep one shared helper for the save-result case). Do not build a new timing system for a shadowed UI (see RE-LAUNCH-044).
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-086 — filer_copy recomputes strlen(out) on every character while counting path depth
- Category: 5
- Locations: `src/filer_copy.c:1105-1108` (`for (i = 0; i <= strlen(out); i++) { if (out[i] == '/') ret++; }`)
- Problem: `strlen(out)` is evaluated on every iteration of a loop whose bound is that same length — O(n²) character scans of a path up to 1025 chars, on the HDD-`__common` folder path of every qualifying folder copy. Trivial in isolation, but it is the only `for (... strlen ...)` site in the core (script-verified) and the fix is smaller than the bug.
- Fix: hoist the length into the loop's initialiser (or walk the string once with a pointer). One line changed, nothing added.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-087 — SystemCnf_VMODE is parsed, stored and never read — write-only state
- Category: 1
- Locations: `src/main.c:70` (`char SystemCnf_VMODE[10]; // … As yet unused`), `src/main_startup.c:159-160` (populated in `scanSystemCnf`), `src/main_startup.c:176` (cleared in `readSystemCnf`), `include/launchelf.h:374` (extern declaration)
- Problem: tree-wide grep shows writes only: declaration, clear, and the `strncat` in the parser. No reader exists. The variable, its parser branch and its header declaration are pure surface — and the parser branch is one of the four overflow-prone `strncat` sites in RE-LAUNCH-018, so the dead state is not even harmless.
- Fix: delete the variable, the `VMODE` branch in `scanSystemCnf`, and the extern. If VMODE handling is ever wanted, it starts from the parser, not from a stored-but-unread global.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-088 — WriteFont_C survives as a ~57-line commented-out function inside draw.c
- Category: 1
- Locations: `src/draw.c:343-399` (`/* int WriteFont_C(char *path_arg) { … } */`, incl. its own unbounded `sprintf`/`strcat` body), `include/launchelf.h:507-508` (commented-out prototype with the note "Comment out WriteFont_C when not used (also requires patch in draw.c)")
- Problem: a complete font-dump utility is carried as a block comment in a production source file, with a matching commented prototype. It is not compiled, not tested, and contains the only `strcat(text, …)` cluster in `draw.c` — every audit (human or automated) must re-derive that it is inert. The repo has `docs/` and `scripts/` homes for exactly this kind of maintainer tooling.
- Fix: delete the commented function and the commented prototype. If the dump utility is still wanted, it belongs as a standalone script/doc artefact, not as a comment inside `draw.c`.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-089 — icon.c stores 2048 bytes as 2055 one-byte-per-line C source
- Category: 7
- Locations: `src/icon.c:2-1027` (`icon_folder[1024]`, one byte per line), `src/icon.c:1030-2055` (`icon_warning[1024]`, same format), consumers at `src/draw.c:211` and `:221`
- Problem: two small textures cost a 2055-line source file — the largest non-generated, non-font file in the core by line count — purely because of representation. The compiled bytes are identical either way; the cost is repo noise, review surface and parse time on every build. The dashboard fonts in the same tree already solved this properly: they are generated by a script (`src/dash_font_data.c:1-5`) into packed rows.
- Fix: regenerate `icon.c` in the same packed-row style the font generator already produces (multiple bytes per line), or move the two arrays to the existing embed path. Data unchanged; representation only.
- Benefit: Low · Effort: S · Risk: Low (byte-for-byte data must be preserved; verifiable by comparing the compiled arrays) · Confidence: READ

### RE-LAUNCH-090 — Development scaffolding (autotest script player, theme tour) is compiled into production sources behind ifdefs
- Category: 3
- Locations: `src/pad.c:284-313` (`#ifdef DASH_AUTOTEST` scripted-input player, includes `dash_autotest.h`), `src/dash_main.c:1295-1296` (`#ifdef DASH_AUTOTEST` sample-library hook), `src/dash_main.c:1483` (`#ifdef DASH_THEME_TOUR` preset-cycling block inside `DashRun`, "development only: show every preset for five seconds")
- Problem: two development-only features live permanently inside the two hottest core files (pad input and the dashboard main loop), each guarded by a build flag that no release configuration defines. They are never exercised by the shipped binary, so they rot silently and add conditional branches every reader of `readpad`/`DashRun` must reason past.
- Fix: delete both blocks from the production sources; if either is still used for development, it belongs on a development branch, not behind an ifdef in the shipped tree. (If a maintainer actively uses one, recording that next to the flag is the minimum alternative.)
- Benefit: Low · Effort: S · Risk: Low · Confidence: INFERRED (the blocks and their "development only" comments are READ; whether a maintainer still flips these flags outside this tree cannot be proven from the clone)

### RE-LAUNCH-091 — Failed smbd thread start leaks the created thread
- Category: 4
- Locations: `iop/smbd/smbd.c:246-270` (`start()`): if `StartThread` fails after `CreateThread` succeeded, the function returns the error with `running = 0` but never deletes the created thread; the next START creates another
- Problem: a small, bounded IOP resource leak on an error path that should be rare (thread creation succeeding while start fails) — included for completeness of the lifecycle audit; every other error path in the file was checked and cleans up.
- Fix: `DeleteThread(id)` in that branch — one call, matching the module's own `ExitDeleteThread` lifecycle.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

### RE-LAUNCH-092 — ps2ftpd keeps one static 32 KB transfer buffer for the whole server
- Category: 4
- Locations: `iop/oldlibs/ps2ftpd/src/FtpClient.c:88` (`static char buffer[32768]`)
- Problem: 32 KB of IOP RAM (of 2 MB total) is permanently reserved for a module that is loaded on demand, and the single buffer is shared across clients/transfers. The size is deliberate (comment cites round trips on slow devices such as MMCE), and the send path is properly bounded, so this is a footprint/robustness observation, not a defect with a known trigger.
- Fix: none required. If IOP RAM pressure ever forces a choice, this buffer is the largest single static in the audited IOP set and the first place to look; halving it costs round trips, so it is a trade-off to make knowingly, not a fix to apply blindly.
- Benefit: Low · Effort: S · Risk: Low · Confidence: READ

---

## Files not read (explicit)

Consolidated from the six section coverage ledgers; everything not listed here and not a binary asset was read in full, read in part with cited ranges, executed, or script-censused as recorded in the section files.

**EE core (`src/`, `include/`):**
- Not read: most of `src/dash_*.c` beyond cited ranges (dashboard UI internals incl. `dash_settings.c` full body — only the cited item arrays/ranges were read), `src/mc_*.c` and `src/mmce_*.c` bodies beyond cited lines, `src/popstarter*.c`, Ember-related sources, `src/main_exploit_installer.c` beyond grep-verified caller lines, `src/filer*.c`/`src/main*.c`/`src/config*.c` portions outside the cited ranges, `src/neutrino_gameid.c`. (Full per-file list: `redo-sections/ee-core.md` §A.4.)
- `windows/cloud-relay/` — not read (host-side relay, outside the console build).
- `ee_libs/tcpips/ps2ipc.c` body — build-graph membership and coverage verified by grep/script only.

**Network/cloud:** `src/dash_settings.c` in full (targeted ranges only, as cited); IOP sources were covered by the IOP sections, not re-read here.

**libsmb2 (pinned tree at `a52c1571a1ff223bde5bb3df9fec7af5ce3855d4` + the repo's 2 patches):**
- Not read line-by-line: the remaining unpatched upstream translation units (crypto internals, asn1, spnego internals, data encoders), `lib/ps2/`, and the client-only middles of `lib/libsmb2.c` (pattern-scanned only). The two patched files (`lib/libsmb2.c` patch sites, `lib/smb2-cmd-query-info.c`) and all server-reachable decoders cited were read.

**IOP:**
- `iop/__precompiled/` and `__v10` binary IRX blobs — not decoded (binaries; inventoried only).
- usmb2's Z80 tiny-TCP stack — not read (not in the IOP build; gate verified).
- `iop/ps2hdd_osd/` `journal.c`/`password.c` deep internals — skimmed only.
- `iop/pccf/` ATA/CF deep internals — skimmed only.
- `iop/ds34bt/` full event parser — not read in full.
- `iop/usbd-ra/` beyond skim; `iop/smap/` (`smap.c`) — not read.

**Tests/build:**
- Bodies not read in full: `scripts/package_kit.py`, `package_apps.py`, `ps2build_sdk.sh`, `check_stack_realign.py`, `release_notes.py`, `download_metrics.py`, `sync_github_wiki.py`, `gen_sfx.py`, `gen_dash_font.py` (all inventoried; most executed indirectly through their tests/workflows).
- `loader/loader.c` lines 121–446 (lines 1–120 read).
- `ee_libs/hdd/libhdd.c`, `ee_libs/iopreboot/*.c` bodies (grep/script only).
- `tests/*_live/*.c` harness bodies; `docs-site/build.py` (head + render logic read; remainder not line-read).
- Live suites not run (environment): `tests/smb2man_live/run.sh`, `tests/smbd_live/run.sh`, `tests/nfsman_live/run.sh` — need Samba/root/kernel NFS; the first two are CI-wired, the third is RE-LAUNCH-072.

**Docs/assets:**
- Licence texts: `docs/licenses/*.txt` (3 files), `LICENSE`, `ship/neutrino-ilink/LICENSE`, `docs/THIRD_PARTY_CREDITS.md` beyond grep — settled per instructions, deliberately not audited.
- `docs/changes.txt` remainder after the head (1,027-line legacy file, self-labelled pre-2014 history) — sampled only.
- `docs/plans/*.md` and `docs/superpowers/**` beyond status banners/heads — sampled (the 1,087-line 2026-09-26 plan sampled).
- Binary assets (PNG/ADP/ICN/ICO/ELF/IMG/XLF/KERNEL) — hashed, sized and reference-checked by script, not decoded, except: CA bundle subjects via openssl, GIF/MP4 durations via ffprobe/Pillow, icon.sys structure via the tree's own packaging checks.
- Translation *quality* of the 9 Lang catalogues — structure verified by script (716 entries each, IDs 0–715), semantics not audited.

**Global:** no PS2 build was attempted (no ps2build SDK in this VM) and nothing in this audit is hardware-verified; Category 5 findings are structural only. The clone was never written to.
