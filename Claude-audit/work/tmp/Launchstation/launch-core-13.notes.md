# launch-core-13 notes

Done: src/dash_smbd.c 1-83, include/dash_sound.h 1-34, src/dash_sound.c 1-525, src/dash_store.c 1-870, src/dash_theme.c 1-148, src/dash_themes.c 1-1308 (all read)
Next: verify candidates, write findings

## Candidates
- src/dash_smbd.c:3,81-83 | #ifdef ETH with #else stub; ETH always defined by ps2.yaml | check other variants (configure_ds34), other dash files
- src/dash_sound.c:12 | include iopcontrol.h possibly unused | grep SifIop in file
- src/dash_sound.c:82-108 | blob_data/blob_size twin switches -> one table | small
- src/dash_sound.c:166 | sound_ready && soundAlive() redundant | trivial
- src/dash_sound.c:138,261,410,508 | boot dir expression repeated 4x | trivial
- src/dash_sound.c:243-248 vs 295-300 | bgm state reset duplicated | small
- src/dash_sound.c:214-217,314-315,326-327 | SFX volume loop 3 copies | small
- src/dash_sound.c:364-369 | fallback loop over 22 voices after failed RPC | overengineering? SDK not in repo -> INFERRED
- include/dash_sound.h | check callers of each prototype (Shutdown, Suspend, ReloadEffects)
- src/dash_store.c:239,247,308-309,324-325,516,524 | dashCfg.enabled / legacy_theme always 1 | grep readers
- src/dash_store.c:248-267 | zero assignments after memset | trivial
- src/dash_store.c:564-567 | buildSettings forwards only, one caller | trivial
- src/dash_store.c:699-702 | dashStorePersisted lead | grep
- src/dash_store.c:683-692,716-720 | return int always 0 | check callers use
- src/dash_store.c:807-833 | ToggleFavorite / TogglePinned identical | small
- src/dash_store.c:740-749 vs 757-759 | hidden lookup loop twice | trivial
- src/dash_store.c:148-175 | dashWriteFile read-back verify | check callers; maybe leave to other lens
- src/dash_store.c:105-146 vs dash_sound.c:407-450 | whole-file read helpers | different padding; check other loaders in repo
- src/dash_themes.c:1122-1159 | dashThemeFolderLoad dead, copy of FolderRead 1046-1079 | grep
- src/dash_themes.c:1161-1195 | dashThemeFolderExport dead, copy of FolderText+FolderWrite 1082-1120 | grep
- src/dash_themes.c:141-144 | dashThemeBgId dead (bg_ids used elsewhere) | grep
- src/dash_themes.c:631-634 | dashThemeMarkCustom dead | grep
- src/dash_themes.c:1200,1281-1292 | MarkDirty dead -> theme_dirty never set -> SavePending no-op | grep SavePending callers
- src/dash_themes.c:1017-1021,1066,1144 | FolderCtx.folder_path never read | 
- src/dash_themes.c:44,904,913 | store_seen written never read | grep
- src/dash_themes.c:402 | P_CRT bold=1 already in base | trivial
- src/dash_themes.c:1108-1110 | mkdir parent then dir; check genMkdir
