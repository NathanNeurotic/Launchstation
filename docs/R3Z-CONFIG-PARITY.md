# Complete upstream configuration mapping

> **Status as of 2026-10-26: current reference (generated mapping).**
> Regenerated/checked by `scripts/audit_r3z_settings.py` against the pinned fixture
> `tests/fixtures/r3z-06b06d0-config-keys.json` (test: `tests/test_r3z_config_inventory.py`). Downstream-only
> keys with Settings controls but outside the 61-key upstream inventory include `EMBER_DIR` and
> `NET_LegacyDrivers`. Upstream moved on after `06b06d0`; re-run with `--upstream <checkout>` before
> claiming parity with newer commits.


Upstream snapshot: `06b06d000072d358c93d98104d64d2f081e47ba7`.

All **61 serialized key families** from upstream config.c and gui_colors.c are mapped below. Indexed launch-key, title and bookmark entries are counted as families. CNF_version is schema metadata.

| CNF key / family | Field | Reachable editor | Runtime consumer |
|---|---|---|---|
| `APP_GameID` | `app_gameid` | `S_GAMEID` | src/main_actions.c |
| `CDROM_Disable_GameID` | `cdrom_disable_gameid` | `S_GAMEID_DISC` | src/main_actions.c; src/neutrino_iso.c; src/popstarter.c |
| `CNF_Path` | `CNF_Path` | `S_CNF_PATH` | src/config.c |
| `Disable_EMBER` | `disable_ember` | `S_DISABLE_EMBER` | src/main_actions.c |
| `Disable_NEUTRINO` | `disable_neutrino` | `S_DISABLE_NEUTRINO` | src/neutrino_iso.c |
| `Disable_POPSTARTER` | `disable_popstarter` | `S_DISABLE_POPSTARTER` | src/popstarter.c |
| `FB_NoIcons` | `FB_NoIcons` | `S_FB_ICONS` | src/filer_browser.c; src/filer_actions.c |
| `FONT_FILE` | `font_file` | `S_FONT_FILE` | src/draw_text.c; src/dash_font.c |
| `GUI_Col_1_ABGR` | `color` | `S_C_COL0 + COLOR_BACKGR` | src/draw.c; classic renderers |
| `GUI_Col_2_ABGR` | `color` | `S_C_COL0 + COLOR_FRAME` | src/draw.c; classic renderers |
| `GUI_Col_3_ABGR` | `color` | `S_C_COL0 + COLOR_SELECT` | src/draw.c; classic renderers |
| `GUI_Col_4_ABGR` | `color` | `S_C_COL0 + COLOR_TEXT` | src/draw.c; classic renderers |
| `GUI_Col_5_ABGR` | `color` | `S_C_COL0 + COLOR_GRAPH1` | src/draw.c; classic renderers |
| `GUI_Col_6_ABGR` | `color` | `S_C_COL0 + COLOR_GRAPH2` | src/draw.c; classic renderers |
| `GUI_Col_7_ABGR` | `color` | `S_C_COL0 + COLOR_GRAPH3` | src/draw.c; classic renderers |
| `GUI_Col_8_ABGR` | `color` | `S_C_COL0 + COLOR_GRAPH4` | src/draw.c; classic renderers |
| `GUI_Swap_Keys` | `swapKeys` | `S_CONFIRM` | src/pad.c; src/pad_ds34.c; src/dash_ui.c |
| `Hide_Hdd` | `Hide_Hdd` | `S_HIDE_HDD` | src/filer.c; src/dash_devices.c |
| `Hide_MCMMCE` | `Hide_MCMMCE` | `S_HIDE_MC` | src/filer.c; src/dash_devices.c |
| `Init_Delay` | `Init_Delay` | `S_INIT_DELAY` | src/main_boot.c; src/main_menu.c |
| `KBDMAP_FILE` | `kbdmap_file` | `S_KBDMAP_FILE` | src/init.c |
| `LANG_FILE` | `lang_file` | `S_LANG_FILE` | src/lang.c |
| `LK_%s_E1` | `LK_Path` | `ST_LKEY` | src/main_boot.c; src/main_menu.c; src/dash_home.c |
| `LK_%s_Title` | `LK_Title` | `ST_LKEY` | src/main_menu.c; src/dash_home.c |
| `LK_auto_Timer` | `timeout` | `S_TIMEOUT` | src/main_menu.c; src/dash_main.c |
| `Menu_Frame` | `Menu_Frame` | `S_MENU_FRAME` | src/draw.c |
| `Menu_Hide_Paths` | `Hide_Paths` | `S_HIDE_PATHS` | src/main_menu.c |
| `Menu_Show_Titles` | `Show_Titles` | `S_SHOW_TITLES` | src/main_menu.c |
| `Misc` | `Misc` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_About_uLE` | `Misc_About_uLE` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_Configure` | `Misc_Configure` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_Debug_Info` | `Misc_Debug_Info` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_Exploit_Installer` | `Misc_Exploit_Installer` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_FileBrowser` | `Misc_FileBrowser` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_HddManager` | `Misc_HddManager` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_OSDSYS` | `Misc_OSDSYS` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_PS2Browser` | `Misc_PS2Browser` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_PS2Disc` | `Misc_PS2Disc` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_PS2Net` | `Misc_PS2Net` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_PS2PowerOff` | `Misc_PS2PowerOff` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_Reboot_IOP` | `Misc_Reboot_IOP` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_ShowFont` | `Misc_ShowFont` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_Show_Build_Info` | `Misc_Show_Build_Info` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `Misc_TextEditor` | `Misc_TextEditor` | `CNF compatibility name` | src/config.c; src/main_actions.c; src/dash_main.c |
| `NET_HOSTwrite` | `HOSTwrite` | `S_HOSTWRITE` | src/init.c |
| `NEUTRINO_ELF` | `neutrino_file` | `S_NEUTRINO_PATH` | src/neutrino_iso.c |
| `POPSTARTER_ELF` | `popstarter_file` | `S_POPS_PATH` | src/popstarter_path.c |
| `PSU_DateNames` | `PSU_DateNames` | `S_PSU_DATE` | src/filer_copy.c |
| `PSU_HugeNames` | `PSU_HugeNames` | `S_PSU_HUGE` | src/filer_copy.c |
| `PSU_NoOverwrite` | `PSU_NoOverwrite` | `S_PSU_NOOVER` | src/filer_copy.c |
| `PathPad[%02d]` | `PathPad` | `F_BOOKMARK` | src/dash_browser.c; src/filer_browser.c |
| `PathPad_Lock` | `PathPad_Lock` | `S_PATHPAD_LOCK` | src/dash_browser.c; src/dash_settings.c |
| `Popup_Opaque` | `Popup_Opaque` | `S_POPUP_OPAQUE` | src/gui_colors.c; classic popup renderers |
| `REBOOT_IOP_ELFLOAD` | `reboot_iop_elf_load` | `S_REBOOT_ELF` | src/main_actions.c; src/popstarter.c |
| `Screen_Offset_X` | `screen_x` | `S_SCREEN_X` | src/draw_gs.c |
| `Screen_Offset_Y` | `screen_y` | `S_SCREEN_Y` | src/draw_gs.c |
| `TV_mode` | `TV_mode` | `S_TVMODE` | src/draw_gs.c |
| `USBKBD_FILE` | `usbkbd_file` | `S_USBKBD_FILE` | src/init.c |
| `USBKBD_USED` | `usbkbd_used` | `S_USBKBD` | src/init.c |
| `VirtualKeyboardLayout` | `virtual_keyboard_layout` | `S_VKEY` | src/dash_widgets.c; src/virtual_keyboard.c |
| `language` | `language` | `S_LANGUAGE` | src/lang.c |

Settings control IDs identify the actual entries in `src/dash_settings.c`. The validation script traverses registered pages from the Settings root, so orphaned handler code does not count as an editor.

Every routine run compares the mapping with an independent checked-in inventory fixture, so dropping a key family fails even without an upstream checkout. Launch-key OK/title dispatch and File Browser bookmark-menu dispatch have separate route checks and executable host tests.

Classic-only frame, opacity, colours, launch labels and file-picker icon settings are now under **Settings > System > Classic screen settings**. They retain their classic consumers and do not change dashboard themes.

Launch keys: Auto is in Automatic start & boot; Circle through START and optional Left/Right are in Launch keys; ESR/OSDSYS paths are in Disc & image loaders. SELECT is reserved for Configure, as in the existing runtime. LK_Flag is derived state, refreshed when a target is chosen/cleared and when CNF loads.

MISC prefix/command names are persisted compatibility strings, not missing upstream GUI options. They remain editable in LAUNCHELF.CNF. Built-in commands themselves remain available through Settings, Tools and Files.

PathPad entries are edited through File Browser > Bookmarks. Theme controls remain in dashboard theme settings.

Historical read aliases (`app_gameid`, `Enable_GameID`, `cdrom_disable_gameid`, `Disable_Disc_GameID`) map to their canonical saved keys. Menu_Pages and Menu_Title are ignored legacy inputs, not supported missing controls.

Downstream-only additions such as EMBER_DIR and NET_LegacyDrivers have existing Settings controls and are outside the pinned upstream key inventory.

Run `python scripts/audit_r3z_settings.py --upstream <pinned-checkout> --write` to regenerate and check the full upstream inventory. Without --upstream it validates the committed mapping against current persistence and page reachability.

This is a source/configuration mapping and automated reachability check, not proof of each device on hardware.
