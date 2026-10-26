# Ember RetroGem Game ID

Ember disc-image launches use the same RetroGem visual signal and identity
rules as POPStarter VCD launches. The existing **Settings > Launching > Game ID
output > Game ID launch signal for discs** preference controls both. No separate
Ember preference or MMCE card-selection command is added.

## Identity lookup

For a selected CUE, Launchstation reads the first supported data track's
`FILE` and `INDEX 01`, opens that track file relative to the CUE, and reads its
ISO9660 filesystem. The CUE describes the track location; it does not itself
contain the game's executable serial.

Lookup uses the same production functions as VCD identification:

1. Parse `BOOT` in disc `SYSTEM.CNF`, such as `cdrom0:\SLUS_123.45;1`.
2. If no usable serial exists, consult the existing PS1 PVD timestamp database
   (including generic `PSX.EXE` games).
3. After confirming an ISO PVD, retain the existing filename-based fallback.
   A CUE and VCD with the same basename receive the same fallback. Different
   arbitrary filenames cannot provide a content-derived identity.

CUE `TITLE`, `PERFORMER` and `REM` text do not override the disc's ID. Parsing
supports quoted relative track filenames, multiple FILE/audio-track entries,
MODE1/2352, MODE2/2352, MODE1/2048, MODE2/2048 and MODE2/2336. `INDEX 01` is a
file-sector offset; neither `PREGAP` nor `INDEX 00` is added to that offset.
Mode 2 Form 2 audio/video sectors cannot be treated as ISO filesystem sectors.

BIN selections first use a same-stem `.cue`/`.CUE` if present; otherwise they
probe the supported raw/cooked sector layouts at file offset zero. An EXE can
use a same-stem CUE, but a standalone PS-X executable has no ISO SYSTEM.CNF/PVD
and receives no guessed disc identity. This feature does not enumerate other
unrelated CUE files or change Ember's existing folder-based launch arguments.

CUE files are bounded to 64 KiB. Missing/unsupported/malformed cues, unreadable
tracks and unknown IDs simply omit the optional signal. Lookup is read-only;
it does not rewrite images, add metadata files or change saves. CUE FILE paths
must be relative to the current device rather than request another device.

## Handoff

The ID and display preference are captured before cleanup frees configuration
or changes mounts. The two-frame visual signal is displayed after successful
cleanup and any USB storage remount, immediately before the existing keep-IOP
loader handoff. USB remount identity checks, argv[0], game-folder arguments,
BIOS handling and device-stack policies remain in their existing paths.

## Validation

PS1 serial normalization follows the conventions verified in
[OPL's resolver at 9d4ee45](https://github.com/NathanNeurotic/Open-PS2-Loader/blob/9d4ee450c14f3ecaf3ca1b343167dfb7dafcc694/src/retrogem.c)
and CosmicScale's normalization feedback: uppercase; remove existing separators;
format ordinary serials as `SLUS_123.45`; reduce `SLUSP` to `SLUS`; and retain
the `LSP12345.67` layout for LSP serials. The shared PS1 identity parser applies
these rules to both VCD and CUE/BIN inputs. This does not change generic ELF
identity, MMCE selection, artwork lookup, or the RetroGEM barcode protocol.
The fixture matrix checks each spelling across all supported sector layouts.

`tests/test_ember_gameid.py` compiles the production CUE reader, shared ISO
reader, VCD ID functions, serial parser and timestamp database. Generated
CUE/BIN and equivalent VCD fixtures must produce identical IDs. Cases include
MODE1/MODE2, 2048/2336/2352-byte sectors, INDEX/pregap, audio entries, BOM/CRLF,
timestamp/name fallbacks, fragmented/failed reads, malformed cues and closed
file descriptors.

`tests/test_ember_retrogem_handoff.py` executes the production Ember launch
function with mocked device/GS handoff: preference on/off, present/missing ID,
USB/non-USB, failed preparation, cancelled cleanup and failed remount. It checks
that the signal follows successful remount and that launch arguments stay the
same.

These are host tests and target-build checks. RetroGem recognition and actual
Ember game execution on a console remain separate hardware acceptance gates.
