# Runtime resources

Files in this directory are either embedded into the ELF at build time,
packaged beside it in the release ZIP/PSU, or provided as templates.

| Path | Used by | Notes |
| --- | --- | --- |
| `sfx/*.adp` | Embedded by `src/embed_blobs.S` | Interface sound effects (cursor, confirm, cancel, transition, message, boot, `bd_connect`, `bd_disconnect`), played through the SDK `audio` driver and `audsrv-compat` client |
| `WLE-UPDATE-CA.PEM` | Packaged by `scripts/package_apps.py`; read by the update check | Public roots for GitHub HTTPS: Sectigo Public Server Authentication Root R46 and ISRG Root X1. `package_apps.py` refuses a bundle with fewer than two certificates. Copy it beside the ELF for standalone installs |
| `music.sad` | Packaged as `BGM.ADP` by `scripts/package_apps.py` | Mono PS-ADPCM (`APCM` header, at most 1 MiB) background music; **not** embedded in the ELF. Copy it unchanged as `BGM.ADP` beside the ELF if you want music |
| `WLE-CLOUD.CNF.example` | Template for the optional custom Google OAuth client | Copy to `WLE-CLOUD.CNF` beside `DASHBOARD.CNF` only if you use your own client; official builds do not need it. Never add tokens to it |
| `icons/*.png` | Reference artwork for controller button glyphs | Not embedded and not packaged (the dashboard draws its pad glyphs from its own atlas) |
| `bgm.ogg` | Source material | Not referenced by any build script and not packaged |

The origin and license of the audio and icon artwork are not recorded in this
repository; see [docs/THIRD_PARTY_CREDITS.md](../docs/THIRD_PARTY_CREDITS.md).
