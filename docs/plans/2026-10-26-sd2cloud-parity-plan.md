# SD2Cloud parity plan (issue #108): verified foundations

> **Update 2026-10-28:** [#226](https://github.com/NathanNeurotic/Launchstation/pull/226) (merged) implemented the
> card image parser and writer, **Card contents...** (list, copy, move, delete, `.psu` in/out, copy to a new size),
> verified safety copies, the active-card check and guarded card switching (`SET_CARD`/`SET_CHANNEL`), the IGR
> fingerprint backup and an mmceman handle-leak fix. Host-tested only; **not tested on a console**. The console test
> matrix is in the PR. The text below is the original plan and is kept as written.
>
> **Status as of 2026-10-26: plan, nothing implemented.** Written after a code audit and two research passes.
> Everything marked *unverified on hardware* has never been run on a console by anyone on this project.

## What the audit found (code-verified)

Implemented and reachable from the GUI: local versioned backup, sync-now, verified restore to a NEW image,
Google Drive link/upload/browse/recover/disconnect, IGR helper status/install/update, auto-sync modes, retention,
format, language, custom OAuth client/CA, path pickers.

Missing: showing a card's saves, free space inside a card image, restore preview, templates, the Library,
"All saves", card create/resize/check, convert to Game ID, per-save backup versions, `.zip` import, `.mc2`/`.bin`
import, MemCard PRO2 support.

Corrections made while verifying:

- **Save copy/move is not read-back verified.** `src/filer_copy.c` detects short reads and writes only. Only
  card-image backups are verified byte-for-byte. Do not claim "verified writes" for saves.
- **MMCE card switching exists in the protocol** (see below). Direct `SET_CARD`/`SET_CHANNEL` switching is not used
  by any EE code; only `SET_GAMEID` is, via `setNeutrinoMmceGameID` during a Neutrino launch. The website sentence
  "switch its game card" overstated this and now says Launchstation asks the adapter to select the Game ID card on launch.
- **A stable/beta update channel is not worth building:** only the rolling `latest` release is published.

## Foundation 1: MMCE card/channel control (sd2psXtd firmware at the commit pinned by `docs/MMCE_SD2PSXTD_FIRMWARE_AUDIT.md`)

Read from firmware and the bundled `iop/mmceman` source; not hardware-tested.

- Commands: `GET_CARD 0x3`, `SET_CARD 0x4` (type 0 regular / 1 BootCard; mode num/next/prev), `GET_CHANNEL 0x5`,
  `SET_CHANNEL 0x6`, `SET_CARD_CHANNEL 0xa`, `GET_STATUS 0x2` (bit 0 = busy), `SET_GAMEID 0x8`. The app calls only
  PING, GET_STATUS and SET_GAMEID (`src/neutrino_gameid.c`).
- `GET_CARD`/`GET_CHANNEL` return numbers only: no name, and no regular / BootCard / Game ID / named type. After a
  switch to a Game ID or named card the index cannot be restored exactly.
- There is no list-cards command. A card list must come from browsing `mmceN:/MemoryCards`.
- A switch is asynchronous: performed only when no write is pending and no controller button is held on the
  device, then ~0.5 s of close/reopen, and the card ignores the console's next 5 requests on purpose. Poll
  `GET_STATUS` busy (the repo uses 15 x 1 s).
- `SET_CARD` does not check that the target exists. PING's protocol version stays 1, so it is not a feature flag.
- Do not switch during a game's save session or with open handles (the driver returns -EBUSY on open handles).
- MemCard PRO2: no public command documentation; SD2Cloud itself calls switching there a "preview". Out of scope.

## Foundation 2: PS2 memory card image format (for a read-only card viewer)

Sources: Ridge's format document, ps2sdk `mcman` / `libmc-common.h` (AFL 2.0), mymc (public domain), PCSX2
`MemoryCardFile.cpp` (GPL, behaviour reference only).

- Little endian. Superblock at offset 0: magic `"Sony PS2 Memory Card Format "` (28 bytes), version at 0x1C,
  `pagesize` 0x28, `pages_per_cluster` 0x2A, `clusters_per_card` 0x30, `alloc_offset` 0x34, `alloc_end` 0x38,
  `rootdir_cluster` 0x3C (must be 0), `ifc_list[32]` 0x50, `bad_block_list[32]` 0xD0, `cardtype` 0x150 (must be 2).
- Require `pagesize * pages_per_cluster == 1024`. Directory/FAT cluster numbers are **relative to `alloc_offset`**;
  `ifc_list` and the FAT-cluster numbers stored in the indirect clusters are **absolute**.
- FAT: 256 u32 entries per cluster, two-level indirect lookup via `ifc_list`. **Free = bit 31 clear** (not "equals
  0x7FFFFFFF"); in use = bit 31 set, low 31 bits = next cluster; end of chain = `0xFFFFFFFF`. Bound every walk by
  `alloc_end` and a loop counter.
- Directory entry: 512 bytes: `mode` u16 @0, `length` u32 @4, `cluster` u32 @0x10, `name[32]` @0x40. Live entries
  have mode bit `0x8000`; directory bit `0x20`; PS1 saves `0x1000`. Skip `.` and `..`. A card is formatted only if
  root entry 0 is `.` and entry 1 is `..`.
- `icon.sys`: magic `PS2D`, u16 line-break offset @6, title 68 bytes @192 in Shift-JIS (offset counts bytes).
- Variants: no-ECC (`.mcd`, `.bin`, `.mc2`; 8 MiB = 8,388,608) and ECC (`.ps2`; 528-byte pages, 8 MB =
  8,650,752). Size separates them (N x 1,048,576 vs N x 1,081,344, N in 1..128). `.mc2` being raw no-ECC is
  medium confidence. sd2psXtd offers 1-128 MB cards; only 8 MB matches real PS2 firmware.
- Free space from the FAT can differ slightly from the PS2 browser (mcman rounds the allocatable limit and skips
  bad blocks). Show "approx." or document it.

### Licensing rule

Implement clean-room from Ridge's document and the ps2sdk struct definitions (AFL). Cross-check against mymc
(public domain) and MIT ports. Treat GPL projects (PCSX2, mymc+/mymc++, ps2vmc-tool, SD2Cloud, ORBIT) as
black-box test oracles only; do not copy their code.

### Validating a parser (required before building on it)

1. Fixtures made by an independent tool (mymc / mymc-py: `format`, `import` a PSU, `dir`, `df`, `check`), 8 MB no-ECC
   and ECC, plus a 16 MB image. **Do not generate fixtures with our own writer only; that proves nothing.**
2. Compare our listing, sizes and free-cluster count with the tool's output.
3. Hostile inputs: looping FAT chain, `rootdir_cluster != 0`, truncated file, wrong magic, version 1.0, ECC and
   no-ECC of the same card parse identically, directory `length` larger than its chain.
4. Run the same parser on a real card dump from the console and compare against the PS2 browser (hardware).

## Ranked implementation order

1. **Card summary parser (host-testable, read-only)**: new `src/mc_card_view.c` with a pure function over a
   read callback, so it can be tested on the host with the independent fixtures. No UI yet. Unlocks everything below.
2. **UI: "Card contents" for a selected `.mcd`/`.ps2`** in the file browser (saves with titles, sizes, approx. free).
3. **MMCE card list** from `mmceN:/MemoryCards` (names/types by folder), with last backup and pending state per
   card; runs as a background job (the scan reads a lot).
4. **Active card display** via `GET_CARD`/`GET_CHANNEL` (index only; say so). Optional guarded switch action,
   labelled hardware-unverified, only on sd2psXtd (`PING` ok), only at safe moments, never with open handles.
5. **Automatic safety copy before delete/rename/overwrite of a card image**, aborting on failure.
6. Per-slot BootCard choice for the IGR install (`src/mmce_igr.c`).
7. Later / separate decisions: templates, the Library, per-save backup versions on Drive, `.zip`, `.mc2`
   import. These need design work and are not cheap.

Every step ships with host-mocked GUI navigation tests (per #108 acceptance criteria 4) and a console test plan.
