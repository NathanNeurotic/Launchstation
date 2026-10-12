# Third-Party Credits and License Inventory

> Reviewed against the repository source and documentation as of 2026-10-11. This is an attribution/provenance inventory, **not** a replacement for individual licenses or a claim that every dependency is packaged into the release.

## Lineage and contributors

- **[wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z)** — direct upstream foundation, maintained by R3Z3N / Saildot4K. Earlier wLaunchELF, uLaunchELF and LaunchELF contributors retain attribution.
- **[johnson-cooper](https://github.com/johnson-cooper)** — created this fork and handed its maintenance to NathanNeurotic / Ripto.
- **[NathanNeurotic / Ripto](https://github.com/NathanNeurotic)** — current maintainer.
- **[techwritescode](https://github.com/techwritescode)** — development collaboration; **TnA** — insight and support; **oMrRexD** — remote/FTP PC-side ecosystem reference and collaboration; **Zack and FitthFox** — dashboard and file browser testing and UX feedback.
- **[ps2dev community](https://github.com/ps2dev)** — toolchain, driver, graphics and filesystem foundations. Additional contributors retain credit through original source comments, Git history, and upstream notices.

## Checked-in source, vendored modules and build dependencies

| Project or contributor | Evidence in this repository | Upstream / license information | Role |
| --- | --- | --- | --- |
| PS2SDK / PS2Dev | `iop/vmcman/`, `iop/ps2hdd_osd/` and EE/IOP build inputs | [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk); inspect the specific upstream license and local source headers | Local variants and build dependency |
| MMCEMAN / MMCEDRV | `iop/mmceman/`, `iop/__precompiled/mmceman.irx` | [ps2-mmce/mmceman](https://github.com/ps2-mmce/mmceman) (MIT in upstream repo); upstream acknowledges Wisi and Maximus32 | MMCE filesystem, protocol and GameID foundations |
| RiptOPL USB driver | `iop/usbd-ra/` | [NathanNeurotic/Open-PS2-Loader](https://github.com/NathanNeurotic/Open-PS2-Loader); source review needed for inherited notices | Updated usbd-ra imported in USB stability work |
| PS2FTPD | `iop/oldlibs/ps2ftpd/` | [ps2homebrew/ps2ftpd](https://github.com/ps2homebrew/ps2ftpd), Jesper Svennevid; [bundled AFL 2.0](../iop/oldlibs/ps2ftpd/LICENSE) | Legacy local FTP source |
| libcdvd | `iop/oldlibs/libcdvd/` | A. Lee and Nicholas Van Veen; [bundled restrictive historical license](../iop/oldlibs/libcdvd/license.txt), expressly limiting commercial use | Legacy checked-in CD/DVD library; inclusion in a particular release must be confirmed from its build graph |
| DS34 USB/Bluetooth | `iop/ds34usb/`, `iop/ds34bt/`, `src/pad_ds34.c` | inherited from [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z); thanks to Alex Parrado and upstream authors | Optional -DS controller build |
| audsrv | `packages/world/audsrv/` | [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk); documented in [packages/README.md](../packages/README.md) as LGPL-2.0 | Legacy/local audio package and build input |
| libogg / libvorbis | `packages/world/ogg/` and `packages/world/vorbis/` | [Xiph.org](https://xiph.org/), BSD-style licenses per [packages/README.md](../packages/README.md) | Retained codec libraries; CI can use SDK copies |
| Mbed TLS | `scripts/build_cloud_tls.sh`; linked native HTTPS in `src/cloud_http.c` | [Mbed TLS](https://github.com/Mbed-TLS/mbedtls), Apache-2.0 per [docs/NATIVE_CLOUD.md](NATIVE_CLOUD.md) | External TLS source built by the cloud build |
| Roboto | `docs/licenses/Roboto-COPYRIGHT.txt` | [Roboto](https://fonts.google.com/specimen/Roboto); [bundled notice](licenses/Roboto-COPYRIGHT.txt) | Font/copyright attribution |
| libpng / libjpeg | `src/dash_png.c`, `src/dash_jpeg.c` | [libpng](http://www.libpng.org/pub/png/libpng.html), [Independent JPEG Group](https://www.ijg.org/) and active SDK/port terms | Image decoding sources and interfaces; inspect exact linked versions |

**License risk:** There is **no root `LICENSE` file in this repository snapshot**. Do not infer uniform relicensing of inherited code or a single license that overrides component terms. In particular, the checked-in `libcdvd` license includes non-commercial-use language. Preserve upstream copyright/license headers in source and release documentation. Confirm which legacy subdirectories are actually embedded/shipped per build variant before declaring a complete binary distribution notice set.

## Independently installed applications and services

These are mentioned or launched through an integration; that does **not** imply their binaries or licenses are included:

| Project | Why LAUNCHSTATION references it |
| --- | --- |
| [Neutrino](https://github.com/rickgaiser/neutrino) — Rick Gaiser / contributors | External PS2 ISO loader, quick-boot/backing-store backend |
| [POPStarter](https://www.psx-place.com/threads/popstarter.19139/) — krHACKen | External PS1 VCD launcher |
| [Ember](https://github.com/Gageformer/Ember) — Gageformer | External PS1 core; its beta license limits redistribution/commercial use. See upstream LICENSE-BETA.txt |
| [PS2Link](https://github.com/ps2dev/ps2link), [PS2Client](https://github.com/ps2dev/ps2client) | Network debugger and PC-side transfer/launch ecosystem |
| [UDPFSD](https://github.com/pcm720/udpfsd) — pcm720 | PC-side network filesystem server |
| [OSDMenu](https://github.com/pcm720/OSDMenu) — pcm720 | KELF launcher and HDD-OSD/PSX references |
| [KELFTool](https://github.com/xfwcfw/kelftool) — xfwcfw | External PS2/PSX KELF handling tool |
| [HDD-OSD Icon Generator](https://github.com/CosmicScale/HDD-OSD-Icon-Generator) — CosmicScale | External HDD partition art generator |
| [PixelFX RetroGem](https://www.pixelfx.co/hdmi-retro-gem) | External video hardware and Game ID compatibility |
| [Google Drive API](https://developers.google.com/workspace/drive/api/guides/about-sdk) / [Google PKI](https://pki.goog/roots.pem) | Public OAuth/Drive and CA resources for original cloud implementation |
| [SD2Cloud/MMCE ecosystem](https://github.com/ps2-mmce) | Comparison/integration context; **no SD2Cloud source was incorporated** |
| oMrRexD / R3Z PC command environment | Reference for compatible remote command/transfer behavior |

## Maintenance checklist

1. Keep the user's documentation in `docs-site/content/`, mirrored to relevant `docs/wiki/` guides where useful. Do not patch generated `gh-pages` HTML.
2. For each new external reference, add its proper name and canonical source to the website credits and this inventory; specify **vendored code**, **compiled dependency**, **external executable**, **external service** or **reference only**.
3. For new vendored source, record upstream repository, base revision, component path, license and any local modifications.
4. Check linked binary/package provenance independently. Do not assert a license for the whole tree based on a single checked-in notice.
5. Build the docs site and run CI before publishing. Retain the hardware-versus-CI distinction in user-facing release notes.
