# Custom ps2build packages used by this project

`packages/world/<name>/` follows the ps2build custom-package layout
(see https://ps2.techwritescode.dev/guide/custom-packages): a
`package.yaml` next to the built artifacts. The build references these
by name in `ps2.yaml` (`libs:` / `headers:` / `embed_irx:`), so the
suite must see them before generating:

- **audsrv** (ps2sdk, LGPL-2.0) — audio server EE lib + IOP driver.
- **vorbis** (libvorbis port, BSD-3-Clause) — `libvorbis.a` + `libvorbisfile.a`.
- **ogg** (libogg port, BSD-3-Clause) — `libogg.a`.

ps2build resolves packages from its own suite mirror
(`%LOCALAPPDATA%\ps2build\packages\world\` on Windows,
`~/.local/share/ps2build/packages/world/` on Linux), so install once per
machine (CI does this automatically):

    cp -r packages/world/. "$HOME/.local/share/ps2build/packages/world/"

and re-run `ps2build generate`. The legacy Makefile reads the copies
straight from this in-repo directory instead.
