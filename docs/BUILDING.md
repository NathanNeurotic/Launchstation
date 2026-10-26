# Building from Source

Launchstation is built with the PS2BUILD SDK and its LLVM/Clang toolchain. This guide reproduces what `.github/workflows/compile.yml` does, step by step, so a local build matches a release build.

**Short version**

```
# one time: install ps2build, then fetch the SDK and LLVM toolchain
export PATH="$HOME/.local/share/ps2build:$PATH"
ps2build update
ps2build update llvm-toolchain

# every checkout (and again after changing toolchain)
bash scripts/build_cloud_tls.sh
bash scripts/build_jpeg.sh
python3 scripts/gen_cloud_client.py
python3 scripts/gen_githash.py
ps2build build
```

The release ELF is `build/bin/wLaunchELF-packed.elf`. The legacy `Makefile` is not the release path and does not include native cloud support.

## Requirements

- **Host:** Linux (what CI uses). The helper scripts also contain MSYS/Git-Bash path handling for Windows, but only Linux is exercised by CI.
- **Tools on `PATH`:** `bash`, `curl`, `tar` with bzip2 and gzip, `sha256sum`, `python3` 3.11 or newer (only the standard library is used; `release_notes.py` needs `hashlib.file_digest`), `git` and **CMake** (the dependency scripts call the CMake on your `PATH`; PS2BUILD only supplies toolchain files). A host `gcc` or `clang` is needed to run the test suite.
- **Network:** the first run downloads the SDK, the LLVM toolchain, Mbed TLS and libjpeg-turbo. The two dependency tarballs are SHA-256 pinned in their scripts and cached under `.cache/`.
- **Optional:** Pillow, only for `scripts/gen_dash_font.py` and `scripts/prepare_psbbn_theme.py`.

## 1. Install the PS2BUILD SDK

```
curl -sSfL --retry 8 --retry-delay 5 --retry-all-errors --connect-timeout 20 \
  https://git.techwritescode.dev/ps2/install/raw/branch/master/install.sh -o install.sh
sh install.sh
export PATH="$HOME/.local/share/ps2build:$PATH"
```

The installer puts the `ps2build` executable directly in `~/.local/share/ps2build` (not in a `bin/` subfolder) and adds that directory to `PATH`. The download host occasionally returns transient 5xx errors, which is why CI retries and does not pipe `curl` into `sh`. Then bring every component to its latest release and make sure the LLVM toolchain is present:

```
ps2build update                  # toolchain, ps2build, sdk-core, sdk-world
ps2build update llvm-toolchain   # only if the Clang config below is missing
test -f ~/.local/share/ps2build/llvm-toolchain/ee/bin/mips64r5900el-ps2-elf-clang.cfg
ps2build version                 # prints ps2build and each SDK component version
```

CI always tracks the latest SDK; a local SDK that is older can fail to build. `ps2build update --check` only reports installed versus available versions. The dependency scripts look for the SDK in `$PS2BUILD_ROOT` (default `~/.local/share/ps2build`).

## 2. Toolchain selection

`ps2.yaml` contains `toolchain: llvm`, so the EE and IOP code is compiled with Clang and linked with LLD against the SDK's `packages-llvm` libraries. Precedence is the `--toolchain` flag, then `PS2BUILD_TOOLCHAIN`, then `toolchain:` in the project file, then `gcc`. Every static library linked into a Clang ELF must itself be Clang-built, which is why the next three steps exist.

## 3. Third-party libraries and generated headers

```
bash scripts/build_cloud_tls.sh
bash scripts/build_jpeg.sh
python3 scripts/gen_cloud_client.py
```

| Script | What it produces |
| --- | --- |
| `build_cloud_tls.sh` | Downloads Mbed TLS 3.6.7 (SHA-256 checked), disables the Unix timing and net modules, supplies the millisecond clock and hardware-entropy hook from `src/cloud_http.c`, and builds `libmbedtls.a`, `libmbedx509.a` and `libmbedcrypto.a` with the active toolchain into `.cloud-deps/` (untracked). |
| `build_jpeg.sh` | Builds libjpeg-turbo 3.1.3 (8-bit, arithmetic coding, memory source/destination, no SIMD) into `.jpeg-deps/` and installs it as `libjpegturbo.a`. The SDK's LLVM-tier `jpeg` archive is GCC-built and is rejected by `ld.lld` (*symbol has invalid binding*), so `ps2.yaml` links `-ljpegturbo` instead. |
| `gen_cloud_client.py` | Writes `.cloud-deps/include/wle_cloud_client.h`. If both `WLE_GOOGLE_CLIENT_ID` and `WLE_GOOGLE_CLIENT_SECRET` are set (see [below](https://nathanneurotic.github.io/Launchstation/developers.html#cloud-client)) they are embedded as the built-in Google OAuth client; if neither is set the header defines nothing and the build has no built-in client. Setting only one, or an ID that does not end in `.apps.googleusercontent.com`, is an error. |

Both dependency scripts follow the same toolchain precedence as `ps2build` (they source `scripts/ee_toolchain.sh`) and record the toolchain in a `TOOLCHAIN` file inside their output folder. Re-run them whenever you switch toolchains or update the SDK. If `.cloud-deps/` or `.jpeg-deps/` is missing, the compile fails on missing headers.

## 4. PC CARD CompactFlash driver check

```
ps2build build -c iop/pccf/ps2.yaml
```

Builds the isolated `pccf` IOP module on its own, the way CI does, to prove the vendored driver still compiles. The main `ps2.yaml` also builds and embeds it. Its host-side tests are `tests/test_pccf.py`, `test_pccf_owner.py` and `test_pccf_fs.py`.

## 5. Build Launchstation

```
python3 scripts/gen_githash.py --require-git
ps2build build
```

`gen_githash.py` writes `githash.h` (untracked) with the 12-character commit and commit date shown in **Build information** and checked by the updater. `--require-git` makes it fail outside a Git checkout, as CI does; without it, a tree that is not a Git checkout is stamped `local`/`unknown`, and the updater cannot compare such a build with a release. `ps2build build` then reads `ps2.yaml`, builds the IOP modules and EE libraries, the embedded loader, the `wle_igr` helper and the main `wLaunchELF` target, and writes the output to `build/`:

- `build/bin/wLaunchELF-packed.elf` is the release ELF (compressed with `ps2pack`).
- `build/bin/wLaunchELF.elf` is the stripped, unpacked ELF; `build/obj/wLaunchELF/wLaunchELF.unstripped.elf` is the linked ELF with symbols used by the checks below.
- `build/bin/*.irx` are the source-built IOP modules (`udpfs_smap`, `usbd_ra`, `mmceman`, `vmcman`, `ps2ftpd`, `ps2host`, `pccf`, `ata_bd` and others); `loader.elf` and `wle_igr.elf` are also embedded.

At the time of writing the packed ELF is about 1.6 MB (unpacked about 4.6 MB). A change to the default feature profile belongs in `ps2.yaml` (`defines:`, `embed_irx:`, `libs:`); the standard profile is MMCE, MX4SIO, iLink, exFAT, UDPFS plus ETH, DVRP, XFROM, TMANIP and custom colors with native cloud enabled and DS34 off.

## 6. DualShock (DS34) variant

```
python3 scripts/configure_ds34.py
ps2build build -c .ps2-ds34.yaml -o build-ds34
```

`configure_ds34.py` derives `.ps2-ds34.yaml` (untracked) from `ps2.yaml` by swapping `src/pad.c` for `src/pad_ds34.c`, adding the `ds34usb`/`ds34bt` IOP modules and EE libraries and the `DS34` define, and fails if any anchor in `ps2.yaml` changed shape. Keep the generated file at the repository root so relative paths still resolve. The result is `build-ds34/bin/wLaunchELF-packed.elf`. The DS34 build identifies itself as `LAUNCHSTATION-DS.ELF` in `APP_LAUNCHSTATION-DS` for the updater.

## 7. Verification scripts

Set `LLVM_BIN` to the toolchain's LLVM tools, then run the same checks as CI against the *unstripped* ELFs:

```
export LLVM_BIN="$HOME/.local/share/ps2build/llvm-toolchain/llvm/bin"

# DS34 symbols must be absent from the standard ELF and present in the DS34 ELF
python3 scripts/check_ds34_build.py "$LLVM_BIN/llvm-nm" \
  build/obj/wLaunchELF/wLaunchELF.unstripped.elf \
  build-ds34/obj/wLaunchELF/wLaunchELF.unstripped.elf

# every producer in .comment must be Clang; any "GCC:" entry fails
python3 scripts/check_toolchain_purity.py "$LLVM_BIN/llvm-readobj" \
  build/obj/wLaunchELF/wLaunchELF.unstripped.elf \
  build-ds34/obj/wLaunchELF/wLaunchELF.unstripped.elf \
  build/obj/loader/loader.unstripped.elf \
  build/obj/wle_igr/wle_igr.unstripped.elf \
  build/obj/usbd_ra/usbd_ra.notiopmod.elf \
  build/obj/mmceman/mmceman.notiopmod.elf

# source-built UDPFS SMAP is embedded and still matches the prebuilt peer ABI
python3 scripts/check_udpfs_smap.py build build-ds34

# the embedded loader must fit the 0x84000..0xa0000 low-memory window
sh scripts/check_loader_layout.sh build/obj/loader/loader.unstripped.elf \
  "$LLVM_BIN/llvm-nm" 0x84000 auto auto 0xa0000
```

The layout script arguments are `<loader.elf> <nm-tool> <loadaddr> <stackaddr|auto> <stacksize|auto> <low_mem_limit>`. CI also prints Mbed TLS archive sizes, the `llvm-size` sections and the 40 largest symbols so footprint work targets measured contributors.

## 8. Package the app archives

```
python3 scripts/package_apps.py \
  --standard build/bin/wLaunchELF-packed.elf \
  --dualshock build-ds34/bin/wLaunchELF-packed.elf \
  --output artifacts \
  --revision "$(git rev-parse HEAD)" \
  --epoch "$(git show -s --format=%ct HEAD)"
python3 tests/test_app_packages.py
```

This writes `APP_LAUNCHSTATION.psu/.zip` and `APP_LAUNCHSTATION-DS.psu/.zip`. Each contains the ELF, a generated `title.cfg`, `WLE-UPDATE-CA.PEM` and `BGM.ADP` (from `res/music.sad`), and the script re-reads every archive to confirm that the ZIP and PSU payloads are identical. `--revision` must be lowercase hexadecimal and `--epoch` must fall in 2000-2099 (PS2 timestamps). `scripts/copy_test_pack.py --output test-fixtures/LS-COPY-TEST.zip` creates the deterministic copy-test fixture CI uploads for hardware testers.

## 9. Run the tests

```
python3 tests/test_self_update.py        # one test
for t in tests/test_*.py; do python3 "$t" || echo "FAIL $t"; done
```

The tests are plain Python scripts that exit non-zero on failure. Many compile production C sources against stubs with the host `gcc` or `clang` and exercise them; others assert on build definitions, workflows and documentation. They need no PS2 tools but must run inside a Git checkout (`test_build_identity.py` and `test_release_notes.py` call `git`). CI runs an explicit list, so a new test must be added to the *Test Settings sessions, navigation and theme compatibility* step of `compile.yml` to count. Hardware timing, USB behavior and console quirks are not covered by any of them. See [Contributing & Testing](https://nathanneurotic.github.io/Launchstation/contributing.html).

## 10. GCC comparison builds

```
export PS2BUILD_TOOLCHAIN=gcc
bash scripts/build_cloud_tls.sh
bash scripts/build_jpeg.sh
python3 scripts/gen_cloud_client.py
ps2build build -o build-gcc
```

Use a separate `-o` directory. The dependency scripts overwrite `.cloud-deps/` and `.jpeg-deps/`, so unset the variable and re-run them before your next LLVM build. GCC output is for comparison only; the purity check does not apply to it and release packages are Clang-built.

## 11. Legacy Makefile

The root `Makefile` and `embed.make` are kept for feature profiles outside `ps2.yaml` and for the older toolchain. They need `PS2DEV`/`PS2SDK` in the environment, `ps2-packer` and `bin2s` (or the fallback scripts in `scripts/`), and produce `BOOT-*.ELF` (packed) and `UNC-BOOT-*.ELF` names derived from the options. `make info` lists the options (`EXFAT`, `ETH`, `UDPFS`, `DS34`, `MX4SIO`, `MMCE`, `DEBUG`, `PPC_UART`, and so on). Local `make` defaults to `ETH=0 UDPFS=1`, whereas `ps2.yaml` enables both. The Makefile does **not** define `WLE_NATIVE_CLOUD` or link Mbed TLS, so Google Drive and the native updater are compiled out ("This build does not include native HTTPS support"). It is not run in CI; treat it as best effort.

## 12. Documentation site and wiki

```
python3 docs-site/build.py --out build/docs-site
python3 docs-site/build.py --out site --zip Launchstation-Documentation.zip
```

`build.py` renders every page in `docs-site/manifest.json` from `docs-site/content/<slug>.html`, builds the search index and fails on missing pages, broken internal links, unresolved template tokens, missing images and stale upstream branding outside Credits. CI builds it on every run; the *Publish Documentation* workflow pushes it to `gh-pages` when `docs-site/**` changes on `master`. *Synchronize GitHub Wiki* runs `scripts/sync_github_wiki.py` to mirror `docs/wiki/*.md`, the site pages and `docs/THIRD_PARTY_CREDITS.md` into the repository wiki without deleting unmanaged pages or force-pushing.

## 13. Claude Code cloud sessions

The repository ships `.claude/settings.json` with a `SessionStart` hook that runs `.claude/hooks/session-start.sh`. In a Claude Code cloud session (`CLAUDE_CODE_REMOTE=true`; it does nothing elsewhere) the hook installs the PS2BUILD SDK with the same installer and retry options as CI if `ps2build` is missing, runs `ps2build update` and `ps2build update llvm-toolchain`, and exports `PATH`, `LLVM_BIN` and `PS2BUILD_TOOLCHAIN=llvm` for the session through `CLAUDE_ENV_FILE`. It honors `PS2DEV_INSTALL_DIR` as the install location. After it runs, the commands in sections 3 to 9 work unchanged.

## Troubleshooting a build

- **`ps2build: command not found`:** add `~/.local/share/ps2build` to `PATH`.
- **`PS2 EE clang config not found`:** run `ps2build update llvm-toolchain`.
- **Missing `mbedtls/ssl.h` or `jpeglib.h`:** run the two dependency scripts first.
- **`symbol has invalid binding` from `ld.lld`, or a purity failure listing `GCC:`:** a GCC-built archive was linked; rebuild the dependencies with the same toolchain, and do not add `jpeg` or the SDK's `audsrv` package to `ps2.yaml` (`tests/test_llvm_toolchain.py` guards this).
- **`Expected a Git revision` / `outside the PS2 timestamp range`:** pass a lowercase hex `--revision` and a Unix `--epoch` in 2000-2099 to `package_apps.py`.
- **Update check says the build cannot be compared:** it was stamped `local`; rebuild with `--require-git` from a real commit.

See also: [architecture-maintenance.md](architecture-maintenance.md), [NATIVE_CLOUD.md](NATIVE_CLOUD.md) and [THIRD_PARTY_CREDITS.md](THIRD_PARTY_CREDITS.md). This file is generated from `docs-site/content/building.html`; keep the two in sync.
