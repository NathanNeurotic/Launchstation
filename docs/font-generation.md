# Dashboard font generation

Settings > Appearance > Customize theme > Text > Font offers Roboto, System,
and Emotion Engine. Roboto remains the default. **Style** selects Regular or
Bold. Emotion Engine uses the original regular/bold outlines with no extra
stroke pass. Roboto and System retain their existing synthesized Bold style.

## Font acceptance requirements

Every requested style must supply all 95 printable ASCII characters
(U+0020–U+007E) and all 96 Latin-1 supplement characters (U+00A0–U+00FF)
from that font itself. Check its character map and rendered glyphs before
adding assets, build entries or settings choices. Space and nonbreaking space
may be blank; other required glyphs must render.

Do not fill missing characters using Roboto, System or any other font. If a
requested style is incomplete, report the missing characters and leave the
font out unless the user explicitly changes this requirement. Apply this rule
to every future font addition, including both Regular and Bold when requested.

## Sources and notices

Roboto uses the existing [Roboto copyright notice](licenses/Roboto-COPYRIGHT.txt).
Emotion Engine version 1.10 (June 16, 2014) is by Neale Davidson / Pixel Sagas:
[designer](https://www.pixelsagas.com/),
[download used](https://www.fontspace.com/emotion-engine-font-f18002),
[original included license](licenses/Emotion-Engine-LICENSE.txt).
The included license permits embedding for general software use and limits
commercial use. Original TTF files are not redistributed in this repository;
obtain them from the designer/distribution page. The generated bitmap atlases
are embedded in Launchstation, with a credit in About Launchstation.

The downloaded source files have these SHA-256 hashes:

| Source file | SHA-256 |
| --- | --- |
| `EmotionEngine-8ynA.ttf` | `f204a4db4acf1a02c0fd0eec369d150de30d12477a28b648e1adefd0b42bd95c` |
| `EmotionEngineBold-YGnL.ttf` | `6be3ccc97b3540e7316e783130fcf4cf93075684ac4486888b22fe5bed8f508a` |

## Regenerate on a development PC

Install the host-only dependencies used for these assets:

```sh
python -m pip install "Pillow==11.3.0" "fonttools==4.60.2"
python scripts/gen_dash_font.py /path/to/Roboto-Regular.ttf
python scripts/gen_dash_font.py /path/to/EmotionEngine-8ynA.ttf --symbol dash_emotion_engine_faces --output src/dash_font_emotion_engine.c --license-notice docs/licenses/Emotion-Engine-LICENSE.txt
python scripts/gen_dash_font.py /path/to/EmotionEngineBold-YGnL.ttf --symbol dash_emotion_engine_bold_faces --output src/dash_font_emotion_engine_bold.c --license-notice docs/licenses/Emotion-Engine-LICENSE.txt
```

The generator checks the font's Unicode character map for all 95 printable
ASCII and 96 Latin-1 supplement characters. It renders individual glyphs with
Pillow's BASIC layout so formatting behavior does not hide the soft hyphen.
Unexpected empty glyphs are rejected; space and nonbreaking space remain blank.
Do not edit the generated C arrays by hand. FontTools and Pillow are not used
on the PS2 or needed to compile the already-generated C files.

## Runtime ownership and memory

Each smooth font has 14, 20, and 32 px source atlases. The renderer picks the
closest size, measures and draws with the chosen family's metrics, and reuses
three texture slots when switching families or Emotion Engine styles. Old VRAM mappings are released;
read-only generated pixel arrays are never freed or copied to another heap buffer.
The System font retains the existing bitmap font and independent controller
glyphs. Font IDs 0/1 remain Roboto/System; Emotion Engine uses `emotion_engine`
with the existing `bold = 0/1` theme key for Regular/Bold. Older
`font = emotion_engine_bold` (or numeric 3) entries load as Emotion Engine with
Bold style, regardless of the old `bold` key's position or value.

Emotion Engine Regular uses 172 KiB of pixel data across its three sizes;
Bold uses 190 KiB. Together they add 362 KiB of embedded pixel data plus glyph
metrics. The largest active smooth-font family remains Roboto at 210 KiB,
plus its three 1 KiB palettes. Only the selected family owns VRAM mappings.

`tests/test_font_weight.py` exercises every family and source size, Latin-1
symbols, family/style switching, texture shutdown/reinitialization, and Bold/shadow
behavior. `tests/test_tile_styles.py` covers font choices in settings/theme
serialization and preserves Roboto/System IDs. Physical-PS2 visual checks
remain necessary for interlaced/CRT readability and texture switching.
