# Legacy copies of ps2build packages

The current ps2build SDK supplies `vorbis` and `ogg` in its world
package set. `audsrv` has a separate release schedule and may need to
be installed separately. Use `ps2build update` and `ps2build build`;
do not copy these files over a current SDK installation. The project
references the package names in `ps2.yaml` (`libs:`, `headers:`,
`embed_irx:`). CI installs the in-repo `audsrv` copy only when the
SDK does not already contain it.

This directory retains copies for the legacy Makefile, which reads
them directly:

- **audsrv** (ps2sdk, LGPL-2.0) — audio server EE lib + IOP driver.
- **vorbis** (libvorbis port, BSD-3-Clause) — `libvorbis.a` + `libvorbisfile.a`.
- **ogg** (libogg port, BSD-3-Clause) — `libogg.a`.

The repository's CI uses the latest ps2build suite packages for `ogg`
and `vorbis` and preserves an existing `audsrv` package.
