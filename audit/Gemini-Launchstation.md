# Launchstation Audit (`Gemini-Launchstation.md`)

**Repository:** `NathanNeurotic/Launchstation`  
**Audited commit:** `master@9fb0598a` (detailed file shards inspected `1882fe9`; zero drift on source findings up to `9fb0598a`)  
**Branches / Tags:** `2` remote branches (`master`, etc.), `143` remote tags; local tags `build-1174..1177`, `latest`  
**Tracked files / Size:** `1,065` files, `29,032,754 B`  
**Host test run (Windows 11 / Python 3.14 / MinGW):** `178/213` `tests/test_*.py` scripts passed, `35` failed (`0` timeouts). Standalone `test_app_packages.py`, `test_ember_gameid.py`, and `test_pccf.py` exited `0`. Thirty-two C-compiled host tests failed under Windows WinLibs MinGW due to missing Linux headers or ASan/UBSan runtime flags (`-fsanitize=address,undefined`); three script/compiler tests (`test_kit_package.py`, `test_build_identity.py`, `test_cloud_tls_paths.py`) failed under Windows CRLF / non-ELF COFF object checks. Linux CI targets (`compile.yml`) remain unverified locally.

---

## Cross-Repo Notes

To avoid repeating multi-repo findings verbatim, the shared issues below live in `Gemini-SUMMARY.md` with exact locations across all affected repositories:
- **`CROSS-001`:** POPS/Ember kit pin and bundled binary churn.
- **`CROSS-002`:** Copied IOP driver source trees (`usbd-ra`, `usbmass_bd`, `mx4sio_bd`, `mmceman`, `pccf`, `ata_bd`, `ps2hdd_osd`, `ds34bt`, `ds34usb`, `smbman`, `smb2man`, `nfsman`) shared with `RiptOPL` and `POPSLoader`.
- **`CROSS-003`..`CROSS-009`, `CROSS-011`:** KELF/MagicGate crypto, `libds34bt`/`libds34usb` EE+IOP wrapper bloat and RPC buffer bugs, `udpfs` duplication, release packaging, updater/manifest parity (`1882fe9` self-update notes), PS2DEV build config duplication, and AI instruction file clutter.
- **`CROSS-012`:** Shared `ds34bt` (`iop/ds34bt/iop/ds34bt.c`) and `ds34usb` (`iop/ds34usb/iop/ds34usb.c`) IOP driver bugs (`CancelAlarm`, negative `l2cap_event_cb` index, `power_level` OOB, unbounded `ds34bt_reset`, uninitialized DS4 BT report tail, unchecked USB descriptor walks and endpoint leaks, `< 0` pad checks, shared `usb_buf` race) shared with `RiptOPL` and `POPSLoader`.
- **`CROSS-014`:** Shared `smbman` (`iop/smbman/{smb.c,smb.h,smb_fio.c,poll.c}`) IOP concurrency, path, handle-check, `NetShareEnum`/`QueryPathInformation`/`Echo`, `devctl`, `FileTimeToDate`, teardown, and dead `POLLNVAL` bugs (note: `Launchstation` already hardened `smb_Read`/`smb_ReadFile`, unlike `RiptOPL`).
- **`CROSS-015`:** Shared IOP storage/USB/MMCE/APA defects across `Launchstation` and `RiptOPL`: `usbd-ra` (`hcIsoTdToIoReqLUT` clears `arg` instead of `curTd`, signed digit check, unchecked HCD init, plus Launchstation `USE_GP_REGISTER` UAF), `usbmass_bd` (unchecked descriptors, `CreateSema` pipe leak, `StartThread` overwrites `ret`), `mx4sio_bd` (`CRC7_MODE_RAM_TABLE` typo, 4 GiB `capacity` overflow, unlocked `bounce` + `const` write mutation, unsafe `module_stop`, `sysclib_IMPORTS_end`), `mmceman` (`mmce_cmd_set_gameid` `u8` wrap + stack `strcpy`, uninit SIO2 `wrbuf`, unlocked `mmce_fs_update_unit`, unaligned DMA, `sio2man_hook` `g_hook_data[1]` hardcode), `ps2hdd_osd` (`apaOpen` `clink` UAF + unchecked `fioSema`), plus 5-file SIO2 duplication (`ioplib.{c,h}`, `sio2man_hook.{c,h}`, `sio2regs.h`) between `mmceman` and `mx4sio_bd`, and shared dead `mmcedrv_config.h`.
- **`CROSS-016`:** Shared `udpfs_smap` IOP defects (`InitPHY` aborts at `i == 0`, `!SPD_REG16(...) & SPD_CAPS_SMAP` precedence bug, `HandleRxIntr` `NumPacketsReceived == 0`, unbounded link loops, `CopyToFIFO` unaligned/over-read TX tail) and precompiled `iop/__precompiled/udpfs_*.irx` core protocol/header exposure shared with `RiptOPL` and `Simple-Media-System`.
- **`CROSS-017`:** Shared `ps2ips_rpc`/`smb2man`/`nfsman` IOP bugs and `linkfile` duplication (`ps2ips_rpc/ps2ips.c` uninit `optval[128]` SIF exposure and unchecked `length`/`malign`, `smb2man` short-reply stale buffer reads, `hmac-md5.c` `k_pad[65]` copy + duplicate `usmb2` MD4/MD5/HMAC-MD5 alongside `smb2auth.c`, `nfsman/unfs4` unchecked `< 0` socket reads and progressive write pointer bugs, and 3x verbatim `linkfile` copies across `iop/{smb2man,smbd,nfsman}/linkfile`).

---

## Findings (Sorted by Benefit)

### LAUNCHSTATION-001: 17 IOP IRX drivers embedded twice (inside `wle_igr` and `wLaunchELF`)
- **Category:** 2, 4 (Duplication, Size and memory)
- **Location(s):** `Launchstation/ps2.yaml:454-484`, `Launchstation/ps2.yaml:644-700`, `Launchstation/src/mmce_igr.c:16-24`
- **Problem:** `wle_igr` (`WLE-IGR.ELF`) embeds 17 core IOP modules (`iomanx`, `filexio`, `sio2man`, `mcman`, `mcserv`, `mmceman`, `usbd`, `bdm`, `bdmfs_fatfs`, `usbmass_bd`, `mx4sio_bd`, `iLinkman`, `IEEE1394_bd`, `ps2dev9`, `ata_bd`, `ps2hdd`, `ps2fs`). `wLaunchELF` then embeds the entire `wle_igr` binary (`wle_igr_elf`) alongside those exact same 17 modules, storing two full copies of the entire IOP storage stack in the release ELF.
- **Fix:** Keep `WLE-IGR.ELF` outside the main binary (as a sidecar in the kit ZIP/PSU archive installed by `scripts/package_apps.py`) instead of embedding `wle_igr_elf` in `Launchstation/ps2.yaml:646`.
- **Benefit:** Removes one complete duplicate copy of 17 IOP IRXs plus the `wle_igr` EE stub (`INFERRED`: ~350 to 450 KB uncompressed / ~150+ KB packed ELF size saved) and drops `src/mmce_igr.c` embedded verify/install copy logic.
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### LAUNCHSTATION-002: Three separate SMB codebases, 53-file `libsmb2` build, and duplicate EE `mc_sha256.c`
- **Category:** 2, 4, 7 (Duplication, Size and memory, Build clutter)
- **Location(s):** `Launchstation/ps2.yaml:243-274` (`smb2man`), `Launchstation/ps2.yaml:293-359` (`smbd`), `Launchstation/ps2.yaml:361-383` (`smbman`), `Launchstation/scripts/fetch_libsmb2.sh:1-31`, `Launchstation/src/mc_sha256.c:1-94`, `Launchstation/ps2.yaml:642` (`mbedcrypto`)
- **Problem:** `wLaunchELF` compiles and embeds three separate SMB implementations: `smbman` (SMB1 client), `smb2man` (`usmb2` client), and `smbd` (`libsmb2` server fetched by `fetch_libsmb2.sh`, compiling 53 files including `quic.c`, `smbdirect.c`, `aes_reference.c`, `sha384-512.c`, `spnego-wrapper.c`, and client command encoders). On EE, `src/mc_sha256.c` reimplements SHA-256 even though `mbedcrypto` (`mbedtls_sha256`) is already linked via `ps2.yaml:642` and used in `src/cloud_google.c` and `src/cloud_http.c`.
- **Fix:** Replace `src/mc_sha256.c` calls in `src/mc_card_view.c`, `src/mc_save_export.c`, and `src/mmce_save_versions.c` with `mbedtls_sha256` and delete `src/mc_sha256.c`. For IOP SMB, drop `smbman` (SMB1) if SMB2/3 covers supported servers, or share `libsmb2`/crypto between `smb2man` and `smbd` and trim unused `libsmb2` files (`quic.c`, `smbdirect.c`, client-only PDUs) from `ps2.yaml:300-352`.
- **Benefit:** Eliminates 94 lines (`src/mc_sha256.c`) on EE immediately; consolidating SMB stacks and trimming `libsmb2` removes up to two SMB engines and unused transport/codec code (`INFERRED`: ~60 to 150 KB IRX bytes across `.rodata`/`.text` plus faster CI builds).
- **Effort:** S (`mc_sha256.c`), L (SMB consolidation). **Risk:** Low (`mc_sha256`), High (SMB). **Confidence:** High (`READ`).

### LAUNCHSTATION-003: Synchronous 1 MB dead double-buffer in `filer_copy.c` and ~390+ KB pinned PS1/MC static BSS
- **Category:** 2, 4 (Duplication, Size and memory)
- **Location(s):** `Launchstation/src/filer_copy.c:32, 1427-1436, 1557-1565, 1591-1592, 1698`, `Launchstation/src/dash_ui.c:1436-1450`, `Launchstation/src/dash_ps1_card.c:28-36`, `Launchstation/include/mc_ps1_card.h:41, 43`, `Launchstation/include/mc_save_export.h:52`, `Launchstation/src/filer.c:480, 544`, `Launchstation/include/launchelf.h:205`
- **Problem:** During fast copies (`COPY_BUFFER_FAST_DEFAULT = 0x100000`, `1 MB`), `copy()` allocates a second 1 MB aligned buffer `buff2` (`filer_copy.c:1427-1430`) and flips `buffIndex ^= 1` each chunk (`1591-1592`), yet both `copyChunkIo(&chunk)` and `DashFileTransferChunk(copyChunkRun, &chunk)` (`dash_ui.c:1447-1448`) wait synchronously for read+write completion before returning, so `buff2` never overlaps I/O. Separately, `dash_ps1_card.c:28-36` permanently reserves three `PS1_CARD_SIZE` (`131,072 B`) aligned BSS buffers (`393,216 B` / `384 KB`) plus `p1_head[3904]`, `p1_titles[15][96]`, and `p1_details[15][64]` (`399,520 B` before `p1_saves[15]` and `p1_items[17]`) even when closed, and `filer.c:480, 544` defines two separate `static sceMcTblGetDir mcDir[MAX_ENTRY + 1]` arrays (`2,049` entries each) for `readMC` and `readXFROM`.
- **Fix:** Remove `buff2` and `buffIndex` toggling in `filer_copy.c` (or pipeline read/write asynchronously), dynamically allocate `dash_ps1_card.c` buffers only while the PS1 Card Tool runs, and share one `mcDir` buffer/helper between `readMC` and `readXFROM`.
- **Benefit:** Reclaims `1 MB` of EE heap during fast transfers, frees `399,520+ B` (`~390+ KB`) of permanent PS1 BSS, and removes one `2,049`-entry `sceMcTblGetDir` static table.
- **Effort:** S (`buff2`, `mcDir`), M (`dash_ps1_card.c`). **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-004: ~440 KB pinned static BSS in on-demand utilities plus 2.56 MB artwork cache ceiling
- **Category:** 4 (Size and memory)
- **Location(s):** `Launchstation/src/pc_game_receiver.c:31-32` (`request_text[8192]` + `transfer_block[262144]` = `270,336 B`), `Launchstation/src/mmce_backup.c:17-18, 42` (`compare_a[65536]` + `compare_b[65536]` + `fp_cache[4][4096]` = `147,456 B`), `Launchstation/src/mmce_igr.c:32` (`verify_buf[32768]`), `Launchstation/src/dash_art.c:28-47`
- **Problem:** Even when no PC transfer, MMCE backup, or IGR install is active, these modules permanently pin `450,560 B` (`440 KB`) of static EE BSS RAM, while `src/dash_art.c:28-47` retains up to `40 * 128 * 128 * 4` (`2.56 MB`) of heap artwork thumbnails simultaneously.
- **Fix:** Allocate `transfer_block`, `compare_a`/`compare_b`, `fp_cache`, and `verify_buf` with `memalign(64, ...)` only while the operation runs and free them before returning, and reduce `ART_SLOTS` in `src/dash_art.c:28` from `40` to a visible viewport count (e.g. `16-20`).
- **Benefit:** Frees `440 KB` of static EE RAM permanently and cuts artwork heap ceiling by `~1.28 to 1.5 MB`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-005: ~231.5 KB non-concurrent static BSS scratch across `dash_card_tool.c`, `dash_browser.c`, and `dash_info.c`
- **Category:** 4 (Size and memory)
- **Location(s):** `Launchstation/src/dash_card_tool.c:29-48, 554, 988, 1217, 1404`, `Launchstation/src/dash_browser.c:808, 964, 1040, 1065, 1158, 1388-1389, 2684, 2781, 3031`, `Launchstation/src/dash_info.c:22-34, 129, 147, 220, 347`, `Launchstation/include/mc_card_view.h:33-58`
- **Problem:** Separate modal screens pin large mutually exclusive static arrays in EE `.bss`: `dash_card_tool.c` keeps `ct_cache[4][4096]`, `ct_view`, `ct_saves[250]`, `ct_titles[250][96]`, `ct_details[250][96]`, `ct_items[253]`, `back[4096]`, and three identical `sb[512] + root[1024]` pairs (`~107,868 B`); `dash_browser.c` keeps eight `static int ...[MAX_ENTRY]` arrays (`65,536 B`) plus `buf_a[16384]`/`buf_b[16384]` (`32,768 B`, `98,304 B` total); and `dash_info.c` keeps four separate `static Sheet s;` structs (`6,340 B` each = `25,360 B`) even though only one info sheet displays modally at a time.
- **Fix:** Allocate `dash_card_tool` session state and `dash_browser` comparison/index scratch on demand (or share one module-scoped scratch buffer in `dash_browser.c`), and share a single `Sheet` struct across `dash_info.c`.
- **Benefit:** Reclaims `~231.5 KB` (`231,532 B` unpadded) of permanent EE `.bss` RAM.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-006: Over 17 MB of demo videos/GIFs and committed AI session plans/scratch models in repo
- **Category:** 7 (Build/CI/repo clutter)
- **Location(s):** `Launchstation/docs-site/static/assets/ui/theme-highlights.gif` (`4,885,148 B`), `Launchstation/docs-site/static/assets/ui/application-views.gif` (`2,894,342 B`), `Launchstation/docs-site/static/assets/ui/theme-tour.mp4` (`1,095,595 B`), `Launchstation/docs-site/` (`123` files, `17,367,161 B`), `Launchstation/docs/plans/` (`10` files, `104,624 B`), `Launchstation/docs/superpowers/` (`5` files, `62,303 B`), `Launchstation/research/sd2psxtd/` (`3` files, `17,055 B`), `Launchstation/exploits/` (`14` files, `147,696 B`), `Launchstation/__Headers/` (`13` files, `129,380 B`)
- **Problem:** The main code repository commits `17.37 MB` of static website media/assets under `docs-site/`, `166,927 B` of point-in-time AI triage/implementation plans (`docs/plans/`, `docs/superpowers/`), `17 KB` of standalone research model code (`research/sd2psxtd/`), and legacy exploit payloads/`__Headers/` (`277 KB`), bloating every clone and CI archive.
- **Fix:** Move historical plans, `research/sd2psxtd/`, and heavy `docs-site` video/GIF assets to GitHub Releases or a separate docs/wiki branch, and prune unused legacy `exploits/` / `__Headers/` if no build/runtime path consumes them.
- **Benefit:** Removes up to `~17.8 MB` from the default git checkout.
- **Effort:** S. **Risk:** Low (verify `src/main_exploit_installer.c` / `src/embed_blobs.S` consumers before deleting `exploits/` files). **Confidence:** High (`READ`).

### LAUNCHSTATION-007: 1.71 MB of C font source (585,728 B pixel `.rodata`) plus classic `font_uLE.c`
- **Category:** 2, 4 (Duplication, Size and memory)
- **Location(s):** `Launchstation/src/dash_font_data.c:1-30` (`601,630 B`, `215,040 B` pixel arrays), `Launchstation/src/dash_font_emotion_engine.c:1-30` (`500,315 B`, `176,128 B` pixel arrays), `Launchstation/src/dash_font_emotion_engine_bold.c:1-30` (`563,823 B`, `194,560 B` pixel arrays), `Launchstation/src/font_uLE.c:1-100` (`41,910 B`)
- **Problem:** The ELF embeds four font datasets: classic `font_uLE.c` (`4,096 B` bitmap) plus three separate multi-size 8-bit grayscale raster atlases (`Roboto-Regular`, `EmotionEngine`, `EmotionEngineBold`) totaling `585,728 B` (`572 KB`) of raw uncompressed pixel bytes in `.rodata`.
- **Fix:** Drop one or more redundant font faces/prebaked sizes (or scale one cleanly packed atlas) and retire `font_uLE.c` once classic UI text paths are migrated or unified.
- **Benefit:** Removing the alternate Emotion Engine regular+bold atlases alone saves `370,688 B` (`362 KB`) of `.rodata` (`1,064,138 B` less generated C source; `INFERRED` smaller packed ELF).
- **Effort:** M. **Risk:** Med (font aesthetic choice). **Confidence:** High (`READ`).

### LAUNCHSTATION-008: Unreferenced audio file `res/bgm.ogg` (462 KB) and unused PNG pad icons (27 KB)
- **Category:** 7 (Build/CI/repo clutter)
- **Location(s):** `Launchstation/res/bgm.ogg` (`462,185 B`), `Launchstation/res/icons/*.png` (`16` files, `27,375 B`), `Launchstation/res/UPDATE-CA.md` (`2,310 B`), `Launchstation/res/WLE-CLOUD.CNF.example` (`626 B`), `Launchstation/src/dash_icons.c:1-7`
- **Problem:** `res/bgm.ogg` (`462,185 B`) has zero references in `src/`, `scripts/`, `ps2.yaml`, or `.github/` (the shipped BGM sidecar comes from `res/music.sad` packaged as `BGM.ADP` by `scripts/package_apps.py:37,44`). Similarly, `src/dash_icons.c:3-6` generates all pad glyphs from signed distance functions at startup, leaving all 16 `res/icons/*.png` files unreferenced.
- **Fix:** Delete `res/bgm.ogg` and the 16 unused `res/icons/*.png` files (keep `res/icons/rocket-launch.svg` only if `docs-site/` uses it), plus `res/UPDATE-CA.md` / `res/WLE-CLOUD.CNF.example` if `docs/` supersedes them.
- **Benefit:** `489,560 B` (`~478 KB`) removed from the repository (`+2,936 B` if stale `res/` examples are pruned).
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-009: Legacy v1.0 IP/DEV9 stack and precompiled UDPFS binaries embedded alongside current/source modules
- **Category:** 1, 2, 4 (Dead code, Duplication, Size and memory)
- **Location(s):** `Launchstation/src/init.c:28-47`, `Launchstation/src/embed_blobs.S:74-80`, `Launchstation/ps2.yaml:28-35`, `Launchstation/ps2.yaml:644-656`, `Launchstation/iop/__v10/ps2dev9.irx` (`13,193 B`), `Launchstation/iop/__v10/ps2ip.irx` (`118,513 B`), `Launchstation/iop/__precompiled/udpfs_ioman.irx` (`11,633 B`), `Launchstation/iop/__precompiled/udpfs_ministack.irx` (`5,025 B`), `Launchstation/iop/__precompiled/udpfs_smap.irx` (`8,081 B`)
- **Problem:** `Launchstation` embeds two full DEV9/TCP-IP stacks (`dev9:ps2dev9`, `netman`, `smap:smap_netman`, `ps2ip-nm:ps2ip`, `smap-ps2ip:ps2smap` in `ps2.yaml:650-655` plus prebuilt `iop/__v10/ps2dev9.irx` and `iop/__v10/ps2ip.irx`, even though `load_ps2ip_v10` has zero callers anywhere in `src/`) and both source-built `udpfs_smap` (`ps2.yaml:28`) and three precompiled `iop/__precompiled/udpfs_*.irx` binaries (`24,739 B`).
- **Fix:** Remove the dead `iop/__v10` binaries (`131,706 B`), unreferenced `smap-ps2ip:ps2smap` if only used for v1.0 PS2IP, and `iop/__precompiled/udpfs_smap.irx` (`8,081 B`, already built from `iop/udpfs_smap/`); build `udpfs_ministack` and `udpfs_ioman` from source or drop duplicate `udpfs_smap`.
- **Benefit:** Removes `131,706 B` of dead `iop/__v10/*.irx` (plus `8,081 B` duplicate `udpfs_smap.irx` and `smap-ps2ip`), shrinking the ELF and binary repo footprint (`INFERRED`: ~140+ KB uncompressed IRX data).
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### LAUNCHSTATION-010: Unreachable classic Main Menu (`src/main_menu.c`), unreachable `dashDevicesScreen` (`src/dash_files.c`), and ~2,900+ lines of duplicated classic UI
- **Category:** 1, 3, 4 (Dead code, Overengineering, Size and memory)
- **Location(s):** `Launchstation/src/main.c:315-373`, `Launchstation/src/main_menu.c:1-348`, `Launchstation/src/main_actions.c:732-803`, `Launchstation/src/main_actions.c:875-886`, `Launchstation/src/main_info_screens.c:1-394`, `Launchstation/src/config_screen.c:1-361`, `Launchstation/src/config_startup.c:1-364`, `Launchstation/src/config_network.c:1-362`, `Launchstation/src/config_advanced.c:1-329`, `Launchstation/src/dash_files.c:85-185, 211, 245-732`, `Launchstation/src/dash_main.c:24, 43-49, 104-116, 206-214, 1315-1327, 1441`, `Launchstation/src/dash_settings.c:1072-1075, 1777-1779`, `Launchstation/src/dash_store.c:255, 343, 551`, `Launchstation/tests/test_zack_feedback.py:12, 28-29`
- **Problem:** `src/main.c:315` calls `DashRun(&dash_ctx, 1)` unconditionally, making `src/main.c:318-373` and all of `src/main_menu.c` (`348` lines) dead code, while `ExecuteMainAction` still routes legacy actions to classic UI screens alongside the dashboard's `dash_settings.c`, `dash_editor.c`, `dash_browser.c`, and `dash_info.c`. Inside the dashboard itself, `visible_sections[]` (`dash_main.c:48`) excludes `DASH_SEC_DEVICES` and remaps it to `DASH_SEC_FILES`, leaving the entire `732`-line `src/dash_files.c` (`dashDevicesScreen`, `dashFilesListScreen`, `dashDevicesGridScreen`), its `30,272 B` static BSS (`fentries[64]`, `fdevs[24]`, `devs[24]`), and `dashCfg.files_mode` unreachable while `dash_main.c:1441` calls `dashFilesPoll()` every frame.
- **Fix:** Delete `src/main.c:318-373`, `src/main_menu.c`, `include/main_menu.h`, `src/dash_files.c`, `&dashDevicesScreen`, `dashFilesPoll()`, and `files_mode` (updating `ps2.yaml` and `tests/test_zack_feedback.py:12, 28-29`), then retire classic duplicate `config_*.c` / `main_info_screens.c` / `editor*.c` screens if superseded by the dashboard.
- **Benefit:** Immediately eliminates `1,136` lines (`main.c:318-373` + `main_menu.c` + `dash_files.c`), stops a wasted per-frame poll call, and reclaims `30,272 B` of EE `.bss` (`~2,900+` additional lines when classic UI duplicates are retired).
- **Effort:** S (`main_menu.c` + `dash_files.c`), L (classic UI retirement). **Risk:** Low (immediate removals), Med (classic UI retirement). **Confidence:** High (`READ`).

### LAUNCHSTATION-011: EE RPC clients omit negative reply length bounds, `!_ps2ip.server` checks, `Hdl_Info_BindRpc` error handling, and `sizesMB[9]` bounds
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/ee_libs/tcpips/ps2ipc.c:114-142, 341-430, 616-698`, `Launchstation/src/cloud_http.c:251`, `Launchstation/src/hdl_rpc.c:13-22, 26-83`, `Launchstation/include/hdl_rpc.h:9-15`, `Launchstation/src/init.c:2571`, `Launchstation/src/dash_hdd.c:267, 281, 448`, `Launchstation/ee_libs/hdd/libhdd.c:58-68, 295-457`, `Launchstation/ps2.yaml:423-427, 504, 553`
- **Problem:**
  1. `ps2ipc_accept` (`131-135`), `ps2ipc_getsockname` (`633-634`), `ps2ipc_getpeername` (`660-661`), and `ps2ipc_getsockopt` (`690-691`) clamp `pkt->len < *outlen` without checking `< 0`, pointer `NULL`, or struct capacity, while `ps2ipc_send` (`341-383`) and `ps2ipc_sendto` (`385-430`) omit `if (!_ps2ip.server) return -1;` and `size < 0` checks.
  2. `Hdl_Info_BindRpc` (`hdl_rpc.c:31-49`) sets `Hdl_Inited = 1;` even if bind times out (`client.server == 0`); `HdlGetGameInfo` and `HdlRenameGame` ignore negative `SifCallRpc` returns and copy strings via unbounded `strcpy` into `33`-byte and `64`-byte packet fields (`60, 77-78`).
  3. `hddMakeFilesystem` (`libhdd.c:309-410`) leaves `maxIndex == 9` if `hddMaxPartitionSize` misses `sizesMB[0..8]` (reading `sizesMB[9]` OOB at `340`), accepts `fsSizeMB <= 0` multiples of `128`, and indexes `sizesMB[--useIndex]` (`406-410`) before checking `useIndex < 0`.
- **Fix:** Validate `[0, max_field_len]` and non-NULL pointers in `ps2ipc_*` and add `if (!_ps2ip.server || size < 0) return -1;` to `ps2ipc_send`/`sendto`; fail `Hdl_Info_BindRpc` when `client.server == 0`, check `SifCallRpc < 0`, and use `snprintf`; reject `fsSizeMB <= 0`, clamp `maxIndex` to `8`, and check `useIndex > 0` before decrementing in `libhdd.c`.
- **Justification:** Prevents EE stack/heap corruption, silent `SifCallRpc` stale reads, and out-of-bounds `sizesMB` array reads during socket, HDL, and APA formatting calls.
- **Benefit:** Closes three EE-side RPC/partition memory-safety defects in `wLaunchELF`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-012: Classic CNF parser negative `PathPad[]` index, unbounded `strcpy`/`sprintf`, unchecked `malloc`, and duplicate `PathPad_Lock`
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/config.c:441, 621, 686, 799-801, 962, 972-1004, 1040-1067, 1095-1101, 1512-1513, 1553`, `Launchstation/include/launchelf.h:203-204, 281-307, 338-339, 548`
- **Problem:** `loadConfig` (`1095-1101`) parses `PathPad[` indices without checking `i >= 0`, allowing negative out-of-bounds writes; copies up to `128 KB` CNF values with unbounded `strcpy` into `32`-byte `SMB_Server_User`/`SMB_Server_Pass` (`1051, 1053`) and `MAX_PATH` fields and unbounded `sprintf` into `64`-byte `setting->Misc*` fields (`972-1004`) plus `LK_Title[s]` (`1553`); dereferences unchecked `malloc(sizeof(SETTING))` (`799, 1512`); and writes `"PathPad_Lock = %d\r\n"` twice (`621, 686`).
- **Fix:** Validate `i >= 0 && i < MAX_PATH_PAD`, replace `strcpy`/`sprintf` into `SETTING` fields with bounded copies, check `malloc(sizeof(SETTING)) != NULL`, and remove the duplicate `PathPad_Lock` output at `686`.
- **Justification:** Prevents BSS/heap corruption and `NULL` dereferences on malformed `LAUNCHELF.CNF` files.
- **Benefit:** Fixes CNF parser OOB/overflow hazards and removes duplicate config output.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-013: `dashModalKeyboard` off-by-one stack buffer overflows and silent 255-byte OAuth truncation
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/dash_widgets.c:341-350, 447, 453-454, 587`, `Launchstation/src/dash_settings.c:398, 688-693, 3375-3379, 3392-3393, 3406-3409`, `Launchstation/src/dash_tools.c:1145-1152`
- **Problem:** `dashModalKeyboard(title, buf, max_len)` allows `max_len` characters plus NUL (`max_len + 1` bytes) while internally capping `max_len` at `sizeof(orig) - 1` (`255`). Three callers (`smbedEditField` `char edit[96]`, `S_SMBED_PORT` `char buf[8]`, `S_SMBED_UID`/`S_SMBED_GID` `char buf[12]`) pass `sizeof(buf)` instead of `sizeof(buf) - 1`, overflowing each stack buffer by 1 byte on full input or cancel; meanwhile `S_SMBED_FOLDER` (`128` B) is clamped to `95` characters and `384`-byte Google OAuth credentials (`dash_tools.c:1146-1151`) are silently capped at `255`.
- **Fix:** Update `dashModalKeyboard` to take buffer capacity (`sizeof(buf)` including NUL), enlarge/allocate `orig` for `384`-byte OAuth fields, and raise `edit` in `smbedEditField` to `128`.
- **Justification:** Fixes three deterministic 1-byte stack overflows and removes silent truncation of valid OAuth secrets/share paths.
- **Benefit:** Eliminates stack corruption in network settings modals and restores full OAuth credential entry.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-014: `make_icon` `32 KB` leak, 3x unchecked `malloc`, RLE heap overflow & `u16` wrap, ignored `colour`, and `strcpy` into `16 B` `mcIcon`
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `Launchstation/src/makeicon.c:110-162, 165-226, 236-256, 270-372, 378-423`, `Launchstation/ps2.yaml:573`, `Launchstation/src/filer_browser.c:1479, 1497, 2438, 2455`
- **Problem:** `make_icon()` allocates `tex_buffer = malloc(128 * 128 * 2)` (`337`) and never frees it on any path (`344, 364, 367`), all three `malloc` calls (`238, 252, 337`) lack `NULL` checks, `tex_compresRLE` (`238-251`) allocates `16,384` `u16`s and uses `u16 outbufferpos` (overflowing the heap whenever runs exceed `8,192` and wrapping `u16` at `16,384`), `tex_drawChar`/`tex_printXY` ignore `colour` and hardcode `0xFFFF`, and `make_iconsys` (`408-410`) copies `iconname` with unbounded `strcpy` into `16`-byte `mcIcon.{view,copy,del}` arrays.
- **Fix:** Free `tex_buffer` on every `make_icon` exit, `NULL`-check all three allocations, size `new_tex` for worst-case RLE output (`128 * 128 * 4` with `u32 outbufferpos`) or fall back to raw type `0x07`, use or drop `colour`, and bound `iconname` copies to `sizeof(icon_sys.view)`.
- **Justification:** Stops a 32 KB leak per generated MC icon and closes unchecked allocation and RLE/`mcIcon` buffer overflows.
- **Benefit:** Eliminates per-icon heap leaks and heap/struct corruption in MC icon generation.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-015: `delete()` discards normalized `dir` before `genRmdir`, and `make_title_cfg` indexes `strlen - 4`
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/filer_actions.c:1922-1927, 1972-1978, 2022-2025`
- **Problem:** After `delete()` clamps names via `genLimObjName(dir, 0)` (`1974`) and translates `host:/` paths via `makeHostPath(dir, dir)` (`1977`), its fallback directory branch (`2022-2025`) overwrites `dir` with `sprintf(dir, "%s%s", path, file->name);` right before `genRmdir(dir)`. Separately, `make_title_cfg` (`1922-1927`) writes `ELF_NAME[strlen(ELF_NAME) - 4] = '\0';` without checking `strlen >= 4` and builds `new_title_cfg[MAX_PATH]` with unbounded `strcpy`/`strcat`.
- **Fix:** Remove `sprintf(dir, "%s%s", path, file->name);` at `2023` so `genRmdir(dir)` uses the normalized path (trimming trailing `'/'` if needed), validate length `>= 4` before stripping `.ELF`, and format `new_title_cfg` with `snprintf`.
- **Justification:** Restores `host:/` translation and length clamping on folder deletion and prevents pre-array writes on short ELF filenames.
- **Benefit:** Fixes directory-delete path corruption and OOB writes in `make_title_cfg`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-016: `filer.c` `info[max]` overruns, unbounded HDD partition loops, overlapping `strcpy`, and duplicated net readers
- **Category:** 2, 6 (Duplication, Bugs and security)
- **Location(s):** `Launchstation/src/filer.c:952-955, 967-970, 991-995, 1000-1003, 1170-1225, 1244-1263, 1771-1821, 1826-1864`, `Launchstation/include/launchelf.h:206`
- **Problem:** `readHOST` (`1771-1819`) and `readNetDir` (`1854-1862`) write past `info[max - 1]` (via `info[hostcount++]` before breaking in `elflist.txt`, plus `strcpy(info[count].name, "\0")` when `count == max`), `readHDD`/`readHDDDVRP` (`1172-1250`) iterate up to `MAX_PARTITIONS` (`1400`) without checking `i < max` or calling `clearMcTable`, `genFixPath` (`954-1003`) uses overlapping `strcpy`, and `readHOST` / `readNetDir` duplicate the `iox_dirent_t` conversion loop.
- **Fix:** Bound `hostcount`, `count`, and HDD partition loops strictly by `< max` with `clearMcTable`, omit or guard sentinel writes, replace overlapping `strcpy` with `memmove`, and extract a shared `iox_dirent_t`-to-`FILEINFO` helper for `readHOST` and `readNetDir`.
- **Justification:** Eliminates `FILEINFO` array overruns across large directories/partition tables and UB in path normalization.
- **Benefit:** Stops array overflows and deduplicates the network listing loop.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-017: Unchecked `genRead` on stack `FontHeader`, uninitialized `iox_stat`, and `bootpath[256]` stack overflow in `elf.c`
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/draw_text.c:74-96`, `Launchstation/src/filer_copy.c:763, 1110-1113`, `Launchstation/src/elf.c:11, 730, 762, 775-780, 787, 797-801`
- **Problem:** `loadFont` (`draw_text.c:74-95`) calls `genClose(fd)` when `fd < 0` and ignores `genLseek`/`genRead` returns before checking stack `FontHeader[0..83]`, `copy()` (`filer_copy.c:763, 1110-1113`) passes uninitialized stack `iox_stat` to `fileXioChStat`, and `RunLoaderElfInternal` (`elf.c:730-801`) formats up to `MAX_PATH` (`1025`) HDD/DVRP paths into `char bootpath[256];` with `sprintf` while carrying two unreachable `else` branches (`777-779, 799-801`).
- **Fix:** Check `genOpen >= 0`, `genLseek >= 0`, and full `genRead` counts in `loadFont`; `memset(&iox_stat, 0, sizeof(iox_stat))` in `copy()`; size `bootpath` to `MAX_PATH` using `snprintf`; and delete the unreachable `else` branches in `elf.c`.
- **Justification:** Prevents uninitialized stack metadata/header parsing and closes a stack overflow on deep HDD ELF paths.
- **Benefit:** Fixes font load validation, `fileXioChStat` garbage fields, and `bootpath` stack corruption.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-018: Out-of-bounds `Editor_nRowsWidth` reads, `msg[MAX_PATH]` overflow, and dead `\n` check in classic editor
- **Category:** 1, 6 (Dead code, Bugs and security)
- **Location(s):** `Launchstation/src/editor_rules.c:66-67, 89-92`, `Launchstation/src/editor_file.c:189, 194-197, 229-232`, `Launchstation/include/editor_private.h:63`
- **Problem:** `editorApplyRules()` indexes `Editor_nRowsWidth[i + 1]` at the last line (`66-67`) and `Editor_nRowsWidth[-1]` at line `0` (`89-92`), `editor_file.c:197` overflows `char msg[MAX_PATH]` via `sprintf(msg, LNG(File_Closed), Path[Win])` when path + format text exceeds `1024` bytes, and `editor_file.c:229` checks `TextBuffer[Win][TextSize[Win]] == '\n'` (the NUL terminator offset) rather than `TextSize[Win] - 1`.
- **Fix:** Guard `i + 1 < Editor_nRowsNum` and `i > 0` in `editor_rules.c`, switch `sprintf(msg, ...)` to `snprintf(msg, sizeof(msg), ...)`, and check `TextSize[Win] > 0 && TextBuffer[Win][TextSize[Win] - 1] == '\n'` in `editor_file.c:229`.
- **Justification:** Prevents out-of-bounds reads during cursor navigation and stack overflow on long editor paths.
- **Benefit:** Fixes cursor OOB reads, path-message stack overflow, and dead trailing-LF check.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-019: Per-keystroke `TextBuffer` heap leak, shrinking clipboard paste bug, and duplicated input paths in `editor_input.c`
- **Category:** 2, 4, 6 (Duplication, Size and memory, Bugs and security)
- **Location(s):** `Launchstation/src/editor_input.c:255-306, 369-375, 570-578, 644-650`
- **Problem:** Both `editorVirtualKeyboardEntry` (`372`) and `editorKeyboardEntry` (`647`) comment out `free(TextBuffer[Active_Window])` before reassigning `malloc(...)`, leaking the entire document buffer on every character inserted. Both paste paths (`300-303, 572-574`) also decrement global `Mark[MARK_SIZE]` in place, shrinking the clipboard on each paste, and duplicate ~200+ lines of edit/clipboard logic.
- **Fix:** Restore `free(TextBuffer[Active_Window])` before reallocation at `372` and `647`, use a local paste-length variable instead of mutating `Mark[MARK_SIZE]`, and extract shared edit/clipboard helpers.
- **Benefit:** Stops continuous heap leaks while editing, preserves clipboard contents, and removes ~200+ lines (`INFERRED`) of duplicate editor code.
- **Effort:** S (`free`/paste fix), M (helper extraction). **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-020: Additional `ps2hdd_osd` defects (`apaDelete`/`apaGetNextHeader` UAF, `apaRename` double `SignalSema`, unchecked cache/journal/`ioctl2`/`devctl`)
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/iop/ps2hdd_osd/apa.c:43-68, 379-405, 504-521`, `Launchstation/iop/ps2hdd_osd/cache.c:34-41`, `Launchstation/iop/ps2hdd_osd/hdd.c:130-131, 158-163, 173-184, 270-272`, `Launchstation/iop/ps2hdd_osd/hdd_blkio.c:215-219`, `Launchstation/iop/ps2hdd_osd/hdd_fio.c:112-125, 333-334, 459-460, 531-580, 809-818, 957-980, 1041-1042, 1052-1167`, `Launchstation/iop/ps2hdd_osd/journal.c:47-88`
- **Problem:** Beyond the `apaOpen` UAF and unchecked `fioSema` shared in `CROSS-015`:
  1. `apaDelete()` (`apa.c:379-380`) and `apaGetNextHeader()` (`508-513`) read `clink->device` and `clink->header->prev`/`next` after calling `apaCacheFree(clink)`. `apaDelete` also leaks `clink_mbr` and masks errors at `380-397`.
  2. `hddReName()` (`hdd_fio.c:809-818`) locks/unlocks `fioSema` around `apaRename()`, which also calls `SignalSema(fioSema)` on three early error returns (`539, 548, 562`), double-releasing `fioSema`.
  3. Unchecked `apaCacheAlloc()`/`apaCacheGetHeader()`/`apaRemovePartition()` results (`apa.c:46-47`, `hdd.c:130-162`, `hdd_fio.c:333-334, 459-460`, `journal.c:73-76`), leaked `header`/`clink` on error (`cache.c:35-38`, `apa.c:59-60`, `hdd_fio.c:1041-1042`), unchecked `journalBuf.num` (`< 126`), `hddIoctl2` `argp`/`arglen`/part bounds, `hddDevctl` `unit`/`arg`/`bufp`, `vhdd` slot `< 0 || >= BLKIO_MAX_VOLUMES`, `-o` `<= 0` check, and `fioInputBreaker` `maxout < len` NUL-termination off-by-one.
- **Fix:** Inspect `clink` fields before `apaCacheFree`, remove duplicate `SignalSema(fioSema)` calls in `apaRename()`, `NULL`-check all cache allocations with error cleanup, and add range/pointer validation across `journal.c`, `hdd.c`, `hdd_blkio.c`, and `hdd_fio.c`.
- **Justification:** Closes APA cache use-after-free, semaphore corruption, slot leaks, and unchecked user/on-disk OOB hazards.
- **Benefit:** Hardens Launchstation's OSD-extended `ps2hdd_osd` driver across rename, delete, journal replay, and `ioctl2`/`devctl` paths.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-021: `vmcman` bitmask (`unit & 9`) breaks `vmc2:`..`vmc7:`, closes `fd 0`/leaks fds, hardcodes 512 B I/O, and wastes 32,040 B BSS
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `Launchstation/iop/vmcman/src/mcman-internal.h:105-109, 298-328, 364`, `Launchstation/iop/vmcman/src/mcdev.c:216-224`, `Launchstation/iop/vmcman/src/mciomanx_backing.c:26-58, 76-85, 130-134, 267, 320`, `Launchstation/iop/vmcman/src/main.c:60, 64, 1572, 1868, 1913, 1916`, `Launchstation/iop/vmcman/src/ps1mc_fio.c:116-127, 443-449, 511-517`
- **Problem:** Under `BUILDING_VMCMAN`, `MCMAN_MAXSLOT` is `10`, so `mcman_unit2card()` (`mcdev.c:221-223`) masking `unit & (MCMAN_MAXSLOT - 1)` (`unit & 9`) maps `vmc2:`..`vmc7:` onto slot `0` or `1`. Zero-initialized `mcman_vmc_cardinfo` (`fd == 0`) makes `mcman_iomanx_backing_clear_slot()` close `fd 0`, remounting overwrites `cardinfo->fd` without closing the prior handle, `slot > MCMAN_MAXSLOT` allows index `10`, `pagesize == 528` mounts transfer `512` bytes, `ps1mc_fio.c`/`main.c` dereference directory entries before checking error codes, and `mcman_fatcache[1][*]` + `mcman_devinfos[1..3][*]` reserve `32,040 B` of dead multi-port BSS despite `mcman_mc_port = 0`.
- **Fix:** Replace `& (MCMAN_MAXSLOT - 1)` with `< MCMAN_MAXSLOT`, enforce `0 <= slot < MCMAN_MAXSLOT`, use `fd = -1` for unmounted slots and close existing fds on remount, match `pagesize` or reject `!= 512`, check directory read errors first, and collapse unused port dimensions under `#ifdef BUILDING_VMCMAN`.
- **Justification:** Restores `vmc2:`..`vmc7:`, stops closing `fd 0` / leaking VMC descriptors, and prevents OOB slot/entry access.
- **Benefit:** Fixes virtual MC slots `2..7`, eliminates fd/OOB bugs, and saves `32,040 B` of IOP `.bss` RAM.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-022: `pc_game_receiver.c` hang on `>= 8192` B requests, `".."` path traversal, fake resume (`FIO_O_TRUNC`), and 8x full IOP reloads
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `Launchstation/src/pc_game_receiver.c:40-48, 70-83, 135-184, 250-281, 324-329`
- **Problem:** `readRequest()` (`43-47, 80-81`) performs one unlooped `8191`-byte `fileXioRead` so any `>= 8192`-byte job stays unparsed and re-polled forever; line `71` rejects `'/'`/`'\\'` but accepts `"."`/`".."`; `copyWithResume()` opens destinations with `FIO_O_TRUNC` (`135`) and deletes `stage` on error (`278`); and short UDPFS files (`got == 0`) trigger `PC_RECV_RETRIES` (`8`) full `reloadUdpfsModules(); loadAtaModules();` IOP reloads (`174-175`).
- **Fix:** Loop `fileXioRead` to EOF in `readRequest` returning `-1` if `>= sizeof(request_text)`, reject `"."` and `".."`, stat-check remote UDPFS file size once instead of reloading IOP stacks 8 times on EOF/mismatch, and either remove misleading resume claims or persist partial stage files.
- **Justification:** Prevents infinite poll loops, `".."` path construction, and multi-second IOP reloads on truncated UDPFS files.
- **Benefit:** Eliminates hang/path-traversal bugs and 8x redundant IOP reloads on failed UDPFS transfers.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-023: Deep recursive `copy()` stack frames on 128 KB EE stack require CI stack-budget enforcement
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `Launchstation/src/filer_copy.c:752-850`, `Launchstation/.github/workflows/compile.yml` (`check_stack_realign.py`)
- **Problem:** `copy()` keeps multiple `char[MAX_PATH]` buffers, `FILEINFO`, and timers on the stack while recursing up to `MAX_RECURSE` levels deep (`761-784`), reaching `~100 KB` (`check_stack_realign.py` budget `104 KB`) on a `128 KB` thread stack.
- **Fix:** Replace recursive `copy()` with an explicit heap-allocated directory traversal stack/queue of pending path pairs.
- **Justification:** Fixes a real stack-overflow crash hazard on deeply nested directories without relying on fragile CI stack-budget checks.
- **Benefit:** Bounds `copy()` stack usage to `O(1)` (`<4 KB` `INFERRED`), eliminating EE stack-overflow risk.
- **Effort:** M. **Risk:** Med. **Confidence:** High (`READ`).

### LAUNCHSTATION-024: Deep recursive `sync_tree` stack, per-`.psu` 64 KB `UPLOADED.TXT` reloads, and raw `snprintf` error writes in `src/cloud_google.c`
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `Launchstation/src/cloud_google.c:28-52, 393-395, 601, 656, 705-710, 740, 909, 1201, 1270-1276, 1336, 1361, 1396, 1413-1442, 1485-1561, 1581-1590`, `Launchstation/src/cloud_http.c:74-81, 728-735`
- **Problem:** `cloudGoogleSyncBackupRoot` places `GoogleSession s` (`~7.9 KB`) on the stack (`1584`), recurses up to `CLOUD_TREE_DEPTH` (`16`) levels in `sync_tree` with `iox_dirent_t` and `2.2 KB` of path buffers per frame, and reaches `CloudTls` inside `cloudHttpRequest` (`~55+ KB` peak `INFERRED`); `upload_ledger_has` (`1413-1442`) re-reads and scans up to `64 KB` of `UPLOADED.TXT` for every single `.psu` visited; and nine error sites (`393..1361`) call raw `snprintf(error, error_size, ...)` instead of `set_error` (`49-52`).
- **Fix:** Replace `sync_tree` recursion with an explicit heap work queue (and heap-allocate `GoogleSession`/`CloudTls`), load `UPLOADED.TXT` once per sync run, and route all nine error writes through `set_error`.
- **Justification:** Prevents EE stack overflow on nested cloud backup trees and adds consistent NULL/size guards on error buffers.
- **Benefit:** Eliminates deep recursion stack risk and `O(files * ledger_size)` slow USB/MMCE ledger re-reads during sync.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-025: `smbd` `DEVCTL_START` OOB string read, allocator integer overflow, thread leak, `2 GB` truncation, and recursive glob explosion
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `Launchstation/iop/smbd/smbd.c:34, 40, 59-65, 150-160, 256, 258-266, 283-287`, `Launchstation/iop/smbd/smbd_fs.c:523-540`, `Launchstation/src/init.c:2785-2814`
- **Problem:** `SMBD_DEVCTL_START` (`smbd.c:283-287`) skips checking `arg != NULL` and a NUL separator strictly before `arg[arglen - 1]` (so `"USER\0"` makes `password` read past `arg`); `calloc(n, size)` and `malloc(size)` (`34, 40, 59-65`) lack integer-overflow checks; `start()` (`258-266`) overwrites `id = StartThread(id, NULL)` on failure without calling `DeleteThread`; `smbd_pread`/`smbd_pwrite` (`150-160`) truncate `uint64_t off` to `(int)off`; and `match()` (`smbd_fs.c:523-540`) runs exponential recursive `'*'` backtracking on a `16 KB` IOP stack (`smbd.c:256`).
- **Fix:** Validate `arg != NULL` and `strnlen(arg, arglen) < arglen - 1` with NUL termination in `SMBD_DEVCTL_START`, guard `calloc`/`malloc` against multiplication/addition overflow, delete failed `StartThread` handles, use `iomanX_lseek64` or check `off <= INT_MAX`, and replace recursive `match()` with an iterative two-pointer wildcard matcher.
- **Justification:** Closes devctl OOB-read, allocator wraparound, thread leak, 2 GB seek truncation, and recursive glob stack-overflow bugs in `smbd`.
- **Benefit:** Hardens Launchstation's embedded SMB2/3 server against malformed client requests and large offsets.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-026: `ps2host` remote-length `pko_read_file` overflow, 256 B `ioctl` over-read, uninit dir reply, fd `0` check, thread leak, and unlocked globals
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `Launchstation/iop/ps2host/net_fio.c:23, 58, 137-159, 169, 210, 308-311, 391-418, 441, 637-651, 741-771`, `Launchstation/iop/ps2host/net_fio.h:25, 34`, `Launchstation/iop/ps2host/net_fsys.c:65-68, 105-115, 135-167, 241-290, 320-350`
- **Problem:** `pko_read_file` (`net_fio.c:391-418`) passes remote `ntohl(readrly->nbytes)` straight into `pko_recv_bytes(..., &buf[0], nbytes)` without verifying `nbytes <= length` (plus dead `return` at `418`); `pko_ioctl` (`441`) always `memcpy`s `256` bytes from `data`; `pko_read_dir` (`637-651`) copies uninitialized stack `dirrly` when `retval <= 0`; `pko_accept_pkt` (`137-159`) reads header fields before validating `hlen >= sizeof(pko_pkt_hdr)` and omits closing `length < 0` sockets; lines `58, 757, 765` test `pko_fileio_sock > 0` instead of `>= 0`; `pko_write_file` (`308-311`) clamps to `PKO_MAX_READ_SEGMENT`; `fsysDestroy` (`net_fsys.c:105-115`) leaks `pko_file_serv` and its socket; and `lastopen_fd`/`renamed_fd`/`remove_flag`/`remove_result` lack synchronization.
- **Fix:** Enforce `0 <= nbytes <= length`, drop line `418`, bound `pko_ioctl` copy/NULL check, skip copying `dirrly` when `retval <= 0`, validate `hlen`, close rejected sockets, use `>= 0` and `PKO_MAX_WRITE_SEGMENT`, stop `pko_file_serv` cleanly on unload, and lock shared state.
- **Justification:** Prevents peer-controlled buffer overflow, stack leaks, `fd 0` breakage, and teardown leaks/races in `ps2host`.
- **Benefit:** Fixes six memory-safety and lifecycle defects in `ps2host.irx`.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-027: `hdl_info` 63-byte OOB read in `hdl_ginfo_write`, `strcpy` overflows, `tmp` leak, `games` double-free, and unit OOB
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `Launchstation/iop/hdl_info/hdl.h:4-15`, `Launchstation/iop/hdl_info/ps2_hdd.h:19`, `Launchstation/iop/hdl_info/hdl.c:48-49, 78, 91-96, 115-120, 141-166`, `Launchstation/iop/hdl_info/hdd.c:106-110, 129-151, 167-185`, `Launchstation/iop/hdl_info/main.c:23`
- **Problem:** `hdl_ginfo_write()` (`hdl.c:115-120`) ignores `hio->read` errors and reads `PS2_PART_NAMEMAX` (`128` B) from `ginfo->name` (`65` B), reading `63` bytes out of bounds to disk; `hdl_ginfo_read` (`48-49`) and `HdlRenameGame` (`hdd.c:180`) use unbounded `strcpy`; `hdl_glist_write` (`165-166`) leaks `tmp` on success (`result == 0`); `hdl_glist_read` (`91-96`) leaves `*glist` dangling after freeing so the next `HdlGetGameInfo`/`HdlRenameGame` double-frees `games`; `hio_iop_probe` (`hdd.c:106-110`) passes `'0'..'9'` into 2-entry `atad_devinfo`; and `main.c:23` reserves `Rpc_Buffer[1024]` (`4,096 B` `.bss`).
- **Fix:** Check `hio->read`, bound name copies to `sizeof(ginfo->name)` / `startup`, unconditionally `free(tmp)` in `hdl_glist_write`, NULL out `*glist` after free, restrict `hio_iop_probe` to `'0'..'1'`, and shrink `Rpc_Buffer` to max packet size.
- **Justification:** Closes OOB disk reads, double-free crashes, and heap leaks during HDD game inspection/renaming.
- **Benefit:** Fixes five safety defects in `hdl_info` and trims IOP `.bss`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-028: `ps2ftpd` broken `-pass` / `-port` flags, `PASV` leak + `disconnect(-1)`, unvalidated `PORT`, and blocking `connect()`
- **Category:** 5, 6 (Performance, Bugs and security)
- **Location(s):** `Launchstation/iop/oldlibs/ps2ftpd/src/main.c:35, 44-58, 103-113`, `Launchstation/iop/oldlibs/ps2ftpd/src/FtpCommands.c:119-123, 139-143, 183-191`, `Launchstation/iop/oldlibs/ps2ftpd/src/FtpClient.c:474-492`, `Launchstation/src/init.c:1170-1195`
- **Problem:** `main.c:51` compares `"-pass"` against `argv[0]` instead of `argv[i]` and line `111` calls `FtpServer_SetPort(&srv, 21)` after `process_args`, clobbering `-port`; `PASV` (`FtpCommands.c:119-143`) calls `disconnect(-1)` when `socket() < 0` and leaks `s` when `getsockname()` fails; `PORT` (`183-191`) allows octets `> 255` and arbitrary third-party IPs; and `FtpClient.c:474-492` performs a blocking `connect()` with non-blocking `fcntl` commented out.
- **Fix:** Compare `argv[i]`, set default port `21` before `process_args`, fix both `PASV` error branches, clamp IP octets `0..255` matching control peer IP, and restore non-blocking/timed `connect()`.
- **Justification:** Stops LWIP socket exhaustion, server stalls, FTP bounce risk, and ignored CLI flags in `ps2ftpd`.
- **Benefit:** Hardens Launchstation's built-in FTP server across `PASV`, `PORT`, and auth startup.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-029: `xparam` `GET_XPARAM_DB_SIZE() == 1` breaks 291/292 entries plus duplicate `SLPM_660.22` row
- **Category:** 1, 2, 4 (Dead code, Bugs and security, Size and memory)
- **Location(s):** `Launchstation/iop/xparam/include/xparam.h:28-36`, `Launchstation/iop/xparam/src/xparam.c:52-62`, `Launchstation/iop/xparam/src/database_merged.c:7-9, 488-493`, `Launchstation/ps2.yaml:386-393, 675`, `Launchstation/src/init.c:1355`
- **Problem:** `GET_XPARAM_DB_SIZE()` (`xparam.h:36`) computes `sizeof(*XPARAMS) / sizeof(XPARAMS[0])`, which always equals `1` on an incomplete extern array `XPARAMS[]`, so `CheckGameID()` inspects only `XPARAMS[0]` (`"PAPX_905.17"`) and skips the other `291` entries (`~5,820 B` `INFERRED` unreachable `.rodata`), while `SLPM_660.22` appears twice at `database_merged.c:489, 492`.
- **Fix:** Export `XPARAMS_COUNT` from `database_merged.c` (or terminate `XPARAMS[]` with a sentinel) and delete the duplicate `SLPM_660.22` entry.
- **Benefit:** Restores Deckard IOP parameter overrides for `291` games and removes the duplicate entry.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-030: `ata_bd` `sceAtaSecurityEraseUnit` 512-byte `NULL` PIO write and `ATA_ENABLE_MAXUDMA` UDMA5/6 drop to UDMA0
- **Category:** 2, 6 (Bugs and security)
- **Location(s):** `Launchstation/iop/ata_bd/src/ps2atad.c:710-717, 1115-1119, 1169-1189, 1320-1337, 1469-1493`, `Launchstation/ps2.yaml:397-408`
- **Problem:** `sceAtaSecurityEraseUnit()` (`1187`) passes `NULL` to `ata_get_security_status(device, devinfo, NULL)`, causing `ata_device_identify`'s PIO loop (`710-717`) to overwrite `512` bytes at IOP address `0x0`; latently under `#ifdef ATA_ENABLE_MAXUDMA`, `udmaMode` `5`/`6` falls into `default: val = 0xa7;` (UDMA0) in `ata_ultra_dma_mode()` (`1469-1493`).
- **Fix:** Pass `ata_param` (or a valid `512`-byte buffer) in `sceAtaSecurityEraseUnit()`, and clamp `udmaMode <= 4` before `ata_ultra_dma_mode()`.
- **Justification:** Prevents IOP low-memory (`0x0`) corruption during ATA security erase and guards latent UDMA mode selection.
- **Benefit:** Fixes a 512-byte `NULL`-pointer DMA/PIO write hazard in `ata_bd`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-031: `edIoRun` `s64->int` overflow on `>= 2 GiB` files and unlooped 512 KB `genWrite`
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/dash_editor.c:22, 312, 318-322, 331-363`, `Launchstation/include/launchelf.h:607-609`
- **Problem:** `edIoRun` assigns `s64 genLseek(fd, 0, SEEK_END)` into `int size` (`312, 331`) before testing `size > ED_MAX_SIZE`, allowing `>= 2 GiB` files to wrap/truncate past the `512 KB` check, and writes up to `512 KB` with one unlooped `genWrite(fd, j->text, j->len)` (`320`).
- **Fix:** Store `genLseek` in `s64 size`, reject `size < 0 || size > ED_MAX_SIZE`, and loop `genWrite` until `j->len` completes or errors (`<= 0`).
- **Justification:** Prevents opening the first 512 KB of huge binaries as text files and prevents silent short writes on slow storage.
- **Benefit:** Fixes editor size-limit overflow and partial-write truncation.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-032: `mcSaveFromMcm` `raw` leak, destructive missing same-path guard in `mc_card_export.c`, and duplicated save helpers
- **Category:** 2, 6 (Duplication, Bugs and security)
- **Location(s):** `Launchstation/src/mc_save_formats.c:216-260, 932-954`, `Launchstation/src/mc_card_export.c:247-248, 306-307, 443-498, 556-620`, `Launchstation/src/mc_ps1_card.c:296-311, 347-359`
- **Problem:** `mcSaveFromMcm` (`mc_save_formats.c:243-244`) leaks `raw` when `startSave(...)` fails, `mcSaveFormatRead` (`951-952`) ends with a redundant ternary always evaluating to `f`, `mcCardExportPcsx2` (`443-498`) and `mcCardImportPcsx2Impl` (`556-610`) omit the `!strcmp(source, dest)` same-path check (truncating and unlinking `source` on `only_new == 0`) while duplicating their copy/verify loop, and `ps1SaveFromFile` (`mc_ps1_card.c:347-359`) duplicates `takeBlocks()`.
- **Fix:** Free `raw` on error at `mc_save_formats.c:243`, delete the redundant ternary, add `MC_CARD_EXPORT_SAME_PATH` guards and share the PCSX2 copy/verify helper in `mc_card_export.c`, and call `takeBlocks()` for `PS1F_RAW`.
- **Justification:** Prevents `.mcm` failure heap leaks and accidental self-destruction when exporting/importing `.ps2`/`.mcd` images.
- **Benefit:** Fixes a memory leak and destructive image-conversion edge case while removing duplicate loops.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-033: `ctLibScan` reads entire `.psu` files (up to 8.5 MB each) and `ctSafetyCopyOf` rescans folders up to 64 times
- **Category:** 5 (Performance)
- **Location(s):** `Launchstation/src/dash_card_tool.c:266-336, 2283, 2353-2419`
- **Problem:** `ctLibScan` (`2399-2418`) calls `ctLoadPsu` to allocate and read entire `.psu` files (up to `8.5 MB` each for up to `500` saves) merely to parse the root name, size sum, and `icon.sys` Shift-JIS title; `ctSafetyCopyOf` (`311-334`) repeatedly reopens and scans the safety folder up to `64` times to delete one oldest file per pass.
- **Fix:** Scan `.psu` headers by seeking over non-`icon.sys` payloads via `genLseek`, and track backup indices during `ctSafetyCopyOf`'s initial directory scan to prune excess backups in one pass.
- **Benefit:** Speeds up Save Library scans on USB/MMCE and cuts up to 63 redundant directory rescans during safety backup rotation.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-034: Dead post-`1882fe9` `.psu` extractors, uninvoked `selfUpdateWriteProgress`, double sidecar scan, and premature `.BAK` removal
- **Category:** 1, 2, 3, 6 (Dead code, Duplication, Overengineering, Bugs and security)
- **Location(s):** `Launchstation/src/self_update.c:203, 280-287, 291-357`, `Launchstation/src/update_psu.c:18, 67-208, 227-364`, `Launchstation/include/self_update.h:13-24`, `Launchstation/src/psu_functions.c`, `Launchstation/src/mc_save_formats.c`, `Launchstation/tests/test_app_packages.py:69-81`, `Launchstation/tests/test_self_update.py:29`
- **Problem:** After commit `1882fe9` switched `self_update.c` to in-memory ELF verification and direct overwrite, `selfUpdateExtractPsu`, `selfUpdateExtractPsuMem`, `selfUpdateExtractPsuSidecar`, `selfUpdateWriteProgress`, and `extract_elf`'s `stage != NULL` branch have zero runtime callers outside `tests/test_app_packages.py`; `update_psu.c` still forms a third custom `.psu` reader alongside `psu_functions.c` and `mc_save_formats.c`; `extract_sidecar` (`255-324`) scans every PSU directory entry twice; and `install_update_files` (`self_update.c:280-287`) deletes the legacy `.BAK` file before download, SHA-256, ELF check, or free-space validation succeed.
- **Fix:** Delete the dead post-`1882fe9` extractors, `PsuIn.fd` file I/O, and `selfUpdateWriteProgress` (update `test_app_packages.py` to test `selfUpdateLocatePsuElf`/`selfUpdateExtractPsuSidecarMem`), fold `extract_sidecar` into one pass using shared PSU record validation, and delay legacy `.BAK` deletion until immediately before `overwrite_elf`.
- **Justification:** Preserves existing `.BAK` recovery if download or validation fails before disk writes begin.
- **Benefit:** Removes `~150` lines of dead extractor scaffolding (`INFERRED`), halves sidecar PSU scans, and prevents premature fallback deletion.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-035: Dead EE functions and lifecycle scaffolding (`~40+` unused symbols, `load_ps2atad_stack`, `#ifndef EXFAT`, unused `DashIsEnabled`/`dashTextWrapScrollTimed`)
- **Category:** 1, 3 (Dead code, Overengineering)
- **Location(s):**
  - Classic config/UI/filer dead helpers (`src/config.c:86, 162, 292, 788, 1342`, `src/config_network.c:70`, `src/main_actions.c:875`, `src/draw.c:206`, `src/filer_browser.c:2315`, `src/main_title.c:680`, `src/virtual_keyboard.c:371`)
  - Dashboard dead wrappers & unreachable `*Shutdown()` functions (`src/dash_main.c:131, 200, 242`, `src/dash_browser.c:3582`, `src/dash_apps.c:94, 103`, `src/dash_art.c:71, 787`, `src/dash_bg.c:543`, `src/dash_gfx.c:362`, `src/dash_icons.c:784, 856`, `src/dash_lib.c:501, 517`, `src/dash_font.c:309, 821`, `src/dash_ui.c:595`, `dashOkLabelKey`, `src/dash_devices.c:187`, `src/dash_store.c:466`, `src/dash_themes.c:469, 631, 1122, 1161, 1281`)
  - Unused IOP loader & `#ifndef EXFAT` (`src/init.c:749-799, 1794-1804, 2095-2099, 2406-2409, 2446-2448`; `ps2.yaml:630` unconditionally defines `EXFAT`), plus internal-only `src/makeicon.c:110, 165, 236` (`tex_drawChar`, `tex_printXY`, `tex_compresRLE`)
- **Problem:** Over `40` public EE functions, shutdown hooks that never run because `DashRun()` never exits, `load_ps2atad_stack`, and `#ifndef EXFAT` fallbacks sit unreferenced in `src/`, while `tex_drawChar`, `tex_printXY`, and `tex_compresRLE` are exported non-`static` despite only having callers inside `src/makeicon.c`.
- **Fix:** Delete all unreferenced functions, `load_ps2atad_stack`, and dead `#ifndef EXFAT` branches, and mark `tex_*` internal helpers `static`.
- **Benefit:** Removes `~800+` lines of unused C code and header declarations across EE modules.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-036: Four unbuilt dead files in `iop/mmceman/` (`609` lines: `mmcedrv.c`, `mmceigr.c`, `mmcedrv_config.h`, `exports.tab`)
- **Category:** 1, 7 (Dead code, Build/CI/repo clutter)
- **Location(s):** `Launchstation/iop/mmceman/src/mmcedrv.c:1-413`, `Launchstation/iop/mmceman/src/mmceigr.c:1-168` (`92-103, 118-125`), `Launchstation/iop/mmceman/include/mmcedrv_config.h:1-10`, `Launchstation/iop/mmceman/src/exports.tab:1-18`, `Launchstation/ps2.yaml:153-169`
- **Problem:** `ps2.yaml:153-169` compiles `mmceman` without any of these four files (`609` lines; zero callers across the repo). Note `mmceigr.c:118-125` also checks `argc < 1` instead of `argc < 2` before reading `argv[1]`.
- **Fix:** Delete `iop/mmceman/src/{mmcedrv.c,mmceigr.c,exports.tab}` and `iop/mmceman/include/mmcedrv_config.h`.
- **Benefit:** Removes `609` lines of dead vendored IOP code.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-037: Duplicate controller drivers (`src/pad.c` vs `src/pad_ds34.c`), build file-swap script, and unported pad fixes / kernel semaphore churn
- **Category:** 2, 5, 6, 7 (Duplication, Performance, Bugs and security, Build/CI/repo clutter)
- **Location(s):** `Launchstation/src/pad.c:84-113, 355-369, 398-423`, `Launchstation/src/pad_ds34.c:107-110, 190-217, 415-428, 456-478`, `Launchstation/scripts/configure_ds34.py:18-39`
- **Problem:** `pad.c` and `pad_ds34.c` maintain two ~450-520 line EE pad polling forks switched by `scripts/configure_ds34.py`. Because of the fork, `pad_ds34.c` still contains three bugs already fixed in `pad.c` (overwriting `paddata_t[port] = PAD_R3_H1` every odd call, missing `pad_reopen_active` timeout in `waitPadReady`, unchecked final `state` in `setupPad`) and calls `CreateSema`/`DeleteSema` inside every `padPollingThread` tick while racing on `paddata_t`/`ret`.
- **Fix:** Merge DS34 support into `src/pad.c` behind `#ifdef DS34`, reuse one persistent `semAlrm` created in `padPollingInit`, and delete `src/pad_ds34.c` and `scripts/configure_ds34.py`.
- **Justification:** Eliminates held-button drops, pad init hangs, and continuous kernel semaphore create/delete churn in the DS34 build.
- **Benefit:** Deletes `~400+` lines of duplicated EE pad code, removes the file-swap script, and fixes three DS34 polling defects.
- **Effort:** M. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-038: `dash_lib.c` allocates `6 KB` `scanned` and dead `12 KB` `subdirs` at `depth == 1` on `128 KB` worker stack
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `Launchstation/src/dash_lib.c:373-376, 656, 696-724, 779-784`, `Launchstation/src/dash_worker.c:28, 57`
- **Problem:** On a fixed `128 KB` `dash_worker` stack, `scanRun` (`783`) puts `scanned` (`6,144 B`) on the stack and calls recursive `scanDir` (`depth = 0` -> `depth = 1`), allocating `char subdirs[48][256]` (`12,288 B`) twice even though `depth == 1` skips `subdirs` (`713`), driving `dash_lib` alone to `~33+ KB` peak stack before IOP RPC frames.
- **Fix:** Move `scanned` into the heap-allocated `ScanJob`/`ScanCtx` and split depth-0/depth-1 into non-recursive helpers without stack-allocating `subdirs` at depth 1.
- **Justification:** Prevents `dash_worker` stack overflow when metadata parsing and IOP RPC frames overlap with directory scanning.
- **Benefit:** Cuts `~24+ KB` from peak `dash_worker` stack usage.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-039: Plaintext SMB server password shown in Settings UI (`S_SMBD_PASS`) plus unbounded `strcpy`
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/dash_settings.c:3353-3363, 3887-3893`, `Launchstation/include/launchelf.h:339`
- **Problem:** Unlike `S_SMBED_PASS` (`3892-3893`, masked as `"set (hidden)"`), `S_SMBD_PASS` (`3889-3890`) renders `setting->SMB_Server_Pass` in plaintext on screen, and `3359` copies `edit` into `32`-byte `SMB_Server_User`/`SMB_Server_Pass` via unbounded `strcpy(v, edit)`.
- **Fix:** Mask `S_SMBD_PASS` identically (`"set (hidden)"` / `"blank"`) and use `dashStrCopy(v, edit, 32)`.
- **Justification:** Protects stored SMB server credentials from on-screen exposure and prevents over-31-byte buffer overrun.
- **Benefit:** Secures UI credential display and bounds credential copy length.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-040: Quadratic recursive `sort()` with two stack `FILEINFO` structs per frame (`gui_sort.c`)
- **Category:** 4, 6 (Size and memory, Bugs and security)
- **Location(s):** `Launchstation/src/gui_sort.c:74-95`, `Launchstation/include/launchelf.h:205, 560-565`
- **Problem:** `sort(FILEINFO *a, int left, int right)` chooses `a[left]` as pivot and recurses `O(N)` deep on sorted or reversed folders (up to `MAX_ENTRY = 2048`) while holding two full `FILEINFO` structs (`tmp`, `pivot`) on every stack frame, risking `128 KB` EE stack exhaustion.
- **Fix:** Replace `sort()` with `qsort`, heapsort, or median-of-three iterative/tail-recursive quicksort using a single non-stack swap buffer.
- **Justification:** Bounds sort stack usage to `O(1)` / `O(log N)` and eliminates worst-case `O(N^2)` stalls on sorted directories.
- **Benefit:** Prevents EE stack overflow and quadratic sorts when toggling sort direction.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-041: Broken `fmodf` wraparound hides half the clouds in `dashBgSceneClouds`
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/dash_bg_ps2.c:795-810`
- **Problem:** When `dir = -1.0f` produces negative `fmodf(...)` in `(-span, 0]`, line `807-808` computes `x = span - x`, placing those clouds at `[820, 1820)` off the right edge of the `640`-pixel viewport permanently.
- **Fix:** Replace `if (dir < 0.0f) x = span - x;` with `if (x < 0.0f) x += span;`.
- **Benefit:** Restores half the animated clouds in the Clouds theme.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-042: Overlapping USB diagnostic text, unguarded `.arg` path formatting, stale `PROBE_PCCF` check, and missing negative alpha clamp
- **Category:** 6 (Bugs and security)
- **Location(s):** `Launchstation/src/dash_browser.c:3251, 3258, 3346-3354`, `Launchstation/src/dash_main.c:541-543, 601-604`, `Launchstation/src/dash_devices.c:52-58, 63, 73, 420, 433`, `Launchstation/src/dash_gfx.c:152, 784, 795, 882-886`
- **Problem:** Four localized UI/runtime defects remain active: `dash_browser.c:3346-3354` leaves in a completed temporary USB pad debug line that overlaps clipboard/args status text (`3251, 3258`); `dashLaunchApp` (`dash_main.c:604`) omits `argSidecarExists`'s `base + 5 >= sizeof(arg)` overflow guard when building `argp`; `dashDevicesList` (`dash_devices.c:420, 433`) reads `PROBE_PCCF` without calling `syncProbeSession()`; and `dashSpriteBatchBegin` (`dash_gfx.c:882-886`) lacks `if (a < 0) a = 0` (`gsColor` clamps it; `dashRoundedBorder` line `795` also shadows `t`).
- **Fix:** Delete `dash_browser.c:3346-3354`, share one `.arg` sidecar path helper with overflow guard, call `syncProbeSession()` before reading `PROBE_PCCF`, clamp `a < 0` in `dashSpriteBatchBegin`, and remove shadowed `float t` at `dash_gfx.c:795`.
- **Justification:** Prevents `.arg` path truncation, stale post-IOP-reset CF reporting, and `u8` alpha wraparound.
- **Benefit:** Removes overlapping debug UI text and fixes three localized correctness/safety bugs.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-043: `dashModalChoice` 24-item silent cap vs 128-item caller, dead `dash_settings.c`/`dash_tools.c` cases, and duplicated resets
- **Category:** 1, 2, 6 (Dead code, Duplication, Bugs and security)
- **Location(s):** `Launchstation/src/dash_ui.c:1110-1126`, `Launchstation/src/dash_settings.c:28, 44, 52, 79, 119-120, 159, 1019, 1022, 1170, 1174, 1698, 1702, 1844, 1848, 2112-2115, 2135-2138, 2771-2784, 2792-2805, 3093, 3298, 3810, 3823-3828, 3955, 3958`, `Launchstation/src/dash_tools.c:27, 56-58, 1612-1617`
- **Problem:** `dashModalChoice` clamps `n` to `24` (`dash_ui.c:1117-1118`) without clamping `current` while `dash_settings.c:3824-3828` passes up to `128` options (`display[128]`); seven `dash_settings.c` enums (`ST_INFO`, `S_LEGACY_THEME`, `S_ENABLED`, `S_LAUNCHKEYS`, `S_IGR_AUTO_BACKUP`, `S_IGR_CLOUD_SYNC`, `S_T_CUSTOMIZE`) have zero `SetItem` entries; `pathApplied` sets `LK_Flag[SETTING_LK_ESR]`/`OSDSYS` twice; `resetScreen` and `resetDisplayThemeDefaults` repeat 14 lines; `single` (`3810`) is unused; and `TA_BROWSER_AT` (`dash_tools.c`) passes `""` identically to `TA_BROWSER`.
- **Fix:** Support `> 24` choices and clamp `current` in `dashModalChoice`, delete dead enums/`TA_BROWSER_AT`/duplicate `pathApplied` writes/`single`, and share one screen-reset helper.
- **Justification:** Prevents silent truncation and out-of-range selection index when choosing from `> 24` items.
- **Benefit:** Fixes option clipping in settings pickers and cleans dead/duplicate dispatch code.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-044: Duplicated MC save/image helpers, duplicated 14,990 B `.icn` files, and duplicate wallpaper PNG
- **Category:** 2, 7 (Duplication, Build/CI/repo clutter)
- **Location(s):** `Launchstation/src/mc_card_write.c:20-54` vs `Launchstation/src/mc_save_formats.c:26-58` vs `Launchstation/src/mc_card_view.c:31-35`, `Launchstation/res/memory-card/{copy,del,list}.icn`, `Launchstation/docs-site/static/assets/wallpaper-example.png` vs `Launchstation/docs/theme-example/background.png`
- **Problem:** Three files duplicate `rd16`/`rd32`/`wr16`/`wr32`/`nameOk`, `res/memory-card/{copy,del,list}.icn` store three byte-identical `14,990 B` files (`44,970 B` total), and `wallpaper-example.png` duplicates `background.png` (`2,901 B`).
- **Fix:** Extract `mcRead16`/`mcRead32`/`mcWrite16`/`mcWrite32`/`mcCardNameOk` in `mc_card_view.{h,c}`, point `icon.sys` and `package_apps.py` to one `.icn` unless distinct icons are rendered, and keep one wallpaper example PNG.
- **Benefit:** Removes ~100 lines of duplicate C helpers, `29,980 B` of identical `.icn` save payload per installed MC folder, and `2,901 B` of duplicate PNGs.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-045: Duplicated `main_*`/`mmce_*`/`neutrino_*`/`popstarter_*`/`virtual_keyboard.c` helpers and double `.VCD` open
- **Category:** 2, 5 (Duplication, Performance)
- **Location(s):** `Launchstation/src/main_console_info.c:7-27`, `Launchstation/src/main_exploit_installer.c:248-268`, `Launchstation/src/mmce_backup.c:73-96, 742-827, 920-1002`, `Launchstation/src/mmce_save_versions.c:27-47`, `Launchstation/src/mmce_igr.c:34-44`, `Launchstation/src/neutrino_gameid.c:20-109`, `Launchstation/src/popstarter_vcd.c:120-215`, `Launchstation/src/popstarter_gameid.c:102-123, 190-226`, `Launchstation/src/virtual_keyboard.c:193-205, 362-378`
- **Problem:** Four duplication clusters repeat verbatim utilities: `copy_printable_prefix` (`main_console_info.c` vs `main_exploit_installer.c`), `join_path`/`dir_exists` + `mmceBackupAllCards`/`mmceBackupScanCards` (`mmce_*`, ~110 repeated traversal lines), ISO9660 `SYSTEM.CNF`/PVD search (`neutrino_gameid.c` vs `popstarter_vcd.c`, plus `buildPopstarterVcdGameID` opening `.VCD` files twice when `SYSTEM.CNF` fails), and byte-identical `virtual_keyboard.c` `DisplayChar`/`Char` wrappers.
- **Fix:** Share `copy_printable_prefix`, `join_path`/`dir_exists`, one dry-run-capable MMCE tree walker, and ISO9660 `SYSTEM.CNF`/PVD helpers; keep the `.VCD` fd open across fallback in `buildPopstarterVcdGameID`; collapse identical `virtual_keyboard` wrappers.
- **Benefit:** Cuts `~180+` duplicated EE lines (`INFERRED`) and eliminates redundant `.VCD` re-opens on Game ID lookup.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-046: Cross-file duplication (`ps2ipc`, BCD, `Hide_Hdd`, HTTP headers, batch summaries) and no-op computations
- **Category:** 1, 2 (Dead code, Duplication)
- **Location(s):** `Launchstation/src/dash_clock_sync.c:24-47, 95-98, 219-259, 377-412, 429-477`, `Launchstation/src/cloud_http.c:37-71, 595-599, 643-644, 658-671, 1123`, `Launchstation/src/dash_browser.c:1243-1246, 2664-2667, 2684, 3030-3032`, `Launchstation/src/dash_card_tool.c:1373-1376, 1917-1932, 2139-2172, 2518-2533`, `Launchstation/src/config_advanced.c:27-51, 120-185`, `Launchstation/src/config_startup.c:30-54, 177-199`, `Launchstation/src/config.c:1374-1444, 1567-1571`, `Launchstation/src/dash_font.c:492-495`, `Launchstation/src/dash_apps.c:519, 541-544, 553-560`, `Launchstation/src/dash_editor.c:923`, `Launchstation/src/dash_devices.c:454, 465`
- **Problem:** `dash_clock_sync.c` duplicates `cloud_http.c`'s `ps2ipc_*`/socket types and its own BCD/calendar functions while missing `result_size > 0` checks (`429-477`); BCD decoding appears four times across `dash_browser.c`, `dash_card_tool.c`, and `dash_clock_sync.c`; `config_advanced.c` and `config_startup.c`/`config.c` duplicate `normalizeHideHddMode` and `"mc0"`/`"mc1"` path normalization; `dash_card_tool.c` duplicates batch summary strings three times; `dash_browser.c:3030-3032` shadows `sv_idx`; `cloud_http.c` re-parses `Content-Length` (3x via `atoi`) and `Transfer-Encoding` (2x); and `dash_apps.c` (`fx/fy/r/fr`), `config.c` (`default_sel`), `dash_font.c:492`, `dash_browser.c:2664`, and `dash_editor.c:923` contain discarded or identical branches (`dash_devices.c:454, 465` also keeps transient buffers `static`).
- **Fix:** Share `ps2ipc`/socket types, `bcdToInt`, `normalizeHideHddMode`, `"mc0"`/`"mc1"` normalization, and batch summary formatting; parse HTTP response headers once with `strtol`; add `result_size > 0` in `dash_clock_sync.c`; and delete dead local variables and identical branches.
- **Justification:** Adding `result_size > 0` prevents zero/negative output buffer writes in clock sync while removing widespread copy-paste.
- **Benefit:** Eliminates ~200+ lines of scattered duplication and no-op computations across 11 EE files.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-047: Repeated-key `strncat` overflow in `scanSystemCnf`, unbounded `main_actions.c` paths, and `popstarter_elf.c` overlapping `strcpy`
- **Category:** 2, 6 (Duplication, Bugs and security)
- **Location(s):** `Launchstation/src/main.c:67-70`, `Launchstation/src/main_startup.c:150-163`, `Launchstation/src/main_actions.c:535-539, 581-594, 726`, `Launchstation/src/popstarter_elf.c:116-157`
- **Problem:** `scanSystemCnf` passes full buffer capacity `- 1` to `strncat` on repeated `SYSTEM.CNF` keys (`SystemCnf_VMODE[10]` at `main.c:70` is also never read outside `readSystemCnf`), `main_actions.c` uses unbounded `strcpy`/`strcat` (`535-594`) and dead `x = x;` (`726`), and `popstarter_elf.c` duplicates `'/'` stripping via overlapping `strcpy` UB (`123-125, 141-143`) while `mass` (`152-155`) computes an index from `path` instead of `fullpath` and uses unbounded `strcpy`.
- **Fix:** Replace `strncat` in `scanSystemCnf` with bounded `snprintf` (and drop dead `SystemCnf_VMODE`), use `snprintf`/`memmove` in `main_actions.c`, delete `x = x;`, and extract one `memmove`-based path helper in `popstarter_elf.c`.
- **Justification:** Prevents repeated-key `SYSTEM.CNF` overflows and overlapping `strcpy` UB during ELF/POPStarter launches.
- **Benefit:** Hardens boot/launcher path builders and removes duplicate string-strip logic.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-048: No-op `switchPsxHddDriverStack`, unreachable `main_exploit_installer.c` guards, and identical `#ifdef CUSTOM_COLORS` enums
- **Category:** 1, 2 (Dead code, Duplication)
- **Location(s):** `Launchstation/src/init.c:2330-2346, 2667, 2719`, `Launchstation/src/main_exploit_installer.c:529-543, 2097-2120`, `Launchstation/include/config_private.h:10-28`, `Launchstation/ps2.yaml:620, 631`
- **Problem:** `switchPsxHddDriverStack` (`init.c:2330-2346`) only logs via `DPRINTF` and updates flags without loading/switching any IOP driver; `main_exploit_installer.c` repeats identical `dtl_signing_console` (`539-540`) and unreachable `console_is_PSX` (`2111-2116`) blocks; and `include/config_private.h:10-28` defines identical `DEF_COLOR1..DEF_COLOR8` values in both `#ifdef CUSTOM_COLORS` branches.
- **Fix:** Implement or delete `switchPsxHddDriverStack`, delete the duplicate/unreachable `if` branches in `main_exploit_installer.c`, and collapse `DEF_COLOR1..DEF_COLOR8`.
- **Benefit:** Removes misleading no-op driver-switch scaffolding and duplicate/dead branches.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-049: Eleven dead `#if 0` IOP blocks and `~2 KB` `strtmp[MAX_PATH]` stack waste in `config.c`
- **Category:** 1, 4 (Dead code, Size and memory)
- **Location(s):** `Launchstation/iop/ps2hdd_osd/cache.c:56`, `Launchstation/iop/ps2host/irx_imports.h:16`, `Launchstation/iop/ps2host/ps2host.c:14, 27`, `Launchstation/iop/usbd-ra/src/usbd.c:200`, `Launchstation/iop/vmcman/src/main.c:2913, 3360, 3372`, `Launchstation/iop/vmcman/src/ps1mc_fio.c:175, 189, 227, 525`, `Launchstation/src/config.c:743, 764`
- **Problem:** Eleven `#if 0` blocks remain across six IOP files, and `saveConfigToPath()` (`src/config.c:743, 764`) allocates two `char strtmp[MAX_PATH]` (`1,025 B` each) stack buffers solely to prepend `'/'` before `CNF`.
- **Fix:** Delete all 11 `#if 0` blocks and format `'/'` directly with `strcat(c, "/"); strcat(c, CNF);` (or bounded `snprintf`).
- **Benefit:** Cleans dead IOP preprocessor blocks and saves `~2 KB` of stack in `saveConfigToPath()`.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

### LAUNCHSTATION-050: Unbound `image_lo_hex` in `check_loader_layout.sh`, duplicate CMake args in `build_cloud_tls.sh`, orphaned `ps2ips_rpc/ps2.yaml`, and omitted CI tests
- **Category:** 2, 6, 7 (Duplication, Bugs and security, Build/CI/repo clutter)
- **Location(s):** `Launchstation/scripts/check_loader_layout.sh:2, 41-69, 93-95`, `Launchstation/scripts/build_cloud_tls.sh:15, 54-66`, `Launchstation/scripts/build_jpeg.sh:23, 40-52`, `Launchstation/scripts/ee_toolchain.sh:60-70`, `Launchstation/iop/ps2ips_rpc/ps2.yaml:1-11`, `Launchstation/tests/test_boot_deferred_init.py`, `Launchstation/tests/test_usb_stat_timeout.py`, `Launchstation/.github/workflows/compile.yml:436-439`
- **Problem:** `scripts/check_loader_layout.sh` (`set -eu`) references unset `image_lo_hex`/`image_hi_hex` on stripped-ELF fallback failure (`94`); `build_cloud_tls.sh` duplicates `cmake_toolchain_args` manually while omitting `-DCMAKE_SYSTEM_PROCESSOR=mips`; `iop/ps2ips_rpc/ps2.yaml` is an orphaned sub-manifest unused by root `ps2.yaml:203-210`; and `tests/test_boot_deferred_init.py` and `tests/test_usb_stat_timeout.py` are omitted from `.github/workflows/compile.yml`.
- **Fix:** Format `$image_lo`/`$image_hi` with `printf '%x'` at `check_loader_layout.sh:94`, reuse `cmake_toolchain_args` in `build_cloud_tls.sh`, delete `iop/ps2ips_rpc/ps2.yaml`, and wire or delete the two omitted host tests in `compile.yml`.
- **Justification:** Fixes stripped-ELF layout error reporting and keeps build/CI manifests accurate.
- **Benefit:** Eliminates CI script drift, unbound-variable abort, and orphaned build metadata.
- **Effort:** S. **Risk:** Low. **Confidence:** High (`READ`).

---

## Files Not Read

- **Source & build scripts read in full:** All `444` files across `src/*.c` (non-table sections), `include/*`, `ee_libs/*`, `iop/*` (`ata_bd`, `AllowDVDV`, `dev9_poweroff`, `ds34bt`, `ds34usb`, `hdl_info`, `include_compat`, `mmceman`, `mx4sio_bd`, `nfsman`, `oldlibs/ps2ftpd`, `pccf`, `ps2hdd_osd`, `ps2host`, `ps2ips_rpc`, `smb2man`, `smbd`, `smbman`, `udpfs_smap`, `usbd-ra`, `usbmass_bd`, `vmcman`, `xparam`), `loader/linkfile`, `scripts/*`, `windows/cloud-relay/*`, `research/sd2psxtd/*`, `.claude/*`, `.github/*`, and `.vscode/*` were read completely (`100%` of assigned code shards).
- **Large generated font tables (headers read; repeating hex/pixel literal data blocks skipped):** `src/dash_font_data.c` (lines `31-9657`), `src/dash_font_emotion_engine.c` (lines `31-8036`), `src/dash_font_emotion_engine_bold.c` (lines `31-8804`), `src/font_uLE.c` (lines `101-652`).
- **Static documentation, plans, website media, binary payloads, and localization assets:** `docs/` (`55` files), `docs-site/` (`123` files including GIF/MP4 media), `Lang/` (`10` `.LNG`/`.inc` files), `ship/` (`11` kit manifests/prebuilts/shortcuts excluding `ship/kit-externals.json` which was read), `exploits/` (`14` binaries/payloads), `__Headers/` (`13` save headers), `res/` binary assets (`bgm.ogg`, `icons/*.png`, `memory-card/*`).
- **Host test suite sources:** Individual `tests/test_*.py` bodies and fixtures beyond `test_app_packages.py`, `test_self_update.py`, and `test_zack_feedback.py` were executed via the automated `213`-script test run rather than opened line-by-line.
