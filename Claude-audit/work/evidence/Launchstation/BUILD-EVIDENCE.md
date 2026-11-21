# Launchstation: build evidence

Audited commit `8155d14`. Build copy: `build/Launchstation/` (never cited for source lines). Source citations are repo-relative paths under `src/Launchstation/`. Raw outputs: `evidence/Launchstation/raw/` (paths below are relative to that folder unless they start with `build/`, which means the build copy's output folder).

Two agents produced this evidence. The first did the builds, relinks, map analysis, warning passes and the `-fdata-sections` build, then hit a usage limit before writing a summary. The second (this file) checked those outputs, measured the experiment, and wrote this summary. Nothing was run on a console or emulator.

Status: **built** (standard and DS34 variants, full logs ending in `exit=0`; one what-if build).

## 0. Toolchain and commands

| Item | Value | Source |
|---|---|---|
| ps2build | v2026.11.06 | `ps2build.exe --version` |
| Components | llvm-toolchain v2026.11.05, sdk-core v2026.11.05, sdk-world v2026.11.05, ninja v1.13.2, srxfixup v2026.09.05, cmake/toolchain/tools/vcl v2026.11.04 | `<HOME>/AppData/Local/ps2build/versions.yaml` |
| Compiler | clang 24.0.0git (ps2build/llvm 701b53ab18b6) | `llvm-toolchain/llvm/bin/clang.exe --version` |
| Linker | LLD 24.0.0 (same commit) | `ld.lld.exe --version` |
| Toolchain choice | LLVM, as the project selects (`toolchain: llvm`, ps2.yaml:25). Not changed. | ps2.yaml |
| Deps | libjpeg-turbo 3.1.3 (`scripts/build_jpeg.sh`), Mbed TLS (`scripts/build_cloud_tls.sh`), libsmb2 (`scripts/fetch_libsmb2.sh`). Logs end in `exit=0`. | `dep-jpeg-build.log`, `dep-mbedtls-build.log` |

Build steps run by the first agent. Its exact command lines were not saved. The log headers show what ran:
- Standard build: ps2build generated `build/build.ninja`, then ninja ran with `-k 0` (`build-pass1-ninja-k0.log`, 499/499 steps, `exit=0`). A second `ps2build build` pass reported `ninja: no work to do.`, `exit=0` (`build-pass2-ps2build-build.log`).
- DS34 build: `scripts/configure_ds34.py` wrote the untracked `.ps2-ds34.yaml`, which was built into `build-ds34/` (`build-ds34.log`, 521/521, `exit=0`). Reconstructed from the log header `ninja: Entering directory 'build-ds34'` and the file's presence in the build copy.
- Map/gc/cref evidence: `raw/tools/relink_with_map.py` re-ran each link command, taken from `ninja -t compdb`, with `-Map --print-gc-sections --why-extract --cref` added. The outputs went to `build/Launchstation/evidence-relink/`. The relinked `wLaunchELF`, `wle_igr` and `loader` ELFs are byte-identical to the build outputs, and so is `smbd` against `smbd.notiopmod.elf` (checked with `cmp` in this session). The map figures therefore describe the shipped link exactly.
- Analysis: `raw/tools/run_all.py BUILD_COPY SDK_ROOT RAW_DIR` ran `analyze_link.py`, `xref_candidates.py`, `report_sizes.py`, `blob_duplicates.py` and `group_warnings.py`.
- Extra-warnings pass: `raw/tools/extra_warnings.py` re-ran every compile command with `-fsyntax-only` plus extra flags (`-Wextra -Wunused-macros -Wmissing-prototypes -Wmissing-variable-declarations -Wshadow -Wunreachable-code` and others, listed at tools/extra_warnings.py:19-22). It produces no objects.

Commands run in this session (exact):
- `py -3 -I fdata_eval.py` (kept in the session scratchpad). It ran `relink_with_map.py build-fdata tools/ninja.exe evidence-relink-fdata obj/wLaunchELF/wLaunchELF.unstripped.elf`, then `analyze_link.py` into `raw/experiment-fdata/link/`, then `llvm-size -A` and `llvm-size` on `build/` and `build-fdata/`. The relink printed `relinked output vs build output: IDENTICAL, 175 object inputs`.
- `py -3 -I summ.py`: counting over the existing TSVs (xref, archive members, gc lists).

## 1. Shipped artifacts

Release ELF: `build/bin/wLaunchELF-packed.elf` (`pack: true`, ps2.yaml:487; comment ps2.yaml:5). `scripts/package_apps.py:14-20` names it `LAUNCHSTATION.ELF`, `LAUNCHSTATION-DS.ELF`, `BOOT.ELF` and `BOOT2.ELF`, and ships only the res sidecars beside it (package_apps.py:34-56). No IRX is shipped loose.

| Artifact | Bytes | Source |
|---|---|---|
| build/bin/wLaunchELF-packed.elf (release, packed) | 1,837,136 | sizes/artifacts.tsv |
| build/bin/wLaunchELF.elf (stripped, unpacked) | 5,365,664 | sizes/artifacts.tsv |
| build/obj/wLaunchELF/wLaunchELF.unstripped.elf | 6,430,712 | sizes/artifacts.tsv |
| build-ds34/bin/wLaunchELF-packed.elf (DS variant release) | 1,846,848 | sizes/artifacts.tsv |
| build-ds34/bin/wLaunchELF.elf | 5,393,568 | sizes/artifacts.tsv |
| build/bin/wle_igr.elf (embedded in the main ELF) | 518,432 | sizes/artifacts.tsv |
| build/bin/loader.elf (embedded in the main ELF) | 16,444 | sizes/artifacts.tsv |
| build/lib/libhdd_ee.a / libiopreboot.a (linked) | 9,670 / 7,078 | sizes/artifacts.tsv |

Packing: `PS2PACK bin/wLaunchELF-packed.elf: File compressed, from 5365664 to 1837136 bytes, ratio = 65.76%` (last lines of `build-pass1-ninja-k0.log`). The packed file is 34.24% of the stripped ELF. DS34 packs 5,393,568 to 1,846,848, also reported as 65.76% (`build-ds34.log`). The packed ELF has one PT_LOAD with filesz 1,837,040 and memsz 1,858,040 (sizes/wLaunchELF-packed-sections.tsv). Every IRX size is in section 6.

## 2. Main EE ELF (unpacked)

### Sections

From `sizes/size-A.txt` (`llvm-size -A`; the stripped and unstripped ELFs have the same allocated sections):

| Section | Bytes |
|---|---|
| .text | 2,188,928 |
| .data | 1,669,488 |
| .rodata | 1,440,560 |
| .bss | 1,613,524 |
| .got / .sdata / .init_array / .reginfo / .MIPS.abiflags | 152 / 4 / 16 / 24 / 24 |
| Allocated total (Berkeley dec) | 6,912,720 |

Berkeley view: text 3,629,536, data 1,669,660, bss 1,613,524 (sizes/size-A.txt). The non-allocated debug and .pdr sections (1,064,635 bytes, sizes/wLaunchELF-unstripped-sections.tsv) are removed by STRIP.

### Allocated bytes by origin

From `link/wLaunchELF-contrib-summary.txt` (bytes of input sections placed in each output section, from the ld.lld map):

| Origin | .text | .rodata | .data | .bss | total |
|---|---|---|---|---|---|
| first-party (repo objects incl. vendored-in-repo) | 1,167,580 | 1,067,068 | 8,758 | 1,511,912 | 3,755,318 |
| embed-blob (`obj/_embed/*`) | 0 | 0 | 1,653,719 | 0 | 1,653,719 |
| third-party dep (mbedtls, libjpeg-turbo) | 627,632 | 56,879 | 24 | 10,439 | 694,974 |
| SDK libs | 384,516 | 19,962 | 6,289 | 89,607 | 500,394 |
| linker-internal (merged strings etc.) | 368 | 296,533 | 0 | 0 | 297,053 |

The first-party .rodata total includes `src/embed_blobs.o` (395,188, the `.incbin` payloads) and the three generated font files (606,464). In practice, 99% of .data is embedded IRX/ELF images.

### Top 80 symbols by size

Full list with address, type and binding: `sizes/wLaunchELF-unstripped-top-symbols.tsv`. Raw `llvm-nm -S --size-sort | tail -80`: `sizes/nm-size-sort-top80.txt`. Every symbol in the ELF: `sizes/wLaunchELF-unstripped-all-symbols-by-size.tsv`. Sizes in bytes (O = object, F = function):

518432 .data wle_igr_elf_irx; 164567 .data smbd_irx; 131136 .bss mcDir; 131072 .bss worker_stack; 122880 .rodata face2_px; 106496 .rodata dash_emotion_engine_bold_faces_2_px; 105895 .data ps2ip_irx; 100352 .rodata dash_emotion_engine_faces_2_px; 92400 .bss hddParties; 71532 .data mcman_physical_irx; 67952 .data vmcman_irx; 67584 .bss verify_out; 67584 .bss export_out; 65536 .bss export_in; 65536 .bss compare_a; 65536 .bss compare_b; 65536 .bss sv_io; 61127 .data xfromman_irx; 57344 .rodata face1_px; 55296 .rodata dash_emotion_engine_bold_faces_1_px; 47104 .rodata dash_emotion_engine_faces_1_px; 46200 .bss dvrParties; 43616 .data ps2fs_irx; 39089 .data bdmfs_fatfs_irx; 37778 .data padman_irx; 37160 .data smbman_irx; 34816 .rodata face0_px; 32800 .bss boot_images; 32768 .bss verify_buf; 32768 .bss hdd_header_copy_buffer; 32768 .rodata dash_emotion_engine_bold_faces_0_px; 31182 .data ps2ftpd_irx; 30750 .bss PathPad; 30273 .data smb2man_irx; 30060 .text F iconSdf; 30000 .bss ct_saves; 28684 .bss dashLists; 28672 .rodata dash_emotion_engine_faces_0_px; 28184 .bss snap_classic; 27549 .data ps2hdd_irx; 24000 .bss ct_details; 24000 .bss ct_titles; 23775 .data usbd_irx; 23476 .data pccf_irx; 21346 .data nfsman_irx; 21248 .bss fentries; 21064 .text F browseFilePath; 20658 .data mmceman_irx; 19872 .bss home_tmp; 19456 .bss __sbuff; 16446 .data libsd_irx; 16444 .data loader_elf_irx; 16384 .bss ct_cache; 16384 .bss fp_cache; 16384 .bss activeHistoryFilesEqual.buf_a; 16384 .bss activeHistoryFilesEqual.buf_b; 16097 .data smap_netman_irx; 15600 .text F mbedtls_ssl_handshake_server_step; 15297 .data ps2smap_irx; 15115 .data audsrv_irx; 15028 .data dvrfile_irx; 14592 .text F ShowExploitInstaller; 14586 .data ps2kbd_irx; 13912 .text F png_do_read_transformations; 13231 .data iLinkman_irx; 13039 .data netman_irx; 12853 .data mx4sio_bd_irx; 12660 .text F mbedtls_ssl_handshake_client_step; 12433 .data usbmass_bd_irx; 12352 .text F dashTextEditorEx; 12288 .bss batch_pts; 12288 .bss __intr_data; 12274 .data ata_bd_irx; 12160 .bss slots; 11937 .data dvrdrv_irx; 11572 .text F browserProcessInput; 11398 .data ps2dev9_irx; 11066 .data iomanx_irx; 11064 .data bdm_irx; 11052 .text F _vfprintf_r.

### Biggest contributors per object

From `link/wLaunchELF-contrib-summary.txt` (top 40 per section listed there; per-object, per-section table for all 175 link objects in `link/wLaunchELF-contrib-by-object.tsv`):
- .text: dash_browser.o 55,136; dash_settings.o 45,360; dash_card_tool.o 45,348; dash_icons.o 37,192; filer_actions.o 34,328; filer_browser.o 30,848; dash_editor.o 29,760; filer.o 28,504; init.o 26,756; dash_ui.o 26,064; cloud_google.o 23,736; main_exploit_installer.o 23,592; config.o 22,432; dash_tools.o 21,840; dash_widgets.o 20,820; qrcodegen.o 19,492.
- .rodata: embed_blobs.o 395,188; linker-internal 296,533; dash_font_data.o 221,952; dash_font_emotion_engine_bold.o 201,472; dash_font_emotion_engine.o 183,040; lang.o 28,840; dash_settings.o 11,576; cloud_http.o 5,420.
- .data: wle_igr_elf_irx.o 518,448; smbd_irx.o 164,583; ps2ip_irx.o 105,911; mcman_physical_irx.o 71,548; vmcman_irx.o 67,968; xfromman_irx.o 61,143; ps2fs_irx.o 43,632; bdmfs_fatfs_irx.o 39,105. First-party code objects together contribute only 8,758 bytes of .data.
- .bss: filer.o 275,492; mc_card_export.o 200,704; mmce_backup.o 149,696; dash_worker.o 133,620; dash_card_tool.o 109,504; dash_browser.o 106,560; mmce_save_versions.o 67,776; mmce_igr.o 65,600; dash_settings.o 39,416; dash_store.o 39,288; hdd_header_injector.o 33,920; config.o 30,760; dash_files.o 30,520.
- Archives by total allocated bytes: libmbedcrypto 275,847 (63 members); libjpegturbo 221,072 (54); libmbedtls 174,257 (12); libc 143,017 (174); libpng 86,474 (14); libkernel 55,126 (28); libfilexio 42,580 (2, of which 32,056 .bss); libz 25,534; libmbedx509 23,798; libm 20,280; libgskit 18,664; libcdvd 17,076; libcglue 15,960; libclang_rt.builtins 14,696; libmc 12,764. Smaller archives are listed in the same file.

### Embedded blobs

From `sizes/wLaunchELF-embedded-blobs.tsv`: every symbol `X` with a `size_X` companion, payload size read from `size_X`. **80 payloads, 2,051,959 bytes.**
- Largest: wle_igr_elf_irx 518,432 (wle_igr.elf, which embeds 17 IOP modules of its own); smbd_irx 164,567; ps2ip_v10_irx 118,513 (`.rodata`, `iop/__v10/ps2ip.irx` via src/embed_blobs.S); ps2ip_irx 105,895; sfx2_boot_adp 74,144 (sound); mcman_physical_irx 71,532; vmcman_irx 67,952; xfromman_irx 61,127; ps2fs_irx 43,616; bdmfs_fatfs_irx 39,089; exploit_list_icn 37,882 (icon); padman_irx 37,778.
- Sounds (8 ADPCM `sfx2_*_adp`, .rodata): 74,144 + 15,936 + 15,680 + 15,680 + 7,744 + 5,600 + 5,088 + 2,064 = 141,936.
- Icons/save metadata: exploit_list_icn 37,882; mc_list_icn 14,990; mc_icon_sys and four exploit_*_sys 964 each.
- Exploit payloads: exploit_ioprp_img 18,652; exploit_xlf_body 13,768; exploit_osdsys_kernel 7,056; exploit_osd110_kernel 6,992; three xlf headers of 128 bytes.
- Fonts: not blobs but generated C arrays in .rodata: dash_font_data.o 221,952, dash_font_emotion_engine.o 183,040, dash_font_emotion_engine_bold.o 201,472 (606,464 in total; per-face arrays such as face2_px 122,880 are in the top-symbol list).
- Duplicates (`sizes/embedded-blob-duplicates.tsv`, by SHA-256): 20 payloads are stored both in wLaunchELF and inside the embedded wle_igr. They are 16 IRX modules (ps2fs, bdmfs_fatfs, ps2hdd, usbd, mmceman, iLinkman, mx4sio_bd, usbmass_bd, ata_bd, ps2dev9, iomanx, bdm, mcserv, IEEE1394_bd, filexio, sio2man) plus 4 libkernel blobs. **269,968 redundant bytes** in the unpacked main ELF. Not measured: how much of that survives compression in the packed ELF.
- `ps2atad_irx` (11,477-byte payload, `atad:ps2atad` in the embed list, ps2.yaml) is garbage-collected. Its object contributes nothing and it is not in the shipped ELF (section 4).

## 3. Linker facts

- Compile flags of the `wLaunchELF` objects: `-O2 -Wall -ffunction-sections`, **no `-fdata-sections`**. Recorded in `build/Launchstation/build/build.ninja`, edge `obj/wLaunchELF/src/main.o`. Across the whole build, `-ffunction-sections` appears 197 times and `-fdata-sections` 68 times (`raw/experiment-fdata/size-compare.txt`). `-fdata-sections` is set only for nfsman, smb2man, smbd, loader and wle_igr (ps2.yaml:272, 289, 357, 436, 459).
- `--gc-sections`: yes. The wLaunchELF link command (`link-raw/wLaunchELF.link-command.txt`) contains `--gc-sections`, and the build.ninja file has 8 `--gc-sections` occurrences. loader, wle_igr, smb2man, nfsman and smbd also pass `-Wl,--gc-sections` (ps2.yaml:273, 290, 358, 439, 461).
- Linker script: SDK `packages-llvm/core/startup/share/ee/linkfile` (link command `-T`). Packing: `pack: true` (ps2.yaml:487), ps2pack, 65.76% saved (section 1).
- Libraries on the link line, in order: `-lstartup -liopreboot -lhdd_ee -lgskit -ldmakit -lpng -lz -ljpegturbo -lmc -lkbd -lmbedtls -lmbedx509 -lmbedcrypto -lps2ips -lcglue -lm -lcdvd -lfilexio -lpatches -lpoweroff -ldebug -lsecr -lpad -laudsrv-compat -laudio`, then the group `-lc -lm -lcdvd -lpthread -lpthreadglue -lcglue -lkernel -latomic -lclang_rt.builtins-mips64el` (`link-raw/wLaunchELF.link-command.txt`).
- Archive members pulled in: **482** (348 SDK, 131 third-party, 3 first-party: libhdd_ee `libhdd.o`, libiopreboot ×2). Each member is listed with its bytes, the input that first referenced it and the triggering symbol in `link/wLaunchELF-archive-members.tsv`; raw `--why-extract` output is in `link-raw/wLaunchELF.why-extract.tsv`. The `-ldebug` archive contributes no member: it does not appear in the per-archive table in `link/wLaunchELF-contrib-summary.txt`.
- Link objects: 175 (124 compiled sources plus `_embed` objects; `link/wLaunchELF-objects-contributing-nothing.txt`).

## 4. Dead-code evidence

### Discarded input sections

From `link/wLaunchELF-gc-summary.txt` (`ld.lld --print-gc-sections` with sizes read from the objects):
- In total, 4,552 sections were discarded. 1,737 of them are non-empty code or data sections, 412,748 bytes in all.
- First-party: **81 non-empty sections, 16,182 bytes** (76 code sections 13,996; 1 data 2,048; 4 rodata 138). Every section is listed in `link/wLaunchELF-gc-discarded-first-party.tsv` (82 rows including the embed blob).
- Embed-blob: 1 section, 11,493 bytes (`obj/_embed/ps2atad_irx.o` `.data`).
- SDK: 882 sections, 229,336 bytes. Third-party: 773 sections, 155,737 bytes.
- Largest first-party discards, in bytes: `icon.o .data` 2,048 (the whole object); `dash_themes.o` dashThemeFolderLoad 956, dashThemeFolderExport 384; `dash_font.o` dashTextWrapScrollTimed 804; libhdd_ee hddGetFilesystemList 784, hddExpandFilesystem 680; `main_title.o` menuTitleFormat 660, menuTitleGetClockText 220; `dash_icons.o` dashHeaderIcon 600; qrcodegen.o 8 sections, 1,424 (vendored); `dash_ui.o` dashDrawFocusFrame 524; `dash_browser.o` dashFileBrowser 496; `virtual_keyboard.o` getVirtualKeyboardEditorDisplayChar 380; `update_psu.o` selfUpdateExtractPsuSidecar 348, selfUpdateExtractPsu 296, selfUpdateExtractPsuMem 108; `config.o` configFormatSavePathValue 320, configAppendPathFile 236, CheckMC 172, configDefaultSaveTargetPath 44, saveConfig 28; `draw.o` loadIcon 308; `libiopreboot` SifIopRebootBufferEncrypted 280; `pad.o` reopenPadPorts 252; `elf.o` LaunchArgsCopyToArgv 236.
- Discarded code is not shipped. These are functions that are compiled but have no live caller in this link. Auditors still need a whole-repo grep, and a check of the DS34 variant, before calling any of them dead.
- Other links: loader 14 first-party sections, 372 bytes; smb2man 7, 1,224; nfsman 2, 228; smbd 1, 124; wle_igr 0. Every other IOP link discards 0 non-empty first-party sections. Per-link files: `link/<name>-gc-summary.txt`, `link/<name>-gc-discarded-first-party.tsv`.
- Limitation: wLaunchELF objects are built without `-fdata-sections`, so unreferenced globals in .data/.bss/.rodata cannot be discarded one at a time and are not visible in this list. Section 8 measures them.

### Objects compiled but contributing nothing

From `link/*-objects-contributing-nothing.txt` (zero bytes in any allocated output section, from the map):
- wLaunchELF: `src/icon.o` (its 2,048-byte `.data` is discarded) and `_embed/ps2atad_irx.o`.
- smbd: libsmb2 `errors.o`, `quic.o`, `smbdirect.o`, `sha1.o` (third-party, fetched).
- ps2hdd_osd: `iop/ps2hdd_osd/hdd_blkio.o`.
- All other links: none.

### Compiler warnings

- Default build: **25 unique diagnostics** (`warnings-default-build.txt`, rows in `warnings-default-build.tsv`). By type: -Wpointer-sign 11 (all iop/mmceman); -Wunused-but-set-variable 4 (iop/hdl_info 2, src 2); -Wunused-but-set-global 3 (src 2, e.g. `src/dash_themes.c:44 store_seen`, `src/dash_main.c:25 prev_section`; iop/ps2host 1); -Wincompatible-pointer-types-discards-qualifiers 3 (src, e.g. `src/ember.c:244`); -Wmisleading-indentation 1; -Wunused-function 1 (iop/smbman/smb_fio.c); -Wself-assign 1; -Wunused-variable 1 (src). By file: mmce_fs.c 9, hdl_info/hdd.c 2, mmce_cmds.c 2, init.c 2, and one each in ps2host/net_fsys.c, ember.c, main_startup.c, filer_browser.c, smbman/smb_fio.c, main_actions.c, filer_actions.c, dash_main.c, config.c, dash_themes.c.
- DS34 build: the same 25 (`warnings-ds34-build.txt`).
- Extra-warnings syntax-only pass: **763 unique** (`warnings-extra-pass.txt`, rows in `warnings-extra-pass.tsv`, raw log in `extra-warnings-syntax-only.raw.log`). By type: -Wmissing-prototypes 229 (src 29), -Wmissing-variable-declarations 180 (generated 104, src 27), -Wunused-parameter 153 (third-party 111), -Wunused-macros 132 (generated 52, third-party 43, src 8), -Wshadow 38 (src 19), plus the 25 default warnings. Also -Wunterminated-string-initialization 2 (iop/AllowDVDV, src), -Wunused-function 2 (smbd, smbman), -Wunreachable-code 2 (third-party, src) and -Wcast-function-type-mismatch 1 (iop/ps2host). The grouping by area and the top 60 files are in the .txt.

## 5. Cross-reference candidates (CANDIDATES ONLY)

These are first-party global symbols defined in one object of a link and left undefined by no other input of the same link (`xref/<link>-xref-candidates.tsv`; method in the file header). They are not proof of dead code. The scan did not check DS34, calls by name from crt0 or the linker script, IRX export tables in other links, or function-pointer use across variants.

| Link | candidates | not in final ELF | zero relocs anywhere | used only in own object (could be `static`) |
|---|---|---|---|---|
| wLaunchELF | 152 | 77 (25,417 bytes of symbol size) | 82 | 70 |
| wLaunchELF, `src/` objects only | 140 | 69 (11,964 bytes) | | 67 |
| loader | 12 | 12 (196 bytes) | 12 | 0 |
| smb2man | 30 | 7 (1,224) | 12 | 17 |
| nfsman | 22 | 2 (228) | 9 | 13 |
| vmcman / mcman_physical | 64 / 47 | 0 | 18 / 5 | 46 / 42 |
| usbd_ra | 56 | 0 | 6 | 50 |
| mmceman | 35 | 0 | 10 | 25 |
| smbman | 28 | 0 | 3 | 25 |

Counts for the other links are in the per-link files; each of those links has at most 19 candidates.

In wLaunchELF, four `src/` globals have no relocation anywhere in the link yet are still in the final ELF. Without `-fdata-sections` they cannot be garbage-collected on their own: `editor_input.o HFONTS` (4, .rodata), `filer.o mcfreeSpace` (4, .bss), `main.o DiscTypes` (340, .data), `main.o CurrTime` (8, .bss).

## 6. IRX modules built from source

All 21 source-built IOP targets appear in the wLaunchELF `embed_irx` list (ps2.yaml, `- name: wLaunchELF` block). Each embedded payload size equals its `.irx` file size (`sizes/artifacts.tsv` vs `sizes/wLaunchELF-embedded-blobs.tsv`). None is shipped loose; package_apps.py ships ELFs and res sidecars only.

| Target | .irx bytes | Embedded as | Also in wle_igr |
|---|---|---|---|
| smbd | 164,567 | smbd_irx | |
| mcman_physical | 71,532 | mcman_physical_irx | |
| vmcman | 67,952 | vmcman_irx | |
| smbman | 37,160 | smbman_irx | |
| ps2ftpd | 31,182 | ps2ftpd_irx | |
| smb2man | 30,273 | smb2man_irx | |
| ps2hdd_osd | 27,549 | ps2hdd_irx | yes |
| usbd_ra | 23,775 | usbd_irx | yes |
| pccf | 23,476 | pccf_irx | |
| nfsman | 21,346 | nfsman_irx | |
| mmceman | 20,658 | mmceman_irx | yes |
| mx4sio_bd_fork | 12,853 | mx4sio_bd_irx | yes |
| usbmass_bd_fork | 12,433 | usbmass_bd_irx | yes |
| ata_bd | 12,274 | ata_bd_irx | yes |
| ps2host | 10,720 | ps2host_irx | |
| udpfs_smap | 8,185 | udpfs_smap_irx | |
| xparam | 6,866 | xparam_irx | |
| hdl_info | 6,682 | hdl_info_irx | |
| ps2ips_rpc | 5,398 | ps2ips_irx | |
| allowdvdv | 1,890 | allowdvdv_irx | |
| dev9_poweroff | 945 | dev9_poweroff_irx | |

Total: 597,716 bytes of source-built IRX. DS34 adds ds34bt.irx 16,383 and ds34usb.irx 9,141 (`build-ds34/bin`, sizes/artifacts.tsv). Per-IRX section sizes, contributors, discards and xref files are in `link/<target>-*.tsv`. Unstripped and stripped `.notiopmod.elf` sizes are in `sizes/artifacts.tsv`.

Embedded helper ELFs:
- wle_igr.elf 518,432 bytes: .text 165,440, .data 344,316 (of which embedded IRX 337,400), .rodata 7,534, .bss 66,628 (`sizes/wle_igr-stripped-sections.tsv`, `link/wle_igr-contrib-summary.txt`). First-party code `igr/wle_igr.o` is 2,328 bytes of .text.
- loader.elf 16,444 bytes: .text 15,244, .bss 39,592 (`sizes/loader-stripped-sections.tsv`). libfilexio contributes 31,812 bytes of that .bss.

## 7. Stack, heap, .bss

From `sizes/wLaunchELF-crt-symbols.txt` and `link/wLaunchELF-output-sections.tsv` (linker-script PROVIDEs):
- `_stack_size = 128 * 1024` (0x20000).
- `_stack = -1` (top of RAM chosen at start-up).
- `_heap_size = -1` (all free memory).
- No first-party override: a grep of `src/Launchstation` (*.c, *.h, *.S, *.s, *.yaml, *.ld) for `_stack_size|_heap_size|\b_stack\b` found nothing.

Memory layout:
- `.bss` is **1,613,524 bytes**, first-party 1,511,912 (section 2).
- `_end = 0x797bd4` (7,961,556): image plus .bss ends at about 7.6 MiB.
- INFERRED heap estimate: 33,554,432 − 7,961,556 − 131,072 ≈ 25.46 MB left for the heap at most. This ignores kernel-reserved RAM below 0x100000 and SDK start-up allocations.
- The dashboard worker thread has its own 128 KB static stack, `worker_stack` 131,072 bytes in .bss (top-symbol list; src/dash_worker.c:28 per brief).

## 8. What-if experiment: `-fdata-sections` (NOT the shipped configuration)

The first agent built `build/Launchstation/build-fdata/` with `-fdata-sections` added to the 124 compile edges that lacked it. Those are exactly the wLaunchELF objects (`raw/experiment-fdata/edges-gaining-fdata-sections.txt`; flag counts 68 → 192). Everything else was identical, including `--gc-sections`. Its log is `experiment-fdata-build.log`: 499/499 steps, `exit=0`.

How it injected the flag was not recorded. `ps2build build` has no cflags option (`ps2build build --help`), and the tracked `ps2.yaml` in the build copy is byte-identical to `src/`. INFERRED: a temporary edited config copy or a regenerated ninja file.

Measured in this session (`raw/experiment-fdata/size-compare.txt`):

| Measure | shipped config | -fdata-sections | delta |
|---|---|---|---|
| .text | 2,188,928 | 2,188,928 | 0 |
| .rodata | 1,440,560 | 1,440,304 | −256 |
| .data | 1,669,488 | 1,668,952 | −536 |
| .bss (RAM only) | 1,613,524 | 1,612,912 | −612 |
| stripped wLaunchELF.elf | 5,365,664 | 5,364,896 | −768 |
| wLaunchELF-packed.elf | 1,837,136 | 1,835,424 | −1,712 |
| wle_igr.elf / loader.elf | 518,432 / 16,444 | same | 0 (already built with -fdata-sections) |

First-party gc discards rise from 81 sections / 16,182 bytes to 96 sections / 16,723 bytes (`raw/experiment-fdata/link/wLaunchELF-gc-summary.txt`). The newly discarded objects are bss 3 sections / 92 bytes, data +2 / +340 and rodata +10 / +109; the full list is in `raw/experiment-fdata/link/wLaunchELF-gc-discarded-first-party.tsv`. The allocated size falls by 1,404 bytes. That is more than the 541 extra discarded bytes; INFERRED: the rest is alignment padding saved by splitting sections. Conclusion: adding `-fdata-sections` to wLaunchELF saves about 1.7 KB of packed ELF and 0.6 KB of .bss.

No `--gc-sections` experiment was needed, since it is already on for every EE link and for the IOP links listed in section 3. No GCC comparison build was run.

## 9. Deviations

- No tracked file was edited in the build copy. Its `ps2.yaml` matches `src/Launchstation/ps2.yaml` byte for byte (checked in this session). `git status --short` in the build copy shows only `build-fdata/` and `evidence-relink/` as untracked; the other outputs are covered by `.gitignore`. `src/Launchstation` was never written.
- Untracked generated inputs were created in the build copy by the repo's own scripts: `.cloud-deps/`, `.jpeg-deps/`, `.smbd-deps/`, `githash.h` and `.ps2-ds34.yaml`. The Google client ID/secret environment inputs (gen_cloud_client.py) were not checked by this agent, and no generated header content was copied into evidence.
- Relinks added only output-neutral flags (`-Map --print-gc-sections --why-extract --cref`) and wrote to `evidence-relink/` and `evidence-relink-fdata/`. All four ELFs compared are byte-identical to the build outputs.
- One what-if build (`build-fdata/`, section 8). Its exact injection method was not recorded.
- The first agent's exact build command lines were not saved; section 0 reconstructs them from log headers.
- Raw link outputs in `raw/link-raw/` are path-normalised (`$SDK`, repo-relative). The 2.4 MB `wLaunchELF.map` is kept whole because the tables are derived from it.
