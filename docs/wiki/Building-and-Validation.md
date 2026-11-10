# Building and validation

Launchstation is built and validated with the **PS2BUILD SDK by [TechWritesCode](https://github.com/techwritescode)**. It provides our EE/IOP toolchains, SDK packages and `ps2.yaml` build workflow, locally and in CI. Start with the [official PS2BUILD site and guide](https://ps2build.dev/).

Install the components pinned in `.github/ps2build-sdk.env`, then build both controller variants:

```sh
export PS2BUILD_ROOT="$HOME/.local/share/ps2build"
sh scripts/ps2build_sdk.sh --llvm "$PS2BUILD_ROOT"
export PATH="$PS2BUILD_ROOT:$PATH"
export PS2BUILD_TOOLCHAIN=llvm
ps2build build
python scripts/configure_ds34.py
ps2build build -c .ps2-ds34.yaml -o build-ds34
```

The standard packed ELF is `build/bin/wLaunchELF-packed.elf`; the optional DualShock variant uses `build-ds34/bin/`. CI uses this same pinned SDK for both variants. The retained Makefile describes historical profiles; the supported workflow is `ps2.yaml` with PS2BUILD.

SDK component names and original license notices remain attributed to their authors. References to upstream PS2SDK, ps2dev or ps2homebrew describe that ancestry, rather than an alternate SDK installation requirement.

Host regression tests, successful target builds and CI packages are separate from physical-console acceptance. Validate PAL/NTSC timing, device transitions and game-core handoffs on the relevant console hardware.
