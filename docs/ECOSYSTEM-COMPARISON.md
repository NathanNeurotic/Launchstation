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
| Backup by save (per-save versions on Drive) | Yes, optional | Not found in the docs: **not audited** |
| Memory card manager (browse saves, 3D icons) | Yes, per card and "All saves" view | Save browsing by title, PSU create/extract, card image backup/conversion exist ([Saves](../docs-site/content/saves.html)); full card manager **not audited** |
| Copy / move / delete saves across cards, multi-select | Yes | Copy/paste in the file browser; cross-card multi-select workflow **not audited** |
| The Library (saves kept off cards) | Yes | None found |
| Templates (default save sets applied to game cards) | Yes | None found |
| Create / resize / rename / check / delete cards, convert to Game ID | Yes, with a safety copy | Neutrino Game ID card switching is documented; card create/resize/convert tools **not audited** |
| OPL VMC and USB/HDD/MX4SIO card files | Yes | VMC support documented; breadth **not audited** |
| Works without an sd2psx | Yes (regular cards, USB/HDD/MX4SIO) | Regular memory cards supported in the file browser |
| MemCard PRO2 | Read/write `.mc2`; card switch is a preview | Not documented |
| Self-update | Stable and Beta channels, SHA-256 verified | "Check for updates" against the rolling release |
| Languages | 6 | 9 dashboard tables (some extended help still English) |
| Installs a SAS package on the card for IGR | Yes (`APP_SD2CLOUD`) | Own IGR helper install/status in Settings |

The authoritative checklist for closing these gaps is [issue #108](https://github.com/NathanNeurotic/Launchstation/issues/108)
("GUI parity: complete on-console SD2Cloud/MMCE management workflows"). It was closed within minutes of being filed
and no pull request is linked, so treat every unchecked row above as open until audited. The existing
`tests/test_sd2cloud_parity.py` checks that labels and features are present; it does not prove the workflows work.

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

1. Audit the Launchstation GUI against the unchecked SD2Cloud rows and #108's acceptance criteria, row by row, and
   record "implemented / partial / missing" with evidence.
2. Implement the cheapest high-value gaps first (save-level backup versions, a default-template apply, card
   create/resize), each as its own PR with host-mocked GUI navigation tests.
3. Keep ORBIT-only ideas (RetroAchievements agent, in-game pause menu) as separate proposals; they need a Neutrino
   fork or an in-game agent and are a different risk class.
4. Do not copy code from either project without settling the licence question above.
