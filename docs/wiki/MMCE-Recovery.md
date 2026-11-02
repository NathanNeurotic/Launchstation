# MMCE: safe local and Google Drive recovery

LAUNCHSTATION retains local verified version history for MMCE card images and, optionally, copies history to Google Drive. The primary requirement is that **a recovery attempt must not destroy the active emulated card or valid older snapshots**.

## Local recovery

1. Configure the parent folder of `WLE-MMCE-Backups` in Settings → Storage & devices → MMCE backups (Backup folder). The same page sets Backups per card (0 keeps every verified version), Backup format (native `.mcd` or PCSX2 `.ps2`), Per-save versions (Off by default; keeps each changed save as a `.psu` in `WLE-Saves/<save>/` beside its card's history, uploaded to Google Drive with the cards) and Automatic sync (Off, Local, Local + Google Drive).
2. Choose **Recover saved card image...** in Settings or select a completed `.mcd`/`.ps2` snapshot in File Browser.
3. Select a new USB/HDD destination. LAUNCHSTATION validates the image, writes a distinct `.PART`, reads back/verifies it and publishes `RECOVER-###.mcd` only after success.
4. Inspect the newly produced file before making any manual card replacement. The current MMCE card is not replaced or activated.

To preserve an individual MMCE card before a manual recovery, highlight its `.mcd` in `mmce0:/MemoryCards` or `mmce1:/MemoryCards` and choose **Back up selected MMCE card to history...** in the File Browser menu. Verification and retention must protect the selected source. The same menu offers **Back up all MMCE cards...** (normal, named, GameID and BootCard images), **Back up active MMCE card...** (the exact firmware-selected backing image), **Export active MMCE card as PCSX2 .ps2...** (adds ECC/spare bytes and verifies), **Back up .mcd...**, **Export as PCSX2 .ps2...** and **Convert to no-ECC .mcd...** for local images.

## Store a snapshot on the adapter without activating it

Highlight a verified local `.mcd`/`.ps2` and choose **Store NEW snapshot on MMCE...**. The image is verified independently and stored in `mmceN:/WLE-Recovered`, outside all firmware-selectable card paths. It is not activated or installed as a game card; use the next section for that.

## Install a NEW selectable card on the existing firmware

In File Browser, select a complete USB/HDD `.mcd` or `.ps2` image, or an
image stored in `mmce0:/WLE-Recovered` / `mmce1:/WLE-Recovered`.
Choose **Install as NEW selectable MMCE card...**, then select the adapter.
Launchstation allocates a previously unused numbered `CardN` directory and
publishes `mmceN:/MemoryCards/PS2/CardN/CardN-1.mcd` after readback
verification. The newly installed card is **not** automatically selected.
Select it with the adapter's buttons, or highlight its `CardN-1.mcd` and choose
**Make this the active MMCE card...**, which selects it and confirms the switch
by number, channel and content. This only works while the adapter is serving a
numbered card: from a BootCard, Game ID card or named card it sends nothing and
asks you to change cards with the adapter's buttons, because the firmware cannot
be moved off those by card number. **Which MMCE card is in use?** shows the card
the adapter is serving, confirmed by content.

## Restore single saves instead of a whole card

To bring back one save without touching the rest of a card, open a recovered
image (or any card image) with **Card contents...**, then use **Copy to another
card...** or **Export as .psu...**. Copying into an MMCE card works only on a
card the adapter is not serving: select another card first. Every write starts
with a verified safety copy of the target card in `WLE-Safety/`
(`mmceN:/WLE-Safety/` on MMCE; the newest 10 are kept). A recovered card of the
wrong size can be rebuilt with **Copy to a new card of another size...**, which
writes a new file to `mmceN:/WLE-Resized/` for **Install as NEW selectable MMCE
card...**. To start from an empty card instead, open the folder menu in an
MMCE folder and choose **New blank card...**: it writes an empty 1-64 MB card to
`mmceN:/WLE-New/` for the same install step. To give each game its own card,
open a card with **Card contents...** and choose **Split into Game ID cards...**:
it writes one card per game to `mmceN:/WLE-GameID/<card>/<ID>/<ID>-1.mcd`, and
**Install as MMCE Game ID card...** copies one into `MemoryCards/PS2/<ID>/`
only when the adapter has no folder for that game yet. Not yet tested on a
console: keep a PC copy of your `MemoryCards` folder.

No existing card or GameID/BootCard backing image is replaced, and no sd2psXtd
firmware update is needed. If copying or publishing fails, leave that
numbered directory unselected: it may contain a partial `.PART` or `.mcd`
and needs inspection before cleanup. This is not a safe operation for
replacing a card already in use and is not an atomic power-loss guarantee.

## Google Drive history

1. Link the Google account using the native OAuth settings and the Google CA trust bundle; Drive support is optional.
2. In Tools → Google Drive backups, choose **Browse cloud history...**. This is a paged, read-only view of application-created backup history.
3. A snapshot **must include its stored `wle_sha256` digest** to be eligible for recovery. Without it, the entry is only inspectable.
4. Choose a qualified snapshot and a USB/HDD folder. LAUNCHSTATION authenticates the download, enforces the length limit, verifies SHA-256, validates card geometry, converts PCSX2 `.ps2` where applicable and publishes a new `RECOVER-CLOUD-###.mcd` only after verifying the completed copy.
5. The Drive entry, local history and live MMCE card stay unchanged; failures cannot publish a completed recovery file.

## Retention, IGR and failures

Local snapshot export uses `.PART` and verifies before publication. Retention follows successful completion and must not prune a protected restore source. Unchanged cards may skip creating a new version. The OPL IGR helper (install or update it from Tools → OPL IGR automatic MMCE backup or Settings → Storage & devices → MMCE backups → IGR helper) backs up changed cards locally when OPL exits a game, before optional cloud upload; **After IGR, open** chooses where to go afterwards. Reinstall the helper after changing the backup folder, because it embeds its destination. A cloud failure does not erase a verified local snapshot.

**Not implemented:** direct overwrite/activation of a live MMCE card from local history or Drive. Do not misread a successfully recovered USB/HDD file as an installed MMCE card.

## Required bug-report fields

Commit SHA, PS2 model, original MMCE unit, selected backup format, destination device/filesystem, exact error, whether the target `.PART` remains, and whether any data changed. Do not send OAuth tokens, game/BIOS data or private backups publicly.
