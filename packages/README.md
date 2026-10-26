# Retained copies of ps2build-style packages

This directory keeps one package-layout folder (`world/audsrv/` with
`package.yaml`, `include/`, `lib/` and `bin/`) that the release build does
**not** take from the PS2BUILD SDK. Do not copy these files over a current SDK
installation.

| Package | Origin and license | Who uses it |
| --- | --- | --- |
| `world/audsrv` | ps2sdk audio server, **LGPL-2.0** (`package.yaml`; the sources in `ee_libs/audsrv/` carry the GNU Library General Public License version 2 notice) | **Release build:** only `bin/audsrv.irx`, embedded unchanged by `src/embed_blobs.S` (`EMBED_FILE(audsrv_irx, "packages/world/audsrv/bin/audsrv.irx")`; `tests/test_llvm_toolchain.py` guards the path). `ps2.yaml` does not use the package's `lib/libaudsrv.a` or `include/`: the EE RPC client is built from `ee_libs/audsrv/` by the selected toolchain (GCC or LLVM), because a prebuilt archive must match the compiler of the ELF that links it. **Legacy Makefile:** also links `lib/libaudsrv.a` and `include/audsrv.h` from here. |

Notes:

- The IOP `audsrv.irx` is compiler-independent. The SDK world set installed by
  `ps2build update` at the time of this review (`sdk-world` v2026.10.13.1) no
  longer ships an `audsrv` package (it provides `audio` and `audsrv-compat`
  instead), so this copy is the
  only source of the audio-server module. (The unused `ogg`/`vorbis` copies that
  used to live here were removed on 2026-10-26.) Keep `audsrv.irx` and its LGPL-2.0
  notice together with any redistribution of the ELF, and update
  [docs/THIRD_PARTY_CREDITS.md](../docs/THIRD_PARTY_CREDITS.md) if this changes.
- The SDK's `audio` package is the intended replacement. Moving `src/dash_sound.c`
  from `audsrv` to `audio` would let `ee_libs/audsrv/`, the embedded `audsrv.irx`
  and this whole directory be deleted. That migration has not been done.
- The repository's CI does not install, update or modify anything in this
  directory.
