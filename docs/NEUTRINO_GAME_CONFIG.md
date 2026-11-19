# Per-game Neutrino settings file

**Games tab > Triangle > Game settings** (and the Neutrino options before a launch) save a game's
settings in `NEUT-<Game ID>.cfg`, for example `NEUT-SCUS_971.13.cfg`. The file goes on **the device
the game is on**, in the `CFG` folder at the device root (`mass:/CFG/NEUT-SCUS_971.13.cfg`), next to
the `VMC` folder. Your memory card is not filled with per-game files. It is plain text in the same
`$Key=value` style as RiptOPL's per-game `.cfg`, so you can edit it by hand. It is named `NEUT-...`
so it never touches RiptOPL's own `CFG/<Game ID>.cfg`.

```
$Compatibility=6
$NeutrinoFastReads=0
$NeutrinoVideo=2
$NeutrinoGsmComp=0
$NeutrinoArgs=-dbc -logo
$VMC_0=My Game
$VMC_1=
$End=1
```

Keep `$End=1` as the last line when you edit a file by hand. A file without it is treated as unfinished (for example a save cut short) and ignored, and the older profile is used instead.

| Key | Meaning |
|---|---|
| `$Compatibility` | RiptOPL's bit mask, written as a number: `2` synchronous disc reads (`-gc` 2), `4` unhook system calls (3), `16` emulate dual-layer DVD (5), `64` fix game buffer overrun (7). Add them: `6` = 2 + 4. |
| `$NeutrinoFastReads` | `1` adds Neutrino's fast disc reads (`-gc` 0). RiptOPL has no bit for it. |
| `$NeutrinoVideo` | `0` off, `1` 240p/288p (`fp1`), `2` 480p/576p (`fp2`), `3` 1080i x1, `4` 1080i x2, `5` 1080i x3 (same numbers as RiptOPL). |
| `$NeutrinoGsmComp` | `0` to `3`, the field-flipping fix after the video mode (`-gsm=fp2:1`). Used only with a video mode. |
| `$NeutrinoArgs` | Space-separated; `-dbc` (debug colors) and `-logo` are read. Other words are ignored. |
| `$VMC_0`, `$VMC_1` | Name of a virtual memory card, giving Neutrino `-mc0` / `-mc1` = `<device root>VMC/<name>.bin` on the game's own device (the file RiptOPL uses). The file must already exist; if it does not, the launch stops with a message. Not available for SMB games. Empty = none. |

- Lines starting with `#` and unknown keys are ignored. A value that is not valid makes Launchstation ignore the whole file. When the game-device file is present but invalid, Game settings refuses to overwrite it with defaults; fix it as text before saving.
- **Game settings rewrites a valid file** with the keys above; extra lines you added are not kept.
- Where it is saved: always on the game's own device or share, in `CFG/` at its root (`mass:/CFG/`, `smb:/<Share>/CFG/`, and the same for exFAT HDD, MX4SIO, MMCE, iLink, UDPFS and NFS). The `CFG` folder is created if it is missing. **Nothing is ever saved on the memory card**: if the device does not accept the file, Game settings says it could not save, and the settings are not stored anywhere else.
- Where it is looked for, in order: the game's device (`CFG/NEUT-<ID>.cfg`), then the app config folder (`SYS-CONF` on the memory card), then the older profiles (`NEUT-<ID>-A.CNF` and `-B.CNF`, lines starting `NTP1|`). The app config folder and the older profiles are only read, so profiles saved there earlier still work; they are never changed or deleted. Only these per-game files are on the game device; Launchstation's own settings stay in the app config folder.
- Not tested on a console yet: writing to each of these device types.
- Neutrino only runs `-mc0` reliably; `-mc1` is accepted but not emulated by current Neutrino.
