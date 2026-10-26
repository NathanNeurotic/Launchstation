# Launchstation Maintainer Notes

This project is organized around two maintenance goals:

1. Keep runtime behavior stable on real PS2 hardware.
2. Make feature combinations predictable in CI and local builds.

See [BUILDING.md](BUILDING.md) for the step-by-step build, the
[repository map](#repository-map) below for where things live, and the website
pages *Architecture & Repository Layout* and *Contributing & Testing* for the
user-facing versions of these notes.

## Build systems

| Build | Definition | Status |
| --- | --- | --- |
| **PS2BUILD (release)** | `ps2.yaml`; `iop/pccf/ps2.yaml` for the isolated CF driver | What CI builds and publishes. LLVM/Clang toolchain (`toolchain: llvm`). Standard variant plus a generated DualShock variant. |
| **DualShock (DS34)** | `.ps2-ds34.yaml`, generated from `ps2.yaml` by `scripts/configure_ds34.py` | Built by CI as `build-ds34/`; shares every source and define except the pad driver. |

The legacy `Makefile`/`embed.make` build was retired on 2026-10-26; `PS2BUILD_TOOLCHAIN=gcc` gives a GCC comparison build of the same `ps2.yaml`.

The release profile in `ps2.yaml` is: MMCE, MX4SIO, iLink, exFAT (BDM), UDPFS and
ETH, DVRP, XFROM, TMANIP, custom colors, homebrew SIO2MAN, latest `cdfs` CD/DVD
module, native cloud; DS34 off. Both network stacks are embedded, and the app
loads exactly one at a time (switching resets the IOP). Embedding a module does
not load it; `src/init.c` decides when.

Changing a feature set means editing `defines:`, `libs:` and `embed_irx:` for the
`wLaunchELF` target in `ps2.yaml`, then re-running the host contract tests
(`tests/test_llvm_toolchain.py` protects the LLVM invariants: no GCC-built
archives, SDK `audio`/`audsrv-compat` for sound, `libjpegturbo`, loader flags).

## CI

Workflows in `.github/workflows/`:

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `compile.yml` (*Automated-Build*) | push to `master`/`main`, pull requests, manual | Explicit list of host regression tests; docs-site build; PS2BUILD SDK install and LLVM toolchain check; Mbed TLS, libjpeg-turbo, Google client header and isolated CF driver builds; standard and DS34 builds; `check_ds34_build.py`, `check_toolchain_purity.py`, `check_udpfs_smap.py`, `check_loader_layout.sh`; `package_apps.py` and package tests; artifact upload; on `master` only, verified publication of the four packages to the rolling `latest` release |
| `docs-pages.yml` | push to `master`/`main` touching `docs-site/**` | Builds the site and offline ZIP and force-publishes the generated `gh-pages` branch |
| `sync-wiki.yml` | push to `master` touching `docs/wiki/**`, `docs-site/content/**`, the manifest or `THIRD_PARTY_CREDITS.md` | Mirrors those into the repository wiki with `scripts/sync_github_wiki.py` (never deletes unmanaged pages, no force-push) |

A test only gates merges if it is listed in the first step of `compile.yml`;
a handful of older test scripts under `tests/` are not currently listed, so run
the whole directory locally before large changes. Two tests
(`test_build_identity.py`, `test_release_notes.py`) require a Git checkout.

Repository secrets used: `WLE_GOOGLE_CLIENT_ID` and `WLE_GOOGLE_CLIENT_SECRET`
(embedded into official builds by `scripts/gen_cloud_client.py`) and the
built-in `GITHUB_TOKEN`.

## Repository map

| Path | Purpose |
| --- | --- |
| `src/`, `include/` | EE application (dashboard, File Browser, backup/recovery, cloud, updater, handoffs, PC protocols) |
| `iop/` | IOP modules: source-built targets from `ps2.yaml`, prebuilt UDPFS blobs in `__precompiled/` and the `__v10/` DEV9/PS2IP pair, `oldlibs/` (ps2ftpd) |
| `ee_libs/` | EE libraries built from vendored source (`hdd`, `iopreboot`) |
| `loader/`, `igr/` | Embedded low-memory ELF loader; WLE-IGR OPL trampoline |
| `exploits/`, `res/`, `Lang/` | Embedded exploit-installer payloads; sounds, CA bundle, icons, BGM, cloud template; language tables |
| `scripts/`, `tests/` | Build helpers and CI checks; host regression tests |
| `docs/`, `docs-site/` | Maintainer notes and wiki guides; website source |
| `windows/cloud-relay/` | Optional PC-side rclone relay |
| `research/` | Non-shipping research code (sd2psXtd restore model) |

## Stale Code Audit

Script: `scripts/stale_code_audit.sh`

Current checks:

- stale markers (`TODO`/`FIXME`/`HACK`, legacy aliases)
- disabled `#if 0` blocks
- duplicate config-probe hotspots
- largest source files (refactor candidates)

Use:

- `sh scripts/stale_code_audit.sh` (needs `rg`; writes `build/stale-code-report.txt`)

CI does not run this; it is a local/manual maintenance aid only.

## Refactoring history and next splits

Most of the code is modularized. The largest hand-written sources today are
`src/dash_settings.c`, `src/dash_browser.c`, `src/init.c`,
`src/main_exploit_installer.c` and `src/filer_browser.c`
(`src/dash_font_data.c` is generated data).

Completed so far:

1. `src/main_fileops.c` owns file/path support helpers (`wleExists`, `uLE_related`, file-type checks).
2. `src/main_startup.c` owns startup/system parsing helpers (`getIpConfig`, `readSystemCnf`).
3. Legacy SMB and the old classic JPG-viewer subsystem were removed from the runtime/build paths. Image viewing has since returned as a native dashboard viewer: PNG uses `src/dash_png.c`, while JPG/JPEG uses the bounded `src/dash_jpeg.c` libjpeg-turbo path.
4. `src/filer.c` helper splits now include:
   - `src/filer_device.c`, `src/filer_mount.c`, `src/filer_fileops.c`, `src/filer_actions.c`, `src/filer_browser.c`, `src/filer_copy.c`
   - `src/gui_sort.c` (`cmpFile`, `sort`)
   - `src/gui_texteditor.c` (`canOpenInTextEditor`)
   - `src/gui_hdd0_format.c` (`getHddParty`, `getHddDVRPParty`)
   - `src/psu_functions.c` (`clear_psu_header`, `pad_psu_header`)
   - `src/gui.c` (`ynDialog`, `nonDialog`, `keyboard`)
   - `src/gui_colors.c` (`scanSkinCNF`, `storeSkinCNF`)
5. PSU data layout structs are centralized in `include/psu_types.h` to avoid duplicated type blocks.
6. `src/main.c` helper splits now include:
   - `src/main_info_screens.c` (`Show_About_uLE`, `Show_build_info`, `ShowDebugInfo`, `ShowFont`)
   - `src/main_gameid.c`, `src/main_gameid_table.c` (RetroGem/game-ID helpers)
   - `src/main_actions.c` (`Execute` action dispatch + launch cleanup)
   - `src/main_history.c`, `src/main_title.c`, `src/main_console_info.c`
7. Main startup/menu flow splits now include:
   - `src/main_boot.c` (early boot sequence + argument capture)
   - `src/main_modules.c` (runtime module/font/pad startup + CNF path validation)
   - `src/main_menu.c` (main menu rendering/input/timer state helpers)
8. The dashboard is split into `dash_*` units (`dash_home`, `dash_apps`, `dash_files`, `dash_tools`, `dash_settings`, `dash_theme`, `dash_worker`, and so on); `dash_main.c` owns the event loop and the PC-command polling.

Candidate next extractions (lowest risk first), each behavior-neutral:

1. Settings page tables and value handlers out of `dash_settings.c`.
2. Driver-stack families out of `init.c` (storage, network, PSX HDD).
3. Exploit-installer menu flow out of `main_exploit_installer.c`.

Verify each split on both an emulator and real hardware, and keep the host contract tests green.

## Third-party and attribution upkeep

When `ps2.yaml` gains a library, `embed_irx` entry or vendored directory, update
[THIRD_PARTY_CREDITS.md](THIRD_PARTY_CREDITS.md) and the website Credits page in
the same change. Vendored source needs an `ORIGIN.md`/`VENDORED.md` with
upstream, base commit, license and local changes. There is no root `LICENSE`;
see the license-risk note in the inventory.
