# launch-core-12 notes (src/dash_settings.c, structure lens)

Done so far: lines 1-4103 (all read, second attempt re-read everything).

Candidates:
- SetIds with no SetItem row and no runAction(const) caller: S_LEGACY_THEME, S_ENABLED, S_FILES_MODE, S_LAUNCHKEYS, S_IGR_AUTO_BACKUP, S_IGR_CLOUD_SYNC, S_SMB_SHARES, S_T_CUSTOMIZE; cases choiceInfo 1018-1023,1071-1075,1169-1176; setChoice 1697-1703,1776-1778,1843-1855; runThemeAction 3092-3094; runAction 3300-3302 | grep ids, check dashCfg fields elsewhere
- S_CLOCK_MANUAL/SOFTWARE/HARDWARE, S_SAVE_CLASSIC, S_REVERT ARE used (runAction from S_CLOCK menu 3569, settingsMenu 3693-3695) -> not dead
- ST_INFO type never used in tables; draw branch 3965-3972, icon 3962 | grep ST_INFO
- choiceInfo `single` out-param always NULL, never read (1004,1007,3812,3997)
- DEVA cases 1130-1149 + setChoice 1738-1743 id->bit mapping twice
- resetScreen 2770-2784 vs resetDisplayThemeDefaults 2791-2804 vs config_screen.c:166-179 | read config_screen.c
- pathApplied 2111-2114 duplicates 2134-2137 (LK_Flag)
- presetPreview 2819-2822 wrapper | check dashModalSetPreview signature
- TargetsJob.finished 2650,2665 set never read? | grep
- value draw dup 3999-4014 vs 4038-4053; display-name expression x4 (3829,4001,4011,4013)
- WLE_NATIVE_CLOUD #else 1164-1167, 1837-1839 | ps2.yaml
- exported dashSettingsOpenLaunchKeys 3596, dashSettingsSaveCnf 2323 | grep callers
