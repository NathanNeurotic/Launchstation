# R3Z-11: independent Game ID channels

Source audit dated 2026-10-25 against Launchstation baseline
`b815d10da29274200572e61467d72a718004a708`, with R3Z-12's changes described
separately. Upstream reference is
[`06b06d000072d358c93d98104d64d2f081e47ba7`](https://github.com/saildot4k/wLaunchELF_R3Z/tree/06b06d000072d358c93d98104d64d2f081e47ba7).
Ember is out of scope. POPStarter uses its own VMC files rather than the physical memory-card path;
MMCE Game ID selection is therefore not applicable to POPStarter. RetroGem
output remains required and independent.

RetroGem's Game ID is an optical code rendered through the GS for an HDMI
device to read. MMCE's Game ID is a command to a memory-card emulator to select
a per-game card. They may share a serial string but have different recipients,
failure modes and acceptance criteria.

| ID channel × launcher | Source path and gate | Finding |
|---|---|---|
| **RetroGem × Neutrino** | `LaunchNeutrinoIso` in `src/neutrino_iso.c` calls `prepareNeutrinoGameID` to parse ISO9660 SYSTEM.CNF BOOT2. After committed-launch cleanup, `displayRetroGemGameID(gameID, 2)` in `src/main_gameid.c` draws the visual payload. `cdrom_disable_gameid` suppresses this display. | Present. The serial/display path is available without MMCE support and for MX4SIO. Valid ID extraction and a submitted GS draw are not proof that RetroGem recognized it. |
| **MMCE × Neutrino** | `setNeutrinoMmceGameID` in `src/neutrino_gameid.c`; PING `0x01`, SET_GAMEID `0x08`, GET_STATUS `0x02` through `fileXioDevctl`. | Present but baseline required active MMCE. R3Z-12 adds manager-only bring-up independently of the source stack, protects ISO/ELF slots, skips MX4SIO path or resident driver, and keeps the existing finite 15-poll busy bound. `cdrom_disable_gameid` does not disable this channel. Unknown IDs or control errors remain best-effort and do not prevent launch. |
| **RetroGem × POPS/POPStarter** | `LaunchPopstarterVcd` in `src/popstarter.c` uses `buildPopstarterVcdGameID` in `src/popstarter_gameid.c`, then conditionally calls `displayRetroGemGameID(..., 2)` after cleanup. | Present. Lookup prefers VCD SYSTEM.CNF, then the existing generic PVD timestamp ID, then filename/partition fallback. The RetroGem disc preference controls display. Correct recognition and fallback identity still require a RetroGem-equipped test. |
| **MMCE × POPS/POPStarter** | Inspected `src/popstarter.c`, `src/popstarter_gameid.c`, candidate preparation/path/ELF modules and `src/main_actions.c`. | **Not applicable, by design.** POPStarter uses its own VMC files and does not need a real-card image selected through MMCE. Do not add SET_GAMEID to VCD launches. The earlier audit incorrectly classified its absence as a gap. |

## Conflations to reject

* A successful `displayRetroGemGameID` call is not a successful MMCE card switch.
* A PING success proves an MMCE responder, not that the target card became
  active. An accepted SET_GAMEID and bounded status handling still need card
  identity verification on hardware.
* `cdrom_disable_gameid` is the RetroGem disc-display preference in these
  image-launch paths; applying it to MMCE selection would couple the channels.
* Physical-disc `updateOSDHistoryFile` in `src/main_actions.c` is a separate
  mechanism. It is separate from POPStarter, whose MMCE selection cell is not applicable.
* A valid serial parsed from SYSTEM.CNF is identity input, not transport output.
* Host mocks and successful standard/DS34 builds do not demonstrate either a
  RetroGem decode or a real emulator switching cards.

## R3Z-12 source policy and limitations

The MMCE control loader lives in `src/init.c` as
`ensureMmceGameIdControl`. It reuses the existing manager if loaded; otherwise
it starts the existing `mmceman` module without invoking the reset-capable
storage-stack switcher. It refuses both a resident MX4SIO driver and the active
MX4SIO stack. Other storage drivers/mounts are left resident. Marking MMCE as
the active SIO2 stack preserves the existing conflict handling if MX4SIO is
subsequently requested.

The Neutrino sender skips a numbered MMCE slot holding the ISO, a numbered MMCE
slot holding Neutrino, and the equivalent physical `mc0:`/`mc1:` ELF slot.
Unresolved `mmce:`/`mc:` aliases are conservatively skipped. These restrictions
can leave no switchable slot; launch then proceeds without selection.

Loading `mmceman` installs SIO2 hooks and its existing startup probes/reset of
the MMCE **filesystem session**; this is not a request to switch the card image.
No fresh IOP reset is introduced. Compatibility with each emulator firmware,
controller build, source-device stack and card-slot arrangement still requires
console evidence. In particular, do not call USB/ATA/i.Link/UDPFS source
preservation proven just because the loader avoids `Reset()`.

## Hardware acceptance matrix still required

For each applicable cell, record the exact ELF/build, source path, Neutrino or
POPStarter path, card firmware, slots, Game ID preference, expected serial and
observed recipient behavior. For MMCE/Neutrino, include both/opposite-slot
switches, ELF on mc0 and mc1, ISO on an MMCE slot, no MMCE attached, busy/error
responses, and MX4SIO active even when paths alone do not identify it. For
RetroGem include preference on/off and a build without MMCE. The POPS/MMCE cell
is not applicable. `tests/test_popstarter_retrogem.py` exercises the actual
POPStarter handoff for ID present/missing, RetroGem preference on/off, reset
and keep-IOP routes, and failed-launch suppression. It requires no MMCE selector.
