# Third-Party Credits and License Inventory

> Reviewed against the repository source, `ps2.yaml`, the build scripts and documentation as of 2026-10-26. This is an attribution/provenance inventory, **not** a replacement for individual licenses or a claim that every dependency is packaged into the release.

## Lineage and contributors

- **[wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z)** — direct upstream foundation, maintained by R3Z3N / [Saildot4K](https://github.com/saildot4k). Earlier wLaunchELF, uLaunchELF and LaunchELF contributors retain attribution.
- **[johnson-cooper](https://github.com/johnson-cooper)** — created this fork and handed its maintenance to [NathanNeurotic / Ripto](https://github.com/NathanNeurotic).
- **[NathanNeurotic / Ripto](https://github.com/NathanNeurotic)** — current maintainer.
- **[techwritescode](https://github.com/techwritescode)** — development collaboration and the [PS2BUILD SDK](https://git.techwritescode.dev/ps2/ps2build) used to build releases; **[TnA](https://github.com/TnA-Plastic)** — insight and support; **[oMrRexD](https://github.com/oMrRexD)** — remote/FTP PC-side ecosystem reference and collaboration; **[zackcage6](https://github.com/zackcage6) and FitthFox** — dashboard and file browser testing and UX feedback.
- **[ps2dev community](https://github.com/ps2dev)** — toolchain, driver, graphics and filesystem foundations. Additional contributors retain credit through original source comments, Git history, and upstream notices.

## How to read the tables

*Role* values: **vendored** (source in this repository), **compiled** (built from upstream or an SDK package into the ELF), **embedded binary** (a prebuilt module or data file placed in the ELF), **external** (a separate program or hardware the user installs), **service** (a network service the app contacts on request) and **reference** (consulted, nothing redistributed). "Release build" means `ps2.yaml` (PS2BUILD, LLVM/Clang).

## Vendored source in this repository

| Project or contributor | Evidence in this repository | Upstream / license information | Role and notes |
| --- | --- | --- | --- |
| [PS2CF](https://github.com/mihhru/PS2CF) / [mihhru](https://github.com/mihhru) | `iop/pccf/`, `tests/test_pccf*`, `tests/pccf-stubs/`; pinned origin in `iop/pccf/VENDORED.md` | Original component notices retained; PS2SDK-derived hardware code AFL-2.0, bundled `iop/pccf/PS2SDK_LICENSE.txt`; host ABI stubs copied from [PS2BUILD SDK](https://git.techwritescode.dev/ps2/ps2build) headers (AFL-2.0) | Early-PS2 CF driver; embedded in the release build and loaded on demand; filesystem integration unfinished |
| [Neutrino](https://github.com/rickgaiser/neutrino) SMAP driver — [Rick Gaiser](https://github.com/rickgaiser) / contributors | `iop/udpfs_smap/`, `tests/test_udpfs_smap_fifo_tail.py`, `tests/test_udpfs_smap_startup.py`; pinned origin in [ORIGIN.md](../iop/udpfs_smap/ORIGIN.md) | Neutrino `iop/smap` at `06188cce7cd07d2a158cb4b2646c5719fc16a565`; [bundled AFL-3.0](../iop/udpfs_smap/LICENSE) | Source-built UDPFS NIC module with receive-tail and startup corrections; checked by `scripts/check_udpfs_smap.py` |
| PS2SDK `mcman` → `vmcman` | `iop/vmcman/` (see its [README](../iop/vmcman/README.md)) | [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk) commit `ff7c5f93763d90c8b10bc97fbce5d25939ed94fb`; AFL-2.0 headers; copyright [jimmikaelkael](https://github.com/jimmikaelkael) (2009) and ps2dev | Compiled; local VMC fixes and diagnostics |
| PS2SDK APA/`ps2hdd` | `iop/ps2hdd_osd/` (see its [README](../iop/ps2hdd_osd/README.md)) | [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk); AFL-2.0 headers | Compiled; local fix tracking the HDD unit per open slot |
| PS2SDK `atad` (BDM build) | `iop/ata_bd/` | ps2dev/ps2sdk; copyright 2003 Marcus R. Brown, AFL-2.0 | Compiled; the SDK suite does not package `ata_bd` |
| `hdl_info`, `xparam`, `AllowDVDV`, `dev9_poweroff`, `iop/include_compat` | `iop/hdl_info/`, `iop/xparam/` (Deckard `xparam` and a homebrew database), `iop/AllowDVDV/`, `iop/dev9_poweroff/`, `iop/include_compat/` | PS2SDK-derived headers (AFL-2.0) where present; `AllowDVDV`/`dev9_poweroff` carry no copyright header; upstream of the remaining parts is not recorded in the repo | Compiled IOP helpers |
| PS2SDK EE sources | `ee_libs/hdd/` (libhdd), `ee_libs/iopreboot/` (libiopreboot) | [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk); libhdd and iopreboot AFL-2.0 | Compiled with the active toolchain |
| MMCEMAN / MMCEDRV | `iop/mmceman/` | [ps2-mmce/mmceman](https://github.com/ps2-mmce/mmceman) (MIT in upstream repo); upstream acknowledges Wisi and [Maximus32](https://github.com/rickgaiser); file headers carry the PS2SDK AFL-2.0 boilerplate | MMCE filesystem, protocol and GameID foundations; compiled from source in the release build |
| RiptOPL USB driver | `iop/usbd-ra/` | [NathanNeurotic/Open-PS2-Loader](https://github.com/NathanNeurotic/Open-PS2-Loader); AFL-2.0 PS2SDK headers; source review needed for any other inherited notices | Compiled; updated usbd-ra imported in USB stability work |
| PS2Link host (`ps2host`) | `iop/ps2host/` | [ps2dev/ps2link](https://github.com/ps2dev/ps2link); copyright 2003 Tord Lindstrom (pukko), 2004 adresd; file headers cite the PS2Link License (no license file is checked in beside it) | Compiled; the `host:` file-I/O server only |
| PS2FTPD | `iop/oldlibs/ps2ftpd/` | [ps2homebrew/ps2ftpd](https://github.com/ps2homebrew/ps2ftpd), Jesper Svennevid; [bundled AFL 2.0](../iop/oldlibs/ps2ftpd/LICENSE) | Compiled; started with anonymous login enabled |
| DS34 USB/Bluetooth | `iop/ds34usb/`, `iop/ds34bt/`, `src/pad_ds34.c` | inherited from [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z); thanks to [Alex Parrado](https://github.com/parrado) and upstream authors; AFL-2.0 PS2DEV headers | Compiled, DualShock package only |
| PS2BUILD SDK `audio` package and `audsrv-compat` | `ps2.yaml` (`embed_irx: "audio:audsrv"`, `libs: audsrv-compat`); used by `src/dash_sound.c` | [PS2BUILD SDK](https://git.techwritescode.dev/ps2/ps2build); AFL-2.0 | SDK `audio.irx` driver embedded unchanged; `audsrv-compat` EE client (audsrv-compatible API on top of `audio`) compiled by the active toolchain. Replaces the former vendored ps2sdk audsrv EE client and prebuilt `audsrv.irx` (LGPL-2.0), which were removed |
| QR Code generator library | `src/qrcodegen.c`, `include/qrcodegen.h` | [Project Nayuki](https://github.com/nayuki/QR-Code-generator), MIT (notice at the top of the file) | Vendored; Google Drive link screen |
| Roboto | `src/dash_font_data.c` (rasterized atlases), `scripts/gen_dash_font.py`, `docs/licenses/Roboto-COPYRIGHT.txt` | [Roboto](https://fonts.google.com/specimen/Roboto), Copyright 2015 Google Inc., Apache-2.0; [bundled notice](licenses/Roboto-COPYRIGHT.txt) | Dashboard typeface glyph data |
| uLaunchELF default font | `src/font_uLE.c` | Inherited from the LaunchELF/uLaunchELF lineage; no separate notice in the file | Built-in 8x16 bitmap font |

## Compiled dependencies and SDK components (release build)

| Project | Where it enters the build | Upstream / license information | Role |
| --- | --- | --- | --- |
| [PS2BUILD SDK](https://git.techwritescode.dev/ps2/ps2build) ([techwritescode](https://github.com/techwritescode) and contributors) | installed by CI and `.claude/hooks/session-start.sh`; `ps2.yaml` | SDK *core* packages: AFL-2.0 per the SDK's `licenses/core/LICENSE`; *world* packages carry their own licenses under `licenses/world/` (for example libpng, zlib) | Build tool, LLVM/Clang toolchain, EE/IOP libraries and IOP modules |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6.7 | `scripts/build_cloud_tls.sh` (tarball SHA-256 pinned); linked by `src/cloud_http.c` | Apache-2.0 (upstream also offers GPL-2.0-or-later) per [docs/NATIVE_CLOUD.md](NATIVE_CLOUD.md) | Compiled; HTTPS for Google Drive and updates |
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) 3.1.3 | `scripts/build_jpeg.sh`; linked by `src/dash_jpeg.c` | IJG / BSD-3-Clause / zlib per its `LICENSE.md` | Compiled; JPEG viewer |
| libpng, zlib | `ps2.yaml` `libs:` (`png`, `z`); `src/dash_png.c` | [libpng](http://www.libpng.org/pub/png/libpng.html) and [zlib](https://zlib.net/) licenses as packaged by the SDK | Compiled; PNG decoding |
| [gsKit / dmaKit](https://github.com/ps2dev/gsKit) | `ps2.yaml` `libs:` (`gskit`, `dmakit`) | Chris "Neovanglist" Gilbert and contributors; upstream AFL-2.0 | Compiled; GS rendering |
| PS2SDK EE libraries and newlib | `ps2.yaml` `libs:` (`mc`, `kbd`, `pad`, `cdvd`, `filexio`, `patches`, `poweroff`, `debug`, `secr`, `ps2ips`, `cglue`, `m`) | Licenses of the SDK packages and toolchain | Compiled |
| PS2SDK IOP modules (SDK packages) | `ps2.yaml` `embed_irx:` — iomanX, fileXio, DEV9, PS2IP, SMAP, netfs, mcman, mcserv, cdfs, ps2kbd, sio2man, padman, libsd, BDM family (bdm, bdmfs_fatfs, usbmass_bd, mx4sio_bd, iLinkman, IEEE1394_bd), ps2fs, poweroff, secrsif, PSX DVR/flash modules | SDK package licenses (AFL-2.0 for core); `bdmfs_fatfs` wraps the FatFs library | Embedded binary |
| PS2SDK socket RPC server — ps2dev / contributors | `iop/ps2ips_rpc/`; pinned origin and local changes in [VENDORED.md](../iop/ps2ips_rpc/VENDORED.md) | [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk/tree/329e2bb43287e16af16c4dde1c614e2662c24468/iop/tcpip/tcpips); bundled Academic Free License 2.0 | Source-built IOP socket server for clock sync and Internet clients; EE client remains an SDK library |
| PS2SDK v1.0 DEV9 / PS2IP | `iop/__v10/`, embedded by `src/embed_blobs.S` | PS2SDK v1.0, preserved from [oMrRexD](https://github.com/oMrRexD)'s builds | Embedded binary; optional *Legacy Ethernet drivers* |
| UDPFS ministack and ioman | `iop/__precompiled/udpfs_ministack.irx`, `udpfs_ioman.irx` | Client half of the UDPFS protocol served by [UDPFSD](https://github.com/pcm720/udpfsd); exact upstream revision and license are not recorded in the repo | Embedded binary; ABI checked by `scripts/check_udpfs_smap.py` |
| Public CA roots | `include/cloud_google_roots.h` (Google Trust Services R1–R4, expire 2036-06-22); `res/WLE-UPDATE-CA.PEM` (Sectigo Public Server Authentication Root R46; ISRG Root X1) | Public certificates from [pki.goog](https://pki.goog/) and the CAs | Embedded data; HTTPS verification |

`iop/__precompiled/` now holds only `udpfs_ioman.irx` and `udpfs_ministack.irx` (embedded, above) and `udpfs_smap.irx`, a defective historical SMAP kept solely so `scripts/check_udpfs_smap.py` can verify the ELF does not embed it. The prebuilt SDK-duplicate modules that used to sit there were removed on 2026-10-26; the release takes those from SDK packages or `ps2.yaml` targets. The upstream and revision of the remaining prebuilt files are not recorded in this repository.

## Bundled data and tooling of unresolved provenance

| Item | Location | Status |
| --- | --- | --- |
| Memory-card installer payloads (`SYSTEM.XLF`, `OSDSYS.KERNEL`, `IOPRP_LTS.IMG`, `*.sys`, `list.icn`, …) | `exploits/`, embedded through `src/embed_blobs.S` | Inherited binaries; authorship and licensing are not documented in the repo. LoadBOOTer's author is not verified |
| KELF signing | `src/kelf_signer.c` | Implements on-console MagicGate/KELF signing from a user-supplied `PS2KEYS.dat`; no upstream is cited in the file |
| Background music, sound effects, button glyphs | `res/music.sad` (packaged as `BGM.ADP`), `res/sfx/`, `res/icons/`, `res/bgm.ogg` | Origin and license not recorded in the repo; confirm before redistribution outside the project's packages. `res/bgm.ogg` is not packaged or embedded |
| PS2 sound theme | `res/sfx_ps2/`, generated by `scripts/gen_sfx.py` | Original procedurally synthesized tones written for this project (project licence); not sampled from Sony firmware |
| Windows Cloud Relay | `windows/cloud-relay/` | Original scripts; they drive the external [rclone](https://rclone.org/) (MIT) that the user installs |
| sd2psXtd restore model | `research/sd2psxtd/` | Original research code, not linked into any ELF |

**License risk:** There is **no root `LICENSE` file in this repository snapshot**. Do not infer uniform relicensing of inherited code or a single license that overrides component terms. Preserve upstream copyright/license headers in source and release documentation. Confirm which legacy subdirectories are actually embedded/shipped per build variant before declaring a complete binary distribution notice set.

## Independently installed applications and services

These are mentioned or launched through an integration; that does **not** imply their binaries or licenses are included:

| Project | Why LAUNCHSTATION references it |
| --- | --- |
| [Neutrino](https://github.com/rickgaiser/neutrino) — [Rick Gaiser](https://github.com/rickgaiser) / contributors | External PS2 ISO loader, quick-boot/backing-store backend |
| [POPStarter](https://www.psx-place.com/threads/popstarter.19139/) — [krHACKen](https://www.psx-place.com/members/krhacken.98/) | External PS1 VCD launcher |
| [Ember](https://github.com/Gageformer/Ember) — [Gageformer](https://github.com/Gageformer) | External PS1 core; its beta license limits redistribution/commercial use. See upstream LICENSE-BETA.txt |
| [PS2Link](https://github.com/ps2dev/ps2link), [PS2Client](https://github.com/ps2dev/ps2client) | Network debugger and PC-side transfer/launch ecosystem; Launchstation embeds only the `host:` server half |
| [UDPFSD](https://github.com/pcm720/udpfsd) — [pcm720](https://github.com/pcm720) | PC-side network filesystem server |
| [OSDMenu](https://github.com/pcm720/OSDMenu) — [pcm720](https://github.com/pcm720) | KELF launcher and HDD-OSD/PSX references; the embedded loader's low-memory layout follows its approach |
| [KELFTool](https://github.com/xfwcfw/kelftool) — xfwcfw | External PS2/PSX KELF handling tool |
| [HDD-OSD Icon Generator](https://github.com/CosmicScale/HDD-OSD-Icon-Generator) — [CosmicScale](https://github.com/CosmicScale) | External HDD partition art generator |
| [PixelFX RetroGem](https://www.pixelfx.co/hdmi-retro-gem) | External video hardware and Game ID compatibility |
| [rclone](https://rclone.org/) | Used by the optional PC Cloud Relay helper |
| [Google Drive API](https://developers.google.com/workspace/drive/api/guides/about-sdk) / [Google PKI](https://pki.goog/roots.pem) | Public OAuth/Drive and CA resources for original cloud implementation |
| [GitHub releases and REST API](https://docs.github.com/en/rest) | Release metadata, commit comparison and package download for the update check |
| Public time server (`0.pool.ntp.org`) | Optional Internet clock sync |
| [SD2Cloud/MMCE ecosystem](https://github.com/ps2-mmce) | Comparison/integration context; **no SD2Cloud source was incorporated** |
| [oMrRexD](https://github.com/oMrRexD) / R3Z PC command environment | Reference for compatible remote command/transfer behavior |

## Maintenance checklist

1. Keep the user's documentation in `docs-site/content/`, mirrored to relevant `docs/wiki/` guides where useful. Do not patch generated `gh-pages` HTML.
2. For each new external reference, add its proper name and canonical source to the website credits and this inventory; specify **vendored code**, **compiled dependency**, **embedded binary**, **external program**, **external service** or **reference only**.
3. For new vendored source, record upstream repository, base revision, component path, license and any local modifications (an `ORIGIN.md` or `VENDORED.md` beside it, as in `iop/udpfs_smap` and `iop/pccf`).
4. Check linked binary/package provenance independently. Do not assert a license for the whole tree based on a single checked-in notice.
5. When `ps2.yaml` gains or drops a library or `embed_irx` entry, update the compiled-dependency table here.
6. Build the docs site and run CI before publishing. Retain the hardware-versus-CI distinction in user-facing release notes.

## Further graphics and bootloader references

- [libpng](http://www.libpng.org/pub/png/libpng.html) and [Independent JPEG Group / libjpeg](https://www.ijg.org/): image decoding and library interfaces used by `src/dash_png.c` and `src/dash_jpeg.c`. Preserve the notices applicable to linked versions.
- [PS2BBL](https://github.com/israpps/PlayStation2-Basic-BootLoader) by [El_isra](https://github.com/israpps) and [PS2BBLE](https://github.com/saildot4k/PlayStation2-Basic-BootLoader-Extended) by [R3Z3N](https://github.com/saildot4k): separate bootloader projects referenced within PS2 homebrew history, not the Launchstation exploit installer.
- LoadBOOTer: named memory-card exploit installer option in `src/main_exploit_installer.c`. Exact historical source and authorship still require verification; do not attribute it to PS2BBL without evidence.
- ESR, FreeMcBoot and PSBBN: compatibility targets, boot environments or platform software. References do not establish redistribution of their binaries.
