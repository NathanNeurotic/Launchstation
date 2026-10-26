# Legacy copies of ps2build packages

The current ps2build SDK supplies `vorbis` and `ogg` in its world
package set. Do not copy these files over a current SDK installation.

`ps2.yaml` does not use the `audsrv` package: its EE client is built from
`ee_libs/audsrv/` by the selected toolchain (GCC or LLVM), and
`audsrv/bin/audsrv.irx` here is embedded unchanged by `src/embed_blobs.S`.
The IRX is compiler-independent and byte-identical to the PS2BUILD package
driver.

This directory retains copies for the legacy Makefile, which reads
them directly:

- **audsrv** (ps2sdk, LGPL-2.0) — audio server EE lib + IOP driver.
- **vorbis** (libvorbis port, BSD-3-Clause) — `libvorbis.a` + `libvorbisfile.a`.
- **ogg** (libogg port, BSD-3-Clause) — `libogg.a`.

The repository's CI uses the latest ps2build suite packages for `ogg`
and `vorbis` and preserves an existing `audsrv` package.
