# SD2Cloud and ORBIT: comparison and parity map

Two community PS2 homebrew projects overlap with parts of Launchstation. This page records what each one does,
how that compares to Launchstation today, and where the gaps are. It is a documentation and planning aid, not a
claim of parity.

- Upstream facts come from each project's public README as read on **2026-10-26**. Both projects change quickly;
  recheck before relying on a row.
- "Launchstation" entries come from this repository's own documentation. Where this page says **not audited**,
  nobody has compared that feature against the code or tested it on a console.
- **No source code from either project is incorporated.** Both are GPL-3.0; this repository has no single root
  licence (see [THIRD_PARTY_CREDITS.md](THIRD_PARTY_CREDITS.md)), so copying their code would need a licence
  decision first. Launchstation's Google Drive client is original ([NATIVE_CLOUD.md](NATIVE_CLOUD.md)).

## SD2Cloud

[oMrRexD/sd2cloud](https://github.com/oMrRexD/sd2cloud) by MrRexD, GPL-3.0. A PS2 application that manages and
backs up memory cards on the console itself, to Google Drive, with no PC. Works with sd2psx-family cards running
the sd2psXtd firmware (the PS2 sees them as MMCE), MemCard PRO2, regular memory cards, OPL virtual memory cards and
card files on USB, MX4SIO or the internal HDD.

| Area | SD2Cloud | Launchstation |
| --- | --- | --- |
| Google Drive sign-in | Device code or QR code; only files it creates are accessible | Same flow (OAuth device flow, QR, `drive.file` scope). [Cloud backup](NATIVE_CLOUD.md) |
| Sync cards to Drive | On demand, or automatically on exit from a game via IGR; only changed cards upload | Back up changed MMCE cards and upload on demand; IGR helper with auto-sync Off / Local / Local + Google Drive |
| Backup history | 10 per card by default, restore any, verified before and after write | Versioned local history with a retention setting; verified recovery from local or cloud history |
| Backup format | `.mcd`, or `.ps2` (PCSX2) | `.mcd` or `.ps2` |
| Browse cloud history | From the Files tab | "Browse cloud history..." (read-only paged view, verified recovery) |
| Backup by save (per-save versions on Drive) | Yes, optional | **Present, not yet tested on a console** (#232): optional **Per-save versions** keeps each changed save of every backed-up card as `WLE-Saves/<save>/WLE-<stamp>.psu`, uploaded with the cards; one version downloads as a verified `.psu`. Taken from the verified history copy, not the live card |
| Memory card manager (browse saves, 3D icons) | Yes, per card and "All saves" view | **Partial**: **Card contents...** opens a card image and lists its saves with titles, file counts, sizes and free space ([Saves](../docs-site/content/saves.html), added in [#226](https://github.com/NathanNeurotic/Launchstation/pull/226)). **All saves here...** lists the saves of every card image in a folder and its subfolders together, sorted by title with each save's card named (#232). No 3D icons |
| Copy / move / delete saves across cards, multi-select | Yes | **Partial**: **Card contents...** copies, moves and deletes saves inside card images, including MMCE cards the adapter is not serving, one save at a time, with a verified safety copy first and a read-back after (#226); the same actions open from **All saves here...** for saves on any card in a folder, where **Square** marks several saves for one batch copy, move or delete with one safety copy per card (#232). Multi-select copy/cut/paste/delete works on `mc`/`vmc` folders; those copies are checked for short reads/writes, not read back |
| The Library (saves kept off cards) | Yes | **Yes, folder-based**: **Save library here...** lists the `.psu` saves in a folder and the folders inside it, sorted by title, and copies them to a card (one verified safety copy of that card, nothing on it replaced) or deletes them (#232). The library is any folder you choose; there is no cloud copy of it |
| Templates (default save sets applied to game cards) | Yes | **Partial**: a set is a named folder of `.psu` files in the library, made with **Save as a set...**; **Apply a set to a card...** copies it to a card you pick after a confirmation, skipping saves already there by name (#232). Sets are never applied automatically, for example to a new Game ID card |
| Create / resize / rename / check / delete cards, convert to Game ID | Yes, with a safety copy | **Partial**: copy a card to a new 8-64 MB size, "Install as NEW card", and select a numbered card with **Make this the active MMCE card...** (#226); file-level rename/delete. **New blank card...** in the File Browser makes an empty 8-64 MB card image, and **Split into Game ID cards...** makes one card per game from a card's saves, installed with **Install as MMCE Game ID card...** (#232; pending hardware validation). No card check. Neutrino selects a Game ID card on launch |
| OPL VMC and USB/HDD/MX4SIO card files | Yes | **Partial**: VMC mounting, card-image backup/export, and **Card contents...** on `.mcd`, `.ps2` (read-only), `.bin`, `.vmc` and `.mc2` images are supported; `.zip` is not |
| Works without an sd2psx | Yes (regular cards, USB/HDD/MX4SIO) | Regular memory cards supported in the file browser |
| MemCard PRO2 | Read/write `.mc2`; card switch is a preview | **Partial**: treated as a generic MMCE; `.mc2` image files open in **Card contents...** like other no-ECC images (#226, not tested with a MemCard PRO2) |
| Self-update | Stable and Beta channels, SHA-256 verified | "Check for updates" against the rolling release (no beta stream is published) |
| Languages | 6 | 9 dashboard tables (some extended help still English) |
| Installs a SAS package on the card for IGR | Yes (`APP_SD2CLOUD`) | Own IGR helper install/status in Settings |

The authoritative checklist for closing these gaps is [issue #108](https://github.com/NathanNeurotic/Launchstation/issues/108)
("GUI parity: complete on-console SD2Cloud/MMCE management workflows"). [#226](https://github.com/NathanNeurotic/Launchstation/pull/226)
(merged 2026-10-28) added card contents, safety copies, card resize, the active-card check and card switching, and
the IGR fingerprint backup. None of it is tested on a console yet. Rows still marked Partial or Missing remain open.
`tests/test_sd2cloud_parity.py` checks that labels and features are present; the #226 host tests
(`test_mc_card_write`, `test_dash_card_tool` and others) exercise the card code under simulation, which still does
not prove the workflows on hardware.

## ORBIT

[danielnuld/ps2-launcher](https://github.com/danielnuld/ps2-launcher), GPL-3.0 (its bundled Neutrino fork is
AFL-3.0). A game launcher for a real PS2 that starts games through Neutrino. Companion app
[ORBIT Jellyfin](https://github.com/danielnuld/orbit-jellyfin) is tracked in
[issue #186](https://github.com/NathanNeurotic/Launchstation/issues/186). ORBIT has no Google Drive feature; its
"cloud" overlap is network-delivered content (covers, a home game server) and save icons.

| Area | ORBIT | Launchstation |
| --- | --- | --- |
| Library views | Carousel, grid, list; filters All / PS2 / PS1 / Apps | Grid, list, carousel; application filters |
| Covers | Downloaded by the console over HTTPS, no PC step | Artwork and covers supported; whether the console downloads them itself is **not audited** |
| 3D save icons | Newest save of each game, spinning on the detail card | Save icon preview support exists; parity **not audited** |
| Game sources | USB, internal HDD (exFAT / HD Loader), MX4SIO, iLink, MMCE, UDPBD, home game server (udpfs) | USB, HDD, MX4SIO, MMCE, iLink, SMB, UDPFS and more ([devices](../docs-site/content/devices.html)) |
| Launch engine | Neutrino (bundled fork) | Neutrino, POPStarter, Ember |
| Virtual memory cards per game | Created on first play, on the game's device | VMC documented; automatic per-game creation **not audited** |
| Per-game options | Video mode, compatibility modes, memory card | Saved per-game Neutrino profiles |
| In-game menu / reset combos | Hold L1+L2+R1+R2+START+SELECT; restart and power-off combos | IGR helper and reset; no in-game pause menu |
| RetroAchievements on console | Yes, via an in-game agent and the xeRAbora PC client | None |
| Languages | Spanish, English | 9 dashboard tables |
| Companion apps | ORBIT Jellyfin | Any ELF via Applications; see #186 |

## Suggested order for closing gaps

The detailed, verified order is in the [parity plan](plans/2026-10-26-sd2cloud-parity-plan.md); in short:

1. Finish auditing the remaining rows above against #108's acceptance criteria, recording implemented / partial /
   missing with evidence.
2. Done in #226: the card image parser, the card contents view, the active-card check, card switching and the
   automatic safety copy before every card write. Next: hardware validation (the console test matrix in #226),
   then restore previews, which build on the same parser. "All saves" and batch copy/move/delete of marked saves are
   in (#232).
4. A folder-based Library, manually applied sets (templates) and per-save backup versions are in (#232; per-save
   versions are not yet tested on a console). Applying a set automatically (for example to a new Game ID card)
   needs its own decision.
5. Keep ORBIT-only ideas (RetroAchievements agent, in-game pause menu) as separate proposals; they need a Neutrino
   fork or an in-game agent and are a different risk class.
6. Do not copy code from either project without settling the licence question above.
