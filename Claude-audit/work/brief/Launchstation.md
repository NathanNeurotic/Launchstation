# Launchstation: orientation brief

Audited commit `8155d14` (1181 commits). Paths are repo-relative. Statements not marked INFERRED were read directly.

## 1. What it is

Launchstation is a PS2 dashboard, file manager and homebrew launcher (README.md:7) shipped as one packed EE ELF with all IOP modules embedded. It adds a `dash_*` dashboard UI, MMCE/memory-card backup tooling, Google Drive backup, a self-updater, SMB1/SMB2/NFS/UDPFS/FTP clients and servers, and Neutrino/POPStarter/Ember launch handoffs on top of the classic wLaunchELF code. The project name inside the build is still `wLaunchELF` (ps2.yaml:18).

Lineage: LaunchELF -> uLaunchELF -> wLaunchELF -> wLaunchELF_R3Z (saildot4k) -> johnson-cooper fork -> this repo (docs/THIRD_PARTY_CREDITS.md:7-9).

Mostly upstream code: all of `iop/` except `iop/smbd`, `iop/nfsman/nfsman.c`, `iop/dev9_poweroff`; `ee_libs/`; classic uLE files in `src/` (`filer*.c`, `config.c`, `editor*.c`, `draw*.c`, `elf.c`, `pad.c`, `makeicon.c`, `font_uLE.c`, `icon.c`, `chkesr.c`, `hdl_rpc.c`; split of classic vs new is INFERRED from headers and docs/architecture-maintenance.md:87-116). `src/dash_*.c`, `mc_*.c`, `mmce_*.c`, `cloud_*.c`, `self_update.c`, `smb_*.c` are first-party (INFERRED from file headers).

## 2. Build

- Only entry point: `ps2.yaml` (ps2build). There is no Makefile; the legacy Makefile was retired 2026-10-26 (docs/BUILDING.md:22). Isolated check builds: `iop/pccf/ps2.yaml`, `iop/ps2ips_rpc/ps2.yaml`.
- Toolchain: PS2BUILD SDK, LLVM/Clang (`toolchain: llvm`, ps2.yaml:25); `PS2BUILD_TOOLCHAIN=gcc` or `--toolchain gcc` for comparison builds. SDK pins in `.github/ps2build-sdk.env`, installer `scripts/ps2build_sdk.sh`.
- Pre-build steps (compile.yml:324-349): `scripts/build_cloud_tls.sh` (Mbed TLS -> `.cloud-deps/`), `scripts/build_jpeg.sh` (libjpeg-turbo -> `.jpeg-deps/`), `scripts/gen_cloud_client.py` (-> `.cloud-deps/include/wle_cloud_client.h`), `scripts/gen_githash.py` (-> `githash.h`), `scripts/fetch_libsmb2.sh` (-> `.smbd-deps/`). All outputs are untracked (.gitignore:15,37-41).
- Artifacts: `build/bin/wLaunchELF.elf`, `build/bin/wLaunchELF-packed.elf` (release, `pack: true` ps2.yaml:487), `build/bin/*.irx`, `loader.elf`, `wle_igr.elf`. `scripts/package_apps.py` renames to `LAUNCHSTATION.ELF`, `LAUNCHSTATION-DS.ELF`, `BOOT.ELF`, `BOOT2.ELF`, `APP_LAUNCHSTATION[-DS].psu/.zip`; `scripts/package_kit.py` builds the full kit `LAUNCHSTATION-<build>.zip`.
- Variants: standard, and DS34 (`scripts/configure_ds34.py` rewrites ps2.yaml into untracked `.ps2-ds34.yaml`: swaps `src/pad.c` -> `src/pad_ds34.c`, adds `ds34usb`/`ds34bt` IOP+EE targets and `DS34`, lines 34-38; built to `build-ds34/`).
- Feature defines of the `wLaunchELF` target, all on by default (ps2.yaml:614-630): `WLE_PS2BUILD`:615, `NEWLIB_PORT_AWARE`:616, `LIBCDVD_LATEST`:617, `XFROM`:618, `DVRP`:619, `MMCE`:620, `MX4SIO`:621, `ILINK`:622, `HOMEBREW_SIO2MAN`:623, `ETH`:624, `WLE_NATIVE_CLOUD`:625, `UDPFS`:626, `SMB`:627, `TMANIP`:628, `EXFAT`:629, `CUSTOM_COLORS`:630. `DS34` default off (configure_ds34.py:36). The `KEY=1` names in ps2.yaml:8-9 are a comment only; `IOP_RESET` there has no define.
- Grep of `src include loader igr ee_libs` found no `#if` test of `TMANIP` (only `TMANIP_MORON`, src/filer_actions.c:1166) or of `LIBCDVD_LATEST` (only `LIBCDVD_LEGACY`, include/launchelf.h:66).
- Conditionals present in source but not defined by ps2.yaml or configure_ds34.py: `DEBUG` (src/main.c:16), `ULE_DEBUG_BUILD` -> `FILEOP_TRACE` (src/filer.c:156-163, filer_copy.c:22, filer_fileops.c:5), `UDPTTY` (src/init.c:56), `POWERPC_UART` (init.c:73), `SIO_DEBUG` (init.c:1244), `NO_IOP_RESET` (init.c:823), `IOPTRAP` (init.c:51), `WLE_BOOT_DIAGNOSTICS` (src/dash_main.c:1354), `DASH_AUTOTEST` (dash_main.c:1304, src/pad.c:284), `DASH_THEME_TOUR` (dash_main.c:1492), `KELF_SIGNER_HOST_TEST` (src/kelf_signer.c:1), `QRCODEGEN_TEST`. Host tests may define some of these.
- Environment inputs: `WLE_GOOGLE_CLIENT_ID`, `WLE_GOOGLE_CLIENT_SECRET` (CI secrets, embedded by gen_cloud_client.py), `PS2BUILD_ROOT`, `LLVM_BIN`.

## 3. What is compiled into the shipped product

- EE object list: ps2.yaml:488-612 (124 files: `src/*.c`, `ee_libs/tcpips/ps2ipc.c`:503, `src/embed_blobs.S`:612). `src/pad_ds34.c` replaces `src/pad.c` only in DS34. `src/popstarter_internal.h` is the one header under `src/`.
- EE libs: ps2.yaml:641 (local `iopreboot` :411-418, `hdd_ee` :423-427; SDK gskit, dmakit, png, z, mc, kbd, ps2ips, cdvd, filexio, patches, poweroff, debug, secr, pad, audsrv-compat; build-time `jpegturbo`, `mbedtls`, `mbedx509`, `mbedcrypto`).
- Source-built IOP targets (ps2.yaml:28-407): udpfs_smap, pccf, usbd_ra, dev9_poweroff, allowdvdv, hdl_info, ps2hdd_osd, mcman_physical, vmcman, mmceman, mx4sio_bd_fork, usbmass_bd_fork, ps2ips_rpc, ps2ftpd, ps2host, smb2man, nfsman, smbd, smbman, xparam, ata_bd. `mcman_physical` and `vmcman` compile the same `iop/vmcman/src` with different defines (ps2.yaml:116-151). `smbd` compiles 53 libsmb2 files from `.smbd-deps/` (ps2.yaml:300-352).
- Embedded EE ELFs: `loader` (ps2.yaml:431-452, `loader/loader.c`) and `wle_igr` (:454-483, `igr/wle_igr.c`), embedded as `loader_elf`, `wle_igr_elf` (:644-645).
- Embed list of the main ELF: ps2.yaml:642-699 (local targets plus SDK binary packages: poweroff, iomanx, filexio, dev9, netman, smap, ps2ip-nm, smap-ps2ip, netfs, fs, mcserv, cdfs, kbd, secrsif, xfromman, xfromserv, extflash, atad, pvrdrv, dvrfile, iLinkman, IEEE1394_bd_mini, sio2man, padman, bdm, bdmfs_fatfs, libsd, audio). `wle_igr` has its own embed list of 17 modules (ps2.yaml:466-483), 16 of which also appear in the main list (SDK `mcman` is in wle_igr only), and wle_igr.elf is itself embedded in the main ELF.
- Raw blobs via `.incbin`: src/embed_blobs.S:53-89 (exploit payloads from `exploits/`, `res/memory-card/icon.sys` + `list.icn`, `iop/__precompiled/udpfs_ministack.irx` + `udpfs_ioman.irx`, `iop/__v10/ps2dev9.irx` + `ps2ip.irx`, eight `res/sfx_ps2/*.adp`).
- Language tables: `src/lang.c:8-56` includes the nine `Lang/*.LNG`; `src/lang.c:581` includes `Lang/DASHBOARD.inc`.
- Loading: embedding does not load. `src/init.c` loads lazily with `SifExecModuleBuffer` (for example `load_ps2dev9` init.c:487, `load_ps2ip` :617, `loadUsbModules` :1740, `loadMmceModules` :2369, `loadNetModules` :2744, `load_smb` :939, `startSmbServer` :2785).
- Shipped beside the ELF (package_apps.py): `res/APPINFO.PBT`, `res/WLE-UPDATE-CA.PEM`, `res/music.sad` as `BGM.ADP`, `res/memory-card/*`.

## 4. Not part of the shipped ELF

- `tests/` (214 `test_*.py`, host C harnesses, stub headers in `card-tool-shim/`, `pccf-stubs/`, `ftp/`, `ftp-ee/`, live suites `smb2man_live/`, `smbd_live/`, `nfsman_live/`): host regression tests; run by an explicit list in compile.yml:50-266. Many grep production sources or build files (for example tests/test_llvm_toolchain.py:12-19).
- `scripts/`: build helpers and CI checks (section 2), release tooling (`package_apps.py`, `package_kit.py`, `release_notes.py`, `download_metrics.py`, `sync_github_wiki.py`), generators (`gen_dash_font.py`, `gen_sfx.py`), `audit_r3z_settings.py` (used by tests/test_r3z_config_inventory.py), `stale_code_audit.sh` (manual, not in CI), `prepare_psbbn_theme.py`, `copy_test_pack.py`.
- `.github/workflows/`: `compile.yml` (tests, both builds, checks, packages, rolling `latest` release on master), `docs-pages.yml`, `sync-wiki.yml`, `download-metrics.yml` (cron every 3 h), `neutrino-watch.yml`, `ps2build-sdk-watch.yml` (+ `.github/scripts/ps2build_sdk_watch.py` and its test).
- `docs/` (maintainer notes, plans, superpowers specs, wiki sources, `changes.txt`, `LAUNCHELF.CNF` sample), root `CHANGELOG.md`, `USB-STABILITY-AUDIT-2026-10-08.md`, `storage_driver_fixes_plan.md`: documentation only.
- `docs-site/`: website source (`build.py`, `content/`, `static/` 16.8 MB incl. two GIFs and one MP4 over 1 MB); published by docs-pages.yml.
- `ship/`: full-kit inputs (`KIT-README.txt`, `kit-externals.json` pins, `neutrino-ilink/*.irx` prebuilt, `.url` shortcuts); used only by `scripts/package_kit.py`.
- `__Headers/`: sample HDD partition header folders (`PP.*`); the app reads such a folder from a user device at runtime (src/hdd_header_injector.c:202); no build file references the repo copy.
- `research/sd2psxtd/`: restore-model C code, not linked; referenced by tests/test_mmce_restore_txn_model.py.
- `windows/cloud-relay/`: optional PC rclone relay scripts.
- `res/icons/*.png`, `res/bgm.ogg`, `res/WLE-CLOUD.CNF.example`: reference/source material, not embedded or packaged (res/README.md).
- `exploits/IOPRP.IMG`, `exploits/XSYSTEM2.XLF`: not referenced by any build file (exploits/README.md:26-27).
- `iop/__precompiled/udpfs_smap.irx`: not embedded; read only by scripts/check_udpfs_smap.py:35.
- `iop/ds34usb`, `iop/ds34bt`, `src/pad_ds34.c`: DS34 variant only.
- `iop/mmceman/src/mmcedrv.c`, `mmceigr.c`, `exports.tab`: not in the `mmceman` target (ps2.yaml:153-168); no other build reference found by grep.
- `iop/oldlibs/ps2ftpd/src/Makefile.linux`, `iop/*/riptopl.patch`, `iop/smbd/libsmb2-*.patch` (applied to the fetched libsmb2 by scripts/fetch_libsmb2.sh:15), `.claude/`, `.vscode/`, `.clang-format`, `.clangd`: tooling.

## 5. Vendored and generated

Vendored source (evidence in parentheses):
- `iop/vmcman` ps2sdk mcman @ff7c5f9 (README.md); `iop/ps2hdd_osd` ps2sdk APA (README.md); `iop/ata_bd` ps2sdk atad (file header); `iop/usbd-ra` ps2sdk USBD pre-rewrite via RiptOPL (ps2.yaml:667-669); `iop/usbmass_bd` sdk-core (ORIGIN.md); `iop/mx4sio_bd` ps2sdk via RiptOPL (ORIGIN.md); `iop/mmceman` ps2-mmce/mmceman (credits table); `iop/udpfs_smap` Neutrino smap (ORIGIN.md); `iop/pccf` mihhru/PS2CF (VENDORED.md); `iop/ps2ips_rpc` + `ee_libs/tcpips` ps2sdk @329e2bb (VENDORED.md); `iop/ps2host` ps2link (file headers); `iop/oldlibs/ps2ftpd` (README, LICENSE, LAUNCHSTATION.md); `iop/smbman` OPL smbman-ra (README.md); `iop/smb2man/usmb2` sahlberg/usmb2 + `smb2man.c`, `smb2auth.c` from RiptOPL (README.md, VENDORED.txt); `iop/nfsman/unfs4` sahlberg/unfs4 (VENDORED.txt); `iop/xparam` ps2sdk deckard; `iop/hdl_info`, `iop/AllowDVDV`, `iop/include_compat`, `iop/ds34usb`, `iop/ds34bt` inherited from R3Z (credits table; upstream not recorded); `ee_libs/hdd`, `ee_libs/iopreboot` ps2sdk (file headers).
- In `src/`: `qrcodegen.c` + `include/qrcodegen.h` (Nayuki, MIT header); `chkesr.c` (Neme & jimmikaelkael header); `main_gameid_table.c` (derived from OSDMenu table, header comment).

Generated or data files:
- `src/dash_font_data.c` (9657 lines), `src/dash_font_emotion_engine.c` (8036), `src/dash_font_emotion_engine_bold.c` (8804): "GENERATED by scripts/gen_dash_font.py" (line 2 of each).
- `src/font_uLE.c` (8x16 bitmap font array), `src/icon.c` (`icon_folder[1024]` byte array, 2055 lines): data tables, no generator recorded.
- `iop/xparam/src/database_merged.c` (887 lines, game table), `include/cloud_google_roots.h` (PEM roots), `Lang/*.LNG` + `Lang/DASHBOARD.inc` (string tables).
- `res/sfx_ps2/*.adp`: produced by `scripts/gen_sfx.py` (embed_blobs.S:81).
- `docs/BUILDING.md`: "generated from docs-site/content/building.html" (line 189). `tests/fixtures/r3z-06b06d0-config-keys.json`: read by scripts/audit_r3z_settings.py:99; that it is machine-generated is INFERRED.

Prebuilt binaries: `iop/__precompiled/*.irx`, `iop/__v10/*.irx`, `exploits/*`, `ship/neutrino-ilink/*.irx`, `res/music.sad`, `res/bgm.ogg`, `res/memory-card/*`, `__Headers/*/list.ico`, `docs-site/static/assets/**`.

## 6. Runtime architecture

- Boot: `main()` src/main.c:270 -> `performEarlyBootInitialization` (src/main_boot.c:36), `initializeRuntimeInputModules`/`DisplayModules` (src/main_modules.c:18,61), optional IGR backup (`runWleIgrBackup` main.c:132), then `DashRun()` (src/dash_main.c:1340), which never returns. The classic main menu was removed (commit 8155d14); classic screens are reached from the dashboard.
- UI loop: `while (1)` at dash_main.c:1397: input poll, per-section `screens[]` callbacks, throttled polls (remote/PC receiver 500 ms :1458, disc 1000 ms :1473, devices 250 ms :1477), `dashRenderScene`, `dashGfxPresent` (vsync).
- EE threads: UI thread plus one worker (`src/dash_worker.c:126`, 128 KB static stack :28, queue of 32 :29, priority UI+1 :118). Rules in dash_worker.c:1-22: one job at a time, UI does no IO while a job runs (`dashWorkerIoBusy`), completion callbacks run on the UI thread (`dashWorkerPump` :251), `dashWorkerAwait` :298 shows a modal overlay. 18 `dashWorkerSubmit` call sites (folder listing, copy at src/dash_ui.c:1458, settings/store saves, art, HDD, update install). DS34 adds a pad thread (src/pad_ds34.c:143).
- On the UI thread: driver loads at startup (`dashDevicesAutoStart`, comment dash_main.c:1293-1296), `dashConsoleIdentityInit`/`dashSoundInit` after first fade (:1517-1524). Cloud/HTTPS calls are made from src/dash_tools.c (:229, :361, :677, :860); their thread context was not traced (INFERRED: mixed).
- Config: `LAUNCHSTATION.CNF` (legacy `LAUNCHELF.CNF` read-only, src/main_boot.c:60-66), saved by `saveConfig` src/config.c:788; search order start folder, `mcN:/SYS-CONF/`, other card (config.c:237-318, 921). Dashboard files `DASHBOARD.CNF`, `DASHAPPS.CNF`, `DASHLIB.CNF` beside the loaded config or `mc0:/SYS-CONF/` (src/dash_store.c:5-9, 212), written through worker jobs (dash_store.c:651). Also `WLE-CLOUD.CNF` (src/cloud_google.c:269), `SMB2.CNF` (src/smb_shares.c:19), `IPCONFIG.DAT` (src/main_startup.c:112), `NEUT-<id>-A/B.CNF` (src/gui_neutrino.c:131), MC folder icon stamp (`configStampMcFolderIcon` config.c:399).
- Network: two IOP stacks, one at a time. Default NETMAN + SMAP + PS2IP (init.c:655-677); optional ps2sdk v1.0 DEV9/PS2IP "legacy" pair (init.c:496-499, 636-654); UDPFS stack (smap + ministack + ioman, `load_udpfs_stack` init.c:1010) switched via `switchNetworkStack` (init.c:625). EE sockets through `ps2ips_rpc` + `ee_libs/tcpips/ps2ipc.c`; TLS by Mbed TLS in `src/cloud_http.c`.
- Device backends (IOP): mc (mcman_physical/mcserv), vmcman, MMCE, USB (usbd_ra + usbmass_bd_fork + bdm + bdmfs_fatfs), MX4SIO, iLink, ATA (ata_bd) and APA/PFS (ps2hdd_osd + fs), PC CARD CF (pccf), cdfs, xfrom, DVRP, host (ps2host), smb (smb2man), smb1 (smbman), nfs (nfsman), udpfs; servers ps2ftpd, ps2netfs, smbd.
- Launch: `src/elf.c` (`RunLoaderElf` :833, `RunEmbeddedLoader` :524) runs the embedded loader, which is linked for the 0x84000-0xa0000 window (ps2.yaml:429).

## 7. Conventions relevant to dead/duplicate judgements

- AGENTS.md and CLAUDE.md are byte-identical and contain only an attribution policy ("Do not remove existing attribution", line 10). No "do not remove code X" rule.
- No script copies code between repos. Cross-repo copies are manual and recorded in ORIGIN/VENDORED/README files (RiptOPL: usbd-ra, mx4sio_bd, smb2man, smbman, `res/APPINFO.PBT`).
- Vendored code needs an ORIGIN.md/VENDORED.md; credits must be updated with ps2.yaml changes (docs/architecture-maintenance.md:126-132).
- Generated-file rules: font C arrays (docs/font-generation.md:55), `.ps2-ds34.yaml`, `docs/BUILDING.md` vs `docs-site/content/building.html`, gh-pages and wiki output.
- Intentional duplicates stated in the repo: `res/memory-card/copy.icn`/`del.icn` identical to `list.icn` (embed_blobs.S:66-67); mcman_physical/vmcman shared sources; `iop/nfsman` and `iop/smbd` reuse smb2man's `imports.lst`/`linkfile` shape (iop/nfsman/README.md); `ioplib.c`, `sio2man_hook.c` exist in both `iop/mmceman` and `iop/mx4sio_bd`; `smsutils.S` in both ds34 dirs.
- New tests must be added to the list in compile.yml to run (docs/BUILDING.md section 9). Tests assert on source text, so removals can break them.
- Variant naming: `*_fork` targets embedded under SDK names (ps2.yaml:174,190); `name:alias` in `embed_irx`.

## 8. Open questions for the audit lead

1. `iop/smbd/`, `iop/nfsman/nfsman.c`, `iop/dev9_poweroff/`, `igr/`, `loader/` are first-party code inside module/vendor-style folders.
2. `src/qrcodegen.c`, `src/chkesr.c`, `src/main_gameid_table.c`, `src/font_uLE.c`, `src/icon.c` and the three font files are vendored/generated code inside the first-party folder.
3. `iop/mmceman/src/mmcedrv.c`, `mmceigr.c`, `exports.tab` are tracked but not built: classify as vendored-unused or dead?
4. `iop/hdl_info`, `iop/AllowDVDV`, `iop/ds34*`, `iop/xparam` have no recorded upstream revision.
5. unfs4 commit differs between `iop/nfsman/README.md` (`449ba4e`) and `iop/nfsman/unfs4/VENDORED.txt` (`9c5f5f5`).
6. `res/README.md` names `sfx/*.adp`; the folder is `res/sfx_ps2/`.
7. libsmb2, Mbed TLS and libjpeg-turbo are compiled in but absent from the clone; auditors can only review their configuration (`iop/smbd/config.h`, `scripts/build_*.sh`).
8. Whether `tests/` stubs that mirror production headers count as duplication.
